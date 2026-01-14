#include "yamaha_vfd.h"

#include <cctype>
#include <cstdlib>

#include "esphome/core/log.h"
#include "soc/gpio_struct.h"

namespace esphome {
namespace yamaha_vfd {

static const char *const TAG = "yamaha_vfd";

static inline void IRAM_ATTR fixed_nop_delay() {
  __asm__ __volatile__("nop; nop");
}

static inline bool IRAM_ATTR is_start_byte(uint8_t b) {
  switch (b) {
    case 0xFC:
    case 0xC0:
    case 0xF8:
    case 0xE8:
    case 0xF0:
    case 0xE0:
      return true;
    default:
      return false;
  }
}

void YamahaVFD::setup() {
  this->ckfd_pin_->setup();
  this->dtfd_pin_->setup();
  this->cefd_pin_->setup();

  const uint8_t ck_pin = this->ckfd_pin_->get_pin();
  const uint8_t dt_pin = this->dtfd_pin_->get_pin();
  this->ck_mask_ = (1UL << ck_pin);
  this->dt_mask_ = (1UL << dt_pin);

  this->cefd_pin_->attach_interrupt(YamahaVFD::handle_ce_interrupt, this, gpio::INTERRUPT_FALLING_EDGE);
}

void IRAM_ATTR YamahaVFD::handle_ce_interrupt(YamahaVFD *parent) {
  uint8_t b = 0;
  const uint32_t now = millis();

  uint32_t timeout = CK_TIMEOUT;
  while ((GPIO.in & parent->ck_mask_) && --timeout) {
    // wait CK low before sampling a new byte
  }
  if (timeout == 0) {
    parent->synchronized_ = false;
    return;
  }

  for (int i = 0; i < 8; i++) {
    timeout = CK_TIMEOUT;
    while (!(GPIO.in & parent->ck_mask_) && --timeout) {
      // wait CK high
    }
    if (timeout == 0) {
      parent->synchronized_ = false;
      return;
    }

    timeout = CK_TIMEOUT;
    while ((GPIO.in & parent->ck_mask_) && --timeout) {
      // wait CK low
    }
    if (timeout == 0) {
      parent->synchronized_ = false;
      return;
    }

    fixed_nop_delay();
    if (GPIO.in & parent->dt_mask_) {
      b |= (1U << (7 - i));
    }
  }

  if (now - parent->last_byte_time_ > GAP_MS) {
    parent->buffer_index_ = 0;
    parent->synchronized_ = false;
  }

  if (!parent->synchronized_) {
    if (is_start_byte(b)) {
      parent->synchronized_ = true;
      parent->buffer_index_ = 0;
      parent->buffer_[parent->buffer_index_++] = b;
      parent->last_byte_time_ = now;
      parent->has_data_ = true;
    }
    return;
  }

  if (parent->buffer_index_ < BUFFER_SIZE) {
    parent->buffer_[parent->buffer_index_++] = b;
    parent->last_byte_time_ = now;
    parent->has_data_ = true;
  }
}

void YamahaVFD::loop() {
  const uint32_t now = millis();

  if (this->has_data_ && (now - this->last_byte_time_ > GAP_MS)) {
    if (this->buffer_index_ > 0) {
      this->process_frame_();
    }
    this->buffer_index_ = 0;
    this->has_data_ = false;
    this->synchronized_ = false;
  }

  if (this->power_sensor_ != nullptr) {
    const bool amp_on = this->power_sensor_->state;
    if (this->last_amp_state_ && !amp_on) {
      if (this->last_published_mute_) {
        if (this->mute_sensor_) this->mute_sensor_->publish_state(false);
        this->last_published_mute_ = false;
      }
    }
    this->last_amp_state_ = amp_on;
  }

  if (this->has_pending_vol_ &&
      ((now - this->pending_vol_ms_ >= VOL_PUBLISH_DELAY_MS) ||
       (now - this->last_vol_publish_ms_ >= VOL_MAX_HOLD_MS))) {
    if (this->volume_sensor_) this->volume_sensor_->publish_state(this->pending_vol_str_);
    this->last_published_vol_ = this->pending_vol_str_;
    this->last_vol_publish_ms_ = now;

    this->has_last_good_vol_ = true;
    this->last_good_vol_db_ = this->pending_vol_db_;
    this->last_good_vol_ms_ = now;

    if (this->pending_patched_sign_ || this->pending_patched_digit_) {
      ESP_LOGD(TAG, "Volume patched%s%s -> %s",
               this->pending_patched_sign_ ? " sign" : "",
               this->pending_patched_digit_ ? " digit" : "",
               this->pending_vol_str_.c_str());
    }

    if (this->pending_vol_db_ <= -80.0f) {
      if (!this->last_published_mute_) {
        if (this->mute_sensor_) this->mute_sensor_->publish_state(true);
        this->last_published_mute_ = true;
      }
    } else if (this->last_published_mute_) {
      ESP_LOGD(TAG, "Unmute auto par action volume (%s)", this->pending_vol_str_.c_str());
      if (this->mute_sensor_) this->mute_sensor_->publish_state(false);
      this->last_published_mute_ = false;
    }

    this->has_pending_vol_ = false;
  }
}

void YamahaVFD::process_frame_() {
  // 1) Log HEX + construire 2 vues ASCII:
  // - content : seulement imprimables (pour sources/mute comme avant)
  // - lossy   : même longueur que buffer, non-imprimables remplacés par '?'
  std::string hex_str;
  std::string content;
  std::string lossy;

  hex_str.reserve(static_cast<size_t>(this->buffer_index_) * 3);
  content.reserve(static_cast<size_t>(this->buffer_index_));
  lossy.reserve(static_cast<size_t>(this->buffer_index_));

  for (uint16_t i = 0; i < this->buffer_index_; i++) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02X ", this->buffer_[i]);
    hex_str += buf;

    const uint8_t c = this->buffer_[i];
    if (c >= 0x20 && c <= 0x7E) {
      const char ch = static_cast<char>(c);
      content += ch;
      lossy += ch;
    } else {
      lossy += '?';
    }
  }

  ESP_LOGI(TAG, "Trame (%d octets): %s", this->buffer_index_, hex_str.c_str());
  if (content.empty()) return;

  // Helpers volume
  auto clamp_in_range = [&](int v) -> bool { return v >= VOL_MIN_DB && v <= VOL_MAX_DB; };

  auto round_to_int = [](float x) -> int {
    return static_cast<int>(x + (x >= 0 ? 0.5f : -0.5f));
  };

  auto max_delta_for_ms = [](uint32_t dt_ms) -> int {
    // Non-bloquant: plus le temps depuis la dernière valeur fiable est long, plus on autorise un saut.
    // 80ms est un bon compromis empirique; cap à 12 dB.
    int delta = 1 + static_cast<int>(dt_ms / 80);
    if (delta > 12) delta = 12;
    return delta;
  };

  auto parse_volume_tolerant = [&](float &out_db, bool &patched_sign, bool &patched_digit,
                                   bool &weak_anchor) -> bool {
    patched_sign = false;
    patched_digit = false;
    weak_anchor = false;

    // IMPORTANT: utiliser lossy (pas content) pour ne pas "perdre" des digits non-printables
    const std::string &s = lossy;

    // Ancre forte: on préfère un "dB" qui a "UME" proche avant (tolère V/O/L corrompus).
    size_t anchor = std::string::npos;
    size_t db_pos = s.rfind("dB");
    while (db_pos != std::string::npos) {
      size_t ume_pos = s.rfind("UME", db_pos);
      if (ume_pos != std::string::npos && db_pos > ume_pos && (db_pos - ume_pos) <= 32) {
        anchor = db_pos;
        break;
      }
      if (db_pos == 0) break;
      db_pos = s.rfind("dB", db_pos - 1);
    }

    int i = -1;
    if (anchor != std::string::npos) {
      i = static_cast<int>(anchor) - 1;
      weak_anchor = false;
    } else {
      // Fallback numérique strict: derniers digits avec signe proche.
      for (int j = static_cast<int>(s.size()) - 1; j >= 0; j--) {
        if ((s[j] >= '0' && s[j] <= '9') || s[j] == '?') {
          bool has_sign = false;
          for (int k = j - 1; k >= 0 && k >= j - 4; k--) {
            if (s[k] == '-' || s[k] == '+') {
              has_sign = true;
              break;
            }
          }
          if (!has_sign) continue;
          i = j;
          break;
        }
      }
      weak_anchor = true;
    }
    if (i < 0) return false;
    while (i >= 0 && s[i] == ' ') i--;
    if (i < 0) return false;

    auto is_digit_or_q = [](char c) { return (c >= '0' && c <= '9') || c == '?'; };

    // Lire unités puis dizaines (tolère '?')
    char d1 = 0;  // unités
    char d2 = 0;  // dizaines
    int slots = 0;

    if (i >= 0 && is_digit_or_q(s[i])) { d1 = s[i--]; slots++; }
    while (i >= 0 && s[i] == ' ') i--;
    if (i >= 0 && is_digit_or_q(s[i])) { d2 = s[i--]; slots++; }

    if (slots == 0) return false;

    while (i >= 0 && s[i] == ' ') i--;
    char sign = 0;
    if (i >= 0 && (s[i] == '-' || s[i] == '+')) sign = s[i];

    auto digit_val = [](char c) -> int { return (c >= '0' && c <= '9') ? (c - '0') : -1; };

    const int u = digit_val(d1);
    const int t = digit_val(d2);

    struct Cand { int v; bool ps; bool pd; };
    Cand best{0,false,false};
    bool have_best = false;
    int best_score = 1000000000;

    auto consider = [&](int v, bool ps, bool pd, int score) {
      if (!have_best || score < best_score) {
        best = {v, ps, pd};
        best_score = score;
        have_best = true;
      }
    };

    auto eval_mag = [&](int mag, bool pd) {
      // Signe connu
      if (sign == '-' || sign == '+') {
        int v = (sign == '-') ? -mag : mag;
        if (!clamp_in_range(v)) return;

        int score = 0;
        if (this->has_last_good_vol_ && pd) {
          const int last = round_to_int(this->last_good_vol_db_);
          const uint32_t dt = millis() - this->last_good_vol_ms_;
          const int md = max_delta_for_ms(dt);
          const int d = std::abs(v - last);
          if (d > md) return;  // uniquement pour patch; si valeur claire, elle passera via "dB" propre
          score = d;
        }
        consider(v, false, pd, score);
        return;
      }

      // Signe inconnu -> tester -mag et +mag
      for (int si = 0; si < 2; si++) {
        int v = (si == 0) ? -mag : mag;
        if (!clamp_in_range(v)) continue;

        int score = 0;
        if (this->has_last_good_vol_) {
          const int last = round_to_int(this->last_good_vol_db_);
          const uint32_t dt = millis() - this->last_good_vol_ms_;
          const int md = max_delta_for_ms(dt);
          const int d = std::abs(v - last);
          if (d > md) continue;
          score = d + 1;  // pénalité légère: signe patché
        } else {
          // Sans historique, Yamaha est généralement négatif : petit bias
          score = (v < 0) ? 0 : 2;
        }
        consider(v, true, pd, score);
      }
    };

    // Construire la magnitude avec '?' possible
    if (slots == 1) {
      if (u >= 0) {
        eval_mag(u, false);
      } else {
        // unité inconnue -> 0..9
        for (int x = 0; x <= 9; x++) eval_mag(x, true);
        patched_digit = true;
      }
    } else {  // slots == 2
      if (t >= 0 && u >= 0) {
        eval_mag(t * 10 + u, false);
      } else if (t >= 0 && u < 0) {
        for (int x = 0; x <= 9; x++) eval_mag(t * 10 + x, true);
        patched_digit = true;
      } else if (t < 0 && u >= 0) {
        for (int x = 0; x <= 9; x++) eval_mag(x * 10 + u, true);
        patched_digit = true;
      } else {
        // deux digits inconnues -> trop ambigu
        return false;
      }
    }

    if (!have_best) return false;

    out_db = static_cast<float>(best.v);
    patched_sign = best.ps;
    patched_digit = best.pd;
    return true;
  };

  // 2) Volume: parsing tolérant (ne dépend pas de "VOLUME" exact ni de "dB" complet)
  // IMPORTANT: si la valeur est claire et dans la plage, on publie (pas de blocage "delta").
  float vol_db = 0.0f;
  bool patched_sign = false;
  bool patched_digit = false;
  bool weak_anchor = false;

  if (parse_volume_tolerant(vol_db, patched_sign, patched_digit, weak_anchor)) {
    // Publication (inchangé dans l’esprit)
    // Format: "<int> dB" (comme avant)
    const int vol_i = static_cast<int>(vol_db);  // car Yamaha: pas de décimales, step 1 dB
    const std::string final_v = std::to_string(vol_i) + " dB";

    if (this->has_last_good_vol_) {
      const int last_i = round_to_int(this->last_good_vol_db_);
      const uint32_t dt = millis() - this->last_good_vol_ms_;
      const int md = max_delta_for_ms(dt);
      const int d = std::abs(vol_i - last_i);
      if (!weak_anchor && d > (md + 2)) {
        ESP_LOGD(TAG, "Skipping volume outlier (strong anchor): %s", final_v.c_str());
        return;
      }
    }

    if (patched_sign && patched_digit) {
      if (!this->has_last_good_vol_) {
        ESP_LOGD(TAG, "Skipping volume with patched sign+digit (no history): %s", final_v.c_str());
        return;
      }
      const int last_i = round_to_int(this->last_good_vol_db_);
      if ((last_i >= 0 && vol_i < 0) || (last_i < 0 && vol_i >= 0)) {
        ESP_LOGD(TAG, "Skipping volume with patched sign+digit (sign flip): %s", final_v.c_str());
        return;
      }
    }

    // Publication différée: on garde la dernière valeur vue dans la rafale
    this->pending_vol_db_ = vol_db;
    this->pending_vol_str_ = final_v;
    this->pending_patched_sign_ = patched_sign;
    this->pending_patched_digit_ = patched_digit;
    this->pending_vol_ms_ = millis();
    this->has_pending_vol_ = true;
  }

  // 3) MUTE ON/OFF (comme avant, basé sur content)
  if (content.find("MUTE ON") != std::string::npos) {
    if (!this->last_published_mute_) {
      if (this->mute_sensor_) this->mute_sensor_->publish_state(true);
      this->last_published_mute_ = true;
    }
  } else if (content.find("MUTE OFF") != std::string::npos) {
    if (this->last_published_mute_) {
      if (this->mute_sensor_) this->mute_sensor_->publish_state(false);
      this->last_published_mute_ = false;
    }
  }

   // 4. DÉCODAGE DES SOURCES
  std::string s = "";
  if (content.find("DVD") != std::string::npos) s = "INPUT DVD";
  else if (content.find("CD-R") != std::string::npos || content.find("CDR") != std::string::npos) {
    s = "INPUT MD/CDR";
  } else if (content.find("DTV") != std::string::npos || content.find("CBL") != std::string::npos) {
    s = "INPUT DTV/CBL";
  } else if (content.find("V-AUX") != std::string::npos) {
    s = "INPUT V-AUX";
  } else if (content.find("DVR") != std::string::npos) {
    s = "INPUT DVR";
  } else if (content.find("CD") != std::string::npos &&
             content.find("CD-R") == std::string::npos &&
             content.find("CDR") == std::string::npos &&
             content.find("MD") == std::string::npos) {
    s = "INPUT CD";
  }
  if (!s.empty() && s != this->last_published_source_) {
    if (this->source_sensor_) this->source_sensor_->publish_state(s);
    this->last_published_source_ = s;
  }
}

void YamahaVFD::dump_config() {
  ESP_LOGCONFIG(TAG, "Yamaha VFD Sniffer - Gap capture + tolerant volume parser");
}

}  // namespace yamaha_vfd
}  // namespace esphome

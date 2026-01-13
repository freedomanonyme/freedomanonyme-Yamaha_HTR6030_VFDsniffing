#include "yamaha_vfd.h"
#include "soc/gpio_struct.h"
#include "esphome/core/log.h"

namespace esphome {
namespace yamaha_vfd {

static const char *const TAG = "yamaha_vfd";

static inline void IRAM_ATTR fixed_nop_delay() {
    __asm__ __volatile__ ("nop; nop");
}

void YamahaVFD::setup() {
  this->ckfd_pin_->setup();
  this->dtfd_pin_->setup();
  this->cefd_pin_->setup();
  this->ck_mask_ = (1 << this->ckfd_pin_->get_pin());
  this->dt_mask_ = (1 << this->dtfd_pin_->get_pin());
  this->cefd_pin_->attach_interrupt(YamahaVFD::handle_ce_interrupt, this, gpio::INTERRUPT_FALLING_EDGE);
}

void IRAM_ATTR YamahaVFD::handle_ce_interrupt(YamahaVFD *parent) {
  uint8_t b = 0;
  uint32_t now = millis();

  // 1. Lecture de l'octet (Strobe par front descendant)
  for (int i = 0; i < 8; i++) {
    uint32_t timeout = 6000;
    while (!(GPIO.in & parent->ck_mask_) && --timeout);
    timeout = 6000;
    while ((GPIO.in & parent->ck_mask_) && --timeout);
    fixed_nop_delay();
    if (GPIO.in & parent->dt_mask_) b |= (1 << (7 - i));
  }

  // 2. Logique de Gap : si silence > 50ms, nouvelle trame
  if (now - parent->last_byte_time_ > 50) {
    parent->buffer_index_ = 0;
  }

  if (parent->buffer_index_ < 1024) {
    parent->buffer_[parent->buffer_index_++] = b;
    parent->last_byte_time_ = now;
    parent->has_data_ = true; // Indique à la loop qu'une capture est en cours
  }
}

void YamahaVFD::loop() {
  uint32_t now = millis();

  // Si silence > 50ms après le dernier octet reçu, on traite la trame
  if (this->has_data_ && (now - this->last_byte_time_ > 50)) {
    if (this->buffer_index_ > 0) {
        this->process_frame_();
    }
    this->buffer_index_ = 0;
    this->has_data_ = false;
  }

  // Règle Power OFF transition (vfd-4)
  if (this->power_sensor_ != nullptr) {
    bool amp_on = this->power_sensor_->state;
    if (this->last_amp_state_ && !amp_on) {
      if (this->last_published_mute_) {
        if (this->mute_sensor_) this->mute_sensor_->publish_state(false);
        this->last_published_mute_ = false;
      }
    }
    this->last_amp_state_ = amp_on;
  }
}

void YamahaVFD::process_frame_() {
  // 1. Log Hexadécimal complet et ASCII
  std::string hex_str = "";
  std::string content = "";
  for (uint16_t i = 0; i < this->buffer_index_; i++) {
    char buf[4];
    sprintf(buf, "%02X ", this->buffer_[i]);
    hex_str += buf;
    if (this->buffer_[i] >= 0x20 && this->buffer_[i] <= 0x7E) content += (char)this->buffer_[i];
  }
  ESP_LOGI(TAG, "Trame (%d octets): %s", this->buffer_index_, hex_str.c_str());

  if (content.empty()) return;

  // 2. DÉCODAGE VOLUME & RÈGLE UNMUTE SUR ACTION
  size_t db_pos = content.rfind("dB");
  if (db_pos != std::string::npos) {
    std::string vol_str = "";
    for (int i = 1; i <= 8; i++) {
      int pos = (int)db_pos - i;
      if (pos < 0) break;
      if (isdigit(content[pos]) || content[pos] == '-' || content[pos] == '.') vol_str = content[pos] + vol_str;
      else if (!vol_str.empty()) break;
    }

    if (!vol_str.empty()) {
      float vol_val = atof(vol_str.c_str());
      std::string final_v = vol_str + " dB";
      
      if (this->volume_sensor_) this->volume_sensor_->publish_state(final_v);
      this->last_published_vol_ = final_v;

      // Règle MUTE à -80 dB
      if (vol_val <= -80.0) {
        if (!this->last_published_mute_) {
          if (this->mute_sensor_) this->mute_sensor_->publish_state(true);
          this->last_published_mute_ = true;
        }
      } 
      // Règle : Toute action volume (> -80) désactive le Mute
      else if (this->last_published_mute_) {
        ESP_LOGD(TAG, "Unmute auto par action volume (%s)", final_v.c_str());
        if (this->mute_sensor_) this->mute_sensor_->publish_state(false);
        this->last_published_mute_ = false;
      }
    }
  }

  // 3. DÉCODAGE MUTE ON/OFF (Texte explicite)
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
  if (content.find("DVD") != std::string::npos) s = "Lecteur DVD";
  else if (content.find("CD") != std::string::npos) s = "Chromecast Audio";
  else if (content.find("DTV") != std::string::npos || content.find("CBL") != std::string::npos) s = "Télévision";
  else if (content.find("V-AUX") != std::string::npos) s = "Entrée Auxiliaire";

  if (!s.empty() && s != this->last_published_source_) {
    if (this->source_sensor_) this->source_sensor_->publish_state(s);
    this->last_published_source_ = s;
  }
}

void YamahaVFD::dump_config() {
  ESP_LOGCONFIG(TAG, "Yamaha VFD Sniffer - Full Frame Gap Mode");
}

} // namespace yamaha_vfd
} // namespace esphome
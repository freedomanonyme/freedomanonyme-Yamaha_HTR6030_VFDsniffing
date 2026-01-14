#pragma once

#include <cstdint>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace yamaha_vfd {

class YamahaVFD : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_ckfd_pin(InternalGPIOPin *pin) { ckfd_pin_ = pin; }
  void set_dtfd_pin(InternalGPIOPin *pin) { dtfd_pin_ = pin; }
  void set_cefd_pin(InternalGPIOPin *pin) { cefd_pin_ = pin; }

  void set_source_sensor(text_sensor::TextSensor *s) { source_sensor_ = s; }
  void set_volume_sensor(text_sensor::TextSensor *s) { volume_sensor_ = s; }
  void set_mute_sensor(binary_sensor::BinarySensor *s) { mute_sensor_ = s; }
  void set_power_sensor(binary_sensor::BinarySensor *s) { power_sensor_ = s; }

  static void IRAM_ATTR handle_ce_interrupt(YamahaVFD *parent);

 protected:
  // Inchangé (ménage + constantes)
  static constexpr uint16_t BUFFER_SIZE = 1024;
  static constexpr uint32_t GAP_MS = 50;
  static constexpr uint32_t CK_TIMEOUT = 6000;

  // Plage volume Yamaha (validée)
  static constexpr int VOL_MIN_DB = -80;
  static constexpr int VOL_MAX_DB = 16;

  void process_frame_();

  InternalGPIOPin *ckfd_pin_{nullptr};
  InternalGPIOPin *dtfd_pin_{nullptr};
  InternalGPIOPin *cefd_pin_{nullptr};

  text_sensor::TextSensor *source_sensor_{nullptr};
  text_sensor::TextSensor *volume_sensor_{nullptr};
  binary_sensor::BinarySensor *mute_sensor_{nullptr};
  binary_sensor::BinarySensor *power_sensor_{nullptr};

  uint32_t ck_mask_{0};
  uint32_t dt_mask_{0};

  uint8_t buffer_[BUFFER_SIZE]{};
  volatile uint16_t buffer_index_{0};
  volatile uint32_t last_byte_time_{0};
  volatile bool has_data_{false};

  // État capteurs
  std::string last_published_vol_{};
  std::string last_published_source_{};
  bool last_published_mute_{false};
  bool last_amp_state_{true};

  // Historique volume fiable (pour patch signe / digit)
  bool has_last_good_vol_{false};
  float last_good_vol_db_{0.0f};
  uint32_t last_good_vol_ms_{0};
};

}  // namespace yamaha_vfd
}  // namespace esphome
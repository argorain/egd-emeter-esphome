#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

namespace esphome {
namespace dlms_push_meter {

using Obis = std::array<uint8_t, 6>;

// One decoded COSEM "Data" value (A-XDR / DLMS Green Book Data CHOICE).
struct DecodedValue {
  enum class Kind { NONE, UNSIGNED, SIGNED, BOOLEAN, STRING, STRUCT } kind = Kind::NONE;
  uint64_t uval{0};
  int64_t ival{0};
  bool bval{false};
  std::string sval;               // ASCII interpretation of octet/visible/utf8-string
  std::vector<uint8_t> raw;       // raw bytes, used for octet-string (e.g. OBIS code)
  std::vector<DecodedValue> children;  // for STRUCT / ARRAY
};

struct ObisEntry {
  Obis obis;
  DecodedValue value;
};

// A DLMS/COSEM "Data-Notification" push meter (EGD/E.ON HAN interface, DLMS Green Book
// A-XDR encoding, sent unsolicited over RS485 every ~60s, no HDLC/CRC framing).
class DlmsPushMeter : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

#ifdef USE_SENSOR
  void register_sensor(const Obis &obis, sensor::Sensor *s, float scale = 1.0f);
  // Diagnostic: count of OBIS codes seen in the latest telegram that no
  // registered sensor/text_sensor/binary_sensor listens for. Useful for
  // noticing when the meter's firmware starts pushing new fields.
  void set_unknown_obis_count_sensor(sensor::Sensor *s) { this->unknown_obis_count_sensor_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  // format_as_obis: some fields (e.g. a push-setup object's own logical-name
  // attribute) carry a 6-byte OBIS code as their *value*, not free text —
  // render those as "A-B:C.D.E.F" instead of raw/garbled octet-string bytes.
  void register_text_sensor(const Obis &obis, text_sensor::TextSensor *s, bool format_as_obis = false);
  // Diagnostic: human-readable "OBIS=value" list of the unknown codes above.
  void set_unknown_obis_list_text_sensor(text_sensor::TextSensor *s) { this->unknown_obis_list_text_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void register_binary_sensor(const Obis &obis, binary_sensor::BinarySensor *s);
#endif

 protected:
  void try_parse_and_dispatch_();
  void dispatch_(const std::vector<ObisEntry> &entries);
  static bool find_by_obis_(const std::vector<ObisEntry> &entries, const Obis &obis, const DecodedValue **out);
  bool is_registered_(const Obis &obis) const;
  void report_unknown_(const std::vector<ObisEntry> &entries);
  static std::string format_obis_(const Obis &obis);
  static std::string describe_value_(const DecodedValue &v);

  // Recursive-descent A-XDR decoder. `pos` is advanced on success.
  bool decode_value_(const std::vector<uint8_t> &buf, size_t &pos, DecodedValue &out, int depth);

  std::vector<uint8_t> buffer_;
  uint32_t last_byte_time_{0};

#ifdef USE_SENSOR
  struct SensorListener {
    Obis obis;
    sensor::Sensor *sensor;
    float scale;
  };
  std::vector<SensorListener> sensor_listeners_;
  sensor::Sensor *unknown_obis_count_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  struct TextSensorListener {
    Obis obis;
    text_sensor::TextSensor *sensor;
    bool format_as_obis;
  };
  std::vector<TextSensorListener> text_sensor_listeners_;
  text_sensor::TextSensor *unknown_obis_list_text_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  struct BinarySensorListener {
    Obis obis;
    binary_sensor::BinarySensor *sensor;
  };
  std::vector<BinarySensorListener> binary_sensor_listeners_;
#endif
};

}  // namespace dlms_push_meter
}  // namespace esphome

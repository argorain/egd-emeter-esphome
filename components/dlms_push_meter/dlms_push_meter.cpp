#include "dlms_push_meter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace dlms_push_meter {

static const char *const TAG = "dlms_push_meter";

// Data-Notification body arrives as one continuous burst at 9600 Bd with no
// inter-byte gaps; a short silence reliably marks the end of a message since
// the meter only pushes once every ~60s.
static const uint32_t FRAME_GAP_MS = 60;
static const size_t MAX_BUFFER = 2048;
static const int MAX_DEPTH = 8;

// DLMS Green Book "Data" CHOICE tags (A-XDR encoding).
enum DlmsTag : uint8_t {
  TAG_NULL_DATA = 0x00,
  TAG_ARRAY = 0x01,
  TAG_STRUCTURE = 0x02,
  TAG_BOOLEAN = 0x03,
  TAG_BIT_STRING = 0x04,
  TAG_DOUBLE_LONG = 0x05,
  TAG_DOUBLE_LONG_UNSIGNED = 0x06,
  TAG_OCTET_STRING = 0x09,
  TAG_VISIBLE_STRING = 0x0A,
  TAG_UTF8_STRING = 0x0C,
  TAG_BCD = 0x0D,
  TAG_INTEGER = 0x0F,
  TAG_LONG = 0x10,
  TAG_UNSIGNED = 0x11,
  TAG_LONG_UNSIGNED = 0x12,
  TAG_LONG64 = 0x14,
  TAG_LONG64_UNSIGNED = 0x15,
  TAG_ENUM = 0x16,
  TAG_FLOAT32 = 0x17,
  TAG_FLOAT64 = 0x18,
  TAG_DATE_TIME = 0x19,
  TAG_DATE = 0x1A,
  TAG_TIME = 0x1B,
};

static const uint8_t APDU_TAG_DATA_NOTIFICATION = 0x0F;

void DlmsPushMeter::setup() { this->buffer_.reserve(512); }

void DlmsPushMeter::dump_config() { ESP_LOGCONFIG(TAG, "DLMS Push Meter:"); }

void DlmsPushMeter::loop() {
  bool got_byte = false;
  uint8_t byte;
  while (this->available()) {
    if (!this->read_byte(&byte))
      break;
    if (this->buffer_.size() < MAX_BUFFER)
      this->buffer_.push_back(byte);
    got_byte = true;
  }
  if (got_byte)
    this->last_byte_time_ = millis();

  if (this->buffer_.empty())
    return;

  if (millis() - this->last_byte_time_ >= FRAME_GAP_MS) {
    this->try_parse_and_dispatch_();
    this->buffer_.clear();
  } else if (this->buffer_.size() >= MAX_BUFFER) {
    ESP_LOGW(TAG, "Buffer overflow without a frame gap, dropping %zu bytes", this->buffer_.size());
    this->buffer_.clear();
  }
}

bool DlmsPushMeter::decode_value_(const std::vector<uint8_t> &buf, size_t &pos, DecodedValue &out, int depth) {
  if (depth > MAX_DEPTH)
    return false;
  if (pos >= buf.size())
    return false;

  uint8_t tag = buf[pos++];

  auto need = [&](size_t n) -> bool { return pos + n <= buf.size(); };

  switch (tag) {
    case TAG_NULL_DATA: {
      out.kind = DecodedValue::Kind::NONE;
      return true;
    }
    case TAG_ARRAY:
    case TAG_STRUCTURE: {
      if (!need(1))
        return false;
      uint8_t count = buf[pos++];
      out.kind = DecodedValue::Kind::STRUCT;
      out.children.clear();
      out.children.reserve(count);
      for (uint8_t i = 0; i < count; i++) {
        DecodedValue child;
        if (!decode_value_(buf, pos, child, depth + 1))
          return false;
        out.children.push_back(std::move(child));
      }
      return true;
    }
    case TAG_BOOLEAN: {
      if (!need(1))
        return false;
      out.kind = DecodedValue::Kind::BOOLEAN;
      out.bval = buf[pos++] != 0;
      out.uval = out.bval ? 1 : 0;
      return true;
    }
    case TAG_BIT_STRING: {
      if (!need(1))
        return false;
      uint8_t nbits = buf[pos++];
      size_t nbytes = (nbits + 7) / 8;
      if (!need(nbytes))
        return false;
      out.kind = DecodedValue::Kind::STRING;
      out.raw.assign(buf.begin() + pos, buf.begin() + pos + nbytes);
      pos += nbytes;
      return true;
    }
    case TAG_DOUBLE_LONG: {
      if (!need(4))
        return false;
      int32_t v = (int32_t) ((uint32_t(buf[pos]) << 24) | (uint32_t(buf[pos + 1]) << 16) |
                              (uint32_t(buf[pos + 2]) << 8) | uint32_t(buf[pos + 3]));
      pos += 4;
      out.kind = DecodedValue::Kind::SIGNED;
      out.ival = v;
      return true;
    }
    case TAG_DOUBLE_LONG_UNSIGNED: {
      if (!need(4))
        return false;
      uint32_t v = (uint32_t(buf[pos]) << 24) | (uint32_t(buf[pos + 1]) << 16) | (uint32_t(buf[pos + 2]) << 8) |
                   uint32_t(buf[pos + 3]);
      pos += 4;
      out.kind = DecodedValue::Kind::UNSIGNED;
      out.uval = v;
      return true;
    }
    case TAG_OCTET_STRING:
    case TAG_VISIBLE_STRING:
    case TAG_UTF8_STRING:
    case TAG_BCD: {
      if (!need(1))
        return false;
      uint8_t len = buf[pos++];
      if (!need(len))
        return false;
      out.kind = DecodedValue::Kind::STRING;
      out.raw.assign(buf.begin() + pos, buf.begin() + pos + len);
      out.sval.assign(buf.begin() + pos, buf.begin() + pos + len);
      pos += len;
      return true;
    }
    case TAG_INTEGER: {
      if (!need(1))
        return false;
      out.kind = DecodedValue::Kind::SIGNED;
      out.ival = (int8_t) buf[pos++];
      return true;
    }
    case TAG_LONG: {
      if (!need(2))
        return false;
      int16_t v = (int16_t) ((uint16_t(buf[pos]) << 8) | uint16_t(buf[pos + 1]));
      pos += 2;
      out.kind = DecodedValue::Kind::SIGNED;
      out.ival = v;
      return true;
    }
    case TAG_UNSIGNED:
    case TAG_ENUM: {
      if (!need(1))
        return false;
      out.kind = DecodedValue::Kind::UNSIGNED;
      out.uval = buf[pos++];
      return true;
    }
    case TAG_LONG_UNSIGNED: {
      if (!need(2))
        return false;
      uint16_t v = (uint16_t(buf[pos]) << 8) | uint16_t(buf[pos + 1]);
      pos += 2;
      out.kind = DecodedValue::Kind::UNSIGNED;
      out.uval = v;
      return true;
    }
    case TAG_LONG64: {
      if (!need(8))
        return false;
      uint64_t v = 0;
      for (int i = 0; i < 8; i++)
        v = (v << 8) | buf[pos + i];
      pos += 8;
      out.kind = DecodedValue::Kind::SIGNED;
      out.ival = (int64_t) v;
      return true;
    }
    case TAG_LONG64_UNSIGNED: {
      if (!need(8))
        return false;
      uint64_t v = 0;
      for (int i = 0; i < 8; i++)
        v = (v << 8) | buf[pos + i];
      pos += 8;
      out.kind = DecodedValue::Kind::UNSIGNED;
      out.uval = v;
      return true;
    }
    case TAG_FLOAT32: {
      if (!need(4))
        return false;
      uint32_t bits = (uint32_t(buf[pos]) << 24) | (uint32_t(buf[pos + 1]) << 16) | (uint32_t(buf[pos + 2]) << 8) |
                      uint32_t(buf[pos + 3]);
      pos += 4;
      float f;
      memcpy(&f, &bits, sizeof(f));
      out.kind = DecodedValue::Kind::SIGNED;
      out.ival = (int64_t) f;
      out.uval = (uint64_t) f;
      return true;
    }
    case TAG_FLOAT64: {
      if (!need(8))
        return false;
      uint64_t bits = 0;
      for (int i = 0; i < 8; i++)
        bits = (bits << 8) | buf[pos + i];
      pos += 8;
      double d;
      memcpy(&d, &bits, sizeof(d));
      out.kind = DecodedValue::Kind::SIGNED;
      out.ival = (int64_t) d;
      return true;
    }
    case TAG_DATE_TIME:
    case TAG_DATE:
    case TAG_TIME: {
      // Fixed-size per Green Book (date-time=12, date=5, time=4), but treat
      // generically as an octet-string with an implicit length to stay robust.
      size_t len = tag == TAG_DATE_TIME ? 12 : (tag == TAG_DATE ? 5 : 4);
      if (!need(len))
        return false;
      out.kind = DecodedValue::Kind::STRING;
      out.raw.assign(buf.begin() + pos, buf.begin() + pos + len);
      pos += len;
      return true;
    }
    default:
      ESP_LOGW(TAG, "Unknown A-XDR tag 0x%02X at offset %zu", tag, pos - 1);
      return false;
  }
}

bool DlmsPushMeter::find_by_obis_(const std::vector<ObisEntry> &entries, const Obis &obis, const DecodedValue **out) {
  for (const auto &e : entries) {
    if (e.obis == obis) {
      *out = &e.value;
      return true;
    }
  }
  return false;
}

void DlmsPushMeter::try_parse_and_dispatch_() {
  const auto &buf = this->buffer_;

  // Find the start of a Data-Notification APDU; skip any leading noise.
  size_t start = 0;
  while (start < buf.size() && buf[start] != APDU_TAG_DATA_NOTIFICATION)
    start++;
  if (start >= buf.size()) {
    ESP_LOGW(TAG, "No data-notification tag found in %zu bytes, discarding", buf.size());
    return;
  }
  if (start > 0)
    ESP_LOGD(TAG, "Skipped %zu byte(s) of leading noise", start);

  size_t pos = start + 1;
  if (pos + 4 > buf.size()) {
    ESP_LOGW(TAG, "Frame too short for long-invoke-id");
    return;
  }
  pos += 4;  // long-invoke-id-and-priority, not used

  DecodedValue date_time;
  if (!this->decode_value_(buf, pos, date_time, 0)) {
    ESP_LOGW(TAG, "Failed to decode date-time field");
    return;
  }

  // notification-body = structure(2) = { class-id enum, array of items }.
  // Each array item is structure(2) = { attribute-descriptor, value }, but
  // unlike every other value in this APDU, the descriptor is NOT a generic
  // tagged "Data" value: it's a fixed-width raw SEQUENCE (Cosem-Attribute-
  // Descriptor: 2-byte class-id + 6-byte OBIS + 1-byte attribute-id, 9 bytes,
  // no type tags at all). Confirmed against the datasheet's own worked
  // example, byte-for-byte, against two different items. A fully generic
  // recursive decoder can't know this without the context that it's parsing
  // this specific field, hence the manual walk below instead of decode_value_.
  if (pos + 2 > buf.size() || buf[pos] != 0x02 || buf[pos + 1] != 0x02) {
    ESP_LOGW(TAG, "Unexpected notification body shape");
    return;
  }
  pos += 2;

  DecodedValue class_enum;  // unused, part of the fixed APDU shape
  if (!this->decode_value_(buf, pos, class_enum, 0)) {
    ESP_LOGW(TAG, "Failed to decode notification body class");
    return;
  }
  (void) class_enum;

  if (pos + 2 > buf.size() || buf[pos] != 0x01) {
    ESP_LOGW(TAG, "Expected item array in notification body");
    return;
  }
  pos++;
  uint8_t item_count = buf[pos++];

  std::vector<ObisEntry> entries;
  entries.reserve(item_count);
  for (uint8_t idx = 0; idx < item_count; idx++) {
    if (pos + 2 + 9 > buf.size()) {
      ESP_LOGW(TAG, "Item %u truncated", idx);
      return;
    }
    if (buf[pos] != 0x02 || buf[pos + 1] != 0x02) {
      ESP_LOGW(TAG, "Item %u is not a 2-element structure (tag 0x%02X)", idx, buf[pos]);
      return;
    }
    pos += 2;
    pos += 2;  // class-id, unused (we match by OBIS only)
    ObisEntry entry;
    std::copy(buf.begin() + pos, buf.begin() + pos + 6, entry.obis.begin());
    pos += 6;
    pos += 1;  // attribute-id, unused

    if (!this->decode_value_(buf, pos, entry.value, 0)) {
      ESP_LOGW(TAG, "Failed to decode value for item %u (OBIS %s)", idx, format_obis_(entry.obis).c_str());
      return;
    }
    entries.push_back(std::move(entry));
  }

  ESP_LOGD(TAG, "Decoded %u OBIS item(s) from %zu byte frame", item_count, buf.size());
  for (const auto &entry : entries) {
    ESP_LOGV(TAG, "  %s = %s", format_obis_(entry.obis).c_str(), describe_value_(entry.value).c_str());
  }
  this->dispatch_(entries);
}

void DlmsPushMeter::dispatch_(const std::vector<ObisEntry> &entries) {
#ifdef USE_SENSOR
  for (const auto &listener : this->sensor_listeners_) {
    const DecodedValue *v = nullptr;
    if (!find_by_obis_(entries, listener.obis, &v))
      continue;
    float f = 0;
    if (v->kind == DecodedValue::Kind::UNSIGNED)
      f = (float) v->uval;
    else if (v->kind == DecodedValue::Kind::SIGNED)
      f = (float) v->ival;
    else if (v->kind == DecodedValue::Kind::BOOLEAN)
      f = v->bval ? 1.0f : 0.0f;
    else
      continue;
    listener.sensor->publish_state(f * listener.scale);
  }
#endif
#ifdef USE_TEXT_SENSOR
  for (const auto &listener : this->text_sensor_listeners_) {
    const DecodedValue *v = nullptr;
    if (!find_by_obis_(entries, listener.obis, &v))
      continue;
    if (v->kind != DecodedValue::Kind::STRING)
      continue;
    if (listener.format_as_obis && v->raw.size() == 6) {
      Obis obis;
      std::copy(v->raw.begin(), v->raw.end(), obis.begin());
      listener.sensor->publish_state(format_obis_(obis));
    } else {
      listener.sensor->publish_state(v->sval);
    }
  }
#endif
#ifdef USE_BINARY_SENSOR
  for (const auto &listener : this->binary_sensor_listeners_) {
    const DecodedValue *v = nullptr;
    if (!find_by_obis_(entries, listener.obis, &v))
      continue;
    bool state;
    if (v->kind == DecodedValue::Kind::UNSIGNED)
      state = v->uval != 0;
    else if (v->kind == DecodedValue::Kind::BOOLEAN)
      state = v->bval;
    else
      continue;
    listener.sensor->publish_state(state);
  }
#endif
  this->report_unknown_(entries);
}

bool DlmsPushMeter::is_registered_(const Obis &obis) const {
#ifdef USE_SENSOR
  for (const auto &l : this->sensor_listeners_)
    if (l.obis == obis)
      return true;
#endif
#ifdef USE_TEXT_SENSOR
  for (const auto &l : this->text_sensor_listeners_)
    if (l.obis == obis)
      return true;
#endif
#ifdef USE_BINARY_SENSOR
  for (const auto &l : this->binary_sensor_listeners_)
    if (l.obis == obis)
      return true;
#endif
  return false;
}

std::string DlmsPushMeter::format_obis_(const Obis &obis) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%u-%u:%u.%u.%u.%u", obis[0], obis[1], obis[2], obis[3], obis[4], obis[5]);
  return std::string(buf);
}

std::string DlmsPushMeter::describe_value_(const DecodedValue &v) {
  switch (v.kind) {
    case DecodedValue::Kind::UNSIGNED:
      return std::to_string(v.uval);
    case DecodedValue::Kind::SIGNED:
      return std::to_string(v.ival);
    case DecodedValue::Kind::BOOLEAN:
      return v.bval ? "true" : "false";
    case DecodedValue::Kind::STRING: {
      bool printable = !v.raw.empty();
      for (uint8_t b : v.raw)
        if (b < 0x20 || b > 0x7E)
          printable = false;
      if (printable)
        return "\"" + v.sval + "\"";
      std::string hex = "0x";
      char h[3];
      for (uint8_t b : v.raw) {
        snprintf(h, sizeof(h), "%02X", b);
        hex += h;
      }
      return hex.empty() ? "\"\"" : hex;
    }
    case DecodedValue::Kind::STRUCT:
      return "<struct:" + std::to_string(v.children.size()) + ">";
    default:
      return "null";
  }
}

void DlmsPushMeter::report_unknown_(const std::vector<ObisEntry> &entries) {
  std::vector<const ObisEntry *> unknown;
  for (const auto &e : entries)
    if (!this->is_registered_(e.obis))
      unknown.push_back(&e);

#ifdef USE_SENSOR
  if (this->unknown_obis_count_sensor_ != nullptr)
    this->unknown_obis_count_sensor_->publish_state((float) unknown.size());
#endif

  if (unknown.empty()) {
#ifdef USE_TEXT_SENSOR
    if (this->unknown_obis_list_text_sensor_ != nullptr)
      this->unknown_obis_list_text_sensor_->publish_state("");
#endif
    return;
  }

  std::string list;
  for (const auto *e : unknown) {
    std::string entry = format_obis_(e->obis) + "=" + describe_value_(e->value);
    ESP_LOGW(TAG, "Unrecognized OBIS in telegram: %s", entry.c_str());
    if (!list.empty())
      list += "; ";
    if (list.size() + entry.size() > 480) {
      list += "...";
      break;
    }
    list += entry;
  }
#ifdef USE_TEXT_SENSOR
  if (this->unknown_obis_list_text_sensor_ != nullptr)
    this->unknown_obis_list_text_sensor_->publish_state(list);
#endif
}

#ifdef USE_SENSOR
void DlmsPushMeter::register_sensor(const Obis &obis, sensor::Sensor *s, float scale) {
  this->sensor_listeners_.push_back({obis, s, scale});
}
#endif
#ifdef USE_TEXT_SENSOR
void DlmsPushMeter::register_text_sensor(const Obis &obis, text_sensor::TextSensor *s, bool format_as_obis) {
  this->text_sensor_listeners_.push_back({obis, s, format_as_obis});
}
#endif
#ifdef USE_BINARY_SENSOR
void DlmsPushMeter::register_binary_sensor(const Obis &obis, binary_sensor::BinarySensor *s) {
  this->binary_sensor_listeners_.push_back({obis, s});
}
#endif

}  // namespace dlms_push_meter
}  // namespace esphome

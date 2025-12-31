#include "message_encoder.h"
#include "encoder.h"
#include "log.h"
#include <cstdlib>
#include <iostream>
#include <variant>

static inline void appendBytes(std::vector<uint8_t> &out,
                               const std::vector<uint8_t> &bytes) {
  out.insert(out.end(), bytes.begin(), bytes.end());
}

static inline void appendTag(std::vector<uint8_t> &out, uint32_t fieldNumber,
                             WireType wire) {
  uint64_t tag = (uint64_t(fieldNumber) << 3) | uint64_t(wire);
  appendBytes(out, encodeVarint(tag));
}

static inline bool skipUnknown(const std::vector<uint8_t> &data, int &idx,
                               uint32_t wireRaw) {
  const int sz = static_cast<int>(data.size());

  switch (wireRaw) {
  case WireType::VARINT: {
    auto [tmp, next] = decodeVarint(data, idx);
    if (!tmp.has_value())
      return false;
    idx = next;
    return true;
  }
  case WireType::I64: {
    if (idx + 8 > sz)
      return false;
    idx += 8;
    return true;
  }
  case WireType::LEN: {
    auto [lenOpt, afterLen] = decodeVarint(data, idx);
    if (!lenOpt.has_value())
      return false;
    int len = static_cast<int>(lenOpt.value());
    if (afterLen + len > sz)
      return false;
    idx = afterLen + len;
    return true;
  }
  default:
    return false;
  }
}

struct Codec {
  WireType scalarWire; // wire type used for ONE scalar element
  bool packable;       // true for varint/fixed64 types, false for LEN types
  bool (*encodeOne)(const FieldDesc &, const Value &, std::vector<uint8_t> &);
  bool (*decodeOne)(const FieldDesc &, const std::vector<uint8_t> &, int &,
                    Value &);
};

// Int (sint64 zigzag -> VARINT)
static bool encInt(const FieldDesc &fd, const Value &v,
                   std::vector<uint8_t> &out) {
  if (fd.type != FieldType::Int)
    return false;
  if (!std::holds_alternative<int64_t>(v))
    return false;
  appendBytes(out, encodeSignedVarint(std::get<int64_t>(v)));
  return true;
}

static bool decInt(const FieldDesc &fd, const std::vector<uint8_t> &in,
                   int &idx, Value &out) {
  if (fd.type != FieldType::Int)
    return false;
  auto [opt, next] = decodeSignedVarint(in, idx);
  if (!opt.has_value())
    return false;
  out = opt.value();
  idx = next;
  return true;
}

// Double (fixed64 -> I64)
static bool encDouble(const FieldDesc &fd, const Value &v,
                      std::vector<uint8_t> &out) {
  if (fd.type != FieldType::Double)
    return false;
  if (!std::holds_alternative<double>(v))
    return false;
  appendBytes(out, encodeDouble(std::get<double>(v)));
  return true;
}

static bool decDouble(const FieldDesc &fd, const std::vector<uint8_t> &in,
                      int &idx, Value &out) {
  if (fd.type != FieldType::Double)
    return false;
  auto opt = decodeDouble(in, idx);
  if (!opt.has_value())
    return false;
  out = opt.value();
  idx += 8; // decodeDouble returns value but does not advance index
  return true;
}

// String (len-delimited -> LEN)
static bool encString(const FieldDesc &fd, const Value &v,
                      std::vector<uint8_t> &out) {
  if (fd.type != FieldType::String)
    return false;
  if (!std::holds_alternative<std::string>(v))
    return false;
  appendBytes(out, encodeStr(std::get<std::string>(v)));
  return true;
}

static bool decString(const FieldDesc &fd, const std::vector<uint8_t> &in,
                      int &idx, Value &out) {
  if (fd.type != FieldType::String)
    return false;
  auto [opt, next] = decodeStr(in, idx);
  if (!opt.has_value())
    return false;
  out = opt.value();
  idx = next;
  return true;
}

// UInt (uint64 -> VARINT)
static bool encUInt(const FieldDesc &fd, const Value &v,
                    std::vector<uint8_t> &out) {
  if (fd.type != FieldType::UInt)
    return false;
  if (!std::holds_alternative<uint64_t>(v))
    return false;
  appendBytes(out, encodeVarint(std::get<uint64_t>(v)));
  return true;
}

static bool decUInt(const FieldDesc &fd, const std::vector<uint8_t> &in,
                    int &idx, Value &out) {
  if (fd.type != FieldType::UInt)
    return false;
  auto [opt, next] = decodeVarint(in, idx);
  if (!opt.has_value())
    return false;
  out = opt.value();
  idx = next;
  return true;
}

// Bool (bool -> VARINT with 0/1)
static bool encBool(const FieldDesc &fd, const Value &v,
                    std::vector<uint8_t> &out) {
  if (fd.type != FieldType::Bool)
    return false;
  if (!std::holds_alternative<bool>(v))
    return false;
  uint64_t b = std::get<bool>(v) ? 1 : 0;
  appendBytes(out, encodeVarint(b));
  return true;
}

static bool decBool(const FieldDesc &fd, const std::vector<uint8_t> &in,
                    int &idx, Value &out) {
  if (fd.type != FieldType::Bool)
    return false;
  auto [opt, next] = decodeVarint(in, idx);
  if (!opt.has_value())
    return false;
  uint64_t raw = opt.value();
  if (raw != 0 && raw != 1)
    return false;
  out = (raw == 1);
  idx = next;
  return true;
}

static bool encMessage(const FieldDesc &fd, const Value &v,
                       std::vector<uint8_t> &out) {
  if (fd.type != FieldType::Message)
    return false;
  if (!std::holds_alternative<Message>(v))
    return false;
  const Message &m = std::get<Message>(v);
  std::vector<uint8_t> encoded = encodeMessage(m);
  appendBytes(out, encodeVarint(encoded.size()));
  appendBytes(out, encoded);
  return true;
}

static bool decMessage(const FieldDesc &fd, const std::vector<uint8_t> &in,
                       int &idx, Value &out) {
  if (fd.type != FieldType::Message)
    return false;
  auto [lenOpt, afterLen] = decodeVarint(in, idx);
  if (!lenOpt.has_value())
    return false;
  int len = static_cast<int>(lenOpt.value());
  idx = afterLen;
  if (idx + len > static_cast<int>(in.size()))
    return false;
  std::vector<uint8_t> msgBytes(in.begin() + idx, in.begin() + idx + len);
  auto [msgOpt, next] = decodeMessage(msgBytes, fd.nestedDesc);
  if (!msgOpt.has_value())
    return false;
  out = msgOpt.value();
  idx += len;
  return true;
}

static const Codec &codecFor(FieldType t) {
  static const Codec INT{VARINT, true, encInt, decInt};
  static const Codec DBL{I64, true, encDouble, decDouble};
  static const Codec STR{LEN, false, encString, decString};
  static const Codec UINT{VARINT, true, encUInt, decUInt};
  static const Codec BOOL{VARINT, true, encBool, decBool};
  static const Codec MSG{LEN, false, encMessage, decMessage};

  switch (t) {
  case FieldType::Int:
    return INT;
  case FieldType::Double:
    return DBL;
  case FieldType::String:
    return STR;
  case FieldType::UInt:
    return UINT;
  case FieldType::Bool:
    return BOOL;
  case FieldType::Message:
    return MSG;
  default:
    std::abort();
  }
}

std::vector<uint8_t> encodeMessage(const Message &m) {
  std::vector<uint8_t> enc;

  for (const auto &field : m.desc->fields) {
    auto maybeValue = m.get(field.name);
    if (!maybeValue)
      continue;

    const Codec &c = codecFor(field.type);

    if (!field.isRepeated) {
      appendTag(enc, field.number, c.scalarWire);
      if (!c.encodeOne(field, maybeValue->get(), enc))
        std::abort();
      continue;
    }

    const Value &v = maybeValue->get();
    if (!std::holds_alternative<RepeatedVal>(v))
      std::abort();
    const RepeatedVal &rv = std::get<RepeatedVal>(v);

    if (rv.elemType != field.type)
      std::abort();

    if (field.isPacked) {
      if (!c.packable) {
        std::abort();
      }

      appendTag(enc, field.number, LEN);

      std::vector<uint8_t> payload;
      for (const auto &elem : rv.values) {
        if (!c.encodeOne(field, elem, payload))
          std::abort();
      }

      appendBytes(enc, encodeVarint(payload.size()));
      appendBytes(enc, payload);
    } else {
      for (const auto &elem : rv.values) {
        appendTag(enc, field.number, c.scalarWire);
        if (!c.encodeOne(field, elem, enc))
          std::abort();
      }
    }
  }

  return enc;
}

std::pair<std::optional<Message>, int>
decodeMessage(const std::vector<uint8_t> &data,
              std::shared_ptr<const ProtoDesc> desc) {
  int index = 0;
  const int sz = static_cast<int>(data.size());
  Message msg(desc);

  while (index < sz) {
    auto [maybeFieldTag, afterTag] = decodeVarint(data, index);
    if (!maybeFieldTag.has_value()) {
      PB_LOG("No Tag for input field");
      return {std::nullopt, index};
    }

    index = afterTag;
    uint32_t fieldTag = static_cast<uint32_t>(maybeFieldTag.value());
    uint32_t fieldNumber = fieldTag >> 3;
    uint32_t wireRaw = fieldTag & 0x7;
    if (fieldNumber == 0) {
      return {std::nullopt, index};
    }

    // Unknown field: skip it
    auto maybeFieldIndex = desc->indexByNumber(fieldNumber);
    if (!maybeFieldIndex.has_value()) {
      PB_LOG("Field Information not found skipping...");
      int before = index;
      if (!skipUnknown(data, index, wireRaw))
        return {std::nullopt, before};
      continue;
    }

    const FieldDesc &fd = desc->fields[*maybeFieldIndex];
    const Codec &c = codecFor(fd.type);

    if (!fd.isRepeated) {
      if (wireRaw != static_cast<uint32_t>(c.scalarWire)) {
        PB_LOG("Mismatch in wire type");
        return {std::nullopt, index}; // index is start of value (matches tests)
      }

      int valueStart = index;
      Value out;
      if (!c.decodeOne(fd, data, index, out)) {
        PB_LOG("Scalar value incorrectly encoded");
        return {std::nullopt, valueStart};
      }

      if (!msg.set(fd.name, std::move(out)))
        return {std::nullopt, valueStart};

      continue;
    }

    if (fd.isPacked) {
      if (wireRaw != static_cast<uint32_t>(WireType::LEN)) {
        PB_LOG("Mismatch in wire type for packed repeated field");
        return {std::nullopt, index};
      }
      if (!c.packable) {
        PB_LOG("Packed encoding not allowed for this field type");
        return {std::nullopt, index};
      }

      int lengthStart = index;
      auto [lenOpt, afterLen] = decodeVarint(data, index);
      if (!lenOpt.has_value()) {
        PB_LOG("Length not properly encoded for packed repeated field");
        return {std::nullopt, lengthStart};
      }

      int payloadLen = static_cast<int>(lenOpt.value());
      index = afterLen;
      int end = index + payloadLen;
      if (end > sz) {
        PB_LOG("Packed repeated field length exceeds data size");
        return {std::nullopt, index};
      }

      while (index < end) {
        if (fd.type == FieldType::Double) {
          if (index + 8 > end) {
            PB_LOG("Double overruns packed payload");
            return {std::nullopt, index};
          }
        }

        int elemStart = index;
        Value out;
        if (!c.decodeOne(fd, data, index, out)) {
          PB_LOG("Element incorrectly encoded in packed repeated field");
          return {std::nullopt, elemStart};
        }
        if (index > end) {
          PB_LOG("Element overruns packed payload");
          return {std::nullopt, elemStart};
        }

        if (!msg.push(fd.name, std::move(out)))
          return {std::nullopt, elemStart};
      }

      if (index != end) {
        PB_LOG("Packed payload did not end on element boundary");
        return {std::nullopt, index};
      }

    } else {
      if (wireRaw != static_cast<uint32_t>(c.scalarWire)) {
        PB_LOG("Mismatch in wire type for repeated field");
        return {std::nullopt, index};
      }

      int elemStart = index;
      Value out;
      if (!c.decodeOne(fd, data, index, out)) {
        PB_LOG("Element incorrectly encoded in repeated field");
        return {std::nullopt, elemStart};
      }

      if (!msg.push(fd.name, std::move(out)))
        return {std::nullopt, elemStart};
    }
  }

  return {msg, index};
}

#include "message_encoder.h"
#include "encoder.h"
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
  bool (*encodeOne)(const Value &, std::vector<uint8_t> &out);
  bool (*decodeOne)(const std::vector<uint8_t> &in, int &idx, Value &out);
};

// Int (sint64 zigzag -> VARINT)
static bool encInt(const Value &v, std::vector<uint8_t> &out) {
  if (!std::holds_alternative<int64_t>(v))
    return false;
  appendBytes(out, encodeSignedVarint(std::get<int64_t>(v)));
  return true;
}

static bool decInt(const std::vector<uint8_t> &in, int &idx, Value &out) {
  auto [opt, next] = decodeSignedVarint(in, idx);
  if (!opt.has_value())
    return false;
  out = opt.value();
  idx = next;
  return true;
}

// Double (fixed64 -> I64)
static bool encDouble(const Value &v, std::vector<uint8_t> &out) {
  if (!std::holds_alternative<double>(v))
    return false;
  appendBytes(out, encodeDouble(std::get<double>(v)));
  return true;
}

static bool decDouble(const std::vector<uint8_t> &in, int &idx, Value &out) {
  auto opt = decodeDouble(in, idx);
  if (!opt.has_value())
    return false;
  out = opt.value();
  idx += 8; // decodeDouble returns value but does not advance index
  return true;
}

// String (len-delimited -> LEN)
static bool encString(const Value &v, std::vector<uint8_t> &out) {
  if (!std::holds_alternative<std::string>(v))
    return false;
  appendBytes(out, encodeStr(std::get<std::string>(v)));
  return true;
}

static bool decString(const std::vector<uint8_t> &in, int &idx, Value &out) {
  auto [opt, next] = decodeStr(in, idx);
  if (!opt.has_value())
    return false;
  out = opt.value();
  idx = next;
  return true;
}

// UInt (uint64 -> VARINT)
static bool encUInt(const Value &v, std::vector<uint8_t> &out) {
  if (!std::holds_alternative<uint64_t>(v))
    return false;
  appendBytes(out, encodeVarint(std::get<uint64_t>(v)));
  return true;
}

static bool decUInt(const std::vector<uint8_t> &in, int &idx, Value &out) {
  auto [opt, next] = decodeVarint(in, idx);
  if (!opt.has_value())
    return false;
  out = opt.value();
  idx = next;
  return true;
}

// Bool (bool -> VARINT with 0/1)
static bool encBool(const Value &v, std::vector<uint8_t> &out) {
  if (!std::holds_alternative<bool>(v))
    return false;
  uint64_t b = std::get<bool>(v) ? 1 : 0;
  appendBytes(out, encodeVarint(b));
  return true;
}

static bool decBool(const std::vector<uint8_t> &in, int &idx, Value &out) {
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

static const Codec &codecFor(FieldType t) {
  static const Codec INT{VARINT, true, encInt, decInt};
  static const Codec DBL{I64, true, encDouble, decDouble};
  static const Codec STR{LEN, false, encString, decString};
  static const Codec UINT{VARINT, true, encUInt, decUInt};
  static const Codec BOOL{VARINT, true, encBool, decBool};

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
      if (!c.encodeOne(maybeValue->get(), enc))
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
        if (!c.encodeOne(elem, payload))
          std::abort();
      }

      appendBytes(enc, encodeVarint(payload.size()));
      appendBytes(enc, payload);
    } else {
      for (const auto &elem : rv.values) {
        appendTag(enc, field.number, c.scalarWire);
        if (!c.encodeOne(elem, enc))
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
      std::cout << "No Tag for input field\n";
      return {std::nullopt, index};
    }

    index = afterTag;
    uint32_t fieldTag = static_cast<uint32_t>(maybeFieldTag.value());
    uint32_t fieldNumber = fieldTag >> 3;
    uint32_t wireRaw = fieldTag & 0x7;

    // Unknown field: skip it
    auto maybeFieldIndex = desc->indexByNumber(fieldNumber);
    if (!maybeFieldIndex.has_value()) {
      std::cout << "Field Information not found skipping...\n";
      int before = index;
      if (!skipUnknown(data, index, wireRaw))
        return {std::nullopt, before};
      continue;
    }

    const FieldDesc &fd = desc->fields[*maybeFieldIndex];
    const Codec &c = codecFor(fd.type);

    if (!fd.isRepeated) {
      if (wireRaw != static_cast<uint32_t>(c.scalarWire)) {
        std::cout << "Mismatch in wire type\n";
        return {std::nullopt, index}; // index is start of value (matches tests)
      }

      int valueStart = index;
      Value out;
      if (!c.decodeOne(data, index, out)) {
        std::cout << "Scalar value incorrectly encoded\n";
        return {std::nullopt, valueStart};
      }

      if (!msg.set(fd.name, std::move(out)))
        return {std::nullopt, valueStart};

      continue;
    }

    if (fd.isPacked) {
      if (wireRaw != static_cast<uint32_t>(WireType::LEN)) {
        std::cout << "Mismatch in wire type for packed repeated field\n";
        return {std::nullopt, index};
      }
      if (!c.packable) {
        std::cout << "Packed encoding not allowed for this field type\n";
        return {std::nullopt, index};
      }

      int lengthStart = index;
      auto [lenOpt, afterLen] = decodeVarint(data, index);
      if (!lenOpt.has_value()) {
        std::cout << "Length not properly encoded for packed repeated field\n";
        return {std::nullopt, lengthStart};
      }

      int payloadLen = static_cast<int>(lenOpt.value());
      index = afterLen;
      int end = index + payloadLen;
      if (end > sz) {
        std::cout << "Packed repeated field length exceeds data size\n";
        return {std::nullopt, index};
      }

      while (index < end) {
        if (fd.type == FieldType::Double) {
          if (index + 8 > end) {
            std::cout << "Double overruns packed payload\n";
            return {std::nullopt, index};
          }
        }

        int elemStart = index;
        Value out;
        if (!c.decodeOne(data, index, out)) {
          std::cout << "Element incorrectly encoded in packed repeated field\n";
          return {std::nullopt, elemStart};
        }
        if (index > end) {
          std::cout << "Element overruns packed payload\n";
          return {std::nullopt, elemStart};
        }

        if (!msg.push(fd.name, std::move(out)))
          return {std::nullopt, elemStart};
      }

      if (index != end) {
        std::cout << "Packed payload did not end on element boundary\n";
        return {std::nullopt, index};
      }

    } else {
      if (wireRaw != static_cast<uint32_t>(c.scalarWire)) {
        std::cout << "Mismatch in wire type for repeated field\n";
        return {std::nullopt, index};
      }

      int elemStart = index;
      Value out;
      if (!c.decodeOne(data, index, out)) {
        std::cout << "Element incorrectly encoded in repeated field\n";
        return {std::nullopt, elemStart};
      }

      if (!msg.push(fd.name, std::move(out)))
        return {std::nullopt, elemStart};
    }
  }

  return {msg, index};
}

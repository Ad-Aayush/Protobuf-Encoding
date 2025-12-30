#include "message_encoder.h"
#include "encoder.h"
#include <iostream>

std::vector<uint8_t> encodeMessage(const Message &m) {
  std::vector<uint8_t> enc;
  auto desc = m.desc;
  for (auto &field : desc->fields) {
    auto maybeValue = m.get(field.name);
    if (!maybeValue.has_value()) {
      continue; // skip
    }
    switch (field.type) {
    case FieldType::Int: {
      // Tag handling
      uint32_t tag = (field.number << 3) | WireType::VARINT;
      std::vector<uint8_t> tagBytes = encodeVarint(tag);
      enc.insert(enc.end(), tagBytes.begin(), tagBytes.end());
      // Value handling
      const Value &val = maybeValue->get();
      std::int64_t intVal = std::get<std::int64_t>(val);
      std::vector<uint8_t> valueBytes = encodeSignedVarint(intVal);
      enc.insert(enc.end(), valueBytes.begin(), valueBytes.end());
      break;
    }

    case FieldType::Double: {
      // Tag handling
      uint32_t tag = (field.number << 3) | WireType::I64;
      std::vector<uint8_t> tagBytes = encodeVarint(tag);
      enc.insert(enc.end(), tagBytes.begin(), tagBytes.end());
      // Value handling
      const Value &val = maybeValue->get();
      double doubleVal = std::get<double>(val);
      std::vector<uint8_t> valueBytes = encodeDouble(doubleVal);
      enc.insert(enc.end(), valueBytes.begin(), valueBytes.end());
      break;
    }

    case FieldType::String: {
      // Tag handling
      uint32_t tag = (field.number << 3) | WireType::LEN;
      std::vector<uint8_t> tagBytes = encodeVarint(tag);
      enc.insert(enc.end(), tagBytes.begin(), tagBytes.end());
      // Value handling
      const Value &val = maybeValue->get();
      const std::string &strVal = std::get<std::string>(val);
      std::vector<uint8_t> strBytes = encodeStr(strVal);
      enc.insert(enc.end(), strBytes.begin(), strBytes.end());
      break;
    }

    default: {
      std::cerr << "Unsupported field type for encoding\n";
      exit(1);
      break;
    }
    }
  }
  // Add some bytes to the start for the LEN type and the size of the message
  std::vector<uint8_t> lenPrefix = encodeVarint(enc.size());
  enc.insert(enc.begin(), lenPrefix.begin(), lenPrefix.end());

  uint32_t tag = (0 << 3) | WireType::LEN;
  std::vector<uint8_t> tagBytes = encodeVarint(tag);
  enc.insert(enc.begin(), tagBytes.begin(), tagBytes.end());

  return enc;
}

std::pair<std::optional<Message>, int>
decodeMessage(const std::vector<uint8_t> &data,
              std::shared_ptr<const ProtoDesc> desc, int index = 0) {
  int sz = data.size();
  Message msg(desc);
  // Match the tag
  auto [maybeTag, nextIndex] = decodeVarint(data, index);
  if (!maybeTag.has_value()) {
    std::cout << "No tag\n";
    return {std::nullopt, index};
  }

  uint32_t expectedTag = (0 << 3) | WireType::LEN; 
  if (maybeTag.value() != expectedTag) {
    std::cout << "Mismatch in tag\n";
    return {std::nullopt, index};
  }

  index = nextIndex;
  // Decode the length
  auto [maybeLen, afterLenIndex] = decodeVarint(data, index);
  if (!maybeLen.has_value()) {
    std::cout << "Length not specified correctly\n";
    return {std::nullopt, index};
  }
  uint64_t msgLen = maybeLen.value();
  index = afterLenIndex;
  int endIndex = index + msgLen;
  if (endIndex > sz) {
    std::cout << "Message too small\n";
    return {std::nullopt, index};
  }

  while (index < endIndex) {
    auto [maybeFieldTag, afterFieldTagIndex] = decodeVarint(data, index);
    if (!maybeFieldTag.has_value()) {
      std::cout << "No Tag for input field\n";
      return {std::nullopt, index};
    }
    index = afterFieldTagIndex;
    uint32_t fieldTag = maybeFieldTag.value();
    std::cout << "|Field Tag: " << fieldTag << "|\n";
    uint32_t fieldNumber = fieldTag >> 3;
    uint32_t wireType = fieldTag & 0x7;

    auto maybeIndex = desc->indexByNumber(fieldNumber);
    if (!maybeIndex.has_value()) {
      std::cout << "Field Information encoded incorrectly\n";
      return {std::nullopt, index};
    }
    size_t fieldIndex = maybeIndex.value();
    FieldType fieldType = desc->fields[fieldIndex].type;
    std::string fieldName = desc->fields[fieldIndex].name;
    std::cout << "|Field Name: " << fieldName << "|\n";
    std::cout << "|Wire Type: " << wireType << "|\n";
    switch (fieldType) {
    case FieldType::Int: {
      if (wireType != 0) {
        std::cout << "Mismatch in wire type\n";
        return {std::nullopt, index};
      }
      auto [maybeInt, endIntIndex] = decodeSignedVarint(data, index);
      if (!maybeInt.has_value()) {
        std::cout << "Integer incorrectly encoded\n";
        return {std::nullopt, index};
      }
      std::cout << ">|Int: " << maybeInt.value() << "|\n";
      msg.set(fieldName, maybeInt.value());
      index = endIntIndex;
      break;
    }
    case FieldType::Double: {
      if (wireType != 1) {
        std::cout << "Mismatch in wire type\n";
        return {std::nullopt, index};
      }
      auto maybeDouble = decodeDouble(data, index);
      if (!maybeDouble.has_value()) {
        std::cout << "Double incorrectly encoded\n";
        return {std::nullopt, index};
      }
      std::cout << ">|Double: " << maybeDouble.value() << "|\n";
      msg.set(fieldName, maybeDouble.value());
      index += 8;
      break;
    }
    case FieldType::String: {
      if (wireType != 2) {
        std::cout << "Mismatch in wire type\n";
        return {std::nullopt, index};
      }
      auto [maybeStr, endStrIndex] = decodeStr(data, index);
      if (!maybeStr.has_value()) {
        std::cout << "String incorrectly encoded\n";
        return {std::nullopt, index};
      }
      std::cout << ">|Str: " << maybeStr.value() << "|\n";
      msg.set(fieldName, maybeStr.value());
      index = endStrIndex;
      break;
    }

    default:
      std::cerr << "Unsupported field type for decoding\n";
      return {std::nullopt, index};
    }
  }

  return {msg, index};
}

int main() {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"value", 2, FieldType::Double},
      {"name", 3, FieldType::String},
  });

  Message m(desc);
  m.set("id", 1234566);
  m.set("value", 123.45);
  m.set("name", "testing");

  std::vector<uint8_t> enc = encodeMessage(m);

  std::cout << "Encoded message: " << enc << "\n";

  auto [maybeMsg, _] = decodeMessage(enc, desc);
}
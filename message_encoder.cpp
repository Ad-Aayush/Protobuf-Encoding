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

    if (field.isRepeated) {
      const Value &val = maybeValue->get();
      const RepeatedVal &rv = std::get<RepeatedVal>(val);
      // Repeated packed fields are LEN wire type
      if (field.isPacked) {
        // Tag handling
        uint64_t tag =
            static_cast<uint64_t>((field.number << 3) | WireType::LEN);
        std::vector<uint8_t> tagBytes = encodeVarint(tag);
        enc.insert(enc.end(), tagBytes.begin(), tagBytes.end());

        // Prepare packed payload
        std::vector<uint8_t> packedPayload;
        // Each element is encoded according to its type without tags and all
        // elements share the same type
        for (const auto &elem : rv.values) {
          switch (field.type) {
          case FieldType::Int: {
            std::int64_t intVal = std::get<std::int64_t>(elem);
            std::vector<uint8_t> valueBytes = encodeSignedVarint(intVal);
            packedPayload.insert(packedPayload.end(), valueBytes.begin(),
                                 valueBytes.end());
            break;
          }
          case FieldType::Double: {
            double doubleVal = std::get<double>(elem);
            std::vector<uint8_t> valueBytes = encodeDouble(doubleVal);
            packedPayload.insert(packedPayload.end(), valueBytes.begin(),
                                 valueBytes.end());
            break;
          }
          case FieldType::UInt: {
            std::uint64_t uintVal = std::get<std::uint64_t>(elem);
            std::vector<uint8_t> valueBytes = encodeVarint(uintVal);
            packedPayload.insert(packedPayload.end(), valueBytes.begin(),
                                 valueBytes.end());
            break;
          }
          case FieldType::Bool: {
            bool boolVal = std::get<bool>(elem);
            uint64_t boolAsInt = boolVal ? 1 : 0;
            std::vector<uint8_t> valueBytes = encodeVarint(boolAsInt);
            packedPayload.insert(packedPayload.end(), valueBytes.begin(),
                                 valueBytes.end());
            break;
          }
          default: {
            std::cerr << "Unsupported repeated field type for encoding\n";
            exit(1);
            break;
          }
          }
        }
        // Insert packed payload length and payload
        std::vector<uint8_t> lengthBytes = encodeVarint(packedPayload.size());
        enc.insert(enc.end(), lengthBytes.begin(), lengthBytes.end());
        enc.insert(enc.end(), packedPayload.begin(), packedPayload.end());
      } else {
        std::cerr << "Non-packed repeated fields not supported in encoding\n";
        exit(1);
      }
    } else {
      switch (field.type) {
      case FieldType::Int: {
        // Tag handling
        uint64_t tag =
            static_cast<uint64_t>((field.number << 3) | WireType::VARINT);
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
        uint64_t tag =
            static_cast<uint64_t>((field.number << 3) | WireType::I64);
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
        uint64_t tag =
            static_cast<uint64_t>((field.number << 3) | WireType::LEN);
        std::vector<uint8_t> tagBytes = encodeVarint(tag);
        enc.insert(enc.end(), tagBytes.begin(), tagBytes.end());
        // Value handling
        const Value &val = maybeValue->get();
        const std::string &strVal = std::get<std::string>(val);
        std::vector<uint8_t> strBytes = encodeStr(strVal);
        enc.insert(enc.end(), strBytes.begin(), strBytes.end());
        break;
      }

      case FieldType::UInt: {
        // Tag handling
        uint64_t tag =
            static_cast<uint64_t>((field.number << 3) | WireType::VARINT);
        std::vector<uint8_t> tagBytes = encodeVarint(tag);
        enc.insert(enc.end(), tagBytes.begin(), tagBytes.end());
        // Value handling
        const Value &val = maybeValue->get();
        std::uint64_t uintVal = std::get<std::uint64_t>(val);
        std::vector<uint8_t> valueBytes = encodeVarint(uintVal);
        enc.insert(enc.end(), valueBytes.begin(), valueBytes.end());
        break;
      }

      case FieldType::Bool: {
        // Tag handling
        uint64_t tag =
            static_cast<uint64_t>((field.number << 3) | WireType::VARINT);
        std::vector<uint8_t> tagBytes = encodeVarint(tag);
        enc.insert(enc.end(), tagBytes.begin(), tagBytes.end());
        // Value handling
        const Value &val = maybeValue->get();
        bool boolVal = std::get<bool>(val);
        uint64_t boolAsInt = boolVal ? 1 : 0;
        std::vector<uint8_t> valueBytes = encodeVarint(boolAsInt);
        enc.insert(enc.end(), valueBytes.begin(), valueBytes.end());
        break;
      }

      default: {
        std::cerr << "Unsupported field type for encoding\n";
        exit(1);
        break;
      }
      }
    }
  }
  return enc;
}

std::pair<std::optional<Message>, int>
decodeMessage(const std::vector<uint8_t> &data,
              std::shared_ptr<const ProtoDesc> desc) {
  int index = 0;
  int sz = data.size();
  Message msg(desc);

  while (index < sz) {
    auto [maybeFieldTag, afterFieldTagIndex] = decodeVarint(data, index);
    if (!maybeFieldTag.has_value()) {
      std::cout << "No Tag for input field\n";
      return {std::nullopt, index};
    }
    index = afterFieldTagIndex;
    uint32_t fieldTag = maybeFieldTag.value();
    uint32_t fieldNumber = fieldTag >> 3;
    uint32_t wireType = fieldTag & 0x7;

    auto maybeIndex = desc->indexByNumber(fieldNumber);
    if (!maybeIndex.has_value()) {
      std::cout << "Field Information not found skipping...\n";
      switch (wireType) {
      case WireType::VARINT: {
        auto [_tmp, endIndex] = decodeVarint(data, index);
        if (_tmp.has_value() == false) {
          return {std::nullopt, index};
        }
        index = endIndex;
        break;
      }
      case WireType::I64: {
        if (index + 8 > sz) {
          return {std::nullopt, index};
        }
        index += 8; // fixed 8 bytes
        break;
      }

      case WireType::LEN: {
        auto [maybeSize, endStrIndex] = decodeVarint(data, index);
        if (!maybeSize.has_value()) {
          std::cout << "Length not properly encoded\n";
          return {std::nullopt, index};
        }
        int size = static_cast<int>(maybeSize.value());
        if (endStrIndex + size > sz) {
          std::cout << "String length exceeds data size\n";
          return {std::nullopt, index};
        }
        index = endStrIndex + size;
        break;
      }

      default: {
        return {std::nullopt, index};
      }
      }
      continue;
    }
    size_t fieldIndex = maybeIndex.value();
    FieldType fieldType = desc->fields[fieldIndex].type;
    std::string fieldName = desc->fields[fieldIndex].name;
    bool isRepeated = desc->fields[fieldIndex].isRepeated;
    bool isPacked = desc->fields[fieldIndex].isPacked;
    if (isRepeated) {
      if (isPacked) {
        if (wireType != WireType::LEN) {
          std::cout << "Mismatch in wire type for packed repeated field\n";
          return {std::nullopt, index};
        }
        auto [maybeSizeRpt, endSizeIndex] = decodeVarint(data, index);
        if (!maybeSizeRpt.has_value()) {
          std::cout
              << "Length not properly encoded for packed repeated field\n";
          return {std::nullopt, index};
        }
        int sizeRpt = static_cast<int>(maybeSizeRpt.value());
        index = endSizeIndex;
        int endIndex = index + sizeRpt;
        if (endIndex > sz) {
          std::cout << "Packed repeated field length exceeds data size\n";
          return {std::nullopt, index};
        }

        RepeatedVal rv;
        rv.elemType = fieldType;

        while (index < endIndex) {
          switch (fieldType) {
          case FieldType::Int: {
            auto [maybeInt, endIntIndex] = decodeSignedVarint(data, index);
            if (!maybeInt.has_value()) {
              std::cout
                  << "Integer incorrectly encoded in packed repeated field\n";
              return {std::nullopt, index};
            }
            rv.values.push_back(maybeInt.value());
            index = endIntIndex;
            break;
          }
          case FieldType::Double: {
            auto maybeDouble = decodeDouble(data, index);
            if (!maybeDouble.has_value()) {
              std::cout
                  << "Double incorrectly encoded in packed repeated field\n";
              return {std::nullopt, index};
            }
            rv.values.push_back(maybeDouble.value());
            index += 8;
            break;
          }
          case FieldType::UInt: {
            auto [maybeUInt, endUIntIndex] = decodeVarint(data, index);
            if (!maybeUInt.has_value()) {
              std::cout << "Unsigned Integer incorrectly encoded in packed "
                           "repeated field\n";
              return {std::nullopt, index};
            }
            rv.values.push_back(maybeUInt.value());
            index = endUIntIndex;
            break;
          }
          case FieldType::Bool: {
            auto [maybeBoolInt, endBoolIndex] = decodeVarint(data, index);
            if (!maybeBoolInt.has_value()) {
              std::cout
                  << "Boolean incorrectly encoded in packed repeated field\n";
              return {std::nullopt, index};
            }
            if (maybeBoolInt.value() != 0 && maybeBoolInt.value() != 1) {
              std::cout
                  << "Boolean value not 0 or 1 in packed repeated field\n";
              return {std::nullopt, index};
            }
            bool boolVal = maybeBoolInt.value() == 1;
            rv.values.push_back(boolVal);
            index = endBoolIndex;
            break;
          }
          default: {
            std::cerr << "Unsupported repeated field type for decoding\n";
            return {std::nullopt, index};
          }
          }
        }
        msg.set(fieldName, rv);
      } else {
        std::cerr << "Non-packed repeated fields not supported in decoding\n";
        return {std::nullopt, index};
      }
    } else {
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
        // std::cout << ">|Int: " << maybeInt.value() << "|\n";
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
        // std::cout << ">|Double: " << maybeDouble.value() << "|\n";
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
        // std::cout << ">|Str: " << maybeStr.value() << "|\n";
        msg.set(fieldName, maybeStr.value());
        index = endStrIndex;
        break;
      }
      case FieldType::UInt: {
        if (wireType != 0) {
          std::cout << "Mismatch in wire type\n";
          return {std::nullopt, index};
        }
        auto [maybeUInt, endUIntIndex] = decodeVarint(data, index);
        if (!maybeUInt.has_value()) {
          std::cout << "Unsigned Integer incorrectly encoded\n";
          return {std::nullopt, index};
        }
        // std::cout << ">|UInt: " << maybeUInt.value() << "|\n";
        msg.set(fieldName, maybeUInt.value());
        index = endUIntIndex;
        break;
      }
      case FieldType::Bool: {
        if (wireType != 0) {
          std::cout << "Mismatch in wire type\n";
          return {std::nullopt, index};
        }
        auto [maybeBoolInt, endBoolIndex] = decodeVarint(data, index);
        if (!maybeBoolInt.has_value()) {
          std::cout << "Boolean incorrectly encoded\n";
          return {std::nullopt, index};
        }

        if (maybeBoolInt.value() != 0 && maybeBoolInt.value() != 1) {
          std::cout << "Boolean value not 0 or 1\n";
          return {std::nullopt, index};
        }

        bool boolVal = maybeBoolInt.value() == 1;
        // std::cout << ">|Bool: " << boolVal << "|\n";
        msg.set(fieldName, boolVal);
        index = endBoolIndex;
        break;
      }

      default:
        std::cerr << "Unsupported field type for decoding\n";
        return {std::nullopt, index};
      }
    }
  }

  return {msg, index};
}

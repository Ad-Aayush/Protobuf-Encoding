#include "proto_desc.h"
#include "log.h"
#include <stdexcept>

ProtoDesc::ProtoDesc(std::vector<FieldDesc> flds) : fields(std::move(flds)) {
  nameToIndex.reserve(fields.size());
  numberToIndex.reserve(fields.size());

  for (size_t i = 0; i < fields.size(); ++i) {
    const auto &fd = fields[i];
    if (fd.number == 0)
      throw std::runtime_error("field number cannot be 0");
    if (!nameToIndex.emplace(fd.name, i).second)
      throw std::runtime_error("duplicate field name: " + fd.name);
    if (!numberToIndex.emplace(fd.number, i).second)
      throw std::runtime_error("duplicate field number: " +
                               std::to_string(fd.number));
  }
}

const FieldDesc *ProtoDesc::findByName(const std::string &name) const {
  auto it = nameToIndex.find(name);
  if (it == nameToIndex.end())
    return nullptr;
  return &fields[it->second];
}

std::optional<size_t> ProtoDesc::indexByName(const std::string &name) const {
  auto it = nameToIndex.find(name);
  if (it == nameToIndex.end())
    return std::nullopt;
  return it->second;
}

std::optional<size_t> ProtoDesc::indexByNumber(uint32_t number) const {
  auto it = numberToIndex.find(number);
  if (it == numberToIndex.end()) {
    PB_LOG("NUM: " << number);
    return std::nullopt;
  }
  return it->second;
}

Message::Message(std::shared_ptr<const ProtoDesc> d)
    : desc(std::move(d)), vals(desc->fields.size()) {}

static bool valueMatchesFieldType(FieldType type, const Value &v) {
  switch (type) {
  case FieldType::Int:
    return std::holds_alternative<std::int64_t>(v);
  case FieldType::Double:
    return std::holds_alternative<double>(v);
  case FieldType::String:
    return std::holds_alternative<std::string>(v);
  case FieldType::UInt:
    return std::holds_alternative<std::uint64_t>(v);
  case FieldType::Bool:
    return std::holds_alternative<bool>(v);
  case FieldType::Message:
    return std::holds_alternative<Message>(v);
  case FieldType::Float:
    return std::holds_alternative<float>(v);
  case FieldType::Bytes:
    return std::holds_alternative<std::vector<uint8_t>>(v);
  default:
    return false;
  }
}

std::optional<std::reference_wrapper<const Value>>
Message::get(const std::string &fieldName) const {
  std::optional<size_t> maybeIdx = desc->indexByName(fieldName);
  if (!maybeIdx.has_value())
    return std::nullopt;
  size_t idx = *maybeIdx;
  if (!vals[idx].has_value())
    return std::nullopt;
  return std::cref(*vals[idx]);
}

std::optional<std::reference_wrapper<const Value>>
Message::getByIndex(const std::string &fieldName, size_t idx) const {
  auto maybeIdx = desc->indexByName(fieldName);
  if (!maybeIdx.has_value()) {
    PB_LOG("Field name not found: " << fieldName);
    return std::nullopt;
  }
  size_t fieldIdx = *maybeIdx;
  const FieldDesc &fd = desc->fields[fieldIdx];
  if (!fd.isRepeated) {
    PB_LOG("Field is not repeated: " << fieldName);
    return std::nullopt;
  }
  if (!vals[fieldIdx].has_value()) {
    PB_LOG("No value set for field: " << fieldName);
    return std::nullopt;
  }
  const Value &v = *vals[fieldIdx];
  if (!std::holds_alternative<RepeatedVal>(v)) {
    PB_LOG("Value is not repeated for field: " << fieldName);
    return std::nullopt;
  }
  const RepeatedVal &rv = std::get<RepeatedVal>(v);
  if (idx >= rv.values.size()) {
    PB_LOG("Index out of bounds for field: " << fieldName);
    return std::nullopt;
  }
  return std::cref(rv.values[idx]);
}

bool Message::set(const std::string &fieldName, Value v) {
  std::optional<size_t> maybeIdx = desc->indexByName(fieldName);
  if (!maybeIdx.has_value()) {
    return false;
  }
  size_t idx = *maybeIdx;
  const FieldDesc &fd = desc->fields[idx];
  if (fd.isRepeated) {
    // Expecting a RepeatedVal
    if (!std::holds_alternative<RepeatedVal>(v))
      return false;
    const RepeatedVal &rv = std::get<RepeatedVal>(v);
    if (rv.elemType != fd.type)
      return false;
  } else {
    // Type check
    switch (fd.type) {
    case FieldType::Int:
      if (!std::holds_alternative<std::int64_t>(v))
        return false;
      break;
    case FieldType::Double:
      if (!std::holds_alternative<double>(v))
        return false;
      break;
    case FieldType::String:
      if (!std::holds_alternative<std::string>(v))
        return false;
      break;
    case FieldType::UInt:
      if (!std::holds_alternative<std::uint64_t>(v))
        return false;
      break;
    case FieldType::Bool:
      if (!std::holds_alternative<bool>(v))
        return false;
      break;
    case FieldType::Message:
      if (!std::holds_alternative<Message>(v))
        return false;
      break;
    case FieldType::Float:
      if (!std::holds_alternative<float>(v))
        return false;
      break;
    case FieldType::Bytes:
      if (!std::holds_alternative<std::vector<uint8_t>>(v))
        return false;
      break;
    default:
      return false;
    }
  }
  vals[idx] = std::move(v);
  return true;
}

bool Message::setByIndex(const std::string &fieldName, size_t idx, Value v) {
  auto maybeIdx = desc->indexByName(fieldName);
  if (!maybeIdx.has_value()) {
    PB_LOG("Field name not found: " << fieldName);
    return false;
  }
  size_t fieldIdx = *maybeIdx;
  const FieldDesc &fd = desc->fields[fieldIdx];
  if (!fd.isRepeated) {
    PB_LOG("Field is not repeated: " << fieldName);
    return false;
  }
  if (!vals[fieldIdx].has_value()) {
    PB_LOG("No value set for field: " << fieldName);
    return false;
  }
  Value &fieldValue = *vals[fieldIdx];
  if (!std::holds_alternative<RepeatedVal>(fieldValue)) {
    PB_LOG("Value is not repeated for field: " << fieldName);
    return false;
  }
  RepeatedVal &rv = std::get<RepeatedVal>(fieldValue);
  if (idx >= rv.values.size()) {
    PB_LOG("Index out of bounds for field: " << fieldName);
    return false;
  }
  if (rv.elemType != fd.type) {
    PB_LOG("Element type mismatch for field: " << fieldName);
    return false;
  }
  if (!valueMatchesFieldType(fd.type, v)) {
    PB_LOG("Element value type mismatch for field: " << fieldName);
    return false;
  }
  rv.values[idx] = std::move(v);
  return true;
}

bool Message::push(const std::string &fieldName, Value v) {
  auto maybeIdx = desc->indexByName(fieldName);
  if (!maybeIdx.has_value()) {
    PB_LOG("Field name not found: " << fieldName);
    return false;
  }
  size_t fieldIdx = *maybeIdx;
  const FieldDesc &fd = desc->fields[fieldIdx];
  if (!fd.isRepeated) {
    PB_LOG("Field is not repeated: " << fieldName);
    return false;
  }
  if (!vals[fieldIdx].has_value()) {
    if (!valueMatchesFieldType(fd.type, v)) {
      PB_LOG("Element value type mismatch for field: " << fieldName);
      return false;
    }
    // Initialize RepeatedVal if not present
    RepeatedVal rv;
    rv.elemType = fd.type;
    vals[fieldIdx] = rv;
  }
  Value &fieldValue = *vals[fieldIdx];
  if (!std::holds_alternative<RepeatedVal>(fieldValue)) {
    PB_LOG("Value is not repeated for field: " << fieldName);
    return false;
  }
  RepeatedVal &rv = std::get<RepeatedVal>(fieldValue);
  if (rv.elemType != fd.type) {
    PB_LOG("Element type mismatch for field: " << fieldName);
    return false;
  }
  if (!valueMatchesFieldType(fd.type, v)) {
    PB_LOG("Element value type mismatch for field: " << fieldName);
    return false;
  }
  rv.values.push_back(std::move(v));
  return true;
}

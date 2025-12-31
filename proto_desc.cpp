#include "proto_desc.h"
#include <iostream>
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
    std::cout << "NUM: " << number << "\n";
    return std::nullopt;
  }
  return it->second;
}

Message::Message(std::shared_ptr<const ProtoDesc> d)
    : desc(std::move(d)), vals(desc->fields.size()) {}

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
    std::cerr << "Field name not found: " << fieldName << "\n";
    return std::nullopt;
  }
  size_t fieldIdx = *maybeIdx;
  const FieldDesc &fd = desc->fields[fieldIdx];
  if (!fd.isRepeated) {
    std::cerr << "Field is not repeated: " << fieldName << "\n";
    return std::nullopt;
  }
  if (!vals[fieldIdx].has_value()) {
    std::cerr << "No value set for field: " << fieldName << "\n";
    return std::nullopt;
  }
  const Value &v = *vals[fieldIdx];
  if (!std::holds_alternative<RepeatedVal>(v)) {
    std::cerr << "Value is not repeated for field: " << fieldName << "\n";
    return std::nullopt;
  }
  const RepeatedVal &rv = std::get<RepeatedVal>(v);
  if (idx >= rv.values.size()) {
    std::cerr << "Index out of bounds for field: " << fieldName << "\n";
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
    std::cerr << "Field name not found: " << fieldName << "\n";
    return false;
  }
  size_t fieldIdx = *maybeIdx;
  const FieldDesc &fd = desc->fields[fieldIdx];
  if (!fd.isRepeated) {
    std::cerr << "Field is not repeated: " << fieldName << "\n";
    return false;
  }
  if (!vals[fieldIdx].has_value()) {
    std::cerr << "No value set for field: " << fieldName << "\n";
    return false;
  }
  Value &fieldValue = *vals[fieldIdx];
  if (!std::holds_alternative<RepeatedVal>(fieldValue)) {
    std::cerr << "Value is not repeated for field: " << fieldName << "\n";
    return false;
  }
  RepeatedVal &rv = std::get<RepeatedVal>(fieldValue);
  if (idx >= rv.values.size()) {
    std::cerr << "Index out of bounds for field: " << fieldName << "\n";
    return false;
  }
  if (rv.elemType != fd.type) {
    std::cerr << "Element type mismatch for field: " << fieldName << "\n";
    return false;
  }
  rv.values[idx] = std::move(v);
  return true;
}

bool Message::push(const std::string &fieldName, Value v) {
  auto maybeIdx = desc->indexByName(fieldName);
  if (!maybeIdx.has_value()) {
    std::cerr << "Field name not found: " << fieldName << "\n";
    return false;
  }
  size_t fieldIdx = *maybeIdx;
  const FieldDesc &fd = desc->fields[fieldIdx];
  if (!fd.isRepeated) {
    std::cerr << "Field is not repeated: " << fieldName << "\n";
    return false;
  }
  if (!vals[fieldIdx].has_value()) {
    // Initialize RepeatedVal if not present
    RepeatedVal rv;
    rv.elemType = fd.type;
    vals[fieldIdx] = rv;
  }
  Value &fieldValue = *vals[fieldIdx];
  if (!std::holds_alternative<RepeatedVal>(fieldValue)) {
    std::cerr << "Value is not repeated for field: " << fieldName << "\n";
    return false;
  }
  RepeatedVal &rv = std::get<RepeatedVal>(fieldValue);
  if (rv.elemType != fd.type) {
    std::cerr << "Element type mismatch for field: " << fieldName << "\n";
    return false;
  }
  rv.values.push_back(std::move(v));
  return true;
}
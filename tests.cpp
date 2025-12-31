#include "encoder.h"
#include "message_encoder.h"
#include "proto_desc.h"
#include <cstring>
#include <gtest/gtest.h>

TEST(Varint, RoundTripKeyValues) {
  std::vector<uint64_t> vals = {
      0ULL,         1ULL,
      2ULL,         10ULL,
      127ULL,       128ULL,
      129ULL,       150ULL,
      16383ULL,     16384ULL,
      (1ULL << 31), (1ULL << 32),
      (1ULL << 63), std::numeric_limits<uint64_t>::max()};

  for (auto v : vals) {
    auto enc = encodeVarint(v);
    auto [dec, next] = decodeVarint(enc, 0);
    ASSERT_TRUE(dec.has_value()) << "Failed to decode v=" << v;
    EXPECT_EQ(*dec, v);
    EXPECT_EQ(next, static_cast<int>(enc.size()));
  }
}

TEST(Varint, RejectTruncatedContinuation) {
  // 0x80 means "continuation follows" but we end immediately -> invalid.
  std::vector<uint8_t> bad = {0x80};
  auto [dec, next] = decodeVarint(bad, 0);
  EXPECT_FALSE(dec.has_value());
  EXPECT_EQ(next, 0);
}

TEST(Varint, ZeroIsSingleByte) {
  auto enc = encodeVarint(0);
  ASSERT_EQ(enc.size(), 1u);
  EXPECT_EQ(enc[0], 0x00);
}

TEST(Varint, RejectTooLongVarint) {
  // 11 bytes is invalid for uint64 varint (max 10)
  std::vector<uint8_t> bad(11, 0x80);
  bad.back() = 0x00;
  auto [dec, next] = decodeVarint(bad, 0);
  EXPECT_FALSE(dec.has_value());
  EXPECT_EQ(next, 0);
}

TEST(Varint, RejectTooLargeTenthByte) {
  // 10th byte must only carry 1 payload bit for uint64.
  std::vector<uint8_t> bad(10, 0x80);
  bad.back() = 0x7F;
  auto [dec, next] = decodeVarint(bad, 0);
  EXPECT_FALSE(dec.has_value());
  EXPECT_EQ(next, 0);
}

TEST(Varint, DecodesFromOffsetAndAdvances) {
  std::vector<uint8_t> buf;
  auto a = encodeVarint(150);
  auto b = encodeVarint(300);

  buf.insert(buf.end(), a.begin(), a.end());
  buf.insert(buf.end(), b.begin(), b.end());

  auto [da, i] = decodeVarint(buf, 0);
  ASSERT_TRUE(da.has_value());
  EXPECT_EQ(*da, static_cast<uint64_t>(150));

  auto [db, j] = decodeVarint(buf, i);
  ASSERT_TRUE(db.has_value());
  EXPECT_EQ(*db, static_cast<uint64_t>(300));
  EXPECT_EQ(j, (int)buf.size());
}

TEST(SignedVarint, RoundTripKeyValues) {
  std::vector<int64_t> vals = {
      0LL,          1LL,         -1LL,         10LL,      -10LL,    127LL,
      -127LL,       128LL,       -128LL,       129LL,     -129LL,   150LL,
      -150LL,       16383LL,     -16383LL,     16384LL,   -16384LL, (1LL << 31),
      -(1LL << 31), (1LL << 32), -(1LL << 32), INT64_MAX, INT64_MIN};

  for (auto v : vals) {
    auto enc = encodeSignedVarint(v);
    auto [dec, next] = decodeSignedVarint(enc, 0);
    ASSERT_TRUE(dec.has_value()) << "Failed to decode v=" << v;
    EXPECT_EQ(*dec, v);
    EXPECT_EQ(next, static_cast<int>(enc.size()));
  }
}

TEST(Fixed64, RoundTrip) {
  std::vector<uint64_t> vals = {0ULL, 1ULL, 0x1122334455667788ULL,
                                std::numeric_limits<uint64_t>::max()};

  for (auto v : vals) {
    auto enc = encodeFixed64(v);
    ASSERT_EQ(enc.size(), 8u);
    auto dec = decodeFixed64(enc, 0);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(*dec, v);
  }
}

TEST(Double, RoundTripCommon) {
  std::vector<double> vals = {0.0, -0.0, 1.0, -1.0, 25.4, 164.25, 1e-9, 1e9};

  for (double v : vals) {
    auto enc = encodeDouble(v);
    ASSERT_EQ(enc.size(), 8u);
    auto dec = decodeDouble(enc, 0);
    ASSERT_TRUE(dec.has_value());
    // Exact bitwise roundtrip should hold because we memcpy bits.
    EXPECT_EQ(std::memcmp(&v, &(*dec), sizeof(double)), 0);
  }
}

TEST(Double, RoundTripSpecial) {
  std::vector<double> vals = {std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()};

  for (double v : vals) {
    auto enc = encodeDouble(v);
    auto dec = decodeDouble(enc, 0);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(std::memcmp(&v, &(*dec), sizeof(double)), 0);
  }
}

TEST(String, RoundTrip) {
  std::vector<std::string> vals = {"", "a", "testing", std::string(200, 'x')};

  for (auto &s : vals) {
    auto enc = encodeStr(s);
    auto [dec, next] = decodeStr(enc, 0);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(*dec, s);
    EXPECT_EQ(next, static_cast<int>(enc.size()));
  }
}

TEST(String, RejectTruncatedPayload) {
  auto enc = encodeStr("abc");
  enc.pop_back(); // remove one byte
  auto [dec, next] = decodeStr(enc, 0);
  EXPECT_FALSE(dec.has_value());
  EXPECT_EQ(next, 0);
}

TEST(String, EmptyIsJustLengthZero) {
  auto enc = encodeStr("");
  ASSERT_GE(enc.size(), 1u);
  EXPECT_EQ(enc[0], 0x00);
  auto [dec, next] = decodeStr(enc, 0);
  ASSERT_TRUE(dec.has_value());
  EXPECT_EQ(*dec, "");
  EXPECT_EQ(next, (int)enc.size());
}

TEST(ProtoDesc, RejectDuplicateName) {
  std::vector<FieldDesc> flds = {
      {"a", 1, FieldType::Int},
      {"a", 2, FieldType::Double},
  };
  EXPECT_THROW(ProtoDesc desc(flds), std::runtime_error);
}

TEST(ProtoDesc, RejectDuplicateNumber) {
  std::vector<FieldDesc> flds = {
      {"a", 1, FieldType::Int},
      {"b", 1, FieldType::Double},
  };
  EXPECT_THROW(ProtoDesc desc(flds), std::runtime_error);
}

TEST(ProtoDesc, RejectZeroFieldNumber) {
  std::vector<FieldDesc> flds = {
      {"a", 0, FieldType::Int},
  };
  EXPECT_THROW(ProtoDesc desc(flds), std::runtime_error);
}

TEST(Message, SetGetHappyPath) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"value", 2, FieldType::Double},
      {"name", 3, FieldType::String},
      {"count", 4, FieldType::UInt},
      {"active", 5, FieldType::Bool},
      {"tags", 6, FieldType::UInt, /*repeated=*/true},
  });
  Message m(desc);

  EXPECT_TRUE(m.set("id", std::int64_t(42)));
  EXPECT_TRUE(m.set("value", 3.14));
  EXPECT_TRUE(m.set("name", std::string("x")));
  EXPECT_TRUE(m.set("count", std::uint64_t(100)));
  EXPECT_TRUE(m.set("active", true));
  EXPECT_TRUE(m.push("tags", std::uint64_t(1)));
  EXPECT_TRUE(m.push("tags", std::uint64_t(2)));

  auto id = m.get("id");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(std::get<std::int64_t>(id->get()), 42);

  auto value = m.get("value");
  ASSERT_TRUE(value.has_value());
  EXPECT_DOUBLE_EQ(std::get<double>(value->get()), 3.14);

  auto name = m.get("name");
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(std::get<std::string>(name->get()), "x");

  auto count = m.get("count");
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(std::get<std::uint64_t>(count->get()), uint64_t(100));

  auto active = m.get("active");
  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(std::get<bool>(active->get()), true);

  auto tag_1 = m.getByIndex("tags", 0);
  ASSERT_TRUE(tag_1.has_value());
  EXPECT_EQ(std::get<std::uint64_t>(tag_1->get()), uint64_t(1));

  auto tag_2 = m.getByIndex("tags", 1);
  ASSERT_TRUE(tag_2.has_value());
  EXPECT_EQ(std::get<std::uint64_t>(tag_2->get()), uint64_t(2));

  auto tag_3 = m.getByIndex("tags", 2);
  EXPECT_FALSE(tag_3.has_value());

  m.setByIndex("tags", 1, std::uint64_t(42));
  auto tag_2_updated = m.getByIndex("tags", 1);
  ASSERT_TRUE(tag_2_updated.has_value());
  EXPECT_EQ(std::get<std::uint64_t>(tag_2_updated->get()), uint64_t(42));
}

TEST(Message, GetUnsetReturnsNullopt) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"name", 2, FieldType::String},
  });
  Message m(desc);

  EXPECT_FALSE(m.get("id").has_value());   // known, but unset
  EXPECT_FALSE(m.get("name").has_value()); // known, but unset
}

TEST(Message, UnknownFieldNameFailsGracefully) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
  });
  Message m(desc);

  EXPECT_FALSE(m.set("does_not_exist", std::int64_t(1)));
  EXPECT_FALSE(m.get("does_not_exist").has_value());
}

TEST(Message, TypeMismatchRejected) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"value", 2, FieldType::Double},
      {"name", 3, FieldType::String},
  });
  Message m(desc);

  EXPECT_FALSE(m.set("id", 3.14));                // double into int
  EXPECT_FALSE(m.set("value", std::int64_t(10))); // int into double
  EXPECT_FALSE(m.set("name", std::int64_t(7)));   // int into string
  EXPECT_FALSE(m.set("name", 2.71));              // double into string
}

TEST(Message, RepeatedPushTypeMismatchRejected) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"tags", 1, FieldType::Int, /*repeated=*/true},
  });
  Message m(desc);

  EXPECT_FALSE(m.push("tags", std::string("nope")));
  EXPECT_FALSE(m.push("tags", 3.14));
  EXPECT_FALSE(m.get("tags").has_value());
}

TEST(Message, SetByIndexTypeMismatchRejected) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"tags", 1, FieldType::Int, /*repeated=*/true},
  });
  Message m(desc);

  ASSERT_TRUE(m.push("tags", std::int64_t(5)));
  EXPECT_FALSE(m.setByIndex("tags", 0, std::string("bad")));

  auto tag = m.getByIndex("tags", 0);
  ASSERT_TRUE(tag.has_value());
  EXPECT_EQ(std::get<std::int64_t>(tag->get()), 5);
}

TEST(Message, NestedMessageField) {
  auto nestedDesc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"nested_id", 1, FieldType::Int},
  });

  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"nested_msg", 2, FieldType::Message, /*repeated=*/false,
       /*packed=*/false, nestedDesc},
  });

  Message m(desc);

  EXPECT_TRUE(m.set("id", std::int64_t(100)));

  Message nestedMsg(nestedDesc);
  EXPECT_TRUE(nestedMsg.set("nested_id", std::int64_t(200)));

  EXPECT_TRUE(m.set("nested_msg", nestedMsg));
}

TEST(Message, OverwriteFieldKeepsLastValue) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
  });
  Message m(desc);

  EXPECT_TRUE(m.set("id", std::int64_t(1)));
  EXPECT_TRUE(m.set("id", std::int64_t(999)));

  auto id = m.get("id");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(std::get<std::int64_t>(id->get()), 999);
}

TEST(MessageCodec, RoundTripBasic) {
  auto desc = std::make_shared<ProtoDesc>(
      std::vector<FieldDesc>{{"id", 1, FieldType::Int},
                             {"value", 2, FieldType::Double},
                             {"name", 3, FieldType::String},
                             {"count", 4, FieldType::UInt},
                             {"active", 5, FieldType::Bool},
                             {"tags", 6, FieldType::Int, /*repeated=*/true}});

  Message m(desc);
  ASSERT_TRUE(m.set("id", std::int64_t(1234566)));
  ASSERT_TRUE(m.set("value", 123.45));
  ASSERT_TRUE(m.set("name", std::string("testing")));
  ASSERT_TRUE(m.set("count", std::uint64_t(7890)));
  ASSERT_TRUE(m.set("active", false));
  ASSERT_TRUE(m.push("tags", std::int64_t(10)));
  ASSERT_TRUE(m.push("tags", std::int64_t(20)));

  auto bytes = encodeMessage(m);
  // std::cout << ".|" << bytes.size() << "|.\n";
  auto [decodedOpt, next] = decodeMessage(bytes, desc);

  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  const Message &d = *decodedOpt;

  auto id = d.get("id");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(std::get<std::int64_t>(id->get()), 1234566);

  auto val = d.get("value");
  ASSERT_TRUE(val.has_value());
  EXPECT_DOUBLE_EQ(std::get<double>(val->get()), 123.45);

  auto name = d.get("name");
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(std::get<std::string>(name->get()), "testing");

  auto count = d.get("count");
  ASSERT_TRUE(count.has_value());
  EXPECT_EQ(std::get<std::uint64_t>(count->get()), 7890u);

  auto active = d.get("active");
  ASSERT_TRUE(active.has_value());
  EXPECT_EQ(std::get<bool>(active->get()), false);

  auto tag_1 = d.getByIndex("tags", 0);
  ASSERT_TRUE(tag_1.has_value());
  EXPECT_EQ(std::get<std::int64_t>(tag_1->get()), 10);

  auto tag_2 = d.getByIndex("tags", 1);
  ASSERT_TRUE(tag_2.has_value());
  EXPECT_EQ(std::get<std::int64_t>(tag_2->get()), 20);
}

TEST(MessageCodec, RoundTripRepeatedUnpackedInt) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"tags", 2, FieldType::Int, /*repeated=*/true, /*packed=*/false},
  });

  Message m(desc);
  ASSERT_TRUE(m.set("id", int64_t(7)));
  ASSERT_TRUE(m.push("tags", int64_t(10)));
  ASSERT_TRUE(m.push("tags", int64_t(20)));
  ASSERT_TRUE(m.push("tags", int64_t(-5)));

  auto bytes = encodeMessage(m);
  auto [decodedOpt, next] = decodeMessage(bytes, desc);

  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  auto t0 = decodedOpt->getByIndex("tags", 0);
  auto t1 = decodedOpt->getByIndex("tags", 1);
  auto t2 = decodedOpt->getByIndex("tags", 2);

  ASSERT_TRUE(t0.has_value());
  ASSERT_TRUE(t1.has_value());
  ASSERT_TRUE(t2.has_value());

  EXPECT_EQ(std::get<int64_t>(t0->get()), 10);
  EXPECT_EQ(std::get<int64_t>(t1->get()), 20);
  EXPECT_EQ(std::get<int64_t>(t2->get()), -5);
}

TEST(MessageCodec, RoundTripRepeatedUnpackedString) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"names", 1, FieldType::String, /*repeated=*/true, /*packed=*/false},
  });

  Message m(desc);
  ASSERT_TRUE(m.push("names", std::string("a")));
  ASSERT_TRUE(m.push("names", std::string("bb")));
  ASSERT_TRUE(m.push("names", std::string("")));

  auto bytes = encodeMessage(m);
  auto [decodedOpt, next] = decodeMessage(bytes, desc);

  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  auto n0 = decodedOpt->getByIndex("names", 0);
  auto n1 = decodedOpt->getByIndex("names", 1);
  auto n2 = decodedOpt->getByIndex("names", 2);

  ASSERT_TRUE(n0.has_value());
  ASSERT_TRUE(n1.has_value());
  ASSERT_TRUE(n2.has_value());

  EXPECT_EQ(std::get<std::string>(n0->get()), "a");
  EXPECT_EQ(std::get<std::string>(n1->get()), "bb");
  EXPECT_EQ(std::get<std::string>(n2->get()), "");
}

static inline void append(std::vector<uint8_t> &out,
                          const std::vector<uint8_t> &b) {
  out.insert(out.end(), b.begin(), b.end());
}

TEST(MessageCodec, RoundTripNestedMessage) {
  auto nestedDesc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"nested_id", 1, FieldType::Int},
  });

  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"nested_msg", 2, FieldType::Message, /*repeated=*/false,
       /*packed=*/false, nestedDesc},
  });

  Message m(desc);
  ASSERT_TRUE(m.set("id", int64_t(7)));

  Message nested(nestedDesc);
  ASSERT_TRUE(nested.set("nested_id", int64_t(123)));
  ASSERT_TRUE(m.set("nested_msg", nested));

  auto bytes = encodeMessage(m);
  auto [decodedOpt, next] = decodeMessage(bytes, desc);

  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  auto id = decodedOpt->get("id");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(std::get<int64_t>(id->get()), 7);

  auto nm = decodedOpt->get("nested_msg");
  ASSERT_TRUE(nm.has_value());
  ASSERT_TRUE(std::holds_alternative<Message>(nm->get()));
  const Message &dn = std::get<Message>(nm->get());

  auto nid = dn.get("nested_id");
  ASSERT_TRUE(nid.has_value());
  EXPECT_EQ(std::get<int64_t>(nid->get()), 123);
}

TEST(MessageCodec, NestedMessageEncodingIsLengthDelimited) {
  auto nestedDesc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"nested_id", 1, FieldType::Int},
  });

  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"nested_msg", 2, FieldType::Message, /*repeated=*/false,
       /*packed=*/false, nestedDesc},
  });

  Message nested(nestedDesc);
  ASSERT_TRUE(nested.set("nested_id", int64_t(200)));

  Message m(desc);
  ASSERT_TRUE(m.set("nested_msg", nested));

  auto got = encodeMessage(m);

  // Expected: key(field 2, LEN) + len(payload) + payload
  auto payload = encodeMessage(nested);
  std::vector<uint8_t> expected;
  append(expected, encodeVarint((uint64_t(2) << 3) | uint64_t(WireType::LEN)));
  append(expected, encodeVarint(payload.size()));
  append(expected, payload);

  EXPECT_EQ(got, expected);
}

TEST(MessageCodec, RoundTripRepeatedNestedMessages) {
  auto childDesc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"name", 1, FieldType::String},
  });

  auto parentDesc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"children", 3, FieldType::Message, /*repeated=*/true,
       /*packed=*/false, childDesc},
  });

  Message parent(parentDesc);

  Message c1(childDesc);
  Message c2(childDesc);
  ASSERT_TRUE(c1.set("name", std::string("A")));
  ASSERT_TRUE(c2.set("name", std::string("B")));

  ASSERT_TRUE(parent.push("children", c1));
  ASSERT_TRUE(parent.push("children", c2));

  auto bytes = encodeMessage(parent);
  auto [decodedOpt, next] = decodeMessage(bytes, parentDesc);

  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  auto e0 = decodedOpt->getByIndex("children", 0);
  auto e1 = decodedOpt->getByIndex("children", 1);
  ASSERT_TRUE(e0.has_value());
  ASSERT_TRUE(e1.has_value());

  ASSERT_TRUE(std::holds_alternative<Message>(e0->get()));
  ASSERT_TRUE(std::holds_alternative<Message>(e1->get()));

  const Message &d0 = std::get<Message>(e0->get());
  const Message &d1 = std::get<Message>(e1->get());

  auto n0 = d0.get("name");
  auto n1 = d1.get("name");
  ASSERT_TRUE(n0.has_value());
  ASSERT_TRUE(n1.has_value());
  EXPECT_EQ(std::get<std::string>(n0->get()), "A");
  EXPECT_EQ(std::get<std::string>(n1->get()), "B");
}

TEST(MessageCodec, SkipsUnknownFieldInsideNestedMessage) {
  auto nestedDesc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"ok", 1, FieldType::String},
  });

  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"nested_msg", 2, FieldType::Message, /*repeated=*/false,
       /*packed=*/false, nestedDesc},
  });

  // Build nested payload: ok="x" plus unknown field #99 LEN "zz"
  Message nested(nestedDesc);
  ASSERT_TRUE(nested.set("ok", std::string("x")));
  auto payload = encodeMessage(nested);
  append(payload, encodeVarint((uint64_t(99) << 3) | uint64_t(WireType::LEN)));
  append(payload, encodeStr("zz")); // includes its own length prefix

  // Wrap as parent.nested_msg (LEN-delimited)
  std::vector<uint8_t> bytes;
  append(bytes, encodeVarint((uint64_t(2) << 3) | uint64_t(WireType::LEN)));
  append(bytes, encodeVarint(payload.size()));
  append(bytes, payload);

  auto [decodedOpt, next] = decodeMessage(bytes, desc);
  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  auto nm = decodedOpt->get("nested_msg");
  ASSERT_TRUE(nm.has_value());
  ASSERT_TRUE(std::holds_alternative<Message>(nm->get()));
  const Message &dn = std::get<Message>(nm->get());

  auto ok = dn.get("ok");
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(std::get<std::string>(ok->get()), "x");
}

TEST(MessageCodec, RejectsMessageFieldWithWrongWireType) {
  auto nestedDesc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"x", 1, FieldType::Int},
  });

  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"nested_msg", 2, FieldType::Message, /*repeated=*/false,
       /*packed=*/false, nestedDesc},
  });

  // Field #2 encoded with VARINT wire type (wrong for message)
  std::vector<uint8_t> bytes;
  append(bytes, encodeVarint((uint64_t(2) << 3) | uint64_t(WireType::VARINT)));
  append(bytes, encodeVarint(1));

  auto [decodedOpt, idx] = decodeMessage(bytes, desc);
  EXPECT_FALSE(decodedOpt.has_value());
  EXPECT_EQ(idx, 1); // start of value (right after tag)
}

TEST(MessageCodec, RejectsTruncatedNestedPayload) {
  auto nestedDesc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"x", 1, FieldType::Int},
  });

  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"nested_msg", 2, FieldType::Message, /*repeated=*/false,
       /*packed=*/false, nestedDesc},
  });

  // key(field 2, LEN) + length(10) but only 1 byte payload
  std::vector<uint8_t> bytes;
  append(bytes, encodeVarint((uint64_t(2) << 3) | uint64_t(WireType::LEN)));
  append(bytes, encodeVarint(10));
  bytes.push_back(0x00);

  auto [decodedOpt, idx] = decodeMessage(bytes, desc);
  EXPECT_FALSE(decodedOpt.has_value());
  (void)idx; // avoid over-constraining error index if you change behavior later
}

TEST(MessageCodec, SkipsUnknownVarintField) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"name", 3, FieldType::String},
  });

  Message m(desc);
  ASSERT_TRUE(m.set("id", std::int64_t(7)));
  ASSERT_TRUE(m.set("name", std::string("ok")));

  auto bytes = encodeMessage(m);

  // Append an unknown field #99 with VARINT wire type and value 150.
  // key = (99 << 3) | 0
  auto key = encodeVarint((uint64_t(99) << 3) | uint64_t(WireType::VARINT));
  auto val = encodeVarint(150);

  bytes.insert(bytes.end(), key.begin(), key.end());
  bytes.insert(bytes.end(), val.begin(), val.end());

  auto [decodedOpt, next] = decodeMessage(bytes, desc);
  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  const Message &d = *decodedOpt;

  auto id = d.get("id");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(std::get<std::int64_t>(id->get()), 7);

  auto name = d.get("name");
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(std::get<std::string>(name->get()), "ok");
}

TEST(MessageCodec, SkipsUnknownLenField) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
  });

  Message m(desc);
  ASSERT_TRUE(m.set("id", std::int64_t(42)));

  auto bytes = encodeMessage(m);

  // Append unknown field #50 length-delimited with payload "xyz".
  auto key = encodeVarint((uint64_t(50) << 3) | uint64_t(WireType::LEN));
  auto payload = encodeStr("xyz"); // includes length prefix
  bytes.insert(bytes.end(), key.begin(), key.end());
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  auto [decodedOpt, next] = decodeMessage(bytes, desc);
  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  auto id = decodedOpt->get("id");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(std::get<std::int64_t>(id->get()), 42);
}

TEST(MessageCodec, RejectsKnownFieldWithWrongWireType) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
  });

  // Manually craft: field #1 with LEN wire type (wrong), payload "a".
  std::vector<uint8_t> bytes;
  auto key = encodeVarint((uint64_t(1) << 3) | uint64_t(WireType::LEN));
  auto payload = encodeStr("a");
  bytes.insert(bytes.end(), key.begin(), key.end());
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  auto [decodedOpt, next] = decodeMessage(bytes, desc);
  EXPECT_FALSE(decodedOpt.has_value());
  EXPECT_EQ(
      next,
      1); // your decode returns {nullopt, index} where index is current start
}

TEST(MessageCodec, RejectsFieldNumberZero) {
  // Tag 0 is invalid in protobuf.
  std::vector<uint8_t> bytes = {0x00};
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
  });

  auto [decodedOpt, next] = decodeMessage(bytes, desc);
  EXPECT_FALSE(decodedOpt.has_value());
  (void)next;
}

TEST(MessageCodec, RejectsTruncatedFixed64) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"value", 2, FieldType::Double},
  });

  // key for field #2 fixed64, but provide only 3 bytes of the 8 required
  std::vector<uint8_t> bytes;
  auto key = encodeVarint((uint64_t(2) << 3) | uint64_t(WireType::I64));
  bytes.insert(bytes.end(), key.begin(), key.end());
  bytes.push_back(0x00);
  bytes.push_back(0x01);
  bytes.push_back(0x02);

  auto [decodedOpt, next] = decodeMessage(bytes, desc);
  EXPECT_FALSE(decodedOpt.has_value());
}

TEST(MessageCodec, RoundTripMissingFields) {
  auto desc = std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
      {"id", 1, FieldType::Int},
      {"value", 2, FieldType::Double},
      {"name", 3, FieldType::String},
  });

  Message m(desc);
  ASSERT_TRUE(m.set("name", std::string("only_name")));

  auto bytes = encodeMessage(m);
  auto [decodedOpt, next] = decodeMessage(bytes, desc);

  ASSERT_TRUE(decodedOpt.has_value());
  EXPECT_EQ(next, (int)bytes.size());

  EXPECT_FALSE(decodedOpt->get("id").has_value());
  EXPECT_FALSE(decodedOpt->get("value").has_value());

  auto name = decodedOpt->get("name");
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(std::get<std::string>(name->get()), "only_name");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

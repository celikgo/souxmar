// SPDX-License-Identifier: Apache-2.0

#include "souxmar/pipeline/value.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace souxmar::pipeline;

namespace {

TEST(Value, DefaultIsNull) {
  Value v;
  EXPECT_EQ(v.kind(), Value::Kind::Null);
}

TEST(Value, ScalarBuildersRoundtrip) {
  EXPECT_EQ(Value::boolean(true).as_bool(), true);
  EXPECT_DOUBLE_EQ(Value::number(1.5).as_number(), 1.5);
  EXPECT_EQ(Value::string("abc").as_string(), "abc");
  EXPECT_EQ(Value::stage_ref("import").as_stage().stage_id, "import");
}

TEST(Value, ListAndMapRoundtrip) {
  auto v = Value::list({Value::number(1.0), Value::number(2.0)});
  ASSERT_EQ(v.as_list().size(), 2u);
  EXPECT_DOUBLE_EQ(v.as_list()[1].as_number(), 2.0);

  auto m = Value::map({{"k", Value::string("v")}});
  ASSERT_TRUE(m.find("k") != nullptr);
  EXPECT_EQ(m.find("k")->as_string(), "v");
  EXPECT_EQ(m.find("missing"), nullptr);
}

TEST(Value, WrongKindAccessThrows) {
  EXPECT_THROW((void)Value::number(1).as_string(), std::runtime_error);
  EXPECT_THROW((void)Value::null_value().as_bool(), std::runtime_error);
}

TEST(Value, TryAccessReturnsNullForWrongKind) {
  Value v = Value::string("hi");
  EXPECT_NE(v.try_string(), nullptr);
  EXPECT_EQ(v.try_number(), nullptr);
  EXPECT_EQ(v.try_bool(), nullptr);
}

TEST(Value, EqualityRespectsKindAndPayload) {
  EXPECT_EQ(Value::number(1.0), Value::number(1.0));
  EXPECT_NE(Value::number(1.0), Value::number(2.0));
  EXPECT_NE(Value::number(1.0), Value::string("1"));
  EXPECT_EQ(Value::stage_ref("a"), Value::stage_ref("a"));
  EXPECT_NE(Value::stage_ref("a"), Value::stage_ref("b"));
}

TEST(Value, KindNameForLogging) {
  EXPECT_EQ(kind_name(Value::Kind::Stage), "stage-ref");
  EXPECT_EQ(kind_name(Value::Kind::Number), "number");
}

}  // namespace

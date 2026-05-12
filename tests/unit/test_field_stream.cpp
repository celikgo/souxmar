// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/field.h"
#include "souxmar/core/field_stream.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

using namespace souxmar::core;

namespace {

// Build a scalar field of 5 locations with known values.
Field MakeScalarField(std::vector<double> values) {
  Field f("test", FieldLocation::Nodal, FieldKind::Scalar, values.size());
  auto buf = f.step(0);
  for (std::size_t i = 0; i < values.size(); ++i)
    buf[i] = values[i];
  return f;
}

// Build a vector field (3 components) with explicit per-location values.
Field MakeVectorField(std::vector<std::array<double, 3>> values) {
  Field f("u", FieldLocation::Nodal, FieldKind::Vector, values.size());
  auto buf = f.step(0);
  for (std::size_t i = 0; i < values.size(); ++i) {
    buf[i * 3 + 0] = values[i][0];
    buf[i * 3 + 1] = values[i][1];
    buf[i * 3 + 2] = values[i][2];
  }
  return f;
}

}  // namespace

TEST(FieldStream, ScalarRangeMatchesData) {
  const Field f = MakeScalarField({1.0, -2.0, 3.5, 0.0, 10.0});
  FieldStream s(f);
  EXPECT_EQ(s.count(), 5u);
  EXPECT_EQ(s.components(), 1u);
  ASSERT_EQ(s.range_min().size(), 1u);
  ASSERT_EQ(s.range_max().size(), 1u);
  EXPECT_DOUBLE_EQ(s.range_min()[0], -2.0);
  EXPECT_DOUBLE_EQ(s.range_max()[0], 10.0);
}

TEST(FieldStream, VectorRangeIsPerComponent) {
  const Field f = MakeVectorField({{{0, 0, 0}}, {{1, -5, 7}}, {{-3, 2, 0}}});
  FieldStream s(f);
  EXPECT_EQ(s.count(), 3u);
  EXPECT_EQ(s.components(), 3u);
  ASSERT_EQ(s.range_min().size(), 3u);
  EXPECT_DOUBLE_EQ(s.range_min()[0], -3.0);
  EXPECT_DOUBLE_EQ(s.range_min()[1], -5.0);
  EXPECT_DOUBLE_EQ(s.range_min()[2], 0.0);
  EXPECT_DOUBLE_EQ(s.range_max()[0], 1.0);
  EXPECT_DOUBLE_EQ(s.range_max()[1], 2.0);
  EXPECT_DOUBLE_EQ(s.range_max()[2], 7.0);
}

TEST(FieldStream, ValuesAreF32DownCastOfSource) {
  const Field f = MakeScalarField({1.5, 2.5, 3.5});
  FieldStream s(f);
  const auto v = s.values();
  ASSERT_EQ(v.size(), 3u);
  EXPECT_FLOAT_EQ(v[0], 1.5f);
  EXPECT_FLOAT_EQ(v[1], 2.5f);
  EXPECT_FLOAT_EQ(v[2], 3.5f);
}

TEST(FieldStream, EmptyFieldHasEmptyRangeAndValues) {
  Field f("empty", FieldLocation::Nodal, FieldKind::Scalar, 0);
  FieldStream s(f);
  EXPECT_EQ(s.count(), 0u);
  // components() reflects the kind even when count is zero.
  EXPECT_EQ(s.components(), 1u);
  EXPECT_EQ(s.values().size(), 0u);
  EXPECT_EQ(s.range_min().size(), 0u);
  EXPECT_EQ(s.range_max().size(), 0u);
}

TEST(FieldStream, UnitsIsEmptyUntilFieldHeaderAddsAccessor) {
  // RFC-002 Open Q1 — the field handle has no units accessor in v1.5.
  // The stream returns "" for now; a future v1.5.1 ratchet adds the
  // souxmar_field_units accessor and wires it through here.
  const Field f = MakeScalarField({1.0, 2.0});
  FieldStream s(f);
  EXPECT_EQ(s.units(), std::string_view{""});
}

TEST(FieldStream, ValueLayoutMatchesSoA) {
  // count = 4 locations, components = 3 (vector). Layout is location-
  // major: [loc0.x, loc0.y, loc0.z, loc1.x, ...].
  const Field f = MakeVectorField({{{1, 2, 3}}, {{4, 5, 6}}, {{7, 8, 9}}, {{10, 11, 12}}});
  FieldStream s(f);
  const auto v = s.values();
  ASSERT_EQ(v.size(), 12u);
  EXPECT_FLOAT_EQ(v[0], 1.0f);
  EXPECT_FLOAT_EQ(v[1], 2.0f);
  EXPECT_FLOAT_EQ(v[2], 3.0f);
  EXPECT_FLOAT_EQ(v[3], 4.0f);
  EXPECT_FLOAT_EQ(v[11], 12.0f);
}

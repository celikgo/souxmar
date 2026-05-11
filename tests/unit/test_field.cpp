// SPDX-License-Identifier: Apache-2.0

#include "souxmar/core/field.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace souxmar::core;

namespace {

TEST(Field, ScalarMetadata) {
  Field f("temperature", FieldLocation::Nodal, FieldKind::Scalar, 100);
  EXPECT_EQ(f.name(),           "temperature");
  EXPECT_EQ(f.location(),       FieldLocation::Nodal);
  EXPECT_EQ(f.kind(),           FieldKind::Scalar);
  EXPECT_EQ(f.components(),     1u);
  EXPECT_EQ(f.count(),          100u);
  EXPECT_EQ(f.num_time_steps(), 1u);
  EXPECT_EQ(f.data().size(),    100u);
}

TEST(Field, VectorAllocates3xN) {
  Field f("velocity", FieldLocation::Cell, FieldKind::Vector, 50);
  EXPECT_EQ(f.components(), 3u);
  EXPECT_EQ(f.data().size(), 150u);
}

TEST(Field, TensorAllocates9xN) {
  Field f("stress", FieldLocation::GaussPoint, FieldKind::Tensor, 10);
  EXPECT_EQ(f.components(), 9u);
  EXPECT_EQ(f.data().size(), 90u);
}

TEST(Field, ZeroInitialised) {
  Field f("density", FieldLocation::Cell, FieldKind::Scalar, 5);
  for (double v : f.data()) EXPECT_DOUBLE_EQ(v, 0.0);
}

TEST(Field, AtAccessRoundtrip) {
  Field f("displacement", FieldLocation::Nodal, FieldKind::Vector, 4);
  // Set node-2 displacement.
  auto comp = f.at(2);
  comp[0] = 1.5;
  comp[1] = 2.5;
  comp[2] = 3.5;

  const Field& cf = f;
  auto read = cf.at(2);
  EXPECT_DOUBLE_EQ(read[0], 1.5);
  EXPECT_DOUBLE_EQ(read[1], 2.5);
  EXPECT_DOUBLE_EQ(read[2], 3.5);
}

TEST(Field, OutOfRangeAtThrows) {
  Field f("scalar", FieldLocation::Nodal, FieldKind::Scalar, 3);
  EXPECT_THROW((void)f.at(5), std::out_of_range);
}

TEST(Field, TimeStepsAllocateContiguously) {
  Field f("temp", FieldLocation::Nodal, FieldKind::Scalar, 10, 4);
  EXPECT_EQ(f.num_time_steps(), 4u);
  EXPECT_EQ(f.data().size(),    40u);

  // Write at time 2, location 5; verify with span lookup.
  f.at(5, 2)[0] = 7.5;
  const Field& cf = f;
  EXPECT_DOUBLE_EQ(cf.at(5, 2)[0], 7.5);
  // Other times unaffected.
  EXPECT_DOUBLE_EQ(cf.at(5, 0)[0], 0.0);
  EXPECT_DOUBLE_EQ(cf.at(5, 3)[0], 0.0);
}

TEST(Field, StepReturnsWholeTimeSlice) {
  Field f("temp", FieldLocation::Nodal, FieldKind::Vector, 3, 2);
  // Components per slice = 3 (Vector) * 3 (count) = 9.
  EXPECT_EQ(f.step(0).size(), 9u);
  EXPECT_EQ(f.step(1).size(), 9u);
}

TEST(Field, OutOfRangeStepThrows) {
  Field f("temp", FieldLocation::Nodal, FieldKind::Scalar, 3, 1);
  EXPECT_THROW((void)f.step(1), std::out_of_range);
}

TEST(Field, ZeroTimeStepsRejected) {
  EXPECT_THROW(
      Field("bad", FieldLocation::Nodal, FieldKind::Scalar, 1, 0),
      std::invalid_argument);
}

TEST(Field, MoveTransfers) {
  Field a("u", FieldLocation::Nodal, FieldKind::Scalar, 2);
  a.at(0)[0] = 9.0;
  Field b = std::move(a);
  EXPECT_DOUBLE_EQ(b.at(0)[0], 9.0);
  EXPECT_EQ(b.name(), "u");
}

}  // namespace

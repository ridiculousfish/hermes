/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "hermes/DynHandle/DynHandle.h"
#include "hermes/DynHandle/DynHandle-Inline.h"

#include <vector>
#include "gtest/gtest.h"

using namespace hermes::vm;

struct DynHandleTests : public ::testing::Test {
  using Slot = DynHandleAllocator::Slot;
  DynHandleAllocator allocator{};

  // \return the number of allocated DynHandles.
  // This is used for testing only.
  size_t countAllocations() const;
};

size_t DynHandleTests::countAllocations() const {
  size_t result = 0;
  for (const auto *cursor = allocator.chunks_; cursor; cursor = cursor->next) {
    // Add allocatedEnd, minus those in the free list.
    uint32_t freedCount = 0;
    for (const Slot *freed = cursor->freeList; freed != nullptr;
         freed = freed->phv.getNativePointer<Slot>()) {
      EXPECT_TRUE(cursor->contains(freed));
      freedCount += 1;
    }
    EXPECT_LE(freedCount, cursor->allocatedEnd);
    result += (cursor->allocatedEnd - freedCount);
  }
  return result;
}

TEST_F(DynHandleTests, BasicTest) {
  std::vector<DynHandle<>> dynhandles;
  // Allocate a 100k DynHandles, encoding HermesValues false, true, false,
  // true...
  for (size_t i = 0; i < (1u << 17); i++) {
    dynhandles.push_back(
        allocator.allocate(HermesValue::encodeBoolValue(i & 1)));
  }
  EXPECT_EQ(dynhandles.size(), countAllocations());
  // Verify that everyone got the right one.
  bool sense = false;
  for (auto &dh : dynhandles) {
    ASSERT_TRUE(dh.valid());
    ASSERT_TRUE(dh->isBool());
    EXPECT_EQ(dh->getBool(), sense);
    sense = !sense;
  }

  // Stress the allocator by freeing half the handles and allocating a bunch
  // more.
  size_t idx = 0;
  auto erase_alternates = [&](const DynHandle<> &dh) { return idx++ & 1; };
  dynhandles.erase(
      std::remove_if(dynhandles.begin(), dynhandles.end(), erase_alternates),
      dynhandles.end());
  EXPECT_EQ(dynhandles.size(), countAllocations());

  for (size_t i = 0; i < (1u << 16); i++) {
    dynhandles.push_back(
        allocator.allocate(HermesValue::encodeBoolValue(i & 1)));
  }
  EXPECT_EQ(dynhandles.size(), countAllocations());

  dynhandles.clear();
  EXPECT_EQ(0, countAllocations());
}

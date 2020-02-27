/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "hermes/DynHandle/DynHandle.h"
#include "hermes/DynHandle/DynHandle-Inline.h"
#include "hermes/VM/JSObject.h"
#include "hermes/VM/Runtime.h"

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

TEST_F(DynHandleTests, GCTest) {
  auto sharedrt = Runtime::create(RuntimeConfig::Builder().build());
  Runtime *rt = sharedrt.get();
  auto &gc = rt->getHeap();
  GCScope scope{rt};
  rt->addCustomRootsFunction(
      [&](GC *gc, SlotAcceptor &sla) { this->allocator.markRoots(gc, sla); });
  // Collect so Runtime's allocations aren't reported in our results.
  gc.collect();

  // Allocate lots of DynHandles.
  std::vector<DynHandle<JSObject>> dynhandles;
  for (size_t i = 0; i < (1u << 16); i++) {
    dynhandles.push_back(allocator.allocate<JSObject>());
  }

  // Place a handful of objects in our dynhandles.
  size_t dhCount = dynhandles.size();
  size_t extantObjCount = 0;
  for (size_t idx : {dhCount * 0, dhCount / 3, dhCount / 2, dhCount - 1}) {
    MutableHandle<JSObject> mh = dynhandles[idx];
    mh.set(JSObject::create(rt, Handle<JSObject>(rt, nullptr)).get());
    extantObjCount++;

    // Allocate some garbage too.
    JSObject::create(rt, Handle<JSObject>(rt, nullptr));
    extantObjCount++;
  }

  // Collect; exactly half should be collected.
  size_t collectableObjCount = extantObjCount / 2;
  gc.collect();
#ifndef NDEBUG
  GCBase::DebugHeapInfo debugInfo;
  gc.getDebugHeapInfo(debugInfo);
  EXPECT_EQ(collectableObjCount, debugInfo.numCollectedObjects);
#endif
  extantObjCount -= collectableObjCount;

  // Stomp one handle and verify one object is collected.
  static_cast<MutableHandle<JSObject>>(dynhandles.front()).set(nullptr);
  collectableObjCount = 1;
  gc.collect();
#ifndef NDEBUG
  gc.getDebugHeapInfo(debugInfo);
  EXPECT_EQ(collectableObjCount, debugInfo.numCollectedObjects);
#endif
  extantObjCount -= collectableObjCount;

  // Stomp all values and verify that all objects are collected.
  dynhandles.clear();
  collectableObjCount = extantObjCount;
  gc.collect();
#ifndef NDEBUG
  gc.getDebugHeapInfo(debugInfo);
  EXPECT_EQ(collectableObjCount, debugInfo.numCollectedObjects);
#endif
}

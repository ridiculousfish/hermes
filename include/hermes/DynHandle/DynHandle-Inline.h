/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef HERMES_DYNHANDLE_DYNHANDLE_INLINE_H
#define HERMES_DYNHANDLE_DYNHANDLE_INLINE_H

namespace hermes {
namespace vm {

template <typename T>
DynHandle<T> DynHandleAllocator::allocate(
    typename HermesValueTraits<T>::value_type value) {
  Slot *slot = allocateSlot();
  slot->phv = HermesValueTraits<T>::encode(value);
  return DynHandle<T>{slot};
}

inline DynHandleAllocator::Chunk *DynHandleAllocator::chunkForSlot(
    DynHandleAllocator::Slot *slot) {
  // Find the chunk as as the slot index, aligned down by the chunk byte size.
  // Avoid llvm::alignDown, which uses 64 bit math.
  assert(slot != nullptr && "Invalid slot");
  uintptr_t slotInt = reinterpret_cast<uintptr_t>(slot);
  uintptr_t chunkInt = slotInt - (slotInt % kChunkByteSize);
  Chunk *result = reinterpret_cast<Chunk *>(chunkInt);
  assert(
      result->contains(slot) &&
      "Chunk does not contain the slot that found it");
  return result;
}

inline DynHandleAllocator::Slot *DynHandleAllocator::Chunk::getSlots() {
  return this->getTrailingObjects<Slot>();
}

inline const DynHandleAllocator::Slot *DynHandleAllocator::Chunk::getSlots()
    const {
  return this->getTrailingObjects<Slot>();
}

inline bool DynHandleAllocator::Chunk::contains(
    const DynHandleAllocator::Slot *slot) const {
  const Slot *mySlots = getSlots();
  return slot >= mySlots && slot < mySlots + DynHandleAllocator::kSlotsPerChunk;
}

inline DynHandleAllocator::Slot *DynHandleAllocator::Chunk::tryAllocate() {
  Slot *result = nullptr;
  if (freeList != nullptr) {
    // Allocate from the free list, and update the free list to the next
    // element (or possibly null).
    result = freeList;
    freeList = result->phv.getNativePointer<Slot>();
    assert((freeList == nullptr || contains(freeList)) && "Corrupt free list");
  } else if (allocatedEnd < DynHandleAllocator::kSlotsPerChunk) {
    // Allocate an untouched slot.
    result = getSlots() + allocatedEnd;
    allocatedEnd++;
  }
  return result;
}

inline void DynHandleAllocator::Chunk::free(Slot *slot) {
  assert(slot && contains(slot) && "Does not contain slot");
  slot->phv = HermesValue::encodeNativePointer(freeList);
  freeList = slot;
}

inline DynHandleAllocator::Slot *DynHandleAllocator::allocateSlot() {
  Slot *result = chunks_ ? chunks_->tryAllocate() : nullptr;
  if (!result) {
    result = allocateSlotSlowPath();
  }
  assert(result && "Should never fail to allocate");
  return result;
}

} // namespace vm
} // namespace hermes

#endif

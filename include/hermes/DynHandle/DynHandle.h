/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef HERMES_DYNHANDLE_DYNHANDLE_H
#define HERMES_DYNHANDLE_DYNHANDLE_H

#include "hermes/VM/Handle.h"

#include "llvm/Support/TrailingObjects.h"

struct DynHandleTests;

namespace hermes {
namespace vm {

template <typename T>
class DynHandle;

/// DynHandleAllocator allocates DynHandles, which are Handles with dynamic
/// lifetimes. The implementation is as a linked list of contiguous chunks,
/// where are aligned to a particular boundary. A DynHandle may find its Chunk
/// by rounding down to the aligned boundary.
class DynHandleAllocator {
  template <typename T>
  friend class DynHandle;
  friend DynHandleTests;

 public:
  /// Allocate a new DynHandle, populating it with \p T.
  template <typename T = HermesValue>
  DynHandle<T> allocate(
      typename HermesValueTraits<T>::value_type value =
          HermesValueTraits<T>::defaultValue());

  DynHandleAllocator() = default;
  ~DynHandleAllocator();

 private:
  // A Slot is storage for a PinnedHermesValue to live.
  struct Slot final {
    PinnedHermesValue phv;

    // Copying and moving is prohibited.
    Slot(const Slot &) = delete;
    void operator=(const Slot &) = delete;
    Slot(Slot &&) = delete;
    void operator=(Slot &&) = delete;
  };

  // A Chunk is an array of Slots, plus a header.
  // Chunks are always allocated with a particular alignment.
  // Note that chunks are allocated malloc-style. Trailing PinnedHermesValues
  // are left deliberately uninitialized.
  struct Chunk final : public llvm::TrailingObjects<Chunk, Slot> {
    // A pointer to the next Chunk, or nullptr if this is the last one.
    Chunk *next{nullptr};

    // The first slot in the free list, or nullptr for none.
    // The free list is encoded as NativePointers which point into the Chunk's
    // Slot storage.
    Slot *freeList{nullptr};

    // One past the largest index of any allocated slot in this storage.
    // At GC time, only the range [0, allocatedEnd) must be marked.
    // Note that this range is guaranteed sensible; slots after this may contain
    // garbage.
    uint32_t allocatedEnd{0};

    // Convenience accessors for slots array.
    inline Slot *getSlots();
    inline const Slot *getSlots() const;

    // Attempt to allocate a slot.
    // \return the slot, or nullptr if we are full. The PHV in the returned
    // slot is not initialized.
    inline Slot *tryAllocate();

    // Free a contained slot.
    inline void free(Slot *slot);

    // \return whether \p slot points into our Slot storage.
    inline bool contains(const Slot *slot) const;
  };

  // The allocation byte alignment of a chunk.
  // This must be a power of 2, to support posix_memalign.
  static constexpr size_t kChunkAlignment = 1u << 10;

  // The desired size of a chunk, in bytes.
  static constexpr size_t kChunkByteSize = kChunkAlignment;

  // How many Slots fit in a Chunk; this is the byte size minus the header size.
  static constexpr size_t kSlotsPerChunk =
      (kChunkByteSize - sizeof(Chunk)) / sizeof(Slot);

  // The byte size must not exceed the alignment, or else a Slot will compute
  // the wrong Chunk.
  static_assert(
      kChunkByteSize <= kChunkAlignment,
      "Chunk byte size may not exceed alignment");

  // Ensure we don't have padding which pushes us larger than our byte size.
  static_assert(
      Chunk::totalSizeToAlloc<Slot>(kSlotsPerChunk) <= kChunkByteSize,
      "Too many slots per chunk");

  // Allocate a new Slot.
  // The PinnedHermesValue contained in the Slot is uninitialized.
  inline Slot *allocateSlot();

  // Slow path for allocating a new slot.
  Slot *allocateSlotSlowPath();

  // \return the chunk corresponding to a slot.
  static inline Chunk *chunkForSlot(Slot *slot);

  // The linked list of chunks, or nullptr if not yet allocated.
  Chunk *chunks_{nullptr};
};

/// DynHandle represents a Handle with a dynamic lifetime.
/// A DynHandle<T> wraps a PinnedHermesValue, providing Handle<T> and
/// MutableHandle<T>.
/// A DynHandle which is default-constructed or moved-from is
/// called Invalid; this indicates that it does not have a storage location, and
/// cannot produce a value.
/// Note that DynHandle does not actually inherit from
/// Handle; this is to prevent object slicing where the DynHandle destructor
/// would not run.
template <typename T = HermesValue>
class DynHandle {
 public:
  using value_type = typename Handle<T>::value_type;

  /// Access the underlying value.
  value_type get() const {
    return HermesValueTraits<T>::decode(getHermesValue());
  }

  /// Access the underlying value as a HermesValue.
  const HermesValue &getHermesValue() const {
    return slot()->phv;
  }

  /// Produce a Handle from this DynHandle.
  /// The MutableHandle is valid as long as the DynHandle is alive.
  /* implicit */ operator Handle<T>() const {
    return Handle<T>(&slot()->phv);
  }

  /// Produce a MutableHandle from this DynHandle.
  /// The MutableHandle is valid as long as the DynHandle is valid.
  /* implicit */ operator MutableHandle<T>() {
    return MutableHandle<T>(&slot()->phv, false);
  }

  /// Dereference to obtain a T.
  /// 'this' must be valid.
  typename HermesValueTraits<T>::arrow_type operator->() const {
    return HermesValueTraits<T>::arrow(getHermesValue());
  }

  value_type operator*() const {
    return get();
  }

  /// Copying is prohibited.
  DynHandle(const DynHandle &) = delete;
  void operator=(const DynHandle &) = delete;

  /// Moving is permitted, leaving the moved-from DynHandle as invalid.
  DynHandle(DynHandle &&rhs) : slot_(rhs.slot_) {
    rhs.slot_ = nullptr;
  }

  /// Move assignment is permitted, leaving the moved-from DynHandle as invalid.
  DynHandle &operator=(DynHandle &&rhs) {
    freeThis();
    std::swap(slot_, rhs.slot_);
    return *this;
  }

  /// Default construction, producing an Invalid DynHandle.
  /// Note such a DynHandle cannot provide a Handle; instead use
  /// DynHandleAllocator.
  DynHandle() : slot_(nullptr) {}

  /// \return whether this DynHandle is valid.
  // A DynHandle is invalid if it is default constructed, or moved-from.
  bool valid() const {
    return slot_ != nullptr;
  }

  ~DynHandle() {
    freeThis();
  }

 private:
  // Construct from a Slot.
  explicit DynHandle(DynHandleAllocator::Slot *slot) : slot_(slot) {
    assert(slot != nullptr && "Slot may not be initially null");
  }

  // Access the slot, asserting that it exists.
  DynHandleAllocator::Slot *slot() {
    assert(slot_ && "Invalid slot");
    return slot_;
  }

  const DynHandleAllocator::Slot *slot() const {
    assert(slot_ && "Invalid slot");
    return slot_;
  }

  // \return the Chunk for this DynHandle.
  DynHandleAllocator::Chunk *getChunk() {
    assert(valid() && "Invalid slot");
    return DynHandleAllocator::chunkForSlot(slot_);
  }

  // Free this DynHandle. This places the slot back into the free list.
  void freeThis() {
    if (valid()) {
      getChunk()->free(slot_);
      slot_ = nullptr;
    }
  }

  // A pointer to the slot where the PinnedHermesValue lives.
  DynHandleAllocator::Slot *slot_{nullptr};

  friend DynHandleAllocator;
  friend DynHandleTests;
};

} // namespace vm
} // namespace hermes

#endif

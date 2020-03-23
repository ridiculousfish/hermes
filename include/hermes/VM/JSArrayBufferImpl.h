/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef HERMES_VM_JSARRAYBUFFERIMPLH
#define HERMES_VM_JSARRAYBUFFERIMPLH

#include "llvm/ADT/Optional.h"

#include <cassert>
#include <memory>

namespace hermes {
namespace vm {

class MallocArrayBufferImpl {
 public:
  /// Allocate a new ArrayBufferImpl of the given \p size.
  /// If \p zero is set, zero it, otherwise its contents are undefined.
  /// \return an ArrayBufferImpl on success, or None on allocation failure.
  static inline llvm::Optional<MallocArrayBufferImpl> allocate(
      size_t size,
      bool zero);

  /// \return the size of this array buffer, in bytes.
  inline size_t size() const;

  /// \return a pointer to the contents.
  inline const uint8_t *data() const;

  /// \return a writable pointer to the contents.
  inline uint8_t *dataForWrite();

  /// MallocArrayBufferImpl may be moved.
  MallocArrayBufferImpl(MallocArrayBufferImpl &&) = default;
  MallocArrayBufferImpl &operator=(MallocArrayBufferImpl &&) = default;

  /// MallocArrayBufferImpl may not be copied.
  MallocArrayBufferImpl(const MallocArrayBufferImpl &) = delete;
  void operator=(const MallocArrayBufferImpl &) = delete;

  ~MallocArrayBufferImpl() = default;

 private:
  /// A unique_ptr deleter for malloc / calloc.
  struct MallocDeleter {
    void operator()(uint8_t *val) {
      std::free(val);
    }
  };
  using MallocPointer = std::unique_ptr<uint8_t, MallocDeleter>;

  /// Private constructor. Use allocate() to create.
  inline MallocArrayBufferImpl(MallocPointer ptr, size_t size);

  /// A pointer to the memory. Note this may be null if size_ is zero.
  MallocPointer ptr_;

  /// The size of the allocated buffer.
  size_t size_;
};

MallocArrayBufferImpl::MallocArrayBufferImpl(MallocPointer ptr, size_t size)
    : ptr_(std::move(ptr)), size_(size) {
  assert(
      (ptr_ != nullptr || size_ == 0) &&
      "Cannot have null pointer unless size is 0");
}

llvm::Optional<MallocArrayBufferImpl> MallocArrayBufferImpl::allocate(
    size_t size,
    bool zero) {
  MallocPointer data{};
  if (size > 0) {
    void *mem =
        zero ? calloc(size, sizeof(uint8_t)) : malloc(size * sizeof(uint8_t));
    if (!mem) {
      return llvm::None;
    }
    data.reset(static_cast<uint8_t *>(mem));
  }
  return MallocArrayBufferImpl(std::move(data), size);
}

size_t MallocArrayBufferImpl::size() const {
  return size_;
}

const uint8_t *MallocArrayBufferImpl::data() const {
  return ptr_.get();
}

uint8_t *MallocArrayBufferImpl::dataForWrite() {
  return ptr_.get();
}

using ArrayBufferImpl = MallocArrayBufferImpl;

} // namespace vm
} // namespace hermes

#endif

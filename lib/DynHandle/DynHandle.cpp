/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "hermes/DynHandle/DynHandle.h"
#include "hermes/DynHandle/DynHandle-Inline.h"
#include "hermes/Support/OSCompat.h"

using namespace hermes::vm;

DynHandleAllocator::~DynHandleAllocator() {
  while (Chunk *cursor = chunks_) {
    chunks_ = cursor->next;
    cursor->~Chunk();
    oscompat::free_aligned(cursor);
  }
}

DynHandleAllocator::Slot *DynHandleAllocator::allocateSlotSlowPath() {
  // Try finding a chunk with empty slots.
  // If we find one, we rotate it to the front.
  for (Chunk *cursor = chunks_, *prev = nullptr; cursor != nullptr;
       prev = cursor, cursor = cursor->next) {
    if (Slot *result = cursor->tryAllocate()) {
      // This chunk had empty slots; splice it to the front if it's not
      // already there.
      if (prev) {
        prev->next = cursor->next;
        cursor->next = chunks_;
        chunks_ = cursor;
      }
      return result;
    }
  }

  // We have gone through all our chunks (perhaps zero) and none had space.
  // Allocate a new chunk and link it in.
  auto allocation = oscompat::allocate_aligned(kChunkByteSize, kChunkAlignment);
  if (!allocation) {
    hermes_fatal("allocate_aligned failed");
  }
  Chunk *chunk = new (*allocation) Chunk();
  chunk->next = chunks_;
  chunks_ = chunk;

  // Allocate from the chunk.
  Slot *res = chunks_->tryAllocate();
  assert(res && "Freshly allocated chunks should always have slots");
  return res;
}

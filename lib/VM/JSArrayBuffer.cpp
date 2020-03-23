/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "hermes/VM/JSArrayBuffer.h"

#include "hermes/VM/BuildMetadata.h"

#include "llvm/Support/Debug.h"
#define DEBUG_TYPE "serialize"

namespace hermes {
namespace vm {

//===----------------------------------------------------------------------===//
// class JSArrayBuffer

ObjectVTable JSArrayBuffer::vt{
    VTable(
        CellKind::ArrayBufferKind,
        cellSize<JSArrayBuffer>(),
        _finalizeImpl,
        nullptr,
        _mallocSizeImpl,
        nullptr,
        nullptr,
        _externalMemorySizeImpl, // externalMemorySize
        VTable::HeapSnapshotMetadata{HeapSnapshot::NodeType::Object,
                                     nullptr,
                                     _snapshotAddEdgesImpl,
                                     _snapshotAddNodesImpl,
                                     nullptr}),
    _getOwnIndexedRangeImpl,
    _haveOwnIndexedImpl,
    _getOwnIndexedPropertyFlagsImpl,
    _getOwnIndexedImpl,
    _setOwnIndexedImpl,
    _deleteOwnIndexedImpl,
    _checkAllOwnIndexedImpl,
};

void ArrayBufferBuildMeta(const GCCell *cell, Metadata::Builder &mb) {
  mb.addJSObjectOverlapSlots(JSObject::numOverlapSlots<JSArrayBuffer>());
  ObjectBuildMeta(cell, mb);
}

#ifdef HERMESVM_SERIALIZE
JSArrayBuffer::JSArrayBuffer(Deserializer &d)
    : JSObject(d, &vt.base), data_(nullptr), size_(0), attached_(false) {
  size_type size = d.readInt<size_type>();
  attached_ = d.readInt<uint8_t>();
  if (!attached_) {
    return;
  }
  // Don't need to zero out the data since we'll be copying into it immediately.
  // This call sets size_, data_, and attached_.
  if (LLVM_UNLIKELY(
          createDataBlock(d.getRuntime(), size, false) ==
          ExecutionStatus::EXCEPTION)) {
    hermes_fatal("Fail to malloc storage for ArrayBuffer");
  }
  if (size != 0) {
    d.readData(data_, size);
    // data_ is tracked by IDTracker for heapsnapshot. We should do relocation
    // for it.
    d.endObject(data_);
  }
}

void ArrayBufferSerialize(Serializer &s, const GCCell *cell) {
  auto *self = vmcast<const JSArrayBuffer>(cell);
  JSObject::serializeObjectImpl(
      s, cell, JSObject::numOverlapSlots<JSArrayBuffer>());
  s.writeInt<JSArrayBuffer::size_type>(self->size_);
  s.writeInt<uint8_t>((uint8_t)self->attached_);
  // Only serialize data_ when attached_.
  if (self->attached_ && self->size_ != 0) {
    s.writeData(self->data_, self->size_);
    // data_ is tracked by IDTracker for heapsnapshot. We should do relocation
    // for it.
    s.endObject(self->data_);
  }

  s.endObject(cell);
}

void ArrayBufferDeserialize(Deserializer &d, CellKind kind) {
  void *mem = d.getRuntime()->alloc</*fixedSize*/ true, HasFinalizer::Yes>(
      cellSize<JSArrayBuffer>());
  auto *cell = new (mem) JSArrayBuffer(d);
  d.endObject(cell);
}
#endif

PseudoHandle<JSArrayBuffer> JSArrayBuffer::create(
    Runtime *runtime,
    Handle<JSObject> parentHandle) {
  JSObjectAlloc<JSArrayBuffer, HasFinalizer::Yes> mem{runtime};
  return mem.initToPseudoHandle(new (mem) JSArrayBuffer(
      runtime,
      *parentHandle,
      runtime->getHiddenClassForPrototypeRaw(
          *parentHandle,
          numOverlapSlots<JSArrayBuffer>() + ANONYMOUS_PROPERTY_SLOTS)));
}

PseudoHandle<JSArrayBuffer> JSArrayBuffer::create(
    Runtime *runtime,
    Handle<JSObject> parentHandle,
    ArrayBufferImpl &&impl) {
  auto res = create(runtime, parentHandle);
  res->impl_ = std::move(impl);
  runtime->getHeap().creditExternalMemory(res.get(), res->impl_->size());
  return res;
}

CallResult<Handle<JSArrayBuffer>> JSArrayBuffer::clone(
    Runtime *runtime,
    Handle<JSArrayBuffer> src,
    size_type srcOffset,
    size_type srcSize) {
  if (!src->attached()) {
    return runtime->raiseTypeError("Cannot clone from a detached buffer");
  }

  auto arr = runtime->makeHandle(JSArrayBuffer::create(
      runtime, Handle<JSObject>::vmcast(&runtime->arrayBufferPrototype)));

  // Don't need to zero out the data since we'll be copying into it immediately.
  if (arr->createDataBlock(runtime, srcSize, false) ==
      ExecutionStatus::EXCEPTION) {
    return ExecutionStatus::EXCEPTION;
  }
  if (srcSize != 0) {
    JSArrayBuffer::copyDataBlockBytes(*arr, 0, *src, srcOffset, srcSize);
  }
  return arr;
}

void JSArrayBuffer::copyDataBlockBytes(
    JSArrayBuffer *dst,
    size_type dstIndex,
    JSArrayBuffer *src,
    size_type srcIndex,
    size_type count) {
  assert(dst && src && "Must be copied between existing objects");
  if (count == 0) {
    // Don't do anything if there was no copy requested.
    return;
  }
  assert(
      dst->getDataBlock() != src->getDataBlock() &&
      "Cannot copy into the same block, must be different blocks");
  assert(
      srcIndex + count <= src->size() &&
      "Cannot copy more data out of a block than what exists");
  assert(
      dstIndex + count <= dst->size() &&
      "Cannot copy more data into a block than it has space for");
  // Copy from the other buffer.
  memcpy(
      dst->getDataBlockForWrite() + dstIndex,
      src->getDataBlock() + srcIndex,
      count);
}

JSArrayBuffer::JSArrayBuffer(
    Runtime *runtime,
    JSObject *parent,
    HiddenClass *clazz)
    : JSObject(runtime, &vt.base, parent, clazz) {}

JSArrayBuffer::~JSArrayBuffer() {
  // We expect this finalizer to be called only by _finalizerImpl,
  // below.  That detaches the buffer; here we just assert that it
  // has been detached, and that resources have been deallocated.
  assert(!impl_);
}

void JSArrayBuffer::_finalizeImpl(GCCell *cell, GC *gc) {
  auto *self = vmcast<JSArrayBuffer>(cell);
  // Need to untrack the native memory that may have been tracked by snapshots.
  if (self->impl_) {
    gc->getIDTracker().untrackNative(self->impl_->data());
  }
  self->detach(gc);
  self->~JSArrayBuffer();
}

size_t JSArrayBuffer::_mallocSizeImpl(GCCell *cell) {
  const auto *buffer = vmcast<JSArrayBuffer>(cell);
  return buffer->size();
}

gcheapsize_t JSArrayBuffer::_externalMemorySizeImpl(
    hermes::vm::GCCell const *cell) {
  const auto *buffer = vmcast<JSArrayBuffer>(cell);
  return buffer->size();
}

void JSArrayBuffer::_snapshotAddEdgesImpl(
    GCCell *cell,
    GC *gc,
    HeapSnapshot &snap) {
  auto *const self = vmcast<JSArrayBuffer>(cell);
  if (!self->impl_ || !self->impl_->data()) {
    return;
  }
  // While this is an internal edge, it is to a native node which is not
  // automatically added by the metadata.
  snap.addNamedEdge(
      HeapSnapshot::EdgeType::Internal,
      "backingStore",
      gc->getNativeID(self->impl_->data()));
  // The backing store just has numbers, so there's no edges to add here.
}

void JSArrayBuffer::_snapshotAddNodesImpl(
    GCCell *cell,
    GC *gc,
    HeapSnapshot &snap) {
  auto *const self = vmcast<JSArrayBuffer>(cell);
  if (!self->impl_) {
    return;
  }
  // Add the native node before the JSArrayBuffer node.
  snap.beginNode();
  auto &allocationLocationTracker = gc->getAllocationLocationTracker();
  snap.endNode(
      HeapSnapshot::NodeType::Native,
      "JSArrayBufferData",
      gc->getNativeID(self->impl_->data()),
      self->size(),
      allocationLocationTracker.isEnabled()
          ? allocationLocationTracker
                .getStackTracesTreeNodeForAlloc(self->impl_->data())
                ->id
          : 0);
}

void JSArrayBuffer::detach(GC *gc) {
  if (impl_) {
    gc->debitExternalMemory(this, impl_->size());
    impl_.reset();
  }
}

ExecutionStatus
JSArrayBuffer::createDataBlock(Runtime *runtime, size_type size, bool zero) {
  detach(&runtime->getHeap());
  // If an external allocation of this size would exceed the GC heap size,
  // raise RangeError.
  if (LLVM_UNLIKELY(
          size > std::numeric_limits<uint32_t>::max() ||
          !runtime->getHeap().canAllocExternalMemory(size))) {
    return runtime->raiseRangeError(
        "Cannot allocate a data block for the ArrayBuffer");
  }

  // Note the spec requires an empty ArrayBuffer to still be considered as
  // attached.
  impl_ = ArrayBufferImpl::allocate(size, zero);
  if (!impl_) {
    // Failed to allocate.
    return runtime->raiseRangeError(
        "Cannot allocate a data block for the ArrayBuffer");
  }
  runtime->getHeap().creditExternalMemory(this, size);
  return ExecutionStatus::RETURNED;
}

} // namespace vm
} // namespace hermes

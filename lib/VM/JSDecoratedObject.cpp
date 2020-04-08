/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "hermes/VM/JSDecoratedObject.h"

namespace hermes {
namespace vm {
//===----------------------------------------------------------------------===//
// class DecoratedObject

ObjectVTable DecoratedObject::vt{
    VTable(
        CellKind::DecoratedObjectKind,
        cellSize<DecoratedObject>(),
        DecoratedObject::_finalizeImpl,
        nullptr, /* markWeak */
        DecoratedObject::_mallocSizeImpl),
    DecoratedObject::_getOwnIndexedRangeImpl,
    DecoratedObject::_haveOwnIndexedImpl,
    DecoratedObject::_getOwnIndexedPropertyFlagsImpl,
    DecoratedObject::_getOwnIndexedImpl,
    DecoratedObject::_setOwnIndexedImpl,
    DecoratedObject::_deleteOwnIndexedImpl,
    DecoratedObject::_checkAllOwnIndexedImpl,
};

void DecoratedObjectBuildMeta(const GCCell *cell, Metadata::Builder &mb) {
  mb.addJSObjectOverlapSlots(JSObject::numOverlapSlots<DecoratedObject>());
  ObjectBuildMeta(cell, mb);
}

// static
PseudoHandle<DecoratedObject> DecoratedObject::create(
    Runtime *runtime,
    Handle<JSObject> parentHandle,
    std::unique_ptr<Decoration> decoration) {
  JSObjectAlloc<DecoratedObject, HasFinalizer::Yes> mem{runtime};
  return mem.initToPseudoHandle(new (mem) DecoratedObject(
      runtime,
      *parentHandle,
      runtime->getHiddenClassForPrototypeRaw(
          *parentHandle,
          numOverlapSlots<DecoratedObject>() + ANONYMOUS_PROPERTY_SLOTS),
      std::move(decoration)));
}

// static
size_t DecoratedObject::_mallocSizeImpl(GCCell *cell) {
  // TODO
  DecoratedObject *self = vmcast<DecoratedObject>(cell);
  (void)self;
  return 0;
}

} // namespace vm
} // namespace hermes

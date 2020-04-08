/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef HERMES_VM_JSDECORATEDOBJECT_H
#define HERMES_VM_JSDECORATEDOBJECT_H

#include "hermes/VM/JSObject.h"

#include <memory>

namespace hermes {
namespace vm {

/// DecoratedObject is a subclass of JSObject with a single field, and a
/// finalizer.
class DecoratedObject final : public JSObject {
 public:
  struct Decoration {
    // This is called when the decorated object is finalized.
    virtual ~Decoration() = default;
  };

  /// Allocate a DecoratedObject with the given prototype and decoration.
  /// If allocation fails, the GC declares an OOM.
  static PseudoHandle<DecoratedObject> create(
      Runtime *runtime,
      Handle<JSObject> parentHandle,
      std::unique_ptr<Decoration> decoration);

  /// Access the decoration.
  std::unique_ptr<Decoration> &getDecoration() {
    return decoration_;
  }

  const std::unique_ptr<Decoration> &getDecoration() const {
    return decoration_;
  }

  using Super = JSObject;
  static ObjectVTable vt;
  static bool classof(const GCCell *cell) {
    return cell->getKind() == CellKind::DecoratedObjectKind;
  }

 protected:
  ~DecoratedObject() = default;

  DecoratedObject(
      Runtime *runtime,
      JSObject *parent,
      HiddenClass *clazz,
      std::unique_ptr<Decoration> decoration)
      : JSObject(runtime, &vt.base, parent, clazz),
        decoration_{std::move(decoration)} {}

  static void _finalizeImpl(GCCell *cell, GC *) {
    auto *self = vmcast<DecoratedObject>(cell);
    self->~DecoratedObject();
  }

 private:
  static size_t _mallocSizeImpl(GCCell *cell);

  std::unique_ptr<Decoration> decoration_;
};

} // namespace vm
} // namespace hermes
#endif

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef shell_ShellModuleObjectWrapper_h
#define shell_ShellModuleObjectWrapper_h

#include "builtin/ModuleObject.h"  // js::ModuleObject
#include "js/Class.h"              // JSClass
#include "js/RootingAPI.h"         // JS::Handle
#include "vm/NativeObject.h"       // js::NativeObject

namespace js {

namespace shell {

// ModuleObject's accessors and methods are only for internal usage in
// js/src/builtin/Module.js, and they don't check arguments types.
//
// To use ModuleObject in tests, add a wrapper that checks arguments types.
class ShellModuleObjectWrapper : public js::NativeObject {
 public:
  using Target = ModuleObject;
  enum ModuleSlot { TargetSlot = 0, ModuleTypeSlot, SlotCount };
  static const JSClass class_;
  static ShellModuleObjectWrapper* create(
      JSContext* cx, JS::Handle<ModuleObject*> moduleObj,
      JS::ModuleType moduleType = JS::ModuleType::JavaScriptOrWasm);
  ModuleObject* get();
  JS::ModuleType getModuleType();
};

}  // namespace shell
}  // namespace js

#endif /* shell_ShellModuleObjectWrapper_h */

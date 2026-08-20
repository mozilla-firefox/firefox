/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_ParseRecordObject_h
#define builtin_ParseRecordObject_h

#include "js/HashTable.h"
#include "js/TracingAPI.h"
#include "vm/JSContext.h"

namespace js {

using JSONParseNode = JSString;

class ParseRecordObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, PARSE_NODE_SLOT, String, Undefined);
  JS_DEFINE_UNTYPED_SLOT(1, VALUE_SLOT);
  static constexpr uint32_t SLOT_COUNT = 2;

 public:
  static const JSClass class_;

  static ParseRecordObject* create(JSContext* cx, const Value& val);
  static ParseRecordObject* create(JSContext* cx,
                                   Handle<js::JSONParseNode*> parseNode,
                                   const Value& val);

  // The source text that was parsed for this record. According to the spec, we
  // don't track this for objects and arrays, so it will be a null pointer.
  JSONParseNode* getParseNode() const {
    const Value& slot = getReservedSlotTyped(PARSE_NODE_SLOT);
    return slot.isUndefined() ? nullptr : slot.toString();
  }

  // The original value corresponding to this record, used to determine if the
  // reviver function has modified it.
  const Value& getValue() const { return getReservedSlot(VALUE_SLOT); }

  void setValue(JS::Handle<JS::Value> value) {
    setReservedSlot(VALUE_SLOT, value);
  }

  bool hasValue() const { return !getValue().isUndefined(); }

  // For objects and arrays, the records for the members and elements
  // (respectively) are added to the ParseRecordObject.
  bool addEntries(JSContext* cx, Handle<JS::PropertyKey> key,
                  Handle<ParseRecordObject*> parseRecord);
};

}  // namespace js

#endif

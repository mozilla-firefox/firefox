/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * FinalizationRegistry objects allow a program to register to receive a
 * callback after a 'target' object dies. The callback is passed a 'held value'
 * (that hopefully doesn't entrain the target). An 'unregister token' is an
 * object which can be used to remove multiple previous registrations in one go.
 *
 * To arrange this, the following data structures are used:
 *
 *   +---------------------------------------+-------------------------------+
 *   |   FinalizationRegistry compartment    |   Target zone / compartment   |
 *   |                                       |                               |
 *   |     +------------------------------+  |     +------------------+      |
 *   |  +--+    FinalizationRegistry      |  |     |       Zone       |      |
 *   |  |  +---+----------------+---------+  |     +---------+--------+      |
 *   |  |      |                |            |               |               |
 *   |  |      v                v            |               v               |
 *   |  |  +---+---+  +---------+---------+  |  +------------+------------+  |
 *   |  |  |Record |  |   Registrations   |  |  |  FinalizationObservers  |  |
 *   |  |  |Vector |  |        map        |  |  +------------+------------+  |
 *   |  |  +---+---+  +-------------------+  |               |               |
 *   |  |      |      |   Weak    :Records|  |               |               |
 *   |  |      |      | unregister:Vector |  |               v               |
 *   |  |      |      |   token   :       |  |  +------------+------------+  |
 *   |  |      |      +--------------+----+  |  |      RecordMap map      |  |
 *   |  |      |                     |       |  +-------------------------+  |
 *   |  |      |                     |       |  |  Target  : ObserverList |  |
 *   |  |      |                     |       |  |  object  :              |  |
 *   |  |    * v                   * v       |  +----+-------------+------+  |
 *   |  |  +-------------------------+-+ *   |       |             |         |
 *   |  |  | FinalizationRecordObject  +<--------------------------+         |
 *   |  |  +---------------------------+     |       |                       |
 *   |  |  | Queue                     +--+  |       v                       |
 *   |  |  +---------------------------+  |  |  +----+-----+                 |
 *   |  |  | Held value                |  |  |  |  Target  |                 |
 *   |  |  +---------------------------+  |  |  | GC thing |                 |
 *   |  |                                 |  |  +----------+                 |
 *   |  +--------------+   +--------------+  |                               |
 *   |                 |   |                 |                               |
 *   |                 v   v                 |                               |
 *   |      +----------+---+----------+      |                               |
 *   |      | FinalizationQueueObject |      |                               |
 *   |      +-------------------------+      |                               |
 *   |                                       |                               |
 *   +---------------------------------------+-------------------------------+
 *
 * A FinalizationRegistry consists of several parts:
 *  - the FinalizationRegistry object that consumers see
 *  - zero or more FinalizationRecordObjects representing registered targets
 *  - a FinalizationQueue containing records for targets that have died, used to
 *    queue and call the cleanup callbacks
 *  - a map tracking unregister tokens and their associated records
 *
 * Registering a target with a FinalizationRegistry creates a FinalizationRecord
 * containing a pointer to the queue and the heldValue. This is added to the
 * registry's vector of registered targets and also to a linked list of
 * finalization observers which is used to actually track the target.
 *
 * When a target is registered an unregister token may be supplied. If so, this
 * is also recorded by the registry and is stored in a map of registrations.
 * They keys of this map are weakly held and do not keep the unregister token
 * alive.
 *
 * When targets are unregistered, the registration is looked up in the
 * registrations map and the corresponding records are cleared.

 * The finalization observer lists are swept during GC to check for records
 * associated with dying targets. For such targets the associated record list is
 * processed and each record is added to the FinalizationQueueObject. At a later
 * time this causes the client's cleanup callback to be run.
 */

#ifndef builtin_FinalizationRegistryObject_h
#define builtin_FinalizationRegistryObject_h

#include "gc/Barrier.h"
#include "gc/FinalizationObservers.h"
#include "js/GCVector.h"
#include "vm/NativeObject.h"

namespace js {

class FinalizationRegistryObject;
class FinalizationRecordObject;
class FinalizationQueueObject;

using HandleFinalizationRegistryObject = Handle<FinalizationRegistryObject*>;
using HandleFinalizationRecordObject = Handle<FinalizationRecordObject*>;
using HandleFinalizationQueueObject = Handle<FinalizationQueueObject*>;
using RootedFinalizationRegistryObject = Rooted<FinalizationRegistryObject*>;
using RootedFinalizationRecordObject = Rooted<FinalizationRecordObject*>;
using RootedFinalizationQueueObject = Rooted<FinalizationQueueObject*>;

// A finalization record: a pair of finalization queue and held value.
//
// A finalization record represents the registered interest of a finalization
// registry in a target's finalization.
//
// Finalization records created in the 'registered' state but may be
// unregistered. This happens when:
//  - the heldValue is passed to the registry's cleanup callback
//  - the registry's unregister method removes the registration
//
// Finalization records are added to a per-zone record map. They are removed
// when the record is queued for cleanup, or if the interest in finalization is
// cancelled. See FinalizationObservers::shouldRemoveRecord for the possible
// reasons.

class FinalizationRecordObject : public gc::ObserverListObject {
  JS_DEFINE_TYPED_SLOT(ObserverListObject::SlotCount + 0, QUEUE_SLOT, Object,
                       Undefined);
  JS_DEFINE_UNTYPED_SLOT(ObserverListObject::SlotCount + 1, HELD_VALUE_SLOT);
  // Used for assertions only.
  JS_DEFINE_TYPED_SLOT(ObserverListObject::SlotCount + 2, DEBUG_STATE_SLOT,
                       Int32, Undefined);
  static constexpr uint32_t SLOT_COUNT = ObserverListObject::SlotCount + 3;

 public:
  enum State { Unknown, InRecordMap, InQueue };

  static const JSClass class_;

  static FinalizationRecordObject* create(JSContext* cx,
                                          HandleFinalizationQueueObject queue,
                                          HandleValue heldValue);

  FinalizationQueueObject* queue() const;
  Value heldValue() const;
  bool isRegistered() const;

#ifdef DEBUG
  void setState(State state);
  State getState() const;
  bool isInRecordMap() const { return getState() == InRecordMap; }
  bool isInQueue() const { return getState() == InQueue; }
#endif

  void setInRecordMap(bool newValue);
  void setInQueue(bool newValue);
  void clear();

 private:
  static const JSClassOps classOps_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

using FinalizationRecordVector =
    GCVector<HeapPtr<FinalizationRecordObject*>, 1, js::CellAllocPolicy>;

// The JS FinalizationRegistry object itself.
class FinalizationRegistryObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, QUEUE_SLOT, Object, Undefined);
  JS_DEFINE_TYPED_SLOT(1, REGISTRATIONS_SLOT, Private, Undefined);
  JS_DEFINE_TYPED_SLOT(2, RECORDS_WITHOUT_TOKEN_SLOT, Private, Undefined);
  static constexpr uint32_t SLOT_COUNT = 3;

 public:
  using RegistrationsMap =
      GCHashMap<HeapPtr<Value>, FinalizationRecordVector, gc::WeakTargetHasher>;

  static const JSClass class_;
  static const JSClass protoClass_;

  FinalizationQueueObject* queue() const;
  RegistrationsMap* registrations() const;
  FinalizationRecordVector* recordsWithoutToken() const;

  void traceWeak(JSTracer* trc, bool* hasSymbolRegistrations);

  static bool unregisterRecord(FinalizationRecordObject* record);

 private:
  static const JSClassOps classOps_;
  static const ClassSpec classSpec_;
  static const JSFunctionSpec methods_[];
  static const JSPropertySpec properties_[];

  static bool construct(JSContext* cx, unsigned argc, Value* vp);
  static bool register_(JSContext* cx, unsigned argc, Value* vp);
  static bool unregister(JSContext* cx, unsigned argc, Value* vp);
  static bool cleanupSome(JSContext* cx, unsigned argc, Value* vp);

  static bool addRegistration(JSContext* cx,
                              HandleFinalizationRegistryObject registry,
                              HandleValue unregisterToken,
                              HandleFinalizationRecordObject record);
  static void removeRegistrationOnError(
      HandleFinalizationRegistryObject registry, HandleValue unregisterToken,
      HandleFinalizationRecordObject record);

  static void trace(JSTracer* trc, JSObject* obj);
  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

// Contains information about the cleanup callback and the records queued to
// be cleaned up. This is not exposed to content JS.
class FinalizationQueueObject : public NativeObject {
  JS_DEFINE_TYPED_SLOT(0, CLEANUP_CALLBACK_SLOT, Object, Undefined);
  JS_DEFINE_TYPED_SLOT(1, INCUMBENT_GLOBAL_REPRESENTATIVE_SLOT, Object, Null,
                       Undefined);
  JS_DEFINE_TYPED_SLOT(2, RECORDS_TO_BE_CLEANED_UP_SLOT, Private, Undefined);
  JS_DEFINE_TYPED_SLOT(3, IS_QUEUED_FOR_CLEANUP_SLOT, Boolean);
  JS_DEFINE_TYPED_SLOT(4, DO_CLEANUP_FUNCTION_SLOT, Object, Undefined);
  JS_DEFINE_TYPED_SLOT(5, HAS_REGISTRY_SLOT, Boolean);
  static constexpr uint32_t SLOT_COUNT = 6;

  enum DoCleanupFunctionSlots {
    DoCleanupFunction_QueueSlot = 0,
  };

 public:
  // Don't track memory for this vector against the zone as it can grow and be
  // deleted in the same GC cycle.
  using QueuedRecordVector =
      GCVector<HeapPtr<FinalizationRecordObject*>, 1, SystemAllocPolicy>;

  static const JSClass class_;

  JSObject* cleanupCallback() const;
  JSObject* getIncumbentGlobalRepresentative() const;
  bool hasRecordsToCleanUp() const;
  QueuedRecordVector* recordsToBeCleanedUp() const;
  bool isQueuedForCleanup() const;
  JSFunction* doCleanupFunction() const;
  bool hasRegistry() const;

  void queueRecordToBeCleanedUp(FinalizationRecordObject* record);
  void setQueuedForCleanup(bool value);

  void setHasRegistry(bool newValue);
  void clear();

  static FinalizationQueueObject* create(JSContext* cx,
                                         HandleObject cleanupCallback);

  static bool cleanupQueuedRecords(JSContext* cx,
                                   HandleFinalizationQueueObject registry,
                                   HandleObject callback = nullptr);

 private:
  static const JSClassOps classOps_;

  static bool doCleanup(JSContext* cx, unsigned argc, Value* vp);

  static void trace(JSTracer* trc, JSObject* obj);
  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

}  // namespace js

#endif /* builtin_FinalizationRegistryObject_h */

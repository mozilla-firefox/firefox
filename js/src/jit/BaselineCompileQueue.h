/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_BaselineCompileQueue_h
#define jit_BaselineCompileQueue_h

#include "gc/Barrier.h"
#include "jit/JitOptions.h"

class JSScript;

namespace js {
namespace jit {

class BaselineCompileQueue {
 public:
  static constexpr uint32_t MaxCapacity = 64;

 private:
  uint32_t numQueued_ = 0;
  HeapPtr<JSScript*> queue_[MaxCapacity];

 public:
  uint32_t numQueued() const { return numQueued_; }

  static constexpr size_t offsetOfQueue() {
    return offsetof(BaselineCompileQueue, queue_);
  }
  static constexpr size_t offsetOfNumQueued() {
    return offsetof(BaselineCompileQueue, numQueued_);
  }

  JSScript* pop();
  void clear();

  bool isEmpty() const { return numQueued_ == 0; }

  bool enqueue(JSScript* script);

#ifdef DEBUG
  void assertInvariants() const;
#else
  void assertInvariants() const {}
#endif

  void trace(JSTracer* trc);
  void remove(JSScript* script);
};

}  // namespace jit
}  // namespace js

#endif  // jit_BaselineCompileQueue_h

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_fuzzing_Nyx_h
#define mozilla_fuzzing_Nyx_h

#include <stdint.h>
#include <atomic>
#include <list>

#ifndef NYX_DISALLOW_COPY_AND_ASSIGN
#  define NYX_DISALLOW_COPY_AND_ASSIGN(T) \
    T(const T&);                          \
    void operator=(const T&)
#endif

namespace mozilla {

class MallocAllocPolicy;
template <class T, size_t MinInlineCapacity, class AllocPolicy>
class Vector;

namespace fuzzing {

class Nyx {
 public:
  static Nyx& instance();

  void start(void);
  bool started(void);
  bool is_enabled(const char* identifier);
  bool is_replay();
  uint32_t get_data(uint8_t* data, uint32_t size);
  uint32_t get_raw_data(uint8_t* data, uint32_t size);
  uint32_t get_raw_data(uint8_t** data);
  void release(uint32_t iterations = 1);
  void handle_event(const char* type, const char* file, int line,
                    const char* reason);
  void dump_file(void* buffer, size_t len, const char* filename);

 private:
  std::atomic<bool> mInited;

  std::atomic<bool> mReplayMode;
  std::list<Vector<uint8_t, 0, MallocAllocPolicy>*> mReplayBuffers;
  Vector<uint8_t, 0, MallocAllocPolicy>* mRawReplayBuffer;

  Nyx();
  NYX_DISALLOW_COPY_AND_ASSIGN(Nyx);
};

}  // namespace fuzzing
}  // namespace mozilla

#endif /* mozilla_fuzzing_Nyx_h */

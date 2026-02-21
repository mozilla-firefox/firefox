/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/SPSCQueue.h"
#include "mozilla/PodOperations.h"
#include <vector>
#include <cstdint>
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#endif

using namespace mozilla;

/* Generate a monotonically increasing sequence of numbers. */
template <typename T>
class SequenceGenerator {
 public:
  SequenceGenerator() = default;
  void Get(T* aElements, size_t aCount) {
    for (size_t i = 0; i < aCount; i++) {
      aElements[i] = static_cast<T>(mIndex);
      mIndex++;
    }
  }
  void Rewind(size_t aCount) { mIndex -= aCount; }

 private:
  size_t mIndex = 0;
};

/* Checks that a sequence is monotonically increasing. */
template <typename T>
class SequenceVerifier {
 public:
  SequenceVerifier() = default;
  void Check(T* aElements, size_t aCount) {
    for (size_t i = 0; i < aCount; i++) {
      if (aElements[i] != static_cast<T>(mIndex)) {
        std::cerr << "Element " << i << " is different. Expected "
                  << static_cast<T>(mIndex) << ", got " << aElements[i] << "."
                  << std::endl;
        MOZ_RELEASE_ASSERT(false);
      }
      mIndex++;
    }
  }

 private:
  size_t mIndex = 0;
};

const int BLOCK_SIZE = 127;

template <typename T>
void TestRing(int capacity) {
  SPSCQueue<T> buf(capacity);
  std::unique_ptr<T[]> seq(new T[capacity]);
  SequenceGenerator<T> gen;
  SequenceVerifier<T> checker;

  int iterations = 1002;

  while (iterations--) {
    gen.Get(seq.get(), BLOCK_SIZE);
    int rv = buf.Enqueue(seq.get(), BLOCK_SIZE);
    MOZ_RELEASE_ASSERT(rv == BLOCK_SIZE);
    PodZero(seq.get(), BLOCK_SIZE);
    rv = buf.Dequeue(seq.get(), BLOCK_SIZE);
    MOZ_RELEASE_ASSERT(rv == BLOCK_SIZE);
    checker.Check(seq.get(), BLOCK_SIZE);
  }
}

void Delay() {
  // On Windows and x86 Android, the timer resolution is so bad that, even if
  // we used `timeBeginPeriod(1)`, any nonzero sleep from the test's inner loops
  // would make this program take far too long.
#ifdef _WIN32
  Sleep(0);
#elif defined(ANDROID)
  std::this_thread::sleep_for(std::chrono::microseconds(0));
#else
  std::this_thread::sleep_for(std::chrono::microseconds(10));
#endif
}

template <typename T>
void TestRingMultiThread(int capacity) {
  SPSCQueue<T> buf(capacity);
  SequenceVerifier<T> checker;
  std::unique_ptr<T[]> outBuffer(new T[capacity]);

  std::thread t([&buf, capacity] {
    int iterations = 1002;
    std::unique_ptr<T[]> inBuffer(new T[capacity]);
    SequenceGenerator<T> gen;

    while (iterations--) {
      Delay();
      gen.Get(inBuffer.get(), BLOCK_SIZE);
      int rv = buf.Enqueue(inBuffer.get(), BLOCK_SIZE);
      MOZ_RELEASE_ASSERT(rv <= BLOCK_SIZE);
      if (rv != BLOCK_SIZE) {
        gen.Rewind(BLOCK_SIZE - rv);
      }
    }
  });

  int remaining = 1002;

  while (remaining--) {
    Delay();
    int rv = buf.Dequeue(outBuffer.get(), BLOCK_SIZE);
    MOZ_RELEASE_ASSERT(rv <= BLOCK_SIZE);
    checker.Check(outBuffer.get(), rv);
  }

  t.join();
}

template <typename T>
void BasicAPITest(T& ring) {
  MOZ_RELEASE_ASSERT(ring.Capacity() == 128);

  MOZ_RELEASE_ASSERT(ring.AvailableRead() == 0);
  MOZ_RELEASE_ASSERT(ring.AvailableWrite() == 128);

  int rv = ring.EnqueueDefault(63);

  MOZ_RELEASE_ASSERT(rv == 63);
  MOZ_RELEASE_ASSERT(ring.AvailableRead() == 63);
  MOZ_RELEASE_ASSERT(ring.AvailableWrite() == 65);

  rv = ring.EnqueueDefault(65);

  MOZ_RELEASE_ASSERT(rv == 65);
  MOZ_RELEASE_ASSERT(ring.AvailableRead() == 128);
  MOZ_RELEASE_ASSERT(ring.AvailableWrite() == 0);

  rv = ring.Dequeue(nullptr, 63);

  MOZ_RELEASE_ASSERT(ring.AvailableRead() == 65);
  MOZ_RELEASE_ASSERT(ring.AvailableWrite() == 63);

  rv = ring.Dequeue(nullptr, 65);

  MOZ_RELEASE_ASSERT(ring.AvailableRead() == 0);
  MOZ_RELEASE_ASSERT(ring.AvailableWrite() == 128);
}

const size_t RING_BUFFER_SIZE = 128;
const size_t ENQUEUE_SIZE = RING_BUFFER_SIZE / 2;

void TestResetAPI() {
  SPSCQueue<float> ring(RING_BUFFER_SIZE);
  std::thread p([&ring] {
    std::unique_ptr<float[]> inBuffer(new float[ENQUEUE_SIZE]);
    int rv = ring.Enqueue(inBuffer.get(), ENQUEUE_SIZE);
    MOZ_RELEASE_ASSERT(rv > 0);
  });

  p.join();

  std::thread c([&ring] {
    std::unique_ptr<float[]> outBuffer(new float[ENQUEUE_SIZE]);
    int rv = ring.Dequeue(outBuffer.get(), ENQUEUE_SIZE);
    MOZ_RELEASE_ASSERT(rv > 0);
  });

  c.join();

  // Enqueue with a different thread. We reset the thread ID in the ring buffer,
  // this should work.
  std::thread p2([&ring] {
    ring.ResetProducerThreadId();
    std::unique_ptr<float[]> inBuffer(new float[ENQUEUE_SIZE]);
    int rv = ring.Enqueue(inBuffer.get(), ENQUEUE_SIZE);
    MOZ_RELEASE_ASSERT(rv > 0);
  });

  p2.join();

  // Dequeue with a different thread. We reset the thread ID in the ring buffer,
  // this should work.
  std::thread c2([&ring] {
    ring.ResetConsumerThreadId();
    std::unique_ptr<float[]> outBuffer(new float[ENQUEUE_SIZE]);
    int rv = ring.Dequeue(outBuffer.get(), ENQUEUE_SIZE);
    MOZ_RELEASE_ASSERT(rv > 0);
  });

  c2.join();

  // Similarly, but do the Enqueues without a Dequeue in between, since a
  // Dequeue could affect memory ordering.
  std::thread p4;
  std::thread p3([&] {
    ring.ResetProducerThreadId();
    std::unique_ptr<float[]> inBuffer(new float[ENQUEUE_SIZE]);
    int rv = ring.Enqueue(inBuffer.get(), ENQUEUE_SIZE);
    MOZ_RELEASE_ASSERT(rv > 0);
    p4 = std::thread([&ring] {
      ring.ResetProducerThreadId();
      std::unique_ptr<float[]> inBuffer(new float[ENQUEUE_SIZE]);
      int rv = ring.Enqueue(inBuffer.get(), ENQUEUE_SIZE);
      MOZ_RELEASE_ASSERT(rv > 0);
    });
  });

  p3.join();
  p4.join();

  std::thread c4;
  std::thread c3([&] {
    ring.ResetConsumerThreadId();
    std::unique_ptr<float[]> outBuffer(new float[ENQUEUE_SIZE]);
    int rv = ring.Dequeue(outBuffer.get(), ENQUEUE_SIZE);
    MOZ_RELEASE_ASSERT(rv > 0);
    c4 = std::thread([&ring] {
      ring.ResetConsumerThreadId();
      std::unique_ptr<float[]> outBuffer(new float[ENQUEUE_SIZE]);
      int rv = ring.Dequeue(outBuffer.get(), ENQUEUE_SIZE);
      MOZ_RELEASE_ASSERT(rv > 0);
    });
  });

  c3.join();
  c4.join();
}

void TestMove() {
  const size_t ELEMENT_COUNT = 16;
  struct Thing {
    Thing() : mStr("") {}
    explicit Thing(const std::string& aStr) : mStr(aStr) {}
    Thing(Thing&& aOtherThing) {
      mStr = std::move(aOtherThing.mStr);
      // aOtherThing.mStr.clear();
    }
    Thing& operator=(Thing&& aOtherThing) {
      mStr = std::move(aOtherThing.mStr);
      return *this;
    }
    std::string mStr;
  };

  std::vector<Thing> vec_in;
  std::vector<Thing> vec_out;

  for (uint32_t i = 0; i < ELEMENT_COUNT; i++) {
    vec_in.push_back(Thing(std::to_string(i)));
    vec_out.push_back(Thing());
  }

  SPSCQueue<Thing> queue(ELEMENT_COUNT);

  int rv = queue.Enqueue(&vec_in[0], ELEMENT_COUNT);
  MOZ_RELEASE_ASSERT(rv == ELEMENT_COUNT);

  // Check that we've moved the std::string into the queue.
  for (uint32_t i = 0; i < ELEMENT_COUNT; i++) {
    MOZ_RELEASE_ASSERT(vec_in[i].mStr.empty());
  }

  rv = queue.Dequeue(&vec_out[0], ELEMENT_COUNT);
  MOZ_RELEASE_ASSERT(rv == ELEMENT_COUNT);

  for (uint32_t i = 0; i < ELEMENT_COUNT; i++) {
    MOZ_RELEASE_ASSERT(std::stoul(vec_out[i].mStr) == i);
  }
}

int main() {
  const int minCapacity = 199;
  const int maxCapacity = 1277;
  const int capacityIncrement = 27;

  SPSCQueue<float> q1(128);
  BasicAPITest(q1);
  SPSCQueue<char> q2(128);
  BasicAPITest(q2);

  for (uint32_t i = minCapacity; i < maxCapacity; i += capacityIncrement) {
    TestRing<uint32_t>(i);
    TestRingMultiThread<uint32_t>(i);
    TestRing<float>(i);
    TestRingMultiThread<float>(i);
  }

  TestResetAPI();
  TestMove();

  return 0;
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdint.h>
#include <type_traits>

#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"

using mozilla::Atomic;
using mozilla::MemoryOrdering;
using mozilla::Relaxed;
using mozilla::ReleaseAcquire;
using mozilla::SequentiallyConsistent;

#define A(a, b) MOZ_RELEASE_ASSERT(a, b)

template <typename T, MemoryOrdering Order>
static void TestTypeWithOrdering() {
  Atomic<T, Order> atomic(5);
  A(atomic == 5, "Atomic variable did not initialize");

  // Test load
  static_assert(std::is_same_v<decltype(atomic.load()), T>);
  A(atomic.load() == T(5), "Atomic load did not work");

  // Test atomic increment
  A(++atomic == T(6), "Atomic increment did not work");
  A(atomic++ == T(6), "Atomic post-increment did not work");
  A(atomic == T(7), "Atomic post-increment did not work");

  // Test atomic decrement
  A(--atomic == 6, "Atomic decrement did not work");
  A(atomic-- == 6, "Atomic post-decrement did not work");
  A(atomic == 5, "Atomic post-decrement did not work");

  // Test other arithmetic.
  T result;
  result = (atomic += T(5));
  A(atomic == T(10), "Atomic += did not work");
  A(result == T(10), "Atomic += returned the wrong value");
  result = (atomic -= T(3));
  A(atomic == T(7), "Atomic -= did not work");
  A(result == T(7), "Atomic -= returned the wrong value");

  // Test assignment
  result = (atomic = T(5));
  A(atomic == T(5), "Atomic assignment failed");
  A(result == T(5), "Atomic assignment returned the wrong value");

  // Test logical operations.
  result = (atomic ^= T(2));
  A(atomic == T(7), "Atomic ^= did not work");
  A(result == T(7), "Atomic ^= returned the wrong value");
  result = (atomic ^= T(4));
  A(atomic == T(3), "Atomic ^= did not work");
  A(result == T(3), "Atomic ^= returned the wrong value");
  result = (atomic |= T(8));
  A(atomic == T(11), "Atomic |= did not work");
  A(result == T(11), "Atomic |= returned the wrong value");
  result = (atomic |= T(8));
  A(atomic == T(11), "Atomic |= did not work");
  A(result == T(11), "Atomic |= returned the wrong value");
  result = (atomic &= T(12));
  A(atomic == T(8), "Atomic &= did not work");
  A(result == T(8), "Atomic &= returned the wrong value");

  // Test exchange.
  atomic = T(30);
  result = atomic.exchange(42);
  A(atomic == T(42), "Atomic exchange did not work");
  A(result == T(30), "Atomic exchange returned the wrong value");

  // Test CAS.
  atomic = T(1);
  bool boolResult = atomic.compareExchange(0, 2);
  A(!boolResult, "CAS should have returned false.");
  A(atomic == T(1), "CAS shouldn't have done anything.");

  boolResult = atomic.compareExchange(1, 42);
  A(boolResult, "CAS should have succeeded.");
  A(atomic == T(42), "CAS should have changed atomic's value.");
}

template <typename T, MemoryOrdering Order>
static void TestPointerWithOrdering() {
  T array1[10];
  Atomic<T*, Order> atomic(array1);
  A(atomic == array1, "Atomic variable did not initialize");

  // Test load
  static_assert(std::is_same_v<decltype(atomic.load()), T*>);
  A(atomic.load() == array1, "Atomic load did not work");

  // Test atomic increment
  A(++atomic == array1 + 1, "Atomic increment did not work");
  A(atomic++ == array1 + 1, "Atomic post-increment did not work");
  A(atomic == array1 + 2, "Atomic post-increment did not work");

  // Test atomic decrement
  A(--atomic == array1 + 1, "Atomic decrement did not work");
  A(atomic-- == array1 + 1, "Atomic post-decrement did not work");
  A(atomic == array1, "Atomic post-decrement did not work");

  // Test other arithmetic operations
  T* result;
  result = (atomic += 2);
  A(atomic == array1 + 2, "Atomic += did not work");
  A(result == array1 + 2, "Atomic += returned the wrong value");
  result = (atomic -= 1);
  A(atomic == array1 + 1, "Atomic -= did not work");
  A(result == array1 + 1, "Atomic -= returned the wrong value");

  // Test stores
  result = (atomic = array1);
  A(atomic == array1, "Atomic assignment did not work");
  A(result == array1, "Atomic assignment returned the wrong value");

  // Test exchange
  atomic = array1 + 2;
  result = atomic.exchange(array1);
  A(atomic == array1, "Atomic exchange did not work");
  A(result == array1 + 2, "Atomic exchange returned the wrong value");

  atomic = array1;
  bool boolResult = atomic.compareExchange(array1 + 1, array1 + 2);
  A(!boolResult, "CAS should have returned false.");
  A(atomic == array1, "CAS shouldn't have done anything.");

  boolResult = atomic.compareExchange(array1, array1 + 3);
  A(boolResult, "CAS should have succeeded.");
  A(atomic == array1 + 3, "CAS should have changed atomic's value.");
}

enum EnumType {
  EnumType_0 = 0,
  EnumType_1 = 1,
  EnumType_2 = 2,
  EnumType_3 = 3
};

template <MemoryOrdering Order>
static void TestEnumWithOrdering() {
  Atomic<EnumType, Order> atomic(EnumType_2);
  A(atomic == EnumType_2, "Atomic variable did not initialize");

  // Test load
  static_assert(std::is_same_v<decltype(atomic.load()), EnumType>);
  A(atomic.load() == EnumType_2, "Atomic load did not work");

  // Test assignment
  EnumType result;
  result = (atomic = EnumType_3);
  A(atomic == EnumType_3, "Atomic assignment failed");
  A(result == EnumType_3, "Atomic assignment returned the wrong value");

  // Test exchange.
  atomic = EnumType_1;
  result = atomic.exchange(EnumType_2);
  A(atomic == EnumType_2, "Atomic exchange did not work");
  A(result == EnumType_1, "Atomic exchange returned the wrong value");

  // Test CAS.
  atomic = EnumType_1;
  bool boolResult = atomic.compareExchange(EnumType_0, EnumType_2);
  A(!boolResult, "CAS should have returned false.");
  A(atomic == EnumType_1, "CAS shouldn't have done anything.");

  boolResult = atomic.compareExchange(EnumType_1, EnumType_3);
  A(boolResult, "CAS should have succeeded.");
  A(atomic == EnumType_3, "CAS should have changed atomic's value.");
}

enum class EnumClass : uint32_t {
  Value0 = 0,
  Value1 = 1,
  Value2 = 2,
  Value3 = 3
};

template <MemoryOrdering Order>
static void TestEnumClassWithOrdering() {
  Atomic<EnumClass, Order> atomic(EnumClass::Value2);
  A(atomic == EnumClass::Value2, "Atomic variable did not initialize");

  // Test load
  static_assert(std::is_same_v<decltype(atomic.load()), EnumClass>);
  A(atomic.load() == EnumClass::Value2, "Atomic load did not work");

  // Test assignment
  EnumClass result;
  result = (atomic = EnumClass::Value3);
  A(atomic == EnumClass::Value3, "Atomic assignment failed");
  A(result == EnumClass::Value3, "Atomic assignment returned the wrong value");

  // Test exchange.
  atomic = EnumClass::Value1;
  result = atomic.exchange(EnumClass::Value2);
  A(atomic == EnumClass::Value2, "Atomic exchange did not work");
  A(result == EnumClass::Value1, "Atomic exchange returned the wrong value");

  // Test CAS.
  atomic = EnumClass::Value1;
  bool boolResult =
      atomic.compareExchange(EnumClass::Value0, EnumClass::Value2);
  A(!boolResult, "CAS should have returned false.");
  A(atomic == EnumClass::Value1, "CAS shouldn't have done anything.");

  boolResult = atomic.compareExchange(EnumClass::Value1, EnumClass::Value3);
  A(boolResult, "CAS should have succeeded.");
  A(atomic == EnumClass::Value3, "CAS should have changed atomic's value.");
}

template <MemoryOrdering Order>
static void TestBoolWithOrdering() {
  Atomic<bool, Order> atomic(false);
  A(atomic == false, "Atomic variable did not initialize");

  // Test load
  static_assert(std::is_same_v<decltype(atomic.load()), bool>);
  A(atomic.load() == false, "Atomic load did not work");

  // Test assignment
  bool result;
  result = (atomic = true);
  A(atomic == true, "Atomic assignment failed");
  A(result == true, "Atomic assignment returned the wrong value");

  // Test exchange.
  atomic = false;
  result = atomic.exchange(true);
  A(atomic == true, "Atomic exchange did not work");
  A(result == false, "Atomic exchange returned the wrong value");

  // Test CAS.
  atomic = false;
  bool boolResult = atomic.compareExchange(true, false);
  A(!boolResult, "CAS should have returned false.");
  A(atomic == false, "CAS shouldn't have done anything.");

  boolResult = atomic.compareExchange(false, true);
  A(boolResult, "CAS should have succeeded.");
  A(atomic == true, "CAS should have changed atomic's value.");
}

template <MemoryOrdering Order>
static void TestDoubleWithOrdering() {
  Atomic<double, Order> atomic(1.5);
  A(atomic == 1.5, "Atomic variable did not initialize");

  // Test load
  static_assert(std::is_same_v<decltype(atomic.load()), double>);
  A(atomic.load() == 1.5, "Atomic load did not work");

  // Test assignment
  double result;
  result = (atomic = 2.5);
  A(atomic.load() == 2.5, "Atomic assignment failed");
  A(result == 2.5, "Atomic assignment returned the wrong value");
}

template <typename T>
static void TestType() {
  TestTypeWithOrdering<T, SequentiallyConsistent>();
  TestTypeWithOrdering<T, ReleaseAcquire>();
  TestTypeWithOrdering<T, Relaxed>();
}

template <typename T>
static void TestPointer() {
  TestPointerWithOrdering<T, SequentiallyConsistent>();
  TestPointerWithOrdering<T, ReleaseAcquire>();
  TestPointerWithOrdering<T, Relaxed>();
}

static void TestEnum() {
  TestEnumWithOrdering<SequentiallyConsistent>();
  TestEnumWithOrdering<ReleaseAcquire>();
  TestEnumWithOrdering<Relaxed>();

  TestEnumClassWithOrdering<SequentiallyConsistent>();
  TestEnumClassWithOrdering<ReleaseAcquire>();
  TestEnumClassWithOrdering<Relaxed>();
}

static void TestBool() {
  TestBoolWithOrdering<SequentiallyConsistent>();
  TestBoolWithOrdering<ReleaseAcquire>();
  TestBoolWithOrdering<Relaxed>();
}

static void TestDouble() {
  TestDoubleWithOrdering<SequentiallyConsistent>();
  TestDoubleWithOrdering<ReleaseAcquire>();
  TestDoubleWithOrdering<Relaxed>();
}

#undef A

int main() {
  TestType<uint32_t>();
  TestType<int32_t>();
  TestType<uint64_t>();
  TestType<int64_t>();
  TestType<intptr_t>();
  TestType<uintptr_t>();
  TestPointer<int>();
  TestPointer<float>();
  TestPointer<uint16_t*>();
  TestPointer<uint32_t*>();
  TestEnum();
  TestBool();
  TestDouble();
  return 0;
}

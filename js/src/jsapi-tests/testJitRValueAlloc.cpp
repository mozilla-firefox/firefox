/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/Snapshots.h"
#include "jsapi-tests/tests.h"

using namespace js;
using namespace js::jit;

// These tests are checking that all slots of the current architecture can all
// be encoded and decoded correctly.  We iterate on all registers and on many
// fake stack locations (Fibonacci).
static RValueAllocation Read(const RValueAllocation& slot) {
  CompactBufferWriter writer;
  slot.write(writer);

  // Call hash to run its assertions.
  slot.hash();

  CompactBufferReader reader(writer);
  return RValueAllocation::read(reader);
}

class Fibonacci {
  class Iterator {
   public:
    // std::iterator traits.
    using iterator_category = std::input_iterator_tag;
    using value_type = int32_t;
    using difference_type = int32_t;
    using pointer = value_type*;
    using reference = value_type&;

   private:
    uint32_t value_{};
    uint32_t last_{};

    Iterator() = default;
    Iterator(value_type value, value_type last) : value_(value), last_(last) {}

    friend class Fibonacci;

   public:
    Iterator& operator++() {
      auto next = value_ + last_;
      if (next <= static_cast<uint32_t>(INT32_MAX)) {
        last_ = value_;
        value_ = next;
      } else {
        *this = Iterator{};
      }
      return *this;
    }

    bool operator==(const Iterator& other) const = default;

    auto operator*() const { return static_cast<int32_t>(value_); }
  };

 public:
  auto begin() { return Iterator{0, 1}; }

  auto end() { return Iterator{}; }
};

BEGIN_TEST(testJitRValueAlloc_Double) {
  RValueAllocation s;
  for (uint32_t i = 0; i < FloatRegisters::Total; i++) {
    s = RValueAllocation::Double(FloatRegister::FromCode(i));
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_Double)

BEGIN_TEST(testJitRValueAlloc_FloatReg) {
  RValueAllocation s;
  for (uint32_t i = 0; i < FloatRegisters::Total; i++) {
    s = RValueAllocation::Float32(FloatRegister::FromCode(i));
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_FloatReg)

BEGIN_TEST(testJitRValueAlloc_FloatStack) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
    s = RValueAllocation::Float32(i);
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_FloatStack)

BEGIN_TEST(testJitRValueAlloc_TypedReg) {
  RValueAllocation s;
  for (uint32_t i = 0; i < Registers::Total; i++) {
#define FOR_EACH_JSVAL(_)       \
  /* _(JSVAL_TYPE_DOUBLE) */    \
  _(JSVAL_TYPE_INT32)           \
  /* _(JSVAL_TYPE_UNDEFINED) */ \
  _(JSVAL_TYPE_BOOLEAN)         \
  /* _(JSVAL_TYPE_MAGIC) */     \
  _(JSVAL_TYPE_STRING)          \
  _(JSVAL_TYPE_SYMBOL)          \
  _(JSVAL_TYPE_BIGINT)          \
  /* _(JSVAL_TYPE_NULL) */      \
  _(JSVAL_TYPE_OBJECT)

#define CHECK_WITH_JSVAL(jsval)                              \
  s = RValueAllocation::Typed(jsval, Register::FromCode(i)); \
  CHECK(s == Read(s));

    FOR_EACH_JSVAL(CHECK_WITH_JSVAL)
#undef CHECK_WITH_JSVAL
#undef FOR_EACH_JSVAL
  }
  return true;
}
END_TEST(testJitRValueAlloc_TypedReg)

BEGIN_TEST(testJitRValueAlloc_TypedStack) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
#define FOR_EACH_JSVAL(_)       \
  _(JSVAL_TYPE_DOUBLE)          \
  _(JSVAL_TYPE_INT32)           \
  /* _(JSVAL_TYPE_UNDEFINED) */ \
  _(JSVAL_TYPE_BOOLEAN)         \
  /* _(JSVAL_TYPE_MAGIC) */     \
  _(JSVAL_TYPE_STRING)          \
  _(JSVAL_TYPE_SYMBOL)          \
  _(JSVAL_TYPE_BIGINT)          \
  /* _(JSVAL_TYPE_NULL) */      \
  _(JSVAL_TYPE_OBJECT)

#define CHECK_WITH_JSVAL(jsval)          \
  s = RValueAllocation::Typed(jsval, i); \
  CHECK(s == Read(s));

    FOR_EACH_JSVAL(CHECK_WITH_JSVAL)
#undef CHECK_WITH_JSVAL
#undef FOR_EACH_JSVAL
  }
  return true;
}
END_TEST(testJitRValueAlloc_TypedStack)

#if defined(JS_NUNBOX32)

BEGIN_TEST(testJitRValueAlloc_UntypedRegReg) {
  RValueAllocation s;
  for (uint32_t i = 0; i < Registers::Total; i++) {
    for (uint32_t j = 0; j < Registers::Total; j++) {
      if (i == j) {
        continue;
      }
      s = RValueAllocation::Untyped(Register::FromCode(i),
                                    Register::FromCode(j));
      MOZ_ASSERT(s == Read(s));
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_UntypedRegReg)

BEGIN_TEST(testJitRValueAlloc_UntypedRegStack) {
  RValueAllocation s;
  for (uint32_t i = 0; i < Registers::Total; i++) {
    for (auto j : Fibonacci{}) {
      s = RValueAllocation::Untyped(Register::FromCode(i), j);
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_UntypedRegStack)

BEGIN_TEST(testJitRValueAlloc_UntypedStackReg) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
    for (uint32_t j = 0; j < Registers::Total; j++) {
      s = RValueAllocation::Untyped(i, Register::FromCode(j));
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_UntypedStackReg)

BEGIN_TEST(testJitRValueAlloc_UntypedStackStack) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
    for (auto j : Fibonacci{}) {
      s = RValueAllocation::Untyped(i, j);
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_UntypedStackStack)

#else

BEGIN_TEST(testJitRValueAlloc_UntypedReg) {
  RValueAllocation s;
  for (uint32_t i = 0; i < Registers::Total; i++) {
    s = RValueAllocation::Untyped(Register::FromCode(i));
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_UntypedReg)

BEGIN_TEST(testJitRValueAlloc_UntypedStack) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
    s = RValueAllocation::Untyped(i);
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_UntypedStack)

#endif

BEGIN_TEST(testJitRValueAlloc_UndefinedAndNull) {
  RValueAllocation s;
  s = RValueAllocation::Undefined();
  CHECK(s == Read(s));
  s = RValueAllocation::Null();
  CHECK(s == Read(s));
  return true;
}
END_TEST(testJitRValueAlloc_UndefinedAndNull)

BEGIN_TEST(testJitRValueAlloc_ConstantPool) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
    s = RValueAllocation::ConstantPool(i);
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_ConstantPool)

BEGIN_TEST(testJitRValueAlloc_NurseryObject) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
    s = RValueAllocation::NurseryObject(i);
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_NurseryObject)

BEGIN_TEST(testJitRValueAlloc_Int64Cst) {
  for (auto i : Fibonacci{}) {
    for (auto j : Fibonacci{}) {
      auto s = RValueAllocation::Int64Constant(i, j);
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_Int64Cst)

#if defined(JS_NUNBOX32)
BEGIN_TEST(testJitRValueAlloc_Int64RegReg) {
  RValueAllocation s;
  for (uint32_t i = 0; i < Registers::Total; i++) {
    for (uint32_t j = 0; j < Registers::Total; j++) {
      if (i == j) {
        continue;
      }
      s = RValueAllocation::Int64(Register::FromCode(i), Register::FromCode(j));
      MOZ_ASSERT(s == Read(s));
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_Int64RegReg)

BEGIN_TEST(testJitRValueAlloc_Int64RegStack) {
  RValueAllocation s;
  for (uint32_t i = 0; i < Registers::Total; i++) {
    for (auto j : Fibonacci{}) {
      s = RValueAllocation::Int64(Register::FromCode(i), j);
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_Int64RegStack)

BEGIN_TEST(testJitRValueAlloc_Int64StackReg) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
    for (uint32_t j = 0; j < Registers::Total; j++) {
      s = RValueAllocation::Int64(i, Register::FromCode(j));
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_Int64StackReg)

BEGIN_TEST(testJitRValueAlloc_Int64StackStack) {
  RValueAllocation s;
  for (auto i : Fibonacci{}) {
    for (auto j : Fibonacci{}) {
      s = RValueAllocation::Int64(i, j);
      CHECK(s == Read(s));
    }
  }
  return true;
}
END_TEST(testJitRValueAlloc_Int64StackStack)
#else
BEGIN_TEST(testJitRValueAlloc_Int64Reg) {
  for (uint32_t i = 0; i < Registers::Total; i++) {
    auto s = RValueAllocation::Int64(Register::FromCode(i));
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_Int64Reg)

BEGIN_TEST(testJitRValueAlloc_Int64Stack) {
  for (auto i : Fibonacci{}) {
    auto s = RValueAllocation::Int64(i);
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_Int64Stack)

BEGIN_TEST(testJitRValueAlloc_Int64Int32Stack) {
  for (auto i : Fibonacci{}) {
    auto s = RValueAllocation::Int64Int32(i);
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_Int64Int32Stack)
#endif

BEGIN_TEST(testJitRValueAlloc_IntPtrCst) {
#if !defined(JS_64BIT)
  for (auto i : Fibonacci{}) {
    auto s = RValueAllocation::IntPtrConstant(i);
    CHECK(s == Read(s));
  }
#else
  for (auto i : Fibonacci{}) {
    for (auto j : Fibonacci{}) {
      auto s = RValueAllocation::IntPtrConstant(i, j);
      CHECK(s == Read(s));
    }
  }
#endif
  return true;
}
END_TEST(testJitRValueAlloc_IntPtrCst)

BEGIN_TEST(testJitRValueAlloc_IntPtrReg) {
  for (uint32_t i = 0; i < Registers::Total; i++) {
    auto s = RValueAllocation::IntPtr(Register::FromCode(i));
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_IntPtrReg)

BEGIN_TEST(testJitRValueAlloc_IntPtrStack) {
  for (auto i : Fibonacci{}) {
    auto s = RValueAllocation::IntPtr(i);
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_IntPtrStack)

BEGIN_TEST(testJitRValueAlloc_IntPtrInt32Stack) {
  for (auto i : Fibonacci{}) {
    auto s = RValueAllocation::IntPtrInt32(i);
    CHECK(s == Read(s));
  }
  return true;
}
END_TEST(testJitRValueAlloc_IntPtrInt32Stack)

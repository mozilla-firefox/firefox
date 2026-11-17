/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHtml5String_h
#define nsHtml5String_h

#include "mozilla/fallible.h"
#include "nsAtom.h"
#include "nsString.h"

namespace mozilla {
class StringBuffer;
}

class nsHtml5TreeBuilder;

/**
 * A pass-by-value type that can represent
 *  * nullptr
 *  * empty string
 *  * Non-empty string as exactly-sized (capacity is length) `StringBuffer*`
 *  * Non-empty string as an nsAtom*
 *
 * Holding or passing this type is as unsafe as holding or passing
 * `StringBuffer*`/`nsAtom*`.
 */
class nsHtml5String final {
 private:
  static const uintptr_t kKindMask = uintptr_t(3);

  static const uintptr_t kPtrMask = ~kKindMask;

  enum Kind : uintptr_t {
    eNull = 0,
    eEmpty = 1,
    eStringBuffer = 2,
    eAtom = 3,
  };

  inline Kind GetKind() const { return (Kind)(mBits & kKindMask); }

 public:
  inline bool IsAtom() const { return GetKind() == eAtom; }

  inline bool IsStringBuffer() const { return GetKind() == eStringBuffer; }

  inline mozilla::StringBuffer* AsStringBuffer() const {
    MOZ_ASSERT(IsStringBuffer());
    return reinterpret_cast<mozilla::StringBuffer*>(mBits & kPtrMask);
  }

  // DANGER: Calling this is only valid if this is a logically owning
  // nsHtml5String and calling Release would be valid.
  inline already_AddRefed<mozilla::StringBuffer> ForgetStringBuffer() {
    already_AddRefed<mozilla::StringBuffer> ret(AsStringBuffer());
    mBits = eNull;
    return ret;
  }

  inline nsAtom* AsAtom() const {
    MOZ_ASSERT(IsAtom());
    return reinterpret_cast<nsAtom*>(mBits & kPtrMask);
  }

  // DANGER: Calling this is only valid if this is a logically owning
  // nsHtml5String and calling Release would be valid.
  inline already_AddRefed<nsAtom> ForgetAtom() {
    already_AddRefed<nsAtom> ret(AsAtom());
    mBits = eNull;
    return ret;
  }

 private:
  inline const char16_t* AsPtr() const {
    switch (GetKind()) {
      case eStringBuffer:
        return reinterpret_cast<char16_t*>(AsStringBuffer()->Data());
      case eAtom:
        return AsAtom()->GetUTF16String();
      default:
        return nsCharTraits<char16_t>::sEmptyBuffer;
    }
  }

 public:
  /**
   * Default constructor.
   */
  inline nsHtml5String() : nsHtml5String(nullptr) {}

  /**
   * Constructor from nullptr.
   */
  inline MOZ_IMPLICIT nsHtml5String(decltype(nullptr)) : mBits(eNull) {}

  inline uint32_t Length() const {
    switch (GetKind()) {
      case eStringBuffer:
        return (AsStringBuffer()->StorageSize() / sizeof(char16_t) - 1);
      case eAtom:
        return AsAtom()->GetLength();
      default:
        return 0;
    }
  }

  /**
   * False iff the string is logically null
   */
  inline MOZ_IMPLICIT operator bool() const { return mBits; }

  /**
   * Get the underlying nsAtom* or nullptr if this nsHtml5String
   * does not hold an atom.
   */
  inline nsAtom* MaybeAsAtom() {
    if (IsAtom()) {
      return AsAtom();
    }
    return nullptr;
  }

  void ToString(nsAString& aString);

  // Same output as above, but this string is logically null afterwards.
  // Avoids an AddRef if this string is holding a StringBuffer.
  // DANGER: Use this only on an instance that is logically an
  // owning instance and `Release()` would be valid to call!
  void MoveToString(nsAString& aString);

  void CopyToBuffer(char16_t* aBuffer) const;

  bool LowerCaseEqualsASCII(const char* aLowerCaseLiteral) const;

  template <size_t N>
  bool LowerCaseEqualsASCII(const char (&aLowerCaseLiteral)[N]) const {
    constexpr size_t length = N - 1;
    return Length() == length &&
           !nsCharTraits<char16_t>::compareLowerCaseToASCII(
               AsPtr(), aLowerCaseLiteral, length);
  }

  bool EqualsASCII(const char* aLiteral) const;

  template <size_t N>
  bool EqualsASCII(const char (&aLiteral)[N]) const {
    constexpr size_t length = N - 1;
    return Length() == length &&
           !nsCharTraits<char16_t>::compareASCII(AsPtr(), aLiteral, length);
  }

  bool LowerCaseStartsWithASCII(const char* aLowerCaseLiteral) const;

  bool Equals(nsHtml5String aOther) const;

  nsHtml5String Clone();

  void Release();

  static nsHtml5String FromBuffer(char16_t* aBuffer, int32_t aLength,
                                  nsHtml5TreeBuilder* aTreeBuilder);

  // Returns a logically null string on allocation failure.
  static nsHtml5String FromBuffer(const char16_t* aBuffer, int32_t aLength,
                                  const mozilla::fallible_t&);

  static nsHtml5String FromLiteral(const char* aLiteral);

  static nsHtml5String FromString(const nsAString& aString);

  static nsHtml5String FromAtom(already_AddRefed<nsAtom> aAtom);

  static nsHtml5String FromStaticAtom(nsStaticAtom* aAtom);

  static nsHtml5String EmptyString();

 private:
  /**
   * Constructor from raw bits.
   */
  explicit nsHtml5String(uintptr_t aBits) : mBits(aBits) {};

  /**
   * Zero if null, one if empty, otherwise tagged pointer
   * to either nsAtom or nsStringBuffer. The two least-significant
   * bits are tag bits.
   */
  uintptr_t mBits;
};

#endif  // nsHtml5String_h

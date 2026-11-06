/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

template <int N>
static bool bufferStartsWithLiteralAssumeSufficientLength(
    const char16_t* aBuf, const char16_t (&aLiteral)[N]) {
  return !memcmp(aBuf, aLiteral, (N - 1) * sizeof(char16_t));
}

template <int N>
static bool bufferStartsWithLiteralAtOffsetAssumeSufficientLength(
    const char16_t* aBuf, const char16_t (&aLiteral)[N], int aOffset) {
  return !memcmp(aBuf + aOffset, aLiteral, (N - 1) * sizeof(char16_t));
}

// Returns the static atom of a value that is a single ASCII digit, or null.
MOZ_ALWAYS_INLINE static nsHtml5String newSingleDigitAttributeValue(
    const char16_t* buf, int32_t length) {
  if (length != 1 || buf[0] < '0' || buf[0] > '9') {
    return nullptr;
  }
  static nsStaticAtom* const digitAtoms[10] = {
      nsGkAtoms::_0, nsGkAtoms::_1, nsGkAtoms::_2, nsGkAtoms::_3,
      nsGkAtoms::_4, nsGkAtoms::_5, nsGkAtoms::_6, nsGkAtoms::_7,
      nsGkAtoms::_8, nsGkAtoms::_9};
  return nsHtml5String::FromStaticAtom(digitAtoms[buf[0] - '0']);
}

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

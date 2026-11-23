/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_intl_l10n_FluentArg_h
#define mozilla_intl_l10n_FluentArg_h

#include "mozilla/Variant.h"
#include "nsString.h"

#include <type_traits>

namespace mozilla::intl {

namespace detail {
// Character types are excluded, so that e.g. u'a' isn't silently passed as a
// number. bool is excluded as well, since Fluent has no boolean type.
template <typename T>
inline constexpr bool kIsFluentNumber =
    std::is_arithmetic_v<T> && !std::is_same_v<T, bool> &&
    !std::is_same_v<T, char> && !std::is_same_v<T, wchar_t> &&
    !std::is_same_v<T, char8_t> && !std::is_same_v<T, char16_t> &&
    !std::is_same_v<T, char32_t>;
}  // namespace detail

/**
 * A named argument (variable) passed to a Fluent message from C++, e.g.
 * l10n->FormatValueSync(id, {{"name"_ns, name}, {"count"_ns, 2}}, ...).
 *
 * This is a simpler alternative to building dom::L10nArgs by hand, which takes
 * several calls for each argument. Unlike L10nArgs, it only depends on strings,
 * so it can be used in widely included headers without pulling in the generated
 * Fluent bindings.
 *
 * Numeric values should be passed as numbers, so that they can be used for
 * plural selection and formatted for the locale. They're stored as double, like
 * all Fluent numbers, so integers above 2^53 may lose precision.
 */
struct FluentArg {
  FluentArg(const nsACString& aName, const nsACString& aValue)
      : mName(aName), mValue(nsCString(aValue)) {}
  FluentArg(const nsACString& aName, const nsAString& aValue)
      : mName(aName), mValue(nsCString(NS_ConvertUTF16toUTF8(aValue))) {}
  template <typename T, typename = std::enable_if_t<detail::kIsFluentNumber<T>>>
  FluentArg(const nsACString& aName, T aValue)
      : mName(aName), mValue(static_cast<double>(aValue)) {}

  nsCString mName;
  Variant<nsCString, double> mValue;
};

}  // namespace mozilla::intl

#endif

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/intl/GlobalIntlData.h"

#include "mozilla/Assertions.h"
#include "mozilla/Span.h"

#include "builtin/intl/Collator.h"
#include "builtin/intl/CommonFunctions.h"
#include "builtin/intl/DateTimeFormat.h"
#include "builtin/intl/FormatBuffer.h"
#include "builtin/intl/LocaleNegotiation.h"
#include "builtin/intl/NumberFormat.h"
#include "builtin/temporal/TimeZone.h"
#include "gc/Tracer.h"
#include "js/RootingAPI.h"
#include "js/TracingAPI.h"
#include "js/Value.h"
#include "vm/DateTime.h"
#include "vm/JSContext.h"
#include "vm/Realm.h"

#include "vm/JSObject-inl.h"

using namespace js;
using namespace js::intl;

void js::intl::GlobalIntlData::resetCollator() {
  collatorLocale_ = nullptr;
  collator_ = nullptr;
}

void js::intl::GlobalIntlData::resetNumberFormat() {
  numberFormatLocale_ = nullptr;
  numberFormat_ = nullptr;
}

void js::intl::GlobalIntlData::resetDateTimeFormat() {
  dateTimeFormatLocale_ = nullptr;
  dateTimeFormatToLocaleAll_ = nullptr;
  dateTimeFormatToLocaleDate_ = nullptr;
  dateTimeFormatToLocaleTime_ = nullptr;
}

bool js::intl::GlobalIntlData::ensureRealmLocale(JSContext* cx) {
  auto locale = cx->realm()->getLocale();
  if (realmLocale_ != locale) {
    realmLocale_ = locale;

    // Clear the cached default locale.
    defaultLocale_ = LanguageId::und();

    // Clear all cached instances when the realm locale has changed.
    resetCollator();
    resetNumberFormat();
    resetDateTimeFormat();
  }

  return true;
}

bool js::intl::GlobalIntlData::ensureRealmTimeZone(JSContext* cx) {
  TimeZoneIdentifierVector timeZoneId;
  if (!DateTimeInfo::timeZoneId(cx->realm()->getDateTimeInfo(), timeZoneId)) {
    ReportOutOfMemory(cx);
    return false;
  }

  if (!realmTimeZone_ || !StringEqualsAscii(realmTimeZone_, timeZoneId.begin(),
                                            timeZoneId.length())) {
    realmTimeZone_ = NewStringCopy<CanGC>(
        cx, static_cast<mozilla::Span<const char>>(timeZoneId));
    if (!realmTimeZone_) {
      return false;
    }

    // Clear the cached default time zone.
    defaultTimeZone_ = nullptr;
    defaultTimeZoneObject_ = nullptr;

    // Clear all cached DateTimeFormat instances when the time zone has changed.
    resetDateTimeFormat();
  }

  return true;
}

bool js::intl::GlobalIntlData::defaultLocale(JSContext* cx,
                                             LanguageId* result) {
  // Ensure the realm locale didn't change.
  if (!ensureRealmLocale(cx)) {
    return false;
  }

  // If we didn't have a cache hit, compute the candidate default locale.
  if (defaultLocale_ == LanguageId::und()) {
    // Cache the computed locale until the realm locale changes.
    auto locale = LanguageId::und();
    if (!ComputeDefaultLocale(cx, &locale)) {
      return false;
    }
    MOZ_ASSERT(locale != LanguageId::und(), "default locale is not 'und'");

    defaultLocale_ = locale;
  }
  *result = defaultLocale_;
  return true;
}

JSLinearString* js::intl::GlobalIntlData::defaultTimeZone(JSContext* cx) {
  // Ensure the realm time zone didn't change.
  if (!ensureRealmTimeZone(cx)) {
    return nullptr;
  }

  // If we didn't have a cache hit, compute the default time zone.
  if (!defaultTimeZone_) {
    // Cache the computed time zone until the realm time zone changes.
    defaultTimeZone_ = temporal::ComputeSystemTimeZoneIdentifier(cx);
  }
  return defaultTimeZone_;
}

static inline bool EqualLocale(const JSLinearString* str1,
                               const JSLinearString* str2) {
  if (str1 && str2) {
    return EqualStrings(str1, str2);
  }
  return !str1 && !str2;
}

static inline Value LocaleOrDefault(JSLinearString* locale) {
  if (locale) {
    return StringValue(locale);
  }
  return UndefinedValue();
}

CollatorObject* js::intl::GlobalIntlData::getOrCreateCollator(
    JSContext* cx, Handle<JSLinearString*> locale) {
  // Ensure the realm locale didn't change.
  if (!ensureRealmLocale(cx)) {
    return nullptr;
  }

  // Ensure the cached locale matches the requested locale.
  if (!EqualLocale(collatorLocale_, locale)) {
    resetCollator();
    collatorLocale_ = locale;
  }

  if (!collator_) {
    Rooted<Value> locales(cx, LocaleOrDefault(locale));
    auto* collator = CreateCollator(cx, locales, UndefinedHandleValue);
    if (!collator) {
      return nullptr;
    }
    collator_ = collator;
  }

  return &collator_->as<CollatorObject>();
}

NumberFormatObject* js::intl::GlobalIntlData::getOrCreateNumberFormat(
    JSContext* cx, Handle<JSLinearString*> locale) {
  // Ensure the realm locale didn't change.
  if (!ensureRealmLocale(cx)) {
    return nullptr;
  }

  // Ensure the cached locale matches the requested locale.
  if (!EqualLocale(numberFormatLocale_, locale)) {
    resetNumberFormat();
    numberFormatLocale_ = locale;
  }

  if (!numberFormat_) {
    Rooted<Value> locales(cx, LocaleOrDefault(locale));
    auto* numberFormat = CreateNumberFormat(cx, locales, UndefinedHandleValue);
    if (!numberFormat) {
      return nullptr;
    }
    numberFormat_ = numberFormat;
  }

  return &numberFormat_->as<NumberFormatObject>();
}

DateTimeFormatObject* js::intl::GlobalIntlData::getOrCreateDateTimeFormat(
    JSContext* cx, DateTimeFormatKind kind, Handle<JSLinearString*> locale) {
  // Ensure the realm didn't change.
  if (!ensureRealmLocale(cx)) {
    return nullptr;
  }

  // Ensure the realm time zone didn't change.
  if (!ensureRealmTimeZone(cx)) {
    return nullptr;
  }

  // Ensure the cached locale matches the requested locale.
  if (!EqualLocale(dateTimeFormatLocale_, locale)) {
    resetDateTimeFormat();
    dateTimeFormatLocale_ = locale;
  }

  JSObject* dtfObject = nullptr;
  switch (kind) {
    case DateTimeFormatKind::All:
      dtfObject = dateTimeFormatToLocaleAll_;
      break;
    case DateTimeFormatKind::Date:
      dtfObject = dateTimeFormatToLocaleDate_;
      break;
    case DateTimeFormatKind::Time:
      dtfObject = dateTimeFormatToLocaleTime_;
      break;
  }

  if (!dtfObject) {
    Rooted<Value> locales(cx, LocaleOrDefault(locale));
    auto* dateTimeFormat =
        CreateDateTimeFormat(cx, locales, UndefinedHandleValue, kind);
    if (!dateTimeFormat) {
      return nullptr;
    }

    switch (kind) {
      case DateTimeFormatKind::All:
        dateTimeFormatToLocaleAll_ = dateTimeFormat;
        break;
      case DateTimeFormatKind::Date:
        dateTimeFormatToLocaleDate_ = dateTimeFormat;
        break;
      case DateTimeFormatKind::Time:
        dateTimeFormatToLocaleTime_ = dateTimeFormat;
        break;
    }

    dtfObject = dateTimeFormat;
  }

  return &dtfObject->as<DateTimeFormatObject>();
}

temporal::TimeZoneObject* js::intl::GlobalIntlData::getOrCreateDefaultTimeZone(
    JSContext* cx) {
  // Ensure the realm time zone didn't change.
  if (!ensureRealmTimeZone(cx)) {
    return nullptr;
  }

  // If we didn't have a cache hit, compute the default time zone.
  if (!defaultTimeZoneObject_) {
    Rooted<JSLinearString*> identifier(cx, defaultTimeZone(cx));
    if (!identifier) {
      return nullptr;
    }

    auto* timeZone = temporal::CreateTimeZoneObject(cx, identifier, identifier);
    if (!timeZone) {
      return nullptr;
    }
    defaultTimeZoneObject_ = timeZone;
  }

  return &defaultTimeZoneObject_->as<temporal::TimeZoneObject>();
}

temporal::TimeZoneObject* js::intl::GlobalIntlData::getOrCreateTimeZone(
    JSContext* cx, Handle<JSLinearString*> identifier,
    Handle<JSLinearString*> primaryIdentifier) {
  // If there's a cached time zone, check if the identifiers are equal.
  if (timeZoneObject_) {
    auto* timeZone = &timeZoneObject_->as<temporal::TimeZoneObject>();
    if (EqualStrings(timeZone->identifier(), identifier)) {
      // Primary identifier must match when the identifiers are equal.
      MOZ_ASSERT(
          EqualStrings(timeZone->primaryIdentifier(), primaryIdentifier));

      // Return the cached time zone.
      return timeZone;
    }
  }

  // If we didn't have a cache hit, create a new time zone.
  auto* timeZone =
      temporal::CreateTimeZoneObject(cx, identifier, primaryIdentifier);
  if (!timeZone) {
    return nullptr;
  }
  timeZoneObject_ = timeZone;

  return &timeZone->as<temporal::TimeZoneObject>();
}

JS::Symbol* js::intl::GlobalIntlData::fallbackSymbol(JSContext* cx) {
  if (!fallbackSymbol_) {
    Handle<PropertyName*> description = cx->names().IntlLegacyConstructedSymbol;
    fallbackSymbol_ =
        JS::Symbol::new_(cx, JS::SymbolCode::UniqueSymbol, description);
  }
  return fallbackSymbol_;
}

void js::intl::GlobalIntlData::trace(JSTracer* trc) {
  TraceEdge(trc, &realmTimeZone_, "GlobalIntlData::realmTimeZone_");
  TraceEdge(trc, &defaultTimeZone_, "GlobalIntlData::defaultTimeZone_");
  TraceEdge(trc, &defaultTimeZoneObject_,
            "GlobalIntlData::defaultTimeZoneObject_");
  TraceEdge(trc, &timeZoneObject_, "GlobalIntlData::timeZoneObject_");

  TraceEdge(trc, &collatorLocale_, "GlobalIntlData::collatorLocale_");
  TraceEdge(trc, &collator_, "GlobalIntlData::collator_");

  TraceEdge(trc, &numberFormatLocale_, "GlobalIntlData::numberFormatLocale_");
  TraceEdge(trc, &numberFormat_, "GlobalIntlData::numberFormat_");

  TraceEdge(trc, &dateTimeFormatLocale_,
            "GlobalIntlData::dateTimeFormatLocale_");
  TraceEdge(trc, &dateTimeFormatToLocaleAll_,
            "GlobalIntlData::dateTimeFormatToLocaleAll_");
  TraceEdge(trc, &dateTimeFormatToLocaleDate_,
            "GlobalIntlData::dateTimeFormatToLocaleDate_");
  TraceEdge(trc, &dateTimeFormatToLocaleTime_,
            "GlobalIntlData::dateTimeFormatToLocaleTime_");

  TraceEdge(trc, &fallbackSymbol_, "GlobalIntlData::fallbackSymbol_");
}

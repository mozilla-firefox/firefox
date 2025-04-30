/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExtraFileParser.h"

#include "CrashAnnotations.h"
#include "mozilla/Maybe.h"

namespace CrashReporter {

using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::Some;

bool ExtraFileParser::startObject() {
  if (mObject) {
    return false;  // We expect only one top-level object
  }

  mObject = true;
  return true;
}

bool ExtraFileParser::endObject() {
  return mObject;  // We should end only one object, anything else is wrong.
}

bool ExtraFileParser::propertyName(const JS::Latin1Char* aName,
                                   size_t aLength) {
  nsDependentCSubstring name(reinterpret_cast<const char*>(aName), aLength);
  mLastAnnotation = AnnotationFromString(name);

  return mLastAnnotation.isSome();
}

bool ExtraFileParser::propertyName(const char16_t* aName, size_t aLength) {
  // We only parse UTF-8 text.
  return false;
}

bool ExtraFileParser::startArray() {
  // The .extra file should not contain arrays.
  return false;
}

bool ExtraFileParser::endArray() {
  // The .extra file should not contain arrays.
  return false;
}

bool ExtraFileParser::stringValue(const JS::Latin1Char* aStr, size_t aLength) {
  nsDependentCSubstring value(reinterpret_cast<const char*>(aStr), aLength);
  mAnnotations[*mLastAnnotation] = value;

  return true;
}

bool ExtraFileParser::stringValue(const char16_t* aStr, size_t aLength) {
  // We only parse UTF-8 text.
  return false;
}

bool ExtraFileParser::numberValue(double aVal) {
  // The .extra file should not contain number values.
  return false;
}

bool ExtraFileParser::booleanValue(bool aBoolean) {
  // The .extra file should not contain number values.
  return false;
}

bool ExtraFileParser::nullValue() {
  // The .extra file should not contain null values.
  return false;
}
void ExtraFileParser::error(const char* aMsg, uint32_t aLine,
                            uint32_t aColumn) {}

Maybe<AnnotationTable> ExtraFileParser::Parse(const nsACString& aJSON) {
  ExtraFileParser handler;
  AnnotationTable annotations;

  if (!JS::ParseJSONWithHandler(
          reinterpret_cast<const JS::Latin1Char*>(aJSON.BeginReading()),
          aJSON.Length(), &handler)) {
    return Nothing();
  }

  handler.getAnnotations(annotations);
  return Some(annotations);
}

}  // namespace CrashReporter

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ExtraFileParser_h__
#define ExtraFileParser_h__

#include "js/JSON.h"  // JS::JSONParseHandler

#include "mozilla/EnumeratedArray.h"
#include "mozilla/Maybe.h"
#include "nsString.h"

#include "CrashAnnotations.h"

namespace CrashReporter {

using mozilla::Maybe;
using AnnotationTable =
    mozilla::EnumeratedArray<Annotation, nsCString, size_t(Annotation::Count)>;

class ExtraFileParser : public JS::JSONParseHandler {
 public:
  ExtraFileParser() : mObject(false) {}

  // JSONParseHandler methods.
  virtual bool startObject() override;
  virtual bool endObject() override;
  virtual bool propertyName(const JS::Latin1Char* aName,
                            size_t aLength) override;
  virtual bool propertyName(const char16_t* aName, size_t aLength) override;
  virtual bool startArray() override;
  virtual bool endArray() override;
  virtual bool stringValue(const JS::Latin1Char* aStr, size_t aLength) override;
  virtual bool stringValue(const char16_t* aStr, size_t aLength) override;
  virtual bool numberValue(double aVal) override;
  virtual bool booleanValue(bool aBoolean) override;
  virtual bool nullValue() override;
  virtual void error(const char* aMsg, uint32_t aLine,
                     uint32_t aColumn) override;

  void getAnnotations(AnnotationTable& aAnnotations) {
    aAnnotations = mAnnotations;
  }

  static mozilla::Maybe<AnnotationTable> Parse(const nsACString& aJSON);

 private:
  AnnotationTable mAnnotations;
  Maybe<Annotation> mLastAnnotation;  // Last annotation seen while parsing
  bool mObject;  // Set to true after we encounter the first object
};

}  // namespace CrashReporter

#endif  // ExtraFileParser_h__

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* diagnostic reporting for CSS style sheet parser */

#ifndef mozilla_css_ErrorReporter_h_
#define mozilla_css_ErrorReporter_h_

#include "nsString.h"

struct nsCSSToken;
class nsIURI;

namespace mozilla {
class StyleSheet;

namespace dom {
class Document;
}

namespace css {

class Loader;

// FIXME(emilio): Probably better to call this ErrorBuilder or something?
class MOZ_STACK_CLASS ErrorReporter final {
 public:
  explicit ErrorReporter(uint64_t aInnerWindowId);
  ~ErrorReporter();

  static void ReleaseGlobals();
  static void EnsureGlobalsInitialized() {
    if (MOZ_UNLIKELY(!sInitialized)) {
      InitGlobals();
    }
  }

  static bool ShouldReportErrors(const dom::Document&);
  static bool ShouldReportErrors(const StyleSheet*, const Loader*);
  static uint64_t FindInnerWindowId(const StyleSheet*, const Loader*);

  void OutputError(const nsACString& aSelectors, uint32_t aLineNumber,
                   uint32_t aColNumber, nsIURI* aURI);

  // In all overloads of ReportUnexpected, aMessage is a stringbundle
  // name, which will be processed as a format string with the
  // indicated number of parameters.

  // no parameters
  void ReportUnexpected(const char* aMessage);
  // one parameter which has already been escaped appropriately
  void ReportUnexpectedUnescaped(const char* aMessage,
                                 const nsTArray<nsString>& aParam);

 private:
  void AddToError(const nsString& aErrorText);
  static void InitGlobals();

  static bool sInitialized;
  static bool sReportErrors;

  nsString mError;
  const uint64_t mInnerWindowId;
};

}  // namespace css
}  // namespace mozilla

#endif  // mozilla_css_ErrorReporter_h_

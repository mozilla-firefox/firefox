/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* implementation of interface for managing user and user-agent style sheets */

#include "nsStyleSheetService.h"

#include "mozilla/MemoryReporting.h"
#include "mozilla/PreloadedStyleSheet.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsCOMPtr.h"
#include "nsIConsoleService.h"
#include "nsISimpleEnumerator.h"
#include "nsISupportsPrimitives.h"
#include "nsIURI.h"
#include "nsLayoutStatics.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"

using namespace mozilla;

nsStyleSheetService* nsStyleSheetService::gInstance = nullptr;

nsStyleSheetService::nsStyleSheetService() {
  static_assert(0 == AGENT_SHEET && 1 == USER_SHEET && 2 == AUTHOR_SHEET,
                "Convention for Style Sheet");
  NS_ASSERTION(!gInstance,
               "Someone is using CreateInstance instead of GetService");
  if (!gInstance) {
    gInstance = this;
  }
  nsLayoutStatics::AddRef();
}

nsStyleSheetService::~nsStyleSheetService() {
  UnregisterWeakMemoryReporter(this);

  if (gInstance == this) {
    gInstance = nullptr;
  }
  nsLayoutStatics::Release();
}

NS_IMPL_ISUPPORTS(nsStyleSheetService, nsIStyleSheetService, nsIMemoryReporter)

static bool SheetHasOriginalURI(StyleSheet* aSheet, nsIURI* aSheetURI) {
  MOZ_ASSERT(aSheetURI);

  bool result;
  nsIURI* uri = aSheet->GetOriginalURI();
  return uri && NS_SUCCEEDED(uri->Equals(aSheetURI, &result)) && result;
}

int32_t nsStyleSheetService::FindSheetByURI(uint32_t aSheetType,
                                            nsIURI* aSheetURI) {
  SheetArray& sheets = mSheets[aSheetType];
  for (int32_t i = sheets.Length() - 1; i >= 0; i--) {
    if (SheetHasOriginalURI(sheets[i], aSheetURI)) {
      return i;
    }
  }

  return -1;
}

nsresult nsStyleSheetService::Init() {
  // Child processes get their style sheets from the ContentParent.
  if (XRE_IsContentProcess()) {
    return NS_OK;
  }

  RegisterWeakMemoryReporter(this);

  return NS_OK;
}

NS_IMETHODIMP
nsStyleSheetService::LoadAndRegisterSheet(nsIURI* aSheetURI,
                                          uint32_t aSheetType) {
  // Warn developers if their stylesheet URL has a #ref at the end.
  // Stylesheet URIs don't benefit from having a #ref suffix -- and if the
  // sheet is a data URI, someone might've created this #ref by accident (and
  // truncated their data-URI stylesheet) by using an unescaped # character in
  // a #RRGGBB color or #foo() ID-selector in their data-URI representation.
  NS_ENSURE_ARG_POINTER(aSheetURI);
  bool hasRef;
  nsresult rv = aSheetURI->GetHasRef(&hasRef);
  NS_ENSURE_SUCCESS(rv, rv);
  if (hasRef) {
    nsCOMPtr<nsIConsoleService> consoleService =
        do_GetService(NS_CONSOLESERVICE_CONTRACTID);
    NS_WARNING_ASSERTION(consoleService, "Failed to get console service!");
    if (consoleService) {
      const char16_t* message =
          u"nsStyleSheetService::LoadAndRegisterSheet: "
          u"URI contains unescaped hash character, which might be truncating "
          u"the sheet, if it's a data URI.";
      consoleService->LogStringMessage(message);
    }
  }

  rv = LoadAndRegisterSheetInternal(aSheetURI, aSheetType);
  if (NS_SUCCEEDED(rv)) {
    // Hold on to a copy of the registered PresShells.
    for (PresShell* presShell : mPresShells.Clone()) {
      StyleSheet* sheet = mSheets[aSheetType].LastElement();
      presShell->NotifyStyleSheetServiceSheetAdded(sheet, aSheetType);
    }

    if (XRE_IsParentProcess()) {
      nsTArray<dom::ContentParent*> children;
      dom::ContentParent::GetAll(children);

      if (children.IsEmpty()) {
        return rv;
      }

      for (uint32_t i = 0; i < children.Length(); i++) {
        (void)children[i]->SendLoadAndRegisterSheet(aSheetURI, aSheetType);
      }
    }
  }
  return rv;
}

static nsresult GetSheetOrigin(uint32_t aSheetType, StyleOrigin* aOrigin) {
  switch (aSheetType) {
    case nsStyleSheetService::AGENT_SHEET:
      *aOrigin = StyleOrigin::UserAgent;
      return NS_OK;

    case nsStyleSheetService::USER_SHEET:
      *aOrigin = StyleOrigin::User;
      return NS_OK;

    case nsStyleSheetService::AUTHOR_SHEET:
      *aOrigin = StyleOrigin::Author;
      return NS_OK;

    default:
      NS_WARNING("invalid sheet type argument");
      return NS_ERROR_INVALID_ARG;
  }
}

nsresult nsStyleSheetService::LoadAndRegisterSheetInternal(
    nsIURI* aSheetURI, uint32_t aSheetType) {
  NS_ENSURE_ARG_POINTER(aSheetURI);

  StyleOrigin origin;
  MOZ_TRY(GetSheetOrigin(aSheetType, &origin));
  auto loader = MakeRefPtr<css::Loader>();
  auto result = loader->LoadSheetSync(aSheetURI, origin,
                                      css::Loader::UseSystemPrincipal::Yes);
  if (result.isErr()) {
    return result.unwrapErr();
  }
  mSheets[aSheetType].AppendElement(result.unwrap());
  return NS_OK;
}

NS_IMETHODIMP
nsStyleSheetService::SheetRegistered(nsIURI* sheetURI, uint32_t aSheetType,
                                     bool* _retval) {
  NS_ENSURE_ARG(aSheetType == AGENT_SHEET || aSheetType == USER_SHEET ||
                aSheetType == AUTHOR_SHEET);
  NS_ENSURE_ARG_POINTER(sheetURI);
  MOZ_ASSERT(_retval, "Null out param");

  // Check to see if we have the sheet.
  *_retval = FindSheetByURI(aSheetType, sheetURI) >= 0;
  return NS_OK;
}

NS_IMETHODIMP
nsStyleSheetService::PreloadSheet(nsIURI* aSheetURI, uint32_t aSheetType,
                                  nsIPreloadedStyleSheet** aSheet) {
  MOZ_ASSERT(aSheet, "Null out param");
  NS_ENSURE_ARG_POINTER(aSheetURI);

  StyleOrigin origin;
  MOZ_TRY(GetSheetOrigin(aSheetType, &origin));

  auto sheet = MakeRefPtr<PreloadedStyleSheet>(aSheetURI, origin);
  MOZ_TRY(sheet->Preload());
  sheet.forget(aSheet);
  return NS_OK;
}

NS_IMETHODIMP
nsStyleSheetService::PreloadSheetAsync(nsIURI* aSheetURI, uint32_t aSheetType,
                                       JSContext* aCx,
                                       JS::MutableHandle<JS::Value> aRval) {
  NS_ENSURE_ARG_POINTER(aSheetURI);

  StyleOrigin origin;
  MOZ_TRY(GetSheetOrigin(aSheetType, &origin));

  nsCOMPtr<nsIGlobalObject> global = xpc::CurrentNativeGlobal(aCx);
  NS_ENSURE_TRUE(global, NS_ERROR_UNEXPECTED);

  ErrorResult errv;
  RefPtr<dom::Promise> promise = dom::Promise::Create(global, errv);
  if (errv.Failed()) {
    return errv.StealNSResult();
  }

  auto sheet = MakeRefPtr<PreloadedStyleSheet>(aSheetURI, origin);
  sheet->PreloadAsync(WrapNotNull(promise));

  if (!ToJSValue(aCx, promise, aRval)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsStyleSheetService::UnregisterSheet(nsIURI* aSheetURI, uint32_t aSheetType) {
  NS_ENSURE_ARG(aSheetType == AGENT_SHEET || aSheetType == USER_SHEET ||
                aSheetType == AUTHOR_SHEET);
  NS_ENSURE_ARG_POINTER(aSheetURI);

  RefPtr<StyleSheet> sheet;
  int32_t foundIndex = FindSheetByURI(aSheetType, aSheetURI);
  if (foundIndex >= 0) {
    sheet = mSheets[aSheetType][foundIndex];
    mSheets[aSheetType].RemoveElementAt(foundIndex);
  }

  // Hold on to a copy of the registered PresShells.
  for (PresShell* presShell : mPresShells.Clone()) {
    if (presShell->StyleSet()) {
      if (sheet) {
        presShell->NotifyStyleSheetServiceSheetRemoved(sheet, aSheetType);
      }
    }
  }

  if (XRE_IsParentProcess()) {
    nsTArray<dom::ContentParent*> children;
    dom::ContentParent::GetAll(children);

    if (children.IsEmpty()) {
      return NS_OK;
    }

    for (uint32_t i = 0; i < children.Length(); i++) {
      (void)children[i]->SendUnregisterSheet(aSheetURI, aSheetType);
    }
  }

  return NS_OK;
}

// static
nsStyleSheetService* nsStyleSheetService::GetInstance() {
  static bool first = true;
  if (first) {
    // make sure at first call that it's inited
    nsCOMPtr<nsIStyleSheetService> dummy =
        do_GetService(NS_STYLESHEETSERVICE_CONTRACTID);
    first = false;
  }

  return gInstance;
}

MOZ_DEFINE_MALLOC_SIZE_OF(StyleSheetServiceMallocSizeOf)

NS_IMETHODIMP
nsStyleSheetService::CollectReports(nsIHandleReportCallback* aHandleReport,
                                    nsISupports* aData, bool aAnonymize) {
  MOZ_COLLECT_REPORT(
      "explicit/layout/style-sheet-service", KIND_HEAP, UNITS_BYTES,
      SizeOfIncludingThis(StyleSheetServiceMallocSizeOf),
      "Memory used for style sheets held by the style sheet service.");

  return NS_OK;
}

size_t nsStyleSheetService::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);
  for (const auto& sheetArray : mSheets) {
    n += sheetArray.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (StyleSheet* sheet : sheetArray) {
      if (sheet) {
        n += sheet->SizeOfIncludingThis(aMallocSizeOf);
      }
    }
  }
  return n;
}

void nsStyleSheetService::RegisterPresShell(PresShell* aPresShell) {
  MOZ_ASSERT(!mPresShells.Contains(aPresShell));
  mPresShells.AppendElement(aPresShell);
}

void nsStyleSheetService::UnregisterPresShell(PresShell* aPresShell) {
  MOZ_ASSERT(mPresShells.Contains(aPresShell));
  mPresShells.RemoveElement(aPresShell);
}

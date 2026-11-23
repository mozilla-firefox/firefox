/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPrinterListBase.h"

#include "PrintBackgroundTask.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/intl/Localization.h"
#include "xpcpublic.h"

using namespace mozilla;

using mozilla::ErrorResult;
using mozilla::intl::Localization;
using PrinterInfo = nsPrinterListBase::PrinterInfo;
using MarginDouble = mozilla::gfx::MarginDouble;

nsPrinterListBase::nsPrinterListBase() = default;
nsPrinterListBase::~nsPrinterListBase() = default;

NS_IMPL_CYCLE_COLLECTION(nsPrinterListBase, mPrintersPromise)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsPrinterListBase)
  NS_INTERFACE_MAP_ENTRY(nsIPrinterList)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIPrinterList)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsPrinterListBase)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsPrinterListBase)

namespace mozilla {

template <>
void ResolveOrReject(dom::Promise& aPromise, nsPrinterListBase& aList,
                     const nsTArray<PrinterInfo>& aInfo) {
  nsTArray<RefPtr<nsIPrinter>> printers;
  printers.SetCapacity(aInfo.Length());
  for (auto& info : aInfo) {
    printers.AppendElement(aList.CreatePrinter(info));
  }
  aPromise.MaybeResolve(printers);
}

template <>
void ResolveOrReject(dom::Promise& aPromise, nsPrinterListBase& aList,
                     const Maybe<PrinterInfo>& aInfo) {
  if (aInfo) {
    aPromise.MaybeResolve(aList.CreatePrinter(aInfo.value()));
  } else {
    aPromise.MaybeRejectWithNotFoundError("Printer not found");
  }
}

}  // namespace mozilla

NS_IMETHODIMP nsPrinterListBase::GetPrinters(JSContext* aCx,
                                             Promise** aResult) {
  EnsureCommonPaperInfo(aCx);
  return mozilla::AsyncPromiseAttributeGetter(
      *this, mPrintersPromise, aCx, aResult, &nsPrinterListBase::Printers);
}

NS_IMETHODIMP nsPrinterListBase::GetPrinterByName(const nsAString& aPrinterName,
                                                  JSContext* aCx,
                                                  Promise** aResult) {
  EnsureCommonPaperInfo(aCx);
  return PrintBackgroundTaskPromise(*this, aCx, aResult,
                                    &nsPrinterListBase::PrinterByName,
                                    nsString{aPrinterName});
}

NS_IMETHODIMP nsPrinterListBase::GetPrinterBySystemName(
    const nsAString& aPrinterName, JSContext* aCx, Promise** aResult) {
  EnsureCommonPaperInfo(aCx);
  return PrintBackgroundTaskPromise(*this, aCx, aResult,
                                    &nsPrinterListBase::PrinterBySystemName,
                                    nsString{aPrinterName});
}

NS_IMETHODIMP nsPrinterListBase::GetNamedOrDefaultPrinter(
    const nsAString& aPrinterName, JSContext* aCx, Promise** aResult) {
  EnsureCommonPaperInfo(aCx);
  return PrintBackgroundTaskPromise(*this, aCx, aResult,
                                    &nsPrinterListBase::NamedOrDefaultPrinter,
                                    nsString{aPrinterName});
}

Maybe<PrinterInfo> nsPrinterListBase::NamedOrDefaultPrinter(
    nsString aName) const {
  if (Maybe<PrinterInfo> value = PrinterByName(std::move(aName))) {
    return value;
  }

  // Since the name had to be passed by-value, we can re-use it to fetch the
  // default printer name, potentially avoiding an extra string allocation.
  if (NS_SUCCEEDED(SystemDefaultPrinterName(aName))) {
    return PrinterByName(std::move(aName));
  }

  return Nothing();
}

NS_IMETHODIMP nsPrinterListBase::GetFallbackPaperList(JSContext* aCx,
                                                      Promise** aResult) {
  ErrorResult rv;
  nsCOMPtr<nsIGlobalObject> global = xpc::CurrentNativeGlobal(aCx);
  RefPtr<Promise> promise = Promise::Create(global, rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    *aResult = nullptr;
    return rv.StealNSResult();
  }

  EnsureCommonPaperInfo(aCx);
  nsTArray<RefPtr<nsPaper>> papers;
  papers.SetCapacity(nsPaper::kNumCommonPaperSizes);
  for (const auto& info : *mCommonPaperInfo) {
    papers.AppendElement(MakeRefPtr<nsPaper>(info));
  }

  promise->MaybeResolve(papers);
  promise.forget(aResult);
  return NS_OK;
}

void nsPrinterListBase::EnsureCommonPaperInfo(JSContext* aCx) {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  if (mCommonPaperInfo) {
    return;
  }
  RefPtr<CommonPaperInfoArray> localizedPaperInfo =
      MakeRefPtr<CommonPaperInfoArray>();
  CommonPaperInfoArray& paperArray = *localizedPaperInfo;
  // Apply localization to the names while constructing the PaperInfo, if
  // available (otherwise leave them as the internal keys, which are at least
  // somewhat recognizable).
  IgnoredErrorResult rv;
  nsTArray<nsCString> resIds = {
      "toolkit/printing/printUI.ftl"_ns,
  };
  RefPtr<Localization> l10n = Localization::Create(resIds, true);

  for (auto i : IntegerRange(nsPaper::kNumCommonPaperSizes)) {
    const CommonPaperSize& size = nsPaper::kCommonPaperSizes[i];
    PaperInfo& info = paperArray[i];

    nsAutoCString key{"printui-paper-"};
    key.Append(size.mLocalizableNameKey);
    nsAutoCString name;
    l10n->FormatValueSync(key, name, rv);

    // Fill out the info with our PWG size and the localized name.
    info.mId = size.mPWGName;
    CopyUTF8toUTF16(
        (rv.Failed() || name.IsEmpty())
            ? static_cast<const nsCString&>(size.mLocalizableNameKey)
            : name,
        info.mName);
    info.mSize = size.mSize;
    info.mUnwriteableMargin = Some(MarginDouble{});
  }
  mCommonPaperInfo = std::move(localizedPaperInfo);
}

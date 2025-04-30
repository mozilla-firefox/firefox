/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPrintJob.h"

#include "nsDebug.h"
#include "nsDocShell.h"
#include "nsReadableUtils.h"
#include "nsQueryObject.h"

#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/PBrowser.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/CustomEvent.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/StaticPrefs_print.h"
#include "mozilla/glean/PrintingMetrics.h"
#include "mozilla/Try.h"
#include "nsIBrowserChild.h"
#include "nsIOService.h"
#include "nsIScriptGlobalObject.h"
#include "nsIStringBundle.h"
#include "nsPIDOMWindow.h"
#include "nsPrintData.h"
#include "nsPrintObject.h"
#include "nsIDocShell.h"
#include "nsIURI.h"
#include "nsITextToSubURI.h"
#include "nsError.h"

#include "nsView.h"
#include <algorithm>

// Print Options
#include "nsIPrintSettings.h"
#include "nsIPrintSettingsService.h"
#include "nsGkAtoms.h"
#include "nsXPCOM.h"

static const char sPrintSettingsServiceContractID[] =
    "@mozilla.org/gfx/printsettings-service;1";

// Printing Timer
#include "nsPagePrintTimer.h"

// FrameSet
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"

// Misc
#include "gfxContext.h"
#include "mozilla/gfx/DrawEventRecorder.h"
#include "mozilla/layout/RemotePrintJobChild.h"
#include "nsISupportsUtils.h"
#include "nsIScriptContext.h"
#include "nsComponentManagerUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "Text.h"

#include "nsIDeviceContextSpec.h"
#include "nsDeviceContextSpecProxy.h"
#include "nsViewManager.h"

#include "nsPageSequenceFrame.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIWebBrowserChrome.h"
#include "mozilla/ReflowInput.h"
#include "nsIDocumentViewer.h"
#include "nsIDocumentViewerPrint.h"

#include "nsFocusManager.h"
#include "nsRange.h"
#include "mozilla/Components.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLFrameElement.h"
#include "mozilla/ServoStyleSet.h"

using namespace mozilla;
using namespace mozilla::dom;

//-----------------------------------------------------
// PR LOGGING
#include "mozilla/Logging.h"

#ifdef DEBUG
// PR_LOGGING is force to always be on (even in release builds)
// but we only want some of it on,
// #define EXTENDED_DEBUG_PRINTING
#endif

// this log level turns on the dumping of each document's layout info
#define DUMP_LAYOUT_LEVEL (static_cast<mozilla::LogLevel>(9))

#ifndef PR_PL
static mozilla::LazyLogModule gPrintingLog("printing");

#  define PR_PL(_p1) MOZ_LOG(gPrintingLog, mozilla::LogLevel::Debug, _p1);
#endif

#ifdef EXTENDED_DEBUG_PRINTING
static uint32_t gDumpFileNameCnt = 0;
static uint32_t gDumpLOFileNameCnt = 0;
#endif

#define PRT_YESNO(_p) ((_p) ? "YES" : "NO")

inline const char* LoggableTypeOfPO(const nsPrintObject* aPO) {
  // TODO(dholbert): These strings used to represent actual enum values, but
  // the enum type has been removed. We could replace them with more
  // descriptive names, if anyone uses this logging and cares to do so.
  return aPO->mParent ? "eIFrame" : "eDoc";
}

inline const char* ShortLoggableTypeOfPO(const nsPrintObject* aPO) {
  return aPO->mParent ? "IF" : "DC";
}

// This processes the selection on aOrigDoc and creates an inverted selection on
// aDoc, which it then deletes. If the start or end of the inverted selection
// ranges occur in text nodes then an ellipsis is added.
static nsresult DeleteNonSelectedNodes(Document& aDoc);

#ifdef EXTENDED_DEBUG_PRINTING
// Forward Declarations
static void DumpPrintObjectsListStart(const char* aStr,
                                      const nsTArray<nsPrintObject*>& aDocList);
static void DumpPrintObjectsTree(nsPrintObject* aPO, int aLevel = 0,
                                 FILE* aFD = nullptr);
static void DumpPrintObjectsTreeLayout(const UniquePtr<nsPrintObject>& aPO,
                                       nsDeviceContext* aDC, int aLevel = 0,
                                       FILE* aFD = nullptr);

#  define DUMP_DOC_LIST(_title) \
    DumpPrintObjectsListStart((_title), mPrintDocList);
#  define DUMP_DOC_TREE DumpPrintObjectsTree(mPrintObject.get());
#  define DUMP_DOC_TREELAYOUT \
    DumpPrintObjectsTreeLayout(mPrintObject, mPrt->mPrintDC);
#else
#  define DUMP_DOC_LIST(_title)
#  define DUMP_DOC_TREE
#  define DUMP_DOC_TREELAYOUT
#endif

// -------------------------------------------------------
// Helpers
// -------------------------------------------------------

/**
 * Build a tree of nsPrintObjects under aPO. It also appends a (depth first)
 * flat list of all the nsPrintObjects created to mPrintDocList. If
 * one of the nsPrintObject's document is the focused document, then the print
 * object is set as mSelectionRoot.
 * @param aParentPO The parent nsPrintObject to populate, must not be null.
 */
void nsPrintJob::BuildNestedPrintObjects(
    const UniquePtr<nsPrintObject>& aParentPO) {
  MOZ_ASSERT(aParentPO);

  // If aParentPO is for an iframe and its original document was the document
  // that had focus then always set as the selection root.
  if (aParentPO->mParent &&
      aParentPO->mDocument->GetProperty(nsGkAtoms::printisfocuseddoc)) {
    mSelectionRoot = aParentPO.get();
  } else if (!mSelectionRoot && aParentPO->HasSelection()) {
    // If there is no focused iframe but there is a selection in one or more
    // frames then we want to set the root nsPrintObject as the focus root so
    // that later EnablePrintingSelectionOnly can search for and enable all
    // nsPrintObjects containing selections.
    mSelectionRoot = mPrintObject.get();
  }

  for (auto& bc : aParentPO->mDocShell->GetBrowsingContext()->Children()) {
    nsCOMPtr<nsIDocShell> docShell = bc->GetDocShell();
    if (!docShell) {
      if (auto* cc = dom::ContentChild::GetSingleton()) {
        nsCOMPtr<nsIPrintSettingsService> printSettingsService =
            do_GetService(sPrintSettingsServiceContractID);
        embedding::PrintData printData;
        printSettingsService->SerializeToPrintData(mPrintSettings, &printData);
        Unused << cc->SendUpdateRemotePrintSettings(bc, printData);
      }
      continue;
    }

    RefPtr<Document> doc = docShell->GetDocument();
    MOZ_DIAGNOSTIC_ASSERT(doc);
    // We might find non-static documents here if the fission remoteness change
    // hasn't happened / finished yet. In that case, just skip them, the same
    // way we do for remote frames above.
    MOZ_DIAGNOSTIC_ASSERT(doc->IsStaticDocument() || doc->IsInitialDocument());
    if (!doc || !doc->IsStaticDocument()) {
      continue;
    }

    // Note: docShell and doc are known-non-null at this point; they've been
    // null-checked above (with null leading to 'continue' statements).
    auto childPO = MakeUnique<nsPrintObject>(*docShell, *doc, aParentPO.get());

    mPrintDocList.AppendElement(childPO.get());
    BuildNestedPrintObjects(childPO);
    aParentPO->mKids.AppendElement(std::move(childPO));
  }
}

static nsresult GetDefaultPrintSettings(nsIPrintSettings** aSettings) {
  *aSettings = nullptr;

  nsresult rv = NS_ERROR_FAILURE;
  nsCOMPtr<nsIPrintSettingsService> printSettingsService =
      do_GetService(sPrintSettingsServiceContractID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  return printSettingsService->GetDefaultPrintSettingsForPrinting(aSettings);
}

//-------------------------------------------------------

NS_IMPL_ISUPPORTS(nsPrintJob, nsIWebProgressListener, nsISupportsWeakReference)

//-------------------------------------------------------
nsPrintJob::~nsPrintJob() {
  Destroy();  // for insurance
  DisconnectPagePrintTimer();
}

bool nsPrintJob::CheckBeforeDestroy() const { return mPreparingForPrint; }

//-------------------------------------------------------
void nsPrintJob::Destroy() {
  if (mIsDestroying) {
    return;
  }
  mIsDestroying = true;

  DestroyPrintingData();

  mDocViewerPrint = nullptr;
}

//-------------------------------------------------------
void nsPrintJob::DestroyPrintingData() {
  mPrintObject = nullptr;
  mPrt = nullptr;
}

nsPrintJob::nsPrintJob(nsIDocumentViewerPrint& aDocViewerPrint,
                       nsIDocShell& aDocShell, Document& aOriginalDoc,
                       float aScreenDPI)
    : mDocViewerPrint(&aDocViewerPrint),
      mDocShell(do_GetWeakReference(&aDocShell)),
      mScreenDPI(aScreenDPI) {
  // Any state that we need from aOriginalDoc must be fetched and stored
  // here, since the document that the user selected to print may mutate
  // across consecutive PrintPreview() calls.

  Element* root = aOriginalDoc.GetRootElement();
  mDisallowSelectionPrint =
      root && root->HasAttr(nsGkAtoms::mozdisallowselectionprint);
}

//-----------------------------------------------------------------
std::tuple<nsPageSequenceFrame*, int32_t>
nsPrintJob::GetSeqFrameAndCountSheets() const {
  if (NS_WARN_IF(!mPrt)) {
    return {nullptr, 0};
  }

  const nsPrintObject* po = mPrintObject.get();
  if (NS_WARN_IF(!po)) {
    return {nullptr, 0};
  }

  // This is sometimes incorrectly called before the pres shell has been created
  // (bug 1141756). MOZ_DIAGNOSTIC_ASSERT so we'll still see the crash in
  // Nightly/Aurora in case the other patch fixes this.
  if (!po->mPresShell) {
    MOZ_DIAGNOSTIC_ASSERT(
        false, "GetSeqFrameAndCountSheets needs a non-null pres shell");
    return {nullptr, 0};
  }

  nsPageSequenceFrame* seqFrame = po->mPresShell->GetPageSequenceFrame();
  if (!seqFrame) {
    return {nullptr, 0};
  }

  // count the total number of sheets
  return {seqFrame, seqFrame->PrincipalChildList().GetLength()};
}

// Foward decl for Debug Helper Functions
#ifdef EXTENDED_DEBUG_PRINTING
#  ifdef XP_WIN
static int RemoveFilesInDir(const char* aDir);
#  endif
static void GetDocTitleAndURL(const UniquePtr<nsPrintObject>& aPO,
                              nsACString& aDocStr, nsACString& aURLStr);
static void DumpPrintObjectsTree(nsPrintObject* aPO, int aLevel, FILE* aFD);
static void DumpPrintObjectsList(const nsTArray<nsPrintObject*>& aDocList);
static void RootFrameList(nsPresContext* aPresContext, FILE* out,
                          const char* aPrefix);
static void DumpViews(nsIDocShell* aDocShell, FILE* out);
static void DumpLayoutData(const char* aTitleStr, const char* aURLStr,
                           nsPresContext* aPresContext, nsDeviceContext* aDC,
                           nsIFrame* aRootFrame, nsIDocShell* aDocShell,
                           FILE* aFD);
#endif

//--------------------------------------------------------------------------------

nsresult nsPrintJob::CommonPrint(bool aIsPrintPreview,
                                 nsIPrintSettings* aPrintSettings,
                                 nsIWebProgressListener* aWebProgressListener,
                                 Document& aSourceDoc) {
  // Callers must hold a strong reference to |this| to ensure that we stay
  // alive for the duration of this method, because our main owning reference
  // (on nsDocumentViewer) might be cleared during this function (if we cause
  // script to run and it cancels the print operation).

  nsresult rv = DoCommonPrint(aIsPrintPreview, aPrintSettings,
                              aWebProgressListener, aSourceDoc);
  if (NS_FAILED(rv)) {
    if (aIsPrintPreview) {
      mIsCreatingPrintPreview = false;
      SetIsPrintPreview(false);
    } else {
      SetIsPrinting(false);
    }
    if (rv != NS_ERROR_ABORT && rv != NS_ERROR_OUT_OF_MEMORY) {
      FirePrintingErrorEvent(rv);
    }
    DestroyPrintingData();
  }

  return rv;
}

nsresult nsPrintJob::DoCommonPrint(bool aIsPrintPreview,
                                   nsIPrintSettings* aPrintSettings,
                                   nsIWebProgressListener* aWebProgressListener,
                                   Document& aDoc) {
  MOZ_ASSERT(aDoc.IsStaticDocument());

  nsresult rv;

  // Grab the new instance with local variable to guarantee that it won't be
  // deleted during this method.
  // Note: Methods we call early below rely on mPrt being set.
  mPrt = new nsPrintData(aIsPrintPreview ? nsPrintData::eIsPrintPreview
                                         : nsPrintData::eIsPrinting);
  RefPtr<nsPrintData> printData = mPrt;

  if (aIsPrintPreview) {
    mIsCreatingPrintPreview = true;
    SetIsPrintPreview(true);
  } else {
    SetIsPrinting(true);
  }

  if (aWebProgressListener) {
    printData->mPrintProgressListeners.AppendObject(aWebProgressListener);
  }
  if (mRemotePrintJob) {
    // If we have a RemotePrintJob add it to the print progress listeners,
    // so it can forward to the parent.
    printData->mPrintProgressListeners.AppendElement(mRemotePrintJob);
  }

  // Get the docshell for this documentviewer
  nsCOMPtr<nsIDocShell> docShell(do_QueryReferent(mDocShell, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  // if they don't pass in a PrintSettings, then get the Global PS
  mPrintSettings = aPrintSettings;
  if (!mPrintSettings) {
    MOZ_TRY(GetDefaultPrintSettings(getter_AddRefs(mPrintSettings)));
  }

  {
    nsAutoScriptBlocker scriptBlocker;
    // Note: docShell is implicitly non-null via do_QueryReferent necessarily
    // having succeeded (if we got here).
    mPrintObject = MakeUnique<nsPrintObject>(*docShell, aDoc);
    mPrintDocList.AppendElement(mPrintObject.get());

    BuildNestedPrintObjects(mPrintObject);
  }

  // The nsAutoScriptBlocker above will now have been destroyed, which may
  // cause our print/print-preview operation to finish. In this case, we
  // should immediately return an error code so that the root caller knows
  // it shouldn't continue to do anything with this instance.
  if (mIsDestroying) {
    return NS_ERROR_FAILURE;
  }

  // XXX This isn't really correct...
  if (!mPrintObject->mDocument || !mPrintObject->mDocument->GetRootElement()) {
    return NS_ERROR_GFX_PRINTER_STARTDOC;
  }

  mPrintSettings->GetShrinkToFit(&mShrinkToFit);

  nsCOMPtr<nsIDeviceContextSpec> devspec;
  if (XRE_IsContentProcess()) {
    devspec = new nsDeviceContextSpecProxy(mRemotePrintJob);
  } else {
    devspec = do_CreateInstance("@mozilla.org/gfx/devicecontextspec;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  bool printSilently = false;
  mPrintSettings->GetPrintSilent(&printSilently);
  if (StaticPrefs::print_always_print_silent()) {
    printSilently = true;
  }

  if (mIsDoingPrinting && printSilently) {
    glean::printing::silent_print.Add(1);
  }

  MOZ_TRY(devspec->Init(mPrintSettings, mIsCreatingPrintPreview));

  printData->mPrintDC = new nsDeviceContext();
  MOZ_TRY(printData->mPrintDC->InitForPrinting(devspec));

  MOZ_TRY(EnablePOsForPrinting());

  if (!mIsCreatingPrintPreview) {
    printData->OnStartPrinting();
  }
  InitPrintDocConstruction(false);

  return NS_OK;
}

//---------------------------------------------------------------------------------
nsresult nsPrintJob::Print(Document& aDoc, nsIPrintSettings* aPrintSettings,
                           RemotePrintJobChild* aRemotePrintJob,
                           nsIWebProgressListener* aWebProgressListener) {
  mRemotePrintJob = aRemotePrintJob;
  return CommonPrint(false, aPrintSettings, aWebProgressListener, aDoc);
}

nsresult nsPrintJob::PrintPreview(Document& aDoc,
                                  nsIPrintSettings* aPrintSettings,
                                  nsIWebProgressListener* aWebProgressListener,
                                  PrintPreviewResolver&& aCallback) {
  // Take ownership of aCallback, otherwise a function further up the call
  // stack will call it to signal failure (by passing zero).
  mPrintPreviewCallback = std::move(aCallback);

  nsresult rv = CommonPrint(true, aPrintSettings, aWebProgressListener, aDoc);
  if (NS_FAILED(rv)) {
    if (mPrintPreviewCallback) {
      // signal error
      mPrintPreviewCallback(
          PrintPreviewResultInfo(0, 0, false, false, false, {}, {}, {}));
      mPrintPreviewCallback = nullptr;
    }
  }
  return rv;
}

int32_t nsPrintJob::GetRawNumPages() const {
  auto [seqFrame, numSheets] = GetSeqFrameAndCountSheets();
  Unused << numSheets;
  return seqFrame ? seqFrame->GetRawNumPages() : 0;
}

bool nsPrintJob::GetIsEmpty() const {
  auto [seqFrame, numSheets] = GetSeqFrameAndCountSheets();
  if (!seqFrame) {
    return true;
  }
  if (numSheets > 1) {
    return false;
  }
  return !seqFrame->GetPagesInFirstSheet();
}

int32_t nsPrintJob::GetPrintPreviewNumSheets() const {
  auto [seqFrame, numSheets] = GetSeqFrameAndCountSheets();
  Unused << seqFrame;
  return numSheets;
}

//-----------------------------------------------------------------
//-- Section: Pre-Reflow Methods
//-----------------------------------------------------------------

// static
void nsPrintJob::GetDisplayTitleAndURL(Document& aDoc,
                                       nsIPrintSettings* aSettings,
                                       DocTitleDefault aTitleDefault,
                                       nsAString& aTitle, nsAString& aURLStr) {
  aTitle.Truncate();
  aURLStr.Truncate();

  if (aSettings) {
    aSettings->GetTitle(aTitle);
    aSettings->GetDocURL(aURLStr);
  }

  if (aTitle.IsEmpty()) {
    aDoc.GetTitle(aTitle);
    if (aTitle.IsEmpty()) {
      if (!aURLStr.IsEmpty() &&
          aTitleDefault == DocTitleDefault::eDocURLElseFallback) {
        aTitle = aURLStr;
      } else {
        nsCOMPtr<nsIStringBundle> brandBundle;
        nsCOMPtr<nsIStringBundleService> svc =
            mozilla::components::StringBundle::Service();
        if (svc) {
          svc->CreateBundle("chrome://branding/locale/brand.properties",
                            getter_AddRefs(brandBundle));
          if (brandBundle) {
            brandBundle->GetStringFromName("brandShortName", aTitle);
          }
        }
        if (aTitle.IsEmpty()) {
          aTitle.AssignLiteral(u"Mozilla Document");
        }
      }
    }
  }

  if (aURLStr.IsEmpty()) {
    nsIURI* url = aDoc.GetDocumentURI();
    if (!url) {
      return;
    }

    nsCOMPtr<nsIURI> exposableURI = net::nsIOService::CreateExposableURI(url);
    nsAutoCString urlCStr;
    nsresult rv = exposableURI->GetSpec(urlCStr);
    if (NS_FAILED(rv)) {
      return;
    }

    nsCOMPtr<nsITextToSubURI> textToSubURI =
        do_GetService(NS_ITEXTTOSUBURI_CONTRACTID, &rv);
    if (NS_FAILED(rv)) {
      return;
    }

    textToSubURI->UnEscapeURIForUI(urlCStr, aURLStr);
  }
}

//---------------------------------------------------------------------
nsresult nsPrintJob::DocumentReadyForPrinting() {
  // Send the document to the printer...
  nsresult rv = SetupToPrintContent();
  if (NS_FAILED(rv)) {
    // The print job was canceled or there was a problem
    // So remove all other documents from the print list
    DonePrintingSheets(nullptr, rv);
  }
  return rv;
}

/** ---------------------------------------------------
 *  Cleans up when an error occurred
 */
nsresult nsPrintJob::CleanupOnFailure(nsresult aResult, bool aIsPrinting) {
  PR_PL(("****  Failed %s - rv 0x%" PRIX32,
         aIsPrinting ? "Printing" : "Print Preview",
         static_cast<uint32_t>(aResult)));
  PROFILER_MARKER_TEXT("PrintJob", LAYOUT_Printing, MarkerStack::Capture(),
                       "nsPrintJob::CleanupOnFailure"_ns);

  /* cleanup... */
  if (mPagePrintTimer) {
    mPagePrintTimer->Stop();
    DisconnectPagePrintTimer();
  }

  if (aIsPrinting) {
    SetIsPrinting(false);
  } else {
    SetIsPrintPreview(false);
    mIsCreatingPrintPreview = false;
  }

  /* cleanup done, let's fire-up an error dialog to notify the user
   * what went wrong...
   *
   * When rv == NS_ERROR_ABORT, it means we want out of the
   * print job without displaying any error messages
   */
  if (aResult != NS_ERROR_ABORT) {
    FirePrintingErrorEvent(aResult);
  }

  FirePrintCompletionEvent();

  return aResult;
}

//---------------------------------------------------------------------
void nsPrintJob::FirePrintingErrorEvent(nsresult aPrintError) {
  PROFILER_MARKER_TEXT("PrintJob", LAYOUT_Printing, MarkerStack::Capture(),
                       "nsPrintJob::FirePrintingErrorEvent"_ns);

  if (mPrintPreviewCallback) {
    // signal error
    mPrintPreviewCallback(
        PrintPreviewResultInfo(0, 0, false, false, false, {}, {}, {}));
    mPrintPreviewCallback = nullptr;
  }

  nsCOMPtr<nsIDocumentViewer> viewer = do_QueryInterface(mDocViewerPrint);
  if (NS_WARN_IF(!viewer)) {
    return;
  }

  const RefPtr<Document> doc = viewer->GetDocument();
  const RefPtr<CustomEvent> event = NS_NewDOMCustomEvent(doc, nullptr, nullptr);

  MOZ_ASSERT(event);

  AutoJSAPI jsapi;
  if (!jsapi.Init(event->GetParentObject())) {
    return;
  }
  JSContext* cx = jsapi.cx();

  JS::Rooted<JS::Value> detail(
      cx, JS::NumberValue(static_cast<double>(aPrintError)));
  event->InitCustomEvent(cx, u"PrintingError"_ns, false, false, detail);
  event->SetTrusted(true);

  // Event listeners in chrome shouldn't delete this.
  AsyncEventDispatcher::RunDOMEventWhenSafe(*doc, *event,
                                            ChromeOnlyDispatch::eYes);

  // Inform any progress listeners of the Error.
  if (mPrt) {
    // Note that nsPrintData::DoOnStatusChange() will call some listeners.
    // So, mPrt can be cleared or recreated.
    RefPtr<nsPrintData> printData = mPrt;
    printData->DoOnStatusChange(aPrintError);
  }
}

//-----------------------------------------------------------------
//-- Section: Reflow Methods
//-----------------------------------------------------------------

nsresult nsPrintJob::ReconstructAndReflow() {
  if (NS_WARN_IF(!mPrt)) {
    return NS_ERROR_FAILURE;
  }

#if defined(XP_WIN) && defined(EXTENDED_DEBUG_PRINTING)
  // We need to clear all the output files here
  // because they will be re-created with second reflow of the docs
  if (MOZ_LOG_TEST(gPrintingLog, DUMP_LAYOUT_LEVEL)) {
    RemoveFilesInDir(".\\");
    gDumpFileNameCnt = 0;
    gDumpLOFileNameCnt = 0;
  }
#endif

  // In this loop, it's conceivable that one of our helpers might clear mPrt,
  // while we're using it & its members!  So we capture it in an owning local
  // reference & use that instead of using mPrt directly.
  RefPtr<nsPrintData> printData = mPrt;
  for (nsPrintObject* po : mPrintDocList) {
    if (!po->PrintingIsEnabled() || po->mInvisible) {
      continue;
    }

    // When the print object has been marked as "print the document" (i.e,
    // po->PrintingIsEnabled() is true), mPresContext and mPresShell should be
    // non-nullptr (i.e., should've been created for the print) since they
    // are necessary to print the document.
    MOZ_ASSERT(po->mPresContext && po->mPresShell,
               "mPresContext and mPresShell shouldn't be nullptr when the "
               "print object "
               "has been marked as \"print the document\"");

    UpdateZoomRatio(po);

    po->mPresContext->SetPageScale(po->mZoomRatio);

    // Calculate scale factor from printer to screen
    float printDPI = float(AppUnitsPerCSSInch()) /
                     float(printData->mPrintDC->AppUnitsPerDevPixel());
    po->mPresContext->SetPrintPreviewScale(mScreenDPI / printDPI);

    RefPtr<PresShell> presShell(po->mPresShell);
    if (NS_WARN_IF(presShell->IsDestroying())) {
      return NS_ERROR_FAILURE;
    }

    presShell->ReconstructFrames();

    // If the printing was canceled or restarted with different data,
    // let's stop doing this printing.
    if (NS_WARN_IF(mPrt != printData)) {
      return NS_ERROR_FAILURE;
    }

    // For all views except the first one, setup the root view.
    // ??? Can there be multiple po for the top-level-document?
    bool documentIsTopLevel = true;
    if (po->mParent) {
      nsSize adjSize;
      bool doReturn;
      nsresult rv = SetRootView(po, doReturn, documentIsTopLevel, adjSize);

      MOZ_ASSERT(!documentIsTopLevel, "How could this happen?");

      if (NS_FAILED(rv) || doReturn) {
        return rv;
      }
    }

    presShell->FlushPendingNotifications(FlushType::Layout);

    if (NS_WARN_IF(presShell->IsDestroying())) {
      return NS_ERROR_FAILURE;
    }

    // If the printing was canceled or restarted with different data,
    // let's stop doing this printing.
    if (NS_WARN_IF(mPrt != printData)) {
      return NS_ERROR_FAILURE;
    }

    po->mDocument->UpdateRemoteFrameEffects();
    MOZ_TRY(UpdateSelectionAndShrinkPrintObject(po, documentIsTopLevel));
  }
  return NS_OK;
}

//-------------------------------------------------------
nsresult nsPrintJob::SetupToPrintContent() {
  // This method may be called while DoCommonPrint() initializes the instance
  // when its script blocker goes out of scope.  In such case, this cannot do
  // its job as expected because some objects in mPrt have not been initialized
  // yet but they are necessary.
  // Note: it shouldn't be possible for mPrintObject to be null; we check
  // it for good measure (after we check its owner) before we start
  // dereferencing it below.
  if (NS_WARN_IF(!mPrt) || NS_WARN_IF(!mPrintObject)) {
    return NS_ERROR_FAILURE;
  }

  // If this is creating print preview, mPrintObject->mPresContext and
  // mPrintObject->mPresShell need to be non-nullptr because this cannot
  // initialize page sequence frame without them at end of this method since
  // page sequence frame has already been destroyed or not been created yet.
  if (mIsCreatingPrintPreview && (NS_WARN_IF(!mPrintObject->mPresContext) ||
                                  NS_WARN_IF(!mPrintObject->mPresShell))) {
    return NS_ERROR_FAILURE;
  }

  // If this is printing some documents (not print-previewing the documents),
  // mPrintObject->mPresContext and mPrintObject->mPresShell can be
  // nullptr only when mPrintObject->PrintingIsEnabled() is false.  E.g.,
  // if the document has a <frameset> element and it's printing only content in
  // a <frame> element or all <frame> elements separately.
  MOZ_ASSERT(
      (!mIsCreatingPrintPreview && !mPrintObject->PrintingIsEnabled()) ||
          (mPrintObject->mPresContext && mPrintObject->mPresShell),
      "mPresContext and mPresShell shouldn't be nullptr when printing the "
      "document or creating print-preview");

  bool didReconstruction = false;

  // This method works with mPrintObject.  So, we need to guarantee that
  // it won't be deleted in this method.  We achieve this by holding a strong
  // local reference to mPrt, which in turn keeps mPrintObject alive.
  RefPtr<nsPrintData> printData = mPrt;

  // If some new content got loaded since the initial reflow rebuild
  // everything.
  if (mDidLoadDataForPrinting) {
    nsresult rv = ReconstructAndReflow();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    // If the printing was canceled or restarted with different data,
    // let's stop doing this printing.
    if (NS_WARN_IF(mPrt != printData)) {
      return NS_ERROR_FAILURE;
    }
    didReconstruction = true;
  }

  // Here is where we figure out if extra reflow for shrinking the content
  // is required.
  if (mShrinkToFit) {
    mShrinkToFitFactor = mPrintObject->mShrinkRatio;

    if (mShrinkToFitFactor < 0.998f) {
      nsresult rv = ReconstructAndReflow();
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      // If the printing was canceled or restarted with different data,
      // let's stop doing this printing.
      if (NS_WARN_IF(mPrt != printData)) {
        return NS_ERROR_FAILURE;
      }
      didReconstruction = true;
    }

    if (MOZ_LOG_TEST(gPrintingLog, LogLevel::Debug)) {
      float calcRatio = mPrintObject->mShrinkRatio;
      PR_PL(
          ("*******************************************************************"
           "*******\n"));
      PR_PL(("STF Ratio is: %8.5f Effective Ratio: %8.5f Diff: %8.5f\n",
             mShrinkToFitFactor, calcRatio, mShrinkToFitFactor - calcRatio));
      PR_PL(
          ("*******************************************************************"
           "*******\n"));
    }
  }

  // If the frames got reconstructed and reflowed the number of pages might
  // has changed.
  if (didReconstruction) {
    FirePrintPreviewUpdateEvent();
    // If the printing was canceled or restarted with different data,
    // let's stop doing this printing.
    if (NS_WARN_IF(mPrt != printData)) {
      return NS_ERROR_FAILURE;
    }
  }

  DUMP_DOC_LIST(("\nAfter Reflow------------------------------------------"));
  PR_PL(("\n"));
  PR_PL(("-------------------------------------------------------\n"));
  PR_PL(("\n"));

  CalcNumPrintablePages(mNumPrintablePages);

  PR_PL(("--- Printing %d pages\n", mNumPrintablePages));
  DUMP_DOC_TREELAYOUT;

  // Print listener setup...
  printData->OnStartPrinting();

  // If the printing was canceled or restarted with different data,
  // let's stop doing this printing.
  if (NS_WARN_IF(mPrt != printData)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoString fileNameStr;
  // check to see if we are printing to a file
  if (mPrintSettings->GetOutputDestination() ==
      nsIPrintSettings::kOutputDestinationFile) {
    // On some platforms the BeginDocument needs to know the name of the file.
    mPrintSettings->GetToFileName(fileNameStr);
  }

  nsAutoString docTitleStr;
  nsAutoString docURLStr;
  GetDisplayTitleAndURL(*mPrintObject->mDocument, mPrintSettings,
                        DocTitleDefault::eDocURLElseFallback, docTitleStr,
                        docURLStr);

  int32_t startPage = 1;
  int32_t endPage = mNumPrintablePages;

  nsTArray<int32_t> ranges;
  mPrintSettings->GetPageRanges(ranges);
  for (size_t i = 0; i < ranges.Length(); i += 2) {
    startPage = std::max(1, std::min(startPage, ranges[i]));
    endPage = std::min(mNumPrintablePages, std::max(endPage, ranges[i + 1]));
  }

  nsresult rv = NS_OK;
  // BeginDocument may pass back a FAILURE code
  // i.e. On Windows, if you are printing to a file and hit "Cancel"
  //      to the "File Name" dialog, this comes back as an error
  // Don't start printing when regression test are executed
  if (mIsDoingPrinting) {
    rv = printData->mPrintDC->BeginDocument(docTitleStr, fileNameStr, startPage,
                                            endPage);
  }

  if (mIsCreatingPrintPreview) {
    // Copy docTitleStr and docURLStr to the pageSequenceFrame, to be displayed
    // in the header
    nsPageSequenceFrame* seqFrame =
        mPrintObject->mPresShell->GetPageSequenceFrame();
    if (seqFrame) {
      seqFrame->StartPrint(mPrintObject->mPresContext, mPrintSettings,
                           docTitleStr, docURLStr);
    }
  }

  PR_PL(("****************** Begin Document ************************\n"));

  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_ABORT,
                         "Failed to begin document for printing");
    return rv;
  }

  // This will print the docshell document
  // when it completes asynchronously in the DonePrintingSheets method
  // it will check to see if there are more docshells to be printed and
  // then PrintDocContent will be called again.

  if (mIsDoingPrinting) {
    // Double-check that mPrintObject is non-null, because it could have
    // gotten cleared while running script since we last checked it.
    if (NS_WARN_IF(!mPrintObject)) {
      return NS_ERROR_FAILURE;
    }

    PrintDocContent(mPrintObject, rv);  // ignore return value
  }

  return rv;
}

//-------------------------------------------------------
// Recursively reflow each sub-doc and then calc
// all the frame locations of the sub-docs
nsresult nsPrintJob::ReflowDocList(const UniquePtr<nsPrintObject>& aPO) {
  NS_ENSURE_ARG_POINTER(aPO);

  // Check to see if the subdocument's element has been hidden by the parent
  // document
  if (aPO->mParent && aPO->mParent->mPresShell) {
    nsIFrame* frame =
        aPO->mContent ? aPO->mContent->GetPrimaryFrame() : nullptr;
    if (!frame || !frame->StyleVisibility()->IsVisible()) {
      aPO->EnablePrinting(false);
      aPO->mInvisible = true;
      return NS_OK;
    }
  }

  UpdateZoomRatio(aPO.get());

  // Reflow the PO
  MOZ_TRY(ReflowPrintObject(aPO));

  for (const UniquePtr<nsPrintObject>& kid : aPO->mKids) {
    MOZ_TRY(ReflowDocList(kid));
  }
  return NS_OK;
}

void nsPrintJob::FirePrintPreviewUpdateEvent() {
  // Dispatch the event only while in PrintPreview. When printing, there is no
  // listener bound to this event and therefore no need to dispatch it.
  if (mCreatedForPrintPreview && !mIsDoingPrinting) {
    nsCOMPtr<nsIDocumentViewer> viewer = do_QueryInterface(mDocViewerPrint);
    if (Document* document = viewer->GetDocument()) {
      AsyncEventDispatcher::RunDOMEventWhenSafe(
          *document, u"printPreviewUpdate"_ns, CanBubble::eYes,
          ChromeOnlyDispatch::eYes);
    }
  }
}

nsresult nsPrintJob::InitPrintDocConstruction(bool aHandleError) {
  // Guarantee that mPrintObject won't be deleted.
  RefPtr<nsPrintData> printData = mPrt;

  if (NS_WARN_IF(!printData)) {
    return NS_ERROR_FAILURE;
  }

  // Attach progressListener to catch network requests.
  mDidLoadDataForPrinting = false;

  {
    AutoRestore<bool> restore{mDoingInitialReflow};
    mDoingInitialReflow = true;

    nsCOMPtr<nsIWebProgress> webProgress =
        do_QueryInterface(mPrintObject->mDocShell);
    webProgress->AddProgressListener(static_cast<nsIWebProgressListener*>(this),
                                     nsIWebProgress::NOTIFY_STATE_REQUEST);

    MOZ_TRY(ReflowDocList(mPrintObject));

    FirePrintPreviewUpdateEvent();
  }

  MaybeResumePrintAfterResourcesLoaded(aHandleError);
  return NS_OK;
}

bool nsPrintJob::ShouldResumePrint() const {
  if (mDoingInitialReflow) {
    return false;
  }
  Document* doc = mPrintObject->mDocument;
  MOZ_ASSERT(doc);
  NS_ENSURE_TRUE(doc, true);
  nsCOMPtr<nsILoadGroup> lg = doc->GetDocumentLoadGroup();
  NS_ENSURE_TRUE(lg, true);
  bool pending = false;
  nsresult rv = lg->IsPending(&pending);
  NS_ENSURE_SUCCESS(rv, true);
  return !pending;
}

nsresult nsPrintJob::MaybeResumePrintAfterResourcesLoaded(
    bool aCleanupOnError) {
  if (!ShouldResumePrint()) {
    mDidLoadDataForPrinting = true;
    return NS_OK;
  }
  // If Destroy() has already been called, mPtr is nullptr.  Then, the instance
  // needs to do nothing anymore in this method.
  // Note: it shouldn't be possible for mPrintObject to be null; we
  // just check it for good measure, as we check its owner.
  // Note: it shouldn't be possible for mPrintObject->mDocShell to be
  // null; we just check it for good measure, as we check its owner.
  if (!mPrt || NS_WARN_IF(!mPrintObject) ||
      NS_WARN_IF(!mPrintObject->mDocShell)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIWebProgress> webProgress =
      do_QueryInterface(mPrintObject->mDocShell);

  webProgress->RemoveProgressListener(
      static_cast<nsIWebProgressListener*>(this));

  nsresult rv;
  if (mIsDoingPrinting) {
    rv = DocumentReadyForPrinting();
  } else {
    rv = FinishPrintPreview();
  }

  /* cleaup on failure + notify user */
  if (aCleanupOnError && NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_ABORT,
                         "nsPrintJob::ResumePrintAfterResourcesLoaded failed");
    CleanupOnFailure(rv, !mIsDoingPrinting);
  }

  return rv;
}

////////////////////////////////////////////////////////////////////////////////
// nsIWebProgressListener

MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
nsPrintJob::OnStateChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                          uint32_t aStateFlags, nsresult aStatus) {
  if (aStateFlags & STATE_STOP) {
    // If all resources are loaded, then finish and reflow.
    MaybeResumePrintAfterResourcesLoaded(/* aCleanupOnError */ true);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPrintJob::OnProgressChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                             int32_t aCurSelfProgress, int32_t aMaxSelfProgress,
                             int32_t aCurTotalProgress,
                             int32_t aMaxTotalProgress) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
nsPrintJob::OnLocationChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                             nsIURI* aLocation, uint32_t aFlags) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
nsPrintJob::OnStatusChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                           nsresult aStatus, const char16_t* aMessage) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
nsPrintJob::OnSecurityChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                             uint32_t aState) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
nsPrintJob::OnContentBlockingEvent(nsIWebProgress* aWebProgress,
                                   nsIRequest* aRequest, uint32_t aEvent) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

//-------------------------------------------------------

void nsPrintJob::UpdateZoomRatio(nsPrintObject* aPO) {
  if (!aPO->mParent) {
    if (mShrinkToFit) {
      aPO->mZoomRatio = mShrinkToFitFactor;
      // If we're actually going to scale (the factor is less than 1), we
      // reduce the scale factor slightly to avoid the possibility of floating
      // point rounding error causing slight clipping of the longest lines.
      if (aPO->mZoomRatio != 1.0f) {
        aPO->mZoomRatio -= 0.005f;
      }
    } else {
      double scaling;
      mPrintSettings->GetScaling(&scaling);
      aPO->mZoomRatio = float(scaling);
    }
  }
}

nsresult nsPrintJob::UpdateSelectionAndShrinkPrintObject(
    nsPrintObject* aPO, bool aDocumentIsTopLevel) {
  PresShell* displayPresShell = aPO->mDocShell->GetPresShell();
  // Transfer Selection Ranges to the new Print PresShell
  RefPtr<Selection> selection, selectionPS;
  // It's okay if there is no display shell, just skip copying the selection
  if (displayPresShell) {
    selection = displayPresShell->GetCurrentSelection(SelectionType::eNormal);
  }
  selectionPS = aPO->mPresShell->GetCurrentSelection(SelectionType::eNormal);

  // Reset all existing selection ranges that might have been added by calling
  // this function before.
  if (selectionPS) {
    selectionPS->RemoveAllRanges(IgnoreErrors());
  }
  if (selection && selectionPS) {
    const uint32_t rangeCount = selection->RangeCount();
    for (const uint32_t inx : IntegerRange(rangeCount)) {
      MOZ_ASSERT(selection->RangeCount() == rangeCount);
      const RefPtr<nsRange> range{selection->GetRangeAt(inx)};
      selectionPS->AddRangeAndSelectFramesAndNotifyListeners(*range,
                                                             IgnoreErrors());
    }
  }

  // If we are trying to shrink the contents to fit on the page
  // we must first locate the "pageContent" frame
  // Then we walk the frame tree and look for the "xmost" frame
  // this is the frame where the right-hand side of the frame extends
  // the furthest
  if (mShrinkToFit && aDocumentIsTopLevel) {
    nsPageSequenceFrame* pageSeqFrame = aPO->mPresShell->GetPageSequenceFrame();
    NS_ENSURE_STATE(pageSeqFrame);
    aPO->mShrinkRatio = pageSeqFrame->GetSTFPercent();
    // Limit the shrink-to-fit scaling for some text-ish type of documents.
    nsAutoString contentType;
    aPO->mPresShell->GetDocument()->GetContentType(contentType);
    if (contentType.EqualsLiteral("application/xhtml+xml") ||
        StringBeginsWith(contentType, u"text/"_ns)) {
      int32_t limitPercent =
          Preferences::GetInt("print.shrink-to-fit.scale-limit-percent", 20);
      limitPercent = std::max(0, limitPercent);
      limitPercent = std::min(100, limitPercent);
      float minShrinkRatio = float(limitPercent) / 100;
      aPO->mShrinkRatio = std::max(aPO->mShrinkRatio, minShrinkRatio);
    }
  }
  return NS_OK;
}

nsView* nsPrintJob::GetParentViewForRoot() {
  if (mIsCreatingPrintPreview) {
    if (nsCOMPtr<nsIDocumentViewer> viewer =
            do_QueryInterface(mDocViewerPrint)) {
      return viewer->FindContainerView();
    }
  }
  return nullptr;
}

nsresult nsPrintJob::SetRootView(nsPrintObject* aPO, bool& doReturn,
                                 bool& documentIsTopLevel, nsSize& adjSize) {
  bool canCreateScrollbars = true;

  nsView* rootView;
  nsView* parentView = nullptr;

  doReturn = false;

  if (aPO->mParent && aPO->mParent->PrintingIsEnabled()) {
    nsIFrame* frame =
        aPO->mContent ? aPO->mContent->GetPrimaryFrame() : nullptr;
    // Without a frame, this document can't be displayed; therefore, there is no
    // point to reflowing it
    if (!frame) {
      aPO->EnablePrinting(false);
      doReturn = true;
      return NS_OK;
    }

    // XXX If printing supported printing document hierarchies with non-constant
    // zoom this would be wrong as we use the same mPrt->mPrintDC for all
    // subdocuments.
    adjSize = frame->GetContentRect().Size();
    documentIsTopLevel = false;
    // presshell exists because parent is printable

    // the top nsPrintObject's widget will always have scrollbars
    if (frame && frame->IsSubDocumentFrame()) {
      nsView* view = frame->GetView();
      NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
      view = view->GetFirstChild();
      NS_ENSURE_TRUE(view, NS_ERROR_FAILURE);
      parentView = view;
      canCreateScrollbars = false;
    }
  } else {
    adjSize = mPrt->mPrintDC->GetDeviceSurfaceDimensions();
    documentIsTopLevel = true;
    parentView = GetParentViewForRoot();
  }

  if (aPO->mViewManager->GetRootView()) {
    // Reuse the root view that is already on the root frame.
    rootView = aPO->mViewManager->GetRootView();
    // Remove it from its existing parent if necessary
    aPO->mViewManager->RemoveChild(rootView);
    rootView->SetParent(parentView);
  } else {
    // Create a child window of the parent that is our "root view/window"
    nsRect tbounds = nsRect(nsPoint(0, 0), adjSize);
    rootView = aPO->mViewManager->CreateView(tbounds, parentView);
    NS_ENSURE_TRUE(rootView, NS_ERROR_OUT_OF_MEMORY);
  }

  if (mIsCreatingPrintPreview && documentIsTopLevel) {
    aPO->mPresContext->SetPaginatedScrolling(canCreateScrollbars);
  }

  // Setup hierarchical relationship in view manager
  aPO->mViewManager->SetRootView(rootView);

  return NS_OK;
}

// Reflow a nsPrintObject
nsresult nsPrintJob::ReflowPrintObject(const UniquePtr<nsPrintObject>& aPO) {
  NS_ENSURE_STATE(aPO);

  if (!aPO->PrintingIsEnabled()) {
    return NS_OK;
  }

  NS_ASSERTION(!aPO->mPresContext, "Recreating prescontext");

  // Guarantee that mPrt and the objects it owns won't be deleted in this method
  // because it might be cleared if other modules called from here may fire
  // events, notifying observers and/or listeners.
  RefPtr<nsPrintData> printData = mPrt;

  // create the PresContext
  nsPresContext::nsPresContextType type =
      mIsCreatingPrintPreview ? nsPresContext::eContext_PrintPreview
                              : nsPresContext::eContext_Print;
  const bool shouldBeRoot =
      (!aPO->mParent || !aPO->mParent->PrintingIsEnabled()) &&
      !GetParentViewForRoot();
  aPO->mPresContext = shouldBeRoot ? new nsRootPresContext(aPO->mDocument, type)
                                   : new nsPresContext(aPO->mDocument, type);
  aPO->mPresContext->SetPrintSettings(mPrintSettings);

  // init it with the DC
  MOZ_TRY(aPO->mPresContext->Init(printData->mPrintDC));

  aPO->mViewManager = new nsViewManager();

  MOZ_TRY(aPO->mViewManager->Init(printData->mPrintDC));

  bool doReturn = false;
  bool documentIsTopLevel = false;
  nsSize adjSize;

  nsresult rv = SetRootView(aPO.get(), doReturn, documentIsTopLevel, adjSize);

  if (NS_FAILED(rv) || doReturn) {
    return rv;
  }

  // Here, we inform nsPresContext of the page size. Note that 'adjSize' is
  // *usually* the page size, but we need to check. Strictly speaking, adjSize
  // is the *device output size*, which is really the dimensions of a "sheet"
  // rather than a "page" (an important distinction in an N-pages-per-sheet
  // scenario). For some pages-per-sheet values, the pages are orthogonal to
  // the sheet; we adjust for that here by swapping the width with the height.
  nsSize pageSize = adjSize;
  if (mPrintSettings->HasOrthogonalPagesPerSheet()) {
    std::swap(pageSize.width, pageSize.height);
  }
  // XXXalaskanemily: Is this actually necessary? We set it again before the
  // first reflow.
  aPO->mPresContext->SetPageSize(pageSize);

  int32_t p2a = aPO->mPresContext->DeviceContext()->AppUnitsPerDevPixel();
  if (documentIsTopLevel && mIsCreatingPrintPreview) {
    if (nsCOMPtr<nsIDocumentViewer> viewer =
            do_QueryInterface(mDocViewerPrint)) {
      // If we're print-previewing and the top level document, use the bounds
      // from our doc viewer. Page bounds is not what we want.
      LayoutDeviceIntRect bounds;
      viewer->GetBounds(bounds);
      adjSize = nsSize(bounds.width * p2a, bounds.height * p2a);
    }
  }
  aPO->mPresContext->SetIsRootPaginatedDocument(documentIsTopLevel);
  aPO->mPresContext->SetVisibleArea(nsRect(nsPoint(), adjSize));
  aPO->mPresContext->SetPageScale(aPO->mZoomRatio);
  // Calculate scale factor from printer to screen
  float printDPI = float(AppUnitsPerCSSInch()) / float(p2a);
  aPO->mPresContext->SetPrintPreviewScale(mScreenDPI / printDPI);

  // Do CreatePresShell() after we setup the page size, the visible area, and
  // the flag |mIsRootPaginatedDocument|, to make sure we can resolve the
  // correct viewport size for the print preview page when notifying the media
  // feature values changed. See au_viewport_size_for_viewport_unit_resolution()
  // in media_queries.rs for more details.
  RefPtr<Document> doc = aPO->mDocument;
  RefPtr<nsPresContext> presContext = aPO->mPresContext;
  RefPtr<nsViewManager> viewManager = aPO->mViewManager;

  aPO->mPresShell = doc->CreatePresShell(presContext, viewManager);
  if (!aPO->mPresShell) {
    return NS_ERROR_FAILURE;
  }

  // If we're printing selection then remove the nonselected nodes from our
  // cloned document.
  if (mPrintSettings->GetPrintSelectionOnly()) {
    // If we fail to remove the nodes then we should fail to print, because if
    // the user was trying to print a small selection from a large document,
    // sending the whole document to a real printer would be very frustrating.
    MOZ_TRY(DeleteNonSelectedNodes(*aPO->mDocument));
  }

  aPO->mPresShell->BeginObservingDocument();

  PR_PL(
      ("In DV::ReflowPrintObject PO: %p pS: %p (%9s) Setting page size w,h to "
       "%d,%d\n",
       aPO.get(), aPO->mPresShell.get(), LoggableTypeOfPO(aPO.get()),
       pageSize.width, pageSize.height));

  if (mIsCreatingPrintPreview && documentIsTopLevel) {
    mDocViewerPrint->SetPrintPreviewPresentation(
        aPO->mViewManager, aPO->mPresContext, aPO->mPresShell.get());
  }

  MOZ_TRY(aPO->mPresShell->Initialize());
  NS_ASSERTION(aPO->mPresShell, "Presshell should still be here");

  RefPtr<PresShell> presShell = aPO->mPresShell;
  {
    const ServoStyleSet::PageSizeAndOrientation sizeAndOrientation =
        presShell->StyleSet()->GetDefaultPageSizeAndOrientation();
    // XXX Should we enable this for known save-to-PDF pseudo-printers once
    // bug 1826301 is fixed?
    if (mPrintSettings->GetOutputFormat() ==
            nsIPrintSettings::kOutputFormatPDF &&
        StaticPrefs::
            print_save_as_pdf_use_page_rule_size_as_paper_size_enabled()) {
      mMaybeCSSPageSize = sizeAndOrientation.size;
      if (sizeAndOrientation.size) {
        pageSize = sizeAndOrientation.size.value();
        aPO->mPresContext->SetPageSize(pageSize);
      }
    }

    // If the document has a specified CSS page-size, we rotate the page to
    // reflect this. Changing the orientation is reflected by the result of
    // FinishPrintPreview, so that the frontend can reflect this.
    // The new document has not yet been reflowed, so we have to query the
    // original document for any CSS page-size.
    if (sizeAndOrientation.orientation) {
      switch (sizeAndOrientation.orientation.value()) {
        case StylePageSizeOrientation::Landscape:
          if (pageSize.width < pageSize.height) {
            // Paper is in portrait, CSS page size is landscape.
            std::swap(pageSize.width, pageSize.height);
          }
          break;
        case StylePageSizeOrientation::Portrait:
          if (pageSize.width > pageSize.height) {
            // Paper is in landscape, CSS page size is portrait.
            std::swap(pageSize.width, pageSize.height);
          }
          break;
      }
      mMaybeCSSPageLandscape = Some(sizeAndOrientation.orientation.value() ==
                                    StylePageSizeOrientation::Landscape);
      aPO->mPresContext->SetPageSize(pageSize);
    }
  }
  // Make sure animations are active.
  for (DocumentTimeline* tl : aPO->mDocument->Timelines()) {
    tl->TriggerAllPendingAnimationsNow();
  }
  // Process the reflow event Initialize posted
  presShell->FlushPendingNotifications(FlushType::Layout);
  aPO->mDocument->UpdateRemoteFrameEffects();

  MOZ_TRY(UpdateSelectionAndShrinkPrintObject(aPO.get(), documentIsTopLevel));

#ifdef EXTENDED_DEBUG_PRINTING
  if (MOZ_LOG_TEST(gPrintingLog, DUMP_LAYOUT_LEVEL)) {
    nsAutoCString docStr;
    nsAutoCString urlStr;
    GetDocTitleAndURL(aPO, docStr, urlStr);
    char filename[256];
    sprintf(filename, "print_dump_%d.txt", gDumpFileNameCnt++);
    // Dump all the frames and view to a a file
    FILE* fd = fopen(filename, "w");
    if (fd) {
      nsIFrame* theRootFrame = aPO->mPresShell->GetRootFrame();
      fprintf(fd, "Title: %s\n", docStr.get());
      fprintf(fd, "URL:   %s\n", urlStr.get());
      fprintf(fd, "--------------- Frames ----------------\n");
      // RefPtr<gfxContext> renderingContext =
      //  printData->mPrintDocDC->CreateRenderingContext();
      RootFrameList(aPO->mPresContext, fd, 0);
      // DumpFrames(fd, aPO->mPresContext, renderingContext, theRootFrame, 0);
      fprintf(fd, "---------------------------------------\n\n");
      fprintf(fd, "--------------- Views From Root Frame----------------\n");
      nsView* v = theRootFrame->GetView();
      if (v) {
        v->List(fd);
      } else {
        printf("View is null!\n");
      }
      if (aPO->mDocShell) {
        fprintf(fd, "--------------- All Views ----------------\n");
        DumpViews(aPO->mDocShell, fd);
        fprintf(fd, "---------------------------------------\n\n");
      }
      fclose(fd);
    }
  }
#endif

  return NS_OK;
}

//-------------------------------------------------------
// Figure out how many documents and how many total pages we are printing
void nsPrintJob::CalcNumPrintablePages(int32_t& aNumPages) {
  aNumPages = 0;
  // Count the number of printable documents and printable pages
  for (nsPrintObject* po : mPrintDocList) {
    // Note: The po->mPresContext null-check below is necessary, because it's
    // possible po->mPresContext might never have been set.  (e.g., if
    // PrintingIsEnabled() returns false, ReflowPrintObject bails before setting
    // mPresContext)
    if (po->mPresContext && po->mPresContext->IsRootPaginatedDocument()) {
      nsPageSequenceFrame* seqFrame = po->mPresShell->GetPageSequenceFrame();
      if (seqFrame) {
        aNumPages += seqFrame->PrincipalChildList().GetLength();
      }
    }
  }
}

//-----------------------------------------------------------------
//-- Done: Reflow Methods
//-----------------------------------------------------------------

//-----------------------------------------------------------------
//-- Section: Printing Methods
//-----------------------------------------------------------------

//-------------------------------------------------------
// Called for each DocShell that needs to be printed
bool nsPrintJob::PrintDocContent(const UniquePtr<nsPrintObject>& aPO,
                                 nsresult& aStatus) {
  NS_ASSERTION(aPO, "Pointer is null!");
  aStatus = NS_OK;

  if (!aPO->mHasBeenPrinted && aPO->PrintingIsEnabled()) {
    aStatus = DoPrint(aPO);
    return true;
  }

  // If |aPO->mHasBeenPrinted| is true,
  // the kids frames are already processed in |PrintPage|.
  // XXX This should be removed. Since bug 1552785 it has no longer been
  // possible for us to have to print multiple subdocuments consecutively.
  if (!aPO->mHasBeenPrinted && !aPO->mInvisible) {
    for (const UniquePtr<nsPrintObject>& po : aPO->mKids) {
      bool printed = PrintDocContent(po, aStatus);
      if (printed || NS_FAILED(aStatus)) {
        return true;
      }
    }
  }
  return false;
}

// A helper struct to aid with DeleteNonSelectedNodes.
struct MOZ_STACK_CLASS SelectionRangeState {
  explicit SelectionRangeState(RefPtr<Selection> aSelection)
      : mSelection(std::move(aSelection)) {
    MOZ_ASSERT(mSelection);
    MOZ_ASSERT(!mSelection->RangeCount());
  }

  // Selects all the nodes that are _not_ included in a given set of ranges.
  MOZ_CAN_RUN_SCRIPT void SelectComplementOf(Span<const RefPtr<nsRange>>);
  // Removes the selected ranges from the document.
  MOZ_CAN_RUN_SCRIPT void RemoveSelectionFromDocument();

 private:
  struct Position {
    nsINode* mNode;
    uint32_t mOffset;
  };

  MOZ_CAN_RUN_SCRIPT void SelectRange(nsRange*);
  MOZ_CAN_RUN_SCRIPT void SelectNodesExcept(const Position& aStart,
                                            const Position& aEnd);
  MOZ_CAN_RUN_SCRIPT void SelectNodesExceptInSubtree(const Position& aStart,
                                                     const Position& aEnd);

  // A map from subtree root (document or shadow root) to the start position of
  // the non-selected content (so far).
  nsTHashMap<nsPtrHashKey<nsINode>, Position> mPositions;

  // The selection we're adding the ranges to.
  const RefPtr<Selection> mSelection;
};

void SelectionRangeState::SelectComplementOf(
    Span<const RefPtr<nsRange>> aRanges) {
  for (const auto& range : aRanges) {
    auto start = Position{range->GetMayCrossShadowBoundaryStartContainer(),
                          range->MayCrossShadowBoundaryStartOffset()};
    auto end = Position{range->GetMayCrossShadowBoundaryEndContainer(),
                        range->MayCrossShadowBoundaryEndOffset()};
    SelectNodesExcept(start, end);
  }
}

void SelectionRangeState::SelectRange(nsRange* aRange) {
  if (aRange && !aRange->AreNormalRangeAndCrossShadowBoundaryRangeCollapsed()) {
    mSelection->AddRangeAndSelectFramesAndNotifyListeners(*aRange,
                                                          IgnoreErrors());
  }
}

void SelectionRangeState::SelectNodesExcept(const Position& aStart,
                                            const Position& aEnd) {
  SelectNodesExceptInSubtree(aStart, aEnd);
  if (!StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()) {
    if (auto* shadow = ShadowRoot::FromNode(aStart.mNode->SubtreeRoot())) {
      auto* host = shadow->Host();
      // Can't just select other nodes except the host, because other nodes that
      // are not in this particular shadow tree could also be selected
      SelectNodesExcept(Position{host, 0},
                        Position{host, host->GetChildCount()});
    } else {
      MOZ_ASSERT(aStart.mNode->IsInUncomposedDoc());
    }
  }
}

void SelectionRangeState::SelectNodesExceptInSubtree(const Position& aStart,
                                                     const Position& aEnd) {
  static constexpr auto kEllipsis = u"\x2026"_ns;

  // Finish https://bugzilla.mozilla.org/show_bug.cgi?id=1903871 once the pref
  // is shipped, so that we only need one position.
  nsINode* root = StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()
                      ? aStart.mNode->OwnerDoc()
                      : aStart.mNode->SubtreeRoot();
  auto& start =
      mPositions.WithEntryHandle(root, [&](auto&& entry) -> Position& {
        return entry.OrInsertWith([&] { return Position{root, 0}; });
      });

  bool ellipsizedStart = false;
  if (auto* text = Text::FromNode(aStart.mNode)) {
    if (start.mNode != text && aStart.mOffset &&
        aStart.mOffset < text->Length()) {
      text->InsertData(aStart.mOffset, kEllipsis, IgnoreErrors());
      ellipsizedStart = true;
    }
  }

  RefPtr<nsRange> range = nsRange::Create(
      start.mNode, start.mOffset, aStart.mNode, aStart.mOffset, IgnoreErrors());
  SelectRange(range);

  start = aEnd;

  // If we added an ellipsis at the start and the end position was relative to
  // the same node account for it here.
  if (ellipsizedStart && aStart.mNode == aEnd.mNode) {
    start.mOffset += kEllipsis.Length();
  }

  // If the end is mid text then add an ellipsis.
  if (auto* text = Text::FromNode(start.mNode)) {
    if (start.mOffset && start.mOffset < text->Length()) {
      text->InsertData(start.mOffset, kEllipsis, IgnoreErrors());
      start.mOffset += kEllipsis.Length();
    }
  }
}

void SelectionRangeState::RemoveSelectionFromDocument() {
  for (auto& entry : mPositions) {
    const Position& pos = entry.GetData();
    nsINode* root = entry.GetKey();
    RefPtr<nsRange> range = nsRange::Create(
        pos.mNode, pos.mOffset, root, root->GetChildCount(), IgnoreErrors());
    SelectRange(range);
  }
  mSelection->DeleteFromDocument(IgnoreErrors());
}

/**
 * Builds the complement set of ranges and adds those to the selection.
 * Deletes all of the nodes contained in the complement set of ranges
 * leaving behind only nodes that were originally selected.
 * Adds ellipses to a selected node's text if text is truncated by a range.
 * This is used to implement the "Print Selection Only" user interface option.
 */
MOZ_CAN_RUN_SCRIPT_BOUNDARY static nsresult DeleteNonSelectedNodes(
    Document& aDoc) {
  MOZ_ASSERT(aDoc.IsStaticDocument());
  const auto* printRanges = static_cast<nsTArray<RefPtr<nsRange>>*>(
      aDoc.GetProperty(nsGkAtoms::printselectionranges));
  if (!printRanges) {
    return NS_OK;
  }

  PresShell* presShell = aDoc.GetPresShell();
  NS_ENSURE_STATE(presShell);
  RefPtr<Selection> selection =
      presShell->GetCurrentSelection(SelectionType::eNormal);
  NS_ENSURE_STATE(selection);

  SelectionRangeState state(std::move(selection));
  state.SelectComplementOf(*printRanges);
  state.RemoveSelectionFromDocument();
  return NS_OK;
}

//-------------------------------------------------------
nsresult nsPrintJob::DoPrint(const UniquePtr<nsPrintObject>& aPO) {
  PR_PL(("\n"));
  PR_PL(("**************************** %s ****************************\n",
         LoggableTypeOfPO(aPO.get())));
  PR_PL(("****** In DV::DoPrint   PO: %p \n", aPO.get()));

  PresShell* poPresShell = aPO->mPresShell;
  nsPresContext* poPresContext = aPO->mPresContext;

  NS_ASSERTION(poPresContext, "PrintObject has not been reflowed");
  NS_ASSERTION(poPresContext->Type() != nsPresContext::eContext_PrintPreview,
               "How did this context end up here?");

  // Guarantee that mPrt and the objects it owns won't be deleted in this method
  // because it might be cleared if other modules called from here may fire
  // events, notifying observers and/or listeners.
  RefPtr<nsPrintData> printData = mPrt;
  if (NS_WARN_IF(!printData)) {
    return NS_ERROR_FAILURE;
  }

  {
    // Ask the page sequence frame to print all the pages
    nsPageSequenceFrame* seqFrame = poPresShell->GetPageSequenceFrame();
    MOZ_ASSERT(seqFrame, "no page sequence frame");

    // We are done preparing for printing, so we can turn this off
    mPreparingForPrint = false;

#ifdef EXTENDED_DEBUG_PRINTING
    nsIFrame* rootFrame = poPresShell->GetRootFrame();
    if (aPO->PrintingIsEnabled()) {
      nsAutoCString docStr;
      nsAutoCString urlStr;
      GetDocTitleAndURL(aPO, docStr, urlStr);
      DumpLayoutData(docStr.get(), urlStr.get(), poPresContext,
                     printData->mPrintDC, rootFrame, aPO->mDocShell, nullptr);
    }
#endif

    if (!mPrintSettings) {
      // not sure what to do here!
      SetIsPrinting(false);
      return NS_ERROR_FAILURE;
    }

    nsAutoString docTitleStr;
    nsAutoString docURLStr;
    GetDisplayTitleAndURL(*aPO->mDocument, mPrintSettings,
                          DocTitleDefault::eFallback, docTitleStr, docURLStr);

    if (!seqFrame) {
      SetIsPrinting(false);
      return NS_ERROR_FAILURE;
    }

    mPageSeqFrame = seqFrame;
    seqFrame->StartPrint(poPresContext, mPrintSettings, docTitleStr, docURLStr);

    // Schedule Page to Print
    PR_PL(("Scheduling Print of PO: %p (%s) \n", aPO.get(),
           LoggableTypeOfPO(aPO.get())));
    StartPagePrintTimer(aPO);
  }

  return NS_OK;
}

//-------------------------------------------------------
bool nsPrintJob::PrePrintSheet() {
  NS_ASSERTION(mPageSeqFrame.IsAlive(), "mPageSeqFrame is not alive!");
  NS_ASSERTION(mPrt, "mPrt is null!");

  // Although these should NEVER be nullptr
  // This is added insurance, to make sure we don't crash in optimized builds
  if (!mPrt || !mPageSeqFrame.IsAlive()) {
    return true;  // means we are done preparing the sheet.
  }

  // Guarantee that mPrt won't be deleted during a call of
  // FirePrintingErrorEvent().
  RefPtr<nsPrintData> printData = mPrt;

  // Ask mPageSeqFrame if the sheet is ready to be printed.
  // If the sheet doesn't get printed at all, the |done| will be |true|.
  bool done = false;
  nsPageSequenceFrame* pageSeqFrame = do_QueryFrame(mPageSeqFrame.GetFrame());
  nsresult rv = pageSeqFrame->PrePrintNextSheet(mPagePrintTimer, &done);
  if (NS_FAILED(rv)) {
    // ??? ::PrintSheet doesn't set |printData->mIsAborted = true| if
    // rv != NS_ERROR_ABORT, but I don't really understand why this should be
    // the right thing to do?  Shouldn't |printData->mIsAborted| set to true
    // all the time if something went wrong?
    if (rv != NS_ERROR_ABORT) {
      FirePrintingErrorEvent(rv);
      printData->mIsAborted = true;
    }
    done = true;
  }
  return done;
}

bool nsPrintJob::PrintSheet(nsPrintObject* aPO) {
  NS_ASSERTION(aPO, "aPO is null!");
  NS_ASSERTION(mPageSeqFrame.IsAlive(), "mPageSeqFrame is not alive!");
  NS_ASSERTION(mPrt, "mPrt is null!");

  // Although these should NEVER be nullptr
  // This is added insurance, to make sure we don't crash in optimized builds
  if (!mPrt || !aPO || !mPageSeqFrame.IsAlive()) {
    FirePrintingErrorEvent(NS_ERROR_FAILURE);
    return true;  // means we are done printing
  }

  // Guarantee that mPrt won't be deleted during a call of
  // nsPrintData::DoOnProgressChange() which runs some listeners,
  // which may clear (& might otherwise destroy).
  RefPtr<nsPrintData> printData = mPrt;

  PR_PL(("-----------------------------------\n"));
  PR_PL(("------ In DV::PrintSheet PO: %p (%s)\n", aPO, LoggableTypeOfPO(aPO)));

  if (printData->mIsAborted) {
    return true;
  }

  nsPageSequenceFrame* pageSeqFrame = do_QueryFrame(mPageSeqFrame.GetFrame());
  const uint32_t sheetIdx = pageSeqFrame->GetCurrentSheetIdx();
  const uint32_t numSheets = pageSeqFrame->PrincipalChildList().GetLength();

  PR_PL(("****** Printing sheet index %d of %d sheets(s)\n", sheetIdx,
         numSheets));

  MOZ_ASSERT(numSheets > 0, "print operations must have at least 1 sheet");
  MOZ_ASSERT(sheetIdx < numSheets,
             "sheetIdx shouldn't be allowed to go out of bounds");
  printData->DoOnProgressChange(sheetIdx, numSheets, false, 0);
  if (NS_WARN_IF(mPrt != printData)) {
    // If current printing is canceled or new print is started, let's return
    // true to notify the caller of current printing is done.
    return true;
  }

  // Print the sheet
  // if a print job was cancelled externally, an EndPage or BeginPage may
  // fail and the failure is passed back here.
  // Returning true means we are done printing.
  //
  // When rv == NS_ERROR_ABORT, it means we want out of the
  // print job without displaying any error messages
  nsresult rv = pageSeqFrame->PrintNextSheet();
  if (NS_FAILED(rv)) {
    if (rv != NS_ERROR_ABORT) {
      FirePrintingErrorEvent(rv);
      printData->mIsAborted = true;
    }
    return true;
  }

  pageSeqFrame->DoPageEnd();

  // If we just printed the final sheet (the one with index "numSheets-1"),
  // then we're done!
  return (sheetIdx == numSheets - 1);
}

void nsPrintJob::PageDone(nsresult aResult) {
  MOZ_ASSERT(mIsDoingPrinting);

  // mPagePrintTimer might be released during RemotePrintFinished, keep a
  // reference here to make sure it lives long enough.
  RefPtr<nsPagePrintTimer> timer = mPagePrintTimer;
  timer->RemotePrintFinished();
}

//-----------------------------------------------------------------
//-- Done: Printing Methods
//-----------------------------------------------------------------

//-----------------------------------------------------------------
//-- Section: Misc Support Methods
//-----------------------------------------------------------------

//---------------------------------------------------------------------
void nsPrintJob::SetIsPrinting(bool aIsPrinting) {
  mIsDoingPrinting = aIsPrinting;
  if (aIsPrinting) {
    mPreparingForPrint = true;
  }
}

//---------------------------------------------------------------------
void nsPrintJob::SetIsPrintPreview(bool aIsPrintPreview) {
  mCreatedForPrintPreview = aIsPrintPreview;

  if (mDocViewerPrint) {
    mDocViewerPrint->SetIsPrintPreview(aIsPrintPreview);
  }
}

//-------------------------------------------------------
bool nsPrintJob::DonePrintingSheets(nsPrintObject* aPO, nsresult aResult) {
  // NS_ASSERTION(aPO, "Pointer is null!");
  PR_PL(("****** In DV::DonePrintingSheets PO: %p (%s)\n", aPO,
         aPO ? LoggableTypeOfPO(aPO) : ""));

  // If there is a pageSeqFrame, make sure there are no more printCanvas active
  // that might call |Notify| on the pagePrintTimer after things are cleaned up
  // and printing was marked as being done.
  if (mPageSeqFrame.IsAlive()) {
    nsPageSequenceFrame* pageSeqFrame = do_QueryFrame(mPageSeqFrame.GetFrame());
    pageSeqFrame->ResetPrintCanvasList();
  }

  // Guarantee that mPrt and mPrintObject won't be deleted during a
  // call of PrintDocContent() and FirePrintCompletionEvent().
  RefPtr<nsPrintData> printData = mPrt;

  if (aPO && !printData->mIsAborted) {
    aPO->mHasBeenPrinted = true;
    nsresult rv;
    bool didPrint = PrintDocContent(mPrintObject, rv);
    if (NS_SUCCEEDED(rv) && didPrint) {
      PR_PL(
          ("****** In DV::DonePrintingSheets PO: %p (%s) didPrint:%s (Not Done "
           "Printing)\n",
           aPO, LoggableTypeOfPO(aPO), PRT_YESNO(didPrint)));
      return false;
    }
  }

  if (NS_SUCCEEDED(aResult)) {
    FirePrintCompletionEvent();
    // XXX mPrt may be cleared or replaced with new instance here.
    //     However, the following methods will clean up with new mPrt or will
    //     do nothing due to no proper nsPrintData instance.
  }

  SetIsPrinting(false);

  // Release reference to mPagePrintTimer; the timer object destroys itself
  // after this returns true
  DisconnectPagePrintTimer();

  return true;
}

//-------------------------------------------------------
nsresult nsPrintJob::EnablePOsForPrinting() {
  // Guarantee that mPrt and the objects it owns won't be deleted.
  RefPtr<nsPrintData> printData = mPrt;

  // NOTE: All POs have been "turned off" for printing
  // this is where we decided which POs get printed.

  if (!printData || !mPrintSettings) {
    return NS_ERROR_FAILURE;
  }

  PR_PL(("\n"));
  PR_PL(("********* nsPrintJob::EnablePOsForPrinting *********\n"));

  if (!mPrintSettings->GetPrintSelectionOnly()) {
    mPrintObject->EnablePrinting(true);
    return NS_OK;
  }

  // This means we are either printing a selected iframe or
  // we are printing the current selection.
  NS_ENSURE_STATE(!mDisallowSelectionPrint && mSelectionRoot);

  // If mSelectionRoot is a selected iframe without a selection, then just
  // enable normally from that point.
  if (mSelectionRoot->mParent && !mSelectionRoot->HasSelection()) {
    mSelectionRoot->EnablePrinting(true);
  } else {
    // Otherwise, only enable nsPrintObjects that have a selection.
    mSelectionRoot->EnablePrintingSelectionOnly();
  }
  return NS_OK;
}

//-----------------------------------------------------------------
//-- Done: Misc Support Methods
//-----------------------------------------------------------------

//-----------------------------------------------------------------
//-- Section: Finishing up or Cleaning up
//-----------------------------------------------------------------

//-----------------------------------------------------------------
nsresult nsPrintJob::FinishPrintPreview() {
  nsresult rv = NS_OK;

#ifdef NS_PRINT_PREVIEW

  // If mPrt is null we've already finished with print preview. If mPrt is not
  // null but mIsCreatingPrintPreview is false FinishPrintPreview must have
  // already failed due to DocumentReadyForPrinting failing.
  if (!mPrt || !mIsCreatingPrintPreview) {
    return rv;
  }

  rv = DocumentReadyForPrinting();

  // Note that this method may be called while the instance is being
  // initialized.  Some methods which initialize the instance (e.g.,
  // DoCommonPrint) may need to stop initializing and return error if
  // this is called.  Therefore it's important to set mIsCreatingPrintPreview
  // state to false here.  If you need to stop setting that here, you need to
  // keep them being able to check whether the owner stopped using this
  // instance.
  mIsCreatingPrintPreview = false;

  // mPrt may be cleared during a call of nsPrintData::OnEndPrinting()
  // because that method invokes some arbitrary listeners.
  // TODO(dshin): Does any listener attach to print preview? Doesn't seem like
  // we call matching `OnStartPrinting()` for previews.
  RefPtr<nsPrintData> printData = mPrt;
  if (NS_FAILED(rv)) {
    printData->OnEndPrinting();

    return rv;
  }

  if (mPrintPreviewCallback) {
    const bool hasSelection = !mDisallowSelectionPrint && mSelectionRoot;

    Maybe<float> pageWidth;
    Maybe<float> pageHeight;
    if (mMaybeCSSPageSize) {
      nsSize cssPageSize = *mMaybeCSSPageSize;
      pageWidth = Some(float(cssPageSize.width) / float(AppUnitsPerCSSInch()));
      pageHeight =
          Some(float(cssPageSize.height) / float(AppUnitsPerCSSInch()));
    }

    mPrintPreviewCallback(PrintPreviewResultInfo(
        GetPrintPreviewNumSheets(), GetRawNumPages(), GetIsEmpty(),
        hasSelection, hasSelection && mPrintObject->HasSelection(),
        mMaybeCSSPageLandscape, pageWidth, pageHeight));
    mPrintPreviewCallback = nullptr;
  }

  // At this point we are done preparing everything
  // before it is to be created

  printData->OnEndPrinting();

#endif  // NS_PRINT_PREVIEW

  return NS_OK;
}

//-----------------------------------------------------------------
//-- Done: Finishing up or Cleaning up
//-----------------------------------------------------------------

/*=============== Timer Related Code ======================*/
nsresult nsPrintJob::StartPagePrintTimer(const UniquePtr<nsPrintObject>& aPO) {
  if (!mPagePrintTimer) {
    // Get the delay time in between the printing of each page
    // this gives the user more time to press cancel
    int32_t printPageDelay = mPrintSettings->GetPrintPageDelay();

    nsCOMPtr<nsIDocumentViewer> viewer = do_QueryInterface(mDocViewerPrint);
    NS_ENSURE_TRUE(viewer, NS_ERROR_FAILURE);
    nsCOMPtr<Document> doc = viewer->GetDocument();
    NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

    mPagePrintTimer =
        new nsPagePrintTimer(this, mDocViewerPrint, doc, printPageDelay);

    if (mRemotePrintJob) {
      mRemotePrintJob->SetPagePrintTimer(mPagePrintTimer);
      mRemotePrintJob->SetPrintJob(this);
    }
  }

  return mPagePrintTimer->Start(aPO.get());
}

//---------------------------------------------------------------
//-- PLEvent Notification
//---------------------------------------------------------------
class nsPrintCompletionEvent : public Runnable {
 public:
  explicit nsPrintCompletionEvent(nsIDocumentViewerPrint* docViewerPrint)
      : mozilla::Runnable("nsPrintCompletionEvent"),
        mDocViewerPrint(docViewerPrint) {
    NS_ASSERTION(mDocViewerPrint, "mDocViewerPrint is null.");
  }

  NS_IMETHOD Run() override {
    if (mDocViewerPrint) {
      mDocViewerPrint->OnDonePrinting();
    }
    return NS_OK;
  }

 private:
  nsCOMPtr<nsIDocumentViewerPrint> mDocViewerPrint;
};

//-----------------------------------------------------------
void nsPrintJob::FirePrintCompletionEvent() {
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIRunnable> event = new nsPrintCompletionEvent(mDocViewerPrint);
  nsCOMPtr<nsIDocumentViewer> viewer = do_QueryInterface(mDocViewerPrint);
  NS_ENSURE_TRUE_VOID(viewer);
  nsCOMPtr<Document> doc = viewer->GetDocument();
  NS_ENSURE_TRUE_VOID(doc);
  NS_ENSURE_SUCCESS_VOID(doc->Dispatch(event.forget()));
}

void nsPrintJob::DisconnectPagePrintTimer() {
  if (mPagePrintTimer) {
    mPagePrintTimer->Disconnect();
    mPagePrintTimer = nullptr;
  }
}

//---------------------------------------------------------------
//---------------------------------------------------------------
//-- Debug helper routines
//---------------------------------------------------------------
//---------------------------------------------------------------
#if defined(XP_WIN) && defined(EXTENDED_DEBUG_PRINTING)
#  include <windows.h>
#  include <process.h>
#  include <direct.h>

#  define MY_FINDFIRST(a, b) FindFirstFile(a, b)
#  define MY_FINDNEXT(a, b) FindNextFile(a, b)
#  define ISDIR(a) (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
#  define MY_FINDCLOSE(a) FindClose(a)
#  define MY_FILENAME(a) a.cFileName
#  define MY_FILESIZE(a) (a.nFileSizeHigh * MAXDWORD) + a.nFileSizeLow

int RemoveFilesInDir(const char* aDir) {
  WIN32_FIND_DATA data_ptr;
  HANDLE find_handle;

  char path[MAX_PATH];

  strcpy(path, aDir);

  // Append slash to the end of the directory names if not there
  if (path[strlen(path) - 1] != '\\') strcat(path, "\\");

  char findPath[MAX_PATH];
  strcpy(findPath, path);
  strcat(findPath, "*.*");

  find_handle = MY_FINDFIRST(findPath, &data_ptr);

  if (find_handle != INVALID_HANDLE_VALUE) {
    do {
      if (ISDIR(data_ptr) && (stricmp(MY_FILENAME(data_ptr), ".")) &&
          (stricmp(MY_FILENAME(data_ptr), ".."))) {
        // skip
      } else if (!ISDIR(data_ptr)) {
        if (!strncmp(MY_FILENAME(data_ptr), "print_dump", 10)) {
          char fileName[MAX_PATH];
          strcpy(fileName, aDir);
          strcat(fileName, "\\");
          strcat(fileName, MY_FILENAME(data_ptr));
          printf("Removing %s\n", fileName);
          remove(fileName);
        }
      }
    } while (MY_FINDNEXT(find_handle, &data_ptr));
    MY_FINDCLOSE(find_handle);
  }
  return TRUE;
}
#endif

#ifdef EXTENDED_DEBUG_PRINTING

/** ---------------------------------------------------
 *  Dumps Frames for Printing
 */
static void RootFrameList(nsPresContext* aPresContext, FILE* out,
                          const char* aPrefix) {
  if (!aPresContext || !out) return;

  if (PresShell* presShell = aPresContext->GetPresShell()) {
    nsIFrame* frame = presShell->GetRootFrame();
    if (frame) {
      frame->List(out, aPrefix);
    }
  }
}

/** ---------------------------------------------------
 *  Dumps Frames for Printing
 */
static void DumpFrames(FILE* out, nsPresContext* aPresContext,
                       gfxContext* aRendContext, nsIFrame* aFrame,
                       int32_t aLevel) {
  NS_ASSERTION(out, "Pointer is null!");
  NS_ASSERTION(aPresContext, "Pointer is null!");
  NS_ASSERTION(aRendContext, "Pointer is null!");
  NS_ASSERTION(aFrame, "Pointer is null!");

  nsIFrame* child = aFrame->PrincipalChildList().FirstChild();
  while (child != nullptr) {
    for (int32_t i = 0; i < aLevel; i++) {
      fprintf(out, "  ");
    }
    nsAutoString tmp;
    child->GetFrameName(tmp);
    fputs(NS_LossyConvertUTF16toASCII(tmp).get(), out);
    bool isSelected;
    if (child->IsVisibleForPainting()) {
      fprintf(out, " %p %s", child, isSelected ? "VIS" : "UVS");
      nsRect rect = child->GetRect();
      fprintf(out, "[%d,%d,%d,%d] ", rect.x, rect.y, rect.width, rect.height);
      fprintf(out, "v: %p ", (void*)child->GetView());
      fprintf(out, "\n");
      DumpFrames(out, aPresContext, aRendContext, child, aLevel + 1);
      child = child->GetNextSibling();
    }
  }
}

/** ---------------------------------------------------
 *  Dumps the Views from the DocShell
 */
static void DumpViews(nsIDocShell* aDocShell, FILE* out) {
  NS_ASSERTION(aDocShell, "Pointer is null!");
  NS_ASSERTION(out, "Pointer is null!");

  if (nullptr != aDocShell) {
    fprintf(out, "docshell=%p \n", aDocShell);
    if (PresShell* presShell = aDocShell->GetPresShell()) {
      nsViewManager* vm = presShell->GetViewManager();
      if (vm) {
        nsView* root = vm->GetRootView();
        if (root) {
          root->List(out);
        }
      }
    } else {
      fputs("null pres shell\n", out);
    }

    // dump the views of the sub documents
    int32_t i, n;
    BrowsingContext* bc = nsDocShell::Cast(aDocShell)->GetBrowsingContext();
    for (auto& child : bc->Children()) {
      if (auto childDS = child->GetDocShell()) {
        DumpViews(childAsShell, out);
      }
    }
  }
}

/** ---------------------------------------------------
 *  Dumps the Views and Frames
 */
void DumpLayoutData(const char* aTitleStr, const char* aURLStr,
                    nsPresContext* aPresContext, nsDeviceContext* aDC,
                    nsIFrame* aRootFrame, nsIDocShell* aDocShell,
                    FILE* aFD = nullptr) {
  if (!MOZ_LOG_TEST(gPrintingLog, DUMP_LAYOUT_LEVEL)) {
    return;
  }

  if (aPresContext == nullptr || aDC == nullptr) {
    return;
  }

#  ifdef NS_PRINT_PREVIEW
  if (aPresContext->Type() == nsPresContext::eContext_PrintPreview) {
    return;
  }
#  endif

  NS_ASSERTION(aRootFrame, "Pointer is null!");
  NS_ASSERTION(aDocShell, "Pointer is null!");

  // Dump all the frames and view to a a file
  char filename[256];
  sprintf(filename, "print_dump_layout_%d.txt", gDumpLOFileNameCnt++);
  FILE* fd = aFD ? aFD : fopen(filename, "w");
  if (fd) {
    fprintf(fd, "Title: %s\n", aTitleStr ? aTitleStr : "");
    fprintf(fd, "URL:   %s\n", aURLStr ? aURLStr : "");
    fprintf(fd, "--------------- Frames ----------------\n");
    fprintf(fd, "--------------- Frames ----------------\n");
    // RefPtr<gfxContext> renderingContext =
    //  aDC->CreateRenderingContext();
    RootFrameList(aPresContext, fd, "");
    // DumpFrames(fd, aPresContext, renderingContext, aRootFrame, 0);
    fprintf(fd, "---------------------------------------\n\n");
    fprintf(fd, "--------------- Views From Root Frame----------------\n");
    nsView* v = aRootFrame->GetView();
    if (v) {
      v->List(fd);
    } else {
      printf("View is null!\n");
    }
    if (aDocShell) {
      fprintf(fd, "--------------- All Views ----------------\n");
      DumpViews(aDocShell, fd);
      fprintf(fd, "---------------------------------------\n\n");
    }
    if (aFD == nullptr) {
      fclose(fd);
    }
  }
}

//-------------------------------------------------------------
static void DumpPrintObjectsList(const nsTArray<nsPrintObject*>& aDocList) {
  if (!MOZ_LOG_TEST(gPrintingLog, DUMP_LAYOUT_LEVEL)) {
    return;
  }

  PR_PL(("Doc List\n***************************************************\n"));
  PR_PL(
      ("T  P A H    PO    DocShell   Seq     Page      Root     Page#    "
       "Rect\n"));
  for (nsPrintObject* po : aDocList) {
    NS_ASSERTION(po, "nsPrintObject can't be null!");
    nsIFrame* rootFrame = nullptr;
    if (po->mPresShell) {
      rootFrame = po->mPresShell->GetRootFrame();
      while (rootFrame != nullptr) {
        nsPageSequenceFrame* sqf = do_QueryFrame(rootFrame);
        if (sqf) {
          break;
        }
        rootFrame = rootFrame->PrincipalChildList().FirstChild();
      }
    }

    PR_PL(("%s %d %d %p %p %p\n", ShortLoggableTypeOfPO(po),
           po->PrintingIsEnabled(), po->mHasBeenPrinted, po,
           po->mDocShell.get(), rootFrame));
  }
}

//-------------------------------------------------------------
static void DumpPrintObjectsTree(nsPrintObject* aPO, int aLevel, FILE* aFD) {
  if (!MOZ_LOG_TEST(gPrintingLog, DUMP_LAYOUT_LEVEL)) {
    return;
  }

  NS_ASSERTION(aPO, "Pointer is null!");

  FILE* fd = aFD ? aFD : stdout;
  if (aLevel == 0) {
    fprintf(fd,
            "DocTree\n***************************************************\n");
    fprintf(fd, "T     PO    DocShell   Seq      Page     Page#    Rect\n");
  }
  for (const auto& po : aPO->mKids) {
    NS_ASSERTION(po, "nsPrintObject can't be null!");
    for (int32_t k = 0; k < aLevel; k++) fprintf(fd, "  ");
    fprintf(fd, "%s %p %p\n", ShortLoggableTypeOfPO(po.get()), po.get(),
            po->mDocShell.get());
  }
}

//-------------------------------------------------------------
static void GetDocTitleAndURL(const UniquePtr<nsPrintObject>& aPO,
                              nsACString& aDocStr, nsACString& aURLStr) {
  nsAutoString docTitleStr;
  nsAutoString docURLStr;
  GetDocumentTitleAndURL(aPO->mDocument, docTitleStr, docURLStr);
  CopyUTF16toUTF8(docTitleStr, aDocStr);
  CopyUTF16toUTF8(docURLStr, aURLStr);
}

//-------------------------------------------------------------
static void DumpPrintObjectsTreeLayout(const UniquePtr<nsPrintObject>& aPO,
                                       nsDeviceContext* aDC, int aLevel,
                                       FILE* aFD) {
  if (!MOZ_LOG_TEST(gPrintingLog, DUMP_LAYOUT_LEVEL)) {
    return;
  }

  NS_ASSERTION(aPO, "Pointer is null!");
  NS_ASSERTION(aDC, "Pointer is null!");

  FILE* fd = nullptr;
  if (aLevel == 0) {
    fd = fopen("tree_layout.txt", "w");
    fprintf(fd,
            "DocTree\n***************************************************\n");
    fprintf(fd, "***************************************************\n");
    fprintf(fd, "T     PO    DocShell   Seq      Page     Page#    Rect\n");
  } else {
    fd = aFD;
  }
  if (fd) {
    nsIFrame* rootFrame = nullptr;
    if (aPO->mPresShell) {
      rootFrame = aPO->mPresShell->GetRootFrame();
    }
    for (int32_t k = 0; k < aLevel; k++) fprintf(fd, "  ");
    fprintf(fd, "%s %p %p\n", ShortLoggableTypeOfPO(aPO.get()), aPO.get(),
            aPO->mDocShell.get());
    if (aPO->PrintingIsEnabled()) {
      nsAutoCString docStr;
      nsAutoCString urlStr;
      GetDocTitleAndURL(aPO, docStr, urlStr);
      DumpLayoutData(docStr.get(), urlStr.get(), aPO->mPresContext, aDC,
                     rootFrame, aPO->mDocShell, fd);
    }
    fprintf(fd, "<***************************************************>\n");

    for (const auto& po : aPO->mKids) {
      NS_ASSERTION(po, "nsPrintObject can't be null!");
      DumpPrintObjectsTreeLayout(po, aDC, aLevel + 1, fd);
    }
  }
  if (aLevel == 0 && fd) {
    fclose(fd);
  }
}

//-------------------------------------------------------------
static void DumpPrintObjectsListStart(
    const char* aStr, const nsTArray<nsPrintObject*>& aDocList) {
  if (!MOZ_LOG_TEST(gPrintingLog, DUMP_LAYOUT_LEVEL)) {
    return;
  }

  NS_ASSERTION(aStr, "Pointer is null!");

  PR_PL(("%s\n", aStr));
  DumpPrintObjectsList(aDocList);
}

#endif

//---------------------------------------------------------------
//---------------------------------------------------------------
//-- End of debug helper routines
//---------------------------------------------------------------

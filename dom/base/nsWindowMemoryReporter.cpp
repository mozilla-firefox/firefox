/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindowMemoryReporter.h"

#include "XPCJSMemoryReporter.h"
#include "js/MemoryMetrics.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/Try.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "nsGlobalWindowInner.h"
#include "nsGlobalWindowOuter.h"
#include "nsNetCID.h"
#include "nsPrintfCString.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"
#include "nsStyleStructList.h"
#include "nsWindowSizes.h"
#include "nsXULPrototypeCache.h"

using namespace mozilla;
using namespace mozilla::dom;

StaticRefPtr<nsWindowMemoryReporter> sWindowReporter;

/**
 * Don't trigger a ghost window check when a DOM window is detached if we've
 * run it this recently.
 */
const int32_t kTimeBetweenChecks = 45; /* seconds */

nsWindowMemoryReporter::nsWindowMemoryReporter()
    : mLastCheckForGhostWindows(TimeStamp::NowLoRes()),
      mCycleCollectorIsRunning(false),
      mCheckTimerWaitingForCCEnd(false),
      mGhostWindowCount(0) {}

nsWindowMemoryReporter::~nsWindowMemoryReporter() { KillCheckTimer(); }

NS_IMPL_ISUPPORTS(nsWindowMemoryReporter, nsIMemoryReporter, nsIObserver,
                  nsISupportsWeakReference)

static nsresult AddNonJSSizeOfWindowAndItsDescendents(
    nsGlobalWindowOuter* aWindow, nsTabSizes* aSizes) {
  // Measure the window.
  SizeOfState state(moz_malloc_size_of);
  nsWindowSizes windowSizes(state);
  aWindow->AddSizeOfIncludingThis(windowSizes);

  // Measure the inner window, if there is one.
  nsPIDOMWindowInner* inner = aWindow->GetCurrentInnerWindow();
  if (inner) {
    nsGlobalWindowInner::Cast(inner)->AddSizeOfIncludingThis(windowSizes);
  }

  windowSizes.addToTabSizes(aSizes);

  BrowsingContext* bc = aWindow->GetBrowsingContext();
  if (!bc) {
    return NS_OK;
  }

  // Measure this window's descendents.
  for (const auto& frame : bc->Children()) {
    if (auto* childWin = nsGlobalWindowOuter::Cast(frame->GetDOMWindow())) {
      MOZ_TRY(AddNonJSSizeOfWindowAndItsDescendents(childWin, aSizes));
    }
  }
  return NS_OK;
}

static nsresult NonJSSizeOfTab(nsPIDOMWindowOuter* aWindow, size_t* aDomSize,
                               size_t* aStyleSize, size_t* aOtherSize) {
  nsGlobalWindowOuter* window = nsGlobalWindowOuter::Cast(aWindow);

  nsTabSizes sizes;
  nsresult rv = AddNonJSSizeOfWindowAndItsDescendents(window, &sizes);
  NS_ENSURE_SUCCESS(rv, rv);

  *aDomSize = sizes.mDom;
  *aStyleSize = sizes.mStyle;
  *aOtherSize = sizes.mOther;
  return NS_OK;
}

/* static */
void nsWindowMemoryReporter::Init() {
  MOZ_ASSERT(!sWindowReporter);
  sWindowReporter = new nsWindowMemoryReporter();
  ClearOnShutdown(&sWindowReporter);
  RegisterStrongMemoryReporter(do_AddRef(sWindowReporter));
  RegisterNonJSSizeOfTab(NonJSSizeOfTab);

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    os->AddObserver(sWindowReporter, "after-minimize-memory-usage",
                    /* weakRef = */ true);
    os->AddObserver(sWindowReporter, "cycle-collector-begin",
                    /* weakRef = */ true);
    os->AddObserver(sWindowReporter, "cycle-collector-end",
                    /* weakRef = */ true);
  }

  RegisterGhostWindowsDistinguishedAmount(GhostWindowsDistinguishedAmount);
}

/* static */
nsWindowMemoryReporter* nsWindowMemoryReporter::Get() {
  return sWindowReporter;
}

static nsCString GetWindowURISpec(nsPIDOMWindowInner* aWindow) {
  NS_ENSURE_TRUE(aWindow, ""_ns);

  nsCOMPtr<Document> doc = aWindow->GetExtantDoc();
  if (doc) {
    nsCOMPtr<nsIURI> uri;
    uri = doc->GetDocumentURI();
    return uri->GetSpecOrDefault();
  }
  nsCOMPtr<nsIScriptObjectPrincipal> scriptObjPrincipal =
      do_QueryObject(aWindow);
  NS_ENSURE_TRUE(scriptObjPrincipal, ""_ns);

  // GetPrincipal() will print a warning if the window does not have an outer
  // window, so check here for an outer window first.  This code is
  // functionally correct if we leave out the GetOuterWindow() check, but we
  // end up printing a lot of warnings during debug mochitests.
  if (!aWindow->GetOuterWindow()) {
    return ""_ns;
  }
  nsIPrincipal* principal = scriptObjPrincipal->GetPrincipal();
  if (!principal) {
    return ""_ns;
  }
  nsCString spec;
  principal->GetAsciiSpec(spec);
  return spec;
}

static void AppendWindowURI(nsPIDOMWindowInner* aWindow, nsACString& aStr,
                            bool aAnonymize) {
  nsCString spec = GetWindowURISpec(aWindow);

  if (spec.IsEmpty()) {
    // If we're unable to find a URI, we're dealing with a chrome window with
    // no document in it (or somesuch), so we call this a "system window".
    aStr += "[system]"_ns;
    return;
  }
  if (aAnonymize && !nsGlobalWindowInner::Cast(aWindow)->IsChromeWindow()) {
    aStr.AppendPrintf("<anonymized-%" PRIu64 ">", aWindow->WindowID());
    return;
  }
  // A hack: replace forward slashes with '\\' so they aren't
  // treated as path separators.  Users of the reporters
  // (such as about:memory) have to undo this change.
  spec.ReplaceChar('/', '\\');
  aStr += spec;
}

MOZ_DEFINE_MALLOC_SIZE_OF(WindowsMallocSizeOf)

// The key is the window ID.
using WindowPaths = nsTHashMap<nsUint64HashKey, nsCString>;

static void ReportAmount(const nsCString& aBasePath, const char* aPathTail,
                         size_t aAmount, const nsCString& aDescription,
                         uint32_t aKind, uint32_t aUnits,
                         nsIHandleReportCallback* aHandleReport,
                         nsISupports* aData) {
  if (aAmount == 0) {
    return;
  }

  nsAutoCString path(aBasePath);
  path += aPathTail;

  aHandleReport->Callback(""_ns, path, aKind, aUnits, aAmount, aDescription,
                          aData);
}

static void ReportSize(const nsCString& aBasePath, const char* aPathTail,
                       size_t aAmount, const nsCString& aDescription,
                       nsIHandleReportCallback* aHandleReport,
                       nsISupports* aData) {
  ReportAmount(aBasePath, aPathTail, aAmount, aDescription,
               nsIMemoryReporter::KIND_HEAP, nsIMemoryReporter::UNITS_BYTES,
               aHandleReport, aData);
}

static void ReportCount(const nsCString& aBasePath, const char* aPathTail,
                        size_t aAmount, const nsCString& aDescription,
                        nsIHandleReportCallback* aHandleReport,
                        nsISupports* aData) {
  ReportAmount(aBasePath, aPathTail, aAmount, aDescription,
               nsIMemoryReporter::KIND_OTHER, nsIMemoryReporter::UNITS_COUNT,
               aHandleReport, aData);
}

static void ReportDOMSize(const nsCString& aBasePath,
                          nsDOMSizes& aTotalDOMSizes,
                          nsIHandleReportCallback* aHandleReport,
                          nsISupports* aData, nsDOMSizes aDOMSizes) {
#define REPORT_DOM_SIZE(_windowPath, _pathTail, _field, _desc) \
  ReportSize(_windowPath, _pathTail, aDOMSizes._field,         \
             nsLiteralCString(_desc), aHandleReport, aData);   \
  aTotalDOMSizes._field += aDOMSizes._field;

  REPORT_DOM_SIZE(aBasePath, "/dom/element-nodes", mDOMElementNodesSize,
                  "Memory used by the element nodes in a window's DOM.");

  REPORT_DOM_SIZE(aBasePath, "/dom/text-nodes", mDOMTextNodesSize,
                  "Memory used by the text nodes in a window's DOM.");

  REPORT_DOM_SIZE(aBasePath, "/dom/cdata-nodes", mDOMCDATANodesSize,
                  "Memory used by the CDATA nodes in a window's DOM.");

  REPORT_DOM_SIZE(aBasePath, "/dom/comment-nodes", mDOMCommentNodesSize,
                  "Memory used by the comment nodes in a window's DOM.");

  REPORT_DOM_SIZE(
      aBasePath, "/dom/event-targets", mDOMEventTargetsSize,
      "Memory used by the event targets table in a window's DOM, and "
      "the objects it points to, which include XHRs.");

  REPORT_DOM_SIZE(aBasePath, "/dom/performance/user-entries",
                  mDOMPerformanceUserEntries,
                  "Memory used for performance user entries.");

  REPORT_DOM_SIZE(aBasePath, "/dom/performance/resource-entries",
                  mDOMPerformanceResourceEntries,
                  "Memory used for performance resource entries.");

  REPORT_DOM_SIZE(aBasePath, "/dom/media-query-lists", mDOMMediaQueryLists,
                  "Memory used by MediaQueryList objects for the window's "
                  "document.");

  REPORT_DOM_SIZE(aBasePath, "/dom/resize-observers",
                  mDOMResizeObserverControllerSize,
                  "Memory used for resize observers.");

  REPORT_DOM_SIZE(aBasePath, "/dom/other", mDOMOtherSize,
                  "Memory used by a window's DOM that isn't measured by the "
                  "other 'dom/' numbers.");
#undef REPORT_DOM_SIZE
}

static void CollectWindowReports(nsGlobalWindowInner* aWindow,
                                 nsWindowSizes* aWindowTotalSizes,
                                 nsTHashSet<uint64_t>* aGhostWindowIDs,
                                 WindowPaths* aWindowPaths,
                                 WindowPaths* aTopWindowPaths,
                                 nsIHandleReportCallback* aHandleReport,
                                 nsISupports* aData, bool aAnonymize) {
  nsAutoCString windowPath("explicit/");

  // Avoid calling aWindow->GetInProcessTop() if there's no outer window.  It
  // will work just fine, but will spew a lot of warnings.
  nsGlobalWindowOuter* top = nullptr;
  if (aWindow->GetOuterWindow()) {
    // Our window should have a null top iff it has a null docshell.
    MOZ_ASSERT(!!aWindow->GetInProcessTopInternal() ==
               !!aWindow->GetDocShell());
    top = aWindow->GetInProcessTopInternal();
  }

  windowPath += "window-objects/"_ns;

  if (top) {
    windowPath += "top("_ns;
    AppendWindowURI(top->GetCurrentInnerWindow(), windowPath, aAnonymize);
    windowPath.AppendPrintf(", id=%" PRIu64 ")", top->WindowID());

    aTopWindowPaths->InsertOrUpdate(aWindow->WindowID(), windowPath);

    windowPath += aWindow->IsFrozen() ? "/cached/"_ns : "/active/"_ns;
  } else {
    if (aGhostWindowIDs->Contains(aWindow->WindowID())) {
      windowPath += "top(none)/ghost/"_ns;
    } else {
      windowPath += "top(none)/detached/"_ns;
    }
  }

  windowPath += "window("_ns;
  AppendWindowURI(aWindow, windowPath, aAnonymize);
  windowPath += ")"_ns;

  // Use |windowPath|, but replace "explicit/" with "event-counts/".
  nsCString censusWindowPath(windowPath);
  censusWindowPath.ReplaceLiteral(0, strlen("explicit"), "event-counts");

  // Remember the path for later.
  aWindowPaths->InsertOrUpdate(aWindow->WindowID(), windowPath);

// Report the size from windowSizes and add to the appropriate total in
// aWindowTotalSizes.
#define REPORT_SIZE(_pathTail, _field, _desc)                \
  ReportSize(windowPath, _pathTail, windowSizes._field,      \
             nsLiteralCString(_desc), aHandleReport, aData); \
  aWindowTotalSizes->_field += windowSizes._field;

// Report the size, which is a sum of other sizes, and so doesn't require
// updating aWindowTotalSizes.
#define REPORT_SUM_SIZE(_pathTail, _amount, _desc)                    \
  ReportSize(windowPath, _pathTail, _amount, nsLiteralCString(_desc), \
             aHandleReport, aData);

// Like REPORT_SIZE, but for a count.
#define REPORT_COUNT(_pathTail, _field, _desc)                 \
  ReportCount(censusWindowPath, _pathTail, windowSizes._field, \
              nsLiteralCString(_desc), aHandleReport, aData);  \
  aWindowTotalSizes->_field += windowSizes._field;

  // This SizeOfState contains the SeenPtrs used for all memory reporting of
  // this window.
  SizeOfState state(WindowsMallocSizeOf);
  nsWindowSizes windowSizes(state);
  aWindow->AddSizeOfIncludingThis(windowSizes);

  ReportDOMSize(windowPath, aWindowTotalSizes->mDOMSizes, aHandleReport, aData,
                windowSizes.mDOMSizes);

  nsCString dataDocumentPath(windowPath);
  dataDocumentPath += "/data-documents";
  nsWindowSizes dataDocumentSizes(state);
  aWindow->CollectDOMSizesForDataDocuments(dataDocumentSizes);
  ReportDOMSize(dataDocumentPath, aWindowTotalSizes->mDOMSizes, aHandleReport,
                aData, dataDocumentSizes.mDOMSizes);

  REPORT_SIZE("/layout/style-sheets", mLayoutStyleSheetsSize,
              "Memory used by document style sheets within a window.");

  REPORT_SIZE("/layout/svg-mapped-declarations", mLayoutSvgMappedDeclarations,
              "Memory used by mapped declarations of SVG elements");

  REPORT_SIZE("/layout/shadow-dom/style-sheets",
              mLayoutShadowDomStyleSheetsSize,
              "Memory used by Shadow DOM style sheets within a window.");

  // TODO(emilio): We might want to split this up between invalidation map /
  // element-and-pseudos / revalidation too just like the style set.
  REPORT_SIZE("/layout/shadow-dom/author-styles", mLayoutShadowDomAuthorStyles,
              "Memory used by Shadow DOM computed rule data within a window.");

  REPORT_SIZE("/layout/pres-shell", mLayoutPresShellSize,
              "Memory used by layout's PresShell, along with any structures "
              "allocated in its arena and not measured elsewhere, "
              "within a window.");

  REPORT_SIZE("/layout/display-list", mLayoutRetainedDisplayListSize,
              "Memory used by the retained display list data, "
              "along with any structures allocated in its arena and not "
              "measured elsewhere, within a window.");

  REPORT_SIZE("/layout/style-sets/stylist/rule-tree",
              mLayoutStyleSetsStylistRuleTree,
              "Memory used by rule trees within style sets within a window.");

  REPORT_SIZE("/layout/style-sets/stylist/element-and-pseudos-maps",
              mLayoutStyleSetsStylistElementAndPseudosMaps,
              "Memory used by element and pseudos maps within style "
              "sets within a window.");

  REPORT_SIZE("/layout/style-sets/stylist/invalidation-map",
              mLayoutStyleSetsStylistInvalidationMap,
              "Memory used by invalidation maps within style sets "
              "within a window.");

  REPORT_SIZE("/layout/style-sets/stylist/revalidation-selectors",
              mLayoutStyleSetsStylistRevalidationSelectors,
              "Memory used by selectors for cache revalidation within "
              "style sets within a window.");

  REPORT_SIZE("/layout/style-sets/stylist/other", mLayoutStyleSetsStylistOther,
              "Memory used by other Stylist data within style sets "
              "within a window.");

  REPORT_SIZE("/layout/style-sets/other", mLayoutStyleSetsOther,
              "Memory used by other parts of style sets within a window.");

  REPORT_SIZE("/layout/element-data-objects", mLayoutElementDataObjects,
              "Memory used for ElementData objects, but not the things "
              "hanging off them.");

  REPORT_SIZE("/layout/text-runs", mLayoutTextRunsSize,
              "Memory used for text-runs (glyph layout) in the PresShell's "
              "frame tree, within a window.");

  REPORT_SIZE("/layout/pres-contexts", mLayoutPresContextSize,
              "Memory used for the PresContext in the PresShell's frame "
              "within a window.");

  REPORT_SIZE("/layout/frame-properties", mLayoutFramePropertiesSize,
              "Memory used for frame properties attached to frames "
              "within a window.");

  REPORT_SIZE("/layout/computed-values/dom", mLayoutComputedValuesDom,
              "Memory used by ComputedValues objects accessible from DOM "
              "elements.");

  REPORT_SIZE("/layout/computed-values/non-dom", mLayoutComputedValuesNonDom,
              "Memory used by ComputedValues objects not accessible from DOM "
              "elements.");

  REPORT_SIZE("/layout/computed-values/visited", mLayoutComputedValuesVisited,
              "Memory used by ComputedValues objects used for visited styles.");

  REPORT_SIZE("/property-tables", mPropertyTablesSize,
              "Memory used for the property tables within a window.");

  REPORT_SIZE("/bindings", mBindingsSize,
              "Memory used by bindings within a window.");

  REPORT_COUNT("/dom/event-targets", mDOMEventTargetsCount,
               "Number of non-node event targets in the event targets table "
               "in a window's DOM, such as XHRs.");

  REPORT_COUNT("/dom/event-listeners", mDOMEventListenersCount,
               "Number of event listeners in a window, including event "
               "listeners on nodes and other event targets.");

  REPORT_COUNT(
      "/media/media-source-urls", mMediaSourceURLsCount,
      "Number of MediaSource object URLs allocated with URL.createObjectURL; "
      "the referenced data cannot be freed until all URLs for it have been "
      "explicitly invalidated with URL.revokeObjectURL.");

  // There are many different kinds of frames, but it is very likely
  // that only a few matter.  Implement a cutoff so we don't bloat
  // about:memory with many uninteresting entries.
  const size_t ARENA_SUNDRIES_THRESHOLD =
      js::MemoryReportingSundriesThreshold();

  size_t presArenaSundriesSize = 0;
#define ARENA_OBJECT(name_, sundries_size_, prefix_)                    \
  {                                                                     \
    size_t size = windowSizes.mArenaSizes.NS_ARENA_SIZES_FIELD(name_);  \
    if (size < ARENA_SUNDRIES_THRESHOLD) {                              \
      sundries_size_ += size;                                           \
    } else {                                                            \
      REPORT_SUM_SIZE(prefix_ #name_, size,                             \
                      "Memory used by objects of type " #name_          \
                      " within a window.");                             \
    }                                                                   \
    aWindowTotalSizes->mArenaSizes.NS_ARENA_SIZES_FIELD(name_) += size; \
  }
#define PRES_ARENA_OBJECT(name_) \
  ARENA_OBJECT(name_, presArenaSundriesSize, "/layout/pres-arena/")
#include "nsPresArenaObjectList.inc"
#undef PRES_ARENA_OBJECT

  if (presArenaSundriesSize > 0) {
    REPORT_SUM_SIZE(
        "/layout/pres-arena/sundries", presArenaSundriesSize,
        "The sum of all memory used by objects in the arena which were too "
        "small to be shown individually.");
  }

  size_t displayListArenaSundriesSize = 0;
#define DISPLAY_LIST_ARENA_OBJECT(name_)            \
  ARENA_OBJECT(name_, displayListArenaSundriesSize, \
               "/layout/display-list-arena/")
#include "nsDisplayListArenaTypes.inc"
#undef DISPLAY_LIST_ARENA_OBJECT

  if (displayListArenaSundriesSize > 0) {
    REPORT_SUM_SIZE(
        "/layout/display-list-arena/sundries", displayListArenaSundriesSize,
        "The sum of all memory used by objects in the DL arena which were too "
        "small to be shown individually.");
  }

#undef ARENA_OBJECT

  // There are many different kinds of style structs, but it is likely that
  // only a few matter. Implement a cutoff so we don't bloat about:memory with
  // many uninteresting entries.
  const size_t STYLE_SUNDRIES_THRESHOLD =
      js::MemoryReportingSundriesThreshold();

  size_t styleSundriesSize = 0;
#define PROCESS_STYLE_STRUCT(name_)                                     \
  {                                                                     \
    size_t size = windowSizes.mStyleSizes.NS_STYLE_SIZES_FIELD(name_);  \
    if (size < STYLE_SUNDRIES_THRESHOLD) {                              \
      styleSundriesSize += size;                                        \
    } else {                                                            \
      REPORT_SUM_SIZE("/layout/style-structs/" #name_, size,            \
                      "Memory used by the " #name_                      \
                      " style structs within a window.");               \
    }                                                                   \
    aWindowTotalSizes->mStyleSizes.NS_STYLE_SIZES_FIELD(name_) += size; \
  }
  FOR_EACH_STYLE_STRUCT(PROCESS_STYLE_STRUCT, PROCESS_STYLE_STRUCT)
#undef PROCESS_STYLE_STRUCT

  if (styleSundriesSize > 0) {
    REPORT_SUM_SIZE(
        "/layout/style-structs/sundries", styleSundriesSize,
        "The sum of all memory used by style structs which were too "
        "small to be shown individually.");
  }

#undef REPORT_SIZE
#undef REPORT_SUM_SIZE
#undef REPORT_COUNT
}

using WindowArray = nsTArray<RefPtr<nsGlobalWindowInner>>;

NS_IMETHODIMP
nsWindowMemoryReporter::CollectReports(nsIHandleReportCallback* aHandleReport,
                                       nsISupports* aData, bool aAnonymize) {
  nsGlobalWindowInner::InnerWindowByIdTable* windowsById =
      nsGlobalWindowInner::GetWindowsTable();
  NS_ENSURE_TRUE(windowsById, NS_OK);

  // Hold on to every window in memory so that window objects can't be
  // destroyed while we're calling the memory reporter callback.
  const auto windows = ToTArray<WindowArray>(windowsById->Values());

  // Get the IDs of all the "ghost" windows, and call
  // aHandleReport->Callback() for each one.
  nsTHashSet<uint64_t> ghostWindows;
  CheckForGhostWindows(&ghostWindows);

  for (const auto& key : ghostWindows) {
    nsGlobalWindowInner* window = windowsById->Get(key);
    if (!window) {
      NS_WARNING("Could not look up window?");
      continue;
    }

    nsAutoCString path;
    path.AppendLiteral("ghost-windows/");
    AppendWindowURI(window, path, aAnonymize);

    aHandleReport->Callback(
        /* process = */ ""_ns, path, nsIMemoryReporter::KIND_OTHER,
        nsIMemoryReporter::UNITS_COUNT,
        /* amount = */ 1,
        /* description = */ "A ghost window."_ns, aData);
  }

  // clang-format off
  MOZ_COLLECT_REPORT(
    "ghost-windows", KIND_OTHER, UNITS_COUNT, ghostWindows.Count(),
"The number of ghost windows present (the number of nodes underneath "
"explicit/window-objects/top(none)/ghost, modulo race conditions).  A ghost "
"window is not shown in any tab, is not in a browsing context group with any "
"non-detached windows, and has met these criteria for at least "
"memory.ghost_window_timeout_seconds, or has survived a round of "
"about:memory's minimize memory usage button.\n\n"
"Ghost windows can happen legitimately, but they are often indicative of "
"leaks in the browser or add-ons.");
  // clang-format on

  WindowPaths windowPaths;
  WindowPaths topWindowPaths;

  // Collect window memory usage.
  SizeOfState fakeState(nullptr);  // this won't be used
  nsWindowSizes windowTotalSizes(fakeState);
  for (uint32_t i = 0; i < windows.Length(); i++) {
    CollectWindowReports(windows[i], &windowTotalSizes, &ghostWindows,
                         &windowPaths, &topWindowPaths, aHandleReport, aData,
                         aAnonymize);
  }

  // Report JS memory usage.  We do this from here because the JS memory
  // reporter needs to be passed |windowPaths|.
  xpc::JSReporter::CollectReports(&windowPaths, &topWindowPaths, aHandleReport,
                                  aData, aAnonymize);

  nsXULPrototypeCache::CollectMemoryReports(aHandleReport, aData);

#define REPORT(_path, _amount, _desc)                                    \
  aHandleReport->Callback(""_ns, nsLiteralCString(_path), KIND_OTHER,    \
                          UNITS_BYTES, _amount, nsLiteralCString(_desc), \
                          aData);

  REPORT("window-objects/dom/element-nodes",
         windowTotalSizes.mDOMSizes.mDOMElementNodesSize,
         "This is the sum of all windows' 'dom/element-nodes' numbers.");

  REPORT("window-objects/dom/text-nodes",
         windowTotalSizes.mDOMSizes.mDOMTextNodesSize,
         "This is the sum of all windows' 'dom/text-nodes' numbers.");

  REPORT("window-objects/dom/cdata-nodes",
         windowTotalSizes.mDOMSizes.mDOMCDATANodesSize,
         "This is the sum of all windows' 'dom/cdata-nodes' numbers.");

  REPORT("window-objects/dom/comment-nodes",
         windowTotalSizes.mDOMSizes.mDOMCommentNodesSize,
         "This is the sum of all windows' 'dom/comment-nodes' numbers.");

  REPORT("window-objects/dom/event-targets",
         windowTotalSizes.mDOMSizes.mDOMEventTargetsSize,
         "This is the sum of all windows' 'dom/event-targets' numbers.");

  REPORT("window-objects/dom/performance",
         windowTotalSizes.mDOMSizes.mDOMPerformanceUserEntries +
             windowTotalSizes.mDOMSizes.mDOMPerformanceResourceEntries,
         "This is the sum of all windows' 'dom/performance/' numbers.");

  REPORT("window-objects/dom/other", windowTotalSizes.mDOMSizes.mDOMOtherSize,
         "This is the sum of all windows' 'dom/other' numbers.");

  REPORT("window-objects/layout/style-sheets",
         windowTotalSizes.mLayoutStyleSheetsSize,
         "This is the sum of all windows' 'layout/style-sheets' numbers.");

  REPORT("window-objects/layout/pres-shell",
         windowTotalSizes.mLayoutPresShellSize,
         "This is the sum of all windows' 'layout/arenas' numbers.");

  REPORT("window-objects/layout/style-sets",
         windowTotalSizes.mLayoutStyleSetsStylistRuleTree +
             windowTotalSizes.mLayoutStyleSetsStylistElementAndPseudosMaps +
             windowTotalSizes.mLayoutStyleSetsStylistInvalidationMap +
             windowTotalSizes.mLayoutStyleSetsStylistRevalidationSelectors +
             windowTotalSizes.mLayoutStyleSetsStylistOther +
             windowTotalSizes.mLayoutStyleSetsOther,
         "This is the sum of all windows' 'layout/style-sets/' numbers.");

  REPORT("window-objects/layout/element-data-objects",
         windowTotalSizes.mLayoutElementDataObjects,
         "This is the sum of all windows' 'layout/element-data-objects' "
         "numbers.");

  REPORT("window-objects/layout/text-runs",
         windowTotalSizes.mLayoutTextRunsSize,
         "This is the sum of all windows' 'layout/text-runs' numbers.");

  REPORT("window-objects/layout/pres-contexts",
         windowTotalSizes.mLayoutPresContextSize,
         "This is the sum of all windows' 'layout/pres-contexts' numbers.");

  REPORT("window-objects/layout/frame-properties",
         windowTotalSizes.mLayoutFramePropertiesSize,
         "This is the sum of all windows' 'layout/frame-properties' numbers.");

  REPORT("window-objects/layout/computed-values",
         windowTotalSizes.mLayoutComputedValuesDom +
             windowTotalSizes.mLayoutComputedValuesNonDom +
             windowTotalSizes.mLayoutComputedValuesVisited,
         "This is the sum of all windows' 'layout/computed-values/' numbers.");

  REPORT("window-objects/property-tables", windowTotalSizes.mPropertyTablesSize,
         "This is the sum of all windows' 'property-tables' numbers.");

  size_t presArenaTotal = 0;
#define PRES_ARENA_OBJECT(name_) \
  presArenaTotal += windowTotalSizes.mArenaSizes.NS_ARENA_SIZES_FIELD(name_);
#include "nsPresArenaObjectList.inc"
#undef PRES_ARENA_OBJECT

  REPORT("window-objects/layout/pres-arena", presArenaTotal,
         "Memory used for the pres arena within windows. "
         "This is the sum of all windows' 'layout/pres-arena/' numbers.");

  size_t displayListArenaTotal = 0;
#define DISPLAY_LIST_ARENA_OBJECT(name_) \
  displayListArenaTotal +=               \
      windowTotalSizes.mArenaSizes.NS_ARENA_SIZES_FIELD(name_);
#include "nsDisplayListArenaTypes.inc"
#undef DISPLAY_LIST_ARENA_OBJECT

  REPORT("window-objects/layout/display-list-arena", displayListArenaTotal,
         "Memory used for the display list arena within windows. This is the "
         "sum of all windows' 'layout/display-list-arena/' numbers.");

  size_t styleTotal = 0;
#define ADD_TO_STYLE_TOTAL(name_) \
  styleTotal += windowTotalSizes.mStyleSizes.NS_STYLE_SIZES_FIELD(name_);
  FOR_EACH_STYLE_STRUCT(ADD_TO_STYLE_TOTAL, ADD_TO_STYLE_TOTAL)
#undef ADD_TO_STYLE_TOTAL

  REPORT("window-objects/layout/style-structs", styleTotal,
         "Memory used for style structs within windows. This is the sum of "
         "all windows' 'layout/style-structs/' numbers.");

#undef REPORT

  return NS_OK;
}

uint32_t nsWindowMemoryReporter::GetGhostTimeout() {
  return Preferences::GetUint("memory.ghost_window_timeout_seconds", 60);
}

NS_IMETHODIMP
nsWindowMemoryReporter::Observe(nsISupports* aSubject, const char* aTopic,
                                const char16_t* aData) {
  if (!strcmp(aTopic, "after-minimize-memory-usage")) {
    ObserveAfterMinimizeMemoryUsage();
  } else if (!strcmp(aTopic, "cycle-collector-begin")) {
    if (mCheckTimer) {
      mCheckTimerWaitingForCCEnd = true;
      KillCheckTimer();
    }
    mCycleCollectorIsRunning = true;
  } else if (!strcmp(aTopic, "cycle-collector-end")) {
    mCycleCollectorIsRunning = false;
    if (mCheckTimerWaitingForCCEnd) {
      mCheckTimerWaitingForCCEnd = false;
      AsyncCheckForGhostWindows();
    }
  } else {
    MOZ_ASSERT(false);
  }

  return NS_OK;
}

void nsWindowMemoryReporter::ObserveDOMWindowDetached(
    nsGlobalWindowInner* aWindow) {
  nsWeakPtr weakWindow = do_GetWeakReference(aWindow);
  if (!weakWindow) {
    NS_WARNING("Couldn't take weak reference to a window?");
    return;
  }

  mDetachedWindows.InsertOrUpdate(weakWindow, TimeStamp());

  AsyncCheckForGhostWindows();
}

// static
void nsWindowMemoryReporter::CheckTimerFired(nsITimer* aTimer, void* aData) {
  if (sWindowReporter) {
    MOZ_ASSERT(!sWindowReporter->mCycleCollectorIsRunning);
    sWindowReporter->CheckForGhostWindows();
  }
}

void nsWindowMemoryReporter::AsyncCheckForGhostWindows() {
  if (mCheckTimer) {
    return;
  }

  if (mCycleCollectorIsRunning) {
    mCheckTimerWaitingForCCEnd = true;
    return;
  }

  // If more than kTimeBetweenChecks seconds have elapsed since the last check,
  // timerDelay is 0.  Otherwise, it is kTimeBetweenChecks, reduced by the time
  // since the last check.  Reducing the delay by the time since the last check
  // prevents the timer from being completely starved if it is repeatedly killed
  // and restarted.
  int32_t timeSinceLastCheck =
      (TimeStamp::NowLoRes() - mLastCheckForGhostWindows).ToSeconds();
  int32_t timerDelay =
      (kTimeBetweenChecks - std::min(timeSinceLastCheck, kTimeBetweenChecks)) *
      PR_MSEC_PER_SEC;

  NS_NewTimerWithFuncCallback(
      getter_AddRefs(mCheckTimer), CheckTimerFired, nullptr, timerDelay,
      nsITimer::TYPE_ONE_SHOT,
      "nsWindowMemoryReporter::AsyncCheckForGhostWindows_timer"_ns);
}

void nsWindowMemoryReporter::ObserveAfterMinimizeMemoryUsage() {
  // Someone claims they've done enough GC/CCs so that all eligible windows
  // have been free'd.  So we deem that any windows which satisfy ghost
  // criteria (1) and (2) now satisfy criterion (3) as well.
  //
  // To effect this change, we'll backdate some of our timestamps.

  TimeStamp minTimeStamp =
      TimeStamp::Now() - TimeDuration::FromSeconds(GetGhostTimeout());

  for (auto iter = mDetachedWindows.Iter(); !iter.Done(); iter.Next()) {
    TimeStamp& timeStamp = iter.Data();
    if (!timeStamp.IsNull() && timeStamp > minTimeStamp) {
      timeStamp = minTimeStamp;
    }
  }
}

/**
 * Iterate over mDetachedWindows and update it to reflect the current state of
 * the world.  In particular:
 *
 *   - Remove weak refs to windows which no longer exist.
 *
 *   - Remove references to windows which are no longer detached.
 *
 *   - Reset the timestamp on detached windows which share a domain with a
 *     non-detached window (they no longer meet ghost criterion (2)).
 *
 *   - If a window now meets ghost criterion (2) but didn't before, set its
 *     timestamp to now.
 *
 * Additionally, if aOutGhostIDs is not null, fill it with the window IDs of
 * all ghost windows we found.
 */
void nsWindowMemoryReporter::CheckForGhostWindows(
    nsTHashSet<uint64_t>* aOutGhostIDs /* = nullptr */) {
  nsGlobalWindowInner::InnerWindowByIdTable* windowsById =
      nsGlobalWindowInner::GetWindowsTable();
  if (!windowsById) {
    NS_WARNING("GetWindowsTable returned null");
    return;
  }

  mLastCheckForGhostWindows = TimeStamp::NowLoRes();
  KillCheckTimer();

  nsTHashSet<BrowsingContextGroup*> nonDetachedBrowsingContextGroups;

  // Populate nonDetachedBrowsingContextGroups.
  for (const auto& entry : *windowsById) {
    // Null outer window implies null top, but calling GetInProcessTop() when
    // there's no outer window causes us to spew debug warnings.
    nsGlobalWindowInner* window = entry.GetWeak();
    if (!window->GetOuterWindow() || !window->GetInProcessTopInternal() ||
        !window->GetBrowsingContextGroup()) {
      // This window is detached, so we don't care about its browsing
      // context group.
      continue;
    }

    nonDetachedBrowsingContextGroups.Insert(window->GetBrowsingContextGroup());
  }

  // Update mDetachedWindows and write the ghost window IDs into aOutGhostIDs,
  // if it's not null.
  uint32_t ghostTimeout = GetGhostTimeout();
  TimeStamp now = mLastCheckForGhostWindows;
  mGhostWindowCount = 0;
  for (auto iter = mDetachedWindows.Iter(); !iter.Done(); iter.Next()) {
    nsWeakPtr weakKey = do_QueryInterface(iter.Key());
    nsCOMPtr<mozIDOMWindow> iwindow = do_QueryReferent(weakKey);
    if (!iwindow) {
      // The window object has been destroyed.  Stop tracking its weak ref in
      // our hashtable.
      iter.Remove();
      continue;
    }

    nsPIDOMWindowInner* window = nsPIDOMWindowInner::From(iwindow);

    // Avoid calling GetInProcessTop() if we have no outer window.  Nothing
    // will break if we do, but it will spew debug output, which can cause our
    // test logs to overflow.
    nsCOMPtr<nsPIDOMWindowOuter> top;
    if (window->GetOuterWindow()) {
      top = window->GetOuterWindow()->GetInProcessTop();
    }

    if (top) {
      // The window is no longer detached, so we no longer want to track it.
      iter.Remove();
      continue;
    }

    TimeStamp& timeStamp = iter.Data();
    BrowsingContextGroup* browsingContextGroup =
        window->GetBrowsingContextGroup();
    if (browsingContextGroup &&
        nonDetachedBrowsingContextGroups.Contains(browsingContextGroup)) {
      // This window is in the same browsing context group as a non-detached
      // window, so reset its clock.
      timeStamp = TimeStamp();
    } else {
      // This window is not in the same browsing context group as a non-detached
      // window, so it meets ghost criterion (2).
      if (timeStamp.IsNull()) {
        // This may become a ghost window later; start its clock.
        timeStamp = now;
      } else if ((now - timeStamp).ToSeconds() > ghostTimeout) {
        // This definitely is a ghost window, so add it to aOutGhostIDs, if
        // that is not null.
        mGhostWindowCount++;
        if (aOutGhostIDs && window) {
          aOutGhostIDs->Insert(window->WindowID());
        }
      }
    }
  }
}

/* static */
int64_t nsWindowMemoryReporter::GhostWindowsDistinguishedAmount() {
  return sWindowReporter->mGhostWindowCount;
}

void nsWindowMemoryReporter::KillCheckTimer() {
  if (mCheckTimer) {
    mCheckTimer->Cancel();
    mCheckTimer = nullptr;
  }
}

#ifdef DEBUG
/* static */
void nsWindowMemoryReporter::UnlinkGhostWindows() {
  if (!sWindowReporter) {
    return;
  }

  nsGlobalWindowInner::InnerWindowByIdTable* windowsById =
      nsGlobalWindowInner::GetWindowsTable();
  if (!windowsById) {
    return;
  }

  // Hold on to every window in memory so that window objects can't be
  // destroyed while we're calling the UnlinkGhostWindows callback.
  const auto windows = ToTArray<WindowArray>(windowsById->Values());

  // Get the IDs of all the "ghost" windows, and unlink them all.
  nsTHashSet<uint64_t> ghostWindows;
  sWindowReporter->CheckForGhostWindows(&ghostWindows);
  for (const auto& key : ghostWindows) {
    nsGlobalWindowInner::InnerWindowByIdTable* windowsById =
        nsGlobalWindowInner::GetWindowsTable();
    if (!windowsById) {
      continue;
    }

    RefPtr<nsGlobalWindowInner> window = windowsById->Get(key);
    if (window) {
      window->RiskyUnlink();
    }
  }
}
#endif

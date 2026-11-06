/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDragSessionSource.h"

#include <dlfcn.h>
#include <gtk/gtk.h>

#include "GRefPtr.h"
#include "mozilla/Logging.h"
#include "mozilla/PresShell.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "nsAppShell.h"
#include "nsArrayUtils.h"
#include "nsCRT.h"
#include "nsComponentManagerUtils.h"
#include "nsICookieJarSettings.h"
#include "nsIFileURL.h"
#include "nsIIOService.h"
#include "nsISupportsPrimitives.h"
#include "nsNetUtil.h"
#include "nsPrimitiveHelpers.h"
#include "nsSystemInfo.h"
#include "nsWidgetsCID.h"
#include "nsWindow.h"
#ifdef MOZ_X11
#  include "gfxXlibSurface.h"
#endif
#include "ScreenHelperGTK.h"
#include "gfxPlatform.h"
#include "mozilla/dom/Document.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/widget/nsGtkHtmlUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsEscape.h"
#include "nsGtkUtils.h"
#include "nsIContent.h"
#include "nsImageToPixbuf.h"
#include "nsPresContext.h"
#include "nsStringStream.h"

using namespace mozilla;
using namespace mozilla::widget;
using namespace mozilla::gfx;

#define NS_DND_TMP_CLEANUP_TIMEOUT (1000 * 60 * 5)

#define NS_SYSTEMINFO_CONTRACTID "@mozilla.org/system-info;1"

#define DRAG_IMAGE_ALPHA_LEVEL 0.5

#undef LOGDRAGSERVICE
#undef LOGDRAGSERVICESTATIC
#ifdef MOZ_LOGGING
extern mozilla::LazyLogModule gWidgetDragLog;
#  define LOGDRAGSERVICE(str, ...)                                             \
    MOZ_LOG(                                                                   \
        gWidgetDragLog, mozilla::LogLevel::Debug,                              \
        ("[D %d]%s %*s" str, nsDragSession::GetLoopDepth(),                    \
         GetDebugTag().get(),                                                  \
         nsDragSession::GetLoopDepth() > 1 ? nsDragSession::GetLoopDepth() * 2 \
                                           : 0,                                \
         "", ##__VA_ARGS__))
#  define LOGDRAGSERVICESTATIC(str, ...) \
    MOZ_LOG(gWidgetDragLog, mozilla::LogLevel::Debug, (str, ##__VA_ARGS__))
#else
#  define LOGDRAGSERVICE(...)
#  define LOGDRAGSERVICESTATIC(...)
#endif

class MOZ_STACK_CLASS AutoSuspendNativeEvents {
 public:
  AutoSuspendNativeEvents() {
    mAppShell = do_GetService(NS_APPSHELL_CID);
    mAppShell->SuspendNative();
  }
  ~AutoSuspendNativeEvents() { mAppShell->ResumeNative(); }

 private:
  nsCOMPtr<nsIAppShell> mAppShell;
};

static guint sMotionEventTimerID;

static GdkEvent* sMotionEvent;
static GUniquePtr<GdkEvent> TakeMotionEvent() {
  GUniquePtr<GdkEvent> event(sMotionEvent);
  sMotionEvent = nullptr;
  return event;
}
static void SetMotionEvent(GUniquePtr<GdkEvent> aEvent) {
  TakeMotionEvent();
  sMotionEvent = aEvent.release();
}

static GtkWidget* sGrabWidget;

static constexpr nsLiteralString kDisallowedExportedSchemes[] = {
    u"about"_ns,          u"blob"_ns,        u"cached-favicon"_ns,
    u"chrome"_ns,         u"imap"_ns,        u"javascript"_ns,
    u"mailbox"_ns,        u"news"_ns,        u"page-icon"_ns,
    u"resource"_ns,       u"view-source"_ns, u"moz-extension"_ns,
    u"moz-page-thumb"_ns,
};

// See https://docs.gtk.org/gtk3/enum.DragResult.html
static const char kGtkDragResults[][100]{
    "GTK_DRAG_RESULT_SUCCESS",        "GTK_DRAG_RESULT_NO_TARGET",
    "GTK_DRAG_RESULT_USER_CANCELLED", "GTK_DRAG_RESULT_TIMEOUT_EXPIRED",
    "GTK_DRAG_RESULT_GRAB_BROKEN",    "GTK_DRAG_RESULT_ERROR"};

static void invisibleSourceDragBegin(GtkWidget* aWidget,
                                     GdkDragContext* aContext, gpointer aData);

static void invisibleSourceDragEnd(GtkWidget* aWidget, GdkDragContext* aContext,
                                   gpointer aData);

static gboolean invisibleSourceDragFailed(GtkWidget* aWidget,
                                          GdkDragContext* aContext,
                                          gint aResult, gpointer aData);

static void invisibleSourceDragDataGet(GtkWidget* aWidget,
                                       GdkDragContext* aContext,
                                       GtkSelectionData* aSelectionData,
                                       guint aInfo, guint32 aTime,
                                       gpointer aData);

static void UTF16ToNewUTF8(const char16_t* aUTF16, uint32_t aUTF16Len,
                           char** aUTF8, uint32_t* aUTF8Len) {
  nsDependentSubstring utf16(aUTF16, aUTF16Len);
  *aUTF8 = ToNewUTF8String(utf16, aUTF8Len);
}

static bool CanExportAsURLTarget(const char16_t* aURLData, uint32_t aURLLen) {
  for (const nsLiteralString& disallowed : kDisallowedExportedSchemes) {
    auto len = disallowed.AsString().Length();
    if (len < aURLLen) {
      if (!memcmp(disallowed.get(), aURLData,
                  /* len is char16_t char count */ len * 2)) {
        LOGDRAGSERVICESTATIC("rejected URL scheme %s\n",
                             NS_ConvertUTF16toUTF8(disallowed).get());
        return false;
      }
    }
  }
  return true;
}

static void TargetArrayAddTarget(nsTArray<GtkTargetEntry*>& aTargetArray,
                                 const char* aTarget) {
  GtkTargetEntry* target = (GtkTargetEntry*)g_malloc(sizeof(GtkTargetEntry));
  target->target = g_strdup(aTarget);
  target->flags = 0;
  aTargetArray.AppendElement(target);
  LOGDRAGSERVICESTATIC("adding target %s\n", aTarget);
}

static GtkWindow* GetGtkWindow(dom::Document* aDocument) {
  if (!aDocument) return nullptr;

  PresShell* presShell = aDocument->GetPresShell();
  if (!presShell) {
    return nullptr;
  }

  nsCOMPtr<nsIWidget> widget = presShell->GetRootWidget();
  if (!widget) {
    return nullptr;
  }

  GtkWidget* gtkWidget =
      GTK_WIDGET(widget->GetNativeData(NS_NATIVE_SHELLWIDGET));
  if (!gtkWidget) return nullptr;

  GtkWidget* toplevel = nullptr;
  toplevel = gtk_widget_get_toplevel(gtkWidget);
  if (!GTK_IS_WINDOW(toplevel)) return nullptr;

  return GTK_WINDOW(toplevel);
}

static nsresult GetDownloadDetails(nsITransferable* aTransferable,
                                   nsIURI** aSourceURI, nsAString& aFilename) {
  *aSourceURI = nullptr;
  MOZ_ASSERT(aTransferable != nullptr, "aTransferable must not be null");

  nsCOMPtr<nsISupports> urlPrimitive;
  nsresult rv = aTransferable->GetTransferData(kFilePromiseURLMime,
                                               getter_AddRefs(urlPrimitive));
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsISupportsString> srcUrlPrimitive = do_QueryInterface(urlPrimitive);
  if (!srcUrlPrimitive) {
    return NS_ERROR_FAILURE;
  }

  nsAutoString srcUri;
  srcUrlPrimitive->GetData(srcUri);
  if (srcUri.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIURI> sourceURI;
  NS_NewURI(getter_AddRefs(sourceURI), srcUri);

  nsAutoString srcFileName;
  nsCOMPtr<nsISupports> fileNamePrimitive;
  rv = aTransferable->GetTransferData(kFilePromiseDestFilename,
                                      getter_AddRefs(fileNamePrimitive));
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsISupportsString> srcFileNamePrimitive =
      do_QueryInterface(fileNamePrimitive);
  if (srcFileNamePrimitive) {
    srcFileNamePrimitive->GetData(srcFileName);
  } else {
    nsCOMPtr<nsIURL> sourceURL = do_QueryInterface(sourceURI);
    if (!sourceURL) {
      return NS_ERROR_FAILURE;
    }
    nsAutoCString urlFileName;
    sourceURL->GetFileName(urlFileName);
    NS_UnescapeURL(urlFileName);
    CopyUTF8toUTF16(urlFileName, srcFileName);
  }
  if (srcFileName.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }

  sourceURI.swap(*aSourceURI);
  aFilename = std::move(srcFileName);
  return NS_OK;
}

// Support for periodic drag events
static gboolean DispatchMotionEventCopy(gpointer aData) {
  sMotionEventTimerID = 0;

  GUniquePtr<GdkEvent> event = TakeMotionEvent();
  if (gtk_widget_has_grab(sGrabWidget)) {
    gtk_propagate_event(sGrabWidget, event.get());
  }

  return FALSE;
}

static void OnSourceGrabEventAfter(GtkWidget* widget, GdkEvent* event,
                                   gpointer user_data) {
  if (!gtk_widget_has_grab(sGrabWidget)) return;

  if (event->type == GDK_MOTION_NOTIFY) {
    SetMotionEvent(GUniquePtr<GdkEvent>(gdk_event_copy(event)));

    nsDragSessionSource* session = static_cast<nsDragSessionSource*>(user_data);
    gint scale = mozilla::widget::ScreenHelperGTK::GetGTKMonitorScaleFactor();
    auto p = LayoutDeviceIntPoint::Round(event->motion.x_root * scale,
                                         event->motion.y_root * scale);
    session->SetDragEndPoint(p.x, p.y);
  } else if (sMotionEvent &&
             (event->type == GDK_KEY_PRESS || event->type == GDK_KEY_RELEASE)) {
    sMotionEvent->motion.state = event->key.state;
  } else {
    return;
  }

  if (sMotionEventTimerID) {
    g_source_remove(sMotionEventTimerID);
  }

  sMotionEventTimerID = g_timeout_add_full(
      G_PRIORITY_DEFAULT_IDLE, 350, DispatchMotionEventCopy, nullptr, nullptr);
}

nsDragSessionSource::nsDragSessionSource() {
  LOGDRAGSERVICE("nsDragSessionSource::nsDragSessionSource()");

  // Using an offscreen window works around bug 983843.
  mHiddenWidget = gtk_offscreen_window_new();
  gtk_widget_realize(mHiddenWidget);
  g_signal_connect(mHiddenWidget, "drag_begin",
                   G_CALLBACK(invisibleSourceDragBegin), this);
  g_signal_connect(mHiddenWidget, "drag_data_get",
                   G_CALLBACK(invisibleSourceDragDataGet), this);
  g_signal_connect(mHiddenWidget, "drag_end",
                   G_CALLBACK(invisibleSourceDragEnd), this);
  g_signal_connect(mHiddenWidget, "drag-failed",
                   G_CALLBACK(invisibleSourceDragFailed), this);

  mTempFileTimerID = 0;
#ifdef MOZ_X11
  mActive = widget::GdkIsX11Display();
#endif
}

nsDragSessionSource::~nsDragSessionSource() {
  LOGDRAGSERVICE("nsDragSessionSource::~nsDragSessionSource");
  MozClearHandleID(mTempFileTimerID, g_source_remove);
  RemoveTempFiles();
  MozClearPointer(mHiddenWidget, gtk_widget_destroy);
}

NS_IMETHODIMP
nsDragSessionSource::InvokeDragSession(
    nsIWidget* aWidget, nsINode* aDOMNode, nsIPrincipal* aPrincipal,
    nsIPolicyContainer* aPolicyContainer,
    nsICookieJarSettings* aCookieJarSettings, nsIArray* aArrayTransferables,
    uint32_t aActionType,
    nsContentPolicyType aContentPolicyType = nsIContentPolicy::TYPE_OTHER) {
  LOGDRAGSERVICE("nsDragSessionSource::InvokeDragSession");

  if (mSourceNode) return NS_ERROR_NOT_AVAILABLE;

  return nsBaseDragSession::InvokeDragSession(
      aWidget, aDOMNode, aPrincipal, aPolicyContainer, aCookieJarSettings,
      aArrayTransferables, aActionType, aContentPolicyType);
}

nsresult nsDragSessionSource::InvokeDragSessionImpl(
    nsIWidget* aWidget, nsIArray* aArrayTransferables,
    const Maybe<CSSIntRegion>& aRegion, uint32_t aActionType) {
  if (!aArrayTransferables) return NS_ERROR_INVALID_ARG;
  mSourceDataItems = aArrayTransferables;

  // Use the device that pressed the button, which GTK also drags with.
  //
  // With several seats, another seat's pointer may not be over any of our
  // windows, so asking it for the origin window would fail the drag.
  GdkEvent* existingEvent = widget::GetLastPointerDownEvent();
  GdkDevice* device = existingEvent ? gdk_event_get_device(existingEvent)
                                    : widget::GdkGetPointer();
  GdkWindow* originGdkWindow = nullptr;
  if (widget::GdkIsWaylandDisplay() || widget::IsXWaylandProtocol()) {
    originGdkWindow =
        gdk_device_get_window_at_position(device, nullptr, nullptr);
    if (!originGdkWindow) {
      NS_WARNING(
          "nsDragSessionSource::InvokeDragSessionImpl(): Missing origin "
          "GdkWindow!");
      return NS_ERROR_FAILURE;
    }
  }
#ifdef MOZ_WAYLAND
  if (widget::GdkIsWaylandDisplay() &&
      !gdk_wayland_window_get_wl_surface(originGdkWindow)) {
    NS_WARNING(
        "nsDragSessionSource::InvokeDragSessionImpl(): Missing origin "
        "wl_surface!");
    return NS_ERROR_FAILURE;
  }
#endif

  GtkTargetList* sourceList = GetSourceList();
  if (!sourceList) {
    return NS_OK;
  }

  GdkDragAction action = GDK_ACTION_DEFAULT;

  if (aActionType & nsIDragService::DRAGDROP_ACTION_COPY)
    action = (GdkDragAction)(action | GDK_ACTION_COPY);
  if (aActionType & nsIDragService::DRAGDROP_ACTION_MOVE)
    action = (GdkDragAction)(action | GDK_ACTION_MOVE);
  if (aActionType & nsIDragService::DRAGDROP_ACTION_LINK)
    action = (GdkDragAction)(action | GDK_ACTION_LINK);

  GdkEvent fakeEvent;
  if (!existingEvent) {
    memset(&fakeEvent, 0, sizeof(GdkEvent));
    fakeEvent.type = GDK_BUTTON_PRESS;
    fakeEvent.button.window = gtk_widget_get_window(mHiddenWidget);
    fakeEvent.button.time = nsWindow::GetLastUserInputTime();
    fakeEvent.button.device = device;
  }

  GtkWindowGroup* window_group =
      gtk_window_get_group(GetGtkWindow(mSourceDocument));
  gtk_window_group_add_window(window_group, GTK_WINDOW(mHiddenWidget));

  LOGDRAGSERVICE(
      "nsDragSessionSource::InvokeDragSessionImpl() originGdkWindow [%p]",
      originGdkWindow);

  GdkDragContext* context = gtk_drag_begin_with_coordinates(
      mHiddenWidget, sourceList, action, 1,
      existingEvent ? existingEvent : &fakeEvent, -1, -1);

  if (originGdkWindow) {
    mSourceWindow = nsWindow::GetWindow(originGdkWindow);
    if (mSourceWindow) {
      mSourceWindow->SetDragSource(context);
    }
  }

  LOGDRAGSERVICE("  GdkDragContext [%p] nsWindow [%p]", context,
                 mSourceWindow.get());

  nsresult rv;
  if (context) {
    sGrabWidget = gtk_window_group_get_current_grab(window_group);
    if (sGrabWidget) {
      g_object_ref(sGrabWidget);
      g_signal_connect(sGrabWidget, "event-after",
                       G_CALLBACK(OnSourceGrabEventAfter), this);
    }
    mEndDragPoint = LayoutDeviceIntPoint(-1, -1);
    rv = NS_OK;
  } else {
    rv = NS_ERROR_FAILURE;
  }

  gtk_target_list_unref(sourceList);

  return rv;
}

nsresult nsDragSessionSource::EndDragSessionImpl(bool aDoneDrag,
                                                 uint32_t aKeyModifiers) {
  LOGDRAGSERVICE("nsDragSessionSource::EndDragSessionImpl() %d", aDoneDrag);

  if (sGrabWidget) {
    g_signal_handlers_disconnect_by_func(
        sGrabWidget, FuncToGpointer(OnSourceGrabEventAfter), this);
    g_object_unref(sGrabWidget);
    sGrabWidget = nullptr;

    if (sMotionEventTimerID) {
      g_source_remove(sMotionEventTimerID);
      sMotionEventTimerID = 0;
    }
    if (sMotionEvent) {
      TakeMotionEvent();
    }
  }

  // start timer to remove temporary files
  if (mTemporaryFiles.Count() > 0 && !mTempFileTimerID) {
    LOGDRAGSERVICE("  queue removing of temporary files");
    AddRef();
    mTempFileTimerID =
        g_timeout_add(NS_DND_TMP_CLEANUP_TIMEOUT, TaskRemoveTempFiles, this);
    mTempFileUrls.Clear();
  }

  // We're done with the drag context.
  if (mSourceWindow) {
    mSourceWindow->SetDragSource(nullptr);
    mSourceWindow = nullptr;
  }

  return nsDragSession::EndDragSessionImpl(aDoneDrag, aKeyModifiers);
}

void nsDragSessionSource::EndDragSessionMainThread() {
  EndDragSession(true, nsDragSession::GetCurrentModifiers());
}

bool nsDragSessionSource::RemoveTempFiles() {
  LOGDRAGSERVICE("nsDragSessionSource::RemoveTempFiles");

  auto files = std::move(mTemporaryFiles);
  for (nsIFile* file : files) {
#ifdef MOZ_LOGGING
    if (MOZ_LOG_TEST(gWidgetDragLog, LogLevel::Debug)) {
      nsAutoCString path;
      if (NS_SUCCEEDED(file->GetNativePath(path))) {
        LOGDRAGSERVICE("  removing %s", path.get());
      }
    }
#endif
    file->Remove(/* recursive = */ true);
  }
  MOZ_ASSERT(mTemporaryFiles.IsEmpty());
  mTempFileTimerID = 0;
  return false;
}

/* static */
gboolean nsDragSessionSource::TaskRemoveTempFiles(gpointer data) {
  RefPtr<nsDragSessionSource> session = static_cast<nsDragSessionSource*>(data);
  session.get()->Release();
  return session->RemoveTempFiles();
}

void nsDragSessionSource::SourceEndDragSession(GdkDragContext* aContext,
                                               gint aResult) {
  LOGDRAGSERVICE("SourceEndDragSession(%p) result %s\n", aContext,
                 kGtkDragResults[aResult]);

  // GTK drops its last reference right after drag-end, so reset ours now.
  //
  // Clearing only from EndDragSessionImpl is not enough (bug 2058661): that
  // runs from an async runnable, by which point the pointer already dangles.
  if (mSourceWindow) {
    mSourceWindow->SetDragSource(nullptr);
  }

  mSourceDataItems = nullptr;

  GdkAtom property = sXdndDirectSaveTypeAtom;
  gdk_property_delete(gdk_drag_context_get_source_window(aContext), property);

  if (!mDoingDrag || mDragTaskSourceFinished) return;

  if (mEndDragPoint == LayoutDeviceIntPoint(-1, -1)) {
    // Nothing took the drop, so no drag-drop signal set the end point.
    //
    // The end point is client-relative, while the positions available here are
    // relative to the outer window, whose origin is the decoration corner, so
    // both branches subtract the margin between the two.
    auto margin = mSourceWindow ? mSourceWindow->GetClientOffset()
                                : LayoutDeviceIntPoint();
    if (GdkIsWaylandDisplay() && !mDragIsOverSourceWindow) {
      // On Wayland gdk_window_get_device_position() doesn't handle a pointer
      // outside the window correctly. Only inside versus outside matters
      // here, so report a point outside (bug 2073336).
      SetDragEndPoint(LayoutDeviceIntPoint(-100, -100) - margin);
      LOGDRAGSERVICE("  drag end point outside the window %d %d",
                     (int)mEndDragPoint.x, (int)mEndDragPoint.y);
    } else {
      gint x, y;
      GdkDisplay* display = gdk_display_get_default();
      GdkScreen* screen = gdk_display_get_default_screen(display);
      GtkWindow* window = GetGtkWindow(mSourceDocument);
      GdkWindow* gdkWindow = window ? gtk_widget_get_window(GTK_WIDGET(window))
                                    : gdk_screen_get_root_window(screen);
      if (!gdkWindow) {
        return;
      }
      gdk_window_get_device_position(
          gdkWindow, gdk_drag_context_get_device(aContext), &x, &y, nullptr);
      gint scale = gdk_window_get_scale_factor(gdkWindow);
      auto point = LayoutDeviceIntPoint(x * scale, y * scale);
      if (window) {
        // Skipped when we fell back to the root window above, where the
        // position is relative to the screen and no margin applies.
        point -= margin;
      }
      SetDragEndPoint(point);
      LOGDRAGSERVICE("  guess drag end point %d %d (over source window %d)\n",
                     (int)point.x, (int)point.y, mDragIsOverSourceWindow);
    }
  }

  uint32_t dropEffect;

  if (aResult == GTK_DRAG_RESULT_SUCCESS) {
    LOGDRAGSERVICE("  drop is accepted");
    GdkDragAction action = gdk_drag_context_get_dest_window(aContext)
                               ? gdk_drag_context_get_actions(aContext)
                               : (GdkDragAction)0;

    if (!action) {
      LOGDRAGSERVICE("  drop action is none");
      dropEffect = nsIDragService::DRAGDROP_ACTION_NONE;
    } else if (action & GDK_ACTION_COPY) {
      LOGDRAGSERVICE("  drop action is copy");
      dropEffect = nsIDragService::DRAGDROP_ACTION_COPY;
    } else if (action & GDK_ACTION_LINK) {
      LOGDRAGSERVICE("  drop action is link");
      dropEffect = nsIDragService::DRAGDROP_ACTION_LINK;
    } else if (action & GDK_ACTION_MOVE) {
      LOGDRAGSERVICE("  drop action is move");
      dropEffect = nsIDragService::DRAGDROP_ACTION_MOVE;
    } else {
      LOGDRAGSERVICE("  drop action is copy");
      dropEffect = nsIDragService::DRAGDROP_ACTION_COPY;
    }
  } else {
    LOGDRAGSERVICE("  drop action is none");
    dropEffect = nsIDragService::DRAGDROP_ACTION_NONE;
    if (aResult != GTK_DRAG_RESULT_NO_TARGET) {
      LOGDRAGSERVICE("  drop is user chancelled\n");
      mUserCancelled = true;
    }
  }

  if (mDataTransfer) {
    mDataTransfer->SetDropEffectInt(dropEffect);
  }

  mDragTaskSourceFinished = true;
  NS_DispatchToMainThread(
      NewRunnableMethod("nsDragSessionSource::EndDragSession", this,
                        &nsDragSessionSource::EndDragSessionMainThread));
}

GtkTargetList* nsDragSessionSource::GetSourceList(void) {
  if (!mSourceDataItems) {
    return nullptr;
  }

  nsTArray<GtkTargetEntry*> targetArray;
  GtkTargetEntry* targets;
  GtkTargetList* targetList = nullptr;
  uint32_t targetCount = 0;
  unsigned int numDragItems = 0;

  mSourceDataItems->GetLength(&numDragItems);
  LOGDRAGSERVICE("  numDragItems = %d", numDragItems);

  if (numDragItems > 1) {
    TargetArrayAddTarget(targetArray, gMimeListType);

    nsCOMPtr<nsITransferable> currItem = do_QueryElementAt(mSourceDataItems, 0);

    if (currItem) {
      nsTArray<nsCString> flavors;
      currItem->FlavorsTransferableCanExport(flavors);
      for (uint32_t i = 0; i < flavors.Length(); ++i) {
        if (flavors[i].EqualsLiteral(kURLMime)) {
          TargetArrayAddTarget(targetArray, gTextUriListType);
          break;
        }
      }
    }
  } else if (numDragItems == 1) {
    nsCOMPtr<nsITransferable> currItem = do_QueryElementAt(mSourceDataItems, 0);
    if (currItem) {
      nsTArray<nsCString> flavors;
      currItem->FlavorsTransferableCanExport(flavors);
      for (uint32_t i = 0; i < flavors.Length(); ++i) {
        nsCString& flavorStr = flavors[i];
        GdkAtom requestedFlavor = gdk_atom_intern(flavorStr.get(), FALSE);
        if (!requestedFlavor) {
          continue;
        }

        TargetArrayAddTarget(targetArray, flavorStr.get());

        if (requestedFlavor == sFileMimeAtom) {
          TargetArrayAddTarget(targetArray, gTextUriListType);
        } else if (requestedFlavor == sTextMimeAtom) {
          TargetArrayAddTarget(targetArray, gTextPlainUTF8Type);
          TargetArrayAddTarget(targetArray, gUTF8STRINGType);
          TargetArrayAddTarget(targetArray, gSTRINGType);
        } else if (requestedFlavor == sURLMimeAtom) {
          nsCOMPtr<nsISupports> data;
          if (NS_SUCCEEDED(currItem->GetTransferData(flavorStr.get(),
                                                     getter_AddRefs(data)))) {
            void* tmpData = nullptr;
            uint32_t tmpDataLen = 0;
            nsPrimitiveHelpers::CreateDataFromPrimitive(
                nsDependentCString(flavorStr.get()), data, &tmpData,
                &tmpDataLen);
            if (tmpData) {
              if (CanExportAsURLTarget(reinterpret_cast<char16_t*>(tmpData),
                                       tmpDataLen / 2)) {
                TargetArrayAddTarget(targetArray, gMozUrlType);
              }
              free(tmpData);
            }
          }
        } else if (requestedFlavor == sFilePromiseURLMimeAtom) {
          TargetArrayAddTarget(targetArray, gTextUriListType);
        } else if (widget::GdkIsX11Display() && !widget::IsXWaylandProtocol() &&
                   requestedFlavor == sFilePromiseMimeAtom) {
          TargetArrayAddTarget(targetArray, gXdndDirectSaveType);
        } else if (requestedFlavor == sNativeImageMimeAtom) {
          TargetArrayAddTarget(targetArray, kPNGImageMime);
          TargetArrayAddTarget(targetArray, kJPEGImageMime);
          TargetArrayAddTarget(targetArray, kJPGImageMime);
          TargetArrayAddTarget(targetArray, kGIFImageMime);
        }
      }
    }
  }

  targetCount = targetArray.Length();
  if (targetCount) {
    targets = (GtkTargetEntry*)g_malloc(sizeof(GtkTargetEntry) * targetCount);
    uint32_t targetIndex;
    for (targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
      GtkTargetEntry* disEntry = targetArray.ElementAt(targetIndex);
      targets[targetIndex].target = disEntry->target;
      targets[targetIndex].flags = disEntry->flags;
      targets[targetIndex].info = 0;
    }
    targetList = gtk_target_list_new(targets, targetCount);
    for (uint32_t cleanIndex = 0; cleanIndex < targetCount; ++cleanIndex) {
      GtkTargetEntry* thisTarget = targetArray.ElementAt(cleanIndex);
      g_free(thisTarget->target);
      g_free(thisTarget);
    }
    g_free(targets);
  } else {
    targetList = gtk_target_list_new(nullptr, 0);
  }
  return targetList;
}

nsresult nsDragSessionSource::CreateTempFile(nsITransferable* aItem,
                                             nsACString& aURI) {
  LOGDRAGSERVICE("nsDragSessionSource::CreateTempFile()");

  nsCOMPtr<nsIFile> tmpDir;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(tmpDir));
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  Failed to get temp directory\n");
    return rv;
  }

  nsCOMPtr<nsIInputStream> inputStream;
  nsCOMPtr<nsIChannel> channel;

  nsAutoString wideFileName;
  nsCOMPtr<nsIURI> sourceURI;
  rv = GetDownloadDetails(aItem, getter_AddRefs(sourceURI), wideFileName);
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE(
        "  Failed to extract file name and source uri from download url");
    return rv;
  }

  nsAutoCString fileName;
  CopyUTF16toUTF8(wideFileName, fileName);
  auto fileLen = fileName.Length();
  for (const auto& url : mTempFileUrls) {
    auto URLLen = url.Length();
    if (URLLen > fileLen &&
        fileName.Equals(nsDependentCString(url, URLLen - fileLen))) {
      aURI = url;
      LOGDRAGSERVICE("  recycle file %s", PromiseFlatCString(aURI).get());
      return NS_OK;
    }
  }

  nsCOMPtr<nsIPrincipal> principal = aItem->GetDataPrincipal();
  nsContentPolicyType contentPolicyType = aItem->GetContentPolicyType();
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      aItem->GetCookieJarSettings();
  rv = NS_NewChannel(getter_AddRefs(channel), sourceURI, principal,
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     contentPolicyType, cookieJarSettings);
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  Failed to create new channel for source uri");
    return rv;
  }

  rv = channel->Open(getter_AddRefs(inputStream));
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  Failed to open channel for source uri");
    return rv;
  }

  tmpDir->Append(NS_LITERAL_STRING_FROM_CSTRING("dnd_file"));
  rv = tmpDir->CreateUnique(nsIFile::DIRECTORY_TYPE, 0700);
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  Failed create tmp dir");
    return rv;
  }

  nsCOMPtr<nsIFile> tempFile;
  tmpDir->Clone(getter_AddRefs(tempFile));
  mTemporaryFiles.AppendObject(tempFile);
  MozClearHandleID(mTempFileTimerID, g_source_remove);

  tmpDir->Append(wideFileName);

  nsCOMPtr<nsIOutputStream> outputStream;
  rv = NS_NewLocalFileOutputStream(getter_AddRefs(outputStream), tmpDir);
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  Failed to open output stream for temporary file");
    return rv;
  }

  char buffer[8192];
  uint32_t readCount = 0;
  uint32_t writeCount = 0;
  while (true) {
    rv = inputStream->Read(buffer, sizeof(buffer), &readCount);
    if (NS_FAILED(rv)) {
      LOGDRAGSERVICE("  Failed to read data from source uri");
      return rv;
    }

    if (readCount == 0) break;

    rv = outputStream->Write(buffer, readCount, &writeCount);
    if (NS_FAILED(rv)) {
      LOGDRAGSERVICE("  Failed to write data to temporary file");
      return rv;
    }
  }

  inputStream->Close();
  rv = outputStream->Close();
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  Failed to write data to temporary file");
    return rv;
  }

  nsCOMPtr<nsIURI> uri;
  rv = NS_NewFileURI(getter_AddRefs(uri), tmpDir);
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  Failed to get file URI");
    return rv;
  }
  nsCOMPtr<nsIURL> fileURL(do_QueryInterface(uri));
  if (!fileURL) {
    LOGDRAGSERVICE("  Failed to query file interface");
    return NS_ERROR_FAILURE;
  }
  rv = fileURL->GetSpec(aURI);
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  Failed to get filepath");
    return rv;
  }

  mTempFileUrls.AppendElement()->Assign(aURI);
  LOGDRAGSERVICE("  storing tmp file as %s", PromiseFlatCString(aURI).get());
  return NS_OK;
}

bool nsDragSessionSource::SourceDataAppendURLFileItem(nsACString& aURI,
                                                      nsITransferable* aItem) {
  nsCOMPtr<nsISupports> data;
  nsresult rv = aItem->GetTransferData(kFileMime, getter_AddRefs(data));
  NS_ENSURE_SUCCESS(rv, false);
  if (nsCOMPtr<nsIFile> file = do_QueryInterface(data)) {
    nsCOMPtr<nsIURI> fileURI;
    NS_NewFileURI(getter_AddRefs(fileURI), file);
    if (fileURI) {
      fileURI->GetSpec(aURI);
      return true;
    }
  }
  return false;
}

bool nsDragSessionSource::SourceDataAppendURLItem(nsITransferable* aItem,
                                                  bool aExternalDrop,
                                                  nsACString& aURI) {
  nsCOMPtr<nsISupports> data;
  nsresult rv = aItem->GetTransferData(kURLMime, getter_AddRefs(data));
  if (NS_FAILED(rv)) {
    return SourceDataAppendURLFileItem(aURI, aItem);
  }

  nsCOMPtr<nsISupportsString> string = do_QueryInterface(data);
  if (!string) {
    return false;
  }

  nsAutoString text;
  string->GetData(text);
  if (!aExternalDrop || CanExportAsURLTarget(text.get(), text.Length())) {
    AppendUTF16toUTF8(text, aURI);
    return true;
  }

  if (SourceDataAppendURLFileItem(aURI, aItem)) {
    return true;
  }

  nsCOMPtr<nsISupports> promiseData;
  rv = aItem->GetTransferData(kFilePromiseURLMime, getter_AddRefs(promiseData));
  NS_ENSURE_SUCCESS(rv, false);

  return NS_SUCCEEDED(CreateTempFile(aItem, aURI));
}

void nsDragSessionSource::SourceDataGetUriList(GdkDragContext* aContext,
                                               GtkSelectionData* aSelectionData,
                                               uint32_t aDragItems) {
  const bool isExternalDrop =
      widget::GdkIsX11Display()
          ? !nsWindow::GetWindow(gdk_drag_context_get_dest_window(aContext))
          : !gdk_drag_context_get_dest_window(aContext);

  LOGDRAGSERVICE(
      "nsDragSessionSource::SourceDataGetUriLists() len %d external %d",
      aDragItems, isExternalDrop);

  AutoSuspendNativeEvents suspend;

  nsAutoCString uriList;
  for (uint32_t i = 0; i < aDragItems; i++) {
    nsCOMPtr<nsITransferable> item = do_QueryElementAt(mSourceDataItems, i);
    if (!item) {
      continue;
    }
    nsAutoCString uri;
    if (!SourceDataAppendURLItem(item, isExternalDrop, uri)) {
      continue;
    }
    int32_t separatorPos = uri.FindChar(u'\n');
    if (separatorPos >= 0) {
      uri.Truncate(separatorPos);
    }
    uriList.Append(uri);
    uriList.AppendLiteral("\r\n");
  }

  LOGDRAGSERVICE("URI list\n%s", uriList.get());
  GdkAtom target = gtk_selection_data_get_target(aSelectionData);
  gtk_selection_data_set(aSelectionData, target, 8, (guchar*)uriList.get(),
                         uriList.Length());
}

bool nsDragSessionSource::SourceDataGetImage(nsITransferable* aItem,
                                             GtkSelectionData* aSelectionData) {
  LOGDRAGSERVICE("nsDragSessionSource::SourceDataGetImage()");

  nsresult rv;
  nsCOMPtr<nsISupports> data;
  rv = aItem->GetTransferData(kNativeImageMime, getter_AddRefs(data));
  NS_ENSURE_SUCCESS(rv, false);

  LOGDRAGSERVICE("  posting image\n");
  nsCOMPtr<imgIContainer> image = do_QueryInterface(data);
  if (!image) {
    LOGDRAGSERVICE("  do_QueryInterface failed\n");
    return false;
  }
  RefPtr<GdkPixbuf> pixbuf = nsImageToPixbuf::ImageToPixbuf(image);
  if (!pixbuf) {
    LOGDRAGSERVICE("  ImageToPixbuf failed\n");
    return false;
  }
  gtk_selection_data_set_pixbuf(aSelectionData, pixbuf);
  LOGDRAGSERVICE("  image data set\n");
  return true;
}

bool nsDragSessionSource::SourceDataGetXDND(nsITransferable* aItem,
                                            GdkDragContext* aContext,
                                            GtkSelectionData* aSelectionData) {
  LOGDRAGSERVICE("nsDragSessionSource::SourceDataGetXDND");

  GdkAtom target = gtk_selection_data_get_target(aSelectionData);
  gtk_selection_data_set(aSelectionData, target, 8, (guchar*)"E", 1);

  GdkWindow* srcWindow = gdk_drag_context_get_source_window(aContext);
  if (!srcWindow) {
    LOGDRAGSERVICE("  failed to get source GdkWindow!");
    return false;
  }

  nsAutoCString data;
  {
    GUniquePtr<guchar> gdata;
    gint length = 0;
    if (!gdk_property_get(srcWindow, sXdndDirectSaveTypeAtom, sTextMimeAtom, 0,
                          INT32_MAX, FALSE, nullptr, nullptr, &length,
                          getter_Transfers(gdata))) {
      LOGDRAGSERVICE("  failed to get gXdndDirectSaveType GdkWindow property.");
      return false;
    }
    data.Assign(nsDependentCSubstring((const char*)gdata.get(), length));
  }

  GUniquePtr<char> hostname;
  GUniquePtr<char> fullpath(
      g_filename_from_uri(data.get(), getter_Transfers(hostname), nullptr));
  if (!fullpath) {
    LOGDRAGSERVICE("  failed to get file from uri.");
    return false;
  }

  if (hostname) {
    nsCOMPtr<nsIPropertyBag2> infoService =
        do_GetService(NS_SYSTEMINFO_CONTRACTID);
    if (!infoService) {
      return false;
    }
    nsAutoCString host;
    if (NS_SUCCEEDED(infoService->GetPropertyAsACString(u"host"_ns, host))) {
      if (!host.Equals(hostname.get())) {
        LOGDRAGSERVICE("  ignored drag because of different host.");
        gtk_selection_data_set(aSelectionData, target, 8, (guchar*)"F", 1);
        return true;
      }
    }
  }

  LOGDRAGSERVICE("  XdndDirectSave filepath is %s", fullpath.get());

  nsCOMPtr<nsIFile> file;
  if (NS_FAILED(NS_NewNativeLocalFile(nsDependentCString(fullpath.get()),
                                      getter_AddRefs(file)))) {
    LOGDRAGSERVICE("  failed to get local file");
    return false;
  }

  nsCOMPtr<nsIFile> directory;
  file->GetParent(getter_AddRefs(directory));

  aItem->SetTransferData(kFilePromiseDirectoryMime, directory);

  nsCOMPtr<nsISupportsString> filenamePrimitive =
      do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID);
  if (!filenamePrimitive) {
    return false;
  }

  nsAutoString leafName;
  file->GetLeafName(leafName);
  filenamePrimitive->SetData(leafName);

  aItem->SetTransferData(kFilePromiseDestFilename, filenamePrimitive);

  nsCOMPtr<nsISupports> promiseData;
  nsresult rv =
      aItem->GetTransferData(kFilePromiseMime, getter_AddRefs(promiseData));
  NS_ENSURE_SUCCESS(rv, false);

  gtk_selection_data_set(aSelectionData, target, 8, (guchar*)"S", 1);
  return true;
}

bool nsDragSessionSource::SourceDataGetText(nsITransferable* aItem,
                                            const nsACString& aMIMEType,
                                            bool aNeedToDoConversionToPlainText,
                                            GtkSelectionData* aSelectionData) {
  LOGDRAGSERVICE("nsDragSessionSource::SourceDataGetPlain()");

  nsresult rv;
  nsCOMPtr<nsISupports> data;
  rv = aItem->GetTransferData(PromiseFlatCString(aMIMEType).get(),
                              getter_AddRefs(data));
  NS_ENSURE_SUCCESS(rv, false);

  void* tmpData = nullptr;
  uint32_t tmpDataLen = 0;

  nsPrimitiveHelpers::CreateDataFromPrimitive(aMIMEType, data, &tmpData,
                                              &tmpDataLen);
  if (aNeedToDoConversionToPlainText) {
    char* plainTextData = nullptr;
    char16_t* castedUnicode = reinterpret_cast<char16_t*>(tmpData);
    uint32_t plainTextLen = 0;
    UTF16ToNewUTF8(castedUnicode, tmpDataLen / 2, &plainTextData,
                   &plainTextLen);
    if (tmpData) {
      free(tmpData);
      tmpData = plainTextData;
      tmpDataLen = plainTextLen;
    }
  }
  if (tmpData) {
    GdkAtom target = gtk_selection_data_get_target(aSelectionData);
    gtk_selection_data_set(aSelectionData, target, 8, (guchar*)tmpData,
                           tmpDataLen);
    free(tmpData);
  }

  return true;
}

void nsDragSessionSource::SourceDataGet(GtkWidget* aWidget,
                                        GdkDragContext* aContext,
                                        GtkSelectionData* aSelectionData,
                                        guint32 aTime) {
  GdkAtom requestedFlavor = gtk_selection_data_get_target(aSelectionData);
  LOGDRAGSERVICE("nsDragSessionSource::SourceDataGet(%p) MIME %s", aContext,
                 GUniquePtr<gchar>(gdk_atom_name(requestedFlavor)).get());

  if (!mSourceDataItems) {
    LOGDRAGSERVICE("  Failed to get our data items\n");
    return;
  }

  uint32_t dragItems;
  mSourceDataItems->GetLength(&dragItems);
  LOGDRAGSERVICE("  source data items %d", dragItems);

  if (requestedFlavor == sTextUriListTypeAtom) {
    SourceDataGetUriList(aContext, aSelectionData, dragItems);
    return;
  }

#ifdef MOZ_LOGGING
  if (dragItems > 1) {
    LOGDRAGSERVICE(
        "  There are %d data items but we're asked for %s MIME type. Only "
        "first data element can be transfered!",
        dragItems, GUniquePtr<gchar>(gdk_atom_name(requestedFlavor)).get());
  }
#endif

  nsCOMPtr<nsITransferable> item = do_QueryElementAt(mSourceDataItems, 0);
  if (!item) {
    LOGDRAGSERVICE("  Failed to get SourceDataItems!");
    return;
  }

  if (IsTextFlavor(requestedFlavor)) {
    if (!SourceDataGetText(item, nsDependentCString(kTextMime),
                           /* aNeedToDoConversionToPlainText */ true,
                           aSelectionData)) {
      LOGDRAGSERVICE(
          "  Failed to send sTextMimeAtom/sTextPlainUTF8TypeAtom data!");
    }
    return;
  } else if (requestedFlavor == sXdndDirectSaveTypeAtom) {
    if (!SourceDataGetXDND(item, aContext, aSelectionData)) {
      LOGDRAGSERVICE("  Failed to send sXdndDirectSaveTypeAtom data!");
    }
    return;
  } else if (requestedFlavor == sPNGImageMimeAtom ||
             requestedFlavor == sJPEGImageMimeAtom ||
             requestedFlavor == sJPGImageMimeAtom ||
             requestedFlavor == sGIFImageMimeAtom) {
    if (!SourceDataGetImage(item, aSelectionData)) {
      LOGDRAGSERVICE("  Failed to send image data!");
    }
    return;
  } else if (requestedFlavor == sMozUrlTypeAtom) {
    if (SourceDataGetText(item, nsDependentCString(kURLMime),
                          /* aNeedToDoConversionToPlainText */ true,
                          aSelectionData)) {
      LOGDRAGSERVICE("  Failed to send kURLMime data!");
      return;
    }
  }

  GUniquePtr<gchar> flavorName(gdk_atom_name(requestedFlavor));

  if (nsDependentCString(flavorName.get()).EqualsLiteral(kHTMLMime)) {
    nsCOMPtr<nsISupports> data;
    if (NS_FAILED(item->GetTransferData(kHTMLMime, getter_AddRefs(data))) ||
        !data) {
      LOGDRAGSERVICE("  Failed to get kHTMLMime data!");
      return;
    }
    nsCOMPtr<nsISupportsString> wideString = do_QueryInterface(data);
    if (!wideString) {
      LOGDRAGSERVICE("  kHTMLMime data is not nsISupportsString");
      return;
    }
    nsAutoString ucs2string;
    wideString->GetData(ucs2string);
    nsAutoCString html;
    html.AppendLiteral(mozilla::widget::kHTMLMarkupPrefix);
    AppendUTF16toUTF8(ucs2string, html);
    GdkAtom target = gtk_selection_data_get_target(aSelectionData);
    gtk_selection_data_set(aSelectionData, target, 8, (const guchar*)html.get(),
                           html.Length());
    return;
  }

  if (!SourceDataGetText(item, nsDependentCString(flavorName.get()),
                         /* aNeedToDoConversionToPlainText */ false,
                         aSelectionData)) {
    LOGDRAGSERVICE("  Failed to send %s data!",
                   nsDependentCString(flavorName.get()).get());
  }
}

void nsDragSessionSource::SourceBeginDrag(GdkDragContext* aContext) {
  LOGDRAGSERVICE("nsDragSessionSource::SourceBeginDrag(%p)\n", aContext);

  nsCOMPtr<nsITransferable> transferable =
      do_QueryElementAt(mSourceDataItems, 0);
  if (!transferable) {
    LOGDRAGSERVICE("  missing transferable!");
    return;
  }

  nsTArray<nsCString> flavors;
  nsresult rv = transferable->FlavorsTransferableCanImport(flavors);
  if (NS_FAILED(rv)) {
    LOGDRAGSERVICE("  FlavorsTransferableCanImport failed!");
    return;
  }

  for (uint32_t i = 0; i < flavors.Length(); ++i) {
    if (flavors[i].EqualsLiteral(kFilePromiseDestFilename)) {
      nsCOMPtr<nsISupports> data;
      rv = transferable->GetTransferData(kFilePromiseDestFilename,
                                         getter_AddRefs(data));
      if (NS_FAILED(rv)) {
        LOGDRAGSERVICE("  transferable doesn't contain '%s",
                       kFilePromiseDestFilename);
        return;
      }

      nsCOMPtr<nsISupportsString> fileName = do_QueryInterface(data);
      if (!fileName) {
        LOGDRAGSERVICE("  failed to get file name");
        return;
      }

      nsAutoString fileNameStr;
      fileName->GetData(fileNameStr);

      nsCString fileNameCStr;
      CopyUTF16toUTF8(fileNameStr, fileNameCStr);

      gdk_property_change(
          gdk_drag_context_get_source_window(aContext), sXdndDirectSaveTypeAtom,
          sTextMimeAtom, 8, GDK_PROP_MODE_REPLACE,
          (const guchar*)fileNameCStr.get(), fileNameCStr.Length());
      break;
    }
  }
}

bool nsDragSessionSource::SetAlphaPixmap(SourceSurface* aSurface,
                                         GdkDragContext* aContext,
                                         int32_t aXOffset, int32_t aYOffset,
                                         const LayoutDeviceIntRect& dragRect) {
  GdkScreen* screen = gtk_widget_get_screen(mHiddenWidget);

  if (!gdk_screen_is_composited(screen)) {
    return false;
  }

#ifdef cairo_image_surface_create
#  error "Looks like we're including Mozilla's cairo instead of system cairo"
#endif

  cairo_surface_t* surf = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, dragRect.width, dragRect.height);
  if (!surf) return false;

  RefPtr<DrawTarget> dt = gfxPlatform::CreateDrawTargetForData(
      cairo_image_surface_get_data(surf),
      nsIntSize(dragRect.width, dragRect.height),
      cairo_image_surface_get_stride(surf), SurfaceFormat::B8G8R8A8);
  if (!dt) return false;

  dt->ClearRect(Rect(0, 0, dragRect.width, dragRect.height));
  dt->DrawSurface(
      aSurface, Rect(0, 0, dragRect.width, dragRect.height),
      Rect(0, 0, dragRect.width, dragRect.height), DrawSurfaceOptions(),
      DrawOptions(DRAG_IMAGE_ALPHA_LEVEL, CompositionOp::OP_SOURCE));

  cairo_surface_mark_dirty(surf);
  cairo_surface_set_device_offset(surf, -aXOffset, -aYOffset);

  static auto sCairoSurfaceSetDeviceScalePtr =
      (void (*)(cairo_surface_t*, double, double))dlsym(
          RTLD_DEFAULT, "cairo_surface_set_device_scale");
  if (sCairoSurfaceSetDeviceScalePtr) {
    gint scale = mozilla::widget::ScreenHelperGTK::GetGTKMonitorScaleFactor();
    sCairoSurfaceSetDeviceScalePtr(surf, scale, scale);
  }

  gtk_drag_set_icon_surface(aContext, surf);
  cairo_surface_destroy(surf);
  return true;
}

void nsDragSessionSource::SetDragIcon(GdkDragContext* aContext) {
  if (!mHasImage && !mSelection) return;

  LOGDRAGSERVICE("nsDragSessionSource::SetDragIcon(%p)", aContext);

  LayoutDeviceIntRect dragRect;
  nsPresContext* pc;
  RefPtr<SourceSurface> surface;
  DrawDrag(mSourceNode, mRegion, mScreenPosition, &dragRect, &surface, &pc);
  if (!pc) {
    LOGDRAGSERVICE("  PresContext is missing!");
    return;
  }

  const auto screenPoint =
      LayoutDeviceIntPoint::Round(mScreenPosition * pc->CSSToDevPixelScale());
  int32_t offsetX = screenPoint.x - dragRect.x;
  int32_t offsetY = screenPoint.y - dragRect.y;

  bool gtk_drag_set_icon_widget_is_working =
      gtk_check_version(3, 19, 4) != nullptr ||
      gtk_check_version(3, 24, 0) == nullptr;
  if (mDragPopup && gtk_drag_set_icon_widget_is_working) {
    GtkWidget* gtkWidget = nullptr;
    nsIFrame* frame = mDragPopup->GetPrimaryFrame();
    if (frame) {
      nsCOMPtr<nsIWidget> widget = frame->GetNearestWidget();
      if (widget) {
        gtkWidget = (GtkWidget*)widget->GetNativeData(NS_NATIVE_SHELLWIDGET);
        if (gtkWidget) {
          g_object_ref(gtkWidget);
          if (GtkWidget* parent = gtk_widget_get_parent(gtkWidget)) {
            gtk_container_remove(GTK_CONTAINER(parent), gtkWidget);
          }
          LOGDRAGSERVICE("  set drag popup [%p]", widget.get());
          OpenDragPopup();
          gtk_drag_set_icon_widget(aContext, gtkWidget, offsetX, offsetY);
          g_object_unref(gtkWidget);
          return;
        } else {
          LOGDRAGSERVICE("  NS_NATIVE_SHELLWIDGET is missing!");
        }
      } else {
        LOGDRAGSERVICE("  NearestWidget is missing!");
      }
    } else {
      LOGDRAGSERVICE("  PrimaryFrame is missing!");
    }
  }

  if (surface) {
    LOGDRAGSERVICE("  We have a surface");
    if (!SetAlphaPixmap(surface, aContext, offsetX, offsetY, dragRect)) {
      RefPtr<GdkPixbuf> dragPixbuf = nsImageToPixbuf::SourceSurfaceToPixbuf(
          surface, dragRect.width, dragRect.height);
      if (dragPixbuf) {
        LOGDRAGSERVICE("  set drag pixbuf");
        gtk_drag_set_icon_pixbuf(aContext, dragPixbuf, offsetX, offsetY);
      } else {
        LOGDRAGSERVICE("  SourceSurfaceToPixbuf failed!");
      }
    }
  } else {
    LOGDRAGSERVICE("  Surface is missing!");
  }
}

static void invisibleSourceDragBegin(GtkWidget* aWidget,
                                     GdkDragContext* aContext, gpointer aData) {
  LOGDRAGSERVICESTATIC("invisibleSourceDragBegin (%p)", aContext);
  nsDragSessionSource* dragSession = (nsDragSessionSource*)aData;

  // Keep nsDragSessionSource until D&D is finished.
  dragSession->AddRef();
  dragSession->SourceBeginDrag(aContext);
  dragSession->SetDragIcon(aContext);
}

static void invisibleSourceDragDataGet(GtkWidget* aWidget,
                                       GdkDragContext* aContext,
                                       GtkSelectionData* aSelectionData,
                                       guint aInfo, guint32 aTime,
                                       gpointer aData) {
  LOGDRAGSERVICESTATIC("invisibleSourceDragDataGet (%p)", aContext);
  nsDragSessionSource* dragSession = (nsDragSessionSource*)aData;
  dragSession->SourceDataGet(aWidget, aContext, aSelectionData, aTime);
}

static gboolean invisibleSourceDragFailed(GtkWidget* aWidget,
                                          GdkDragContext* aContext,
                                          gint aResult, gpointer aData) {
  if (widget::GdkIsWaylandDisplay() && aResult == GTK_DRAG_RESULT_ERROR) {
    aResult = GTK_DRAG_RESULT_NO_TARGET;
  }

  LOGDRAGSERVICESTATIC("invisibleSourceDragFailed(%p) %s", aContext,
                       kGtkDragResults[aResult]);
  nsDragSessionSource* dragSession = (nsDragSessionSource*)aData;
  dragSession->SourceEndDragSession(aContext, aResult);

  return FALSE;
}

static void invisibleSourceDragEnd(GtkWidget* aWidget, GdkDragContext* aContext,
                                   gpointer aData) {
  LOGDRAGSERVICESTATIC("invisibleSourceDragEnd(%p)", aContext);
  // Release reference taken at invisibleSourceDragBegin.
  RefPtr dragSession = dont_AddRef(static_cast<nsDragSessionSource*>(aData));
  dragSession->SourceEndDragSession(aContext, GTK_DRAG_RESULT_SUCCESS);
}

#undef LOGDRAGSERVICE
#undef LOGDRAGSERVICESTATIC

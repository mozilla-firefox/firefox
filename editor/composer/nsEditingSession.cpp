/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>  // for nullptr, strcmp

#include "imgIContainer.h"                    // for imgIContainer, etc
#include "mozilla/ComposerCommandsUpdater.h"  // for ComposerCommandsUpdater
#include "mozilla/FlushType.h"                // for FlushType::Frames
#include "mozilla/HTMLEditor.h"               // for HTMLEditor
#include "mozilla/mozalloc.h"                 // for operator new
#include "mozilla/PresShell.h"                // for PresShell
#include "mozilla/Try.h"                      // for MOZ_TRY
#include "nsAString.h"
#include "nsBaseCommandController.h"  // for nsBaseCommandController
#include "nsCommandManager.h"         // for nsCommandManager
#include "nsComponentManagerUtils.h"  // for do_CreateInstance
#include "nsContentUtils.h"
#include "nsDebug.h"     // for NS_ENSURE_SUCCESS, etc
#include "nsDocShell.h"  // for nsDocShell
#include "nsEditingSession.h"
#include "nsError.h"                     // for NS_ERROR_FAILURE, NS_OK, etc
#include "nsIChannel.h"                  // for nsIChannel
#include "nsIDocumentViewer.h"           // for nsIDocumentViewer
#include "nsIControllers.h"              // for nsIControllers
#include "nsID.h"                        // for NS_GET_IID, etc
#include "nsHTMLDocument.h"              // for nsHTMLDocument
#include "nsIDocShell.h"                 // for nsIDocShell
#include "mozilla/dom/Document.h"        // for Document
#include "nsIEditor.h"                   // for nsIEditor
#include "nsIInterfaceRequestorUtils.h"  // for do_GetInterface
#include "nsIRefreshURI.h"               // for nsIRefreshURI
#include "nsIRequest.h"                  // for nsIRequest
#include "nsITimer.h"                    // for nsITimer, etc
#include "nsIWeakReference.h"            // for nsISupportsWeakReference, etc
#include "nsIWebNavigation.h"            // for nsIWebNavigation
#include "nsIWebProgress.h"              // for nsIWebProgress, etc
#include "nsLiteralString.h"             // for NS_LITERAL_STRING
#include "nsPIDOMWindow.h"               // for nsPIDOMWindow
#include "nsPIDOMWindowInlines.h"  // for nsPIDOMWindowOuter::GetDocShell(), etc
#include "nsPresContext.h"         // for nsPresContext
#include "nsReadableUtils.h"       // for AppendUTF16toUTF8
#include "nsStringFwd.h"           // for nsString
#include "mozilla/dom/BrowsingContext.h"  // for BrowsingContext
#include "mozilla/dom/Selection.h"        // for AutoHideSelectionChanges, etc
#include "mozilla/dom/WindowContext.h"    // for WindowContext
#include "nsFrameSelection.h"             // for nsFrameSelection
#include "nsBaseCommandController.h"      // for nsBaseCommandController
#include "mozilla/dom/LoadURIOptionsBinding.h"

class nsISupports;
class nsIURI;

using namespace mozilla;
using namespace mozilla::dom;

/*---------------------------------------------------------------------------

  nsEditingSession

----------------------------------------------------------------------------*/
nsEditingSession::nsEditingSession()
    : mDoneSetup(false),
      mCanCreateEditor(false),
      mInteractive(false),
      mMakeWholeDocumentEditable(true),
      mDisabledJS(false),
      mScriptsEnabled(true),
      mProgressListenerRegistered(false),
      mImageAnimationMode(0),
      mEditorFlags(0),
      mEditorStatus(eEditorOK),
      mBaseCommandControllerId(0),
      mDocStateControllerId(0),
      mHTMLCommandControllerId(0) {}

/*---------------------------------------------------------------------------

  ~nsEditingSession

----------------------------------------------------------------------------*/
nsEditingSession::~nsEditingSession() {
  // Must cancel previous timer?
  if (mLoadBlankDocTimer) mLoadBlankDocTimer->Cancel();
}

NS_IMPL_ISUPPORTS(nsEditingSession, nsIEditingSession, nsIWebProgressListener,
                  nsISupportsWeakReference)

/*---------------------------------------------------------------------------

  MakeWindowEditable

  aEditorType string, "html" "htmlsimple" "text" "textsimple"
  void makeWindowEditable(in nsIDOMWindow aWindow, in string aEditorType,
                          in boolean aDoAfterUriLoad,
                          in boolean aMakeWholeDocumentEditable,
                          in boolean aInteractive);
----------------------------------------------------------------------------*/
#define DEFAULT_EDITOR_TYPE "html"

NS_IMETHODIMP
nsEditingSession::MakeWindowEditable(mozIDOMWindowProxy* aWindow,
                                     const char* aEditorType,
                                     bool aDoAfterUriLoad,
                                     bool aMakeWholeDocumentEditable,
                                     bool aInteractive) {
  mEditorType.Truncate();
  mEditorFlags = 0;

  NS_ENSURE_TRUE(aWindow, NS_ERROR_FAILURE);
  auto* window = nsPIDOMWindowOuter::From(aWindow);

  // disable plugins
  nsCOMPtr<nsIDocShell> docShell = window->GetDocShell();
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);
  mDocShell = do_GetWeakReference(docShell);

  mInteractive = aInteractive;
  mMakeWholeDocumentEditable = aMakeWholeDocumentEditable;

  nsresult rv;
  if (!mInteractive) {
    rv = DisableJS(window->GetCurrentInnerWindow());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Always remove existing editor
  TearDownEditorOnWindow(aWindow);

  // Tells embedder that startup is in progress
  mEditorStatus = eEditorCreationInProgress;

  // temporary to set editor type here. we will need different classes soon.
  if (!aEditorType) aEditorType = DEFAULT_EDITOR_TYPE;
  mEditorType = aEditorType;

  // if all this does is setup listeners and I don't need listeners,
  // can't this step be ignored?? (based on aDoAfterURILoad)
  rv = PrepareForEditing(window);
  NS_ENSURE_SUCCESS(rv, rv);

  // set the flag on the docShell to say that it's editable
  rv = docShell->MakeEditable(aDoAfterUriLoad);
  NS_ENSURE_SUCCESS(rv, rv);

  // Setup commands common to plaintext and html editors,
  //  including the document creation observers
  // the first is an editing controller
  rv = SetupEditorCommandController(
      nsBaseCommandController::CreateEditingController, aWindow, this,
      &mBaseCommandControllerId);
  NS_ENSURE_SUCCESS(rv, rv);

  // The second is a controller to monitor doc state,
  // such as creation and "dirty flag"
  rv = SetupEditorCommandController(
      nsBaseCommandController::CreateHTMLEditorDocStateController, aWindow,
      this, &mDocStateControllerId);
  NS_ENSURE_SUCCESS(rv, rv);

  // aDoAfterUriLoad can be false only when making an existing window editable
  if (!aDoAfterUriLoad) {
    rv = SetupEditorOnWindow(MOZ_KnownLive(*window));

    // mEditorStatus is set to the error reason
    // Since this is used only when editing an existing page,
    //  it IS ok to destroy current editor
    if (NS_FAILED(rv)) {
      TearDownEditorOnWindow(aWindow);
    }
  }
  return rv;
}

nsresult nsEditingSession::DisableJS(nsPIDOMWindowInner* aWindow) {
  WindowContext* wc = aWindow->GetWindowContext();

  mScriptsEnabled = wc->GetAllowJavascript();
  MOZ_TRY(wc->SetAllowJavascript(false));
  mDisabledJS = true;
  return NS_OK;
}

nsresult nsEditingSession::RestoreJS(nsPIDOMWindowInner* aWindow) {
  if (!mDisabledJS) {
    return NS_OK;
  }

  mDisabledJS = false;

  if (NS_WARN_IF(!aWindow)) {
    // DetachFromWindow may call this method with nullptr.
    return NS_ERROR_FAILURE;
  }

  WindowContext* wc = aWindow->GetWindowContext();
  return wc->SetAllowJavascript(mScriptsEnabled);
}

/*---------------------------------------------------------------------------

  WindowIsEditable

  boolean windowIsEditable (in nsIDOMWindow aWindow);
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::WindowIsEditable(mozIDOMWindowProxy* aWindow,
                                   bool* outIsEditable) {
  NS_ENSURE_STATE(aWindow);
  nsCOMPtr<nsIDocShell> docShell =
      nsPIDOMWindowOuter::From(aWindow)->GetDocShell();
  NS_ENSURE_STATE(docShell);

  return docShell->GetEditable(outIsEditable);
}

bool IsSupportedTextType(const nsAString& aMIMEType) {
  // These are MIME types that are automatically parsed as "text/plain"
  //   and thus we can edit them as plaintext
  // Note: in older versions, we attempted to convert the mimetype of
  //   the network channel for these and "text/xml" to "text/plain",
  //   but further investigation reveals that strategy doesn't work
  static constexpr nsLiteralString sSupportedTextTypes[] = {
      u"text/plain"_ns,
      u"text/css"_ns,
      u"text/rdf"_ns,
      u"text/xsl"_ns,
      u"text/javascript"_ns,  // obsolete type
      u"text/ecmascript"_ns,  // obsolete type
      u"application/javascript"_ns,
      u"application/ecmascript"_ns,
      u"application/x-javascript"_ns,  // obsolete type
      u"text/xul"_ns                   // obsolete type
  };

  for (const nsLiteralString& supportedTextType : sSupportedTextTypes) {
    if (aMIMEType.Equals(supportedTextType)) {
      return true;
    }
  }

  return false;
}

nsresult nsEditingSession::SetupEditorOnWindow(nsPIDOMWindowOuter& aWindow) {
  mDoneSetup = true;

  // MIME CHECKING
  // must get the content type
  // Note: the doc gets this from the network channel during StartPageLoad,
  //    so we don't have to get it from there ourselves
  nsAutoString mimeType;

  // then lets check the mime type
  if (RefPtr<Document> doc = aWindow.GetDoc()) {
    doc->GetContentType(mimeType);

    if (IsSupportedTextType(mimeType)) {
      mEditorType.AssignLiteral("text");
      mimeType.AssignLiteral("text/plain");
    } else if (!doc->IsHTMLOrXHTML()) {
      // Neither an acceptable text or html type.
      mEditorStatus = eEditorErrorCantEditMimeType;

      // Turn editor into HTML -- we will load blank page later
      mEditorType.AssignLiteral("html");
      mimeType.AssignLiteral("text/html");
    }

    // Flush out frame construction to make sure that the subframe's
    // presshell is set up if it needs to be.
    doc->FlushPendingNotifications(mozilla::FlushType::Frames);
    if (mMakeWholeDocumentEditable) {
      doc->SetDocumentEditableFlag(true);
      // Enable usage of the execCommand API
      doc->SetEditingState(Document::EditingState::eDesignMode);
    }
  }
  bool needHTMLController = false;

  if (mEditorType.EqualsLiteral("textmail")) {
    mEditorFlags = nsIEditor::eEditorPlaintextMask |
                   nsIEditor::eEditorEnableWrapHackMask |
                   nsIEditor::eEditorMailMask;
  } else if (mEditorType.EqualsLiteral("text")) {
    mEditorFlags =
        nsIEditor::eEditorPlaintextMask | nsIEditor::eEditorEnableWrapHackMask;
  } else if (mEditorType.EqualsLiteral("htmlmail")) {
    if (mimeType.EqualsLiteral("text/html")) {
      needHTMLController = true;
      mEditorFlags = nsIEditor::eEditorMailMask;
    } else {
      // Set the flags back to textplain.
      mEditorFlags = nsIEditor::eEditorPlaintextMask |
                     nsIEditor::eEditorEnableWrapHackMask;
    }
  } else {
    // Defaulted to html
    needHTMLController = true;
  }

  if (mInteractive) {
    mEditorFlags |= nsIEditor::eEditorAllowInteraction;
  }

  // make the UI state maintainer
  RefPtr commandsUpdater = MakeRefPtr<ComposerCommandsUpdater>();
  mComposerCommandsUpdater = commandsUpdater;

  // now init the state maintainer
  // This allows notification of error state
  //  even if we don't create an editor
  commandsUpdater->Init(aWindow);

  if (mEditorStatus != eEditorCreationInProgress) {
    commandsUpdater->OnHTMLEditorCreated();

    // At this point we have made a final decision that we don't support
    // editing the current document.  This is an internal failure state, but
    // we return NS_OK to avoid throwing an exception from the designMode
    // setter for web compatibility.  The document editing APIs will tell the
    // developer if editing has been disabled because we're in a document type
    // that doesn't support editing.
    return NS_OK;
  }

  // Create editor and do other things
  //  only if we haven't found some error above,
  const RefPtr<nsDocShell> docShell = nsDocShell::Cast(aWindow.GetDocShell());
  if (NS_WARN_IF(!docShell)) {
    return NS_ERROR_FAILURE;
  }
  const RefPtr<PresShell> presShell = docShell->GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return NS_ERROR_FAILURE;
  }

  if (!mInteractive) {
    // Disable animation of images in this document:
    nsPresContext* presContext = presShell->GetPresContext();
    NS_ENSURE_TRUE(presContext, NS_ERROR_FAILURE);

    mImageAnimationMode = presContext->ImageAnimationMode();
    presContext->SetImageAnimationMode(imgIContainer::kDontAnimMode);
  }

  // Hide selection changes during initialization, in order to hide this
  // from web pages.
  RefPtr<nsFrameSelection> fs = presShell->FrameSelection();
  NS_ENSURE_TRUE(fs, NS_ERROR_FAILURE);
  AutoHideSelectionChanges hideSelectionChanges(fs);

  nsCOMPtr<nsIDocumentViewer> viewer;
  nsresult rv = docShell->GetDocViewer(getter_AddRefs(viewer));
  if (NS_FAILED(rv) || NS_WARN_IF(!viewer)) {
    NS_WARNING("nsDocShell::GetDocViewer() failed");
    return rv;
  }

  const RefPtr<Document> doc = viewer->GetDocument();
  if (NS_WARN_IF(!doc)) {
    return NS_ERROR_FAILURE;
  }

  // create and set editor
  // Try to reuse an existing editor
  nsCOMPtr<nsIEditor> editor = do_QueryReferent(mExistingEditor);
  RefPtr<HTMLEditor> htmlEditor = HTMLEditor::GetFrom(editor);
  MOZ_ASSERT_IF(editor, htmlEditor);
  if (htmlEditor) {
    htmlEditor->PreDestroy();
  } else {
    htmlEditor = new HTMLEditor(*doc);
    mExistingEditor =
        do_GetWeakReference(static_cast<nsIEditor*>(htmlEditor.get()));
  }
  // set the editor on the docShell. The docShell now owns it.
  rv = docShell->SetHTMLEditor(htmlEditor);
  NS_ENSURE_SUCCESS(rv, rv);

  // setup the HTML editor command controller
  if (needHTMLController) {
    // The third controller takes an nsIEditor as the context
    rv = SetupEditorCommandController(
        nsBaseCommandController::CreateHTMLEditorController, &aWindow,
        htmlEditor, &mHTMLCommandControllerId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Set mimetype on editor
  rv = htmlEditor->SetContentsMIMEType(mimeType);
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ASSERT(docShell->HasDocumentViewer());
  MOZ_ASSERT(viewer->GetDocument());

  MOZ_DIAGNOSTIC_ASSERT(commandsUpdater == mComposerCommandsUpdater);
  if (MOZ_UNLIKELY(commandsUpdater != mComposerCommandsUpdater)) {
    commandsUpdater = mComposerCommandsUpdater;
  }
  rv = htmlEditor->Init(*doc, *commandsUpdater, mEditorFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<Selection> selection = htmlEditor->GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_ERROR_FAILURE;
  }

  // Set context on all controllers to be the editor
  rv = SetEditorOnControllers(aWindow, htmlEditor);
  NS_ENSURE_SUCCESS(rv, rv);

  // Everything went fine!
  mEditorStatus = eEditorOK;

  // This will trigger documentCreation notification
  return htmlEditor->PostCreate();
}

// Removes all listeners and controllers from aWindow and aEditor.
void nsEditingSession::RemoveListenersAndControllers(
    nsPIDOMWindowOuter* aWindow, HTMLEditor* aHTMLEditor) {
  if (!mComposerCommandsUpdater || !aHTMLEditor) {
    return;
  }

  // Remove all the listeners
  RefPtr<ComposerCommandsUpdater> composertCommandsUpdater =
      std::move(mComposerCommandsUpdater);
  MOZ_ASSERT(!mComposerCommandsUpdater);
  aHTMLEditor->Detach(*composertCommandsUpdater);

  // Remove editor controllers from the window now that we're not
  // editing in that window any more.
  RemoveEditorControllers(aWindow);
}

/*---------------------------------------------------------------------------

  TearDownEditorOnWindow

  void tearDownEditorOnWindow (in nsIDOMWindow aWindow);
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::TearDownEditorOnWindow(mozIDOMWindowProxy* aWindow) {
  if (!mDoneSetup) {
    return NS_OK;
  }

  NS_ENSURE_TRUE(aWindow, NS_ERROR_NULL_POINTER);

  // Kill any existing reload timer
  if (mLoadBlankDocTimer) {
    mLoadBlankDocTimer->Cancel();
    mLoadBlankDocTimer = nullptr;
  }

  mDoneSetup = false;

  // Check if we're turning off editing (from contentEditable or designMode).
  auto* window = nsPIDOMWindowOuter::From(aWindow);

  RefPtr<Document> doc = window->GetDoc();
  bool stopEditing = doc && doc->IsEditingOn();
  if (stopEditing) {
    RemoveWebProgressListener(window);
  }

  nsCOMPtr<nsIDocShell> docShell = window->GetDocShell();
  NS_ENSURE_STATE(docShell);

  RefPtr<HTMLEditor> htmlEditor = docShell->GetHTMLEditor();
  if (stopEditing) {
    doc->TearingDownEditor();
  }

  if (mComposerCommandsUpdater && htmlEditor) {
    // Null out the editor on the controllers first to prevent their weak
    // references from pointing to a destroyed editor.
    SetEditorOnControllers(*window, nullptr);
  }

  // Null out the editor on the docShell to trigger PreDestroy which
  // needs to happen before document state listeners are removed below.
  docShell->SetEditor(nullptr);

  RemoveListenersAndControllers(window, htmlEditor);

  if (stopEditing) {
    // Make things the way they were before we started editing.
    RestoreJS(window->GetCurrentInnerWindow());
    RestoreAnimationMode(window);

    if (mMakeWholeDocumentEditable) {
      doc->SetDocumentEditableFlag(false);
      doc->SetEditingState(Document::EditingState::eOff);
    }
  }

  return NS_OK;
}

/*---------------------------------------------------------------------------

  GetEditorForFrame

  nsIEditor getEditorForFrame (in nsIDOMWindow aWindow);
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::GetEditorForWindow(mozIDOMWindowProxy* aWindow,
                                     nsIEditor** outEditor) {
  if (NS_WARN_IF(!aWindow)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<nsIEditor> editor = GetHTMLEditorForWindow(aWindow);
  editor.forget(outEditor);
  return NS_OK;
}

/*---------------------------------------------------------------------------

  OnStateChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnStateChange(nsIWebProgress* aWebProgress,
                                nsIRequest* aRequest, uint32_t aStateFlags,
                                nsresult aStatus) {
#ifdef NOISY_DOC_LOADING
  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (channel) {
    nsAutoCString contentType;
    channel->GetContentType(contentType);
    if (!contentType.IsEmpty()) {
      printf(" ++++++ MIMETYPE = %s\n", contentType.get());
    }
  }
#endif

  //
  // A Request has started...
  //
  if (aStateFlags & nsIWebProgressListener::STATE_START) {
#ifdef NOISY_DOC_LOADING
    {
      nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
      if (channel) {
        nsCOMPtr<nsIURI> uri;
        channel->GetURI(getter_AddRefs(uri));
        if (uri) {
          nsCString spec;
          uri->GetSpec(spec);
          printf(" **** STATE_START: CHANNEL URI=%s, flags=%x\n", spec.get(),
                 aStateFlags);
        }
      } else {
        printf("    STATE_START: NO CHANNEL flags=%x\n", aStateFlags);
      }
    }
#endif
    // Page level notification...
    if (aStateFlags & nsIWebProgressListener::STATE_IS_NETWORK) {
      nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
      StartPageLoad(channel);
#ifdef NOISY_DOC_LOADING
      printf("STATE_START & STATE_IS_NETWORK flags=%x\n", aStateFlags);
#endif
    }

    // Document level notification...
    if (aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT &&
        !(aStateFlags & nsIWebProgressListener::STATE_RESTORING)) {
#ifdef NOISY_DOC_LOADING
      printf("STATE_START & STATE_IS_DOCUMENT flags=%x\n", aStateFlags);
#endif

      bool progressIsForTargetDocument =
          IsProgressForTargetDocument(aWebProgress);

      if (progressIsForTargetDocument) {
        nsCOMPtr<mozIDOMWindowProxy> window;
        aWebProgress->GetDOMWindow(getter_AddRefs(window));

        auto* piWindow = nsPIDOMWindowOuter::From(window);
        RefPtr<Document> doc = piWindow->GetDoc();
        nsHTMLDocument* htmlDoc =
            doc && doc->IsHTMLOrXHTML() ? doc->AsHTMLDocument() : nullptr;
        if (htmlDoc && doc->IsWriting()) {
          nsAutoString designMode;
          htmlDoc->GetDesignMode(designMode);

          if (designMode.EqualsLiteral("on")) {
            // This notification is for data coming in through
            // document.open/write/close(), ignore it.

            return NS_OK;
          }
        }

        mCanCreateEditor = true;
        StartDocumentLoad(aWebProgress, progressIsForTargetDocument);
      }
    }
  }
  //
  // A Request is being processed
  //
  else if (aStateFlags & nsIWebProgressListener::STATE_TRANSFERRING) {
    if (aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT) {
      // document transfer started
    }
  }
  //
  // Got a redirection
  //
  else if (aStateFlags & nsIWebProgressListener::STATE_REDIRECTING) {
    if (aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT) {
      // got a redirect
    }
  }
  //
  // A network or document Request has finished...
  //
  else if (aStateFlags & nsIWebProgressListener::STATE_STOP) {
#ifdef NOISY_DOC_LOADING
    {
      nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
      if (channel) {
        nsCOMPtr<nsIURI> uri;
        channel->GetURI(getter_AddRefs(uri));
        if (uri) {
          nsCString spec;
          uri->GetSpec(spec);
          printf(" **** STATE_STOP: CHANNEL URI=%s, flags=%x\n", spec.get(),
                 aStateFlags);
        }
      } else {
        printf("     STATE_STOP: NO CHANNEL  flags=%x\n", aStateFlags);
      }
    }
#endif

    // Document level notification...
    if (aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT) {
      nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
      EndDocumentLoad(aWebProgress, channel, aStatus,
                      IsProgressForTargetDocument(aWebProgress));
#ifdef NOISY_DOC_LOADING
      printf("STATE_STOP & STATE_IS_DOCUMENT flags=%x\n", aStateFlags);
#endif
    }

    // Page level notification...
    if (aStateFlags & nsIWebProgressListener::STATE_IS_NETWORK) {
      nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
      (void)EndPageLoad(aWebProgress, channel, aStatus);
#ifdef NOISY_DOC_LOADING
      printf("STATE_STOP & STATE_IS_NETWORK flags=%x\n", aStateFlags);
#endif
    }
  }

  return NS_OK;
}

/*---------------------------------------------------------------------------

  OnProgressChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnProgressChange(nsIWebProgress* /*aWebProgress*/,
                                   nsIRequest* /*aRequest*/,
                                   int32_t /*aCurSelfProgress*/,
                                   int32_t /*aMaxSelfProgress*/,
                                   int32_t /*aCurTotalProgress*/,
                                   int32_t /*aMaxTotalProgress*/) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

/*---------------------------------------------------------------------------

  OnLocationChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnLocationChange(nsIWebProgress* aWebProgress,
                                   nsIRequest* /*aRequest*/, nsIURI* aURI,
                                   uint32_t /*aFlags*/) {
  nsCOMPtr<mozIDOMWindowProxy> domWindow;
  nsresult rv = aWebProgress->GetDOMWindow(getter_AddRefs(domWindow));
  NS_ENSURE_SUCCESS(rv, rv);

  auto* piWindow = nsPIDOMWindowOuter::From(domWindow);

  RefPtr<Document> doc = piWindow->GetDoc();
  NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

  doc->SetDocumentURI(aURI);

  // Notify the location-changed observer that
  //  the document URL has changed
  nsIDocShell* docShell = piWindow->GetDocShell();
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  RefPtr<nsCommandManager> commandManager = docShell->GetCommandManager();
  commandManager->CommandStatusChanged("obs_documentLocationChanged");
  return NS_OK;
}

/*---------------------------------------------------------------------------

  OnStatusChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnStatusChange(nsIWebProgress* /*aWebProgress*/,
                                 nsIRequest* /*aRequest*/, nsresult /*aStatus*/,
                                 const char16_t* /*aMessage*/) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

/*---------------------------------------------------------------------------

  OnSecurityChange

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnSecurityChange(nsIWebProgress* /*aWebProgress*/,
                                   nsIRequest* /*aRequest*/,
                                   uint32_t /*aState*/) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

/*---------------------------------------------------------------------------

  OnContentBlockingEvent

----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::OnContentBlockingEvent(nsIWebProgress* /*aWebProgress*/,
                                         nsIRequest* /*aRequest*/,
                                         uint32_t /*aEvent*/) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

/*---------------------------------------------------------------------------

  IsProgressForTargetDocument

  Check that this notification is for our document.
----------------------------------------------------------------------------*/

bool nsEditingSession::IsProgressForTargetDocument(
    nsIWebProgress* aWebProgress) {
  nsCOMPtr<nsIWebProgress> editedWebProgress = do_QueryReferent(mDocShell);
  return editedWebProgress == aWebProgress;
}

/*---------------------------------------------------------------------------

  GetEditorStatus

  Called during GetCommandStateParams("obs_documentCreated"...)
  to determine if editor was created and document
  was loaded successfully
----------------------------------------------------------------------------*/
NS_IMETHODIMP
nsEditingSession::GetEditorStatus(uint32_t* aStatus) {
  NS_ENSURE_ARG_POINTER(aStatus);
  *aStatus = mEditorStatus;
  return NS_OK;
}

/*---------------------------------------------------------------------------

  StartDocumentLoad

  Called on start of load in a single frame
----------------------------------------------------------------------------*/
nsresult nsEditingSession::StartDocumentLoad(nsIWebProgress* aWebProgress,
                                             bool aIsToBeMadeEditable) {
#ifdef NOISY_DOC_LOADING
  printf("======= StartDocumentLoad ========\n");
#endif

  NS_ENSURE_ARG_POINTER(aWebProgress);

  if (aIsToBeMadeEditable) {
    mEditorStatus = eEditorCreationInProgress;
  }

  return NS_OK;
}

/*---------------------------------------------------------------------------

  EndDocumentLoad

  Called on end of load in a single frame
----------------------------------------------------------------------------*/
nsresult nsEditingSession::EndDocumentLoad(
    nsIWebProgress* aWebProgress, [[maybe_unused]] nsIChannel* aChannel,
    nsresult aStatus, bool aIsToBeMadeEditable) {
  NS_ENSURE_ARG_POINTER(aWebProgress);

#ifdef NOISY_DOC_LOADING
  printf("======= EndDocumentLoad ========\n");
  printf("with status %d, ", aStatus);
  nsCOMPtr<nsIURI> uri;
  nsCString spec;
  if (NS_SUCCEEDED(aChannel->GetURI(getter_AddRefs(uri)))) {
    uri->GetSpec(spec);
    printf(" uri %s\n", spec.get());
  }
#endif

  // We want to call the base class EndDocumentLoad,
  // but avoid some of the stuff
  // that nsDocShell does (need to refactor).

  // OK, time to make an editor on this document
  nsCOMPtr<mozIDOMWindowProxy> domWindow;
  aWebProgress->GetDOMWindow(getter_AddRefs(domWindow));
  NS_ENSURE_TRUE(domWindow, NS_ERROR_FAILURE);

  // Set the error state -- we will create an editor
  // anyway and load empty doc later
  if (aIsToBeMadeEditable && aStatus == NS_ERROR_FILE_NOT_FOUND) {
    mEditorStatus = eEditorErrorFileNotFound;
  }

  auto* window = nsPIDOMWindowOuter::From(domWindow);
  nsIDocShell* docShell = window->GetDocShell();
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);  // better error handling?

  // cancel refresh from meta tags
  // we need to make sure that all pages in editor (whether editable or not)
  // can't refresh contents being edited
  nsCOMPtr<nsIRefreshURI> refreshURI = do_QueryInterface(docShell);
  if (refreshURI) {
    refreshURI->CancelRefreshURITimers();
  }

  nsresult rv = NS_OK;

  // did someone set the flag to make this shell editable?
  if (aIsToBeMadeEditable && mCanCreateEditor) {
    bool makeEditable;
    docShell->GetEditable(&makeEditable);

    if (makeEditable) {
      // To keep pre Gecko 1.9 behavior, setup editor always when
      // mMakeWholeDocumentEditable.
      bool needsSetup = false;
      if (mMakeWholeDocumentEditable) {
        needsSetup = true;
      } else {
        // do we already have an editor here?
        needsSetup = !docShell->GetHTMLEditor();
      }

      if (needsSetup) {
        mCanCreateEditor = false;
        rv = SetupEditorOnWindow(MOZ_KnownLive(*window));
        if (NS_FAILED(rv)) {
          // If we had an error, setup timer to load a blank page later
          if (mLoadBlankDocTimer) {
            // Must cancel previous timer?
            mLoadBlankDocTimer->Cancel();
            mLoadBlankDocTimer = nullptr;
          }

          rv = NS_NewTimerWithFuncCallback(
              getter_AddRefs(mLoadBlankDocTimer),
              nsEditingSession::TimerCallback,
              static_cast<void*>(mDocShell.get()), 10, nsITimer::TYPE_ONE_SHOT,
              "nsEditingSession::EndDocumentLoad"_ns);
          NS_ENSURE_SUCCESS(rv, rv);

          mEditorStatus = eEditorCreationInProgress;
        }
      }
    }
  }
  return rv;
}

void nsEditingSession::TimerCallback(nsITimer* /*aTimer*/, void* aClosure) {
  nsCOMPtr<nsIDocShell> docShell =
      do_QueryReferent(static_cast<nsIWeakReference*>(aClosure));
  if (docShell) {
    nsCOMPtr<nsIWebNavigation> webNav(do_QueryInterface(docShell));
    if (webNav) {
      LoadURIOptions loadURIOptions;
      loadURIOptions.mTriggeringPrincipal =
          nsContentUtils::GetSystemPrincipal();
      nsCOMPtr<nsIURI> uri;
      MOZ_ALWAYS_SUCCEEDS(NS_NewURI(getter_AddRefs(uri), "about:blank"_ns));
      webNav->LoadURI(uri, loadURIOptions);
    }
  }
}

/*---------------------------------------------------------------------------

  StartPageLoad

  Called on start load of the entire page (incl. subframes)
----------------------------------------------------------------------------*/
nsresult nsEditingSession::StartPageLoad(nsIChannel* /*aChannel*/) {
#ifdef NOISY_DOC_LOADING
  printf("======= StartPageLoad ========\n");
#endif
  return NS_OK;
}

/*---------------------------------------------------------------------------

  EndPageLoad

  Called on end load of the entire page (incl. subframes)
----------------------------------------------------------------------------*/
nsresult nsEditingSession::EndPageLoad(nsIWebProgress* aWebProgress,
                                       [[maybe_unused]] nsIChannel* aChannel,
                                       nsresult aStatus) {
#ifdef NOISY_DOC_LOADING
  printf("======= EndPageLoad ========\n");
  printf("  with status %d, ", aStatus);
  nsCOMPtr<nsIURI> uri;
  nsCString spec;
  if (NS_SUCCEEDED(aChannel->GetURI(getter_AddRefs(uri)))) {
    uri->GetSpec(spec);
    printf("uri %s\n", spec.get());
  }

  nsAutoCString contentType;
  aChannel->GetContentType(contentType);
  if (!contentType.IsEmpty()) {
    printf("   flags = %d, status = %d, MIMETYPE = %s\n", mEditorFlags,
           mEditorStatus, contentType.get());
  }
#endif

  // Set the error state -- we will create an editor anyway
  // and load empty doc later
  if (aStatus == NS_ERROR_FILE_NOT_FOUND) {
    mEditorStatus = eEditorErrorFileNotFound;
  }

  nsCOMPtr<mozIDOMWindowProxy> domWindow;
  aWebProgress->GetDOMWindow(getter_AddRefs(domWindow));

  nsIDocShell* docShell =
      domWindow ? nsPIDOMWindowOuter::From(domWindow)->GetDocShell() : nullptr;
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  // cancel refresh from meta tags
  // we need to make sure that all pages in editor (whether editable or not)
  // can't refresh contents being edited
  nsCOMPtr<nsIRefreshURI> refreshURI = do_QueryInterface(docShell);
  if (refreshURI) {
    refreshURI->CancelRefreshURITimers();
  }

#if 0
  // Shouldn't we do this when we want to edit sub-frames?
  return MakeWindowEditable(domWindow, "html", false, mInteractive);
#else
  return NS_OK;
#endif
}

/*---------------------------------------------------------------------------

  PrepareForEditing

  Set up this editing session for one or more editors
----------------------------------------------------------------------------*/
nsresult nsEditingSession::PrepareForEditing(nsPIDOMWindowOuter* aWindow) {
  if (mProgressListenerRegistered) {
    return NS_OK;
  }

  nsIDocShell* docShell = aWindow ? aWindow->GetDocShell() : nullptr;

  // register callback
  nsCOMPtr<nsIWebProgress> webProgress = do_GetInterface(docShell);
  NS_ENSURE_TRUE(webProgress, NS_ERROR_FAILURE);

  nsresult rv = webProgress->AddProgressListener(
      this, (nsIWebProgress::NOTIFY_STATE_NETWORK |
             nsIWebProgress::NOTIFY_STATE_DOCUMENT |
             nsIWebProgress::NOTIFY_LOCATION));

  mProgressListenerRegistered = NS_SUCCEEDED(rv);

  return rv;
}

/*---------------------------------------------------------------------------

  SetupEditorCommandController

  Create a command controller, append to controllers,
  get and return the controller ID, and set the context
----------------------------------------------------------------------------*/
nsresult nsEditingSession::SetupEditorCommandController(
    nsEditingSession::ControllerCreatorFn aControllerCreatorFn,
    mozIDOMWindowProxy* aWindow, nsISupportsWeakReference* aContext,
    uint32_t* aControllerId) {
  NS_ENSURE_ARG_POINTER(aControllerCreatorFn);
  NS_ENSURE_ARG_POINTER(aWindow);
  NS_ENSURE_ARG_POINTER(aContext);
  NS_ENSURE_ARG_POINTER(aControllerId);

  auto* piWindow = nsPIDOMWindowOuter::From(aWindow);
  MOZ_ASSERT(piWindow);

  nsCOMPtr<nsIControllers> controllers;
  nsresult rv = piWindow->GetControllers(getter_AddRefs(controllers));
  NS_ENSURE_SUCCESS(rv, rv);

  // We only have to create each singleton controller once
  // We know this has happened once we have a controllerId value
  if (!*aControllerId) {
    RefPtr<nsBaseCommandController> commandController = aControllerCreatorFn();
    NS_ENSURE_TRUE(commandController, NS_ERROR_FAILURE);

    // We must insert at head of the list to be sure our
    //   controller is found before other implementations
    //   (e.g., not-implemented versions by browser)
    rv = controllers->InsertControllerAt(0, commandController);
    NS_ENSURE_SUCCESS(rv, rv);

    // Remember the ID for the controller
    rv = controllers->GetControllerId(commandController, aControllerId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Set the context
  return SetContextOnControllerById(controllers, aContext, *aControllerId);
}

nsresult nsEditingSession::SetEditorOnControllers(nsPIDOMWindowOuter& aWindow,
                                                  HTMLEditor* aEditor) {
  nsCOMPtr<nsIControllers> controllers;
  nsresult rv = aWindow.GetControllers(getter_AddRefs(controllers));
  NS_ENSURE_SUCCESS(rv, rv);

  if (mBaseCommandControllerId) {
    rv = SetContextOnControllerById(controllers, aEditor,
                                    mBaseCommandControllerId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mDocStateControllerId) {
    rv =
        SetContextOnControllerById(controllers, aEditor, mDocStateControllerId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mHTMLCommandControllerId) {
    rv = SetContextOnControllerById(controllers, aEditor,
                                    mHTMLCommandControllerId);
  }

  return rv;
}

nsresult nsEditingSession::SetContextOnControllerById(
    nsIControllers* aControllers, nsISupportsWeakReference* aContext,
    uint32_t aID) {
  NS_ENSURE_ARG_POINTER(aControllers);

  // aContext can be null (when destroying editor)
  nsCOMPtr<nsIController> controller;
  aControllers->GetControllerById(aID, getter_AddRefs(controller));

  // ok with nil controller
  nsCOMPtr<nsBaseCommandController> editorController =
      do_QueryInterface(controller);
  NS_ENSURE_TRUE(editorController, NS_ERROR_FAILURE);

  editorController->SetContext(aContext);
  return NS_OK;
}

void nsEditingSession::RemoveEditorControllers(nsPIDOMWindowOuter* aWindow) {
  // Remove editor controllers from the aWindow, call when we're
  // tearing down/detaching editor.

  nsCOMPtr<nsIControllers> controllers;
  if (aWindow) {
    aWindow->GetControllers(getter_AddRefs(controllers));
  }

  if (controllers) {
    nsCOMPtr<nsIController> controller;
    if (mBaseCommandControllerId) {
      controllers->GetControllerById(mBaseCommandControllerId,
                                     getter_AddRefs(controller));
      if (controller) {
        controllers->RemoveController(controller);
      }
    }

    if (mDocStateControllerId) {
      controllers->GetControllerById(mDocStateControllerId,
                                     getter_AddRefs(controller));
      if (controller) {
        controllers->RemoveController(controller);
      }
    }

    if (mHTMLCommandControllerId) {
      controllers->GetControllerById(mHTMLCommandControllerId,
                                     getter_AddRefs(controller));
      if (controller) {
        controllers->RemoveController(controller);
      }
    }
  }

  // Clear IDs to trigger creation of new controllers.
  mBaseCommandControllerId = 0;
  mDocStateControllerId = 0;
  mHTMLCommandControllerId = 0;
}

void nsEditingSession::RemoveWebProgressListener(nsPIDOMWindowOuter* aWindow) {
  nsIDocShell* docShell = aWindow ? aWindow->GetDocShell() : nullptr;
  nsCOMPtr<nsIWebProgress> webProgress = do_GetInterface(docShell);
  if (webProgress) {
    webProgress->RemoveProgressListener(this);
    mProgressListenerRegistered = false;
  }
}

void nsEditingSession::RestoreAnimationMode(nsPIDOMWindowOuter* aWindow) {
  if (mInteractive) {
    return;
  }

  nsCOMPtr<nsIDocShell> docShell = aWindow ? aWindow->GetDocShell() : nullptr;
  NS_ENSURE_TRUE_VOID(docShell);
  RefPtr<PresShell> presShell = docShell->GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return;
  }
  nsPresContext* presContext = presShell->GetPresContext();
  NS_ENSURE_TRUE_VOID(presContext);

  presContext->SetImageAnimationMode(mImageAnimationMode);
}

nsresult nsEditingSession::DetachFromWindow(nsPIDOMWindowOuter* aWindow) {
  NS_ENSURE_TRUE(mDoneSetup, NS_OK);

  NS_ASSERTION(mComposerCommandsUpdater,
               "mComposerCommandsUpdater should exist.");

  // Kill any existing reload timer
  if (mLoadBlankDocTimer) {
    mLoadBlankDocTimer->Cancel();
    mLoadBlankDocTimer = nullptr;
  }

  // Remove controllers, webprogress listener, and otherwise
  // make things the way they were before we started editing.
  RemoveEditorControllers(aWindow);
  RemoveWebProgressListener(aWindow);
  RestoreJS(aWindow->GetCurrentInnerWindow());
  RestoreAnimationMode(aWindow);

  // Kill our weak reference to our original window, in case
  // it changes on restore, or otherwise dies.
  mDocShell = nullptr;

  return NS_OK;
}

HTMLEditor* nsIEditingSession::GetHTMLEditorForWindow(
    mozIDOMWindowProxy* aWindow) {
  if (NS_WARN_IF(!aWindow)) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShell> docShell =
      nsPIDOMWindowOuter::From(aWindow)->GetDocShell();
  if (NS_WARN_IF(!docShell)) {
    return nullptr;
  }

  return docShell->GetHTMLEditor();
}

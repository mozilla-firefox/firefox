/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsFileControlFrame.h"

#include "MiddleCroppingBlockFrame.h"
#include "gfxContext.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextEditor.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/DOMStringList.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/Directory.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DragEvent.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/FileList.h"
#include "mozilla/dom/HTMLButtonElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/UnionTypes.h"
#include "nsCOMPtr.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsIFile.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsNodeInfoManager.h"
#include "nsTextFrame.h"
#include "nsTextNode.h"

using namespace mozilla;
using namespace mozilla::dom;

nsIFrame* NS_NewFileControlFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsFileControlFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsFileControlFrame)

nsFileControlFrame::nsFileControlFrame(ComputedStyle* aStyle,
                                       nsPresContext* aPresContext)
    : nsBlockFrame(aStyle, aPresContext, kClassID) {}

void nsFileControlFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                              nsIFrame* aPrevInFlow) {
  nsBlockFrame::Init(aContent, aParent, aPrevInFlow);

  mMouseListener = new DnDListener(this);
}

void nsFileControlFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aReflowOutput,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  nsBlockFrame::Reflow(aPresContext, aReflowOutput, aReflowInput, aStatus);

  // Form control frame should be monolithic, and cannot be split, so our reflow
  // status should be fully-complete.
  aStatus.Reset();
}

void nsFileControlFrame::Destroy(DestroyContext& aContext) {
  NS_ENSURE_TRUE_VOID(mContent);

  // Remove the events.
  if (mContent) {
    mContent->RemoveSystemEventListener(u"drop"_ns, mMouseListener, false);
    mContent->RemoveSystemEventListener(u"dragover"_ns, mMouseListener, false);
  }

  aContext.AddAnonymousContent(mTextContent.forget());
  aContext.AddAnonymousContent(mBrowseFilesOrDirs.forget());

  mMouseListener->ForgetFrame();
  nsBlockFrame::Destroy(aContext);
}

static already_AddRefed<Element> MakeAnonButton(
    Document* aDoc, const char* labelKey, HTMLInputElement* aInputElement) {
  RefPtr<Element> button = aDoc->CreateHTMLElement(nsGkAtoms::button);
  // NOTE: SetIsNativeAnonymousRoot() has to be called before setting any
  // attribute.
  button->SetIsNativeAnonymousRoot();
  button->SetPseudoElementType(PseudoStyleType::FileSelectorButton);

  // Set the file picking button text depending on the current locale.
  nsAutoString buttonTxt;
  nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                          labelKey, aDoc, buttonTxt);

  auto* nim = aDoc->NodeInfoManager();
  // Set the browse button text. It's a bit of a pain to do because we want to
  // make sure we are not notifying.
  RefPtr textContent = new (nim) nsTextNode(nim);
  textContent->SetText(buttonTxt, false);

  IgnoredErrorResult error;
  button->AppendChildTo(textContent, false, error);
  if (error.Failed()) {
    return nullptr;
  }

  auto* buttonElement = HTMLButtonElement::FromNode(button);
  // We allow tabbing over the input itself, not the button.
  buttonElement->SetTabIndex(-1, IgnoreErrors());
  return button.forget();
}

nsresult nsFileControlFrame::CreateAnonymousContent(
    nsTArray<ContentInfo>& aElements) {
  nsCOMPtr<Document> doc = mContent->GetComposedDoc();
  RefPtr fileContent = HTMLInputElement::FromNode(mContent);

  mBrowseFilesOrDirs = MakeAnonButton(doc, "Browse", fileContent);
  if (!mBrowseFilesOrDirs) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier, or change the return type to void.
  aElements.AppendElement(mBrowseFilesOrDirs);

  // Create and setup the text showing the selected files.
  mTextContent = doc->CreateHTMLElement(nsGkAtoms::label);
  // NOTE: SetIsNativeAnonymousRoot() has to be called before setting any
  // attribute.
  mTextContent->SetIsNativeAnonymousRoot();
  mTextContent->SetPseudoElementType(PseudoStyleType::MozFileContent);
  RefPtr<nsTextNode> text = doc->CreateEmptyTextNode();
  mTextContent->AppendChildTo(text, false, IgnoreErrors());

  aElements.AppendElement(mTextContent);

  // We should be able to interact with the element by doing drag and drop.
  mContent->AddSystemEventListener(u"drop"_ns, mMouseListener, false, false);
  mContent->AddSystemEventListener(u"dragover"_ns, mMouseListener, false,
                                   false);

  SyncDisabledState();

  return NS_OK;
}

void nsFileControlFrame::AppendAnonymousContentTo(
    nsTArray<nsIContent*>& aElements, uint32_t aFilter) {
  if (mBrowseFilesOrDirs) {
    aElements.AppendElement(mBrowseFilesOrDirs);
  }

  if (mTextContent) {
    aElements.AppendElement(mTextContent);
  }
}

NS_QUERYFRAME_HEAD(nsFileControlFrame)
  NS_QUERYFRAME_ENTRY(nsFileControlFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
NS_QUERYFRAME_TAIL_INHERITING(nsBlockFrame)

static void AppendBlobImplAsDirectory(nsTArray<OwningFileOrDirectory>& aArray,
                                      BlobImpl* aBlobImpl,
                                      nsIContent* aContent) {
  MOZ_ASSERT(aBlobImpl);
  MOZ_ASSERT(aBlobImpl->IsDirectory());

  nsAutoString fullpath;
  ErrorResult err;
  aBlobImpl->GetMozFullPath(fullpath, SystemCallerGuarantee(), err);
  if (err.Failed()) {
    err.SuppressException();
    return;
  }

  nsCOMPtr<nsIFile> file;
  nsresult rv = NS_NewLocalFile(fullpath, getter_AddRefs(file));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  nsPIDOMWindowInner* inner = aContent->OwnerDoc()->GetInnerWindow();
  if (!inner || !inner->IsCurrentInnerWindow()) {
    return;
  }

  RefPtr<Directory> directory = Directory::Create(inner->AsGlobal(), file);
  MOZ_ASSERT(directory);

  OwningFileOrDirectory* element = aArray.AppendElement();
  element->SetAsDirectory() = directory;
}

/**
 * This is called when we receive a drop or a dragover.
 */
NS_IMETHODIMP
nsFileControlFrame::DnDListener::HandleEvent(Event* aEvent) {
  NS_ASSERTION(mFrame, "We should have been unregistered");

  if (aEvent->DefaultPrevented()) {
    return NS_OK;
  }

  DragEvent* dragEvent = aEvent->AsDragEvent();
  if (!dragEvent) {
    return NS_OK;
  }

  RefPtr<DataTransfer> dataTransfer = dragEvent->GetDataTransfer();
  if (!IsValidDropData(dataTransfer)) {
    return NS_OK;
  }

  RefPtr<HTMLInputElement> inputElement =
      HTMLInputElement::FromNode(mFrame->GetContent());
  bool supportsMultiple = inputElement->HasAttr(nsGkAtoms::multiple);
  if (!CanDropTheseFiles(dataTransfer, supportsMultiple)) {
    dataTransfer->SetDropEffect(u"none"_ns);
    aEvent->StopPropagation();
    return NS_OK;
  }

  nsAutoString eventType;
  aEvent->GetType(eventType);
  if (eventType.EqualsLiteral("dragover")) {
    // Prevent default if we can accept this drag data
    aEvent->PreventDefault();
    return NS_OK;
  }

  if (eventType.EqualsLiteral("drop")) {
    aEvent->StopPropagation();
    aEvent->PreventDefault();

    RefPtr<FileList> fileList =
        dataTransfer->GetFiles(*nsContentUtils::GetSystemPrincipal());

    RefPtr<BlobImpl> webkitDir;
    nsresult rv =
        GetBlobImplForWebkitDirectory(fileList, getter_AddRefs(webkitDir));
    NS_ENSURE_SUCCESS(rv, NS_OK);

    nsTArray<OwningFileOrDirectory> array;
    if (webkitDir) {
      AppendBlobImplAsDirectory(array, webkitDir, inputElement);
      inputElement->MozSetDndFilesAndDirectories(array);
    } else {
      bool blinkFileSystemEnabled =
          StaticPrefs::dom_webkitBlink_filesystem_enabled();
      if (blinkFileSystemEnabled) {
        FileList* files = static_cast<FileList*>(fileList.get());
        if (files) {
          for (uint32_t i = 0; i < files->Length(); ++i) {
            File* file = files->Item(i);
            if (file) {
              if (file->Impl() && file->Impl()->IsDirectory()) {
                AppendBlobImplAsDirectory(array, file->Impl(), inputElement);
              } else {
                OwningFileOrDirectory* element = array.AppendElement();
                element->SetAsFile() = file;
              }
            }
          }
        }
      }

      // Entries API.
      if (blinkFileSystemEnabled) {
        // This is rather ugly. Pass the directories as Files using SetFiles,
        // but then if blink filesystem API is enabled, it wants
        // FileOrDirectory array.
        inputElement->SetFiles(fileList, true);
        inputElement->UpdateEntries(array);
      }
      // Normal DnD
      else {
        inputElement->SetFiles(fileList, true);
      }

      RefPtr<TextEditor> textEditor;
      DebugOnly<nsresult> rvIgnored =
          nsContentUtils::DispatchInputEvent(inputElement);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "Failed to dispatch input event");
      nsContentUtils::DispatchTrustedEvent(inputElement->OwnerDoc(),
                                           inputElement, u"change"_ns,
                                           CanBubble::eYes, Cancelable::eNo);
    }
  }

  return NS_OK;
}

nsresult nsFileControlFrame::DnDListener::GetBlobImplForWebkitDirectory(
    FileList* aFileList, BlobImpl** aBlobImpl) {
  *aBlobImpl = nullptr;

  HTMLInputElement* inputElement =
      HTMLInputElement::FromNode(mFrame->GetContent());
  bool webkitDirPicker = StaticPrefs::dom_webkitBlink_dirPicker_enabled() &&
                         inputElement->HasAttr(nsGkAtoms::webkitdirectory);
  if (!webkitDirPicker) {
    return NS_OK;
  }

  if (!aFileList) {
    return NS_ERROR_FAILURE;
  }

  // webkitdirectory doesn't care about the length of the file list but
  // only about the first item on it.
  uint32_t len = aFileList->Length();
  if (len) {
    File* file = aFileList->Item(0);
    if (file) {
      BlobImpl* impl = file->Impl();
      if (impl && impl->IsDirectory()) {
        RefPtr<BlobImpl> retVal = impl;
        retVal.swap(*aBlobImpl);
        return NS_OK;
      }
    }
  }

  return NS_ERROR_FAILURE;
}

bool nsFileControlFrame::DnDListener::IsValidDropData(
    DataTransfer* aDataTransfer) {
  if (!aDataTransfer) {
    return false;
  }

  // We only support dropping files onto a file upload control
  return aDataTransfer->HasFile();
}

bool nsFileControlFrame::DnDListener::CanDropTheseFiles(
    DataTransfer* aDataTransfer, bool aSupportsMultiple) {
  RefPtr<FileList> fileList =
      aDataTransfer->GetFiles(*nsContentUtils::GetSystemPrincipal());

  RefPtr<BlobImpl> webkitDir;
  nsresult rv =
      GetBlobImplForWebkitDirectory(fileList, getter_AddRefs(webkitDir));
  // Just check if either there isn't webkitdirectory attribute, or
  // fileList has a directory which can be dropped to the element.
  // No need to use webkitDir for anything here.
  NS_ENSURE_SUCCESS(rv, false);

  uint32_t listLength = 0;
  if (fileList) {
    listLength = fileList->Length();
  }
  return listLength <= 1 || aSupportsMultiple;
}

void nsFileControlFrame::SyncDisabledState() {
  if (mContent->AsElement()->State().HasState(ElementState::DISABLED)) {
    mBrowseFilesOrDirs->SetAttr(kNameSpaceID_None, nsGkAtoms::disabled, u""_ns,
                                true);
  } else {
    mBrowseFilesOrDirs->UnsetAttr(kNameSpaceID_None, nsGkAtoms::disabled, true);
  }
}

void nsFileControlFrame::ElementStateChanged(ElementState aStates) {
  if (aStates.HasState(ElementState::DISABLED)) {
    nsContentUtils::AddScriptRunner(
        MakeAndAddRef<SyncDisabledStateEvent>(this));
  }
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsFileControlFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"FileControl"_ns, aResult);
}
#endif

void nsFileControlFrame::SelectedFilesUpdated() {
  if (MiddleCroppingBlockFrame* f =
          do_QueryFrame(mTextContent->GetPrimaryFrame())) {
    f->UpdateDisplayedValueToUncroppedValue(true);
  }
}

#ifdef ACCESSIBILITY
a11y::AccType nsFileControlFrame::AccessibleType() {
  return a11y::eHTMLFileInputType;
}
#endif

NS_IMPL_ISUPPORTS(nsFileControlFrame::MouseListener, nsIDOMEventListener)

class FileControlLabelFrame final : public MiddleCroppingBlockFrame {
 public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(FileControlLabelFrame)

  FileControlLabelFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : MiddleCroppingBlockFrame(aStyle, aPresContext, kClassID) {}

  HTMLInputElement& FileInput() const {
    return *HTMLInputElement::FromNode(mContent->GetParent());
  }

  void GetUncroppedValue(nsAString& aValue) override {
    return FileInput().GetDisplayFileName(aValue);
  }
};

NS_QUERYFRAME_HEAD(FileControlLabelFrame)
  NS_QUERYFRAME_ENTRY(FileControlLabelFrame)
NS_QUERYFRAME_TAIL_INHERITING(MiddleCroppingBlockFrame)
NS_IMPL_FRAMEARENA_HELPERS(FileControlLabelFrame)

nsIFrame* NS_NewFileControlLabelFrame(PresShell* aPresShell,
                                      ComputedStyle* aStyle) {
  return new FileControlLabelFrame(aStyle, aPresShell->GetPresContext());
}

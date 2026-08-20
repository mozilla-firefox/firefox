/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLInputElement.h"

#include <algorithm>
#include <cmath>

#include "HTMLDataListElement.h"
#include "HTMLFormSubmissionConstants.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/Maybe.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresState.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ServoCSSParser.h"
#include "mozilla/ServoComputedData.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_signon.h"
#include "mozilla/TextControlState.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TextUtils.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/Try.h"
#include "mozilla/dom/AutocompleteInfoBinding.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/CustomEvent.h"
#include "mozilla/dom/Directory.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentOrShadowRoot.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/FileSystemUtils.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/GetFilesHelper.h"
#include "mozilla/dom/HTMLDataListElement.h"
#include "mozilla/dom/HTMLOptionElement.h"
#include "mozilla/dom/InputType.h"
#include "mozilla/dom/MouseEvent.h"
#include "mozilla/dom/NumericInputTypes.h"
#include "mozilla/dom/ProgressEvent.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/WheelEventBinding.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/glean/DomMetrics.h"
#include "nsAttrValueInlines.h"
#include "nsAttrValueOrString.h"
#include "nsBaseCommandController.h"
#include "nsCRTGlue.h"
#include "nsColorControlFrame.h"
#include "nsError.h"
#include "nsFileControlFrame.h"
#include "nsFocusManager.h"
#include "nsGkAtoms.h"
#include "nsIEditor.h"
#include "nsIFilePicker.h"
#include "nsIFormControl.h"
#include "nsIFormFillController.h"
#include "nsIFrame.h"
#include "nsIMutationObserver.h"
#include "nsIPromptCollection.h"
#include "nsIStringBundle.h"
#include "nsLayoutUtils.h"
#include "nsLinebreakConverter.h"  //to strip out carriage returns
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPresContext.h"
#include "nsQueryObject.h"
#include "nsRangeFrame.h"
#include "nsReadableUtils.h"
#include "nsRepeatService.h"
#include "nsStyleConsts.h"
#include "nsTextControlFrame.h"
#include "nsUnicharUtils.h"
#include "nsVariant.h"

// input type=radio
#include "mozilla/dom/RadioGroupContainer.h"

// input type=file
#include "mozilla/dom/File.h"
#include "mozilla/dom/FileList.h"
#include "mozilla/dom/FileSystem.h"
#include "mozilla/dom/FileSystemEntry.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIContentPrefService2.h"
#include "nsIFile.h"
#include "nsIMIMEService.h"
#include "nsIObserverService.h"

// input type=image

#include "HTMLSplitOnSpacesTokenizer.h"
#include "imgRequestProxy.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/DirectionalityUtils.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsFrameSelection.h"
#include "nsIColorPicker.h"
#include "nsIMIMEInfo.h"
#include "nsIStringEnumerator.h"
#include "nsImageLoadingContent.h"
#include "nsXULControllers.h"

// input type=date
#include "js/Date.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(Input)

// XXX align=left, hspace, vspace, border? other nav4 attrs

namespace mozilla::dom {

// First bits are needed for the control type.
#define NS_OUTER_ACTIVATE_EVENT (1 << 9)
#define NS_ORIGINAL_CHECKED_VALUE (1 << 10)
// (1 << 11 is unused)
#define NS_ORIGINAL_INDETERMINATE_VALUE (1 << 12)
#define NS_PRE_HANDLE_BLUR_EVENT (1 << 13)
#define NS_IN_SUBMIT_CLICK (1 << 15)
#define NS_CONTROL_TYPE(bits)                                              \
  ((bits) & ~(NS_OUTER_ACTIVATE_EVENT | NS_ORIGINAL_CHECKED_VALUE |        \
              NS_ORIGINAL_INDETERMINATE_VALUE | NS_PRE_HANDLE_BLUR_EVENT | \
              NS_IN_SUBMIT_CLICK))

UploadLastDir* HTMLInputElement::gUploadLastDir;

static constexpr nsAttrValue::EnumTableEntry kInputTypeTable[] = {
    {"button", FormControlType::InputButton},
    {"checkbox", FormControlType::InputCheckbox},
    {"color", FormControlType::InputColor},
    {"date", FormControlType::InputDate},
    {"datetime-local", FormControlType::InputDatetimeLocal},
    {"email", FormControlType::InputEmail},
    {"file", FormControlType::InputFile},
    {"hidden", FormControlType::InputHidden},
    {"reset", FormControlType::InputReset},
    {"image", FormControlType::InputImage},
    {"month", FormControlType::InputMonth},
    {"number", FormControlType::InputNumber},
    {"password", FormControlType::InputPassword},
    {"radio", FormControlType::InputRadio},
    {"range", FormControlType::InputRange},
    {"search", FormControlType::InputSearch},
    {"submit", FormControlType::InputSubmit},
    {"tel", FormControlType::InputTel},
    {"time", FormControlType::InputTime},
    {"url", FormControlType::InputUrl},
    {"week", FormControlType::InputWeek},
    // "text" must be last for ParseAttribute to work right.  If you add things
    // before it, please update kInputDefaultType.
    {"text", FormControlType::InputText},
};

// Default type is 'text'.
static constexpr const nsAttrValue::EnumTableEntry* kInputDefaultType =
    &kInputTypeTable[std::size(kInputTypeTable) - 1];

static constexpr nsAttrValue::EnumTableEntry kCaptureTable[] = {
    {"user", nsIFilePicker::captureUser},
    {"environment", nsIFilePicker::captureEnv},
    {"", nsIFilePicker::captureDefault},
};

static constexpr const nsAttrValue::EnumTableEntry* kCaptureDefault =
    &kCaptureTable[2];

static constexpr nsAttrValue::EnumTableEntry kColorSpaceTable[] = {
    {"limited-srgb", StyleColorSpace::Srgb},
    {"display-p3", StyleColorSpace::DisplayP3},
};

static constexpr const nsAttrValue::EnumTableEntry* kColorSpaceDefault =
    &kColorSpaceTable[0];

using namespace blink;

constexpr Decimal HTMLInputElement::kStepScaleFactorDate(86400000_d);
constexpr Decimal HTMLInputElement::kStepScaleFactorNumberRange(1_d);
constexpr Decimal HTMLInputElement::kStepScaleFactorTime(1000_d);
constexpr Decimal HTMLInputElement::kStepScaleFactorMonth(1_d);
constexpr Decimal HTMLInputElement::kStepScaleFactorWeek(7 * 86400000_d);
constexpr Decimal HTMLInputElement::kDefaultStepBase(0_d);
constexpr Decimal HTMLInputElement::kDefaultStepBaseWeek(-259200000_d);
constexpr Decimal HTMLInputElement::kDefaultStep(1_d);
constexpr Decimal HTMLInputElement::kDefaultStepTime(60_d);
constexpr Decimal HTMLInputElement::kStepAny(0_d);

const double HTMLInputElement::kMinimumYear = 1;
const double HTMLInputElement::kMaximumYear = 275760;
const double HTMLInputElement::kMaximumWeekInMaximumYear = 37;
const double HTMLInputElement::kMaximumDayInMaximumYear = 13;
const double HTMLInputElement::kMaximumMonthInMaximumYear = 9;
const double HTMLInputElement::kMaximumWeekInYear = 53;
const double HTMLInputElement::kMsPerDay = 24 * 60 * 60 * 1000;

// An helper class for the dispatching of the 'change' event.
// This class is used when the FilePicker finished its task (or when files and
// directories are set by some chrome/test only method).
// The task of this class is to postpone the dispatching of 'change' and 'input'
// events at the end of the exploration of the directories.
class DispatchChangeEventCallback final : public GetFilesCallback {
 public:
  explicit DispatchChangeEventCallback(HTMLInputElement* aInputElement)
      : mInputElement(aInputElement) {
    MOZ_ASSERT(aInputElement);
  }

  void Callback(nsresult aStatus,
                const FallibleTArray<RefPtr<BlobImpl>>& aBlobImpls) override {
    nsCOMPtr<nsIGlobalObject> global = mInputElement->GetRelevantGlobal();
    if (!global) {
      return;
    }

    nsTArray<OwningFileOrDirectory> array;
    for (uint32_t i = 0; i < aBlobImpls.Length(); ++i) {
      OwningFileOrDirectory* element = array.AppendElement();
      RefPtr<File> file =
          File::Create(mInputElement->GetRelevantGlobal(), aBlobImpls[i]);
      if (NS_WARN_IF(!file)) {
        return;
      }

      element->SetAsFile() = file;
    }

    mInputElement->SetFilesOrDirectories(array, true);
    (void)NS_WARN_IF(NS_FAILED(DispatchEvents()));
  }

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult DispatchEvents() {
    RefPtr<HTMLInputElement> inputElement(mInputElement);
    nsresult rv = nsContentUtils::DispatchInputEvent(inputElement);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to dispatch input event");
    mInputElement->SetUserInteracted(true);
    rv = nsContentUtils::DispatchTrustedEvent(mInputElement->OwnerDoc(),
                                              mInputElement, u"change"_ns,
                                              CanBubble::eYes, Cancelable::eNo);

    return rv;
  }

 private:
  RefPtr<HTMLInputElement> mInputElement;
};

struct HTMLInputElement::FileData {
  /**
   * The value of the input if it is a file input. This is the list of files or
   * directories DOM objects used when uploading a file. It is vital that this
   * is kept separate from mValue so that it won't be possible to 'leak' the
   * value from a text-input to a file-input. Additionally, the logic for this
   * value is kept as simple as possible to avoid accidental errors where the
   * wrong filename is used.  Therefor the list of filenames is always owned by
   * this member, never by the frame. Whenever the frame wants to change the
   * filename it has to call SetFilesOrDirectories to update this member.
   */
  nsTArray<OwningFileOrDirectory> mFilesOrDirectories;

  RefPtr<GetFilesHelper> mGetFilesRecursiveHelper;
  RefPtr<GetFilesHelper> mGetFilesNonRecursiveHelper;

  /**
   * Hack for bug 1086684: Stash the .value when we're a file picker.
   */
  nsString mFirstFilePath;

  RefPtr<FileList> mFileList;
  Sequence<RefPtr<FileSystemEntry>> mEntries;

  nsString mStaticDocFileList;

  void ClearGetFilesHelpers() {
    if (mGetFilesRecursiveHelper) {
      mGetFilesRecursiveHelper->Unlink();
      mGetFilesRecursiveHelper = nullptr;
    }

    if (mGetFilesNonRecursiveHelper) {
      mGetFilesNonRecursiveHelper->Unlink();
      mGetFilesNonRecursiveHelper = nullptr;
    }
  }

  // Cycle Collection support.
  void Traverse(nsCycleCollectionTraversalCallback& cb) {
    FileData* tmp = this;
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFilesOrDirectories)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFileList)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEntries)
    if (mGetFilesRecursiveHelper) {
      mGetFilesRecursiveHelper->Traverse(cb);
    }

    if (mGetFilesNonRecursiveHelper) {
      mGetFilesNonRecursiveHelper->Traverse(cb);
    }
  }

  void Unlink() {
    FileData* tmp = this;
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mFilesOrDirectories)
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mFileList)
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mEntries)
    ClearGetFilesHelpers();
  }
};

HTMLInputElement::nsFilePickerShownCallback::nsFilePickerShownCallback(
    HTMLInputElement* aInput, nsIFilePicker* aFilePicker)
    : mFilePicker(aFilePicker), mInput(aInput) {}

NS_IMPL_ISUPPORTS(UploadLastDir::ContentPrefCallback, nsIContentPrefCallback2)

NS_IMETHODIMP
UploadLastDir::ContentPrefCallback::HandleCompletion(uint16_t aReason) {
  nsCOMPtr<nsIFile> localFile;
  nsAutoString prefStr;

  if (aReason == nsIContentPrefCallback2::COMPLETE_ERROR || !mResult) {
    Preferences::GetString("dom.input.fallbackUploadDir", prefStr);
  }

  if (prefStr.IsEmpty() && mResult) {
    nsCOMPtr<nsIVariant> pref;
    mResult->GetValue(getter_AddRefs(pref));
    pref->GetAsAString(prefStr);
  }

  if (!prefStr.IsEmpty()) {
    nsresult rv = NS_NewLocalFile(prefStr, getter_AddRefs(localFile));
    (void)NS_WARN_IF(NS_FAILED(rv));
  }

  if (localFile) {
    mFilePicker->SetDisplayDirectory(localFile);
  } else {
    // If no custom directory was set through the pref, default to
    // "desktop" directory for each platform.
    mFilePicker->SetDisplaySpecialDirectory(
        NS_LITERAL_STRING_FROM_CSTRING(NS_OS_DESKTOP_DIR));
  }

  mFilePicker->Open(mFpCallback);
  return NS_OK;
}

NS_IMETHODIMP
UploadLastDir::ContentPrefCallback::HandleResult(nsIContentPref* pref) {
  mResult = pref;
  return NS_OK;
}

NS_IMETHODIMP
UploadLastDir::ContentPrefCallback::HandleError(nsresult error) {
  // HandleCompletion is always called (even with HandleError was called),
  // so we don't need to do anything special here.
  return NS_OK;
}

namespace {

/**
 * This may return nullptr if the DOM File's implementation of
 * File::mozFullPathInternal does not successfully return a non-empty
 * string that is a valid path. This can happen on Firefox OS, for example,
 * where the file picker can create Blobs.
 */
static already_AddRefed<nsIFile> LastUsedDirectory(
    const OwningFileOrDirectory& aData) {
  if (aData.IsFile()) {
    nsAutoString path;
    ErrorResult error;
    aData.GetAsFile()->GetMozFullPathInternal(path, error);
    if (error.Failed() || path.IsEmpty()) {
      error.SuppressException();
      return nullptr;
    }

    nsCOMPtr<nsIFile> localFile;
    nsresult rv = NS_NewLocalFile(path, getter_AddRefs(localFile));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }

    nsCOMPtr<nsIFile> parentFile;
    rv = localFile->GetParent(getter_AddRefs(parentFile));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }

    return parentFile.forget();
  }

  MOZ_ASSERT(aData.IsDirectory());

  nsCOMPtr<nsIFile> localFile = aData.GetAsDirectory()->GetInternalNsIFile();
  MOZ_ASSERT(localFile);

  return localFile.forget();
}

void GetDOMFileOrDirectoryName(const OwningFileOrDirectory& aData,
                               nsAString& aName) {
  if (aData.IsFile()) {
    aData.GetAsFile()->GetName(aName);
  } else {
    MOZ_ASSERT(aData.IsDirectory());
    ErrorResult rv;
    aData.GetAsDirectory()->GetName(aName, rv);
    if (NS_WARN_IF(rv.Failed())) {
      rv.SuppressException();
    }
  }
}

void GetDOMFileOrDirectoryPath(const OwningFileOrDirectory& aData,
                               nsAString& aPath, ErrorResult& aRv) {
  if (aData.IsFile()) {
    aData.GetAsFile()->GetMozFullPathInternal(aPath, aRv);
  } else {
    MOZ_ASSERT(aData.IsDirectory());
    aData.GetAsDirectory()->GetFullRealPath(aPath);
  }
}

}  // namespace

NS_IMETHODIMP
HTMLInputElement::nsFilePickerShownCallback::Done(
    nsIFilePicker::ResultCode aResult) {
  mInput->PickerClosed();

  if (aResult == nsIFilePicker::returnCancel) {
    RefPtr<HTMLInputElement> inputElement(mInput);
    return nsContentUtils::DispatchTrustedEvent(
        inputElement->OwnerDoc(), inputElement, u"cancel"_ns, CanBubble::eYes,
        Cancelable::eNo);
  }

  mInput->OwnerDoc()->NotifyUserGestureActivation();

  nsIFilePicker::Mode mode;
  mFilePicker->GetMode(&mode);

  // Collect new selected filenames
  nsTArray<OwningFileOrDirectory> newFilesOrDirectories;
  if (mode == nsIFilePicker::modeOpenMultiple) {
    nsCOMPtr<nsISimpleEnumerator> iter;
    nsresult rv =
        mFilePicker->GetDomFileOrDirectoryEnumerator(getter_AddRefs(iter));
    NS_ENSURE_SUCCESS(rv, rv);

    if (!iter) {
      return NS_OK;
    }

    nsCOMPtr<nsISupports> tmp;
    bool hasMore = true;

    while (NS_SUCCEEDED(iter->HasMoreElements(&hasMore)) && hasMore) {
      iter->GetNext(getter_AddRefs(tmp));
      RefPtr<Blob> domBlob = do_QueryObject(tmp);
      MOZ_ASSERT(domBlob,
                 "Null file object from FilePicker's file enumerator?");
      if (!domBlob) {
        continue;
      }

      OwningFileOrDirectory* element = newFilesOrDirectories.AppendElement();
      element->SetAsFile() = domBlob->ToFile();
    }
  } else {
    MOZ_ASSERT(mode == nsIFilePicker::modeOpen ||
               mode == nsIFilePicker::modeGetFolder);
    nsCOMPtr<nsISupports> tmp;
    nsresult rv = mFilePicker->GetDomFileOrDirectory(getter_AddRefs(tmp));
    NS_ENSURE_SUCCESS(rv, rv);

    if (!tmp) {
      return NS_OK;
    }

    // Show a prompt to get user confirmation before allowing folder access.
    // This is to prevent sites from tricking the user into uploading files.
    // See Bug 1338637.
    if (mode == nsIFilePicker::modeGetFolder) {
      nsCOMPtr<nsIPromptCollection> prompter =
          do_GetService("@mozilla.org/embedcomp/prompt-collection;1");
      if (!prompter) {
        return NS_ERROR_NOT_AVAILABLE;
      }

      bool confirmed = false;
      BrowsingContext* bc = mInput->OwnerDoc()->GetBrowsingContext();

      // Get directory name
      RefPtr<Directory> directory = static_cast<Directory*>(tmp.get());
      nsAutoString directoryName;
      ErrorResult error;
      directory->GetName(directoryName, error);
      if (NS_WARN_IF(error.Failed())) {
        return error.StealNSResult();
      }

      rv = prompter->ConfirmFolderUpload(bc, directoryName, &confirmed);
      NS_ENSURE_SUCCESS(rv, rv);
      if (!confirmed) {
        // User aborted upload
        return NS_OK;
      }
    }

    RefPtr<Blob> blob = do_QueryObject(tmp);
    if (blob) {
      RefPtr<File> file = blob->ToFile();
      MOZ_ASSERT(file);

      OwningFileOrDirectory* element = newFilesOrDirectories.AppendElement();
      element->SetAsFile() = file;
    } else if (tmp) {
      RefPtr<Directory> directory = static_cast<Directory*>(tmp.get());
      OwningFileOrDirectory* element = newFilesOrDirectories.AppendElement();
      element->SetAsDirectory() = directory;
    }
  }

  if (newFilesOrDirectories.IsEmpty()) {
    return NS_OK;
  }

  // Store the last used directory using the content pref service:
  nsCOMPtr<nsIFile> lastUsedDir = LastUsedDirectory(newFilesOrDirectories[0]);

  if (lastUsedDir) {
    HTMLInputElement::gUploadLastDir->StoreLastUsedDirectory(mInput->OwnerDoc(),
                                                             lastUsedDir);
  }

  // The text control frame (if there is one) isn't going to send a change
  // event because it will think this is done by a script.
  // So, we can safely send one by ourself.
  mInput->SetFilesOrDirectories(newFilesOrDirectories, true);

  // mInput(HTMLInputElement) has no scriptGlobalObject, don't create
  // DispatchChangeEventCallback
  if (!mInput->GetRelevantGlobal()) {
    return NS_OK;
  }
  RefPtr<DispatchChangeEventCallback> dispatchChangeEventCallback =
      new DispatchChangeEventCallback(mInput);

  if (StaticPrefs::dom_webkitBlink_dirPicker_enabled() &&
      mInput->HasAttr(nsGkAtoms::webkitdirectory)) {
#ifdef MOZ_WIDGET_ANDROID
    // Android 13 or later cannot enumerate files into user directory due to
    // no permission. So we store file list into file picker.
    FallibleTArray<RefPtr<BlobImpl>> filesInWebKitDirectory;

    nsCOMPtr<nsISimpleEnumerator> iter;
    if (NS_SUCCEEDED(
            mFilePicker->GetDomFilesInWebKitDirectory(getter_AddRefs(iter))) &&
        iter) {
      nsCOMPtr<nsISupports> supports;

      bool loop = true;
      while (NS_SUCCEEDED(iter->HasMoreElements(&loop)) && loop) {
        iter->GetNext(getter_AddRefs(supports));
        if (supports) {
          RefPtr<BlobImpl> file = static_cast<File*>(supports.get())->Impl();
          MOZ_ASSERT(file);
          if (!filesInWebKitDirectory.AppendElement(file, fallible)) {
            return nsresult::NS_ERROR_OUT_OF_MEMORY;
          }
        }
      }
    }

    if (!filesInWebKitDirectory.IsEmpty()) {
      dispatchChangeEventCallback->Callback(NS_OK, filesInWebKitDirectory);
      return NS_OK;
    }
#endif

    ErrorResult error;
    GetFilesHelper* helper = mInput->GetOrCreateGetFilesHelper(true, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }

    helper->AddCallback(dispatchChangeEventCallback);
    return NS_OK;
  }

  return dispatchChangeEventCallback->DispatchEvents();
}

NS_IMPL_ISUPPORTS(HTMLInputElement::nsFilePickerShownCallback,
                  nsIFilePickerShownCallback)

class nsColorPickerShownCallback final : public nsIColorPickerShownCallback {
  ~nsColorPickerShownCallback() = default;

 public:
  nsColorPickerShownCallback(HTMLInputElement* aInput,
                             nsIColorPicker* aColorPicker)
      : mInput(aInput), mColorPicker(aColorPicker), mValueChanged(false) {}

  NS_DECL_ISUPPORTS

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Update(const nsAString& aColor) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Done(const nsAString& aColor) override;

 private:
  /**
   * Updates the internals of the object using aColor as the new value.
   * If aTrustedUpdate is true, it will consider that aColor is a new value.
   * Otherwise, it will check that aColor is different from the current value.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult UpdateInternal(const nsAString& aColor, bool aTrustedUpdate);

  RefPtr<HTMLInputElement> mInput;
  nsCOMPtr<nsIColorPicker> mColorPicker;
  bool mValueChanged;
};

nsresult nsColorPickerShownCallback::UpdateInternal(const nsAString& aColor,
                                                    bool aTrustedUpdate) {
  bool valueChanged = false;
  nsAutoString oldValue;
  if (aTrustedUpdate) {
    mInput->OwnerDoc()->NotifyUserGestureActivation();
    valueChanged = true;
  } else {
    mInput->GetValue(oldValue, CallerType::System);
  }

  mInput->SetValue(aColor, CallerType::System, IgnoreErrors());

  if (!aTrustedUpdate) {
    nsAutoString newValue;
    mInput->GetValue(newValue, CallerType::System);
    if (!oldValue.Equals(newValue)) {
      valueChanged = true;
    }
  }

  if (!valueChanged) {
    return NS_OK;
  }

  mValueChanged = true;
  RefPtr<HTMLInputElement> input(mInput);
  DebugOnly<nsresult> rvIgnored = nsContentUtils::DispatchInputEvent(input);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "Failed to dispatch input event");
  return NS_OK;
}

NS_IMETHODIMP
nsColorPickerShownCallback::Update(const nsAString& aColor) {
  return UpdateInternal(aColor, true);
}

NS_IMETHODIMP
nsColorPickerShownCallback::Done(const nsAString& aColor) {
  /**
   * When Done() is called, we might be at the end of a serie of Update() calls
   * in which case mValueChanged is set to true and a change event will have to
   * be fired but we might also be in a one shot Done() call situation in which
   * case we should fire a change event iif the value actually changed.
   * UpdateInternal(bool) is taking care of that logic for us.
   */
  nsresult rv = NS_OK;

  mInput->PickerClosed();

  if (!aColor.IsEmpty()) {
    UpdateInternal(aColor, false);
  }

  if (mValueChanged) {
    mInput->SetUserInteracted(true);
    rv = nsContentUtils::DispatchTrustedEvent(
        mInput->OwnerDoc(), static_cast<Element*>(mInput.get()), u"change"_ns,
        CanBubble::eYes, Cancelable::eNo);
  }

  return rv;
}

NS_IMPL_ISUPPORTS(nsColorPickerShownCallback, nsIColorPickerShownCallback)

static bool IsPickerBlocked(Document* aDoc) {
  if (aDoc->ConsumeTransientUserGestureActivation()) {
    return false;
  }

  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns, aDoc,
                                  PropertiesFile::DOM_PROPERTIES,
                                  "InputPickerBlockedNoUserActivation");
  return true;
}

/**
 * Parse a CSS color string and convert it to the target colorspace if it
 * succeeds.
 * Step 2 of:
 * https://html.spec.whatwg.org/#update-a-color-well-control-color
 *
 * We have this function separately for datalist implementation to filter out
 * invalid values.
 *
 * @param aValue the string to be parsed
 * @return the parsed result as a HTML compatible form
 */
static Maybe<StyleAbsoluteColor> MaybeComputeColor(Document* aDocument,
                                                   const nsAString& aValue) {
  // Step 2: Let color be the result of parsing element's value.
  return ServoCSSParser::ComputeAbsoluteColor(
      aDocument->EnsureStyleSet().RawData(), NS_ConvertUTF16toUTF8(aValue));
}

/**
 * MaybeComputeColor + Step 3 of:
 * https://html.spec.whatwg.org/#update-a-color-well-control-color
 */
static StyleAbsoluteColor MaybeComputeColorOrBlack(Document* aDocument,
                                                   const nsAString& aValue) {
  return MaybeComputeColor(aDocument, aValue)
      // Step 3: If color is failure, then set color to opaque black.
      .valueOr(StyleAbsoluteColor::BLACK);
}

/**
 * https://html.spec.whatwg.org/#serialize-a-color-well-control-color
 * https://drafts.csswg.org/css-color/#color-serialization-html-compatible-serialization-is-requested
 *
 * @param aColor The parsed color
 * @param aResult The result in the form of #ffffff.
 */
static void SerializeColorForHTMLCompatibility(const StyleAbsoluteColor& aColor,
                                               nsAString& aResult) {
  // Raw StyleAbsoluteColor can have floats outside of 0-1 range e.g. when
  // display-p3 color is converted to srgb, and ToColor guarantees to fit the
  // values within the range.
  nscolor color = aColor.ToColor();
  aResult.Truncate();
  aResult.AppendPrintf("#%02x%02x%02x", NS_GET_R(color), NS_GET_G(color),
                       NS_GET_B(color));
}

static void ClampColorComponents(StyleAbsoluteColor& aColor) {
  MOZ_ASSERT(aColor.color_space == StyleColorSpace::Srgb);
  aColor.components._0 = std::clamp(aColor.components._0, 0.0f, 1.0f);
  aColor.components._1 = std::clamp(aColor.components._1, 0.0f, 1.0f);
  aColor.components._2 = std::clamp(aColor.components._2, 0.0f, 1.0f);
}

// https://html.spec.whatwg.org/#serialize-a-color-well-control-color
static void SerializeColor(const StyleAbsoluteColor& aColor,
                           StyleColorSpace aTargetColorSpace,
                           bool aSpecifiedAlpha, nsAString& aResult) {
  // Step 2: Let htmlCompatible be false.
  bool htmlCompatible = false;

  // Step 3: If element's alpha attribute is not specified, then set color's
  // alpha component to be fully opaque.
  // (Setting colorspace here as it's easier.)
  StyleAbsoluteColor color = aColor.ToColorSpace(aTargetColorSpace);
  if (!aSpecifiedAlpha) {
    color.alpha = 1.0;
  }

  // Step 4: If element's colorspace attribute is in the Limited sRGB state:
  if (color.color_space == StyleColorSpace::Srgb) {
    // Step 4.2: Round each of color's components so they are in the range 0 to
    // 255, inclusive. Components are to be rounded towards +∞.
    ClampColorComponents(color);

    if (!aSpecifiedAlpha) {
      // Step 4.3: If element's alpha attribute is not specified, then set
      // htmlCompatible to true.
      htmlCompatible = true;
    } else {
      // Step 4.4: Otherwise, set color to color converted using the 'color()'
      // function.
      // (Unset the legacy bit to force `color()`)
      color.flags &= ~StyleColorFlags::IS_LEGACY_SRGB;
    }
  }

  // Step 6: Return the result of serializing color. If htmlCompatible is true,
  // then do so with HTML-compatible serialization requested.
  if (htmlCompatible) {
    SerializeColorForHTMLCompatibility(color, aResult);
    return;
  }
  nsAutoCString result;
  Servo_AbsoluteColor_ToCss(&color, &result);
  CopyUTF8toUTF16(result, aResult);
}

nsTArray<nsString> HTMLInputElement::GetColorsFromList() {
  RefPtr<HTMLDataListElement> dataList = GetListInternal();
  if (!dataList) {
    return {};
  }

  nsTArray<nsString> colors;

  RefPtr<ContentList> options = dataList->Options();
  uint32_t length = options->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    auto* option = HTMLOptionElement::FromNodeOrNull(options->Item(i, false));
    if (!option) {
      continue;
    }

    nsAutoString value;
    option->GetValue(value);
    // https://html.spec.whatwg.org/#update-a-color-well-control-color
    // https://html.spec.whatwg.org/#serialize-a-color-well-control-color
    if (Maybe<StyleAbsoluteColor> result =
            MaybeComputeColor(OwnerDoc(), value)) {
      SerializeColor(*result, GetColorSpaceEnum(), Alpha(), value);
      colors.AppendElement(value);
    }
  }

  return colors;
}

nsresult HTMLInputElement::InitColorPicker() {
  MOZ_ASSERT(IsMutable());

  if (mPickerRunning) {
    NS_WARNING("Just one nsIColorPicker is allowed");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<Document> doc = OwnerDoc();

  RefPtr<BrowsingContext> bc = doc->GetBrowsingContext();
  if (!bc) {
    return NS_ERROR_FAILURE;
  }

  if (IsPickerBlocked(doc)) {
    return NS_OK;
  }

  // NOTE(krosylight): Android doesn't support HTML widgets. We can modify
  // GeckoView to handle MozOpenColorPicker and let it keep using its current
  // picker, but for now this is ok.
#ifndef ANDROID
  if (StaticPrefs::dom_forms_html_color_picker_enabled()) {
    OpenColorPicker();
    return NS_OK;
  }
#endif

  // Get Loc title
  nsAutoString title;
  nsContentUtils::GetLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                     "ColorPicker", title);

  nsCOMPtr<nsIColorPicker> colorPicker =
      do_CreateInstance("@mozilla.org/colorpicker;1");
  if (!colorPicker) {
    return NS_ERROR_FAILURE;
  }

  nsAutoString initialValue;
  GetNonFileValueInternal(initialValue);
  nsTArray<nsString> colors = GetColorsFromList();
  nsresult rv = colorPicker->Init(bc, title, initialValue, colors);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIColorPickerShownCallback> callback =
      new nsColorPickerShownCallback(this, colorPicker);

  rv = colorPicker->Open(callback);
  if (NS_SUCCEEDED(rv)) {
    mPickerRunning = true;
    SetOpenState(true);
  }

  return rv;
}

nsresult HTMLInputElement::InitFilePicker(FilePickerType aType) {
  MOZ_ASSERT(IsMutable());

  if (mPickerRunning) {
    NS_WARNING("Just one nsIFilePicker is allowed");
    return NS_ERROR_FAILURE;
  }

  // Get parent nsPIDOMWindow object.
  nsCOMPtr<Document> doc = OwnerDoc();

  RefPtr<BrowsingContext> bc = doc->GetBrowsingContext();
  if (!bc) {
    return NS_ERROR_FAILURE;
  }

  if (IsPickerBlocked(doc)) {
    return NS_OK;
  }

  // Get Loc title
  nsAutoString title;
  nsAutoString okButtonLabel;
  if (aType == FILE_PICKER_DIRECTORY) {
    nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                            "DirectoryUpload", doc, title);

    nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                            "DirectoryPickerOkButtonLabel", doc,
                                            okButtonLabel);
  } else {
    nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                            "FileUpload", doc, title);
  }

  nsCOMPtr<nsIFilePicker> filePicker =
      do_CreateInstance("@mozilla.org/filepicker;1");
  if (!filePicker) return NS_ERROR_FAILURE;

  nsIFilePicker::Mode mode;

  if (aType == FILE_PICKER_DIRECTORY) {
    mode = nsIFilePicker::modeGetFolder;
  } else if (HasAttr(nsGkAtoms::multiple)) {
    mode = nsIFilePicker::modeOpenMultiple;
  } else {
    mode = nsIFilePicker::modeOpen;
  }

  nsresult rv = filePicker->Init(bc, title, mode, GetRelevantGlobal());
  NS_ENSURE_SUCCESS(rv, rv);

  if (!okButtonLabel.IsEmpty()) {
    filePicker->SetOkButtonLabel(okButtonLabel);
  }

  // Native directory pickers ignore file type filters, so we don't spend
  // cycles adding them for FILE_PICKER_DIRECTORY.
  if (HasAttr(nsGkAtoms::accept) && aType != FILE_PICKER_DIRECTORY) {
    SetFilePickerFiltersFromAccept(filePicker);

    if (StaticPrefs::dom_capture_enabled()) {
      if (const nsAttrValue* captureVal = GetParsedAttr(nsGkAtoms::capture)) {
        filePicker->SetCapture(static_cast<nsIFilePicker::CaptureTarget>(
            captureVal->GetEnumValue()));
      }
    }
  } else {
    filePicker->AppendFilters(nsIFilePicker::filterAll);
  }

  // Set default directory and filename
  nsAutoString defaultName;

  const nsTArray<OwningFileOrDirectory>& oldFiles =
      GetFilesOrDirectoriesInternal();

  nsCOMPtr<nsIFilePickerShownCallback> callback =
      new HTMLInputElement::nsFilePickerShownCallback(this, filePicker);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    // Used by WebDriver BiDi to emit input.fileDialogOpened whenever an input
    // type=file opens a file picker.
    obs->NotifyObservers(ToSupports(this), "file-input-picker-opening",
                         nullptr);
  }

  if (!oldFiles.IsEmpty() && aType != FILE_PICKER_DIRECTORY) {
    nsAutoString path;

    nsCOMPtr<nsIFile> parentFile = LastUsedDirectory(oldFiles[0]);
    if (parentFile) {
      filePicker->SetDisplayDirectory(parentFile);
    }

    // Unfortunately nsIFilePicker doesn't allow multiple files to be
    // default-selected, so only select something by default if exactly
    // one file was selected before.
    if (oldFiles.Length() == 1) {
      nsAutoString leafName;
      GetDOMFileOrDirectoryName(oldFiles[0], leafName);

      if (!leafName.IsEmpty()) {
        filePicker->SetDefaultString(leafName);
      }
    }

    rv = filePicker->Open(callback);
    if (NS_SUCCEEDED(rv)) {
      mPickerRunning = true;
      SetOpenState(true);
    }

    return rv;
  }

  HTMLInputElement::gUploadLastDir->FetchDirectoryAndDisplayPicker(
      doc, filePicker, callback);
  mPickerRunning = true;
  SetOpenState(true);
  return NS_OK;
}

#define CPS_PREF_NAME u"browser.upload.lastDir"_ns

NS_IMPL_ISUPPORTS(UploadLastDir, nsIObserver, nsISupportsWeakReference)

void HTMLInputElement::InitUploadLastDir() {
  gUploadLastDir = new UploadLastDir();
  NS_ADDREF(gUploadLastDir);

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService && gUploadLastDir) {
    observerService->AddObserver(gUploadLastDir,
                                 "browser:purge-session-history", true);
  }
}

void HTMLInputElement::DestroyUploadLastDir() { NS_IF_RELEASE(gUploadLastDir); }

nsresult UploadLastDir::FetchDirectoryAndDisplayPicker(
    Document* aDoc, nsIFilePicker* aFilePicker,
    nsIFilePickerShownCallback* aFpCallback) {
  MOZ_ASSERT(aDoc, "aDoc is null");
  MOZ_ASSERT(aFilePicker, "aFilePicker is null");
  MOZ_ASSERT(aFpCallback, "aFpCallback is null");

  nsIURI* docURI = aDoc->GetDocumentURI();
  MOZ_ASSERT(docURI, "docURI is null");

  nsCOMPtr<nsILoadContext> loadContext = aDoc->GetLoadContext();
  nsCOMPtr<nsIContentPrefCallback2> prefCallback =
      new UploadLastDir::ContentPrefCallback(aFilePicker, aFpCallback);

  // Attempt to get the CPS, if it's not present we'll fallback to use the
  // Desktop folder
  nsCOMPtr<nsIContentPrefService2> contentPrefService =
      do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
  if (!contentPrefService) {
    prefCallback->HandleCompletion(nsIContentPrefCallback2::COMPLETE_ERROR);
    return NS_OK;
  }

  nsAutoCString cstrSpec;
  docURI->GetSpec(cstrSpec);
  NS_ConvertUTF8toUTF16 spec(cstrSpec);

  contentPrefService->GetByDomainAndName(spec, CPS_PREF_NAME, loadContext,
                                         prefCallback);
  return NS_OK;
}

nsresult UploadLastDir::StoreLastUsedDirectory(Document* aDoc, nsIFile* aDir) {
  MOZ_ASSERT(aDoc, "aDoc is null");
  if (!aDir) {
    return NS_OK;
  }

  nsCOMPtr<nsIURI> docURI = aDoc->GetDocumentURI();
  MOZ_ASSERT(docURI, "docURI is null");

  // Attempt to get the CPS, if it's not present we'll just return
  nsCOMPtr<nsIContentPrefService2> contentPrefService =
      do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
  if (!contentPrefService) return NS_ERROR_NOT_AVAILABLE;

  nsAutoCString cstrSpec;
  docURI->GetSpec(cstrSpec);
  NS_ConvertUTF8toUTF16 spec(cstrSpec);

  // Find the parent of aFile, and store it
  nsString unicodePath;
  aDir->GetPath(unicodePath);
  if (unicodePath.IsEmpty())  // nothing to do
    return NS_OK;
  RefPtr<nsVariantCC> prefValue = new nsVariantCC();
  prefValue->SetAsAString(unicodePath);

  // Use the document's current load context to ensure that the content pref
  // service doesn't persistently store this directory for this domain if the
  // user is using private browsing:
  nsCOMPtr<nsILoadContext> loadContext = aDoc->GetLoadContext();
  return contentPrefService->Set(spec, CPS_PREF_NAME, prefValue, loadContext,
                                 nullptr);
}

NS_IMETHODIMP
UploadLastDir::Observe(nsISupports* aSubject, char const* aTopic,
                       char16_t const* aData) {
  if (strcmp(aTopic, "browser:purge-session-history") == 0) {
    nsCOMPtr<nsIContentPrefService2> contentPrefService =
        do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
    if (contentPrefService)
      contentPrefService->RemoveByName(CPS_PREF_NAME, nullptr, nullptr);
  }
  return NS_OK;
}

//
// construction, destruction
//

HTMLInputElement::HTMLInputElement(already_AddRefed<dom::NodeInfo> aNodeInfo,
                                   FromParser aFromParser, FromClone aFromClone)
    : TextControlElement(std::move(aNodeInfo), aFromParser,
                         FormControlType(kInputDefaultType->value)),
      mAutocompleteAttrState(nsContentUtils::eAutocompleteAttrState_Unknown),
      mAutocompleteInfoState(nsContentUtils::eAutocompleteAttrState_Unknown),
      mDisabledChanged(false),
      mValueChanged(false),
      mUserInteracted(false),
      mLastValueChangeWasInteractive(false),
      mCheckedChanged(false),
      mChecked(false),
      mShouldInitChecked(false),
      mDoneCreating(aFromParser == NOT_FROM_PARSER &&
                    aFromClone == FromClone::No),
      mInInternalActivate(false),
      mCheckedIsToggled(false),
      mIndeterminate(false),
      mInhibitRestoration(aFromParser & FROM_PARSER_FRAGMENT),
      mHasRange(false),
      mIsDraggingRange(false),
      mNumberControlSpinnerIsSpinning(false),
      mNumberControlSpinnerSpinsUp(false),
      mPickerRunning(false),
      mHasBeenTypePassword(false),
      mHasPatternAttribute(false),
      mUserChangedSinceFocus(false),
      mIsUserInteracting(false),
      mRadioGroupContainer(nullptr) {
  // If size is above 512, mozjemalloc allocates 1kB, see
  // memory/build/mozjemalloc.cpp
  static_assert(sizeof(HTMLInputElement) <= 512,
                "Keep the size of HTMLInputElement under 512 to avoid "
                "performance regression!");

  // We are in a type=text but we create TextControlState lazily.
  mInputData.mState = nullptr;

  void* memory = mInputTypeMem;
  mInputType = InputType::Create(this, mType, memory);

  if (!gUploadLastDir) HTMLInputElement::InitUploadLastDir();

  // Set up our default state.  By default we're enabled (since we're a control
  // type that can be disabled but not actually disabled right now), optional,
  // read-write, and valid. Also by default we don't have to show validity UI
  // and so forth.
  AddStatesSilently(ElementState::ENABLED | ElementState::OPTIONAL_ |
                    ElementState::VALID | ElementState::VALUE_EMPTY |
                    ElementState::READWRITE);
  RemoveStatesSilently(ElementState::READONLY);
  UpdateApzAwareFlag();
}

HTMLInputElement::~HTMLInputElement() {
  if (mNumberControlSpinnerIsSpinning) {
    StopNumberControlSpinnerSpin(eDisallowDispatchingEvents);
  }
  nsImageLoadingContent::Destroy();
  FreeData(TextControlStateDisposition::Destroy);
}

void HTMLInputElement::FreeData(TextControlStateDisposition aStateDisposition) {
  if (!IsSingleLineTextControl(false)) {
    free(mInputData.mValue);
    mInputData.mValue = nullptr;
  } else if (mInputData.mState &&
             aStateDisposition == TextControlStateDisposition::Destroy) {
    mInputData.mState->Destroy();
    mInputData.mState = nullptr;
  }

  if (mInputType) {
    mInputType->DropReference();
    mInputType = nullptr;
  }
}

void HTMLInputElement::EnsureEditorState() {
  MOZ_ASSERT(IsSingleLineTextControl(false));
  if (!mInputData.mState) {
    mInputData.mState = TextControlState::Construct(this);
  }
}

TextControlState* HTMLInputElement::GetEditorState() const {
  if (!IsSingleLineTextControl(false)) {
    return nullptr;
  }

  // We've postponed allocating TextControlState, doing that in a const
  // method is fine.
  const_cast<HTMLInputElement*>(this)->EnsureEditorState();

  MOZ_ASSERT(mInputData.mState,
             "Single line text controls need to have a state"
             " associated with them");

  return mInputData.mState;
}

// nsISupports

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLInputElement)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLInputElement,
                                                  TextControlElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mValidity)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mControllers)
  if (tmp->IsSingleLineTextControl(false) && tmp->mInputData.mState) {
    tmp->mInputData.mState->Traverse(cb);
  }

  if (tmp->mFileData) {
    tmp->mFileData->Traverse(cb);
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLInputElement,
                                                TextControlElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mValidity)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mControllers)
  if (tmp->IsSingleLineTextControl(false) && tmp->mInputData.mState) {
    tmp->mInputData.mState->Unlink();
  }

  if (tmp->mFileData) {
    tmp->mFileData->Unlink();
  }
  // XXX should unlink more?
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED(HTMLInputElement,
                                             TextControlElement,
                                             imgINotificationObserver,
                                             nsIImageLoadingContent,
                                             nsIConstraintValidation)

// nsINode

nsresult HTMLInputElement::Clone(dom::NodeInfo* aNodeInfo,
                                 nsINode** aResult) const {
  *aResult = nullptr;

  RefPtr<HTMLInputElement> it = new (aNodeInfo->NodeInfoManager())
      HTMLInputElement(do_AddRef(aNodeInfo), NOT_FROM_PARSER, FromClone::Yes);

  nsresult rv = const_cast<HTMLInputElement*>(this)->CopyInnerTo(it);
  NS_ENSURE_SUCCESS(rv, rv);

  switch (GetValueMode()) {
    case VALUE_MODE_VALUE:
      if (mValueChanged) {
        // We don't have our default value anymore.  Set our value on
        // the clone.
        nsAutoString value;
        GetNonFileValueInternal(value);
        // SetValueInternal handles setting the VALUE_CHANGED bit for us
        if (NS_WARN_IF(
                NS_FAILED(rv = it->SetValueInternal(
                              value, {ValueSetterOption::SetValueChanged})))) {
          return rv;
        }
      }
      break;
    case VALUE_MODE_FILENAME:
      if (it->OwnerDoc()->IsStaticDocument()) {
        // We're going to be used in print preview.  Since the doc is static
        // we can just grab the pretty string and use it as wallpaper
        GetDisplayFileName(it->mFileData->mStaticDocFileList);
      } else {
        it->mFileData->ClearGetFilesHelpers();
        it->mFileData->mFilesOrDirectories.Clear();
        it->mFileData->mFilesOrDirectories.AppendElements(
            mFileData->mFilesOrDirectories);
      }
      break;
    case VALUE_MODE_DEFAULT_ON:
    case VALUE_MODE_DEFAULT:
      break;
  }

  if (mCheckedChanged) {
    // We no longer have our original checked state.  Set our
    // checked state on the clone.
    it->DoSetChecked(mChecked, /* aNotify */ false,
                     /* aSetValueChanged */ true);
    // Then tell DoneCreatingElement() not to overwrite:
    it->mShouldInitChecked = false;
  }

  it->mIndeterminate = mIndeterminate;

  it->DoneCreatingElement();

  it->SetLastValueChangeWasInteractive(mLastValueChangeWasInteractive);
  it.forget(aResult);
  return NS_OK;
}

void HTMLInputElement::BeforeSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                     const nsAttrValue* aValue, bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aNotify && aName == nsGkAtoms::disabled) {
      mDisabledChanged = true;
    }

    // When name or type changes, radio should be removed from radio group.
    // If we are not done creating the radio, we also should not do it.
    if (mType == FormControlType::InputRadio) {
      if ((aName == nsGkAtoms::name || (aName == nsGkAtoms::type && !mForm)) &&
          (mForm || mDoneCreating)) {
        RemoveFromRadioGroup();
      } else if (aName == nsGkAtoms::required) {
        auto* container = GetCurrentRadioGroupContainer();

        if (container && ((aValue && !HasAttr(aNameSpaceID, aName)) ||
                          (!aValue && HasAttr(aNameSpaceID, aName)))) {
          nsAutoString name;
          GetAttr(nsGkAtoms::name, name);
          container->RadioRequiredWillChange(name, !!aValue);
        }
      }
    }
  }

  return nsGenericHTMLFormControlElementWithState::BeforeSetAttr(
      aNameSpaceID, aName, aValue, aNotify);
}

void HTMLInputElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                    const nsAttrValue* aValue,
                                    const nsAttrValue* aOldValue,
                                    nsIPrincipal* aSubjectPrincipal,
                                    bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None) {
    bool needValidityUpdate = false;
    if (aName == nsGkAtoms::value) {
      // If the element has a value in value mode, the value content attribute
      // is the default value. So if the elements value didn't change from the
      // default, we have to re-set it.
      if (!mValueChanged && GetValueMode() == VALUE_MODE_VALUE) {
        SetDefaultValueAsValue();
      } else if (GetValueMode() == VALUE_MODE_DEFAULT) {
        ResetDirFormAssociatedElement(this, aNotify, HasDirAuto());
      }
      // GetStepBase() depends on the `value` attribute if `min` is not present,
      // even if the value doesn't change.
      UpdateStepMismatchValidityState();
      needValidityUpdate = true;
    } else if (aName == nsGkAtoms::checked) {
      // Checked must be set no matter what type of control it is, since
      // mChecked must reflect the new value
      if (IsRadioOrCheckbox()) {
        SetStates(ElementState::DEFAULT, !!aValue, aNotify);
      }
      if (!mCheckedChanged) {
        // Delay setting checked if we are creating this element (wait
        // until everything is set)
        if (!mDoneCreating) {
          mShouldInitChecked = true;
        } else {
          DoSetChecked(!!aValue, aNotify, /* aSetValueChanged */ false);
        }
      }
      needValidityUpdate = true;
    } else if (aName == nsGkAtoms::type) {
      FormControlType newType;
      if (!aValue) {
        // We're now a text input.
        newType = FormControlType(kInputDefaultType->value);
      } else {
        newType = FormControlType(aValue->GetEnumValue());
      }
      if (newType != mType) {
        HandleTypeChange(newType, aNotify);
        needValidityUpdate = true;
      }
    } else if (aName == nsGkAtoms::required || aName == nsGkAtoms::disabled ||
               aName == nsGkAtoms::readonly) {
      if (aName == nsGkAtoms::disabled) {
        // This *has* to be called *before* validity state check because
        // UpdateBarredFromConstraintValidation and
        // UpdateValueMissingValidityState depend on our disabled state.
        UpdateDisabledState(aNotify);
      }

      if (aName == nsGkAtoms::required && DoesRequiredApply()) {
        // This *has* to be called *before* UpdateValueMissingValidityState
        // because UpdateValueMissingValidityState depends on our required
        // state.
        UpdateRequiredState(!!aValue, aNotify);
      }

      if (aName == nsGkAtoms::readonly && !!aValue != !!aOldValue) {
        UpdateReadOnlyState(aNotify);
      }

      UpdateValueMissingValidityState();

      // This *has* to be called *after* validity has changed.
      if (aName == nsGkAtoms::readonly || aName == nsGkAtoms::disabled) {
        UpdateBarredFromConstraintValidation();
      }
      needValidityUpdate = true;
    } else if (aName == nsGkAtoms::src) {
      nsAttrValueOrString value(aValue);
      mSrcTriggeringPrincipal = nsContentUtils::GetAttrTriggeringPrincipal(
          this, value.String(), aSubjectPrincipal);
      if (aNotify && mType == FormControlType::InputImage) {
        if (aValue) {
          // Mark channel as urgent-start before load image if the image load is
          // initiated by a user interaction.
          mUseUrgentStartForChannel = UserActivation::IsHandlingUserInput();

          LoadImage(value.String(), true, aNotify, eImageLoadType_Normal,
                    mSrcTriggeringPrincipal);
        } else {
          // Null value means the attr got unset; drop the image
          CancelImageRequests(aNotify);
        }
      }
    } else if (aName == nsGkAtoms::maxlength) {
      UpdateTooLongValidityState();
      if (auto* editor = GetExtantTextEditor()) {
        editor->SetMaxTextLength(UsedMaxLength());
      }
      needValidityUpdate = true;
    } else if (aName == nsGkAtoms::minlength) {
      UpdateTooShortValidityState();
      needValidityUpdate = true;
    } else if (aName == nsGkAtoms::pattern) {
      // Although pattern attribute only applies to single line text controls,
      // we set this flag for all input types to save having to check the type
      // here.
      mHasPatternAttribute = !!aValue;

      if (mDoneCreating) {
        UpdatePatternMismatchValidityState();
      }
      needValidityUpdate = true;
    } else if (aName == nsGkAtoms::multiple) {
      UpdateTypeMismatchValidityState();
      needValidityUpdate = true;
    } else if (aName == nsGkAtoms::max) {
      UpdateHasRange(aNotify);
      mInputType->MinMaxStepAttrChanged();
      // Validity state must be updated *after* the UpdateValueDueToAttrChange
      // call above or else the following assert will not be valid.
      // We don't assert the state of underflow during creation since
      // DoneCreatingElement sanitizes.
      UpdateRangeOverflowValidityState();
      needValidityUpdate = true;
      MOZ_ASSERT(!mDoneCreating || mType != FormControlType::InputRange ||
                     !GetValidityState(VALIDITY_STATE_RANGE_UNDERFLOW),
                 "HTML5 spec does not allow underflow for type=range");
    } else if (aName == nsGkAtoms::min) {
      UpdateHasRange(aNotify);
      mInputType->MinMaxStepAttrChanged();
      // See corresponding @max comment
      UpdateRangeUnderflowValidityState();
      UpdateStepMismatchValidityState();
      needValidityUpdate = true;
      MOZ_ASSERT(!mDoneCreating || mType != FormControlType::InputRange ||
                     !GetValidityState(VALIDITY_STATE_RANGE_UNDERFLOW),
                 "HTML5 spec does not allow underflow for type=range");
    } else if (aName == nsGkAtoms::step) {
      mInputType->MinMaxStepAttrChanged();
      // See corresponding @max comment
      UpdateStepMismatchValidityState();
      needValidityUpdate = true;
      MOZ_ASSERT(!mDoneCreating || mType != FormControlType::InputRange ||
                     !GetValidityState(VALIDITY_STATE_RANGE_UNDERFLOW),
                 "HTML5 spec does not allow underflow for type=range");
    } else if (aName == nsGkAtoms::dir && aValue &&
               aValue->Equals(nsGkAtoms::_auto, eIgnoreCase)) {
      ResetDirFormAssociatedElement(this, aNotify, true);
    } else if (aName == nsGkAtoms::lang) {
      // FIXME(emilio, bug 1651070): This doesn't account for lang changes on
      // ancestors.
      if (mType == FormControlType::InputNumber) {
        // The validity of our value may have changed based on the locale.
        UpdateValidityState();
        needValidityUpdate = true;
      }
    } else if (aName == nsGkAtoms::autocomplete) {
      // Clear the cached @autocomplete attribute and autocompleteInfo state.
      mAutocompleteAttrState = nsContentUtils::eAutocompleteAttrState_Unknown;
      mAutocompleteInfoState = nsContentUtils::eAutocompleteAttrState_Unknown;
    } else if (aName == nsGkAtoms::placeholder) {
      UpdatePlaceholder(aOldValue, aValue);
      UpdatePlaceholderShownState();
      needValidityUpdate = true;
    } else if (aName == nsGkAtoms::colorspace || aName == nsGkAtoms::alpha) {
      UpdateColor();
    } else if (aName == nsGkAtoms::webkitdirectory) {
      glean::dom::webkit_directory_used
          .EnumGet(glean::dom::WebkitDirectoryUsedLabel::eTrue)
          .Add();
    }

    // When name or type changes, radio should be added to radio group.
    // If we are not done creating the radio, we also should not do it.
    if (mType == FormControlType::InputRadio) {
      if ((aName == nsGkAtoms::name || (aName == nsGkAtoms::type && !mForm)) &&
          (mForm || mDoneCreating)) {
        AddToRadioGroup();
        UpdateValueMissingValidityStateForRadio(false);
        needValidityUpdate = true;
      }
    } else if (CreatesDateTimeWidget()) {
      if (aName == nsGkAtoms::value || aName == nsGkAtoms::readonly ||
          aName == nsGkAtoms::tabindex || aName == nsGkAtoms::required ||
          aName == nsGkAtoms::disabled) {
        // If original target is this and not the inner text control, we should
        // pass the focus to the inner text control.
        if (Element* dateTimeBoxElement = GetDateTimeBoxElement()) {
          AsyncEventDispatcher::RunDOMEventWhenSafe(
              *dateTimeBoxElement,
              aName == nsGkAtoms::value ? u"MozDateTimeValueChanged"_ns
                                        : u"MozDateTimeAttributeChanged"_ns,
              CanBubble::eNo, ChromeOnlyDispatch::eNo);
        }
      }
    }
    if (needValidityUpdate) {
      UpdateValidityElementStates(aNotify);
    }
  }

  return nsGenericHTMLFormControlElementWithState::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aSubjectPrincipal, aNotify);
}

void HTMLInputElement::BeforeSetForm(HTMLFormElement* aForm, bool aBindToTree) {
  // No need to remove from radio group if we are just binding to tree.
  if (mType == FormControlType::InputRadio && !aBindToTree) {
    RemoveFromRadioGroup();
  }

  // Dispatch event when <input> @form is set
  if (!aBindToTree) {
    MaybeDispatchLoginManagerEvents(aForm);
  }
}

void HTMLInputElement::AfterClearForm(bool aUnbindOrDelete) {
  MOZ_ASSERT(!mForm);

  // Do not add back to radio group if we are releasing or unbinding from tree.
  if (mType == FormControlType::InputRadio && !aUnbindOrDelete &&
      !GetCurrentRadioGroupContainer()) {
    AddToRadioGroup();
    UpdateValueMissingValidityStateForRadio(false);
  }
}

void HTMLInputElement::ResultForDialogSubmit(nsAString& aResult) {
  if (mType == FormControlType::InputImage) {
    // Get a property set by the frame to find out where it was clicked.
    const auto* lastClickedPoint =
        static_cast<CSSIntPoint*>(GetProperty(nsGkAtoms::imageClickedPoint));
    int32_t x, y;
    if (lastClickedPoint) {
      x = lastClickedPoint->x;
      y = lastClickedPoint->y;
    } else {
      x = y = 0;
    }
    aResult.AppendInt(x);
    aResult.AppendLiteral(",");
    aResult.AppendInt(y);
  } else {
    GetAttr(nsGkAtoms::value, aResult);
  }
}

bool HTMLInputElement::Alpha() const {
  if (!StaticPrefs::dom_forms_alpha_enabled()) {
    return false;
  }
  return HasAttr(nsGkAtoms::alpha);
}

void HTMLInputElement::GetAutocomplete(nsAString& aValue) {
  if (!DoesAutocompleteApply()) {
    return;
  }

  aValue.Truncate();
  const nsAttrValue* attributeVal = GetParsedAttr(nsGkAtoms::autocomplete);

  mAutocompleteAttrState = nsContentUtils::SerializeAutocompleteAttribute(
      attributeVal, aValue, mAutocompleteAttrState);
}

void HTMLInputElement::GetAutocompleteInfo(Nullable<AutocompleteInfo>& aInfo) {
  if (!DoesAutocompleteApply()) {
    aInfo.SetNull();
    return;
  }

  const nsAttrValue* attributeVal = GetParsedAttr(nsGkAtoms::autocomplete);
  mAutocompleteInfoState = nsContentUtils::SerializeAutocompleteAttribute(
      attributeVal, aInfo.SetValue(), mAutocompleteInfoState, true);
}

void HTMLInputElement::GetCapture(nsAString& aValue) {
  GetEnumAttr(nsGkAtoms::capture, kCaptureDefault->tag, aValue);
}

void HTMLInputElement::GetColorSpace(nsAString& aValue) const {
  if (!StaticPrefs::dom_forms_colorspace_enabled()) {
    aValue.Truncate();
    aValue.AppendLiteral("limited-srgb");
    return;
  }
  GetEnumAttr(nsGkAtoms::colorspace, kColorSpaceDefault->tag, aValue);
}

StyleColorSpace HTMLInputElement::GetColorSpaceEnum() const {
  if (!StaticPrefs::dom_forms_colorspace_enabled()) {
    return StyleColorSpace::Srgb;
  }
  if (const nsAttrValue* captureVal = GetParsedAttr(nsGkAtoms::colorspace)) {
    return static_cast<StyleColorSpace>(captureVal->GetEnumValue());
  }
  return StyleColorSpace::Srgb;
}

void HTMLInputElement::GetFormEnctype(nsAString& aValue) {
  GetEnumAttr(nsGkAtoms::formenctype, "", kFormDefaultEnctype->tag, aValue);
}

void HTMLInputElement::GetFormMethod(nsAString& aValue) {
  GetEnumAttr(nsGkAtoms::formmethod, "", kFormDefaultMethod->tag, aValue);
}

void HTMLInputElement::GetType(nsAString& aValue) const {
  GetEnumAttr(nsGkAtoms::type, kInputDefaultType->tag, aValue);
}

int32_t HTMLInputElement::TabIndexDefault() { return 0; }

uint32_t HTMLInputElement::Height() {
  if (mType != FormControlType::InputImage) {
    return 0;
  }
  return GetWidthHeightForImage().height;
}

void HTMLInputElement::SetIndeterminateInternal(bool aValue,
                                                bool aShouldInvalidate) {
  mIndeterminate = aValue;
  if (mType != FormControlType::InputCheckbox) {
    return;
  }

  SetStates(ElementState::INDETERMINATE, aValue);

  if (aShouldInvalidate) {
    // Repaint the frame
    if (nsIFrame* frame = GetPrimaryFrame()) {
      frame->InvalidateFrameSubtree();
    }
  }
}

void HTMLInputElement::SetIndeterminate(bool aValue) {
  SetIndeterminateInternal(aValue, true);
}

uint32_t HTMLInputElement::Width() {
  if (mType != FormControlType::InputImage) {
    return 0;
  }
  return GetWidthHeightForImage().width;
}

bool HTMLInputElement::SanitizesOnValueGetter() const {
  // Don't return non-sanitized value for datetime types, email, or number.
  return mType == FormControlType::InputEmail ||
         mType == FormControlType::InputNumber || IsDateTimeInputType(mType);
}

void HTMLInputElement::GetValue(nsAString& aValue, CallerType aCallerType) {
  GetValueInternal(aValue, aCallerType);

  // In the case where we need to sanitize an input value without affecting
  // the displayed user's input, we instead sanitize only on .value accesses.
  // For the more general case of input elements displaying text that isn't
  // their current value, see bug 805049.
  if (SanitizesOnValueGetter()) {
    SanitizeValue(aValue, SanitizationKind::ForValueGetter);
  }
}

void HTMLInputElement::GetValueInternal(nsAString& aValue,
                                        CallerType aCallerType) const {
  if (mType != FormControlType::InputFile) {
    GetNonFileValueInternal(aValue);
    return;
  }

  if (aCallerType == CallerType::System) {
    aValue.Assign(mFileData->mFirstFilePath);
    return;
  }

  if (mFileData->mFilesOrDirectories.IsEmpty()) {
    aValue.Truncate();
    return;
  }

  nsAutoString file;
  GetDOMFileOrDirectoryName(mFileData->mFilesOrDirectories[0], file);
  if (file.IsEmpty()) {
    aValue.Truncate();
    return;
  }

  aValue.AssignLiteral("C:\\fakepath\\");
  aValue.Append(file);
}

void HTMLInputElement::GetNonFileValueInternal(nsAString& aValue) const {
  switch (GetValueMode()) {
    case VALUE_MODE_VALUE:
      if (IsSingleLineTextControl(false)) {
        if (mInputData.mState) {
          mInputData.mState->GetValue(aValue, /* aForDisplay = */ false);
        } else {
          // Value hasn't been set yet.
          aValue.Truncate();
        }
      } else if (!aValue.Assign(mInputData.mValue, fallible)) {
        aValue.Truncate();
      }
      return;

    case VALUE_MODE_FILENAME:
      MOZ_ASSERT_UNREACHABLE("Someone screwed up here");
      // We'll just return empty string if someone does screw up.
      aValue.Truncate();
      return;

    case VALUE_MODE_DEFAULT:
      // Treat defaultValue as value.
      GetAttr(nsGkAtoms::value, aValue);
      return;

    case VALUE_MODE_DEFAULT_ON:
      // Treat default value as value and returns "on" if no value.
      if (!GetAttr(nsGkAtoms::value, aValue)) {
        aValue.AssignLiteral("on");
      }
      return;
  }
}

void HTMLInputElement::ClearFiles(bool aSetValueChanged) {
  nsTArray<OwningFileOrDirectory> data;
  SetFilesOrDirectories(data, aSetValueChanged);
}

int32_t HTMLInputElement::MonthsSinceJan1970(uint32_t aYear,
                                             uint32_t aMonth) const {
  return (aYear - 1970) * 12 + aMonth - 1;
}

/* static */
Decimal HTMLInputElement::StringToDecimal(const nsAString& aValue) {
  auto d = nsContentUtils::ParseHTMLFloatingPointNumber(aValue);
  return d ? Decimal::fromDouble(*d) : Decimal::nan();
}

Decimal HTMLInputElement::GetValueAsDecimal() const {
  nsAutoString stringValue;
  GetNonFileValueInternal(stringValue);
  Decimal result =
      mInputType->ConvertStringToNumber(stringValue, InputType::Localized::Yes)
          .mResult;
  return result.isFinite() ? result : Decimal::nan();
}

void HTMLInputElement::SetValue(const nsAString& aValue, CallerType aCallerType,
                                ErrorResult& aRv) {
  // check security.  Note that setting the value to the empty string is always
  // OK and gives pages a way to clear a file input if necessary.
  if (mType == FormControlType::InputFile) {
    if (!aValue.IsEmpty()) {
      if (aCallerType != CallerType::System) {
        // setting the value of a "FILE" input widget requires
        // chrome privilege
        aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
        return;
      }
      Sequence<nsString> list;
      if (!list.AppendElement(aValue, fallible)) {
        aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }

      MozSetFileNameArray(list, aRv);
      return;
    }
    ClearFiles(true);
  } else {
    if (MayFireChangeOnBlur()) {
      // If the value has been set by a script, we basically want to keep the
      // current change event state. If the element is ready to fire a change
      // event, we should keep it that way. Otherwise, we should make sure the
      // element will not fire any event because of the script interaction.
      //
      // NOTE: this is currently quite expensive work (too much string
      // manipulation). We should probably optimize that.
      nsAutoString currentValue;
      GetNonFileValueInternal(currentValue);

      nsresult rv = SetValueInternal(
          aValue, &currentValue,
          {ValueSetterOption::ByContentAPI, ValueSetterOption::SetValueChanged,
           ValueSetterOption::MoveCursorToEndIfValueChanged});
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }

      if (!State().HasState(ElementState::FOCUS) && !mIsUserInteracting) {
        GetValue(mFocusedValue, aCallerType);
      }
    } else {
      nsresult rv = SetValueInternal(
          aValue,
          {ValueSetterOption::ByContentAPI, ValueSetterOption::SetValueChanged,
           ValueSetterOption::MoveCursorToEndIfValueChanged});
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
        return;
      }
    }
  }
}

Element* HTMLInputElement::GetListForBindings() const {
  return RetargetReferenceTargetForBindings(GetListInternal());
}

HTMLDataListElement* HTMLInputElement::GetListInternal() const {
  nsAutoString dataListId;
  GetAttr(nsGkAtoms::list, dataListId);
  if (dataListId.IsEmpty()) {
    return nullptr;
  }

  DocumentOrShadowRoot* docOrShadow = GetUncomposedDocOrConnectedShadowRoot();
  if (!docOrShadow) {
    return nullptr;
  }

  Element* target = docOrShadow->GetElementById(dataListId);
  Element* list = target ? target->ResolveReferenceTarget() : nullptr;
  return HTMLDataListElement::FromNodeOrNull(list);
}

void HTMLInputElement::SetValue(Decimal aValue, CallerType aCallerType) {
  MOZ_ASSERT(!aValue.isInfinity(), "aValue must not be Infinity!");

  if (aValue.isNaN()) {
    SetValue(u""_ns, aCallerType, IgnoreErrors());
    return;
  }

  nsAutoString value;
  mInputType->ConvertNumberToString(aValue, InputType::Localized::No, value);
  SetValue(value, aCallerType, IgnoreErrors());
}

void HTMLInputElement::GetValueAsDate(JSContext* aCx,
                                      JS::MutableHandle<JSObject*> aObject,
                                      ErrorResult& aRv) {
  aObject.set(nullptr);
  // valueAsDate does not apply to datetime-local:
  // https://html.spec.whatwg.org/#local-date-and-time-state-(type=datetime-local):dom-input-valueasdate
  if (!IsDateTimeInputType(mType) ||
      mType == FormControlType::InputDatetimeLocal) {
    return;
  }

  Maybe<JS::ClippedTime> time;

  switch (mType) {
    case FormControlType::InputDate: {
      uint32_t year, month, day;
      nsAutoString value;
      GetNonFileValueInternal(value);
      if (!ParseDate(value, &year, &month, &day)) {
        return;
      }

      time.emplace(JS::TimeClip(JS::MakeDate(year, month - 1, day)));
      break;
    }
    case FormControlType::InputTime: {
      uint32_t millisecond;
      nsAutoString value;
      GetNonFileValueInternal(value);
      if (!ParseTime(value, &millisecond)) {
        return;
      }

      time.emplace(JS::TimeClip(int64_t(millisecond)));
      MOZ_ASSERT(time->toDouble() == millisecond,
                 "HTML times are restricted to the day after the epoch and "
                 "never clip");
      break;
    }
    case FormControlType::InputMonth: {
      uint32_t year, month;
      nsAutoString value;
      GetNonFileValueInternal(value);
      if (!ParseMonth(value, &year, &month)) {
        return;
      }

      time.emplace(JS::TimeClip(JS::MakeDate(year, month - 1, 1)));
      break;
    }
    case FormControlType::InputWeek: {
      uint32_t year, week;
      nsAutoString value;
      GetNonFileValueInternal(value);
      if (!ParseWeek(value, &year, &week)) {
        return;
      }

      double days = DaysSinceEpochFromWeek(year, week);
      time.emplace(JS::TimeClip(days * kMsPerDay));

      break;
    }
    default:
      break;
  }

  if (time) {
    aObject.set(JS::NewDateObject(aCx, *time));
    if (!aObject) {
      aRv.NoteJSContextException(aCx);
    }
    return;
  }

  MOZ_ASSERT(false, "Unrecognized input type");
  aRv.Throw(NS_ERROR_UNEXPECTED);
}

void HTMLInputElement::SetValueAsDate(JSContext* aCx,
                                      JS::Handle<JSObject*> aObj,
                                      ErrorResult& aRv) {
  // valueAsDate does not apply to datetime-local:
  // https://html.spec.whatwg.org/#local-date-and-time-state-(type=datetime-local):dom-input-valueasdate
  if (!IsDateTimeInputType(mType) ||
      mType == FormControlType::InputDatetimeLocal) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (aObj) {
    bool isDate;
    if (!JS::ObjectIsDate(aCx, aObj, &isDate)) {
      aRv.NoteJSContextException(aCx);
      return;
    }
    if (!isDate) {
      aRv.ThrowTypeError("Value being assigned is not a date.");
      return;
    }
  }

  double milliseconds;
  if (aObj) {
    if (!js::DateGetMsecSinceEpoch(aCx, aObj, &milliseconds)) {
      aRv.NoteJSContextException(aCx);
      return;
    }
  } else {
    milliseconds = UnspecifiedNaN<double>();
  }

  // At this point we know we're not a file input, so we can just pass "not
  // system" as the caller type, since the caller type only matters in the file
  // input case.
  if (std::isnan(milliseconds)) {
    SetValue(u""_ns, CallerType::NonSystem, aRv);
    return;
  }

  if (mType != FormControlType::InputMonth) {
    SetValue(Decimal::fromDouble(milliseconds), CallerType::NonSystem);
    return;
  }

  // type=month expects the value to be number of months.
  double year = JS::YearFromTime(milliseconds);
  double month = JS::MonthFromTime(milliseconds);

  if (std::isnan(year) || std::isnan(month)) {
    SetValue(u""_ns, CallerType::NonSystem, aRv);
    return;
  }

  int32_t months = MonthsSinceJan1970(year, month + 1);
  SetValue(Decimal(int32_t(months)), CallerType::NonSystem);
}

void HTMLInputElement::SetValueAsNumber(double aValueAsNumber,
                                        ErrorResult& aRv) {
  if (std::isinf(aValueAsNumber)) {
    aRv.ThrowTypeError("Value being assigned is infinite.");
    return;
  }

  if (!DoesValueAsNumberApply()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  // At this point we know we're not a file input, so we can just pass "not
  // system" as the caller type, since the caller type only matters in the file
  // input case.
  SetValue(Decimal::fromDouble(aValueAsNumber), CallerType::NonSystem);
}

Decimal HTMLInputElement::GetMinimum() const {
  MOZ_ASSERT(
      DoesValueAsNumberApply(),
      "GetMinimum() should only be used for types that allow .valueAsNumber");

  // Only type=range has a default minimum
  Decimal defaultMinimum =
      mType == FormControlType::InputRange ? Decimal(0) : Decimal::nan();

  if (!HasAttr(nsGkAtoms::min)) {
    return defaultMinimum;
  }

  nsAutoString minStr;
  GetAttr(nsGkAtoms::min, minStr);

  Decimal min =
      mInputType->ConvertStringToNumber(minStr, InputType::Localized::No)
          .mResult;
  return min.isFinite() ? min : defaultMinimum;
}

Decimal HTMLInputElement::GetMaximum() const {
  MOZ_ASSERT(
      DoesValueAsNumberApply(),
      "GetMaximum() should only be used for types that allow .valueAsNumber");

  // Only type=range has a default maximum
  Decimal defaultMaximum =
      mType == FormControlType::InputRange ? Decimal(100) : Decimal::nan();

  if (!HasAttr(nsGkAtoms::max)) {
    return defaultMaximum;
  }

  nsAutoString maxStr;
  GetAttr(nsGkAtoms::max, maxStr);

  Decimal max =
      mInputType->ConvertStringToNumber(maxStr, InputType::Localized::No)
          .mResult;
  return max.isFinite() ? max : defaultMaximum;
}

Decimal HTMLInputElement::GetStepBase() const {
  MOZ_ASSERT(IsDateTimeInputType(mType) ||
                 mType == FormControlType::InputNumber ||
                 mType == FormControlType::InputRange,
             "Check that kDefaultStepBase is correct for this new type");
  // Do NOT use GetMinimum here - the spec says to use "the min content
  // attribute", not "the minimum".
  nsAutoString minStr;
  if (GetAttr(nsGkAtoms::min, minStr)) {
    Decimal min =
        mInputType->ConvertStringToNumber(minStr, InputType::Localized::No)
            .mResult;
    if (min.isFinite()) {
      return min;
    }
  }

  // If @min is not a double, we should use @value.
  nsAutoString valueStr;
  if (GetAttr(nsGkAtoms::value, valueStr)) {
    Decimal value =
        mInputType->ConvertStringToNumber(valueStr, InputType::Localized::Yes)
            .mResult;
    if (value.isFinite()) {
      return value;
    }
  }

  if (mType == FormControlType::InputWeek) {
    return kDefaultStepBaseWeek;
  }

  return kDefaultStepBase;
}

void HTMLInputElement::GetColor(InputPickerColor& aValue) {
  MOZ_ASSERT(mType == FormControlType::InputColor,
             "getColor is only for type=color.");

  nsAutoString value;
  GetValue(value, CallerType::System);

  // We should pass colorspace info to the color picker (bug 2009748), but for
  // now we pass sRGB values.
  StyleAbsoluteColor color = MaybeComputeColorOrBlack(OwnerDoc(), value)
                                 .ToColorSpace(StyleColorSpace::Srgb);
  ClampColorComponents(color);
  aValue.mComponent1 = color.components._0;
  aValue.mComponent2 = color.components._1;
  aValue.mComponent3 = color.components._2;
  aValue.mAlpha = Alpha() ? color.alpha : NAN;

  // aValue.mColorSpace = mColorSpace;
}

// SetValueInternal is CAN_RUN_SCRIPT but only for text inputs.
MOZ_CAN_RUN_SCRIPT_BOUNDARY void HTMLInputElement::UpdateColor() {
  // https://html.spec.whatwg.org/#attr-input-colorspace
  // Whenever the element's alpha or colorspace attributes are changed, the user
  // agent must run update a color well control color given the element.
  // (But it involves setting value, which will run sanitization, which will
  // call the same function. So we just call Get/SetValue here.)
  if (mType != FormControlType::InputColor) {
    // This is only needed for color, basically no-op for others.
    return;
  }
  if (!mValueChanged) {
    SetDefaultValueAsValue();
    return;
  }
  nsAutoString value;
  GetValue(value, CallerType::NonSystem);
  SetValueInternal(value, {ValueSetterOption::ByInternalAPI});
}

void HTMLInputElement::SetUserInputColor(const InputPickerColor& aValue) {
  MOZ_ASSERT(mType == FormControlType::InputColor,
             "setUserInputColor is only for type=color.");

  nsAutoString serialized;
  SerializeColor(
      StyleAbsoluteColor{
          .components =
              StyleColorComponents{
                  ._0 = aValue.mComponent1,
                  ._1 = aValue.mComponent2,
                  ._2 = aValue.mComponent3,
              },
          .alpha = aValue.mAlpha,
          .color_space = StyleColorSpace::Srgb,
      },
      GetColorSpaceEnum(), Alpha(), serialized);

  // (We are either Chrome/UA but the principal doesn't matter for color inputs)
  SetUserInput(serialized, *NodePrincipal());
}

Decimal HTMLInputElement::GetValueIfStepped(int32_t aStep,
                                            StepCallerType aCallerType,
                                            ErrorResult& aRv) {
  constexpr auto kNaN = Decimal::nan();
  if (!DoStepDownStepUpApply()) {
    aRv.ThrowInvalidStateError("Step doesn't apply to this input type");
    return kNaN;
  }

  Decimal stepBase = GetStepBase();
  Decimal step = GetStep();
  if (step == kStepAny) {
    if (aCallerType != StepCallerType::ForUserEvent) {
      aRv.ThrowInvalidStateError("Can't step an input with step=\"any\"");
      return kNaN;
    }
    // Allow the spin buttons and up/down arrow keys to do something sensible:
    step = GetDefaultStep();
  }

  Decimal minimum = GetMinimum();
  Decimal maximum = GetMaximum();

  if (!maximum.isNaN()) {
    // "max - (max - stepBase) % step" is the nearest valid value to max.
    maximum = maximum - NS_floorModulo(maximum - stepBase, step);
    if (!minimum.isNaN()) {
      if (minimum > maximum) {
        // Either the minimum was greater than the maximum prior to our
        // adjustment to align maximum on a step, or else (if we adjusted
        // maximum) there is no valid step between minimum and the unadjusted
        // maximum.
        return kNaN;
      }
    }
  }

  Decimal value = GetValueAsDecimal();
  bool valueWasNaN = false;
  if (value.isNaN()) {
    value = Decimal(0);
    valueWasNaN = true;
  }
  Decimal valueBeforeStepping = value;

  Decimal deltaFromStep = NS_floorModulo(value - stepBase, step);

  if (deltaFromStep != Decimal(0)) {
    if (aStep > 0) {
      value += step - deltaFromStep;       // partial step
      value += step * Decimal(aStep - 1);  // then remaining steps
    } else if (aStep < 0) {
      value -= deltaFromStep;              // partial step
      value += step * Decimal(aStep + 1);  // then remaining steps
    }
  } else {
    value += step * Decimal(aStep);
  }

  if (value < minimum) {
    value = minimum;
    deltaFromStep = NS_floorModulo(value - stepBase, step);
    if (deltaFromStep != Decimal(0)) {
      value += step - deltaFromStep;
    }
  }
  if (value > maximum) {
    value = maximum;
    deltaFromStep = NS_floorModulo(value - stepBase, step);
    if (deltaFromStep != Decimal(0)) {
      value -= deltaFromStep;
    }
  }

  if (!valueWasNaN &&  // value="", resulting in us using "0"
      ((aStep > 0 && value < valueBeforeStepping) ||
       (aStep < 0 && value > valueBeforeStepping))) {
    // We don't want step-up to effectively step down, or step-down to
    // effectively step up, so return;
    return kNaN;
  }

  return value;
}

void HTMLInputElement::ApplyStep(int32_t aStep, ErrorResult& aRv) {
  Decimal nextStep = GetValueIfStepped(aStep, StepCallerType::ForScript, aRv);
  if (aRv.Failed() || !nextStep.isFinite()) {
    return;
  }
  // We know we're not a file input, so the caller type does not matter; just
  // pass "not system" to be safe.
  SetValue(nextStep, CallerType::NonSystem);
}

bool HTMLInputElement::IsDateTimeInputType(FormControlType aType) {
  switch (aType) {
    case FormControlType::InputDate:
    case FormControlType::InputTime:
    case FormControlType::InputMonth:
    case FormControlType::InputWeek:
    case FormControlType::InputDatetimeLocal:
      return true;
    default:
      return false;
  }
}

void HTMLInputElement::MozGetFileNameArray(nsTArray<nsString>& aArray,
                                           ErrorResult& aRv) {
  if (NS_WARN_IF(mType != FormControlType::InputFile)) {
    return;
  }

  const nsTArray<OwningFileOrDirectory>& filesOrDirs =
      GetFilesOrDirectoriesInternal();
  for (uint32_t i = 0; i < filesOrDirs.Length(); i++) {
    nsAutoString str;
    GetDOMFileOrDirectoryPath(filesOrDirs[i], str, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    aArray.AppendElement(str);
  }
}

void HTMLInputElement::MozSetFileArray(
    const Sequence<OwningNonNull<File>>& aFiles) {
  if (NS_WARN_IF(mType != FormControlType::InputFile)) {
    return;
  }

  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  MOZ_ASSERT(global);
  if (!global) {
    return;
  }

  nsTArray<OwningFileOrDirectory> files;
  for (uint32_t i = 0; i < aFiles.Length(); ++i) {
    RefPtr<File> file = File::Create(global, aFiles[i].get()->Impl());
    if (NS_WARN_IF(!file)) {
      return;
    }

    OwningFileOrDirectory* element = files.AppendElement();
    element->SetAsFile() = file;
  }

  SetFilesOrDirectories(files, true);
}

void HTMLInputElement::MozSetFileNameArray(const Sequence<nsString>& aFileNames,
                                           ErrorResult& aRv) {
  if (NS_WARN_IF(mType != FormControlType::InputFile)) {
    return;
  }

  if (XRE_IsContentProcess()) {
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return;
  }

  nsTArray<OwningFileOrDirectory> files;
  for (uint32_t i = 0; i < aFileNames.Length(); ++i) {
    nsCOMPtr<nsIFile> file;

    if (StringBeginsWith(aFileNames[i], u"file:"_ns,
                         nsASCIICaseInsensitiveStringComparator)) {
      // Converts the URL string into the corresponding nsIFile if possible
      // A local file will be created if the URL string begins with file://
      (void)NS_GetFileFromURLSpec(NS_ConvertUTF16toUTF8(aFileNames[i]),
                                  getter_AddRefs(file));
    }

    if (!file) {
      // this is no "file://", try as local file
      (void)NS_NewLocalFile(aFileNames[i], getter_AddRefs(file));
    }

    if (!file) {
      continue;  // Not much we can do if the file doesn't exist
    }

    nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
    if (NS_WARN_IF(!global)) {
      continue;
    }
    RefPtr<File> domFile = File::CreateFromFile(global, file);
    if (NS_WARN_IF(!domFile)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    OwningFileOrDirectory* element = files.AppendElement();
    element->SetAsFile() = domFile;
  }

  SetFilesOrDirectories(files, true);
}

void HTMLInputElement::MozSetDirectory(const nsAString& aDirectoryPath,
                                       ErrorResult& aRv) {
  if (NS_WARN_IF(mType != FormControlType::InputFile)) {
    return;
  }

  nsCOMPtr<nsIFile> file;
  aRv = NS_NewLocalFile(aDirectoryPath, getter_AddRefs(file));
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  nsIGlobalObject* global = GetRelevantGlobal();
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  RefPtr<Directory> directory = Directory::Create(global, file);
  MOZ_ASSERT(directory);

  nsTArray<OwningFileOrDirectory> array;
  OwningFileOrDirectory* element = array.AppendElement();
  element->SetAsDirectory() = directory;

  SetFilesOrDirectories(array, true);
}

void HTMLInputElement::GetDateTimeInputBoxValue(DateTimeValue& aValue) {
  if (NS_WARN_IF(!IsDateTimeInputType(mType)) || !mDateTimeInputBoxValue) {
    return;
  }

  aValue = *mDateTimeInputBoxValue;
}

Element* HTMLInputElement::GetDateTimeBoxElement() {
  if (!CreatesDateTimeWidget()) {
    return nullptr;
  }
  auto* sr = GetShadowRoot();
  if (!sr) {
    return nullptr;
  }
  // The datetimebox <div> is the only child of the UA Widget Shadow Root
  // if it is present, but note that during a type change it might not be set up
  // yet / this might be a previous shadow tree from e.g. a text input.
  MOZ_ASSERT(sr->IsUAWidget());

  nsIContent* inputAreaContent = sr->GetFirstChild();
  if (inputAreaContent && inputAreaContent->GetID() == nsGkAtoms::datetimebox) {
    return inputAreaContent->AsElement();
  }
  return nullptr;
}

void HTMLInputElement::OpenDateTimePicker(const DateTimeValue& aInitialValue) {
  if (NS_WARN_IF(!IsDateTimeInputType(mType))) {
    return;
  }

  mDateTimeInputBoxValue = MakeUnique<DateTimeValue>(aInitialValue);
  nsContentUtils::DispatchChromeEvent(OwnerDoc(), this,
                                      u"MozOpenDateTimePicker"_ns,
                                      CanBubble::eYes, Cancelable::eYes);
}

void HTMLInputElement::CloseDateTimePicker() {
  if (NS_WARN_IF(!IsDateTimeInputType(mType))) {
    return;
  }

  nsContentUtils::DispatchChromeEvent(OwnerDoc(), this,
                                      u"MozCloseDateTimePicker"_ns,
                                      CanBubble::eYes, Cancelable::eYes);
}

void HTMLInputElement::SetOpenState(bool aIsOpen) {
  SetStates(ElementState::OPEN, aIsOpen);
}

void HTMLInputElement::OpenColorPicker() {
  if (NS_WARN_IF(mType != FormControlType::InputColor)) {
    return;
  }

  nsContentUtils::DispatchChromeEvent(OwnerDoc(), this,
                                      u"MozOpenColorPicker"_ns, CanBubble::eYes,
                                      Cancelable::eYes);
}

void HTMLInputElement::SetFocusState(bool aIsFocused) {
  if (NS_WARN_IF(!IsDateTimeInputType(mType))) {
    return;
  }
  SetStates(ElementState::FOCUS | ElementState::FOCUSRING, aIsFocused);
}

void HTMLInputElement::UpdateValidityState() {
  if (NS_WARN_IF(!IsDateTimeInputType(mType))) {
    return;
  }

  // For now, datetime input box call this function only when the value may
  // become valid/invalid. For other validity states, they will be updated when
  // .value is actually changed.
  UpdateBadInputValidityState();
  UpdateValidityElementStates(true);
}

bool HTMLInputElement::MozIsTextField(bool aExcludePassword) {
  // TODO: temporary until bug 888320 is fixed.
  //
  // FIXME: Historically we never returned true for `number`, we should consider
  // changing that now that it is similar to other inputs.
  if (IsDateTimeInputType(mType) || mType == FormControlType::InputNumber) {
    return false;
  }

  return IsSingleLineTextControl(aExcludePassword);
}

void HTMLInputElement::SetUserInput(const nsAString& aValue,
                                    nsIPrincipal& aSubjectPrincipal) {
  AutoHandlingUserInputStatePusher inputStatePusher(true);

  if (mType == FormControlType::InputFile &&
      !aSubjectPrincipal.IsSystemPrincipal()) {
    return;
  }

  if (mType == FormControlType::InputFile) {
    Sequence<nsString> list;
    if (!list.AppendElement(aValue, fallible)) {
      return;
    }

    MozSetFileNameArray(list, IgnoreErrors());
    return;
  }

  bool isInputEventDispatchedByTextControlState =
      GetValueMode() == VALUE_MODE_VALUE && IsSingleLineTextControl(false);

  nsresult rv = SetValueInternal(
      aValue,
      {ValueSetterOption::BySetUserInputAPI, ValueSetterOption::SetValueChanged,
       ValueSetterOption::MoveCursorToEndIfValueChanged});
  NS_ENSURE_SUCCESS_VOID(rv);

  if (!isInputEventDispatchedByTextControlState) {
    DebugOnly<nsresult> rvIgnored = nsContentUtils::DispatchInputEvent(this);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "Failed to dispatch input event");
  }

  // If this element is not currently focused, it won't receive a change event
  // for this update through the normal channels. So fire a change event
  // immediately, instead.
  if (CreatesDateTimeWidget() || !ShouldBlur(this)) {
    FireChangeEventIfNeeded();
  }
}

nsIEditor* HTMLInputElement::GetEditorForBindings() {
  if (!GetPrimaryFrame()) {
    // Ensure we construct frames (and thus an editor) if needed.
    GetPrimaryFrame(FlushType::Frames);
  }
  return GetTextEditorFromState();
}

bool HTMLInputElement::HasEditor() const { return !!GetExtantTextEditor(); }

TextEditor* HTMLInputElement::GetTextEditorFromState() {
  TextControlState* state = GetEditorState();
  if (state) {
    return state->GetTextEditor();
  }
  return nullptr;
}

TextEditor* HTMLInputElement::GetTextEditor() {
  return GetTextEditorFromState();
}

TextEditor* HTMLInputElement::GetExtantTextEditor() const {
  const TextControlState* const state = GetEditorState();
  if (!state) {
    return nullptr;
  }
  return state->GetExtantTextEditor();
}

nsISelectionController* HTMLInputElement::GetSelectionController() {
  TextControlState* state = GetEditorState();
  if (state) {
    return state->GetSelectionController();
  }
  return nullptr;
}

nsFrameSelection* HTMLInputElement::GetIndependentFrameSelection() const {
  TextControlState* state = GetEditorState();
  if (state) {
    return state->GetIndependentFrameSelection();
  }
  return nullptr;
}

void HTMLInputElement::GetDisplayFileName(nsAString& aValue) const {
  MOZ_ASSERT(mFileData);

  if (OwnerDoc()->IsStaticDocument()) {
    aValue = mFileData->mStaticDocFileList;
    return;
  }

  if (mFileData->mFilesOrDirectories.Length() == 1) {
    GetDOMFileOrDirectoryName(mFileData->mFilesOrDirectories[0], aValue);
    return;
  }

  nsAutoString value;

  if (mFileData->mFilesOrDirectories.IsEmpty()) {
    if (StaticPrefs::dom_webkitBlink_dirPicker_enabled() &&
        HasAttr(nsGkAtoms::webkitdirectory)) {
      nsContentUtils::GetMaybeLocalizedString(
          PropertiesFile::FORMS_PROPERTIES, "NoDirSelected", OwnerDoc(), value);
    } else if (HasAttr(nsGkAtoms::multiple)) {
      nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                              "NoFilesSelected", OwnerDoc(),
                                              value);
    } else {
      nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                              "NoFileSelected", OwnerDoc(),
                                              value);
    }
  } else {
    nsString count;
    count.AppendInt(int(mFileData->mFilesOrDirectories.Length()));

    nsContentUtils::FormatMaybeLocalizedString(
        value, PropertiesFile::FORMS_PROPERTIES, "XFilesSelected", OwnerDoc(),
        count);
  }

  aValue = std::move(value);
}

const nsTArray<OwningFileOrDirectory>&
HTMLInputElement::GetFilesOrDirectoriesInternal() const {
  return mFileData->mFilesOrDirectories;
}

void HTMLInputElement::SetFilesOrDirectories(
    const nsTArray<OwningFileOrDirectory>& aFilesOrDirectories,
    bool aSetValueChanged) {
  if (NS_WARN_IF(mType != FormControlType::InputFile)) {
    return;
  }

  MOZ_ASSERT(mFileData);

  mFileData->ClearGetFilesHelpers();

  if (StaticPrefs::dom_webkitBlink_filesystem_enabled()) {
    HTMLInputElement_Binding::ClearCachedWebkitEntriesValue(this);
    mFileData->mEntries.Clear();
  }

  mFileData->mFilesOrDirectories.Clear();
  mFileData->mFilesOrDirectories.AppendElements(aFilesOrDirectories);

  AfterSetFilesOrDirectories(aSetValueChanged);
}

void HTMLInputElement::SetFiles(FileList* aFiles, bool aSetValueChanged) {
  MOZ_ASSERT(mFileData);

  mFileData->mFilesOrDirectories.Clear();
  mFileData->ClearGetFilesHelpers();

  if (StaticPrefs::dom_webkitBlink_filesystem_enabled()) {
    HTMLInputElement_Binding::ClearCachedWebkitEntriesValue(this);
    mFileData->mEntries.Clear();
  }

  if (aFiles) {
    uint32_t listLength = aFiles->Length();
    for (uint32_t i = 0; i < listLength; i++) {
      OwningFileOrDirectory* element =
          mFileData->mFilesOrDirectories.AppendElement();
      element->SetAsFile() = aFiles->Item(i);
    }
  }

  AfterSetFilesOrDirectories(aSetValueChanged);
}

// This method is used for testing only.
void HTMLInputElement::MozSetDndFilesAndDirectories(
    const nsTArray<OwningFileOrDirectory>& aFilesOrDirectories) {
  if (NS_WARN_IF(mType != FormControlType::InputFile)) {
    return;
  }

  SetFilesOrDirectories(aFilesOrDirectories, true);

  if (StaticPrefs::dom_webkitBlink_filesystem_enabled()) {
    UpdateEntries(aFilesOrDirectories);
  }

  RefPtr<DispatchChangeEventCallback> dispatchChangeEventCallback =
      new DispatchChangeEventCallback(this);

  if (StaticPrefs::dom_webkitBlink_dirPicker_enabled() &&
      HasAttr(nsGkAtoms::webkitdirectory)) {
    ErrorResult rv;
    GetFilesHelper* helper =
        GetOrCreateGetFilesHelper(true /* recursionFlag */, rv);
    if (NS_WARN_IF(rv.Failed())) {
      rv.SuppressException();
      return;
    }

    helper->AddCallback(dispatchChangeEventCallback);
  } else {
    dispatchChangeEventCallback->DispatchEvents();
  }
}

void HTMLInputElement::AfterSetFilesOrDirectories(bool aSetValueChanged) {
  // No need to flush here, if there's no frame at this point we
  // don't need to force creation of one just to tell it about this
  // new value.  We just want the display to update as needed.
  if (nsFileControlFrame* f = do_QueryFrame(GetPrimaryFrame())) {
    f->SelectedFilesUpdated();
  }

  // Grab the full path here for any chrome callers who access our .value via a
  // CPOW. This path won't be called from a CPOW meaning the potential sync IPC
  // call under GetMozFullPath won't be rejected for not being urgent.
  if (mFileData->mFilesOrDirectories.IsEmpty()) {
    mFileData->mFirstFilePath.Truncate();
  } else {
    ErrorResult rv;
    GetDOMFileOrDirectoryPath(mFileData->mFilesOrDirectories[0],
                              mFileData->mFirstFilePath, rv);
    if (NS_WARN_IF(rv.Failed())) {
      rv.SuppressException();
    }
  }

  // Null out |mFileData->mFileList| to return a new file list when asked for.
  // Don't clear it since the file list might come from the user via SetFiles.
  if (mFileData->mFileList) {
    mFileData->mFileList = nullptr;
  }

  if (aSetValueChanged) {
    SetValueChanged(true);
  }

  UpdateAllValidityStates(true);
}

void HTMLInputElement::FireChangeEventIfNeeded() {
  if (!MayFireChangeOnBlur()) {
    return;
  }

  // We're not exposing the GetValue return value anywhere here, so it's safe to
  // claim to be a system caller.
  nsAutoString value;
  GetValue(value, CallerType::System);

  // NOTE(emilio): Per spec we should not set this if we don't fire the change
  // event, but that seems like a bug. Using mValueChanged seems reasonable to
  // keep the expected behavior while
  // https://github.com/whatwg/html/issues/10013 is resolved.
  if (mValueChanged) {
    SetUserInteracted(true);
  }
  const bool changedByUser = mUserChangedSinceFocus;
  mUserChangedSinceFocus = false;
  mIsUserInteracting = false;
  if (mFocusedValue.Equals(value)) {
    return;
  }
  mFocusedValue = std::move(value);
  if (!changedByUser) {
    // value was changed, but only by scripts
    return;
  }
  // Dispatch the change event.
  nsContentUtils::DispatchTrustedEvent(
      OwnerDoc(), static_cast<nsIContent*>(this), u"change"_ns, CanBubble::eYes,
      Cancelable::eNo);
}

FileList* HTMLInputElement::GetFiles() {
  if (mType != FormControlType::InputFile) {
    return nullptr;
  }

  if (!mFileData->mFileList) {
    mFileData->mFileList = new FileList(static_cast<nsIContent*>(this));
    for (const OwningFileOrDirectory& item : GetFilesOrDirectoriesInternal()) {
      if (item.IsFile()) {
        mFileData->mFileList->Append(item.GetAsFile());
      }
    }
  }

  return mFileData->mFileList;
}

void HTMLInputElement::SetFiles(FileList* aFiles) {
  if (mType != FormControlType::InputFile || !aFiles) {
    return;
  }

  // Update |mFileData->mFilesOrDirectories|
  SetFiles(aFiles, true);

  MOZ_ASSERT(!mFileData->mFileList, "Should've cleared the existing file list");

  // Update |mFileData->mFileList| without copy
  mFileData->mFileList = aFiles;
}

/* static */
void HTMLInputElement::HandleNumberControlSpin(void* aData) {
  RefPtr input = static_cast<HTMLInputElement*>(aData);
  NS_ASSERTION(input->mNumberControlSpinnerIsSpinning,
               "Should have called nsRepeatService::Stop()");

  if (input->mType != FormControlType::InputNumber ||
      !input->GetPrimaryFrame()) {
    // Type has changed (and possibly our frame type hasn't been updated yet)
    // or else we've lost our frame. Either way, stop the timer and don't do
    // anything else.
    input->StopNumberControlSpinnerSpin();
  } else {
    input->StepNumberControlForUserEvent(
        input->mNumberControlSpinnerSpinsUp ? 1 : -1);
  }
}

nsresult HTMLInputElement::SetValueInternal(
    const nsAString& aValue, const nsAString* aOldValue,
    const ValueSetterOptions& aOptions) {
  MOZ_ASSERT(GetValueMode() != VALUE_MODE_FILENAME,
             "Don't call SetValueInternal for file inputs");

  // We want to remember if the SetValueInternal() call is being made for a XUL
  // element.  We do that by looking at the parent node here, and if that node
  // is a XUL node, we consider our control a XUL control. XUL controls preserve
  // edit history across value setters.
  //
  // TODO(emilio): Rather than doing this maybe add an attribute instead and
  // read it only on chrome docs or something? That'd allow front-end code to
  // move away from xul without weird side-effects.
  const bool forcePreserveUndoHistory = mParent && mParent->IsXULElement();

  if (aOptions.contains(ValueSetterOption::BySetUserInputAPI)) {
    mUserChangedSinceFocus = true;
  }

  switch (GetValueMode()) {
    case VALUE_MODE_VALUE: {
      // At the moment, only single line text control have to sanitize their
      // value Because we have to create a new string for that, we should
      // prevent doing it if it's useless.
      nsAutoString value(aValue);

      if (mDoneCreating &&
          !(mType == FormControlType::InputNumber &&
            aOptions.contains(ValueSetterOption::BySetUserInputAPI))) {
        // When the value of a number input is set by a script, we need to make
        // sure the value is a valid floating-point number.
        // https://html.spec.whatwg.org/#valid-floating-point-number
        // When it's set by a user, however, we need to be more permissive, so
        // we don't sanitize its value here. See bug 1839572.
        SanitizeValue(value, SanitizationKind::ForValueSetter);
      }
      // else DoneCreatingElement calls us again once mDoneCreating is true

      const bool setValueChanged =
          aOptions.contains(ValueSetterOption::SetValueChanged);
      if (setValueChanged) {
        SetValueChanged(true);
      }

      if (IsSingleLineTextControl(false)) {
        // Note that if aOptions includes
        // ValueSetterOption::BySetUserInputAPI, "input" event is automatically
        // dispatched by TextControlState::SetValue(). If you'd change condition
        // of calling this method, you need to maintain SetUserInput() too. FYI:
        // After calling SetValue(), the input type might have been
        //      modified so that mInputData may not store TextControlState.
        EnsureEditorState();
        if (!mInputData.mState->SetValue(
                value, aOldValue,
                forcePreserveUndoHistory
                    ? aOptions + ValueSetterOption::PreserveUndoHistory
                    : aOptions)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
        // If the caller won't dispatch "input" event via
        // nsContentUtils::DispatchInputEvent(), we need to modify
        // validationMessage value here.
        //
        // FIXME(emilio): ValueSetterOption::ByInternalAPI is not supposed to
        // change state, but maybe we could run this too?
        if (aOptions.contains(ValueSetterOption::ByContentAPI)) {
          MaybeUpdateAllValidityStates(!mDoneCreating);
        }
      } else {
        free(mInputData.mValue);
        mInputData.mValue = ToNewUnicode(value);
        if (setValueChanged) {
          SetValueChanged(true);
        }
        if (mType == FormControlType::InputRange) {
          nsRangeFrame* frame = do_QueryFrame(GetPrimaryFrame());
          if (frame) {
            frame->UpdateForValueChange();
          }
        } else if (CreatesDateTimeWidget() &&
                   !aOptions.contains(ValueSetterOption::BySetUserInputAPI)) {
          if (Element* dateTimeBoxElement = GetDateTimeBoxElement()) {
            AsyncEventDispatcher::RunDOMEventWhenSafe(
                *dateTimeBoxElement, u"MozDateTimeValueChanged"_ns,
                CanBubble::eNo, ChromeOnlyDispatch::eNo);
          }
        }
        if (mDoneCreating) {
          OnValueChanged(ValueChangeKind::Internal, value.IsEmpty(), &value);
        }
        // else DoneCreatingElement calls us again once mDoneCreating is true
      }

      if (mType == FormControlType::InputColor) {
        // Update color frame, to reflect color changes
        nsColorControlFrame* colorControlFrame =
            do_QueryFrame(GetPrimaryFrame());
        if (colorControlFrame) {
          AutoWeakFrame weakFrame(colorControlFrame);
          colorControlFrame->UpdateColor();
#ifdef ACCESSIBILITY
          if (weakFrame.IsAlive()) {
            if (nsAccessibilityService* accService = GetAccService()) {
              accService->ColorValueChanged(colorControlFrame->PresShell(),
                                            this);
            }
          }
#endif
        }
      }
      return NS_OK;
    }

    case VALUE_MODE_DEFAULT:
    case VALUE_MODE_DEFAULT_ON:
      // If the value of a hidden input was changed, we mark it changed so that
      // we will know we need to save / restore the value.  Yes, we are
      // overloading the meaning of ValueChanged just a teensy bit to save a
      // measly byte of storage space in HTMLInputElement.  Yes, you are free to
      // make a new flag, NEED_TO_SAVE_VALUE, at such time as mBitField becomes
      // a 16-bit value.
      if (mType == FormControlType::InputHidden) {
        SetValueChanged(true);
      }

      // Make sure to keep track of the last value change not being interactive,
      // just in case this used to be another kind of editable input before.
      // Note that a checked change _could_ really be interactive, but we don't
      // keep track of that elsewhere so seems fine to just do this.
      SetLastValueChangeWasInteractive(false);

      // Treat value == defaultValue for other input elements.
      return nsGenericHTMLFormControlElementWithState::SetAttr(
          kNameSpaceID_None, nsGkAtoms::value, aValue, true);

    case VALUE_MODE_FILENAME:
      return NS_ERROR_UNEXPECTED;
  }

  // This return statement is required for some compilers.
  return NS_OK;
}

void HTMLInputElement::SetValueChanged(bool aValueChanged) {
  if (mValueChanged == aValueChanged) {
    return;
  }
  mValueChanged = aValueChanged;
  UpdateTooLongValidityState();
  UpdateTooShortValidityState();
  UpdateValidityElementStates(true);
}

void HTMLInputElement::SetLastValueChangeWasInteractive(bool aWasInteractive) {
  if (aWasInteractive == mLastValueChangeWasInteractive) {
    return;
  }
  mLastValueChangeWasInteractive = aWasInteractive;
  const bool wasValid = IsValid();
  UpdateTooLongValidityState();
  UpdateTooShortValidityState();
  if (wasValid != IsValid()) {
    UpdateValidityElementStates(true);
  }
}

void HTMLInputElement::SetCheckedChanged(bool aCheckedChanged) {
  if (mType == FormControlType::InputRadio) {
    if (mCheckedChanged != aCheckedChanged) {
      VisitGroup(
          [aCheckedChanged](HTMLInputElement* aRadio) {
            aRadio->SetCheckedChangedInternal(aCheckedChanged);
            return true;
          },
          false);
    }
  } else {
    SetCheckedChangedInternal(aCheckedChanged);
  }
}

void HTMLInputElement::SetCheckedChangedInternal(bool aCheckedChanged) {
  if (mCheckedChanged == aCheckedChanged) {
    return;
  }
  mCheckedChanged = aCheckedChanged;
  UpdateValidityElementStates(true);
}

void HTMLInputElement::SetChecked(bool aChecked) {
  DoSetChecked(aChecked, /* aNotify */ true, /* aSetValueChanged */ true);
}

void HTMLInputElement::DoSetChecked(bool aChecked, bool aNotify,
                                    bool aSetValueChanged,
                                    bool aUpdateOtherElement) {
  // If the user or JS attempts to set checked, whether it actually changes the
  // value or not, we say the value was changed so that defaultValue don't
  // affect it no more.
  if (aSetValueChanged) {
    SetCheckedChanged(true);
  }

  // Don't do anything if we're not changing whether it's checked (it would
  // screw up state actually, especially when you are setting radio button to
  // false)
  if (mChecked == aChecked) {
    return;
  }

  // Set checked
  if (mType != FormControlType::InputRadio) {
    SetCheckedInternal(aChecked, aNotify);
    return;
  }

  // For radio button, we need to do some extra fun stuff
  if (aChecked) {
    RadioSetChecked(aNotify, aUpdateOtherElement);
    return;
  }

  if (auto* container = GetCurrentRadioGroupContainer()) {
    nsAutoString name;
    GetAttr(nsGkAtoms::name, name);
    container->SetCurrentRadioButton(name, nullptr);
  }
  // SetCheckedInternal is going to ask all radios to update their
  // validity state. We have to be sure the radio group container knows
  // the currently selected radio.
  SetCheckedInternal(false, aNotify);
}

void HTMLInputElement::RadioSetChecked(bool aNotify, bool aUpdateOtherElement) {
  if (aUpdateOtherElement) {
    // It’s possible for multiple radio input to have their checkedness set to
    // true, so we need to deselect all of them.
    VisitGroup([](HTMLInputElement* aRadio) {
      aRadio->SetCheckedInternal(false, true, false);
      return true;
    });
  }

  // Let the group know that we are now the One True Radio Button
  if (auto* container = GetCurrentRadioGroupContainer()) {
    nsAutoString name;
    GetAttr(nsGkAtoms::name, name);
    container->SetCurrentRadioButton(name, this);
  }

  // SetCheckedInternal is going to ask all radios to update their
  // validity state.
  SetCheckedInternal(true, aNotify);
}

RadioGroupContainer* HTMLInputElement::GetCurrentRadioGroupContainer() const {
  NS_ASSERTION(
      mType == FormControlType::InputRadio,
      "GetRadioGroupContainer should only be called when type='radio'");
  return mRadioGroupContainer;
}

RadioGroupContainer* HTMLInputElement::FindTreeRadioGroupContainer() const {
  nsAutoString name;
  GetAttr(nsGkAtoms::name, name);

  if (name.IsEmpty()) {
    return nullptr;
  }
  if (mForm) {
    return &mForm->OwnedRadioGroupContainer();
  }
  if (IsInNativeAnonymousSubtree()) {
    return nullptr;
  }
  if (Document* doc = GetUncomposedDoc()) {
    return &doc->OwnedRadioGroupContainer();
  }
  return &static_cast<FragmentOrElement*>(SubtreeRoot())
              ->OwnedRadioGroupContainer();
}

void HTMLInputElement::DisconnectRadioGroupContainer() {
  mRadioGroupContainer = nullptr;
}

HTMLInputElement* HTMLInputElement::GetSelectedRadioButton() const {
  auto* container = GetCurrentRadioGroupContainer();
  if (!container) {
    return nullptr;
  }

  nsAutoString name;
  GetAttr(nsGkAtoms::name, name);

  return container->GetCurrentRadioButton(name);
}

void HTMLInputElement::MaybeSubmitForm(nsPresContext* aPresContext) {
  if (!mForm) {
    // Nothing to do here.
    return;
  }

  RefPtr<PresShell> presShell = aPresContext->GetPresShell();
  if (!presShell) {
    return;
  }

  // Get the default submit element
  if (RefPtr<nsGenericHTMLFormElement> submitContent =
          mForm->GetDefaultSubmitElement()) {
    WidgetPointerEvent event(true, ePointerClick, nullptr);
    event.mInputSource = MouseEvent_Binding::MOZ_SOURCE_KEYBOARD;
    // pointerId definition in Pointer Events:
    // > The pointerId value of -1 MUST be reserved and used to indicate events
    // > that were generated by something other than a pointing device.
    event.pointerId = -1;
    nsEventStatus status = nsEventStatus_eIgnore;
    presShell->HandleDOMEventWithTarget(submitContent, &event, &status);
  } else if (!mForm->ImplicitSubmissionIsDisabled()) {
    // If there's only one text control, just submit the form
    // Hold strong ref across the event
    RefPtr<dom::HTMLFormElement> form(mForm);
    form->MaybeSubmit(nullptr);
  }
}

void HTMLInputElement::UpdateCheckedState(bool aNotify) {
  SetStates(ElementState::CHECKED, IsRadioOrCheckbox() && mChecked, aNotify);
}

void HTMLInputElement::UpdateIndeterminateState(bool aNotify) {
  bool indeterminate = [&] {
    if (mType == FormControlType::InputCheckbox) {
      return mIndeterminate;
    }
    if (mType == FormControlType::InputRadio) {
      return !mChecked && !GetSelectedRadioButton();
    }
    return false;
  }();
  SetStates(ElementState::INDETERMINATE, indeterminate, aNotify);
}

void HTMLInputElement::SetCheckedInternal(bool aChecked, bool aNotify,
                                          bool aUpdateRadioGroup) {
  // Set the value
  mChecked = aChecked;

  if (IsRadioOrCheckbox()) {
    SetStates(ElementState::CHECKED, aChecked, aNotify);
  }

  // No need to update element state, since we're about to call
  // UpdateState anyway.
  UpdateAllValidityStatesButNotElementState();
  UpdateIndeterminateState(aNotify);
  UpdateValidityElementStates(aNotify);

  // Notify all radios in the group that value has changed, this is to let
  // radios to have the chance to update its states, e.g., :indeterminate.
  if (mType == FormControlType::InputRadio && aUpdateRadioGroup) {
    UpdateRadioGroupState();
  }
}

#if !defined(ANDROID) && !defined(XP_MACOSX)
bool HTMLInputElement::IsNodeApzAwareInternal() const {
  // Tell APZC we may handle mouse wheel event and do preventDefault when input
  // type is number.
  return mType == FormControlType::InputNumber ||
         mType == FormControlType::InputRange ||
         nsINode::IsNodeApzAwareInternal();
}
#endif

bool HTMLInputElement::IsInteractiveHTMLContent() const {
  return mType != FormControlType::InputHidden ||
         nsGenericHTMLFormControlElementWithState::IsInteractiveHTMLContent();
}

void HTMLInputElement::AsyncEventRunning(AsyncEventDispatcher* aEvent) {
  nsImageLoadingContent::AsyncEventRunning(aEvent);
}

void HTMLInputElement::Select() {
  if (!IsSingleLineTextControl(false)) {
    return;
  }

  if (FocusState() != FocusTristate::eUnfocusable) {
    TextControlState* state = GetEditorState();
    MOZ_ASSERT(state, "Single line text controls are expected to have a state");
    RefPtr<nsFrameSelection> fs = state->GetIndependentFrameSelection();
    if (fs && fs->MouseDownRecorded()) {
      // This means that we're being called while the frame selection has a
      // mouse down event recorded to adjust the caret during the mouse up
      // event. We are probably called from the focus event handler.  We should
      // override the delayed caret data in this case to ensure that this
      // select() call takes effect.
      fs->SetDelayedCaretData(nullptr);
    }

    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      fm->SetFocus(this, nsIFocusManager::FLAG_NOSCROLL);
    }
  }

  SelectAll();
}

bool HTMLInputElement::NeedToInitializeEditorForEvent(
    EventChainPreVisitor& aVisitor) const {
  // We only need to initialize the editor for single line input controls
  // because they are lazily initialized.  We don't need to initialize the
  // control for certain types of events, because we know that those events are
  // safe to be handled without the editor being initialized.  These events
  // include: mousein/move/out, overflow/underflow, and void events. Void events
  // are dispatched frequently by async keyboard scrolling to focused elements,
  // so it's important to handle them to prevent excessive DOM mutations.
  if (!IsSingleLineTextControl(false)) {
    return false;
  }

  return TextControlElement::NeedToInitializeEditorForEvent(aVisitor);
}

bool HTMLInputElement::IsDisabledForEvents(WidgetEvent* aEvent) {
  return IsElementDisabledForEvents(aEvent, GetPrimaryFrame());
}

bool HTMLInputElement::CheckActivationBehaviorPreconditions(
    EventChainVisitor& aVisitor) const {
  // Track whether we're in the outermost Dispatch invocation that will
  // cause activation of the input.  That is, if we're a click event, or a
  // DOMActivate that was dispatched directly, this will be set, but if
  // we're a DOMActivate dispatched from click handling, it will not be set.
  WidgetMouseEvent* mouseEvent = aVisitor.mEvent->AsMouseEvent();
  bool outerActivateEvent =
      (mouseEvent && mouseEvent->IsLeftClickEvent()) ||
      (aVisitor.mEvent->mMessage == eLegacyDOMActivate && !mInInternalActivate);
  if (outerActivateEvent) {
    aVisitor.mItemFlags |= NS_OUTER_ACTIVATE_EVENT;
  }
  return outerActivateEvent;
}

enum class SpinnerDirection { Up, Down, None };
static SpinnerDirection SpinnerDirectionForEvent(const WidgetEvent& aEvent,
                                                 Element* aSpinBox) {
  if (!aSpinBox) {
    return SpinnerDirection::None;
  }
  MOZ_ASSERT(aSpinBox->GetPseudoElementType() ==
             PseudoStyleType::MozNumberSpinBox);
  if (aEvent.mOriginalTarget == aSpinBox->GetFirstChild()) {
    MOZ_ASSERT(aSpinBox->GetFirstChild()->AsElement()->GetPseudoElementType() ==
               PseudoStyleType::MozNumberSpinUp);
    return SpinnerDirection::Up;
  }
  if (aEvent.mOriginalTarget == aSpinBox->GetLastChild()) {
    MOZ_ASSERT(aSpinBox->GetLastChild()->AsElement()->GetPseudoElementType() ==
               PseudoStyleType::MozNumberSpinDown);
    return SpinnerDirection::Down;
  }
  return SpinnerDirection::None;
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY
void HTMLInputElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  // Do not process any DOM events if the element is disabled
  aVisitor.mCanHandle = false;
  if (IsDisabledForEvents(aVisitor.mEvent)) {
    return;
  }

  // Initialize the editor if needed.
  if (NeedToInitializeEditorForEvent(aVisitor)) {
    if (auto* state = GetTextControlState()) {
      // FIXME(bug 2020902): This is rather evil. Remove
      // CAN_RUN_SCRIPT_BOUNDARY when removing this.
      state->EnsureEditorInitialized();
    }
  }

  if (CheckActivationBehaviorPreconditions(aVisitor)) {
    aVisitor.mWantsActivationBehavior = true;
  }

  // We must cache type because mType may change during JS event (bug 2369)
  aVisitor.mItemFlags |= uint8_t(mType);

  if (aVisitor.mEvent->mMessage == eFocus && aVisitor.mEvent->IsTrusted() &&
      MayFireChangeOnBlur() &&
      // StartRangeThumbDrag already set mFocusedValue on 'mousedown' before
      // we get the 'focus' event.
      !mIsDraggingRange) {
    GetValue(mFocusedValue, CallerType::System);
  }

  // Fire onchange (if necessary), before we do the blur, bug 357684.
  if (aVisitor.mEvent->mMessage == eBlur) {
    // We set NS_PRE_HANDLE_BLUR_EVENT here and handle it in PreHandleEvent to
    // prevent breaking event target chain creation.
    aVisitor.mWantsPreHandleEvent = true;
    aVisitor.mItemFlags |= NS_PRE_HANDLE_BLUR_EVENT;
  }

  if (mType == FormControlType::InputRange &&
      (aVisitor.mEvent->mMessage == eFocus ||
       aVisitor.mEvent->mMessage == eBlur)) {
    // We handle focus here.
    // FIXME(emilio): Why is this needed? If it is it should be moved to
    // nsRangeFrame::ElementStateChanged.
    if (nsIFrame* frame = GetPrimaryFrame()) {
      frame->InvalidateFrameSubtree();
    }
  }

  if (mType == FormControlType::InputNumber && aVisitor.mEvent->IsTrusted()) {
    if (mNumberControlSpinnerIsSpinning) {
      // If the timer is running the user has depressed the mouse on one of the
      // spin buttons. If the mouse exits the button we either want to reverse
      // the direction of spin if it has moved over the other button, or else
      // we want to end the spin. We do this here (rather than in
      // PostHandleEvent) because we don't want to let content preventDefault()
      // the end of the spin.
      if (aVisitor.mEvent->mMessage == eMouseMove) {
        // Be aggressive about stopping the spin:
        bool stopSpin = true;
        switch (
            SpinnerDirectionForEvent(*aVisitor.mEvent, GetTextEditorButton())) {
          case SpinnerDirection::Up:
            mNumberControlSpinnerSpinsUp = true;
            stopSpin = false;
            break;
          case SpinnerDirection::Down:
            mNumberControlSpinnerSpinsUp = false;
            stopSpin = false;
            break;
          case SpinnerDirection::None:
            break;
        }
        if (stopSpin) {
          StopNumberControlSpinnerSpin();
        }
      } else if (aVisitor.mEvent->mMessage == eMouseUp) {
        StopNumberControlSpinnerSpin();
      }
    }

    if (StaticPrefs::dom_input_number_and_range_modified_by_mousewheel() &&
        aVisitor.mEvent->mMessage == eWheel) {
      aVisitor.mMaybeUncancelable = false;
    }
  }

  nsGenericHTMLFormControlElementWithState::GetEventTargetParent(aVisitor);
}

void HTMLInputElement::LegacyPreActivationBehavior(
    EventChainVisitor& aVisitor) {
  //
  // Web pages expect the value of a radio button or checkbox to be set
  // *before* onclick and DOMActivate fire, and they expect that if they set
  // the value explicitly during onclick or DOMActivate it will not be toggled
  // or any such nonsense.
  // In order to support that (bug 57137 and 58460 are examples) we toggle
  // the checked attribute *first*, and then fire onclick.  If the user
  // returns false, we reset the control to the old checked value.  Otherwise,
  // we dispatch DOMActivate.  If DOMActivate is cancelled, we also reset
  // the control to the old checked value.  We need to keep track of whether
  // we've already toggled the state from onclick since the user could
  // explicitly dispatch DOMActivate on the element.
  //
  // These are compatibility hacks and are defined as legacy-pre-activation
  // and legacy-canceled-activation behavior in HTML.
  //

  // Assert mType didn't change after GetEventTargetParent
  MOZ_ASSERT(NS_CONTROL_TYPE(aVisitor.mItemFlags) == uint8_t(mType));

  bool originalCheckedValue = false;
  mCheckedIsToggled = false;

  if (mType == FormControlType::InputCheckbox) {
    if (mIndeterminate) {
      // indeterminate is always set to FALSE when the checkbox is toggled
      SetIndeterminateInternal(false, false);
      aVisitor.mItemFlags |= NS_ORIGINAL_INDETERMINATE_VALUE;
    }

    originalCheckedValue = Checked();
    DoSetChecked(!originalCheckedValue, /* aNotify */ true,
                 /* aSetValueChanged */ true);
    mCheckedIsToggled = true;

    if (aVisitor.mEventStatus != nsEventStatus_eConsumeNoDefault) {
      aVisitor.mEventStatus = nsEventStatus_eConsumeDoDefault;
    }
  } else if (mType == FormControlType::InputRadio) {
    HTMLInputElement* selectedRadioButton = GetSelectedRadioButton();
    aVisitor.mItemData = static_cast<Element*>(selectedRadioButton);

    originalCheckedValue = Checked();
    if (!originalCheckedValue) {
      DoSetChecked(/* aValue */ true, /* aNotify */ true,
                   /* aSetValueChanged */ true);
      mCheckedIsToggled = true;
    }

    if (aVisitor.mEventStatus != nsEventStatus_eConsumeNoDefault) {
      aVisitor.mEventStatus = nsEventStatus_eConsumeDoDefault;
    }
  }

  if (originalCheckedValue) {
    aVisitor.mItemFlags |= NS_ORIGINAL_CHECKED_VALUE;
  }

  // out-of-spec legacy pre-activation behavior needed because of bug 1803805.
  // XXXedgar: We exclude the radio type because `mItemData` is already used to
  // store the originally selected radio button above.
  if (mForm && mType != FormControlType::InputRadio) {
    aVisitor.mItemFlags |= NS_IN_SUBMIT_CLICK;
    aVisitor.mItemData = static_cast<Element*>(mForm);
    // tell the form that we are about to enter a click handler.
    // that means that if there are scripted submissions, the
    // latest one will be deferred until after the exit point of the
    // handler.
    mForm->OnSubmitClickBegin();

    if ((mType == FormControlType::InputSubmit ||
         mType == FormControlType::InputImage) &&
        aVisitor.mDOMEvent) {
      if (auto* mouseEvent = aVisitor.mDOMEvent->AsMouseEvent()) {
        const CSSIntPoint pt = RoundedToInt(mouseEvent->OffsetPoint());
        if (auto* imageClickedPoint = static_cast<CSSIntPoint*>(
                GetProperty(nsGkAtoms::imageClickedPoint))) {
          // Ensures that a dispatched event's clicked point is not the default
          // value.
          *imageClickedPoint = pt;
        }
      }
    }
  }
}

void HTMLInputElement::MaybeDispatchWillBlur(EventChainVisitor& aVisitor) {
  if (!CreatesDateTimeWidget() || !aVisitor.mEvent->IsTrusted()) {
    return;
  }
  RefPtr<Element> dateTimeBoxElement = GetDateTimeBoxElement();
  if (!dateTimeBoxElement) {
    return;
  }
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(GetRelevantGlobal()))) {
    return;
  }
  if (!aVisitor.mDOMEvent) {
    RefPtr<Event> event = EventDispatcher::CreateEvent(
        aVisitor.mEvent->mOriginalTarget, aVisitor.mPresContext,
        aVisitor.mEvent, u""_ns);
    event.swap(aVisitor.mDOMEvent);
  }
  JS::Rooted<JS::Value> detail(jsapi.cx(), JS::NullHandleValue);
  if (NS_WARN_IF(!ToJSValue(jsapi.cx(), aVisitor.mDOMEvent, &detail))) {
    return;
  }
  // Event is dispatched to closed-shadow tree and doesn't bubble.
  RefPtr<CustomEvent> event =
      NS_NewDOMCustomEvent(OwnerDoc(), aVisitor.mPresContext, nullptr);
  event->InitCustomEvent(jsapi.cx(), u"MozDateTimeWillBlur"_ns,
                         /* CanBubble */ false,
                         /* Cancelable */ false, detail);
  event->SetTrusted(true);
  dateTimeBoxElement->DispatchEvent(*event);
}

nsresult HTMLInputElement::PreHandleEvent(EventChainVisitor& aVisitor) {
  if (aVisitor.mItemFlags & NS_PRE_HANDLE_BLUR_EVENT) {
    MOZ_ASSERT(aVisitor.mEvent->mMessage == eBlur);
    // TODO(emilio): This should probably happen only if the event is trusted?
    FireChangeEventIfNeeded();
    MaybeDispatchWillBlur(aVisitor);
  }
  return nsGenericHTMLFormControlElementWithState::PreHandleEvent(aVisitor);
}

void HTMLInputElement::StartRangeThumbDrag(WidgetGUIEvent* aEvent) {
  nsRangeFrame* rangeFrame = do_QueryFrame(GetPrimaryFrame());
  if (!rangeFrame) {
    return;
  }

  mIsDraggingRange = true;
  mIsUserInteracting = true;
  mRangeThumbDragStartValue = GetValueAsDecimal();
  // Don't use CaptureFlags::RetargetToElement, as that breaks pseudo-class
  // styling of the thumb.
  PresShell::SetCapturingContent(this, CaptureFlags::IgnoreAllowedState);

  // Before we change the value, record the current value so that we'll
  // correctly send a 'change' event if appropriate. We need to do this here
  // because the 'focus' event is handled after the 'mousedown' event that
  // we're being called for (i.e. too late to update mFocusedValue, since we'll
  // have changed it by then).
  GetValue(mFocusedValue, CallerType::System);

  SetValueOfRangeForUserEvent(rangeFrame->GetValueAtEventPoint(aEvent),
                              SnapToTickMarks::Yes);
}

void HTMLInputElement::FinishRangeThumbDrag(WidgetGUIEvent* aEvent) {
  MOZ_ASSERT(mIsDraggingRange);

  if (PresShell::GetCapturingContent() == this) {
    PresShell::ReleaseCapturingContent();
  }
  if (aEvent) {
    if (nsRangeFrame* rangeFrame = do_QueryFrame(GetPrimaryFrame())) {
      SetValueOfRangeForUserEvent(rangeFrame->GetValueAtEventPoint(aEvent),
                                  SnapToTickMarks::Yes);
    }
  }
  mIsDraggingRange = false;
  FireChangeEventIfNeeded();
}

void HTMLInputElement::CancelRangeThumbDrag(bool aIsForUserEvent) {
  MOZ_ASSERT(mIsDraggingRange);

  mIsDraggingRange = false;
  mIsUserInteracting = false;
  if (PresShell::GetCapturingContent() == this) {
    PresShell::ReleaseCapturingContent();
  }
  if (aIsForUserEvent) {
    SetValueOfRangeForUserEvent(mRangeThumbDragStartValue,
                                SnapToTickMarks::Yes);
  } else {
    // Don't dispatch an 'input' event - at least not using
    // DispatchTrustedEvent.
    // TODO: decide what we should do here - bug 851782.
    nsAutoString val;
    mInputType->ConvertNumberToString(mRangeThumbDragStartValue,
                                      InputType::Localized::No, val);
    // TODO: What should we do if SetValueInternal fails?  (The allocation
    // is small, so we should be fine here.)
    SetValueInternal(val, {ValueSetterOption::BySetUserInputAPI,
                           ValueSetterOption::SetValueChanged});
    if (nsRangeFrame* frame = do_QueryFrame(GetPrimaryFrame())) {
      frame->UpdateForValueChange();
    }
    DebugOnly<nsresult> rvIgnored = nsContentUtils::DispatchInputEvent(this);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "Failed to dispatch input event");
  }
}

void HTMLInputElement::SetValueOfRangeForUserEvent(
    Decimal aValue, SnapToTickMarks aSnapToTickMarks) {
  MOZ_ASSERT(aValue.isFinite());
  if (aSnapToTickMarks == SnapToTickMarks::Yes) {
    MaybeSnapToTickMark(aValue);
  }

  Decimal oldValue = GetValueAsDecimal();

  nsAutoString val;
  mInputType->ConvertNumberToString(aValue, InputType::Localized::No, val);
  // TODO: What should we do if SetValueInternal fails?  (The allocation
  // is small, so we should be fine here.)
  SetValueInternal(val, {ValueSetterOption::BySetUserInputAPI,
                         ValueSetterOption::SetValueChanged});
  if (nsRangeFrame* frame = do_QueryFrame(GetPrimaryFrame())) {
    frame->UpdateForValueChange();
  }

  if (GetValueAsDecimal() != oldValue) {
    DebugOnly<nsresult> rvIgnored = nsContentUtils::DispatchInputEvent(this);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "Failed to dispatch input event");
  }
}

void HTMLInputElement::StartNumberControlSpinnerSpin() {
  MOZ_ASSERT(!mNumberControlSpinnerIsSpinning);

  mNumberControlSpinnerIsSpinning = true;

  nsRepeatService::GetInstance()->Start(
      HandleNumberControlSpin, this, OwnerDoc(), "HandleNumberControlSpin"_ns);

  // Capture the mouse so that we can tell if the pointer moves from one
  // spin button to the other, or to some other element:
  PresShell::SetCapturingContent(this, CaptureFlags::IgnoreAllowedState);
}

void HTMLInputElement::StopNumberControlSpinnerSpin(SpinnerStopState aState) {
  if (!mNumberControlSpinnerIsSpinning) {
    return;
  }
  if (PresShell::GetCapturingContent() == this) {
    PresShell::ReleaseCapturingContent();
  }

  nsRepeatService::GetInstance()->Stop(HandleNumberControlSpin, this);

  mNumberControlSpinnerIsSpinning = false;

  if (aState == eAllowDispatchingEvents) {
    FireChangeEventIfNeeded();
  }
}

void HTMLInputElement::StepNumberControlForUserEvent(int32_t aDirection) {
  // We can't use GetValidityState here because the validity state is not set
  // if the user hasn't previously taken an action to set or change the value,
  // according to the specs.
  if (HasBadInput()) {
    // If the user has typed a value into the control and inadvertently made a
    // mistake (e.g. put a thousand separator at the wrong point) we do not
    // want to wipe out what they typed if they try to increment/decrement the
    // value. Better is to highlight the value as being invalid so that they
    // can correct what they typed.
    // We only do this if there actually is a value typed in by/displayed to
    // the user. (IsValid() can return false if the 'required' attribute is
    // set and the value is the empty string.)
    if (!IsValueEmpty()) {
      // We pass 'true' for SetUserInteracted because we need the UI to update
      // _now_ or the user will wonder why the step behavior isn't functioning.
      SetUserInteracted(true);
      return;
    }
  }

  Decimal newValue = GetValueIfStepped(aDirection, StepCallerType::ForUserEvent,
                                       IgnoreErrors());
  if (!newValue.isFinite()) {
    return;  // value should not or will not change
  }

  mIsUserInteracting = true;

  nsAutoString newVal;
  mInputType->ConvertNumberToString(newValue, InputType::Localized::No, newVal);
  // TODO: What should we do if SetValueInternal fails?  (The allocation
  // is small, so we should be fine here.)
  SetValueInternal(newVal, {ValueSetterOption::BySetUserInputAPI,
                            ValueSetterOption::SetValueChanged});
}

bool HTMLInputElement::ShouldPreventDOMActivateDispatch(
    EventTarget* aOriginalTarget) {
  /*
   * For the moment, there is only one situation where we actually want to
   * prevent firing a DOMActivate event:
   *  - we are a <input type='file'> that just got a click event,
   *  - the event was targeted to our button which should have sent a
   *    DOMActivate event.
   */

  if (mType != FormControlType::InputFile) {
    return false;
  }

  Element* target = Element::FromEventTargetOrNull(aOriginalTarget);
  if (!target) {
    return false;
  }

  return target->GetParent() == this &&
         target->IsRootOfNativeAnonymousSubtree() &&
         target->IsHTMLElement(nsGkAtoms::button);
}

nsresult HTMLInputElement::MaybeInitPickers(EventChainPostVisitor& aVisitor) {
  // Open a file picker when we receive a click on a <input type='file'>, or
  // open a color picker when we receive a click on a <input type='color'>.
  // A click is handled if it's the left mouse button.
  // We do not prevent non-trusted click because authors can already use
  // .click(). However, the pickers will check and consume user activation.
  WidgetMouseEvent* mouseEvent = aVisitor.mEvent->AsMouseEvent();
  if (!(mouseEvent && mouseEvent->IsLeftClickEvent())) {
    return NS_OK;
  }
  if (mType == FormControlType::InputFile) {
    // If the user clicked on the "Choose folder..." button we open the
    // directory picker, else we open the file picker.
    FilePickerType type = FILE_PICKER_FILE;
    nsIContent* target =
        nsIContent::FromEventTargetOrNull(aVisitor.mEvent->mOriginalTarget);
    if (target && target->FindFirstNonChromeOnlyAccessContent() == this &&
        StaticPrefs::dom_webkitBlink_dirPicker_enabled() &&
        HasAttr(nsGkAtoms::webkitdirectory)) {
      type = FILE_PICKER_DIRECTORY;
    }
    return InitFilePicker(type);
  }
  if (mType == FormControlType::InputColor) {
    return InitColorPicker();
  }

  return NS_OK;
}

/**
 * Return true if the input event should be ignored because of its modifiers.
 * Control is treated specially, since sometimes we ignore it, and sometimes
 * we don't (for webcompat reasons).
 */
static bool IgnoreInputEventWithModifier(const WidgetInputEvent& aEvent,
                                         bool ignoreControl) {
  return (ignoreControl && aEvent.IsControl()) ||
         aEvent.IsAltGraph()
#if defined(XP_WIN) || defined(MOZ_WIDGET_GTK)
         // Meta key is the Windows Logo key on Windows and Linux which may
         // assign some special meaning for the events while it's pressed.
         // On the other hand, it's a normal modifier in macOS and Android.
         // Therefore, We should ignore it only in Win/Linux.
         || aEvent.IsMeta()
#endif
         || aEvent.IsFn();
}

bool HTMLInputElement::StepsInputValue(
    const WidgetKeyboardEvent& aEvent) const {
  if (mType != FormControlType::InputNumber) {
    return false;
  }
  if (aEvent.mMessage != eKeyPress) {
    return false;
  }
  if (!aEvent.IsTrusted()) {
    return false;
  }
  if (aEvent.mKeyCode != NS_VK_UP && aEvent.mKeyCode != NS_VK_DOWN) {
    return false;
  }
  if (IgnoreInputEventWithModifier(aEvent, false)) {
    return false;
  }
  if (aEvent.DefaultPrevented()) {
    return false;
  }
  if (!IsMutable()) {
    return false;
  }
  return true;
}

static bool ActivatesWithKeyboard(FormControlType aType, uint32_t aKeyCode) {
  switch (aType) {
    case FormControlType::InputCheckbox:
    case FormControlType::InputRadio:
      // Checkbox and Radio try to submit on Enter press
      return aKeyCode != NS_VK_RETURN;
    case FormControlType::InputButton:
    case FormControlType::InputReset:
    case FormControlType::InputSubmit:
    case FormControlType::InputFile:
    case FormControlType::InputImage:  // Bug 34418
    case FormControlType::InputColor:
      return true;
    default:
      return false;
  }
}

nsresult HTMLInputElement::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  if (aVisitor.mEvent->mMessage == eBlur) {
    if (mIsDraggingRange) {
      FinishRangeThumbDrag();
    } else if (mNumberControlSpinnerIsSpinning) {
      StopNumberControlSpinnerSpin();
    }
  }

  nsresult rv = NS_OK;
  auto oldType = FormControlType(NS_CONTROL_TYPE(aVisitor.mItemFlags));

  // Ideally we would make the default action for click and space just dispatch
  // DOMActivate, and the default action for DOMActivate flip the checkbox/
  // radio state and fire onchange.  However, for backwards compatibility, we
  // need to flip the state before firing click, and we need to fire click
  // when space is pressed.  So, we just nest the firing of DOMActivate inside
  // the click event handling, and allow cancellation of DOMActivate to cancel
  // the click.
  if (aVisitor.mEventStatus != nsEventStatus_eConsumeNoDefault &&
      !IsSingleLineTextControl(true) && mType != FormControlType::InputNumber) {
    WidgetMouseEvent* mouseEvent = aVisitor.mEvent->AsMouseEvent();
    if (mouseEvent && mouseEvent->IsLeftClickEvent() &&
        OwnerDoc()->MayHaveDOMActivateListeners() &&
        !ShouldPreventDOMActivateDispatch(aVisitor.mEvent->mOriginalTarget)) {
      // DOMActive event should be trusted since the activation is actually
      // occurred even if the cause is an untrusted click event.
      InternalUIEvent actEvent(true, eLegacyDOMActivate, mouseEvent);
      actEvent.mDetail = 1;

      if (RefPtr<PresShell> presShell =
              aVisitor.mPresContext ? aVisitor.mPresContext->GetPresShell()
                                    : nullptr) {
        nsEventStatus status = nsEventStatus_eIgnore;
        mInInternalActivate = true;
        rv = presShell->HandleDOMEventWithTarget(this, &actEvent, &status);
        mInInternalActivate = false;

        // If activate is cancelled, we must do the same as when click is
        // cancelled (revert the checkbox to its original value).
        if (status == nsEventStatus_eConsumeNoDefault) {
          aVisitor.mEventStatus = status;
        }
      }
    }
  }

  bool preventDefault =
      aVisitor.mEventStatus == nsEventStatus_eConsumeNoDefault;
  if (IsDisabled() && oldType != FormControlType::InputCheckbox &&
      oldType != FormControlType::InputRadio) {
    // Behave as if defaultPrevented when the element becomes disabled by event
    // listeners. Checkboxes and radio buttons should still process clicks for
    // web compat. See:
    // https://html.spec.whatwg.org/multipage/input.html#the-input-element:activation-behaviour
    preventDefault = true;
  }

  if (NS_SUCCEEDED(rv)) {
    WidgetKeyboardEvent* keyEvent = aVisitor.mEvent->AsKeyboardEvent();
    if (keyEvent && StepsInputValue(*keyEvent)) {
      StepNumberControlForUserEvent(keyEvent->mKeyCode == NS_VK_UP ? 1 : -1);
      FireChangeEventIfNeeded();
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
    } else if (!preventDefault) {
      if (keyEvent && ActivatesWithKeyboard(mType, keyEvent->mKeyCode) &&
          keyEvent->IsTrusted()) {
        // We maybe dispatch a synthesized click for keyboard activation.
        HandleKeyboardActivation(aVisitor);
      }

      switch (aVisitor.mEvent->mMessage) {
        case eKeyDown: {
          // For compatibility with the other browsers, we should active this
          // element at least when a checkbox or a radio button.
          // TODO: Investigate which elements are activated by space key in the
          //       other browsers.
          if (aVisitor.mPresContext && keyEvent->IsTrusted() && !IsDisabled() &&
              keyEvent->ShouldWorkAsSpaceKey() &&
              (mType == FormControlType::InputCheckbox ||
               mType == FormControlType::InputRadio)) {
            EventStateManager::SetActiveManager(
                aVisitor.mPresContext->EventStateManager(), this);
          }

          if (keyEvent->mKeyCode == NS_VK_ESCAPE && keyEvent->IsTrusted() &&
              !keyEvent->DefaultPrevented() && !keyEvent->mIsComposing &&
              mType == FormControlType::InputSearch &&
              StaticPrefs::dom_forms_search_esc() && !IsDisabledOrReadOnly() &&
              !IsValueEmpty()) {
            // WebKit and Blink both also do this on keydown, see:
            //   https://source.chromium.org/chromium/chromium/src/+/main:third_party/blink/renderer/core/html/forms/search_input_type.cc;l=82;drc=04f1f437aaefbd3bb4e0cdb5911c1ea1e3eb3557;bpv=1;bpt=1
            //   https://searchfox.org/wubkat/rev/717f9adc97dd16bf639d27addbe0faf420f7dfce/Source/WebCore/html/SearchInputType.cpp#145
            SetUserInput(EmptyString(), *NodePrincipal());
            aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
          }
          break;
        }

        case eKeyPress: {
          if (mType == FormControlType::InputRadio && keyEvent->IsTrusted() &&
              !keyEvent->IsAlt() && !keyEvent->IsControl() &&
              !keyEvent->IsMeta()) {
            // Radio button navigation needs to check visibility, so flush
            // to ensure visibility is up to date.
            if (Document* doc = GetComposedDoc()) {
              doc->FlushPendingNotifications(
                  FlushType::EnsurePresShellInitAndFrames);
            }
            rv = MaybeHandleRadioButtonNavigation(aVisitor, keyEvent->mKeyCode);
          }

          /*
           * For some input types, if the user hits enter, the form is
           * submitted.
           *
           * Bug 99920, bug 109463 and bug 147850:
           * (a) if there is a submit control in the form, click the first
           *     submit control in the form.
           * (b) if there is just one text control in the form, submit by
           *     sending a submit event directly to the form
           * (c) if there is more than one text input and no submit buttons, do
           *     not submit, period.
           */

          if (keyEvent->mKeyCode == NS_VK_RETURN && keyEvent->IsTrusted() &&
              (IsSingleLineTextControl(false, mType) ||
               IsDateTimeInputType(mType) ||
               mType == FormControlType::InputCheckbox ||
               mType == FormControlType::InputRadio)) {
            if (IsSingleLineTextControl(false, mType) ||
                IsDateTimeInputType(mType)) {
              FireChangeEventIfNeeded();
            }

            if (aVisitor.mPresContext) {
              MaybeSubmitForm(aVisitor.mPresContext);
            }
          }

          if (mType == FormControlType::InputRange && keyEvent->IsTrusted() &&
              !keyEvent->IsAlt() && !keyEvent->IsControl() &&
              !keyEvent->IsMeta() &&
              (keyEvent->mKeyCode == NS_VK_LEFT ||
               keyEvent->mKeyCode == NS_VK_RIGHT ||
               keyEvent->mKeyCode == NS_VK_UP ||
               keyEvent->mKeyCode == NS_VK_DOWN ||
               keyEvent->mKeyCode == NS_VK_PAGE_UP ||
               keyEvent->mKeyCode == NS_VK_PAGE_DOWN ||
               keyEvent->mKeyCode == NS_VK_HOME ||
               keyEvent->mKeyCode == NS_VK_END)) {
            Decimal minimum = GetMinimum();
            Decimal maximum = GetMaximum();
            MOZ_ASSERT(minimum.isFinite() && maximum.isFinite());
            if (minimum < maximum) {  // else the value is locked to the minimum
              Decimal value = GetValueAsDecimal();
              Decimal step = GetStep();
              if (step == kStepAny) {
                step = GetDefaultStep();
              }
              MOZ_ASSERT(value.isFinite() && step.isFinite());
              Decimal newValue;
              switch (keyEvent->mKeyCode) {
                case NS_VK_LEFT:
                  newValue = value +
                             (GetComputedDirectionality() == Directionality::Rtl
                                  ? step
                                  : -step);
                  break;
                case NS_VK_RIGHT:
                  newValue = value +
                             (GetComputedDirectionality() == Directionality::Rtl
                                  ? -step
                                  : step);
                  break;
                case NS_VK_UP:
                  // Even for horizontal range, "up" means "increase"
                  newValue = value + step;
                  break;
                case NS_VK_DOWN:
                  // Even for horizontal range, "down" means "decrease"
                  newValue = value - step;
                  break;
                case NS_VK_HOME:
                  newValue = minimum;
                  break;
                case NS_VK_END:
                  newValue = maximum;
                  break;
                case NS_VK_PAGE_UP:
                  // For PgUp/PgDn we jump 10% of the total range, unless step
                  // requires us to jump more.
                  newValue =
                      value + std::max(step, (maximum - minimum) / Decimal(10));
                  break;
                case NS_VK_PAGE_DOWN:
                  newValue =
                      value - std::max(step, (maximum - minimum) / Decimal(10));
                  break;
              }
              SetValueOfRangeForUserEvent(newValue);
              FireChangeEventIfNeeded();
              aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
            }
          }
        } break;  // eKeyPress

        case eMouseDown:
        case eMouseUp:
        case eMouseDoubleClick: {
          // cancel all of these events for buttons
          // XXXsmaug Why?
          WidgetMouseEvent* mouseEvent = aVisitor.mEvent->AsMouseEvent();
          if (mouseEvent->mButton == MouseButton::eMiddle ||
              mouseEvent->mButton == MouseButton::eSecondary) {
            if (mType == FormControlType::InputButton ||
                mType == FormControlType::InputReset ||
                mType == FormControlType::InputSubmit) {
              if (aVisitor.mDOMEvent) {
                aVisitor.mDOMEvent->StopPropagation();
              } else {
                rv = NS_ERROR_FAILURE;
              }
            }
          }
          if (mType == FormControlType::InputNumber &&
              aVisitor.mEvent->IsTrusted()) {
            if (mouseEvent->mButton == MouseButton::ePrimary &&
                !IgnoreInputEventWithModifier(*mouseEvent, false) &&
                aVisitor.mEvent->mMessage == eMouseDown && IsMutable()) {
              switch (SpinnerDirectionForEvent(*aVisitor.mEvent,
                                               GetTextEditorButton())) {
                case SpinnerDirection::Up:
                  StepNumberControlForUserEvent(1);
                  mNumberControlSpinnerSpinsUp = true;
                  StartNumberControlSpinnerSpin();
                  aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
                  break;
                case SpinnerDirection::Down:
                  StepNumberControlForUserEvent(-1);
                  mNumberControlSpinnerSpinsUp = false;
                  StartNumberControlSpinnerSpin();
                  aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
                  break;
                case SpinnerDirection::None:
                  break;
              }
            }
            if (aVisitor.mEventStatus != nsEventStatus_eConsumeNoDefault) {
              // We didn't handle this to step up/down. Whatever this was, be
              // aggressive about stopping the spin. (And don't set
              // nsEventStatus_eConsumeNoDefault after doing so, since that
              // might prevent, say, the context menu from opening.)
              StopNumberControlSpinnerSpin();
            }
          }
          break;
        }
        case eWheel: {
          if (StaticPrefs::
                  dom_input_number_and_range_modified_by_mousewheel()) {
            // Handle wheel events as increasing / decreasing the input
            // element's value when it's focused and it's type is number or
            // range.
            WidgetWheelEvent* wheelEvent = aVisitor.mEvent->AsWheelEvent();
            if (!aVisitor.mEvent->DefaultPrevented() &&
                aVisitor.mEvent->IsTrusted() && IsMutable() && wheelEvent &&
                wheelEvent->mDeltaY != 0 &&
                wheelEvent->mDeltaMode != WheelEvent_Binding::DOM_DELTA_PIXEL) {
              if (mType == FormControlType::InputNumber) {
                if (nsFocusManager::GetFocusedElementStatic() == this) {
                  StepNumberControlForUserEvent(wheelEvent->mDeltaY > 0 ? -1
                                                                        : 1);
                  FireChangeEventIfNeeded();
                  aVisitor.mEvent->PreventDefault();
                }
              } else if (mType == FormControlType::InputRange &&
                         nsFocusManager::GetFocusedElementStatic() == this &&
                         GetMinimum() < GetMaximum()) {
                Decimal value = GetValueAsDecimal();
                Decimal step = GetStep();
                if (step == kStepAny) {
                  step = GetDefaultStep();
                }
                MOZ_ASSERT(value.isFinite() && step.isFinite());
                SetValueOfRangeForUserEvent(
                    wheelEvent->mDeltaY < 0 ? value + step : value - step);
                FireChangeEventIfNeeded();
                aVisitor.mEvent->PreventDefault();
              }
            }
          }
          break;
        }
        case ePointerClick: {
          if (!aVisitor.mEvent->DefaultPrevented() &&
              aVisitor.mEvent->IsTrusted() &&
              aVisitor.mEvent->AsMouseEvent()->mButton ==
                  MouseButton::ePrimary) {
            const auto IsClickedOn = [&](const Element* aButton) {
              if (!aButton) {
                return false;
              }
              if (aButton == aVisitor.mEvent->mOriginalTarget) {
                return true;
              }
              // During the button click, the button may have been recreated.
              // First, check whether the mouseup occurred on the new button.
              nsIContent* mouseUpContent =
                  aVisitor.mPresContext->EventStateManager()
                      ->GetMouseUpTargetContent(MouseButton::ePrimary);
              if (!mouseUpContent) [[unlikely]] {
                return false;
              }
              if (!mouseUpContent->IsElement()) {
                // FYI: We don't use shadow DOM in the native anonymous subtree.
                // Therefore, it should be fine not to use
                // GetFlattenedTreeParentElement() here.
                mouseUpContent = mouseUpContent->GetAsElementOrParentElement();
              }
              if (aButton != mouseUpContent) {
                return false;
              }
              // Then, check whether the mousedown target is disconnected and
              // the same button as the new button.
              nsIContent* mouseDownContent =
                  aVisitor.mPresContext->EventStateManager()
                      ->GetMouseDownTargetContent(MouseButton::ePrimary);
              if (!mouseDownContent || mouseDownContent->IsInComposedDoc()) {
                return false;
              }
              if (!mouseDownContent->IsElement()) {
                // FYI: We don't use shadow DOM in the native anonymous subtree.
                // Therefore, it should be fine not to use
                // GetFlattenedTreeParentElement() here.
                mouseDownContent =
                    mouseDownContent->GetAsElementOrParentElement();
                if (!mouseDownContent) [[unlikely]] {
                  return false;
                }
              }
              MOZ_ASSERT_IF(
                  mouseDownContent->AsElement()->GetPseudoElementType() ==
                      mouseUpContent->AsElement()->GetPseudoElementType(),
                  mouseDownContent->NodeInfo()->NameAtom() ==
                      mouseUpContent->NodeInfo()->NameAtom());
              return mouseDownContent->AsElement()->GetPseudoElementType() ==
                     mouseUpContent->AsElement()->GetPseudoElementType();
            };
            // TODO(emilio): Handling this should ideally not move focus.
            if (mType == FormControlType::InputSearch) {
              Element* button = GetTextEditorButton();
              if (IsClickedOn(button)) {
                SetUserInput(EmptyString(),
                             *nsContentUtils::GetSystemPrincipal());
              }
            } else if (mType == FormControlType::InputPassword) {
              Element* button = GetTextEditorButton();
              if (IsClickedOn(button)) {
                SetRevealPassword(!RevealPassword());
              }
            }
          }
          break;
        }
        default:
          break;
      }

      // Bug 1459231: Temporarily needed till links respect activation target,
      // then also remove NS_OUTER_ACTIVATE_EVENT. The appropriate
      // behavior/model for links is still under discussion (see
      // https://github.com/whatwg/html/issues/1576). For now, we aim for
      // consistency with other browsers.
      if (aVisitor.mItemFlags & NS_OUTER_ACTIVATE_EVENT) {
        switch (mType) {
          case FormControlType::InputReset:
          case FormControlType::InputSubmit:
          case FormControlType::InputImage:
            if (mForm) {
              aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
            }
            break;
          case FormControlType::InputCheckbox:
          case FormControlType::InputRadio:
            aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
            break;
          default:
            break;
        }
      }
    }
  }  // if

  if (NS_SUCCEEDED(rv) && mType == FormControlType::InputRange) {
    PostHandleEventForRangeThumb(aVisitor);
  }

  if (!preventDefault) {
    MOZ_TRY(MaybeInitPickers(aVisitor));
  }
  return NS_OK;
}

void EndSubmitClick(EventChainPostVisitor& aVisitor) {
  if (aVisitor.mItemFlags & NS_IN_SUBMIT_CLICK) {
    nsCOMPtr<nsIContent> content(do_QueryInterface(aVisitor.mItemData));
    RefPtr<HTMLFormElement> form = HTMLFormElement::FromNodeOrNull(content);
    // Tell the form that we are about to exit a click handler,
    // so the form knows not to defer subsequent submissions.
    // The pending ones that were created during the handler
    // will be flushed or forgotten.
    form->OnSubmitClickEnd();
    // tell the form to flush a possible pending submission.
    // the reason is that the script returned false (the event was
    // not ignored) so if there is a stored submission, it needs to
    // be submitted immediately.
    form->FlushPendingSubmission();
  }
}

void HTMLInputElement::ActivationBehavior(EventChainPostVisitor& aVisitor) {
  auto oldType = FormControlType(NS_CONTROL_TYPE(aVisitor.mItemFlags));

  auto endSubmit = MakeScopeExit([&] { EndSubmitClick(aVisitor); });

  if (IsDisabled() && oldType != FormControlType::InputCheckbox &&
      oldType != FormControlType::InputRadio) {
    // Behave as if defaultPrevented when the element becomes disabled by event
    // listeners. Checkboxes and radio buttons should still process clicks for
    // web compat. See:
    // https://html.spec.whatwg.org/multipage/input.html#the-input-element:activation-behaviour
    return;
  }

  // https://html.spec.whatwg.org/#checkbox-state-(type=checkbox):input-activation-behavior
  // If element is connected, fire input and change event
  if (mCheckedIsToggled && IsInComposedDoc()) {
    SetUserInteracted(true);

    // Fire input event and then change event.
    DebugOnly<nsresult> rvIgnored = nsContentUtils::DispatchInputEvent(this);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "Failed to dispatch input event");

    // FIXME: Why is this different than every other change event?
    nsContentUtils::DispatchTrustedEvent<WidgetEvent>(
        OwnerDoc(), static_cast<Element*>(this), eFormChange, CanBubble::eYes,
        Cancelable::eNo);
  }

  switch (mType) {
    case FormControlType::InputReset:
    case FormControlType::InputSubmit:
    case FormControlType::InputImage:
      if (mForm) {
        // Hold a strong ref while dispatching
        RefPtr<HTMLFormElement> form(mForm);
        if (mType == FormControlType::InputReset) {
          form->MaybeReset(this);
        } else {
          form->MaybeSubmit(this);
        }
        aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        return;
      }
      break;

    default:
      break;
  }  // switch
  if (IsButtonControl()) {
    nsCOMPtr<Element> eventTarget =
        do_QueryInterface(aVisitor.mEvent->mOriginalTarget);
    HandlePopoverTargetAction(eventTarget);
  }
}

void HTMLInputElement::LegacyCanceledActivationBehavior(
    EventChainPostVisitor& aVisitor) {
  bool originalCheckedValue =
      !!(aVisitor.mItemFlags & NS_ORIGINAL_CHECKED_VALUE);
  auto oldType = FormControlType(NS_CONTROL_TYPE(aVisitor.mItemFlags));

  if (mCheckedIsToggled) {
    // if it was canceled and a radio button, then set the old
    // selected btn to TRUE. if it is a checkbox then set it to its
    // original value (legacy-canceled-activation)
    if (oldType == FormControlType::InputRadio) {
      nsCOMPtr<nsIContent> content = do_QueryInterface(aVisitor.mItemData);
      HTMLInputElement* selectedRadioButton =
          HTMLInputElement::FromNodeOrNull(content);
      if (selectedRadioButton) {
        selectedRadioButton->SetChecked(true);
      }
      // If there was no checked radio button or this one is no longer a
      // radio button we must reset it back to false to cancel the action.
      // See how the web of hack grows?
      if (!selectedRadioButton || mType != FormControlType::InputRadio) {
        DoSetChecked(/* aValue */ false, /* aNotify */ true,
                     /* aSetValueChanged */ true);
      }
    } else if (oldType == FormControlType::InputCheckbox) {
      bool originalIndeterminateValue =
          !!(aVisitor.mItemFlags & NS_ORIGINAL_INDETERMINATE_VALUE);
      SetIndeterminateInternal(originalIndeterminateValue, false);
      DoSetChecked(originalCheckedValue, /* aNotify */ true,
                   /* aSetValueChanged */ true);
    }
  }

  // Relevant for bug 242494: submit button with "submit(); return false;"
  EndSubmitClick(aVisitor);
}

enum class RadioButtonMove { Back, Forward, None };
nsresult HTMLInputElement::MaybeHandleRadioButtonNavigation(
    EventChainPostVisitor& aVisitor, uint32_t aKeyCode) {
  auto move = [&] {
    switch (aKeyCode) {
      case NS_VK_UP:
        return RadioButtonMove::Back;
      case NS_VK_DOWN:
        return RadioButtonMove::Forward;
      case NS_VK_LEFT:
      case NS_VK_RIGHT: {
        const bool isRtl = GetComputedDirectionality() == Directionality::Rtl;
        return isRtl == (aKeyCode == NS_VK_LEFT) ? RadioButtonMove::Forward
                                                 : RadioButtonMove::Back;
      }
    }
    return RadioButtonMove::None;
  }();
  if (move == RadioButtonMove::None) {
    return NS_OK;
  }
  // Arrow key pressed, focus+select prev/next radio button
  RefPtr<HTMLInputElement> selectedRadioButton;
  if (auto* container = GetCurrentRadioGroupContainer()) {
    nsAutoString name;
    GetAttr(nsGkAtoms::name, name);
    container->GetNextRadioButton(name, move == RadioButtonMove::Back, this,
                                  getter_AddRefs(selectedRadioButton));
  }
  if (!selectedRadioButton) {
    return NS_OK;
  }
  FocusOptions options;
  ErrorResult error;
  selectedRadioButton->Focus(options, CallerType::System, error);
  if (error.Failed()) {
    return error.StealNSResult();
  }
  nsresult rv = DispatchSimulatedClick(
      selectedRadioButton, aVisitor.mEvent->IsTrusted(), aVisitor.mPresContext);
  if (NS_SUCCEEDED(rv)) {
    aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
  }
  return rv;
}

void HTMLInputElement::PostHandleEventForRangeThumb(
    EventChainPostVisitor& aVisitor) {
  MOZ_ASSERT(mType == FormControlType::InputRange);

  if (nsEventStatus_eConsumeNoDefault == aVisitor.mEventStatus ||
      !(aVisitor.mEvent->mClass == eMouseEventClass ||
        aVisitor.mEvent->mClass == eTouchEventClass ||
        aVisitor.mEvent->mClass == eKeyboardEventClass)) {
    return;
  }

  nsRangeFrame* rangeFrame = do_QueryFrame(GetPrimaryFrame());
  if (!rangeFrame && mIsDraggingRange) {
    CancelRangeThumbDrag();
    return;
  }

  switch (aVisitor.mEvent->mMessage) {
    case eMouseDown:
    case eTouchStart: {
      if (mIsDraggingRange) {
        break;
      }
      if (PresShell::GetCapturingContent()) {
        break;  // don't start drag if someone else is already capturing
      }
      WidgetInputEvent* inputEvent = aVisitor.mEvent->AsInputEvent();
      if (IgnoreInputEventWithModifier(*inputEvent, true)) {
        break;  // ignore
      }
      if (aVisitor.mEvent->mMessage == eMouseDown) {
        if (aVisitor.mEvent->AsMouseEvent()->mButtons ==
            MouseButtonsFlag::ePrimaryFlag) {
          StartRangeThumbDrag(inputEvent);
        } else if (mIsDraggingRange) {
          CancelRangeThumbDrag();
        }
      } else {
        if (aVisitor.mEvent->AsTouchEvent()->mTouches.Length() == 1) {
          StartRangeThumbDrag(inputEvent);
        } else if (mIsDraggingRange) {
          CancelRangeThumbDrag();
        }
      }
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
    } break;

    case eMouseMove:
    case eTouchMove:
      if (!mIsDraggingRange) {
        break;
      }
      if (PresShell::GetCapturingContent() != this) {
        // Someone else grabbed capture.
        CancelRangeThumbDrag();
        break;
      }
      SetValueOfRangeForUserEvent(
          rangeFrame->GetValueAtEventPoint(aVisitor.mEvent->AsInputEvent()),
          SnapToTickMarks::Yes);
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
      break;

    case eMouseUp:
    case eTouchEnd:
      if (!mIsDraggingRange) {
        break;
      }
      // We don't check to see whether we are the capturing content here and
      // call CancelRangeThumbDrag() if that is the case. We just finish off
      // the drag and set our final value (unless someone has called
      // preventDefault() and prevents us getting here).
      FinishRangeThumbDrag(aVisitor.mEvent->AsInputEvent());
      aVisitor.mEvent->mFlags.mMultipleActionsPrevented = true;
      break;

    case eKeyPress:
      if (mIsDraggingRange &&
          aVisitor.mEvent->AsKeyboardEvent()->mKeyCode == NS_VK_ESCAPE) {
        CancelRangeThumbDrag();
      }
      break;

    case eTouchCancel:
      if (mIsDraggingRange) {
        CancelRangeThumbDrag();
      }
      break;

    default:
      break;
  }
}

void HTMLInputElement::MaybeLoadImage() {
  // Our base URI may have changed; claim that our URI changed, and the
  // nsImageLoadingContent will decide whether a new image load is warranted.
  nsAutoString uri;
  if (mType == FormControlType::InputImage && GetAttr(nsGkAtoms::src, uri) &&
      (NS_FAILED(LoadImage(uri, false, true, eImageLoadType_Normal,
                           mSrcTriggeringPrincipal)) ||
       !LoadingEnabled())) {
    CancelImageRequests(true);
  }
}

nsresult HTMLInputElement::BindToTree(BindContext& aContext, nsINode& aParent) {
  // If we are currently bound to a disconnected subtree root, remove
  // ourselves from it first.
  if (!mForm && mType == FormControlType::InputRadio) {
    RemoveFromRadioGroup();
  }

  nsresult rv =
      nsGenericHTMLFormControlElementWithState::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  nsImageLoadingContent::BindToTree(aContext, aParent);

  if (mType == FormControlType::InputImage) {
    // Our base URI may have changed; claim that our URI changed, and the
    // nsImageLoadingContent will decide whether a new image load is warranted.
    if (HasAttr(nsGkAtoms::src)) {
      // Mark channel as urgent-start before load image if the image load is
      // initaiated by a user interaction.
      mUseUrgentStartForChannel = UserActivation::IsHandlingUserInput();

      nsContentUtils::AddScriptRunner(
          NewRunnableMethod("dom::HTMLInputElement::MaybeLoadImage", this,
                            &HTMLInputElement::MaybeLoadImage));
    }
  }

  // Add radio to document if we don't have a form already (if we do it's
  // already been added into that group)
  if (!mForm && mType == FormControlType::InputRadio) {
    AddToRadioGroup();
  }

  // Set direction based on value if dir=auto
  ResetDirFormAssociatedElement(this, false, HasDirAuto());

  // An element can't suffer from value missing if it is not in a document.
  // We have to check if we suffer from that as we are now in a document.
  UpdateValueMissingValidityState();

  // If there is a disabled fieldset in the parent chain, the element is now
  // barred from constraint validation and can't suffer from value missing
  // (call done before).
  UpdateBarredFromConstraintValidation();

  // And now make sure our state is up to date
  UpdateValidityElementStates(true);

  if (mDoneCreating && IsInComposedDoc() && CreatesDateTimeWidget()) {
    SetupShadowTree(/* aNotify = */ false);
  }

  MaybeDispatchLoginManagerEvents(mForm);

  return rv;
}

void HTMLInputElement::SetupShadowTree(bool aNotify) {
  MOZ_ASSERT(CreatesUAShadowTree());
  MOZ_ASSERT(IsInComposedDoc());
  MOZ_ASSERT(!GetShadowRoot());

  auto uaWidget = NotifiesUAWidget();
  AttachAndSetUAShadowRoot(uaWidget,
                           uaWidget == NotifyUAWidget::Yes ? DelegatesFocus::Yes
                                                           : DelegatesFocus::No,
                           CustomSlotDispatch::No, aNotify);
  if (uaWidget == NotifyUAWidget::Yes) {
    // The UA widget system takes care of this.
    return;
  }
  auto* shadow = GetShadowRoot();
  if (!shadow) {
    return;
  }
  // For now, only text controls should get here.
  MOZ_ASSERT(IsSingleLineTextControl());
  TextControlElement::SetupShadowTree(*shadow, aNotify);
}

ShadowRoot* HTMLInputElement::CreateShadowTreeFromLayoutIfNeeded() {
  if (!CreatesUAShadowTree()) {
    return nullptr;
  }
  auto* existing = GetShadowRoot();
  if (existing) {
    return nullptr;
  }
  if (HasChildren()) [[unlikely]] {
    // In the unlikely case we have any child, they are guaranteed to not have
    // frames, but they might still be styled and about to go out of the flat
    // tree, so need to clear their styles now, before creating the shadow tree.
    RestyleManager::ClearServoDataFromSubtree(this,
                                              RestyleManager::IncludeRoot::No);
  }
  SetupShadowTree(/* aNotify = */ false);
  return GetShadowRoot();
}

void HTMLInputElement::MaybeDispatchLoginManagerEvents(HTMLFormElement* aForm) {
  // Don't dispatch the event if the <input> is disconnected
  // or belongs to a disconnected form
  if (!IsInComposedDoc()) {
    return;
  }

  nsString eventType;
  EventTarget* target = nullptr;

  if (mType == FormControlType::InputPassword) {
    // Don't fire another event if we have a pending event.
    if (aForm && aForm->mHasPendingPasswordEvent) {
      return;
    }

    // TODO(Bug 1864404): Use one event for formless and form inputs.
    eventType = aForm ? u"DOMFormHasPassword"_ns : u"DOMInputPasswordAdded"_ns;

    if (aForm) {
      target = aForm;
      aForm->mHasPendingPasswordEvent = true;
    } else {
      target = this;
    }

  } else if (mType == FormControlType::InputEmail ||
             mType == FormControlType::InputText) {
    // Don't fire a username event if:
    // - we have a pending event
    // - username only forms are not supported
    // fire event if we have a username field without a form with the
    // autcomplete value of username

    if (!StaticPrefs::signon_usernameOnlyForm_enabled()) {
      return;
    }

    if (aForm) {
      if (aForm->mHasPendingPossibleUsernameEvent) {
        return;
      }
      aForm->mHasPendingPossibleUsernameEvent = true;
      target = aForm;
    } else {
      nsAutoString autocompleteValue;
      GetAutocomplete(autocompleteValue);
      if (!autocompleteValue.EqualsASCII("username")) {
        return;
      }
      target = GetComposedDoc();
    }
    eventType = u"DOMPossibleUsernameInputAdded"_ns;
  } else {
    return;
  }

  RefPtr<AsyncEventDispatcher> dispatcher = new AsyncEventDispatcher(
      target, eventType, CanBubble::eYes, ChromeOnlyDispatch::eYes);
  dispatcher->PostDOMEvent();
}

void HTMLInputElement::UnbindFromTree(UnbindContext& aContext) {
  if (mType == FormControlType::InputPassword) {
    MaybeFireInputPasswordRemoved();
  }

  // If we have a form and are unbound from it,
  // nsGenericHTMLFormControlElementWithState::UnbindFromTree() will unset the
  // form and that takes care of form's WillRemove so we just have to take care
  // of the case where we're removing from the document and we don't
  // have a form
  if (!mForm && mType == FormControlType::InputRadio) {
    RemoveFromRadioGroup();
  }

  if (CreatesUAShadowTree() && IsInComposedDoc()) {
    TeardownUAShadowRoot(NotifiesUAWidget());
  }

  nsImageLoadingContent::UnbindFromTree();
  nsGenericHTMLFormControlElementWithState::UnbindFromTree(aContext);

  // If we are contained within a disconnected subtree, attempt to add
  // ourselves to the subtree root's radio group.
  if (!mForm && mType == FormControlType::InputRadio) {
    AddToRadioGroup();
  }

  // GetCurrentDoc is returning nullptr so we can update the value
  // missing validity state to reflect we are no longer into a doc.
  UpdateValueMissingValidityState();
  // We might be no longer disabled because of parent chain changed.
  UpdateBarredFromConstraintValidation();
  // And now make sure our state is up to date
  UpdateValidityElementStates(false);
}

/**
 * @param aType InputElementTypes
 * @return true, iff SetRangeText applies to aType as specified at
 * https://html.spec.whatwg.org/#concept-input-apply.
 */
static bool SetRangeTextApplies(FormControlType aType) {
  return aType == FormControlType::InputText ||
         aType == FormControlType::InputSearch ||
         aType == FormControlType::InputUrl ||
         aType == FormControlType::InputTel ||
         aType == FormControlType::InputPassword;
}

void HTMLInputElement::HandleTypeChange(FormControlType aNewType,
                                        bool aNotify) {
  FormControlType oldType = mType;
  MOZ_ASSERT(oldType != aNewType);

  mHasBeenTypePassword =
      mHasBeenTypePassword || aNewType == FormControlType::InputPassword;

  if (nsFocusManager* fm = nsFocusManager::GetFocusManager()) {
    // Input element can represent very different kinds of UIs, and we may
    // need to flush styling even when focusing the already focused input
    // element.
    fm->NeedsFlushBeforeEventHandling(this);
  }

  if (oldType == FormControlType::InputPassword &&
      State().HasState(ElementState::REVEALED)) {
    // Modify the state directly to avoid dispatching events.
    RemoveStates(ElementState::REVEALED, aNotify);
  }

  if (aNewType == FormControlType::InputFile ||
      oldType == FormControlType::InputFile) {
    if (aNewType == FormControlType::InputFile) {
      mFileData.reset(new FileData());
    } else {
      mFileData->Unlink();
      mFileData = nullptr;
    }
  }

  if (oldType == FormControlType::InputRange && mIsDraggingRange) {
    CancelRangeThumbDrag(false);
  }

  const ValueModeType oldValueMode = GetValueMode();
  nsAutoString oldValue;
  if (oldValueMode == VALUE_MODE_VALUE) {
    // Doesn't matter what caller type we pass here, since we know we're not a
    // file input anyway.
    GetValue(oldValue, CallerType::NonSystem);
  }

  const bool wasTextControl = IsSingleLineTextControl(false, oldType);
  const bool isTextControl = IsSingleLineTextControl(false, aNewType);
  if (wasTextControl && !isTextControl && mInputData.mState) {
    mInputData.mState->DeinitSelection();
  }

  // We already have a copy of the value, lets free it and changes the type.
  FreeData(isTextControl ? TextControlStateDisposition::Reuse
                         : TextControlStateDisposition::Destroy);
  mType = aNewType;
  void* memory = mInputTypeMem;
  mInputType = InputType::Create(this, mType, memory);

  if (isTextControl) {
    if (!mInputData.mState) {
      mInputData.mState = TextControlState::Construct(this);
    } else {
      if (!SupportsTextSelection(oldType)) {
        // Collapse our selection if whether we honor
        // selection{Start,End,select()} has changed.
        mInputData.mState->SetSelectionRange(
            0, 0, SelectionDirection::Forward, IgnoreErrors(),
            TextControlState::ScrollAfterSelection::No);
      }
      mInputData.mState->UpdateEditorOnTypeChange();
    }
  }

  // Whether placeholder applies might have changed.
  UpdatePlaceholderShownState();
  // Whether readonly applies might have changed.
  UpdateReadOnlyState(aNotify);
  UpdateCheckedState(aNotify);
  UpdateIndeterminateState(aNotify);
  const bool isDefault = IsRadioOrCheckbox()
                             ? DefaultChecked()
                             : (mForm && mForm->IsDefaultSubmitElement(this));
  SetStates(ElementState::DEFAULT, isDefault, aNotify);

  // https://html.spec.whatwg.org/#input-type-change
  switch (GetValueMode()) {
    case VALUE_MODE_DEFAULT:
    case VALUE_MODE_DEFAULT_ON:
      // 1. If the previous state of the element's type attribute put the value
      //    IDL attribute in the value mode, and the element's value is not the
      //    empty string, and the new state of the element's type attribute puts
      //    the value IDL attribute in either the default mode or the default/on
      //    mode, then set the element's value content attribute to the
      //    element's value.
      if (oldValueMode == VALUE_MODE_VALUE && !oldValue.IsEmpty()) {
        SetAttr(kNameSpaceID_None, nsGkAtoms::value, oldValue, true);
      }
      break;
    case VALUE_MODE_VALUE: {
      ValueSetterOptions options{ValueSetterOption::ByInternalAPI};
      if (!SetRangeTextApplies(oldType) && SetRangeTextApplies(mType)) {
        options +=
            ValueSetterOption::MoveCursorToBeginSetSelectionDirectionForward;
      }
      if (oldValueMode != VALUE_MODE_VALUE) {
        // 2. Otherwise, if the previous state of the element's type attribute
        //    put the value IDL attribute in any mode other than the value
        //    mode, and the new state of the element's type attribute puts the
        //    value IDL attribute in the value mode, then set the value of the
        //    element to the value of the value content attribute, if there is
        //    one, or the empty string otherwise, and then set the control's
        //    dirty value flag to false.
        nsAutoString value;
        GetAttr(nsGkAtoms::value, value);
        SetValueInternal(value, options);
        SetValueChanged(false);
      } else if (mValueChanged) {
        // We're both in the "value" mode state, we need to make no change per
        // spec, but due to how we store the value internally we need to call
        // SetValueInternal, if our value had changed at all.
        // TODO: What should we do if SetValueInternal fails?  (The allocation
        // may potentially be big, but most likely we've failed to allocate
        // before the type change.)
        SetValueInternal(oldValue, options);
      } else {
        // The value dirty flag is not set, so our value is based on our default
        // value. But our default value might be dependent on the type. Make
        // sure to set it so that state is consistent.
        SetDefaultValueAsValue();
      }
      break;
    }
    case VALUE_MODE_FILENAME:
    default:
      // 3. Otherwise, if the previous state of the element's type attribute
      //    put the value IDL attribute in any mode other than the filename
      //    mode, and the new state of the element's type attribute puts the
      //    value IDL attribute in the filename mode, then set the value of the
      //    element to the empty string.
      //
      // Setting the attribute to the empty string is basically calling
      // ClearFiles, but there can't be any files.
      break;
  }

  // Updating mFocusedValue in consequence:
  // If the new type fires a change event on blur, but the previous type
  // doesn't, we should set mFocusedValue to the current value.
  // Otherwise, if the new type doesn't fire a change event on blur, but the
  // previous type does, we should clear out mFocusedValue.
  if (MayFireChangeOnBlur(mType) && !MayFireChangeOnBlur(oldType)) {
    GetValue(mFocusedValue, CallerType::System);
  } else if (!IsSingleLineTextControl(false, mType) &&
             IsSingleLineTextControl(false, oldType)) {
    mFocusedValue.Truncate();
  }

  // Update or clear our required states since we may have changed from a
  // required input type to a non-required input type or viceversa.
  if (DoesRequiredApply()) {
    const bool isRequired = HasAttr(nsGkAtoms::required);
    UpdateRequiredState(isRequired, aNotify);
  } else {
    RemoveStates(ElementState::REQUIRED_STATES, aNotify);
  }

  UpdateHasRange(aNotify);

  // Update validity states, but not element state.  We'll update
  // element state later, as part of this attribute change.
  UpdateAllValidityStatesButNotElementState();

  UpdateApzAwareFlag();

  UpdateBarredFromConstraintValidation();

  // Changing type might change auto directionality of this or the assigned slot
  if (IsAutoDirectionalityAssociated(oldType) !=
      IsAutoDirectionalityAssociated(mType)) {
    ResetDirFormAssociatedElement(this, aNotify, HasDirAuto());
  }
  // Special case for <input type=tel> as specified in
  // https://html.spec.whatwg.org/multipage/dom.html#the-directionality
  if (!HasDirAuto() && (oldType == FormControlType::InputTel ||
                        mType == FormControlType::InputTel)) {
    RecomputeDirectionality(this, aNotify);
  }

  if (oldType == FormControlType::InputImage ||
      mType == FormControlType::InputImage) {
    if (oldType == FormControlType::InputImage) {
      // We're no longer an image input.  Cancel our image requests, if we have
      // any.
      CancelImageRequests(aNotify);
      RemoveStates(ElementState::BROKEN, aNotify);
    } else {
      // We just got switched to be an image input; we should see whether we
      // have an image to load;
      bool hasSrc = false;
      if (aNotify) {
        nsAutoString src;
        if ((hasSrc = GetAttr(nsGkAtoms::src, src))) {
          // Mark channel as urgent-start before load image if the image load is
          // initiated by a user interaction.
          mUseUrgentStartForChannel = UserActivation::IsHandlingUserInput();

          LoadImage(src, false, aNotify, eImageLoadType_Normal,
                    mSrcTriggeringPrincipal);
        }
      } else {
        hasSrc = HasAttr(nsGkAtoms::src);
      }
      if (!hasSrc) {
        AddStates(ElementState::BROKEN, aNotify);
      }
    }
    // We should update our mapped attribute mapping function.
    if (mAttrs.HasAttrs() && !mAttrs.IsPendingMappedAttributeEvaluation()) {
      mAttrs.InfallibleMarkAsPendingPresAttributeEvaluation();
      if (auto* doc = GetComposedDoc()) {
        doc->ScheduleForPresAttrEvaluation(this);
      }
    }
  }

  MaybeDispatchLoginManagerEvents(mForm);

  if (IsInComposedDoc()) {
    if (mDoneCreating) {
      const auto oldNotifiesUAWidget = NotifiesUAWidget(oldType);
      if (CreatesUAShadowTree()) {
        if (wasTextControl && isTextControl) {
          // Keep existing shadow
          UpdateTextEditorShadowTree();
        } else {
          const auto notifiesUAWidget = NotifiesUAWidget();
          if (oldNotifiesUAWidget == notifiesUAWidget &&
              notifiesUAWidget == NotifyUAWidget::Yes) {
            NotifyUAWidgetSetupOrChange();
          } else {
            TeardownUAShadowRoot(oldNotifiesUAWidget);
            if (notifiesUAWidget == NotifyUAWidget::Yes) {
              SetupShadowTree(aNotify);
            }
          }
        }
      } else {
        TeardownUAShadowRoot(oldNotifiesUAWidget);
      }
    }
    // If we're becoming a text control and have focus, make sure to show focus
    // rings.
    if (State().HasState(ElementState::FOCUS) && IsSingleLineTextControl() &&
        !IsSingleLineTextControl(/* aExcludePassword = */ false, oldType)) {
      AddStates(ElementState::FOCUSRING);
    }
  }
}

void HTMLInputElement::MaybeSnapToTickMark(Decimal& aValue) {
  nsRangeFrame* rangeFrame = do_QueryFrame(GetPrimaryFrame());
  if (!rangeFrame) {
    return;
  }
  auto tickMark = rangeFrame->NearestTickMark(aValue);
  if (tickMark.isNaN()) {
    return;
  }
  auto rangeFrameSize = CSSPixel::FromAppUnits(rangeFrame->GetSize());
  CSSCoord rangeTrackLength;
  if (rangeFrame->IsHorizontal()) {
    rangeTrackLength = rangeFrameSize.width;
  } else {
    rangeTrackLength = rangeFrameSize.height;
  }
  auto stepBase = GetStepBase();
  auto distanceToTickMark =
      rangeTrackLength * float(rangeFrame->GetDoubleAsFractionOfRange(
                             stepBase + (tickMark - aValue).abs()));
  const CSSCoord magnetEffectRange(
      StaticPrefs::dom_range_element_magnet_effect_threshold());
  if (distanceToTickMark <= magnetEffectRange) {
    aValue = tickMark;
  }
}

void HTMLInputElement::SanitizeValue(nsAString& aValue,
                                     SanitizationKind aKind) const {
  NS_ASSERTION(mDoneCreating, "The element creation should be finished!");

  switch (mType) {
    case FormControlType::InputText:
    case FormControlType::InputSearch:
    case FormControlType::InputTel:
    case FormControlType::InputPassword: {
      aValue.StripCRLF();
    } break;
    case FormControlType::InputEmail: {
      aValue.StripCRLF();
      aValue = nsContentUtils::TrimWhitespace<nsContentUtils::IsHTMLWhitespace>(
          aValue);

      if (Multiple() && !aValue.IsEmpty()) {
        nsAutoString oldValue(aValue);
        HTMLSplitOnSpacesTokenizer tokenizer(oldValue, ',');
        aValue.Truncate(0);
        aValue.Append(tokenizer.nextToken());
        while (tokenizer.hasMoreTokens() ||
               tokenizer.separatorAfterCurrentToken()) {
          aValue.Append(',');
          aValue.Append(tokenizer.nextToken());
        }
      }
    } break;
    case FormControlType::InputUrl: {
      aValue.StripCRLF();

      aValue = nsContentUtils::TrimWhitespace<nsContentUtils::IsHTMLWhitespace>(
          aValue);
    } break;
    case FormControlType::InputNumber: {
      auto result = mInputType->ConvertStringToNumber(
          aValue, aKind == SanitizationKind::ForValueSetter
                      ? InputType::Localized::No
                      : InputType::Localized::Yes);
      if (!result.mResult.isFinite()) {
        aValue.Truncate();
        return;
      }
      switch (aKind) {
        case SanitizationKind::ForValueGetter: {
          // If the default non-localized algorithm parses the value, then we're
          // done, don't un-localize it, to avoid precision loss, and to
          // preserve scientific notation as well for example.
          if (!result.mLocalized) {
            return;
          }
          // For the <input type=number> value getter, we return the unlocalized
          // value if it doesn't parse as StringToDecimal, for compat with other
          // browsers.
          aValue.AssignASCII(result.mResult.toString().c_str());
          break;
        }
        case SanitizationKind::ForDisplay:
        case SanitizationKind::ForValueSetter: {
          // We localize as needed, but if both the localized and unlocalized
          // version parse with the generic parser, we just use the unlocalized
          // one, to preserve the input as much as possible.
          //
          // FIXME(emilio, bug 1622808): Localization should ideally be more
          // input-preserving.
          nsString localizedValue;
          mInputType->ConvertNumberToString(
              result.mResult, InputType::Localized::Yes, localizedValue);
          if (!StringToDecimal(localizedValue).isFinite()) {
            aValue = std::move(localizedValue);
          }
          break;
        }
      }
      break;
    }
    case FormControlType::InputRange: {
      Decimal minimum = GetMinimum();
      Decimal maximum = GetMaximum();
      MOZ_ASSERT(minimum.isFinite() && maximum.isFinite(),
                 "type=range should have a default maximum/minimum");

      // We use this to avoid modifying the string unnecessarily, since that
      // may introduce rounding. This is set to true only if the value we
      // parse out from aValue needs to be sanitized.
      bool needSanitization = false;

      Decimal value =
          mInputType->ConvertStringToNumber(aValue, InputType::Localized::Yes)
              .mResult;
      if (!value.isFinite()) {
        needSanitization = true;
        // Set value to midway between minimum and maximum.
        value = maximum <= minimum ? minimum
                                   : minimum + (maximum - minimum) / Decimal(2);
      } else if (value < minimum || maximum < minimum) {
        needSanitization = true;
        value = minimum;
      } else if (value > maximum) {
        needSanitization = true;
        value = maximum;
      }

      Decimal step = GetStep();
      if (step != kStepAny) {
        Decimal stepBase = GetStepBase();
        // There could be rounding issues below when dealing with fractional
        // numbers, but let's ignore that until ECMAScript supplies us with a
        // decimal number type.
        Decimal deltaToStep = NS_floorModulo(value - stepBase, step);
        if (deltaToStep != Decimal(0)) {
          // "suffering from a step mismatch"
          // Round the element's value to the nearest number for which the
          // element would not suffer from a step mismatch, and which is
          // greater than or equal to the minimum, and, if the maximum is not
          // less than the minimum, which is less than or equal to the
          // maximum, if there is a number that matches these constraints:
          MOZ_ASSERT(deltaToStep > Decimal(0),
                     "stepBelow/stepAbove will be wrong");
          Decimal stepBelow = value - deltaToStep;
          Decimal stepAbove = value - deltaToStep + step;
          Decimal halfStep = step / Decimal(2);
          bool stepAboveIsClosest = (stepAbove - value) <= halfStep;
          bool stepAboveInRange = stepAbove >= minimum && stepAbove <= maximum;
          bool stepBelowInRange = stepBelow >= minimum && stepBelow <= maximum;

          if ((stepAboveIsClosest || !stepBelowInRange) && stepAboveInRange) {
            needSanitization = true;
            value = stepAbove;
          } else if ((!stepAboveIsClosest || !stepAboveInRange) &&
                     stepBelowInRange) {
            needSanitization = true;
            value = stepBelow;
          }
        }
      }

      if (needSanitization) {
        aValue.AssignASCII(value.toString().c_str());
      }
    } break;
    case FormControlType::InputDate: {
      if (!aValue.IsEmpty() && !IsValidDate(aValue)) {
        aValue.Truncate();
      }
    } break;
    case FormControlType::InputTime: {
      if (!aValue.IsEmpty() && !IsValidTime(aValue)) {
        aValue.Truncate();
      }
    } break;
    case FormControlType::InputMonth: {
      if (!aValue.IsEmpty() && !IsValidMonth(aValue)) {
        aValue.Truncate();
      }
    } break;
    case FormControlType::InputWeek: {
      if (!aValue.IsEmpty() && !IsValidWeek(aValue)) {
        aValue.Truncate();
      }
    } break;
    case FormControlType::InputDatetimeLocal: {
      if (!aValue.IsEmpty() && !IsValidDateTimeLocal(aValue)) {
        aValue.Truncate();
      } else {
        NormalizeDateTimeLocal(aValue);
      }
    } break;
    case FormControlType::InputColor: {
      // https://html.spec.whatwg.org/#update-a-color-well-control-color
      // https://html.spec.whatwg.org/#serialize-a-color-well-control-color
      StyleAbsoluteColor color = MaybeComputeColorOrBlack(OwnerDoc(), aValue);
      // Serialization step 6: If htmlCompatible is true, then do so with
      // HTML-compatible serialization requested.
      SerializeColor(color, GetColorSpaceEnum(), Alpha(), aValue);
      break;
    }
    default:
      break;
  }
}

Maybe<nscolor> HTMLInputElement::ParseSimpleColor(const nsAString& aColor) {
  // Input color string should be 7 length (i.e. a string representing a valid
  // simple color)
  if (aColor.Length() != 7 || aColor.First() != '#') {
    return {};
  }

  const nsAString& withoutHash = StringTail(aColor, 6);
  nscolor color;
  if (!NS_HexToRGBA(withoutHash, nsHexColorType::NoAlpha, &color)) {
    return {};
  }

  return Some(color);
}

bool HTMLInputElement::IsLeapYear(uint32_t aYear) const {
  if ((aYear % 4 == 0 && aYear % 100 != 0) || (aYear % 400 == 0)) {
    return true;
  }
  return false;
}

uint32_t HTMLInputElement::DayOfWeek(uint32_t aYear, uint32_t aMonth,
                                     uint32_t aDay, bool isoWeek) const {
  MOZ_ASSERT(1 <= aMonth && aMonth <= 12, "month is in 1..12");
  MOZ_ASSERT(1 <= aDay && aDay <= 31, "day is in 1..31");

  // Tomohiko Sakamoto algorithm.
  int monthTable[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  aYear -= aMonth < 3;

  uint32_t day = (aYear + aYear / 4 - aYear / 100 + aYear / 400 +
                  monthTable[aMonth - 1] + aDay) %
                 7;

  if (isoWeek) {
    return ((day + 6) % 7) + 1;
  }

  return day;
}

uint32_t HTMLInputElement::MaximumWeekInYear(uint32_t aYear) const {
  int day = DayOfWeek(aYear, 1, 1, true);  // January 1.
  // A year starting on Thursday or a leap year starting on Wednesday has 53
  // weeks. All other years have 52 weeks.
  return day == 4 || (day == 3 && IsLeapYear(aYear)) ? kMaximumWeekInYear
                                                     : kMaximumWeekInYear - 1;
}

bool HTMLInputElement::IsValidWeek(const nsAString& aValue) const {
  uint32_t year, week;
  return ParseWeek(aValue, &year, &week);
}

bool HTMLInputElement::IsValidMonth(const nsAString& aValue) const {
  uint32_t year, month;
  return ParseMonth(aValue, &year, &month);
}

bool HTMLInputElement::IsValidDate(const nsAString& aValue) const {
  uint32_t year, month, day;
  return ParseDate(aValue, &year, &month, &day);
}

bool HTMLInputElement::IsValidDateTimeLocal(const nsAString& aValue) const {
  uint32_t year, month, day, time;
  return ParseDateTimeLocal(aValue, &year, &month, &day, &time);
}

bool HTMLInputElement::ParseYear(const nsAString& aValue,
                                 uint32_t* aYear) const {
  if (aValue.Length() < 4) {
    return false;
  }

  return DigitSubStringToNumber(aValue, 0, aValue.Length(), aYear) &&
         *aYear > 0;
}

bool HTMLInputElement::ParseMonth(const nsAString& aValue, uint32_t* aYear,
                                  uint32_t* aMonth) const {
  // Parse the year, month values out a string formatted as 'yyyy-mm'.
  if (aValue.Length() < 7) {
    return false;
  }

  uint32_t endOfYearOffset = aValue.Length() - 3;
  if (aValue[endOfYearOffset] != '-') {
    return false;
  }

  const nsAString& yearStr = Substring(aValue, 0, endOfYearOffset);
  if (!ParseYear(yearStr, aYear)) {
    return false;
  }

  return DigitSubStringToNumber(aValue, endOfYearOffset + 1, 2, aMonth) &&
         *aMonth > 0 && *aMonth <= 12;
}

bool HTMLInputElement::ParseWeek(const nsAString& aValue, uint32_t* aYear,
                                 uint32_t* aWeek) const {
  // Parse the year, month values out a string formatted as 'yyyy-Www'.
  if (aValue.Length() < 8) {
    return false;
  }

  uint32_t endOfYearOffset = aValue.Length() - 4;
  if (aValue[endOfYearOffset] != '-') {
    return false;
  }

  if (aValue[endOfYearOffset + 1] != 'W') {
    return false;
  }

  const nsAString& yearStr = Substring(aValue, 0, endOfYearOffset);
  if (!ParseYear(yearStr, aYear)) {
    return false;
  }

  return DigitSubStringToNumber(aValue, endOfYearOffset + 2, 2, aWeek) &&
         *aWeek > 0 && *aWeek <= MaximumWeekInYear(*aYear);
}

bool HTMLInputElement::ParseDate(const nsAString& aValue, uint32_t* aYear,
                                 uint32_t* aMonth, uint32_t* aDay) const {
  /*
   * Parse the year, month, day values out a date string formatted as
   * yyyy-mm-dd. -The year must be 4 or more digits long, and year > 0 -The
   * month must be exactly 2 digits long, and 01 <= month <= 12 -The day must be
   * exactly 2 digit long, and 01 <= day <= maxday Where maxday is the number of
   * days in the month 'month' and year 'year'
   */
  if (aValue.Length() < 10) {
    return false;
  }

  uint32_t endOfMonthOffset = aValue.Length() - 3;
  if (aValue[endOfMonthOffset] != '-') {
    return false;
  }

  const nsAString& yearMonthStr = Substring(aValue, 0, endOfMonthOffset);
  if (!ParseMonth(yearMonthStr, aYear, aMonth)) {
    return false;
  }

  return DigitSubStringToNumber(aValue, endOfMonthOffset + 1, 2, aDay) &&
         *aDay > 0 && *aDay <= NumberOfDaysInMonth(*aMonth, *aYear);
}

bool HTMLInputElement::ParseDateTimeLocal(const nsAString& aValue,
                                          uint32_t* aYear, uint32_t* aMonth,
                                          uint32_t* aDay,
                                          uint32_t* aTime) const {
  // Parse the year, month, day and time values out a string formatted as
  // 'yyyy-mm-ddThh:mm[:ss.s] or 'yyyy-mm-dd hh:mm[:ss.s]', where fractions of
  // seconds can be 1 to 3 digits.
  // The minimum length allowed is 16, which is of the form 'yyyy-mm-ddThh:mm'
  // or 'yyyy-mm-dd hh:mm'.
  if (aValue.Length() < 16) {
    return false;
  }

  int32_t sepIndex = aValue.FindChar('T');
  if (sepIndex == -1) {
    sepIndex = aValue.FindChar(' ');

    if (sepIndex == -1) {
      return false;
    }
  }

  const nsAString& dateStr = Substring(aValue, 0, sepIndex);
  if (!ParseDate(dateStr, aYear, aMonth, aDay)) {
    return false;
  }

  const nsAString& timeStr =
      Substring(aValue, sepIndex + 1, aValue.Length() - sepIndex + 1);
  if (!ParseTime(timeStr, aTime)) {
    return false;
  }

  return true;
}

void HTMLInputElement::NormalizeDateTimeLocal(nsAString& aValue) const {
  if (aValue.IsEmpty()) {
    return;
  }

  // Use 'T' as the separator between date string and time string.
  int32_t sepIndex = aValue.FindChar(' ');
  if (sepIndex != -1) {
    aValue.ReplaceLiteral(sepIndex, 1, u"T");
  } else {
    sepIndex = aValue.FindChar('T');
  }

  // Time expressed as the shortest possible string, which is hh:mm.
  if ((aValue.Length() - sepIndex) == 6) {
    return;
  }

  // Fractions of seconds part is optional, ommit it if it's 0.
  if ((aValue.Length() - sepIndex) > 9) {
    const uint32_t millisecSepIndex = sepIndex + 9;
    uint32_t milliseconds;
    if (!DigitSubStringToNumber(aValue, millisecSepIndex + 1,
                                aValue.Length() - (millisecSepIndex + 1),
                                &milliseconds)) {
      return;
    }

    if (milliseconds != 0) {
      return;
    }

    aValue.Cut(millisecSepIndex, aValue.Length() - millisecSepIndex);
  }

  // Seconds part is optional, ommit it if it's 0.
  const uint32_t secondSepIndex = sepIndex + 6;
  uint32_t seconds;
  if (!DigitSubStringToNumber(aValue, secondSepIndex + 1,
                              aValue.Length() - (secondSepIndex + 1),
                              &seconds)) {
    return;
  }

  if (seconds != 0) {
    return;
  }

  aValue.Cut(secondSepIndex, aValue.Length() - secondSepIndex);
}

double HTMLInputElement::DaysSinceEpochFromWeek(uint32_t aYear,
                                                uint32_t aWeek) const {
  double days = JS::DayFromYear(aYear) + (aWeek - 1) * 7;
  uint32_t dayOneIsoWeekday = DayOfWeek(aYear, 1, 1, true);

  // If day one of that year is on/before Thursday, we should subtract the
  // days that belong to last year in our first week, otherwise, our first
  // days belong to last year's last week, and we should add those days
  // back.
  if (dayOneIsoWeekday <= 4) {
    days -= (dayOneIsoWeekday - 1);
  } else {
    days += (7 - dayOneIsoWeekday + 1);
  }

  return days;
}

uint32_t HTMLInputElement::NumberOfDaysInMonth(uint32_t aMonth,
                                               uint32_t aYear) const {
  /*
   * Returns the number of days in a month.
   * Months that are |longMonths| always have 31 days.
   * Months that are not |longMonths| have 30 days except February (month 2).
   * February has 29 days during leap years which are years that are divisible
   * by 400. or divisible by 100 and 4. February has 28 days otherwise.
   */

  static const bool longMonths[] = {true, false, true,  false, true,  false,
                                    true, true,  false, true,  false, true};
  MOZ_ASSERT(aMonth <= 12 && aMonth > 0);

  if (longMonths[aMonth - 1]) {
    return 31;
  }

  if (aMonth != 2) {
    return 30;
  }

  return IsLeapYear(aYear) ? 29 : 28;
}

/* static */
bool HTMLInputElement::DigitSubStringToNumber(const nsAString& aStr,
                                              uint32_t aStart, uint32_t aLen,
                                              uint32_t* aRetVal) {
  MOZ_ASSERT(aStr.Length() > (aStart + aLen - 1));

  for (uint32_t offset = 0; offset < aLen; ++offset) {
    if (!IsAsciiDigit(aStr[aStart + offset])) {
      return false;
    }
  }

  nsresult ec;
  *aRetVal = static_cast<uint32_t>(
      PromiseFlatString(Substring(aStr, aStart, aLen)).ToInteger(&ec));

  return NS_SUCCEEDED(ec);
}

bool HTMLInputElement::IsValidTime(const nsAString& aValue) const {
  return ParseTime(aValue, nullptr);
}

/* static */
bool HTMLInputElement::ParseTime(const nsAString& aValue, uint32_t* aResult) {
  /* The string must have the following parts:
   * - HOURS: two digits, value being in [0, 23];
   * - Colon (:);
   * - MINUTES: two digits, value being in [0, 59];
   * - Optional:
   *   - Colon (:);
   *   - SECONDS: two digits, value being in [0, 59];
   *   - Optional:
   *     - DOT (.);
   *     - FRACTIONAL SECONDS: one to three digits, no value range.
   */

  // The following format is the shorter one allowed: "HH:MM".
  if (aValue.Length() < 5) {
    return false;
  }

  uint32_t hours;
  if (!DigitSubStringToNumber(aValue, 0, 2, &hours) || hours > 23) {
    return false;
  }

  // Hours/minutes separator.
  if (aValue[2] != ':') {
    return false;
  }

  uint32_t minutes;
  if (!DigitSubStringToNumber(aValue, 3, 2, &minutes) || minutes > 59) {
    return false;
  }

  if (aValue.Length() == 5) {
    if (aResult) {
      *aResult = ((hours * 60) + minutes) * 60000;
    }
    return true;
  }

  // The following format is the next shorter one: "HH:MM:SS".
  if (aValue.Length() < 8 || aValue[5] != ':') {
    return false;
  }

  uint32_t seconds;
  if (!DigitSubStringToNumber(aValue, 6, 2, &seconds) || seconds > 59) {
    return false;
  }

  if (aValue.Length() == 8) {
    if (aResult) {
      *aResult = (((hours * 60) + minutes) * 60 + seconds) * 1000;
    }
    return true;
  }

  // The string must follow this format now: "HH:MM:SS.{s,ss,sss}".
  // There can be 1 to 3 digits for the fractions of seconds.
  if (aValue.Length() == 9 || aValue.Length() > 12 || aValue[8] != '.') {
    return false;
  }

  uint32_t fractionsSeconds;
  if (!DigitSubStringToNumber(aValue, 9, aValue.Length() - 9,
                              &fractionsSeconds)) {
    return false;
  }

  if (aResult) {
    *aResult = (((hours * 60) + minutes) * 60 + seconds) * 1000 +
               // NOTE: there is 10.0 instead of 10 and static_cast<int> because
               // some old [and stupid] compilers can't just do the right thing.
               fractionsSeconds *
                   pow(10.0, static_cast<int>(3 - (aValue.Length() - 9)));
  }

  return true;
}

/* static */
bool HTMLInputElement::IsDateTimeTypeSupported(
    FormControlType aDateTimeInputType) {
  switch (aDateTimeInputType) {
    case FormControlType::InputDate:
    case FormControlType::InputTime:
    case FormControlType::InputDatetimeLocal:
      return true;
    case FormControlType::InputMonth:
    case FormControlType::InputWeek:
      return StaticPrefs::dom_forms_datetime_others();
    default:
      return false;
  }
}

void HTMLInputElement::GetLastInteractiveValue(nsAString& aValue) {
  if (mLastValueChangeWasInteractive) {
    return GetValue(aValue, CallerType::System);
  }
  if (TextControlState* state = GetEditorState()) {
    return aValue.Assign(
        state->LastInteractiveValueIfLastChangeWasNonInteractive());
  }
  aValue.Truncate();
}

bool HTMLInputElement::ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                                      const nsAString& aValue,
                                      nsIPrincipal* aMaybeScriptedPrincipal,
                                      nsAttrValue& aResult) {
  static_assert(
      FormControlType(kInputDefaultType->value) == FormControlType::InputText,
      "Someone forgot to update kInputDefaultType when adding a new "
      "input type.");
  static_assert(
      FormControlType(kInputTypeTable[std::size(kInputTypeTable) - 1].value) ==
          FormControlType::InputText,
      "Last entry in the table must be the \"text\" entry");

  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::type) {
      aResult.ParseEnumValue(aValue, kInputTypeTable, false, kInputDefaultType);
      auto newType = FormControlType(aResult.GetEnumValue());
      if (IsDateTimeInputType(newType) && !IsDateTimeTypeSupported(newType)) {
        // There's no public way to set an nsAttrValue to an enum value, but we
        // can just re-parse with a table that doesn't have any types other than
        // "text" in it.
        MOZ_ASSERT(&Span(kInputTypeTable).Last<1>()[0] == kInputDefaultType);
        aResult.ParseEnumValue(aValue, Span(kInputTypeTable).Last<1>(), false,
                               kInputDefaultType);
      }

      return true;
    }
    if (aAttribute == nsGkAtoms::width) {
      return aResult.ParseHTMLDimension(aValue);
    }
    if (aAttribute == nsGkAtoms::height) {
      return aResult.ParseHTMLDimension(aValue);
    }
    if (aAttribute == nsGkAtoms::maxlength) {
      return aResult.ParseNonNegativeIntValue(aValue);
    }
    if (aAttribute == nsGkAtoms::minlength) {
      return aResult.ParseNonNegativeIntValue(aValue);
    }
    if (aAttribute == nsGkAtoms::size) {
      return aResult.ParsePositiveIntValue(aValue);
    }
    if (aAttribute == nsGkAtoms::align) {
      return ParseAlignValue(aValue, aResult);
    }
    if (aAttribute == nsGkAtoms::formmethod) {
      return aResult.ParseEnumValue(aValue, kFormMethodTable, false);
    }
    if (aAttribute == nsGkAtoms::formenctype) {
      return aResult.ParseEnumValue(aValue, kFormEnctypeTable, false);
    }
    if (aAttribute == nsGkAtoms::autocomplete) {
      aResult.ParseAtomArray(aValue);
      return true;
    }
    if (aAttribute == nsGkAtoms::capture) {
      return aResult.ParseEnumValue(aValue, kCaptureTable, false,
                                    kCaptureDefault);
    }
    if (aAttribute == nsGkAtoms::colorspace) {
      return aResult.ParseEnumValue(aValue, kColorSpaceTable, false,
                                    kColorSpaceDefault);
    }
    if (ParseImageAttribute(aAttribute, aValue, aResult)) {
      // We have to call |ParseImageAttribute| unconditionally since we
      // don't know if we're going to have a type="image" attribute yet,
      // (or could have it set dynamically in the future).  See bug
      // 214077.
      return true;
    }
  }

  return TextControlElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                            aMaybeScriptedPrincipal, aResult);
}

void HTMLInputElement::ImageInputMapAttributesIntoRule(
    MappedDeclarationsBuilder& aBuilder) {
  nsGenericHTMLFormControlElementWithState::MapImageBorderAttributeInto(
      aBuilder);
  nsGenericHTMLFormControlElementWithState::MapImageMarginAttributeInto(
      aBuilder);
  nsGenericHTMLFormControlElementWithState::MapImageSizeAttributesInto(
      aBuilder, MapAspectRatio::Yes);
  // Images treat align as "float"
  nsGenericHTMLFormControlElementWithState::MapImageAlignAttributeInto(
      aBuilder);
  nsGenericHTMLFormControlElementWithState::MapCommonAttributesInto(aBuilder);
}

nsChangeHint HTMLInputElement::GetAttributeChangeHint(
    const nsAtom* aAttribute, AttrModType aModType) const {
  nsChangeHint retval =
      nsGenericHTMLFormControlElementWithState::GetAttributeChangeHint(
          aAttribute, aModType);

  const bool isAdditionOrRemoval = IsAdditionOrRemoval(aModType);
  const bool reconstruct = [&] {
    if (aAttribute == nsGkAtoms::type) {
      return true;
    }

    if (mType == FormControlType::InputFile &&
        aAttribute == nsGkAtoms::webkitdirectory) {
      // The presence or absence of the 'directory' attribute determines what
      // value we show in the file label when empty, via GetDisplayFileName.
      return true;
    }

    if (mType == FormControlType::InputImage && isAdditionOrRemoval &&
        (aAttribute == nsGkAtoms::alt || aAttribute == nsGkAtoms::value)) {
      // We might need to rebuild our alt text.  Just go ahead and
      // reconstruct our frame.  This should be quite rare..
      return true;
    }
    return false;
  }();

  if (reconstruct) {
    retval |= nsChangeHint_ReconstructFrame;
  } else if (aAttribute == nsGkAtoms::value) {
    retval |= NS_STYLE_HINT_REFLOW;
  } else if (aAttribute == nsGkAtoms::size && IsSingleLineTextControl(false)) {
    retval |= NS_STYLE_HINT_REFLOW;
  }

  return retval;
}

NS_IMETHODIMP_(bool)
HTMLInputElement::IsAttributeMapped(const nsAtom* aAttribute) const {
  static const MappedAttributeEntry attributes[] = {
      {nsGkAtoms::align},
      {nullptr},
  };

  static const MappedAttributeEntry* const map[] = {
      attributes,
      sCommonAttributeMap,
      sImageMarginSizeAttributeMap,
      sImageBorderAttributeMap,
  };

  return FindAttributeDependence(aAttribute, map);
}

nsMapRuleToAttributesFunc HTMLInputElement::GetAttributeMappingFunction()
    const {
  // GetAttributeChangeHint guarantees that changes to mType will trigger a
  // reframe, and we update the mapping function in our mapped attrs when our
  // type changes, so it's safe to condition our attribute mapping function on
  // mType.
  if (mType == FormControlType::InputImage) {
    return &ImageInputMapAttributesIntoRule;
  }

  return &MapCommonAttributesInto;
}

// Directory picking methods:

already_AddRefed<Promise> HTMLInputElement::GetFilesAndDirectories(
    ErrorResult& aRv) {
  if (mType != FormControlType::InputFile) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  MOZ_ASSERT(global);
  if (!global) {
    return nullptr;
  }

  RefPtr<Promise> p = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  const nsTArray<OwningFileOrDirectory>& filesAndDirs =
      GetFilesOrDirectoriesInternal();

  Sequence<OwningFileOrDirectory> filesAndDirsSeq;

  if (!filesAndDirsSeq.SetLength(filesAndDirs.Length(), fallible)) {
    p->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
    return p.forget();
  }

  for (uint32_t i = 0; i < filesAndDirs.Length(); ++i) {
    if (filesAndDirs[i].IsDirectory()) {
      RefPtr<Directory> directory = filesAndDirs[i].GetAsDirectory();

      // In future we could refactor SetFilePickerFiltersFromAccept to return a
      // semicolon separated list of file extensions and include that in the
      // filter string passed here.
      directory->SetContentFilters(u"filter-out-sensitive"_ns);
      filesAndDirsSeq[i].SetAsDirectory() = directory;
    } else {
      MOZ_ASSERT(filesAndDirs[i].IsFile());

      // This file was directly selected by the user, so don't filter it.
      filesAndDirsSeq[i].SetAsFile() = filesAndDirs[i].GetAsFile();
    }
  }

  p->MaybeResolve(filesAndDirsSeq);
  return p.forget();
}

// Controllers Methods

nsIControllers* HTMLInputElement::GetControllers(ErrorResult& aRv) {
  // XXX: what about type "file"?
  if (IsSingleLineTextControl(false)) {
    if (!mControllers) {
      mControllers = new nsXULControllers();
      if (!mControllers) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }

      RefPtr<nsBaseCommandController> commandController =
          nsBaseCommandController::CreateEditorController();
      if (!commandController) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }

      mControllers->AppendController(commandController);

      commandController = nsBaseCommandController::CreateEditingController();
      if (!commandController) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }

      mControllers->AppendController(commandController);
    }
  }

  return GetExtantControllers();
}

nsresult HTMLInputElement::GetControllers(nsIControllers** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  ErrorResult rv;
  RefPtr<nsIControllers> controller = GetControllers(rv);
  controller.forget(aResult);
  return rv.StealNSResult();
}

int32_t HTMLInputElement::InputTextLength(CallerType aCallerType) {
  nsAutoString val;
  GetValue(val, aCallerType);
  return val.Length();
}

void HTMLInputElement::SetSelectionRange(uint32_t aSelectionStart,
                                         uint32_t aSelectionEnd,
                                         const Optional<nsAString>& aDirection,
                                         ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  TextControlState* state = GetEditorState();
  MOZ_ASSERT(state, "SupportsTextSelection() returned true!");
  state->SetSelectionRange(aSelectionStart, aSelectionEnd, aDirection, aRv);
}

void HTMLInputElement::SetRangeText(const nsAString& aReplacement,
                                    ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  TextControlState* state = GetEditorState();
  MOZ_ASSERT(state, "SupportsTextSelection() returned true!");
  state->SetRangeText(aReplacement, aRv);
}

void HTMLInputElement::SetRangeText(const nsAString& aReplacement,
                                    uint32_t aStart, uint32_t aEnd,
                                    SelectionMode aSelectMode,
                                    ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  TextControlState* state = GetEditorState();
  MOZ_ASSERT(state, "SupportsTextSelection() returned true!");
  state->SetRangeText(aReplacement, aStart, aEnd, aSelectMode, aRv);
}

void HTMLInputElement::GetValueFromSetRangeText(nsAString& aValue) {
  GetNonFileValueInternal(aValue);
}

nsresult HTMLInputElement::SetValueFromSetRangeText(const nsAString& aValue) {
  return SetValueInternal(aValue, {ValueSetterOption::ByContentAPI,
                                   ValueSetterOption::BySetRangeTextAPI,
                                   ValueSetterOption::SetValueChanged});
}

Nullable<uint32_t> HTMLInputElement::GetSelectionStart(ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    return Nullable<uint32_t>();
  }

  uint32_t selStart = GetSelectionStartIgnoringType(aRv);
  if (aRv.Failed()) {
    return Nullable<uint32_t>();
  }

  return Nullable<uint32_t>(selStart);
}

uint32_t HTMLInputElement::GetSelectionStartIgnoringType(ErrorResult& aRv) {
  uint32_t selEnd = 0, selStart = 0;
  GetSelectionRange(&selStart, &selEnd, aRv);
  return selStart;
}

void HTMLInputElement::SetSelectionStart(
    const Nullable<uint32_t>& aSelectionStart, ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  TextControlState* state = GetEditorState();
  MOZ_ASSERT(state, "SupportsTextSelection() returned true!");
  state->SetSelectionStart(aSelectionStart, aRv);
}

Nullable<uint32_t> HTMLInputElement::GetSelectionEnd(ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    return Nullable<uint32_t>();
  }

  uint32_t selEnd = GetSelectionEndIgnoringType(aRv);
  if (aRv.Failed()) {
    return Nullable<uint32_t>();
  }

  return Nullable<uint32_t>(selEnd);
}

uint32_t HTMLInputElement::GetSelectionEndIgnoringType(ErrorResult& aRv) {
  uint32_t selEnd = 0, selStart = 0;
  GetSelectionRange(&selStart, &selEnd, aRv);
  return selEnd;
}

void HTMLInputElement::SetSelectionEnd(const Nullable<uint32_t>& aSelectionEnd,
                                       ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  TextControlState* state = GetEditorState();
  MOZ_ASSERT(state, "SupportsTextSelection() returned true!");
  state->SetSelectionEnd(aSelectionEnd, aRv);
}

void HTMLInputElement::GetSelectionRange(uint32_t* aSelectionStart,
                                         uint32_t* aSelectionEnd,
                                         ErrorResult& aRv) {
  TextControlState* state = GetEditorState();
  if (!state) {
    // Not a text control.
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  state->GetSelectionRange(aSelectionStart, aSelectionEnd, aRv);
}

void HTMLInputElement::GetSelectionDirection(nsAString& aDirection,
                                             ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    aDirection.SetIsVoid(true);
    return;
  }

  TextControlState* state = GetEditorState();
  MOZ_ASSERT(state, "SupportsTextSelection came back true!");
  state->GetSelectionDirectionString(aDirection, aRv);
}

void HTMLInputElement::SetSelectionDirection(const nsAString& aDirection,
                                             ErrorResult& aRv) {
  if (!SupportsTextSelection()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  TextControlState* state = GetEditorState();
  MOZ_ASSERT(state, "SupportsTextSelection came back true!");
  state->SetSelectionDirection(aDirection, aRv);
}

// https://html.spec.whatwg.org/multipage/input.html#dom-input-showpicker
void HTMLInputElement::ShowPicker(ErrorResult& aRv) {
  // Step 1. If this is not mutable, then throw an "InvalidStateError"
  // DOMException.
  if (!IsMutable()) {
    return aRv.ThrowInvalidStateError(
        "This input is either disabled or readonly.");
  }

  // Step 2. If this's relevant settings object's origin is not same origin with
  // this's relevant settings object's top-level origin, and this's type
  // attribute is not in the File Upload state or Color state, then throw a
  // "SecurityError" DOMException.
  if (mType != FormControlType::InputFile &&
      mType != FormControlType::InputColor) {
    nsPIDOMWindowInner* window = OwnerDoc()->GetInnerWindow();
    WindowGlobalChild* windowGlobalChild =
        window ? window->GetWindowGlobalChild() : nullptr;
    if (!windowGlobalChild || !windowGlobalChild->SameOriginWithTop()) {
      return aRv.ThrowSecurityError(
          "Call was blocked because the current origin isn't same-origin with "
          "top.");
    }
  }

  // Step 3. If this's relevant global object does not have transient
  // activation, then throw a "NotAllowedError" DOMException.
  if (!OwnerDoc()->HasValidTransientUserGestureActivation()) {
    return aRv.ThrowNotAllowedError(
        "Call was blocked due to lack of user activation.");
  }

  // Step 4. Show the picker, if applicable, for this.
  //
  // https://html.spec.whatwg.org/multipage/input.html#show-the-picker,-if-applicable
  // To show the picker, if applicable for an input element element:

  // Step 1. Assert: element's relevant global object has transient activation.
  // Step 2. If element is not mutable, then return.
  // (See above.)

  // Step 3. Consume user activation given element's relevant global object.
  // InitFilePicker() and InitColorPicker() consume it themselves,
  // so only consume in this function if not those.

  // Step 5. If element's type attribute is in the File Upload state, then run
  // these steps in parallel:
  if (mType == FormControlType::InputFile) {
    FilePickerType type = FILE_PICKER_FILE;
    if (StaticPrefs::dom_webkitBlink_dirPicker_enabled() &&
        HasAttr(nsGkAtoms::webkitdirectory)) {
      type = FILE_PICKER_DIRECTORY;
    }
    InitFilePicker(type);
    return;
  }

  // Step 6. Otherwise, the user agent should show any relevant user interface
  // for selecting a value for element, in the way it normally would when the
  // user interacts with the control

  // Step 6 for color
  if (mType == FormControlType::InputColor) {
    InitColorPicker();
    return;
  }

  // See Step 3.
  OwnerDoc()->ConsumeTransientUserGestureActivation();

  if (!IsInComposedDoc()) {
    return;
  }

  // Step 6 for date and time types
  if (IsDateTimeTypeSupported(mType)) {
    if (CreatesDateTimeWidget()) {
      if (RefPtr<Element> dateTimeBoxElement = GetDateTimeBoxElement()) {
        // Event is dispatched to closed-shadow tree and doesn't bubble.
        RefPtr<Document> doc = OwnerDoc();
        nsContentUtils::DispatchTrustedEvent(doc, dateTimeBoxElement,
                                             u"MozDateTimeShowPickerForJS"_ns,
                                             CanBubble::eNo, Cancelable::eNo);
      }
    } else {
      DateTimeValue value;
      GetDateTimeInputBoxValue(value);
      OpenDateTimePicker(value);
    }
    return;
  }

  // Step 6 for input elements with a suggestions source element.
  // I.e. show the autocomplete dropdown based on the list attribute.
  // XXX Form-fill support on android is bug 1535985.
  if (StaticPrefs::dom_input_showPicker_datalist_enabled() &&
      IsSingleLineTextControl(true) && GetListInternal()) {
    if (nsCOMPtr<nsIFormFillController> controller =
            do_GetService("@mozilla.org/satchel/form-fill-controller;1")) {
      controller->SetControlledElement(this);
      controller->ShowPopup();
    }
  }
}

void HTMLInputElement::UpdateApzAwareFlag() {
#if !defined(ANDROID) && !defined(XP_MACOSX)
  if (mType == FormControlType::InputNumber ||
      mType == FormControlType::InputRange) {
    SetMayBeApzAware();
  }
#endif
}

nsresult HTMLInputElement::SetDefaultValueAsValue() {
  NS_ASSERTION(GetValueMode() == VALUE_MODE_VALUE,
               "GetValueMode() should return VALUE_MODE_VALUE!");

  // The element has a content attribute value different from it's value when
  // it's in the value mode value.
  nsAutoString resetVal;
  GetDefaultValue(resetVal);

  // SetValueInternal is going to sanitize the value.
  // TODO(mbrodesser): sanitizing will only happen if `mDoneCreating` is true.
  return SetValueInternal(resetVal, ValueSetterOption::ByInternalAPI);
}

NS_IMETHODIMP
HTMLInputElement::Reset() {
  // We should be able to reset all dirty flags regardless of the type.
  SetCheckedChanged(false);
  SetValueChanged(false);
  SetLastValueChangeWasInteractive(false);
  SetUserInteracted(false);

  switch (GetValueMode()) {
    case VALUE_MODE_VALUE: {
      nsresult result = SetDefaultValueAsValue();
      if (CreatesDateTimeWidget()) {
        // mFocusedValue has to be set here, so that `FireChangeEventIfNeeded`
        // can fire a change event if necessary.
        GetValue(mFocusedValue, CallerType::System);
      }
      return result;
    }
    case VALUE_MODE_DEFAULT_ON:
      DoSetChecked(DefaultChecked(), /* aNotify */ true,
                   /* aSetValueChanged */ false);
      return NS_OK;
    case VALUE_MODE_FILENAME:
      ClearFiles(false);
      return NS_OK;
    case VALUE_MODE_DEFAULT:
    default:
      return NS_OK;
  }
}

NS_IMETHODIMP
HTMLInputElement::SubmitNamesValues(FormData* aFormData) {
  // For type=reset, and type=button, we just never submit, period.
  // For type=image and type=button, we only submit if we were the button
  // pressed
  // For type=radio and type=checkbox, we only submit if checked=true
  if (mType == FormControlType::InputReset ||
      mType == FormControlType::InputButton ||
      ((mType == FormControlType::InputSubmit ||
        mType == FormControlType::InputImage) &&
       aFormData->GetSubmitterElement() != this) ||
      ((mType == FormControlType::InputRadio ||
        mType == FormControlType::InputCheckbox) &&
       !mChecked)) {
    return NS_OK;
  }

  // Get the name
  nsAutoString name;
  GetAttr(nsGkAtoms::name, name);

  // Submit .x, .y for input type=image
  if (mType == FormControlType::InputImage) {
    // Get a property set by the frame to find out where it was clicked.
    const auto* lastClickedPoint =
        static_cast<CSSIntPoint*>(GetProperty(nsGkAtoms::imageClickedPoint));
    int32_t x, y;
    if (lastClickedPoint) {
      // Convert the values to strings for submission
      x = lastClickedPoint->x;
      y = lastClickedPoint->y;
    } else {
      x = y = 0;
    }

    nsAutoString xVal, yVal;
    xVal.AppendInt(x);
    yVal.AppendInt(y);

    if (!name.IsEmpty()) {
      aFormData->AddNameValuePair(name + u".x"_ns, xVal);
      aFormData->AddNameValuePair(name + u".y"_ns, yVal);
    } else {
      // If the Image Element has no name, simply return x and y
      // to Nav and IE compatibility.
      aFormData->AddNameValuePair(u"x"_ns, xVal);
      aFormData->AddNameValuePair(u"y"_ns, yVal);
    }

    return NS_OK;
  }

  // If name not there, don't submit
  if (name.IsEmpty()) {
    return NS_OK;
  }

  //
  // Submit file if its input type=file and this encoding method accepts files
  //
  if (mType == FormControlType::InputFile) {
    // Submit files

    const nsTArray<OwningFileOrDirectory>& files =
        GetFilesOrDirectoriesInternal();

    if (files.IsEmpty()) {
      NS_ENSURE_STATE(GetRelevantGlobal());
      ErrorResult rv;
      RefPtr<Blob> blob = Blob::CreateStringBlob(
          GetRelevantGlobal(), ""_ns, u"application/octet-stream"_ns);
      RefPtr<File> file = blob->ToFile(u""_ns, rv);

      if (!rv.Failed()) {
        aFormData->AddNameBlobPair(name, file);
      }

      return rv.StealNSResult();
    }

    for (uint32_t i = 0; i < files.Length(); ++i) {
      if (files[i].IsFile()) {
        aFormData->AddNameBlobPair(name, files[i].GetAsFile());
      } else {
        MOZ_ASSERT(files[i].IsDirectory());
        aFormData->AddNameDirectoryPair(name, files[i].GetAsDirectory());
      }
    }

    return NS_OK;
  }

  if (mType == FormControlType::InputHidden &&
      name.LowerCaseEqualsLiteral("_charset_")) {
    nsCString charset;
    aFormData->GetCharset(charset);
    return aFormData->AddNameValuePair(name, NS_ConvertASCIItoUTF16(charset));
  }

  //
  // Submit name=value
  //

  // Get the value
  nsAutoString value;
  GetValue(value, CallerType::System);

  if (mType == FormControlType::InputSubmit && value.IsEmpty() &&
      !HasAttr(nsGkAtoms::value)) {
    // Get our default value, which is the same as our default label
    nsAutoString defaultValue;
    nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                            "Submit", OwnerDoc(), defaultValue);
    value = defaultValue;
  }

  const nsresult rv = aFormData->AddNameValuePair(name, value);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Submit dirname=dir
  if (IsAutoDirectionalityAssociated()) {
    return SubmitDirnameDir(aFormData);
  }

  return NS_OK;
}

static nsTArray<FileContentData> SaveFileContentData(
    const nsTArray<OwningFileOrDirectory>& aArray) {
  nsTArray<FileContentData> res(aArray.Length());
  for (const auto& it : aArray) {
    if (it.IsFile()) {
      RefPtr<BlobImpl> impl = it.GetAsFile()->Impl();
      res.AppendElement(std::move(impl));
    } else {
      MOZ_ASSERT(it.IsDirectory());
      nsString fullPath;
      nsresult rv = it.GetAsDirectory()->GetFullRealPath(fullPath);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }
      res.AppendElement(std::move(fullPath));
    }
  }
  return res;
}

void HTMLInputElement::SaveState() {
  PresState* state = nullptr;
  switch (GetValueMode()) {
    case VALUE_MODE_DEFAULT_ON:
      if (mCheckedChanged) {
        state = GetPrimaryPresState();
        if (!state) {
          return;
        }

        state->contentData() = CheckedContentData(mChecked);
      }
      break;
    case VALUE_MODE_FILENAME:
      if (!mFileData->mFilesOrDirectories.IsEmpty()) {
        state = GetPrimaryPresState();
        if (!state) {
          return;
        }

        state->contentData() =
            SaveFileContentData(mFileData->mFilesOrDirectories);
      }
      break;
    case VALUE_MODE_VALUE:
    case VALUE_MODE_DEFAULT:
      // VALUE_MODE_DEFAULT shouldn't have their value saved except 'hidden',
      // mType should have never been FormControlType::InputPassword and value
      // should have changed.
      if ((GetValueMode() == VALUE_MODE_DEFAULT &&
           mType != FormControlType::InputHidden) ||
          mHasBeenTypePassword || !mValueChanged) {
        break;
      }

      state = GetPrimaryPresState();
      if (!state) {
        return;
      }

      nsAutoString value;
      GetValue(value, CallerType::System);

      if (!IsSingleLineTextControl(false) &&
          NS_FAILED(nsLinebreakConverter::ConvertStringLineBreaks(
              value, nsLinebreakConverter::eLinebreakPlatform,
              nsLinebreakConverter::eLinebreakContent))) {
        NS_ERROR("Converting linebreaks failed!");
        return;
      }

      state->contentData() =
          TextContentData(value, mLastValueChangeWasInteractive);
      break;
  }

  if (mDisabledChanged) {
    if (!state) {
      state = GetPrimaryPresState();
    }
    if (state) {
      // We do not want to save the real disabled state but the disabled
      // attribute.
      state->disabled() = HasAttr(nsGkAtoms::disabled);
      state->disabledSet() = true;
    }
  }
}

void HTMLInputElement::DoneCreatingElement() {
  MOZ_ASSERT(!mDoneCreating);
  mDoneCreating = true;

  //
  // Restore state as needed.  Note that disabled state applies to all control
  // types.
  //
  bool restoredCheckedState = false;
  if (!mInhibitRestoration) {
    GenerateStateKey();
    restoredCheckedState = RestoreFormControlState();
  }

  //
  // If restore does not occur, we initialize .checked using the CHECKED
  // property.
  //
  if (!restoredCheckedState && mShouldInitChecked) {
    DoSetChecked(DefaultChecked(), /* aNotify */ false,
                 /* aSetValueChanged */ false, mForm || IsInComposedDoc());
  }

  // Sanitize the value and potentially set mFocusedValue.
  if (GetValueMode() == VALUE_MODE_VALUE) {
    nsAutoString value;
    GetValue(value, CallerType::System);
    // TODO: What should we do if SetValueInternal fails?  (The allocation
    // may potentially be big, but most likely we've failed to allocate
    // before the type change.)
    SetValueInternal(value, ValueSetterOption::ByInternalAPI);

    if (CreatesDateTimeWidget()) {
      // mFocusedValue has to be set here, so that `FireChangeEventIfNeeded` can
      // fire a change event if necessary.
      mFocusedValue = value;
    }
  }

  if (CreatesDateTimeWidget() && IsInComposedDoc()) {
    SetupShadowTree(/* aNotify = */ false);
  }

  mShouldInitChecked = false;
}

void HTMLInputElement::DestroyContent() {
  nsImageLoadingContent::Destroy();
  TextControlElement::DestroyContent();
}

void HTMLInputElement::UpdateValidityElementStates(bool aNotify) {
  AutoStateChangeNotifier notifier(*this, aNotify);
  RemoveStatesSilently(ElementState::VALIDITY_STATES);
  if (!IsCandidateForConstraintValidation()) {
    return;
  }
  ElementState state;
  if (IsValid()) {
    state |= ElementState::VALID;
    if (mUserInteracted) {
      state |= ElementState::USER_VALID;
    }
  } else {
    state |= ElementState::INVALID;
    if (mUserInteracted) {
      state |= ElementState::USER_INVALID;
    }
  }
  AddStatesSilently(state);
}

static nsTArray<OwningFileOrDirectory> RestoreFileContentData(
    nsPIDOMWindowInner* aWindow, const nsTArray<FileContentData>& aData) {
  nsTArray<OwningFileOrDirectory> res(aData.Length());
  for (const auto& it : aData) {
    if (it.type() == FileContentData::TBlobImpl) {
      if (!it.get_BlobImpl()) {
        // Serialization failed, skip this file.
        continue;
      }

      RefPtr<File> file = File::Create(aWindow->AsGlobal(), it.get_BlobImpl());
      if (NS_WARN_IF(!file)) {
        continue;
      }

      OwningFileOrDirectory* element = res.AppendElement();
      element->SetAsFile() = file;
    } else {
      MOZ_ASSERT(it.type() == FileContentData::TnsString);
      nsCOMPtr<nsIFile> file;
      nsresult rv = NS_NewLocalFile(it.get_nsString(), getter_AddRefs(file));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }

      RefPtr<Directory> directory =
          Directory::Create(aWindow->AsGlobal(), file);
      MOZ_ASSERT(directory);

      OwningFileOrDirectory* element = res.AppendElement();
      element->SetAsDirectory() = directory;
    }
  }
  return res;
}

bool HTMLInputElement::RestoreState(PresState* aState) {
  bool restoredCheckedState = false;

  const PresContentData& inputState = aState->contentData();

  switch (GetValueMode()) {
    case VALUE_MODE_DEFAULT_ON:
      if (inputState.type() == PresContentData::TCheckedContentData) {
        restoredCheckedState = true;
        bool checked = inputState.get_CheckedContentData().checked();
        DoSetChecked(checked, /* aNotify */ true, /* aSetValueChanged */ true);
      }
      break;
    case VALUE_MODE_FILENAME:
      if (inputState.type() == PresContentData::TArrayOfFileContentData) {
        nsPIDOMWindowInner* window = OwnerDoc()->GetInnerWindow();
        if (window) {
          nsTArray<OwningFileOrDirectory> array =
              RestoreFileContentData(window, inputState);
          SetFilesOrDirectories(array, true);
        }
      }
      break;
    case VALUE_MODE_VALUE:
    case VALUE_MODE_DEFAULT:
      if (GetValueMode() == VALUE_MODE_DEFAULT &&
          mType != FormControlType::InputHidden) {
        break;
      }

      if (inputState.type() == PresContentData::TTextContentData) {
        // TODO: What should we do if SetValueInternal fails?  (The allocation
        // may potentially be big, but most likely we've failed to allocate
        // before the type change.)
        SetValueInternal(inputState.get_TextContentData().value(),
                         ValueSetterOption::SetValueChanged);
        if (inputState.get_TextContentData().lastValueChangeWasInteractive()) {
          SetLastValueChangeWasInteractive(true);
        }
      }
      break;
  }

  if (aState->disabledSet() && !aState->disabled()) {
    SetDisabled(false, IgnoreErrors());
  }

  return restoredCheckedState;
}

/*
 * Radio group stuff
 */

void HTMLInputElement::AddToRadioGroup() {
  MOZ_ASSERT(!mRadioGroupContainer,
             "Radio button must be removed from previous radio group container "
             "before being added to another!");

  // If the element has no radio group container we can stop here.
  auto* container = FindTreeRadioGroupContainer();
  if (!container) {
    return;
  }

  nsAutoString name;
  GetAttr(nsGkAtoms::name, name);
  // If we are part of a radio group, the element must have a name.
  MOZ_ASSERT(!name.IsEmpty());

  //
  // Add the radio to the radio group container.
  //
  container->AddToRadioGroup(name, this, mForm);
  mRadioGroupContainer = container;

  //
  // If the input element is checked, and we add it to the group, it will
  // deselect whatever is currently selected in that group
  //
  if (mChecked) {
    //
    // If it is checked, call "RadioSetChecked" to perform the selection/
    // deselection ritual.  This has the side effect of repainting the
    // radio button, but as adding a checked radio button into the group
    // should not be that common an occurrence, I think we can live with
    // that.
    // Make sure not to notify if we're still being created.
    //
    RadioSetChecked(mDoneCreating, mForm || IsInComposedDoc());
  } else {
    bool indeterminate = !container->GetCurrentRadioButton(name);
    SetStates(ElementState::INDETERMINATE, indeterminate, mDoneCreating);
  }

  //
  // For integrity purposes, we have to ensure that "checkedChanged" is
  // the same for this new element as for all the others in the group
  //
  bool checkedChanged = mCheckedChanged;

  VisitGroup([&checkedChanged](HTMLInputElement* aRadio) {
    checkedChanged = aRadio->GetCheckedChanged();
    return false;
  });

  SetCheckedChangedInternal(checkedChanged);

  // We initialize the validity of the element to the validity of the group
  // because we assume UpdateValueMissingState() will be called after.
  SetValidityState(VALIDITY_STATE_VALUE_MISSING,
                   container->GetValueMissingState(name));
}

void HTMLInputElement::RemoveFromRadioGroup() {
  auto* container = GetCurrentRadioGroupContainer();
  if (!container) {
    return;
  }

  nsAutoString name;
  GetAttr(nsGkAtoms::name, name);

  // If this button was checked, we need to notify the group that there is no
  // longer a selected radio button
  if (mChecked) {
    container->SetCurrentRadioButton(name, nullptr);
    UpdateRadioGroupState();
  } else {
    AddStates(ElementState::INDETERMINATE);
  }

  // Remove this radio from its group in the container.
  // We need to call UpdateValueMissingValidityStateForRadio before to make sure
  // the group validity is updated (with this element being ignored).
  UpdateValueMissingValidityStateForRadio(true);
  container->RemoveFromRadioGroup(name, this);
  mRadioGroupContainer = nullptr;
}

bool HTMLInputElement::IsHTMLFocusable(IsFocusableFlags aFlags,
                                       bool* aIsFocusable, int32_t* aTabIndex) {
  if (nsGenericHTMLFormControlElementWithState::IsHTMLFocusable(
          aFlags, aIsFocusable, aTabIndex)) {
    return true;
  }

  if (IsDisabled()) {
    *aIsFocusable = false;
    return true;
  }

  if (IsSingleLineTextControl(false) || mType == FormControlType::InputRange) {
    *aIsFocusable = true;
    return false;
  }

  const bool defaultFocusable = IsFormControlDefaultFocusable(aFlags);
  if (CreatesDateTimeWidget()) {
    if (aTabIndex) {
      // We only want our native anonymous child to be tabable to, not ourself.
      *aTabIndex = -1;
    }
    *aIsFocusable = true;
    return true;
  }

  if (mType == FormControlType::InputHidden) {
    if (aTabIndex) {
      *aTabIndex = -1;
    }
    *aIsFocusable = false;
    return false;
  }

  if (!aTabIndex) {
    // The other controls are all focusable
    *aIsFocusable = defaultFocusable;
    return false;
  }

  if (mType != FormControlType::InputRadio) {
    *aIsFocusable = defaultFocusable;
    return false;
  }

  if (mChecked) {
    // Selected radio buttons are tabbable
    *aIsFocusable = defaultFocusable;
    return false;
  }

  // Current radio button is not selected.
  // Make it tabbable if nothing in group is selected and it is the first radio
  // button.
  auto* container = GetCurrentRadioGroupContainer();
  if (!container) {
    *aIsFocusable = defaultFocusable;
    return false;
  }

  nsAutoString name;
  GetAttr(nsGkAtoms::name, name);

  // If there is a selected radio button but it is disabled or hidden, it
  // shouldn't be considered as selected for this check. Otherwise, the entire
  // group will be unreachable with the tab key.
  HTMLInputElement* selectedRadio = container->GetCurrentRadioButton(name);
  if ((selectedRadio && !selectedRadio->Disabled() &&
       selectedRadio->GetPrimaryFrame()) ||
      container->GetFirstRadioButton(name) != this) {
    *aTabIndex = -1;
  }
  *aIsFocusable = defaultFocusable;
  return false;
}

template <typename VisitCallback>
void HTMLInputElement::VisitGroup(VisitCallback&& aCallback, bool aSkipThis) {
  if (auto* container = GetCurrentRadioGroupContainer()) {
    nsAutoString name;
    GetAttr(nsGkAtoms::name, name);
    container->WalkRadioGroup(name, aCallback, aSkipThis ? this : nullptr);
    return;
  }

  aCallback(this);
}

HTMLInputElement::ValueModeType HTMLInputElement::GetValueMode() const {
  switch (mType) {
    case FormControlType::InputHidden:
    case FormControlType::InputSubmit:
    case FormControlType::InputButton:
    case FormControlType::InputReset:
    case FormControlType::InputImage:
      return VALUE_MODE_DEFAULT;
    case FormControlType::InputCheckbox:
    case FormControlType::InputRadio:
      return VALUE_MODE_DEFAULT_ON;
    case FormControlType::InputFile:
      return VALUE_MODE_FILENAME;
#ifdef DEBUG
    case FormControlType::InputText:
    case FormControlType::InputPassword:
    case FormControlType::InputSearch:
    case FormControlType::InputTel:
    case FormControlType::InputEmail:
    case FormControlType::InputUrl:
    case FormControlType::InputNumber:
    case FormControlType::InputRange:
    case FormControlType::InputDate:
    case FormControlType::InputTime:
    case FormControlType::InputColor:
    case FormControlType::InputMonth:
    case FormControlType::InputWeek:
    case FormControlType::InputDatetimeLocal:
      return VALUE_MODE_VALUE;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected input type in GetValueMode()");
      return VALUE_MODE_VALUE;
#else   // DEBUG
    default:
      return VALUE_MODE_VALUE;
#endif  // DEBUG
  }
}

bool HTMLInputElement::IsMutable() const {
  return !IsDisabled() &&
         !(DoesReadWriteApply() && State().HasState(ElementState::READONLY));
}

bool HTMLInputElement::DoesRequiredApply() const {
  switch (mType) {
    case FormControlType::InputHidden:
    case FormControlType::InputButton:
    case FormControlType::InputImage:
    case FormControlType::InputReset:
    case FormControlType::InputSubmit:
    case FormControlType::InputRange:
    case FormControlType::InputColor:
      return false;
#ifdef DEBUG
    case FormControlType::InputRadio:
    case FormControlType::InputCheckbox:
    case FormControlType::InputFile:
    case FormControlType::InputText:
    case FormControlType::InputPassword:
    case FormControlType::InputSearch:
    case FormControlType::InputTel:
    case FormControlType::InputEmail:
    case FormControlType::InputUrl:
    case FormControlType::InputNumber:
    case FormControlType::InputDate:
    case FormControlType::InputTime:
    case FormControlType::InputMonth:
    case FormControlType::InputWeek:
    case FormControlType::InputDatetimeLocal:
      return true;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected input type in DoesRequiredApply()");
      return true;
#else   // DEBUG
    default:
      return true;
#endif  // DEBUG
  }
}

bool HTMLInputElement::PlaceholderApplies() const {
  if (IsDateTimeInputType(mType)) {
    return false;
  }
  return IsSingleLineTextControl(false);
}

bool HTMLInputElement::DoesMinMaxApply() const {
  switch (mType) {
    case FormControlType::InputNumber:
    case FormControlType::InputDate:
    case FormControlType::InputTime:
    case FormControlType::InputRange:
    case FormControlType::InputMonth:
    case FormControlType::InputWeek:
    case FormControlType::InputDatetimeLocal:
      return true;
#ifdef DEBUG
    case FormControlType::InputReset:
    case FormControlType::InputSubmit:
    case FormControlType::InputImage:
    case FormControlType::InputButton:
    case FormControlType::InputHidden:
    case FormControlType::InputRadio:
    case FormControlType::InputCheckbox:
    case FormControlType::InputFile:
    case FormControlType::InputText:
    case FormControlType::InputPassword:
    case FormControlType::InputSearch:
    case FormControlType::InputTel:
    case FormControlType::InputEmail:
    case FormControlType::InputUrl:
    case FormControlType::InputColor:
      return false;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected input type in DoesRequiredApply()");
      return false;
#else   // DEBUG
    default:
      return false;
#endif  // DEBUG
  }
}

bool HTMLInputElement::DoesAutocompleteApply() const {
  switch (mType) {
    case FormControlType::InputHidden:
    case FormControlType::InputText:
    case FormControlType::InputSearch:
    case FormControlType::InputUrl:
    case FormControlType::InputTel:
    case FormControlType::InputEmail:
    case FormControlType::InputPassword:
    case FormControlType::InputDate:
    case FormControlType::InputTime:
    case FormControlType::InputNumber:
    case FormControlType::InputRange:
    case FormControlType::InputColor:
    case FormControlType::InputMonth:
    case FormControlType::InputWeek:
    case FormControlType::InputDatetimeLocal:
      return true;
#ifdef DEBUG
    case FormControlType::InputReset:
    case FormControlType::InputSubmit:
    case FormControlType::InputImage:
    case FormControlType::InputButton:
    case FormControlType::InputRadio:
    case FormControlType::InputCheckbox:
    case FormControlType::InputFile:
      return false;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Unexpected input type in DoesAutocompleteApply()");
      return false;
#else   // DEBUG
    default:
      return false;
#endif  // DEBUG
  }
}

Decimal HTMLInputElement::GetStep() const {
  MOZ_ASSERT(DoesStepApply(), "GetStep() can only be called if @step applies");

  if (!HasAttr(nsGkAtoms::step)) {
    return GetDefaultStep() * GetStepScaleFactor();
  }

  nsAutoString stepStr;
  GetAttr(nsGkAtoms::step, stepStr);

  if (stepStr.LowerCaseEqualsLiteral("any")) {
    // The element can't suffer from step mismatch if there is no step.
    return kStepAny;
  }

  Decimal step = StringToDecimal(stepStr);
  if (!step.isFinite() || step <= Decimal(0)) {
    step = GetDefaultStep();
  }

  // For input type=date, we round the step value to have a rounded day.
  if (mType == FormControlType::InputDate ||
      mType == FormControlType::InputMonth ||
      mType == FormControlType::InputWeek) {
    step = std::max(step.round(), Decimal(1));
  }

  return step * GetStepScaleFactor();
}

// ConstraintValidation

void HTMLInputElement::SetCustomValidity(const nsAString& aError) {
  ConstraintValidation::SetCustomValidity(aError);
  UpdateValidityElementStates(true);
}

bool HTMLInputElement::IsTooLong() {
  if (!mValueChanged || !mLastValueChangeWasInteractive) {
    return false;
  }

  return mInputType->IsTooLong();
}

bool HTMLInputElement::IsTooShort() {
  if (!mValueChanged || !mLastValueChangeWasInteractive) {
    return false;
  }

  return mInputType->IsTooShort();
}

bool HTMLInputElement::IsValueMissing() const {
  // Should use UpdateValueMissingValidityStateForRadio() for type radio.
  MOZ_ASSERT(mType != FormControlType::InputRadio);

  return mInputType->IsValueMissing();
}

bool HTMLInputElement::HasTypeMismatch() const {
  return mInputType->HasTypeMismatch();
}

Maybe<bool> HTMLInputElement::HasPatternMismatch() const {
  return mInputType->HasPatternMismatch();
}

bool HTMLInputElement::IsRangeOverflow() const {
  return mInputType->IsRangeOverflow();
}

bool HTMLInputElement::IsRangeUnderflow() const {
  return mInputType->IsRangeUnderflow();
}

bool HTMLInputElement::ValueIsStepMismatch(const Decimal& aValue) const {
  if (aValue.isNaN()) {
    // The element can't suffer from step mismatch if its value isn't a
    // number.
    return false;
  }

  Decimal step = GetStep();
  if (step == kStepAny) {
    return false;
  }

  // Value has to be an integral multiple of step.
  return NS_floorModulo(aValue - GetStepBase(), step) != Decimal(0);
}

bool HTMLInputElement::HasStepMismatch() const {
  return mInputType->HasStepMismatch();
}

bool HTMLInputElement::HasBadInput() const { return mInputType->HasBadInput(); }

void HTMLInputElement::UpdateTooLongValidityState() {
  SetValidityState(VALIDITY_STATE_TOO_LONG, IsTooLong());
}

void HTMLInputElement::UpdateTooShortValidityState() {
  SetValidityState(VALIDITY_STATE_TOO_SHORT, IsTooShort());
}

void HTMLInputElement::UpdateValueMissingValidityStateForRadio(
    bool aIgnoreSelf) {
  MOZ_ASSERT(mType == FormControlType::InputRadio,
             "This should be called only for radio input types");

  HTMLInputElement* selection = GetSelectedRadioButton();

  // If there is no selection, that might mean the radio is not in a group.
  // In that case, we can look for the checked state of the radio.
  bool selected = selection || (!aIgnoreSelf && mChecked);
  bool required = !aIgnoreSelf && IsRequired();

  auto* container = GetCurrentRadioGroupContainer();
  if (!container) {
    SetValidityState(VALIDITY_STATE_VALUE_MISSING, false);
    return;
  }

  nsAutoString name;
  GetAttr(nsGkAtoms::name, name);

  // If the current radio is required and not ignored, we can assume the entire
  // group is required.
  if (!required) {
    required = (aIgnoreSelf && IsRequired())
                   ? container->GetRequiredRadioCount(name) - 1
                   : container->GetRequiredRadioCount(name);
  }

  bool valueMissing = required && !selected;
  if (container->GetValueMissingState(name) != valueMissing) {
    container->SetValueMissingState(name, valueMissing);

    SetValidityState(VALIDITY_STATE_VALUE_MISSING, valueMissing);

    // nsRadioSetValueMissingState will call ElementStateChanged while visiting.
    nsAutoScriptBlocker scriptBlocker;
    VisitGroup([valueMissing](HTMLInputElement* aRadio) {
      aRadio->SetValidityState(
          nsIConstraintValidation::VALIDITY_STATE_VALUE_MISSING, valueMissing);
      aRadio->UpdateValidityElementStates(true);
      return true;
    });
  }
}

void HTMLInputElement::UpdateValueMissingValidityState() {
  if (mType == FormControlType::InputRadio) {
    UpdateValueMissingValidityStateForRadio(false);
    return;
  }

  SetValidityState(VALIDITY_STATE_VALUE_MISSING, IsValueMissing());
}

void HTMLInputElement::UpdateTypeMismatchValidityState() {
  SetValidityState(VALIDITY_STATE_TYPE_MISMATCH, HasTypeMismatch());
}

void HTMLInputElement::UpdatePatternMismatchValidityState() {
  Maybe<bool> hasMismatch = HasPatternMismatch();
  // Don't update if the JS engine failed to evaluate it.
  if (hasMismatch.isSome()) {
    SetValidityState(VALIDITY_STATE_PATTERN_MISMATCH, hasMismatch.value());
  }
}

void HTMLInputElement::UpdateRangeOverflowValidityState() {
  SetValidityState(VALIDITY_STATE_RANGE_OVERFLOW, IsRangeOverflow());
  UpdateInRange(true);
}

void HTMLInputElement::UpdateRangeUnderflowValidityState() {
  SetValidityState(VALIDITY_STATE_RANGE_UNDERFLOW, IsRangeUnderflow());
  UpdateInRange(true);
}

void HTMLInputElement::UpdateStepMismatchValidityState() {
  SetValidityState(VALIDITY_STATE_STEP_MISMATCH, HasStepMismatch());
}

void HTMLInputElement::UpdateBadInputValidityState() {
  SetValidityState(VALIDITY_STATE_BAD_INPUT, HasBadInput());
}

void HTMLInputElement::UpdateAllValidityStates(bool aNotify) {
  bool validBefore = IsValid();
  UpdateAllValidityStatesButNotElementState();
  if (validBefore != IsValid()) {
    UpdateValidityElementStates(aNotify);
  }
}

void HTMLInputElement::UpdateAllValidityStatesButNotElementState() {
  UpdateTooLongValidityState();
  UpdateTooShortValidityState();
  UpdateValueMissingValidityState();
  UpdateTypeMismatchValidityState();
  UpdatePatternMismatchValidityState();
  UpdateRangeOverflowValidityState();
  UpdateRangeUnderflowValidityState();
  UpdateStepMismatchValidityState();
  UpdateBadInputValidityState();
}

void HTMLInputElement::UpdateBarredFromConstraintValidation() {
  // NOTE: readonly attribute causes an element to be barred from constraint
  // validation even if it doesn't apply to that input type. That's rather
  // weird, but pre-existing behavior.
  bool wasCandidate = IsCandidateForConstraintValidation();
  SetBarredFromConstraintValidation(
      mType == FormControlType::InputHidden ||
      mType == FormControlType::InputButton ||
      mType == FormControlType::InputReset || IsDisabled() ||
      HasAttr(nsGkAtoms::readonly) ||
      HasFlag(ELEMENT_IS_DATALIST_OR_HAS_DATALIST_ANCESTOR));
  if (IsCandidateForConstraintValidation() != wasCandidate) {
    UpdateInRange(true);
  }
}

nsresult HTMLInputElement::GetValidationMessage(nsAString& aValidationMessage,
                                                ValidityStateType aType) {
  return mInputType->GetValidationMessage(aValidationMessage, aType);
}

Maybe<int32_t> HTMLInputElement::GetNumberInputCols() const {
  // This logic is ported from WebKit, see
  // https://github.com/whatwg/html/issues/10390
  struct RenderSize {
    uint32_t mBeforeDecimal = 0;
    uint32_t mAfterDecimal = 0;

    RenderSize Max(const RenderSize& aOther) const {
      return {std::max(mBeforeDecimal, aOther.mBeforeDecimal),
              std::max(mAfterDecimal, aOther.mAfterDecimal)};
    }

    static RenderSize From(const Decimal& aValue) {
      MOZ_ASSERT(aValue.isFinite());
      nsAutoCString tmp;
      tmp.AppendInt(aValue.value().coefficient());
      const uint32_t sizeOfDigits = tmp.Length();
      const uint32_t sizeOfSign = aValue.isNegative() ? 1 : 0;
      const int32_t exponent = aValue.exponent();
      if (exponent >= 0) {
        return {sizeOfSign + sizeOfDigits, 0};
      }

      const int32_t sizeBeforeDecimalPoint = exponent + int32_t(sizeOfDigits);
      if (sizeBeforeDecimalPoint > 0) {
        // In case of "123.456"
        return {sizeOfSign + sizeBeforeDecimalPoint,
                sizeOfDigits - sizeBeforeDecimalPoint};
      }

      // In case of "0.00012345"
      const uint32_t sizeOfZero = 1;
      const uint32_t numberOfZeroAfterDecimalPoint = -sizeBeforeDecimalPoint;
      return {sizeOfSign + sizeOfZero,
              numberOfZeroAfterDecimalPoint + sizeOfDigits};
    }
  };

  if (mType != FormControlType::InputNumber) {
    return {};
  }
  Decimal min = GetMinimum();
  if (!min.isFinite()) {
    return {};
  }
  Decimal max = GetMaximum();
  if (!max.isFinite()) {
    return {};
  }
  Decimal step = GetStep();
  if (step == kStepAny) {
    return {};
  }
  MOZ_ASSERT(step.isFinite());
  RenderSize size = RenderSize::From(min).Max(
      RenderSize::From(max).Max(RenderSize::From(step)));
  return Some(size.mBeforeDecimal + size.mAfterDecimal +
              (size.mAfterDecimal ? 1 : 0));
}

Maybe<int32_t> HTMLInputElement::GetCols() {
  if (const nsAttrValue* attr = GetParsedAttr(nsGkAtoms::size);
      attr && attr->Type() == nsAttrValue::eInteger) {
    int32_t cols = attr->GetIntegerValue();
    if (cols > 0) {
      return Some(cols);
    }
  }

  if (Maybe<int32_t> cols = GetNumberInputCols(); cols && *cols > 0) {
    return cols;
  }

  return {};
}

int32_t HTMLInputElement::GetWrapCols() {
  return 0;  // only textarea's can have wrap cols
}

int32_t HTMLInputElement::GetRows() { return DEFAULT_ROWS; }

void HTMLInputElement::GetDefaultValueFromContent(nsAString& aValue,
                                                  bool aForDisplay) {
  if (!GetEditorState()) {
    return;
  }
  GetDefaultValue(aValue);
  // This is called by the frame to show the value.
  // We have to sanitize it when needed.
  // FIXME: Do we want to sanitize even when aForDisplay is false?
  if (mDoneCreating) {
    SanitizeValue(aValue, aForDisplay ? SanitizationKind::ForDisplay
                                      : SanitizationKind::ForValueGetter);
  }
}

bool HTMLInputElement::ValueChanged() const { return mValueChanged; }

void HTMLInputElement::GetTextEditorValue(nsAString& aValue) const {
  if (TextControlState* state = GetEditorState()) {
    state->GetValue(aValue, /* aForDisplay = */ true);
  }
}

void HTMLInputElement::UpdatePlaceholderShownState() {
  SetStates(ElementState::PLACEHOLDER_SHOWN,
            IsValueEmpty() && PlaceholderApplies() &&
                HasAttr(nsGkAtoms::placeholder));
}

void HTMLInputElement::OnValueChanged(ValueChangeKind aKind,
                                      bool aNewValueEmpty,
                                      const nsAString* aKnownNewValue) {
  MOZ_ASSERT_IF(aKnownNewValue, aKnownNewValue->IsEmpty() == aNewValueEmpty);
  if (aKind != ValueChangeKind::Internal) {
    mLastValueChangeWasInteractive = aKind == ValueChangeKind::UserInteraction;

    if (mLastValueChangeWasInteractive) {
      mUserChangedSinceFocus = true;
    }
    if (mLastValueChangeWasInteractive &&
        State().HasState(ElementState::AUTOFILL)) {
      RemoveStates(ElementState::AUTOFILL | ElementState::AUTOFILL_PREVIEW);
    }
  }

  if (aNewValueEmpty != IsValueEmpty()) {
    SetStates(ElementState::VALUE_EMPTY, aNewValueEmpty);
    UpdatePlaceholderShownState();
  }

  UpdateAllValidityStates(true);

  ResetDirFormAssociatedElement(this, true, HasDirAuto(), aKnownNewValue);
}

bool HTMLInputElement::HasCachedSelection() {
  TextControlState* state = GetEditorState();
  if (!state) {
    return false;
  }
  return state->IsSelectionCached() && state->HasNeverInitializedBefore() &&
         state->GetSelectionProperties().GetStart() !=
             state->GetSelectionProperties().GetEnd();
}

void HTMLInputElement::SetRevealPassword(bool aValue) {
  if (NS_WARN_IF(mType != FormControlType::InputPassword)) {
    return;
  }
  if (aValue == State().HasState(ElementState::REVEALED)) {
    return;
  }
  RefPtr doc = OwnerDoc();
  // We allow chrome code to prevent this. This is important for about:logins,
  // which may need to run some OS-dependent authentication code before
  // revealing the saved passwords.
  bool defaultAction = true;
  nsContentUtils::DispatchEventOnlyToChrome(
      doc, this, u"MozWillToggleReveal"_ns, CanBubble::eYes, Cancelable::eYes,
      &defaultAction);
  if (NS_WARN_IF(!defaultAction)) {
    return;
  }
  SetStates(ElementState::REVEALED, aValue);
}

bool HTMLInputElement::RevealPassword() const {
  if (NS_WARN_IF(mType != FormControlType::InputPassword)) {
    return false;
  }
  return State().HasState(ElementState::REVEALED);
}

void HTMLInputElement::FieldSetDisabledChanged(bool aNotify) {
  // This *has* to be called *before* UpdateBarredFromConstraintValidation and
  // UpdateValueMissingValidityState because these two functions depend on our
  // disabled state.
  nsGenericHTMLFormControlElementWithState::FieldSetDisabledChanged(aNotify);

  UpdateValueMissingValidityState();
  UpdateBarredFromConstraintValidation();
  UpdateValidityElementStates(aNotify);
}

void HTMLInputElement::SetFilePickerFiltersFromAccept(
    nsIFilePicker* filePicker) {
  // We always add |filterAll|
  filePicker->AppendFilters(nsIFilePicker::filterAll);

  NS_ASSERTION(HasAttr(nsGkAtoms::accept),
               "You should not call SetFilePickerFiltersFromAccept if the"
               " element has no accept attribute!");

  // Services to retrieve image/*, audio/*, video/* filters
  nsCOMPtr<nsIStringBundleService> stringService =
      components::StringBundle::Service();
  if (!stringService) {
    return;
  }
  nsCOMPtr<nsIStringBundle> filterBundle;
  if (NS_FAILED(stringService->CreateBundle(
          "chrome://global/content/filepicker.properties",
          getter_AddRefs(filterBundle)))) {
    return;
  }

  // Service to retrieve mime type information for mime types filters
  nsCOMPtr<nsIMIMEService> mimeService = do_GetService("@mozilla.org/mime;1");
  if (!mimeService) {
    return;
  }

  nsAutoString accept;
  GetAttr(nsGkAtoms::accept, accept);

  HTMLSplitOnSpacesTokenizer tokenizer(accept, ',');

  nsTArray<nsFilePickerFilter> filters;
  nsString allExtensionsList;

  // Retrieve all filters
  while (tokenizer.hasMoreTokens()) {
    const nsDependentSubstring& token = tokenizer.nextToken();

    if (token.IsEmpty()) {
      continue;
    }

    int32_t filterMask = 0;
    nsString filterName;
    nsString extensionListStr;

    // First, check for image/audio/video filters...
    if (token.EqualsLiteral("image/*")) {
      filterMask = nsIFilePicker::filterImages;
      filterBundle->GetStringFromName("imageFilter", extensionListStr);
    } else if (token.EqualsLiteral("audio/*")) {
      filterMask = nsIFilePicker::filterAudio;
      filterBundle->GetStringFromName("audioFilter", extensionListStr);
    } else if (token.EqualsLiteral("video/*")) {
      filterMask = nsIFilePicker::filterVideo;
      filterBundle->GetStringFromName("videoFilter", extensionListStr);
    } else if (token.First() == '.') {
      if (token.Contains(';') || token.Contains('*')) {
        // Ignore this filter as it contains reserved characters
        continue;
      }
      extensionListStr = u"*"_ns + token;
      filterName = extensionListStr;
    } else {
      //... if no image/audio/video filter is found, check mime types filters
      nsCOMPtr<nsIMIMEInfo> mimeInfo;
      if (NS_FAILED(
              mimeService->GetFromTypeAndExtension(NS_ConvertUTF16toUTF8(token),
                                                   ""_ns,  // No extension
                                                   getter_AddRefs(mimeInfo))) ||
          !mimeInfo) {
        continue;
      }

      // Get a name for the filter: first try the description, then the mime
      // type name if there is no description
      mimeInfo->GetDescription(filterName);
      if (filterName.IsEmpty()) {
        nsCString mimeTypeName;
        mimeInfo->GetType(mimeTypeName);
        CopyUTF8toUTF16(mimeTypeName, filterName);
      }

      // Get extension list
      nsCOMPtr<nsIUTF8StringEnumerator> extensions;
      mimeInfo->GetFileExtensions(getter_AddRefs(extensions));

      bool hasMore;
      while (NS_SUCCEEDED(extensions->HasMore(&hasMore)) && hasMore) {
        nsCString extension;
        if (NS_FAILED(extensions->GetNext(extension))) {
          continue;
        }
        if (!extensionListStr.IsEmpty()) {
          extensionListStr.AppendLiteral("; ");
        }
        extensionListStr += u"*."_ns + NS_ConvertUTF8toUTF16(extension);
      }
    }

    if (!filterMask && (extensionListStr.IsEmpty() || filterName.IsEmpty())) {
      // No valid filter found
      continue;
    }

    // At this point we're sure the token represents a valid filter, so pass
    // it directly as a raw filter.
    filePicker->AppendRawFilter(token);

    // If we arrived here, that means we have a valid filter: let's create it
    // and add it to our list, if no similar filter is already present
    nsFilePickerFilter filter;
    if (filterMask) {
      filter = nsFilePickerFilter(filterMask);
    } else {
      filter = nsFilePickerFilter(filterName, extensionListStr);
    }

    if (!filters.Contains(filter)) {
      if (!allExtensionsList.IsEmpty()) {
        allExtensionsList.AppendLiteral("; ");
      }
      allExtensionsList += extensionListStr;
      filters.AppendElement(filter);
    }
  }

  // Remove similar filters
  // Iterate over a copy, as we might modify the original filters list
  const nsTArray<nsFilePickerFilter> filtersCopy = filters.Clone();
  for (uint32_t i = 0; i < filtersCopy.Length(); ++i) {
    const nsFilePickerFilter& filterToCheck = filtersCopy[i];
    if (filterToCheck.mFilterMask) {
      continue;
    }
    for (uint32_t j = 0; j < filtersCopy.Length(); ++j) {
      if (i == j) {
        continue;
      }
      // Check if this filter's extension list is a substring of the other one.
      // e.g. if filters are "*.jpeg" and "*.jpeg; *.jpg" the first one should
      // be removed.
      // Add an extra "; " to be sure the check will work and avoid cases like
      // "*.xls" being a subtring of "*.xslx" while those are two differents
      // filters and none should be removed.
      if (FindInReadable(filterToCheck.mFilter + u";"_ns,
                         filtersCopy[j].mFilter + u";"_ns)) {
        // We already have a similar, less restrictive filter (i.e.
        // filterToCheck extensionList is just a subset of another filter
        // extension list): remove this one
        filters.RemoveElement(filterToCheck);
      }
    }
  }

  // Add "All Supported Types" filter
  if (filters.Length() > 1) {
    nsAutoString title;
    nsContentUtils::GetLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                       "AllSupportedTypes", title);
    filePicker->AppendFilter(title, allExtensionsList);
  }

  // Add each filter
  for (uint32_t i = 0; i < filters.Length(); ++i) {
    const nsFilePickerFilter& filter = filters[i];
    if (filter.mFilterMask) {
      filePicker->AppendFilters(filter.mFilterMask);
    } else {
      filePicker->AppendFilter(filter.mTitle, filter.mFilter);
    }
  }

  if (filters.Length() >= 1) {
    // |filterAll| will always use index=0 so we need to set index=1 as the
    // current filter. This will be "All Supported Types" for multiple filters.
    filePicker->SetFilterIndex(1);
  }
}

Decimal HTMLInputElement::GetStepScaleFactor() const {
  MOZ_ASSERT(DoesStepApply());

  switch (mType) {
    case FormControlType::InputDate:
      return kStepScaleFactorDate;
    case FormControlType::InputNumber:
    case FormControlType::InputRange:
      return kStepScaleFactorNumberRange;
    case FormControlType::InputTime:
    case FormControlType::InputDatetimeLocal:
      return kStepScaleFactorTime;
    case FormControlType::InputMonth:
      return kStepScaleFactorMonth;
    case FormControlType::InputWeek:
      return kStepScaleFactorWeek;
    default:
      MOZ_ASSERT(false, "Unrecognized input type");
      return Decimal::nan();
  }
}

Decimal HTMLInputElement::GetDefaultStep() const {
  MOZ_ASSERT(DoesStepApply());

  switch (mType) {
    case FormControlType::InputDate:
    case FormControlType::InputMonth:
    case FormControlType::InputWeek:
    case FormControlType::InputNumber:
    case FormControlType::InputRange:
      return kDefaultStep;
    case FormControlType::InputTime:
    case FormControlType::InputDatetimeLocal:
      return kDefaultStepTime;
    default:
      MOZ_ASSERT(false, "Unrecognized input type");
      return Decimal::nan();
  }
}

void HTMLInputElement::SetUserInteracted(bool aInteracted) {
  if (mUserInteracted == aInteracted) {
    return;
  }
  mUserInteracted = aInteracted;
  UpdateValidityElementStates(true);
}

void HTMLInputElement::UpdateInRange(bool aNotify) {
  AutoStateChangeNotifier notifier(*this, aNotify);
  RemoveStatesSilently(ElementState::INRANGE | ElementState::OUTOFRANGE);
  if (!mHasRange || !IsCandidateForConstraintValidation()) {
    return;
  }
  bool outOfRange = GetValidityState(VALIDITY_STATE_RANGE_OVERFLOW) ||
                    GetValidityState(VALIDITY_STATE_RANGE_UNDERFLOW);
  AddStatesSilently(outOfRange ? ElementState::OUTOFRANGE
                               : ElementState::INRANGE);
}

void HTMLInputElement::UpdateHasRange(bool aNotify) {
  // There is a range if min/max applies for the type and if the element
  // currently have a valid min or max.
  const bool newHasRange = [&] {
    if (!DoesMinMaxApply()) {
      return false;
    }
    return !GetMinimum().isNaN() || !GetMaximum().isNaN();
  }();

  if (newHasRange == mHasRange) {
    return;
  }

  mHasRange = newHasRange;
  UpdateInRange(aNotify);
}

void HTMLInputElement::PickerClosed() {
  mPickerRunning = false;
  SetOpenState(false);
}

JSObject* HTMLInputElement::WrapNode(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return HTMLInputElement_Binding::Wrap(aCx, this, aGivenProto);
}

GetFilesHelper* HTMLInputElement::GetOrCreateGetFilesHelper(bool aRecursiveFlag,
                                                            ErrorResult& aRv) {
  MOZ_ASSERT(mFileData);

  if (aRecursiveFlag) {
    if (!mFileData->mGetFilesRecursiveHelper) {
      mFileData->mGetFilesRecursiveHelper = GetFilesHelper::Create(
          GetFilesOrDirectoriesInternal(), aRecursiveFlag, aRv);
      if (NS_WARN_IF(aRv.Failed())) {
        return nullptr;
      }
    }

    return mFileData->mGetFilesRecursiveHelper;
  }

  if (!mFileData->mGetFilesNonRecursiveHelper) {
    mFileData->mGetFilesNonRecursiveHelper = GetFilesHelper::Create(
        GetFilesOrDirectoriesInternal(), aRecursiveFlag, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  return mFileData->mGetFilesNonRecursiveHelper;
}

void HTMLInputElement::UpdateEntries(
    const nsTArray<OwningFileOrDirectory>& aFilesOrDirectories) {
  MOZ_ASSERT(mFileData && mFileData->mEntries.IsEmpty());

  nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal();
  MOZ_ASSERT(global);

  RefPtr<FileSystem> fs = FileSystem::Create(global);
  if (NS_WARN_IF(!fs)) {
    return;
  }

  Sequence<RefPtr<FileSystemEntry>> entries;
  for (uint32_t i = 0; i < aFilesOrDirectories.Length(); ++i) {
    RefPtr<FileSystemEntry> entry =
        FileSystemEntry::Create(global, aFilesOrDirectories[i], fs);
    MOZ_ASSERT(entry);

    if (!entries.AppendElement(entry, fallible)) {
      return;
    }
  }

  // The root fileSystem is a DirectoryEntry object that contains only the
  // dropped fileEntry and directoryEntry objects.
  fs->CreateRoot(entries);

  mFileData->mEntries = std::move(entries);
}

void HTMLInputElement::GetWebkitEntries(
    nsTArray<RefPtr<FileSystemEntry>>& aSequence) {
  if (NS_WARN_IF(mType != FormControlType::InputFile)) {
    return;
  }

  glean::dom::blink_filesystem_used
      .EnumGet(glean::dom::BlinkFilesystemUsedLabel::eTrue)
      .Add();
  aSequence.AppendElements(mFileData->mEntries);
}

already_AddRefed<NodeList> HTMLInputElement::GetLabelsForBindings() {
  return GetLabelsInternal();
}

already_AddRefed<NodeList> HTMLInputElement::GetLabelsInternal() {
  if (!IsLabelable()) {
    return nullptr;
  }

  return nsGenericHTMLElement::LabelsInternal();
}

void HTMLInputElement::MaybeFireInputPasswordRemoved() {
  // We want this event to be fired only when the password field is removed
  // from the DOM tree, not when it is released (ex, tab is closed). So don't
  // fire an event when the password input field doesn't have a docshell.
  Document* doc = GetComposedDoc();
  nsIDocShell* container = doc ? doc->GetDocShell() : nullptr;
  if (!container) {
    return;
  }

  // Right now, only the password manager listens to the event and only listen
  // to it under certain circumstances. So don't fire this event unless
  // necessary.
  if (!doc->ShouldNotifyFormOrPasswordRemoved()) {
    return;
  }

  AsyncEventDispatcher::RunDOMEventWhenSafe(
      *this, u"DOMInputPasswordRemoved"_ns, CanBubble::eNo,
      ChromeOnlyDispatch::eYes);
}

void HTMLInputElement::UpdateRadioGroupState() {
  VisitGroup([](HTMLInputElement* aRadio) {
    aRadio->UpdateIndeterminateState(true);
    aRadio->UpdateValidityElementStates(true);
    return true;
  });
}

}  // namespace mozilla::dom

#undef NS_OUTER_ACTIVATE_EVENT
#undef NS_ORIGINAL_CHECKED_VALUE
#undef NS_ORIGINAL_INDETERMINATE_VALUE
#undef NS_PRE_HANDLE_BLUR_EVENT
#undef NS_IN_SUBMIT_CLICK
#undef NS_CONTROL_TYPE

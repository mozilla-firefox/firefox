/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TSFUtils_h
#define TSFUtils_h

#include <ostream>

#include <msctf.h>
#include <textstor.h>

#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TextRange.h"
#include "mozilla/ToString.h"
#include "mozilla/widget/IMEData.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "nsTArray.h"

// GUID_PROP_INPUTSCOPE is declared in inputscope.h using INIT_GUID.
// With initguid.h, we get its instance instead of extern declaration.
#ifdef INPUTSCOPE_INIT_GUID
#  include <initguid.h>
#endif
#ifdef TEXTATTRS_INIT_GUID
#  include <tsattrs.h>
#endif
#include <inputscope.h>

// TSF InputScope, for earlier SDK 8
#define IS_SEARCH static_cast<InputScope>(50)

class nsWindow;

namespace mozilla::widget {
class TSFEmptyTextStore;
class TSFTextStore;
class TSFTextStoreBase;
struct IMENotificationRequests;
struct InputContext;
struct InputContextAction;

class TSFUtils final {
 public:
  TSFUtils() = delete;
  ~TSFUtils() = delete;

  static void Initialize();
  static void Shutdown();

  /**
   * Return true while TSF is available.
   */
  [[nodiscard]] static bool IsAvailable() { return sThreadMgr; }

  [[nodiscard]] static IMENotificationRequests GetIMENotificationRequests();

  enum class GotFocus : bool { No, Yes };
  /**
   * Called when focus changed in the DOM level and aContext has the details of
   * the new focused element.
   */
  static nsresult OnFocusChange(GotFocus aGotFocus, nsWindow* aFocusedWindow,
                                const InputContext& aContext);

  /**
   * Called when input context for aWindow is set.  aWindow should have focus
   * when this is called.
   */
  static void OnSetInputContext(nsWindow* aWindow, const InputContext& aContext,
                                const InputContextAction& aAction);

  // FIXME: Simplify the following APIs with TSFTextStoreBase::IsEditable().

  /**
   * Active TextStore is a TSFTextStoreBase instance which is for editable
   * content.
   * Note that it may disable IME, e.g., when the editable content is a password
   * field or `ime-mode: disabled`.
   */
  [[nodiscard]] static TSFTextStore* GetActiveTextStore() {
    MOZ_ASSERT_IF(sActiveTextStore, (void*)sActiveTextStore.get() ==
                                        (void*)sCurrentTextStore.get());
    return sActiveTextStore.get();
  }

  /**
   * Current TextStore ia TSFTextStoreBase instance for either the content is
   * editable nor not editable.
   */
  [[nodiscard]] static TSFTextStoreBase* GetCurrentTextStore() {
    MOZ_ASSERT_IF(sActiveTextStore, (void*)sActiveTextStore.get() ==
                                        (void*)sCurrentTextStore.get());
    return sCurrentTextStore.get();
  }

  /**
   * Check whether current TextStore is for editable one.  I.e., when there is
   * an active TextStore.  This is just an alias to make the caller easier to
   * read.
   */
  [[nodiscard]] static bool CurrentTextStoreIsEditable() {
    MOZ_ASSERT_IF(sActiveTextStore, (void*)sActiveTextStore.get() ==
                                        (void*)sCurrentTextStore.get());
    return sActiveTextStore;
  }

  template <typename TSFTextStoreClass>
  static void ClearStoringTextStoresIf(
      const RefPtr<TSFTextStoreClass>& aTextStore);

  [[nodiscard]] static ITfThreadMgr* GetThreadMgr() { return sThreadMgr; }
  [[nodiscard]] static ITfMessagePump* GetMessagePump() {
    if (!sMessagePump) {
      EnsureMessagePump();
    }
    return sMessagePump;
  }
  [[nodiscard]] static ITfKeystrokeMgr* GetKeystrokeMgr() {
    if (!sKeystrokeMgr) {
      EnsureKeystrokeMgr();
    }
    return sKeystrokeMgr;
  }
  [[nodiscard]] static ITfInputProcessorProfiles* GetInputProcessorProfiles();
  [[nodiscard]] static ITfDisplayAttributeMgr* GetDisplayAttributeMgr();
  [[nodiscard]] static ITfCategoryMgr* GetCategoryMgr();
  [[nodiscard]] static ITfCompartment* GetCompartmentForOpenClose();

  [[nodiscard]] static DWORD ClientId() { return sClientId; }

  // TODO: GUID_PROP_URL has not been declared in the SDK yet.  We should drop
  // this after it's released by a new SDK and it becomes the minimum supported
  // SDK version.
  constexpr static const GUID sGUID_PROP_URL = {
      0xd5138268,
      0xa1bf,
      0x4308,
      {0xbc, 0xbf, 0x2e, 0x73, 0x93, 0x98, 0xe2, 0x34}};

  constexpr static TsViewCookie sDefaultView = 1;

  /**
   * Returns true if the Windows may have a crash bug at handling
   * ITfTextStoreACP::GetSelection() returns error.
   */
  [[nodiscard]] static bool DoNotReturnErrorFromGetSelection();

  struct MOZ_STACK_CLASS AutoRangeExtant {
    explicit AutoRangeExtant(ITfRange* aRange);

    [[nodiscard]] bool isErr() const { return FAILED(mHR); }
    [[nodiscard]] bool isOk() const { return SUCCEEDED(mHR); }
    [[nodiscard]] LONG End() const { return mStart + mLength; }

    LONG mStart = 0;
    LONG mLength = 0;
    HRESULT mHR = E_NOT_SET;
  };

  /**
   * Get TextRangeType corresponding to aDisplayAttr.
   */
  [[nodiscard]] static TextRangeType GetTextRangeType(
      TF_DISPLAYATTRIBUTE& aDisplayAttr);

  /**
   * Get nscolor corresponding to aTSFColor.
   */
  [[nodiscard]] static Maybe<nscolor> GetColor(const TF_DA_COLOR& aTSFColor);

  /**
   * Get LineStyle corresponding to aTSFLineStyle.
   */
  [[nodiscard]] static Maybe<TextRangeStyle::LineStyle> GetLineStyle(
      TF_DA_LINESTYLE aTSFLineStyle);

  /**
   * Returns true if active TIP or IME is a black listed one and we should
   * set input scope of URL bar to IS_DEFAULT rather than IS_URL.
   */
  [[nodiscard]] static bool ShouldSetInputScopeOfURLBarToDefault();

  // Support retrieving attributes.
  // TODO: We should support RightToLeft, perhaps.
  enum AttrIndex {
    // Used for result of GetRequestedAttrIndex()
    NotSupported = -1,

    // Supported attributes even in TSFEmptyTextStore.
    InputScope = 0,
    DocumentURL,

    // Count of the supported attrs in empty text store
    NUM_OF_SUPPORTED_ATTRS_IN_EMPTY_TEXT_STORE,

    // Supported attributes in any TextStores.
    TextVerticalWriting = NUM_OF_SUPPORTED_ATTRS_IN_EMPTY_TEXT_STORE,
    TextOrientation,

    // Count of the supported attributes
    NUM_OF_SUPPORTED_ATTRS,
  };

  /**
   * Return AttrIndex fo aAttrID.
   */
  [[nodiscard]] static AttrIndex GetRequestedAttrIndex(
      const TS_ATTRID& aAttrID);

  /**
   * Return TS_ATTRID for aIndex.
   */
  [[nodiscard]] static TS_ATTRID GetAttrID(AttrIndex aIndex);

  /**
   * Get compartment instance.
   */
  [[nodiscard]] static Result<RefPtr<ITfCompartment>, bool> GetCompartment(
      IUnknown* pUnk, const GUID& aID);

  /**
   * Mark aContent as keyboard disabled.
   *
   * @return true if succeeded, otherwise, false.
   */
  static bool MarkContextAsKeyboardDisabled(ITfContext* aContext);

  /**
   * Mark aContext as empty.
   *
   * @return true if succeeded, otherwise, false.
   */
  static bool MarkContextAsEmpty(ITfContext* aContext);

  static const char* BoolToChar(bool aValue) {
    return aValue ? "true" : "false";
  }
  static const char* MouseButtonToChar(int16_t aButton);
  static const char* CommonHRESULTToChar(HRESULT);
  static const char* HRESULTToChar(HRESULT);

  static TS_SELECTION_ACP EmptySelectionACP() {
    return TS_SELECTION_ACP{
        .acpStart = 0,
        .acpEnd = 0,
        .style = {.ase = TS_AE_NONE, .fInterimChar = FALSE}};
  }

 private:
  static void EnsureMessagePump();
  static void EnsureKeystrokeMgr();

  // TSF thread manager object for the current application
  static StaticRefPtr<ITfThreadMgr> sThreadMgr;
  // sMessagePump is QI'ed from sThreadMgr
  static StaticRefPtr<ITfMessagePump> sMessagePump;
  // sKeystrokeMgr is QI'ed from sThreadMgr
  static StaticRefPtr<ITfKeystrokeMgr> sKeystrokeMgr;

  // TSF display attribute manager
  static StaticRefPtr<ITfDisplayAttributeMgr> sDisplayAttrMgr;
  // TSF category manager
  static StaticRefPtr<ITfCategoryMgr> sCategoryMgr;
  // Compartment for (Get|Set)IMEOpenState()
  static StaticRefPtr<ITfCompartment> sCompartmentForOpenClose;

  static StaticRefPtr<ITfInputProcessorProfiles> sInputProcessorProfiles;

  // Current active text store which is managing a keyboard enabled editor
  // (i.e., editable editor).  Currently only ONE TSFTextStore instance is ever
  // used, although Create is called when an editor is focused and Destroy
  // called when the focused editor is blurred.
  static StaticRefPtr<TSFTextStore> sActiveTextStore;

  // Current text store which may be an empty one for disabled state.
  static StaticRefPtr<TSFTextStoreBase> sCurrentTextStore;

  // Global instance for non-editable state.
  static StaticRefPtr<TSFEmptyTextStore> sEmptyTextStore;

  // TSF client ID for the current application
  static DWORD sClientId;
};

class AutoFlagsCString : public nsAutoCString {
 protected:
  void AppendFlag(const nsCString& aFlagStr) { AppendFlag(aFlagStr.get()); }
  void AppendFlag(const char* aFlagStr) {
    if (!IsEmpty()) {
      AppendLiteral(" | ");
    }
    Append(aFlagStr);
  }
};

class AutoFindFlagsCString final : public AutoFlagsCString {
 public:
  explicit AutoFindFlagsCString(DWORD);
};
class AutoACPFromPointFlagsCString final : public AutoFlagsCString {
 public:
  explicit AutoACPFromPointFlagsCString(DWORD);
};
class AutoSinkMasksCString final : public AutoFlagsCString {
 public:
  explicit AutoSinkMasksCString(DWORD);
};
class AutoLockFlagsCString final : public AutoFlagsCString {
 public:
  explicit AutoLockFlagsCString(DWORD);
};

class AutoClsidCString final : public nsAutoCString {
 public:
  explicit AutoClsidCString(REFGUID);
};
class AutoRawGuidCString : public nsAutoCString {
 public:
  explicit AutoRawGuidCString(REFGUID aGUID) { AssignRawGuid(aGUID); }

 protected:
  AutoRawGuidCString() = default;
  void AssignRawGuid(REFGUID);
};
class AutoGuidCString final : public AutoRawGuidCString {
 public:
  explicit AutoGuidCString(REFGUID);
};
class AutoRiidCString final : public nsAutoCString {
 public:
  explicit AutoRiidCString(REFIID);
};

class AutoMouseButtonsCString final : public AutoFlagsCString {
 public:
  explicit AutoMouseButtonsCString(int16_t aButtons);
};

class AutoInputScopesCString : public AutoFlagsCString {
 public:
  explicit AutoInputScopesCString(const nsTArray<InputScope>& aList);
};

class AutoEscapedUTF8String final : public NS_ConvertUTF16toUTF8 {
 public:
  explicit AutoEscapedUTF8String(const nsAString& aString)
      : NS_ConvertUTF16toUTF8(aString) {
    Escape();
  }
  explicit AutoEscapedUTF8String(const char16ptr_t aString)
      : NS_ConvertUTF16toUTF8(aString) {
    Escape();
  }
  AutoEscapedUTF8String(const char16ptr_t aString, uint32_t aLength)
      : NS_ConvertUTF16toUTF8(aString, aLength) {
    Escape();
  }

 private:
  void Escape() {
    ReplaceSubstring("\r", "\\r");
    ReplaceSubstring("\n", "\\n");
    ReplaceSubstring("\t", "\\t");
  }
};

}  // namespace mozilla::widget

inline std::ostream& operator<<(std::ostream& aStream,
                                const TS_SELECTIONSTYLE& aSelectionStyle) {
  const char* ase = "Unknown";
  switch (aSelectionStyle.ase) {
    case TS_AE_START:
      ase = "TS_AE_START";
      break;
    case TS_AE_END:
      ase = "TS_AE_END";
      break;
    case TS_AE_NONE:
      ase = "TS_AE_NONE";
      break;
  }
  return aStream << "{ ase=" << ase << ", fInterimChar="
                 << (aSelectionStyle.fInterimChar ? "TRUE" : "FALSE") << " }";
}

inline std::ostream& operator<<(std::ostream& aStream,
                                const TS_SELECTION_ACP& aACP) {
  return aStream << "{ acpStart=" << aACP.acpStart << ", acpEnd=" << aACP.acpEnd
                 << ", style=" << mozilla::ToString(aACP.style).c_str() << " }";
}

inline std::ostream& operator<<(std::ostream& aStream,
                                const TsRunType& aRunType) {
  switch (aRunType) {
    case TS_RT_PLAIN:
      return aStream << "TS_RT_PLAIN";
    case TS_RT_HIDDEN:
      return aStream << "TS_RT_HIDDEN";
    case TS_RT_OPAQUE:
      return aStream << "TS_RT_OPAQUE";
    default:
      return aStream << nsPrintfCString("Unknown(%08X)",
                                        static_cast<int32_t>(aRunType));
  }
}

inline std::ostream& operator<<(std::ostream& aStream,
                                const TF_DA_COLOR& aColor) {
  switch (aColor.type) {
    case TF_CT_NONE:
      return aStream << "TF_CT_NONE";
    case TF_CT_SYSCOLOR:
      return aStream << nsPrintfCString("TF_CT_SYSCOLOR, nIndex:0x%08X",
                                        static_cast<int32_t>(aColor.nIndex));
    case TF_CT_COLORREF:
      return aStream << nsPrintfCString("TF_CT_COLORREF, cr:0x%08X",
                                        static_cast<int32_t>(aColor.cr));
      break;
    default:
      return aStream << nsPrintfCString("Unknown(%08X)",
                                        static_cast<int32_t>(aColor.type));
  }
}

inline std::ostream& operator<<(std::ostream& aStream,
                                const TF_DA_LINESTYLE& aLineStyle) {
  switch (aLineStyle) {
    case TF_LS_NONE:
      return aStream << "TF_LS_NONE";
    case TF_LS_SOLID:
      return aStream << "TF_LS_SOLID";
    case TF_LS_DOT:
      return aStream << "TF_LS_DOT";
    case TF_LS_DASH:
      return aStream << "TF_LS_DASH";
    case TF_LS_SQUIGGLE:
      return aStream << "TF_LS_SQUIGGLE";
    default:
      return aStream << nsPrintfCString("Unknown(%08X)",
                                        static_cast<int32_t>(aLineStyle));
  }
}

inline std::ostream& operator<<(std::ostream& aStream,
                                const TF_DA_ATTR_INFO& aAttr) {
  switch (aAttr) {
    case TF_ATTR_INPUT:
      return aStream << "TF_ATTR_INPUT";
    case TF_ATTR_TARGET_CONVERTED:
      return aStream << "TF_ATTR_TARGET_CONVERTED";
    case TF_ATTR_CONVERTED:
      return aStream << "TF_ATTR_CONVERTED";
    case TF_ATTR_TARGET_NOTCONVERTED:
      return aStream << "TF_ATTR_TARGET_NOTCONVERTED";
    case TF_ATTR_INPUT_ERROR:
      return aStream << "TF_ATTR_INPUT_ERROR";
    case TF_ATTR_FIXEDCONVERTED:
      return aStream << "TF_ATTR_FIXEDCONVERTED";
    case TF_ATTR_OTHER:
      return aStream << "TF_ATTR_OTHER";
    default:
      return aStream << nsPrintfCString("Unknown(%08X)",
                                        static_cast<int32_t>(aAttr));
  }
}

inline std::ostream& operator<<(std::ostream& aStream,
                                const TF_DISPLAYATTRIBUTE& aDispAttr) {
  return aStream << "{ crText:{" << aDispAttr.crText << " }, crBk:{ "
                 << aDispAttr.crBk << " }, lsStyle: " << aDispAttr.lsStyle
                 << ", fBoldLine: "
                 << mozilla::widget::TSFUtils::BoolToChar(aDispAttr.fBoldLine)
                 << ", crLine:{ " << aDispAttr.crLine
                 << " }, bAttr: " << aDispAttr.bAttr << " }";
}

#endif  // #ifndef TSFUtils_h

/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define INPUTSCOPE_INIT_GUID
#define TEXTATTRS_INIT_GUID
#include "TSFUtils.h"

#include "IMMHandler.h"
#include "TSFEmptyTextStore.h"
#include "TSFStaticSink.h"
#include "TSFTextInputProcessorList.h"
#include "TSFTextStore.h"

#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/StaticPrefs_intl.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/widget/WinRegistry.h"
#include "nsPrintfCString.h"

// For collecting other people's log, tell `MOZ_LOG=IMEHandler:4,sync`
// rather than `MOZ_LOG=IMEHandler:5,sync` since using `5` may create too
// big file.
// Therefore you shouldn't use `LogLevel::Verbose` for logging usual behavior.
mozilla::LazyLogModule gIMELog("IMEHandler");

namespace mozilla::widget {

/******************************************************************************
 * Logging helper classes
 ******************************************************************************/

AutoFindFlagsCString::AutoFindFlagsCString(DWORD aFindFlags) {
  if (!aFindFlags) {
    AssignLiteral("no flags (0)");
    return;
  }
  if (aFindFlags & TS_ATTR_FIND_BACKWARDS) {
    AppendFlag("TS_ATTR_FIND_BACKWARDS");
  }
  if (aFindFlags & TS_ATTR_FIND_WANT_OFFSET) {
    AppendFlag("TS_ATTR_FIND_WANT_OFFSET");
  }
  if (aFindFlags & TS_ATTR_FIND_UPDATESTART) {
    AppendFlag("TS_ATTR_FIND_UPDATESTART");
  }
  if (aFindFlags & TS_ATTR_FIND_WANT_VALUE) {
    AppendFlag("TS_ATTR_FIND_WANT_VALUE");
  }
  if (aFindFlags & TS_ATTR_FIND_WANT_END) {
    AppendFlag("TS_ATTR_FIND_WANT_END");
  }
  if (aFindFlags & TS_ATTR_FIND_HIDDEN) {
    AppendFlag("TS_ATTR_FIND_HIDDEN");
  }
  if (IsEmpty()) {
    AppendPrintf("Unknown(%lu)", aFindFlags);
  }
}

AutoACPFromPointFlagsCString::AutoACPFromPointFlagsCString(DWORD aFlags) {
  if (!aFlags) {
    AssignLiteral("no flags (0)");
    return;
  }
  if (aFlags & GXFPF_ROUND_NEAREST) {
    AppendFlag("GXFPF_ROUND_NEAREST");
    aFlags &= ~GXFPF_ROUND_NEAREST;
  }
  if (aFlags & GXFPF_NEAREST) {
    AppendFlag("GXFPF_NEAREST");
    aFlags &= ~GXFPF_NEAREST;
  }
  if (aFlags) {
    AppendFlag(nsPrintfCString("Unknown(%lu)", aFlags));
  }
}

AutoClsidCString::AutoClsidCString(REFCLSID aCLSID) {
  LPOLESTR str = nullptr;
  HRESULT hr = ::StringFromCLSID(aCLSID, &str);
  if (FAILED(hr) || !str || !str[0]) {
    return;
  }
  Assign(NS_ConvertUTF16toUTF8(str));
  ::CoTaskMemFree(str);
}

void AutoRawGuidCString::AssignRawGuid(REFGUID aGUID) {
  OLECHAR str[40];
  int len = ::StringFromGUID2(aGUID, str, std::size(str));
  if (!len || !str[0]) {
    return;
  }
  Assign(NS_ConvertUTF16toUTF8(str));
}

AutoGuidCString::AutoGuidCString(REFGUID aGUID) {
#define ASSIGN_GUID_NAME(aNamedGUID)    \
  if (IsEqualGUID(aGUID, aNamedGUID)) { \
    AssignLiteral(#aNamedGUID);         \
    return;                             \
  }

  ASSIGN_GUID_NAME(GUID_PROP_INPUTSCOPE)
  ASSIGN_GUID_NAME(TSFUtils::sGUID_PROP_URL)
  ASSIGN_GUID_NAME(TSATTRID_OTHERS)
  ASSIGN_GUID_NAME(TSATTRID_Font)
  ASSIGN_GUID_NAME(TSATTRID_Font_FaceName)
  ASSIGN_GUID_NAME(TSATTRID_Font_SizePts)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Bold)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Italic)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_SmallCaps)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Capitalize)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Uppercase)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Lowercase)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation_LasVegasLights)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation_BlinkingBackground)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation_SparkleText)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation_MarchingBlackAnts)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation_MarchingRedAnts)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation_Shimmer)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation_WipeDown)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Animation_WipeRight)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Emboss)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Engrave)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Hidden)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Kerning)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Outlined)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Position)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Protected)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Shadow)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Spacing)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Weight)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Height)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Underline)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Underline_Single)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Underline_Double)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Strikethrough)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Strikethrough_Single)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Strikethrough_Double)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Overline)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Overline_Single)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Overline_Double)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Blink)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Subscript)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Superscript)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_Color)
  ASSIGN_GUID_NAME(TSATTRID_Font_Style_BackgroundColor)
  ASSIGN_GUID_NAME(TSATTRID_Text)
  ASSIGN_GUID_NAME(TSATTRID_Text_VerticalWriting)
  ASSIGN_GUID_NAME(TSATTRID_Text_RightToLeft)
  ASSIGN_GUID_NAME(TSATTRID_Text_Orientation)
  ASSIGN_GUID_NAME(TSATTRID_Text_Language)
  ASSIGN_GUID_NAME(TSATTRID_Text_ReadOnly)
  ASSIGN_GUID_NAME(TSATTRID_Text_EmbeddedObject)
  ASSIGN_GUID_NAME(TSATTRID_Text_Alignment)
  ASSIGN_GUID_NAME(TSATTRID_Text_Alignment_Left)
  ASSIGN_GUID_NAME(TSATTRID_Text_Alignment_Right)
  ASSIGN_GUID_NAME(TSATTRID_Text_Alignment_Center)
  ASSIGN_GUID_NAME(TSATTRID_Text_Alignment_Justify)
  ASSIGN_GUID_NAME(TSATTRID_Text_Link)
  ASSIGN_GUID_NAME(TSATTRID_Text_Hyphenation)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_FirstLineIndent)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_LeftIndent)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_RightIndent)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_SpaceAfter)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_SpaceBefore)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_LineSpacing)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_LineSpacing_Single)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_LineSpacing_OnePtFive)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_LineSpacing_Double)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_LineSpacing_AtLeast)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_LineSpacing_Exactly)
  ASSIGN_GUID_NAME(TSATTRID_Text_Para_LineSpacing_Multiple)
  ASSIGN_GUID_NAME(TSATTRID_List)
  ASSIGN_GUID_NAME(TSATTRID_List_LevelIndel)
  ASSIGN_GUID_NAME(TSATTRID_List_Type)
  ASSIGN_GUID_NAME(TSATTRID_List_Type_Bullet)
  ASSIGN_GUID_NAME(TSATTRID_List_Type_Arabic)
  ASSIGN_GUID_NAME(TSATTRID_List_Type_LowerLetter)
  ASSIGN_GUID_NAME(TSATTRID_List_Type_UpperLetter)
  ASSIGN_GUID_NAME(TSATTRID_List_Type_LowerRoman)
  ASSIGN_GUID_NAME(TSATTRID_List_Type_UpperRoman)
  ASSIGN_GUID_NAME(TSATTRID_App)
  ASSIGN_GUID_NAME(TSATTRID_App_IncorrectSpelling)
  ASSIGN_GUID_NAME(TSATTRID_App_IncorrectGrammar)

#undef ASSIGN_GUID_NAME

  AssignRawGuid(aGUID);
}

AutoRiidCString::AutoRiidCString(REFIID aRIID) {
  LPOLESTR str = nullptr;
  HRESULT hr = ::StringFromIID(aRIID, &str);
  if (FAILED(hr) || !str || !str[0]) {
    return;
  }

  nsAutoString key(L"Interface\\");
  key += str;

  nsCString result;
  wchar_t buf[256];
  if (WinRegistry::GetString(HKEY_CLASSES_ROOT, key, u""_ns, buf,
                             WinRegistry::kLegacyWinUtilsStringFlags)) {
    Assign(NS_ConvertUTF16toUTF8(buf));
  } else {
    Assign(NS_ConvertUTF16toUTF8(str));
  }
  ::CoTaskMemFree(str);
}

const char* TSFUtils::CommonHRESULTToChar(HRESULT aResult) {
  switch (aResult) {
    case S_OK:
      return "S_OK";
    case E_ABORT:
      return "E_ABORT";
    case E_ACCESSDENIED:
      return "E_ACCESSDENIED";
    case E_FAIL:
      return "E_FAIL";
    case E_HANDLE:
      return "E_HANDLE";
    case E_INVALIDARG:
      return "E_INVALIDARG";
    case E_NOINTERFACE:
      return "E_NOINTERFACE";
    case E_NOTIMPL:
      return "E_NOTIMPL";
    case E_OUTOFMEMORY:
      return "E_OUTOFMEMORY";
    case E_POINTER:
      return "E_POINTER";
    case E_UNEXPECTED:
      return "E_UNEXPECTED";
    case E_NOT_SET:
      return "E_NOT_SET";
    default:
      return SUCCEEDED(aResult) ? "Succeeded" : "Failed";
  }
}

const char* TSFUtils::HRESULTToChar(HRESULT aResult) {
  switch (aResult) {
    case TS_E_FORMAT:
      return "TS_E_FORMAT";
    case TS_E_INVALIDPOINT:
      return "TS_E_INVALIDPOINT";
    case TS_E_INVALIDPOS:
      return "TS_E_INVALIDPOS";
    case TS_E_NOINTERFACE:
      return "TS_E_NOINTERFACE";
    case TS_E_NOLAYOUT:
      return "TS_E_NOLAYOUT";
    case TS_E_NOLOCK:
      return "TS_E_NOLOCK";
    case TS_E_NOOBJECT:
      return "TS_E_NOOBJECT";
    case TS_E_NOSELECTION:
      return "TS_E_NOSELECTION";
    case TS_E_NOSERVICE:
      return "TS_E_NOSERVICE";
    case TS_E_READONLY:
      return "TS_E_READONLY";
    case TS_E_SYNCHRONOUS:
      return "TS_E_SYNCHRONOUS";
    case TS_S_ASYNC:
      return "TS_S_ASYNC";
    default:
      return TSFUtils::CommonHRESULTToChar(aResult);
  }
}

AutoSinkMasksCString::AutoSinkMasksCString(DWORD aSinkMask) {
  if (aSinkMask & TS_AS_TEXT_CHANGE) {
    AppendFlag("TS_AS_TEXT_CHANGE");
  }
  if (aSinkMask & TS_AS_SEL_CHANGE) {
    AppendFlag("TS_AS_SEL_CHANGE");
  }
  if (aSinkMask & TS_AS_LAYOUT_CHANGE) {
    AppendFlag("TS_AS_LAYOUT_CHANGE");
  }
  if (aSinkMask & TS_AS_ATTR_CHANGE) {
    AppendFlag("TS_AS_ATTR_CHANGE");
  }
  if (aSinkMask & TS_AS_STATUS_CHANGE) {
    AppendFlag("TS_AS_STATUS_CHANGE");
  }
  if (IsEmpty()) {
    AssignLiteral("not-specified");
  }
}

AutoLockFlagsCString::AutoLockFlagsCString(DWORD aLockFlags) {
  if ((aLockFlags & TS_LF_READWRITE) == TS_LF_READWRITE) {
    AppendFlag("TS_LF_READWRITE");
  } else if (aLockFlags & TS_LF_READ) {
    AppendFlag("TS_LF_READ");
  }
  if (aLockFlags & TS_LF_SYNC) {
    AppendFlag("TS_LF_SYNC");
  }
  if (IsEmpty()) {
    AssignLiteral("not-specified");
  }
}

const char* TSFUtils::MouseButtonToChar(int16_t aButton) {
  switch (aButton) {
    case MouseButton::ePrimary:
      return "LeftButton";
    case MouseButton::eMiddle:
      return "MiddleButton";
    case MouseButton::eSecondary:
      return "RightButton";
    default:
      return "UnknownButton";
  }
}

AutoMouseButtonsCString::AutoMouseButtonsCString(int16_t aButtons) {
  if (!aButtons) {
    AssignLiteral("no buttons");
    return;
  }
  if (aButtons & MouseButtonsFlag::ePrimaryFlag) {
    AppendFlag("LeftButton");
  }
  if (aButtons & MouseButtonsFlag::eSecondaryFlag) {
    AppendFlag("RightButton");
  }
  if (aButtons & MouseButtonsFlag::eMiddleFlag) {
    AppendFlag("MiddleButton");
  }
  if (aButtons & MouseButtonsFlag::e4thFlag) {
    AppendFlag("4thButton");
  }
  if (aButtons & MouseButtonsFlag::e5thFlag) {
    AppendFlag("5thButton");
  }
}

AutoInputScopesCString::AutoInputScopesCString(
    const nsTArray<InputScope>& aList) {
  for (InputScope inputScope : aList) {
    switch (inputScope) {
      case IS_DEFAULT:
        AppendFlag("IS_DEFAULT");
        break;
      case IS_URL:
        AppendFlag("IS_URL");
        break;
      case IS_FILE_FULLFILEPATH:
        AppendFlag("IS_FILE_FULLFILEPATH");
        break;
      case IS_FILE_FILENAME:
        AppendFlag("IS_FILE_FILENAME");
        break;
      case IS_EMAIL_USERNAME:
        AppendFlag("IS_EMAIL_USERNAME");
        break;
      case IS_EMAIL_SMTPEMAILADDRESS:
        AppendFlag("IS_EMAIL_SMTPEMAILADDRESS");
        break;
      case IS_LOGINNAME:
        AppendFlag("IS_LOGINNAME");
        break;
      case IS_PERSONALNAME_FULLNAME:
        AppendFlag("IS_PERSONALNAME_FULLNAME");
        break;
      case IS_PERSONALNAME_PREFIX:
        AppendFlag("IS_PERSONALNAME_PREFIX");
        break;
      case IS_PERSONALNAME_GIVENNAME:
        AppendFlag("IS_PERSONALNAME_GIVENNAME");
        break;
      case IS_PERSONALNAME_MIDDLENAME:
        AppendFlag("IS_PERSONALNAME_MIDDLENAME");
        break;
      case IS_PERSONALNAME_SURNAME:
        AppendFlag("IS_PERSONALNAME_SURNAME");
        break;
      case IS_PERSONALNAME_SUFFIX:
        AppendFlag("IS_PERSONALNAME_SUFFIX");
        break;
      case IS_ADDRESS_FULLPOSTALADDRESS:
        AppendFlag("IS_ADDRESS_FULLPOSTALADDRESS");
        break;
      case IS_ADDRESS_POSTALCODE:
        AppendFlag("IS_ADDRESS_POSTALCODE");
        break;
      case IS_ADDRESS_STREET:
        AppendFlag("IS_ADDRESS_STREET");
        break;
      case IS_ADDRESS_STATEORPROVINCE:
        AppendFlag("IS_ADDRESS_STATEORPROVINCE");
        break;
      case IS_ADDRESS_CITY:
        AppendFlag("IS_ADDRESS_CITY");
        break;
      case IS_ADDRESS_COUNTRYNAME:
        AppendFlag("IS_ADDRESS_COUNTRYNAME");
        break;
      case IS_ADDRESS_COUNTRYSHORTNAME:
        AppendFlag("IS_ADDRESS_COUNTRYSHORTNAME");
        break;
      case IS_CURRENCY_AMOUNTANDSYMBOL:
        AppendFlag("IS_CURRENCY_AMOUNTANDSYMBOL");
        break;
      case IS_CURRENCY_AMOUNT:
        AppendFlag("IS_CURRENCY_AMOUNT");
        break;
      case IS_DATE_FULLDATE:
        AppendFlag("IS_DATE_FULLDATE");
        break;
      case IS_DATE_MONTH:
        AppendFlag("IS_DATE_MONTH");
        break;
      case IS_DATE_DAY:
        AppendFlag("IS_DATE_DAY");
        break;
      case IS_DATE_YEAR:
        AppendFlag("IS_DATE_YEAR");
        break;
      case IS_DATE_MONTHNAME:
        AppendFlag("IS_DATE_MONTHNAME");
        break;
      case IS_DATE_DAYNAME:
        AppendFlag("IS_DATE_DAYNAME");
        break;
      case IS_DIGITS:
        AppendFlag("IS_DIGITS");
        break;
      case IS_NUMBER:
        AppendFlag("IS_NUMBER");
        break;
      case IS_ONECHAR:
        AppendFlag("IS_ONECHAR");
        break;
      case IS_PASSWORD:
        AppendFlag("IS_PASSWORD");
        break;
      case IS_TELEPHONE_FULLTELEPHONENUMBER:
        AppendFlag("IS_TELEPHONE_FULLTELEPHONENUMBER");
        break;
      case IS_TELEPHONE_COUNTRYCODE:
        AppendFlag("IS_TELEPHONE_COUNTRYCODE");
        break;
      case IS_TELEPHONE_AREACODE:
        AppendFlag("IS_TELEPHONE_AREACODE");
        break;
      case IS_TELEPHONE_LOCALNUMBER:
        AppendFlag("IS_TELEPHONE_LOCALNUMBER");
        break;
      case IS_TIME_FULLTIME:
        AppendFlag("IS_TIME_FULLTIME");
        break;
      case IS_TIME_HOUR:
        AppendFlag("IS_TIME_HOUR");
        break;
      case IS_TIME_MINORSEC:
        AppendFlag("IS_TIME_MINORSEC");
        break;
      case IS_NUMBER_FULLWIDTH:
        AppendFlag("IS_NUMBER_FULLWIDTH");
        break;
      case IS_ALPHANUMERIC_HALFWIDTH:
        AppendFlag("IS_ALPHANUMERIC_HALFWIDTH");
        break;
      case IS_ALPHANUMERIC_FULLWIDTH:
        AppendFlag("IS_ALPHANUMERIC_FULLWIDTH");
        break;
      case IS_CURRENCY_CHINESE:
        AppendFlag("IS_CURRENCY_CHINESE");
        break;
      case IS_BOPOMOFO:
        AppendFlag("IS_BOPOMOFO");
        break;
      case IS_HIRAGANA:
        AppendFlag("IS_HIRAGANA");
        break;
      case IS_KATAKANA_HALFWIDTH:
        AppendFlag("IS_KATAKANA_HALFWIDTH");
        break;
      case IS_KATAKANA_FULLWIDTH:
        AppendFlag("IS_KATAKANA_FULLWIDTH");
        break;
      case IS_HANJA:
        AppendFlag("IS_HANJA");
        break;
      case IS_PHRASELIST:
        AppendFlag("IS_PHRASELIST");
        break;
      case IS_REGULAREXPRESSION:
        AppendFlag("IS_REGULAREXPRESSION");
        break;
      case IS_SRGS:
        AppendFlag("IS_SRGS");
        break;
      case IS_XML:
        AppendFlag("IS_XML");
        break;
      case IS_PRIVATE:
        AppendFlag("IS_PRIVATE");
        break;
      default:
        AppendFlag(nsPrintfCString("Unknown Value(%d)", inputScope));
        break;
    }
  }
}

/******************************************************************************
 * mozilla::widget::TSFUtils
 ******************************************************************************/

StaticRefPtr<ITfThreadMgr> TSFUtils::sThreadMgr;
StaticRefPtr<ITfMessagePump> TSFUtils::sMessagePump;
StaticRefPtr<ITfKeystrokeMgr> TSFUtils::sKeystrokeMgr;
StaticRefPtr<ITfDisplayAttributeMgr> TSFUtils::sDisplayAttrMgr;
StaticRefPtr<ITfCategoryMgr> TSFUtils::sCategoryMgr;
StaticRefPtr<ITfCompartment> TSFUtils::sCompartmentForOpenClose;
StaticRefPtr<ITfInputProcessorProfiles> TSFUtils::sInputProcessorProfiles;
StaticRefPtr<TSFTextStore> TSFUtils::sActiveTextStore;
StaticRefPtr<TSFTextStoreBase> TSFUtils::sCurrentTextStore;
StaticRefPtr<TSFEmptyTextStore> TSFUtils::sEmptyTextStore;
DWORD TSFUtils::sClientId = 0;

template void TSFUtils::ClearStoringTextStoresIf(
    const RefPtr<TSFTextStoreBase>& aTextStore);
template void TSFUtils::ClearStoringTextStoresIf(
    const RefPtr<TSFTextStore>& aTextStore);
template void TSFUtils::ClearStoringTextStoresIf(
    const RefPtr<TSFEmptyTextStore>& aTextStore);

void TSFUtils::Initialize() {
  MOZ_LOG(gIMELog, LogLevel::Info, ("TSFUtils::Initialize() is called..."));

  if (sThreadMgr) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFUtils::Initialize() FAILED due to already initialized"));
    return;
  }

  RefPtr<ITfThreadMgr> threadMgr;
  HRESULT hr =
      ::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                         IID_ITfThreadMgr, getter_AddRefs(threadMgr));
  if (FAILED(hr) || !threadMgr) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFUtils::Initialize() FAILED to "
             "create the thread manager, hr=0x%08lX",
             hr));
    return;
  }

  hr = threadMgr->Activate(&sClientId);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFUtils::Initialize() FAILED to activate, hr=0x%08lX", hr));
    return;
  }

  sThreadMgr = std::move(threadMgr);
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("  TSFTextStore::Initialize(), sThreadMgr=0x%p, sClientId=0x%08lX",
           sThreadMgr.get(), sClientId));
}

// static
void TSFUtils::Shutdown() {
  MOZ_LOG(gIMELog, LogLevel::Info, ("TSFUtils::Shutdown()"));

  TSFStaticSink::Shutdown();

  sDisplayAttrMgr = nullptr;
  sCategoryMgr = nullptr;
  if (RefPtr<TSFEmptyTextStore> emptyTextStore = sEmptyTextStore.forget()) {
    emptyTextStore->Destroy();
    MOZ_ASSERT(!sEmptyTextStore);
  }
  sActiveTextStore = nullptr;
  if (RefPtr<TSFTextStoreBase> textStore = sCurrentTextStore.forget()) {
    textStore->Destroy();
    MOZ_ASSERT(!sCurrentTextStore);
  }
  sCompartmentForOpenClose = nullptr;
  sInputProcessorProfiles = nullptr;
  sClientId = 0;
  if (sThreadMgr) {
    sThreadMgr->Deactivate();
    sThreadMgr = nullptr;
    sMessagePump = nullptr;
    sKeystrokeMgr = nullptr;
  }
}

template <typename TSFTextStoreClass>
void TSFUtils::ClearStoringTextStoresIf(
    const RefPtr<TSFTextStoreClass>& aTextStore) {
  if (sCurrentTextStore.get() ==
      static_cast<TSFTextStoreBase*>(aTextStore.get())) {
    MOZ_ASSERT(sActiveTextStore == sCurrentTextStore);
    sActiveTextStore = nullptr;
    sCurrentTextStore = nullptr;
    return;
  }
  if (static_cast<TSFTextStoreBase*>(sActiveTextStore.get()) ==
      static_cast<TSFTextStoreBase*>(aTextStore.get())) {
    sActiveTextStore = nullptr;
  }
}

IMENotificationRequests TSFUtils::GetIMENotificationRequests() {
  return sCurrentTextStore ? sCurrentTextStore->GetIMENotificationRequests()
                           : IMENotificationRequests();
}

inline std::ostream& operator<<(
    std::ostream& aStream,
    const mozilla::widget::TSFUtils::GotFocus& aGotFocus) {
  return aStream << "GotFocus::"
                 << (static_cast<bool>(aGotFocus) ? "Yes" : "No");
}

nsresult TSFUtils::OnFocusChange(GotFocus aGotFocus, nsWindow* aFocusedWindow,
                                 const InputContext& aContext) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("  TSFUtils::OnFocusChange(aGotFocus=%s, "
           "aFocusedWindow=0x%p, aContext=%s), "
           "ThreadMgr=0x%p, EnabledTextStore=0x%p",
           mozilla::ToString(aGotFocus).c_str(), aFocusedWindow,
           mozilla::ToString(aContext).c_str(), TSFUtils::GetThreadMgr(),
           TSFUtils::GetActiveTextStore()));

  if (!TSFUtils::IsAvailable()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  const RefPtr<TSFTextStoreBase> oldTextStore = sCurrentTextStore.forget();
  sActiveTextStore = nullptr;

  // If the oldTextStore is editable and still has focus, disassociate document
  // manager from the window handle.
  // NOTE: We never associate the document manager of TSFEmptyTextStore with
  // a window handle.  Therefore, we don't need to do this if the oldTextStore
  // is not editable.
  if (oldTextStore && oldTextStore->IsEditable()) {
    if (oldTextStore->MaybeHasFocus()) {
      const RefPtr<ITfThreadMgr> threadMgr(sThreadMgr);
      // If active window is switched, threadMgr has already handled the focus
      // change, then, we'll fail AssociateFocus() and the following assertions
      // will fail.  To avoid the latter, we should check whether the focused
      // documentMgr is still what oldTextStore set to.
      RefPtr<ITfDocumentMgr> focusedDocumentMgr;
      threadMgr->GetFocus(getter_AddRefs(focusedDocumentMgr));
      if (focusedDocumentMgr) {
        RefPtr<ITfDocumentMgr> prevFocusedDocumentMgr;
        DebugOnly<HRESULT> hr = threadMgr->AssociateFocus(
            oldTextStore->GetWindow()->GetWindowHandle(), nullptr,
            getter_AddRefs(prevFocusedDocumentMgr));
        NS_WARNING_ASSERTION(SUCCEEDED(hr), "Disassociating focus failed");
        NS_ASSERTION(
            FAILED(hr) ||
                prevFocusedDocumentMgr == oldTextStore->GetDocumentMgr(),
            nsPrintfCString("different documentMgr has been associated "
                            "with the window: expected: %p, but got: %p",
                            oldTextStore->GetDocumentMgr(),
                            prevFocusedDocumentMgr.get())
                .get());
      }
    }

    // Even if there was an editable focused TextStore, we won't use it with new
    // focused editor.  So, release it now.
    oldTextStore->Destroy();
  }

  if (NS_WARN_IF(!sThreadMgr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFUtils::OnFocusChange() FAILED, due to "
             "ThreadMgr being destroyed during calling "
             "ITfThreadMgr::AssociateFocus()"));
    return NS_ERROR_FAILURE;
  }
  if (NS_WARN_IF(sCurrentTextStore)) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("  TSFUtils::OnFocusChange() FAILED, due to "
         "nested event handling has created another focused TextStore during "
         "calling ITfThreadMgr::AssociateFocus()"));
    return NS_ERROR_FAILURE;
  }

  // If this is a notification of blur, move focus to the empty text store.
  if (aGotFocus == GotFocus::No || !aContext.mIMEState.IsEditable()) {
    if (aFocusedWindow->Destroyed()) {
      return NS_OK;
    }
    if (!aContext.mIMEState.IsEditable()) {
      if (RefPtr<TSFEmptyTextStore> emptyTextStore = sEmptyTextStore) {
        nsresult rv =
            emptyTextStore->SetFocusAndUpdateDocumentURLAndBrowsingMode(
                aFocusedWindow, aContext);
        if (NS_SUCCEEDED(rv)) {
          sCurrentTextStore = emptyTextStore.forget();
          return NS_OK;
        }
        MOZ_LOG(gIMELog, LogLevel::Error,
                ("  TSFUtils::OnFocusChange() FAILED due to the failure of "
                 "TSFEmptyTextStore::"
                 "SetFocusAndUpdateDocumentURLAndBrowsingMode(), trying to "
                 "create new TSFEmptyTextStore..."));
      }
    }
    Result<RefPtr<TSFEmptyTextStore>, nsresult> ret =
        TSFEmptyTextStore::CreateAndSetFocus(aFocusedWindow, aContext);
    if (NS_WARN_IF(ret.isErr())) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("  TSFUtils::OnFocusChange() FAILED due to failing to "
               "create and set focus to new TSFEmptyTextStore"));
      return ret.unwrapErr();
    }
    if (const RefPtr<TSFEmptyTextStore> oldEmptyTextStore =
            sEmptyTextStore.forget()) {
      oldEmptyTextStore->Destroy();
    }
    sEmptyTextStore = ret.unwrap().forget();
    sCurrentTextStore = sEmptyTextStore;
    return NS_OK;
  }

  // If an editor is getting focus, create new TextStore and set focus.
  Result<RefPtr<TSFTextStore>, nsresult> ret =
      TSFTextStore::CreateAndSetFocus(aFocusedWindow, aContext);
  if (NS_WARN_IF(ret.isErr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFUtils::OnFocusChange() FAILED due to failing to create "
             "and set focus to new TSFTextStore failed"));
    return ret.unwrapErr();
  }
  sActiveTextStore = ret.unwrap().forget();
  sCurrentTextStore = sActiveTextStore;
  return NS_OK;
}

void TSFUtils::EnsureMessagePump() {
  if (!sThreadMgr) {
    return;
  }
  static bool sInitialized = false;
  // If it tried to retrieve ITfMessagePump from sThreadMgr but it failed,
  // we shouldn't retry it at every message due to performance reason.
  // Although this shouldn't occur actually.
  if (sInitialized) {
    return;
  }
  sInitialized = true;

  RefPtr<ITfMessagePump> messagePump;
  HRESULT hr = sThreadMgr->QueryInterface(IID_ITfMessagePump,
                                          getter_AddRefs(messagePump));
  if (NS_WARN_IF(FAILED(hr)) || NS_WARN_IF(!messagePump)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::GetMessagePump() FAILED to "
             "QI message pump from the thread manager, hr=0x%08lX",
             hr));
    return;
  }
  sMessagePump = messagePump.forget();
}

void TSFUtils::EnsureKeystrokeMgr() {
  static bool sInitialized = false;
  // If it tried to retrieve ITfKeystrokeMgr from sThreadMgr but it failed,
  // we shouldn't retry it at every keydown nor keyup due to performance
  // reason.  Although this shouldn't occur actually.
  if (sInitialized) {
    return;
  }
  RefPtr<ITfKeystrokeMgr> keystrokeMgr;
  HRESULT hr = sThreadMgr->QueryInterface(IID_ITfKeystrokeMgr,
                                          getter_AddRefs(keystrokeMgr));
  if (NS_WARN_IF(FAILED(hr)) || NS_WARN_IF(!keystrokeMgr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::EnsureKeystrokeMgr() FAILED to "
             "QI keystroke manager from the thread manager, hr=0x%08lX",
             hr));
    return;
  }
  sKeystrokeMgr = keystrokeMgr.forget();
}

ITfInputProcessorProfiles* TSFUtils::GetInputProcessorProfiles() {
  if (sInputProcessorProfiles) {
    return sInputProcessorProfiles;
  }
  // XXX MSDN documents that ITfInputProcessorProfiles is available only on
  //     desktop apps.  However, there is no known way to obtain
  //     ITfInputProcessorProfileMgr instance without ITfInputProcessorProfiles
  //     instance.
  RefPtr<ITfInputProcessorProfiles> inputProcessorProfiles;
  HRESULT hr = ::CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
      IID_ITfInputProcessorProfiles, getter_AddRefs(inputProcessorProfiles));
  if (NS_WARN_IF(FAILED(hr)) || NS_WARN_IF(!inputProcessorProfiles)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::GetInputProcessorProfiles() FAILED to create input "
             "processor profiles, hr=0x%08lX",
             hr));
    return nullptr;
  }
  sInputProcessorProfiles = inputProcessorProfiles;
  return sInputProcessorProfiles;
}

ITfDisplayAttributeMgr* TSFUtils::GetDisplayAttributeMgr() {
  if (sDisplayAttrMgr) {
    return sDisplayAttrMgr;
  }

  RefPtr<ITfDisplayAttributeMgr> displayAttributeMgr;
  HRESULT hr = ::CoCreateInstance(
      CLSID_TF_DisplayAttributeMgr, nullptr, CLSCTX_INPROC_SERVER,
      IID_ITfDisplayAttributeMgr, getter_AddRefs(displayAttributeMgr));
  if (NS_WARN_IF(FAILED(hr)) || NS_WARN_IF(!displayAttributeMgr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::GetDisplayAttributeMgr() FAILED to create "
             "a display attribute manager instance, hr=0x%08lX",
             hr));
    return nullptr;
  }
  sDisplayAttrMgr = displayAttributeMgr.forget();
  return sDisplayAttrMgr;
}

ITfCategoryMgr* TSFUtils::GetCategoryMgr() {
  if (sCategoryMgr) {
    return sCategoryMgr;
  }
  RefPtr<ITfCategoryMgr> categoryMgr;
  HRESULT hr =
      ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                         IID_ITfCategoryMgr, getter_AddRefs(categoryMgr));
  if (NS_WARN_IF(FAILED(hr)) || NS_WARN_IF(!categoryMgr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::GetCategoryMgr() FAILED to create "
             "a category manager instance, hr=0x%08lX",
             hr));
    return nullptr;
  }
  sCategoryMgr = categoryMgr.forget();
  return sCategoryMgr;
}

// static
ITfCompartment* TSFUtils::GetCompartmentForOpenClose() {
  if (sCompartmentForOpenClose) {
    return sCompartmentForOpenClose;
  }

  if (!sThreadMgr) {
    return nullptr;
  }

  RefPtr<ITfCompartmentMgr> compartmentMgr;
  HRESULT hr = sThreadMgr->QueryInterface(IID_ITfCompartmentMgr,
                                          getter_AddRefs(compartmentMgr));
  if (NS_WARN_IF(FAILED(hr)) || NS_WARN_IF(!compartmentMgr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::GetCompartmentForOpenClose() FAILED due to"
             "sThreadMgr not having ITfCompartmentMgr, hr=0x%08lX",
             hr));
    return nullptr;
  }

  RefPtr<ITfCompartment> compartment;
  hr = compartmentMgr->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                      getter_AddRefs(compartment));
  if (NS_WARN_IF(FAILED(hr)) || NS_WARN_IF(!compartment)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::GetCompartmentForOpenClose() FAILED due to"
             "ITfCompartmentMgr::GetCompartment() failuere, hr=0x%08lX",
             hr));
    return nullptr;
  }

  sCompartmentForOpenClose = compartment;
  return sCompartmentForOpenClose;
}

bool TSFUtils::DoNotReturnErrorFromGetSelection() {
  // There is a crash bug of TSF if we return error from GetSelection().
  // That was introduced in Anniversary Update (build 14393, see bug 1312302)
  // TODO: We should avoid to run this hack on fixed builds.  When we get
  //       exact build number, we should get back here.
  static bool sTSFMayCrashIfGetSelectionReturnsError =
      IsWin10AnniversaryUpdateOrLater();
  return sTSFMayCrashIfGetSelectionReturnsError;
}

TSFUtils::AutoRangeExtant::AutoRangeExtant(ITfRange* aRange) {
  RefPtr<ITfRangeACP> rangeACP;
  aRange->QueryInterface(IID_ITfRangeACP, getter_AddRefs(rangeACP));
  if (MOZ_UNLIKELY(!rangeACP)) {
    return;
  }
  mHR = rangeACP->GetExtent(&mStart, &mLength);
}

TextRangeType TSFUtils::GetTextRangeType(TF_DISPLAYATTRIBUTE& aDisplayAttr) {
  switch (aDisplayAttr.bAttr) {
    case TF_ATTR_TARGET_CONVERTED:
      return TextRangeType::eSelectedClause;
    case TF_ATTR_CONVERTED:
      return TextRangeType::eConvertedClause;
    case TF_ATTR_TARGET_NOTCONVERTED:
      return TextRangeType::eSelectedRawClause;
    default:
      return TextRangeType::eRawClause;
  }
}

Maybe<nscolor> TSFUtils::GetColor(const TF_DA_COLOR& aTSFColor) {
  switch (aTSFColor.type) {
    case TF_CT_SYSCOLOR: {
      DWORD sysColor = ::GetSysColor(aTSFColor.nIndex);
      return Some(NS_RGB(GetRValue(sysColor), GetGValue(sysColor),
                         GetBValue(sysColor)));
    }
    case TF_CT_COLORREF:
      return Some(NS_RGB(GetRValue(aTSFColor.cr), GetGValue(aTSFColor.cr),
                         GetBValue(aTSFColor.cr)));
    case TF_CT_NONE:
    default:
      return Nothing();
  }
}

Maybe<TextRangeStyle::LineStyle> TSFUtils::GetLineStyle(
    TF_DA_LINESTYLE aTSFLineStyle) {
  switch (aTSFLineStyle) {
    case TF_LS_NONE:
      return Some(TextRangeStyle::LineStyle::None);
    case TF_LS_SOLID:
      return Some(TextRangeStyle::LineStyle::Solid);
    case TF_LS_DOT:
      return Some(TextRangeStyle::LineStyle::Dotted);
    case TF_LS_DASH:
      return Some(TextRangeStyle::LineStyle::Dashed);
    case TF_LS_SQUIGGLE:
      return Some(TextRangeStyle::LineStyle::Wavy);
    default:
      return Nothing();
  }
}

bool TSFUtils::ShouldSetInputScopeOfURLBarToDefault() {
  // FYI: Google Japanese Input may be an IMM-IME.  If it's installed on
  //      Win7, it's always IMM-IME.  Otherwise, basically, it's a TIP.
  //      However, if it's installed on Win7 and has not been updated yet
  //      after the OS is upgraded to Win8 or later, it's still an IMM-IME.
  //      Therefore, we also need to check with IMMHandler here.
  if (!StaticPrefs::intl_ime_hack_set_input_scope_of_url_bar_to_default()) {
    return false;
  }

  if (IMMHandler::IsGoogleJapaneseInputActive()) {
    return true;
  }

  switch (TSFStaticSink::ActiveTIP()) {
    case TextInputProcessorID::MicrosoftIMEForJapanese:
    case TextInputProcessorID::GoogleJapaneseInput:
    case TextInputProcessorID::MicrosoftBopomofo:
    case TextInputProcessorID::MicrosoftChangJie:
    case TextInputProcessorID::MicrosoftPhonetic:
    case TextInputProcessorID::MicrosoftQuick:
    case TextInputProcessorID::MicrosoftNewChangJie:
    case TextInputProcessorID::MicrosoftNewPhonetic:
    case TextInputProcessorID::MicrosoftNewQuick:
    case TextInputProcessorID::MicrosoftPinyin:
    case TextInputProcessorID::MicrosoftPinyinNewExperienceInputStyle:
    case TextInputProcessorID::MicrosoftOldHangul:
    case TextInputProcessorID::MicrosoftWubi:
    case TextInputProcessorID::MicrosoftIMEForKorean:
      return true;
    default:
      return false;
  }
}

TSFUtils::AttrIndex TSFUtils::GetRequestedAttrIndex(const TS_ATTRID& aAttrID) {
  if (IsEqualGUID(aAttrID, GUID_PROP_INPUTSCOPE)) {
    return AttrIndex::InputScope;
  }
  if (IsEqualGUID(aAttrID, sGUID_PROP_URL)) {
    return AttrIndex::DocumentURL;
  }
  if (IsEqualGUID(aAttrID, TSATTRID_Text_VerticalWriting)) {
    return AttrIndex::TextVerticalWriting;
  }
  if (IsEqualGUID(aAttrID, TSATTRID_Text_Orientation)) {
    return AttrIndex::TextOrientation;
  }
  return AttrIndex::NotSupported;
}

TS_ATTRID TSFUtils::GetAttrID(AttrIndex aIndex) {
  switch (aIndex) {
    case AttrIndex::InputScope:
      return GUID_PROP_INPUTSCOPE;
    case AttrIndex::DocumentURL:
      return sGUID_PROP_URL;
    case AttrIndex::TextVerticalWriting:
      return TSATTRID_Text_VerticalWriting;
    case AttrIndex::TextOrientation:
      return TSATTRID_Text_Orientation;
    default:
      MOZ_CRASH("Invalid index? Or not implemented yet?");
      return GUID_NULL;
  }
}

Result<RefPtr<ITfCompartment>, bool> TSFUtils::GetCompartment(IUnknown* pUnk,
                                                              const GUID& aID) {
  if (MOZ_UNLIKELY(!pUnk)) {
    return Err(false);
  }

  RefPtr<ITfCompartmentMgr> compMgr;
  pUnk->QueryInterface(IID_ITfCompartmentMgr, getter_AddRefs(compMgr));
  if (MOZ_UNLIKELY(!compMgr)) {
    return Err(false);
  }

  RefPtr<ITfCompartment> compartment;
  HRESULT hr = compMgr->GetCompartment(aID, getter_AddRefs(compartment));
  if (MOZ_UNLIKELY(FAILED(hr) || !compartment)) {
    return Err(false);
  }
  return std::move(compartment);
}

bool TSFUtils::MarkContextAsKeyboardDisabled(ITfContext* aContext) {
  VARIANT variant_int4_value1;
  variant_int4_value1.vt = VT_I4;
  variant_int4_value1.lVal = 1;

  Result<RefPtr<ITfCompartment>, bool> compartmentOrError =
      TSFUtils::GetCompartment(aContext, GUID_COMPARTMENT_KEYBOARD_DISABLED);
  if (MOZ_UNLIKELY(compartmentOrError.isErr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::MarkContextAsKeyboardDisabled(aContext=0x%p) failed",
             aContext));
    return false;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("TSFUtils::MarkContextAsKeyboardDisabled(), setting "
           "to disable context 0x%p...",
           aContext));
  return SUCCEEDED(
      compartmentOrError.unwrap()->SetValue(sClientId, &variant_int4_value1));
}

bool TSFUtils::MarkContextAsEmpty(ITfContext* aContext) {
  VARIANT variant_int4_value1;
  variant_int4_value1.vt = VT_I4;
  variant_int4_value1.lVal = 1;

  Result<RefPtr<ITfCompartment>, bool> compartmentOrError =
      TSFUtils::GetCompartment(aContext, GUID_COMPARTMENT_EMPTYCONTEXT);
  if (MOZ_UNLIKELY(compartmentOrError.isErr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFUtils::MarkContextAsEmpty(aContext=%p) failed", aContext));
    return false;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("TSFUtils::MarkContextAsEmpty(aContext=%p), setting to mark as "
           "empty context",
           aContext));
  return SUCCEEDED(
      compartmentOrError.unwrap()->SetValue(sClientId, &variant_int4_value1));
}

}  // namespace mozilla::widget

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FrameMetrics.h"
#include "js/Array.h"  // JS::GetArrayLength, JS::IsArrayObject
#include "js/JSON.h"
#include "js/Object.h"
#include "js/PropertyAndElement.h"  // JS_GetElement
#include "js/TypeDecls.h"
#include "jsapi.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/AutocompleteInfoBinding.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/CustomElementTypes.h"
#include "mozilla/dom/CustomElementRegistry.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/FragmentOrElement.h"
#include "mozilla/dom/HTMLElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "mozilla/dom/HTMLTextAreaElement.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/SessionStorageManager.h"
#include "mozilla/dom/PBackgroundSessionStorageCache.h"
#include "mozilla/dom/SessionStoreChangeListener.h"
#include "mozilla/dom/SessionStoreChild.h"
#include "mozilla/dom/SessionStoreUtils.h"
#include "mozilla/dom/SessionStoreUtilsBinding.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/TrustedHTML.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/dom/sessionstore/SessionStoreTypes.h"
#include "mozilla/dom/txIXPathContext.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/WindowProxyHolder.h"
#include "mozilla/dom/XPathResult.h"
#include "mozilla/dom/XPathEvaluator.h"
#include "mozilla/dom/XPathExpression.h"
#include "mozilla/dom/PBackgroundSessionStorageCache.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/UniquePtr.h"
#include "nsCharSeparatedTokenizer.h"
#include "mozilla/dom/ContentList.h"
#include "nsContentUtils.h"
#include "nsFocusManager.h"
#include "nsGlobalWindowInner.h"
#include "nsIContentInlines.h"
#include "nsIDocShell.h"
#include "nsIFormControl.h"
#include "nsISHistory.h"
#include "nsIXULRuntime.h"
#include "nsPresContext.h"
#include "nsPrintfCString.h"
#include "nsDocShell.h"
#include "nsNetUtil.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::dom::sessionstore;

/* static */
void SessionStoreUtils::ForEachNonDynamicChildFrame(
    const GlobalObject& aGlobal, WindowProxyHolder& aWindow,
    SessionStoreUtilsFrameCallback& aCallback, ErrorResult& aRv) {
  if (!aWindow.get()) {
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  nsCOMPtr<nsIDocShell> docShell = aWindow.get()->GetDocShell();
  if (!docShell) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  int32_t length;
  aRv = docShell->GetInProcessChildCount(&length);
  if (aRv.Failed()) {
    return;
  }

  for (int32_t i = 0; i < length; ++i) {
    nsCOMPtr<nsIDocShellTreeItem> item;
    docShell->GetInProcessChildAt(i, getter_AddRefs(item));
    if (!item) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    RefPtr<BrowsingContext> context = item->GetBrowsingContext();
    if (!context) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    if (!context->CreatedDynamically()) {
      int32_t childOffset = context->ChildOffset();
      aCallback.Call(WindowProxyHolder(context.forget()), childOffset);
    }
  }
}

/* static */
void SessionStoreUtils::CollectDocShellCapabilities(const GlobalObject& aGlobal,
                                                    nsIDocShell* aDocShell,
                                                    nsCString& aRetVal) {
  bool allow;

#define TRY_ALLOWPROP(y)          \
  PR_BEGIN_MACRO                  \
  aDocShell->GetAllow##y(&allow); \
  if (!allow) {                   \
    if (!aRetVal.IsEmpty()) {     \
      aRetVal.Append(',');        \
    }                             \
    aRetVal.Append(#y);           \
  }                               \
  PR_END_MACRO

  // Bug 1328013 : Don't collect "AllowJavascript" property
  // TRY_ALLOWPROP(Javascript);
  TRY_ALLOWPROP(MetaRedirects);
  TRY_ALLOWPROP(Subframes);
  TRY_ALLOWPROP(Images);
  TRY_ALLOWPROP(Media);
  TRY_ALLOWPROP(DNSPrefetch);
  TRY_ALLOWPROP(WindowControl);
  TRY_ALLOWPROP(Auth);
  TRY_ALLOWPROP(ContentRetargeting);
  TRY_ALLOWPROP(ContentRetargetingOnChildren);
#undef TRY_ALLOWPROP
}

/* static */
void SessionStoreUtils::RestoreDocShellCapabilities(
    nsIDocShell* aDocShell, const nsCString& aDisallowCapabilities) {
  aDocShell->SetAllowMetaRedirects(true);
  aDocShell->SetAllowSubframes(true);
  aDocShell->SetAllowImages(true);
  aDocShell->SetAllowMedia(true);
  aDocShell->SetAllowDNSPrefetch(true);
  aDocShell->SetAllowWindowControl(true);
  aDocShell->SetAllowContentRetargeting(true);
  aDocShell->SetAllowContentRetargetingOnChildren(true);

  for (const nsACString& token :
       nsCCharSeparatedTokenizer(aDisallowCapabilities, ',').ToRange()) {
    if (token.EqualsLiteral("MetaRedirects")) {
      aDocShell->SetAllowMetaRedirects(false);
    } else if (token.EqualsLiteral("Subframes")) {
      aDocShell->SetAllowSubframes(false);
    } else if (token.EqualsLiteral("Images")) {
      aDocShell->SetAllowImages(false);
    } else if (token.EqualsLiteral("Media")) {
      aDocShell->SetAllowMedia(false);
    } else if (token.EqualsLiteral("DNSPrefetch")) {
      aDocShell->SetAllowDNSPrefetch(false);
    } else if (token.EqualsLiteral("WindowControl")) {
      aDocShell->SetAllowWindowControl(false);
    } else if (token.EqualsLiteral("ContentRetargeting")) {
      bool allow;
      aDocShell->GetAllowContentRetargetingOnChildren(&allow);
      aDocShell->SetAllowContentRetargeting(
          false);  // will also set AllowContentRetargetingOnChildren
      aDocShell->SetAllowContentRetargetingOnChildren(
          allow);  // restore the allowProp to original
    } else if (token.EqualsLiteral("ContentRetargetingOnChildren")) {
      aDocShell->SetAllowContentRetargetingOnChildren(false);
    }
  }
}

static void CollectCurrentScrollPosition(JSContext* aCx, Document& aDocument,
                                         Nullable<CollectedData>& aRetVal) {
  PresShell* presShell = aDocument.GetPresShell();
  if (!presShell) {
    return;
  }
  nsPoint scrollPos = presShell->GetVisualViewportOffset();
  int scrollX = nsPresContext::AppUnitsToIntCSSPixels(scrollPos.x);
  int scrollY = nsPresContext::AppUnitsToIntCSSPixels(scrollPos.y);

  if ((scrollX != 0) || (scrollY != 0)) {
    aRetVal.SetValue().mScroll.Construct() =
        nsPrintfCString("%d,%d", scrollX, scrollY);
  }
}

/* static */
void SessionStoreUtils::RestoreScrollPosition(const GlobalObject& aGlobal,
                                              nsGlobalWindowInner& aWindow,
                                              const CollectedData& aData) {
  if (aData.mScroll.WasPassed()) {
    RestoreScrollPosition(aWindow, aData.mScroll.Value());
  }
}

/* static */
void SessionStoreUtils::RestoreScrollPosition(
    nsGlobalWindowInner& aWindow, const nsCString& aScrollPosition) {
  using Change = mozilla::dom::SessionStoreChangeListener::Change;
  SessionStoreChangeListener::CollectSessionStoreData(
      aWindow.GetWindowContext(), EnumSet<Change>(Change::Scroll));

  nsCCharSeparatedTokenizer tokenizer(aScrollPosition, ',');
  nsAutoCString token(tokenizer.nextToken());
  int pos_X = atoi(token.get());
  token = tokenizer.nextToken();
  int pos_Y = atoi(token.get());

  aWindow.ScrollTo(pos_X, pos_Y);

  if (nsCOMPtr<Document> doc = aWindow.GetExtantDoc()) {
    if (nsPresContext* presContext = doc->GetPresContext()) {
      if (presContext->IsRootContentDocumentCrossProcess()) {
        // Use MainThread so this takes precedence over session history
        // (ScrollFrameHelper::ScrollToRestoredPosition()).
        presContext->PresShell()->ScrollToVisual(
            CSSPoint::ToAppUnits(CSSPoint(pos_X, pos_Y)),
            layers::ScrollOffsetUpdateType::MainThread, ScrollMode::Instant);
      }
    }
  }
}

// Implements the Luhn checksum algorithm as described at
// http://wikipedia.org/wiki/Luhn_algorithm
// Number digit lengths vary with network, but should fall within 12-19 range.
// [2] More details at https://en.wikipedia.org/wiki/Payment_card_number
static bool IsValidCCNumber(nsAString& aValue) {
  uint32_t total = 0;
  uint32_t numLength = 0;
  uint32_t strLen = aValue.Length();
  for (uint32_t i = 0; i < strLen; ++i) {
    uint32_t idx = strLen - i - 1;
    // ignore whitespace and dashes)
    char16_t chr = aValue[idx];
    if (IsSpaceCharacter(chr) || chr == '-') {
      continue;
    }
    // If our number is too long, note that fact
    ++numLength;
    if (numLength > 19) {
      return false;
    }
    // Try to parse the character as a base-10 integer.
    nsresult rv = NS_OK;
    uint32_t val = Substring(aValue, idx, 1).ToInteger(&rv, 10);
    if (NS_FAILED(rv)) {
      return false;
    }
    if (i % 2 == 1) {
      val *= 2;
      if (val > 9) {
        val -= 9;
      }
    }
    total += val;
  }

  return numLength >= 12 && total % 10 == 0;
}

// Limit the number of XPath expressions for performance reasons. See bug
// 477564.
static const uint16_t kMaxTraversedXPaths = 100;

// A helper function to append a element into mId or mXpath of CollectedData
static Record<nsString, OwningStringOrBooleanOrObject>::EntryType*
AppendEntryToCollectedData(nsINode* aNode, const nsAString& aId,
                           uint16_t& aGeneratedCount,
                           Nullable<CollectedData>& aRetVal) {
  Record<nsString, OwningStringOrBooleanOrObject>::EntryType* entry;
  if (!aId.IsEmpty()) {
    if (!aRetVal.SetValue().mId.WasPassed()) {
      aRetVal.SetValue().mId.Construct();
    }
    auto& recordEntries = aRetVal.SetValue().mId.Value().Entries();
    entry = recordEntries.AppendElement();
    entry->mKey = aId;
  } else {
    if (!aRetVal.SetValue().mXpath.WasPassed()) {
      aRetVal.SetValue().mXpath.Construct();
    }
    auto& recordEntries = aRetVal.SetValue().mXpath.Value().Entries();
    entry = recordEntries.AppendElement();
    nsAutoString xpath;
    aNode->GenerateXPath(xpath);
    aGeneratedCount++;
    entry->mKey = std::move(xpath);
  }
  return entry;
}

/* for bool value */
static void AppendValueToCollectedData(nsINode* aNode, const nsAString& aId,
                                       const bool& aValue,
                                       uint16_t& aGeneratedCount,
                                       JSContext* aCx,
                                       Nullable<CollectedData>& aRetVal) {
  Record<nsString, OwningStringOrBooleanOrObject>::EntryType* entry =
      AppendEntryToCollectedData(aNode, aId, aGeneratedCount, aRetVal);
  entry->mValue.SetAsBoolean() = aValue;
}

/* for nsString value */
static void AppendValueToCollectedData(nsINode* aNode, const nsAString& aId,
                                       const nsString& aValue,
                                       uint16_t& aGeneratedCount,
                                       Nullable<CollectedData>& aRetVal) {
  Record<nsString, OwningStringOrBooleanOrObject>::EntryType* entry =
      AppendEntryToCollectedData(aNode, aId, aGeneratedCount, aRetVal);
  entry->mValue.SetAsString() = aValue;
}

/* for single select value */
static void AppendValueToCollectedData(
    nsINode* aNode, const nsAString& aId,
    const CollectedNonMultipleSelectValue& aValue, uint16_t& aGeneratedCount,
    JSContext* aCx, Nullable<CollectedData>& aRetVal) {
  JS::Rooted<JS::Value> jsval(aCx);
  if (!ToJSValue(aCx, aValue, &jsval)) {
    JS_ClearPendingException(aCx);
    return;
  }
  Record<nsString, OwningStringOrBooleanOrObject>::EntryType* entry =
      AppendEntryToCollectedData(aNode, aId, aGeneratedCount, aRetVal);
  entry->mValue.SetAsObject() = &jsval.toObject();
}

/* special handing for input element with string type */
static void AppendValueToCollectedData(Document& aDocument, nsINode* aNode,
                                       const nsAString& aId,
                                       const nsString& aValue,
                                       uint16_t& aGeneratedCount,
                                       JSContext* aCx,
                                       Nullable<CollectedData>& aRetVal) {
  if (!aId.IsEmpty()) {
    // We want to avoid saving data for about:sessionrestore as a string.
    // Since it's stored in the form as stringified JSON, stringifying
    // further causes an explosion of escape characters. cf. bug 467409
    if (aId.EqualsLiteral("sessionData")) {
      nsAutoCString url;
      (void)aDocument.GetDocumentURI()->GetSpecIgnoringRef(url);
      if (url.EqualsLiteral("about:sessionrestore") ||
          url.EqualsLiteral("about:welcomeback")) {
        JS::Rooted<JS::Value> jsval(aCx);
        if (JS_ParseJSON(aCx, aValue.get(), aValue.Length(), &jsval) &&
            jsval.isObject()) {
          Record<nsString, OwningStringOrBooleanOrObject>::EntryType* entry =
              AppendEntryToCollectedData(aNode, aId, aGeneratedCount, aRetVal);
          entry->mValue.SetAsObject() = &jsval.toObject();
        } else {
          JS_ClearPendingException(aCx);
        }
        return;
      }
    }
  }
  AppendValueToCollectedData(aNode, aId, aValue, aGeneratedCount, aRetVal);
}

/* for nsTArray<nsString>: file and multipleSelect */
static void AppendValueToCollectedData(nsINode* aNode, const nsAString& aId,
                                       const nsAString& aValueType,
                                       nsTArray<nsString>& aValue,
                                       uint16_t& aGeneratedCount,
                                       JSContext* aCx,
                                       Nullable<CollectedData>& aRetVal) {
  JS::Rooted<JS::Value> jsval(aCx);
  if (aValueType.EqualsLiteral("file")) {
    CollectedFileListValue val;
    val.mType = aValueType;
    val.mFileList = std::move(aValue);
    if (!ToJSValue(aCx, val, &jsval)) {
      JS_ClearPendingException(aCx);
      return;
    }
  } else {
    if (!ToJSValue(aCx, aValue, &jsval)) {
      JS_ClearPendingException(aCx);
      return;
    }
  }
  Record<nsString, OwningStringOrBooleanOrObject>::EntryType* entry =
      AppendEntryToCollectedData(aNode, aId, aGeneratedCount, aRetVal);
  entry->mValue.SetAsObject() = &jsval.toObject();
}

/* For form-associated custom element state */
static void AppendValueToCollectedData(
    nsINode* aNode, const nsAString& aId,
    const Nullable<OwningFileOrUSVStringOrFormData>& aValue,
    const Nullable<OwningFileOrUSVStringOrFormData>& aState,
    uint16_t& aGeneratedCount, JSContext* aCx,
    Nullable<CollectedData>& aRetVal) {
  Record<nsString, OwningStringOrBooleanOrObject>::EntryType* entry =
      AppendEntryToCollectedData(aNode, aId, aGeneratedCount, aRetVal);

  CollectedCustomElementValue val;
  val.mValue = aValue;
  val.mState = aState;
  JS::Rooted<JS::Value> jsval(aCx);
  if (!ToJSValue(aCx, val, &jsval)) {
    JS_ClearPendingException(aCx);
    return;
  }
  entry->mValue.SetAsObject() = &jsval.toObject();
}

// This isn't size as in binary size, just a heuristic to not store too large
// fields in session store. See StaticPrefs::browser_sessionstore_dom_form_limit
static uint32_t SizeOfFormEntry(const FormEntryValue& aValue) {
  uint32_t size = 0;
  switch (aValue.type()) {
    case FormEntryValue::TCheckbox:
      size = aValue.get_Checkbox().value() ? 4 : 5;
      break;
    case FormEntryValue::TTextField:
      size = aValue.get_TextField().value().Length();
      break;
    case FormEntryValue::TFileList: {
      for (const auto& value : aValue.get_FileList().valueList()) {
        size += value.Length();
      }
      break;
    }
    case FormEntryValue::TSingleSelect:
      size = aValue.get_SingleSelect().value().Length();
      break;
    case FormEntryValue::TMultipleSelect: {
      for (const auto& value : aValue.get_MultipleSelect().valueList()) {
        size += value.Length();
      }
      break;
    }
    case FormEntryValue::TCustomElementTuple: {
      auto customElementTupleSize =
          [](const CustomElementFormValue& value) -> uint32_t {
        switch (value.type()) {
          case CustomElementFormValue::TBlobImpl:
            return value.get_BlobImpl()->GetAllocationSize();
          case CustomElementFormValue::TnsString:
            return value.get_nsString().Length();
          case CustomElementFormValue::TArrayOfFormDataTuple: {
            uint32_t formDataSize = 0;
            for (const auto& entry : value.get_ArrayOfFormDataTuple()) {
              formDataSize += entry.name().Length();
              const auto& entryValue = entry.value();
              switch (entryValue.type()) {
                case IPCFormDataValue::TBlobImpl:
                  formDataSize +=
                      entryValue.get_BlobImpl()->GetAllocationSize();
                  break;
                case IPCFormDataValue::TnsString:
                  formDataSize += entryValue.get_nsString().Length();
                  break;
                default:
                  break;
              }
            }
            return formDataSize;
          }
          default:
            return 0;
        }
      };

      const auto& ceTuple = aValue.get_CustomElementTuple();
      size += customElementTupleSize(ceTuple.value());
      size += customElementTupleSize(ceTuple.state());
      break;
    }
    default:
      break;
  }
  return size;
}

static uint32_t AppendEntry(nsINode* aNode, const nsString& aId,
                            const FormEntryValue& aValue,
                            sessionstore::FormData& aFormData) {
  uint32_t size = SizeOfFormEntry(aValue);
  if (size > StaticPrefs::browser_sessionstore_dom_form_limit()) {
    return 0;
  }

  if (aId.IsEmpty()) {
    FormEntry* entry = aFormData.xpath().AppendElement();
    entry->value() = aValue;
    aNode->GenerateXPath(entry->id());
    size += entry->id().Length();
  } else {
    aFormData.id().AppendElement(FormEntry{aId, aValue});
    size += aId.Length();
  }

  return size;
}

static uint32_t CollectTextAreaElement(Document* aDocument,
                                       sessionstore::FormData& aFormData) {
  uint32_t size = 0;
  RefPtr<ContentList> textlist =
      NS_GetContentList(aDocument, kNameSpaceID_XHTML, u"textarea"_ns);
  uint32_t length = textlist->Length();
  for (uint32_t i = 0; i < length; ++i) {
    MOZ_ASSERT(textlist->Item(i), "null item in node list!");

    auto* textArea = HTMLTextAreaElement::FromNodeOrNull(textlist->Item(i));
    if (!textArea) {
      continue;
    }
    DOMString autocomplete;
    textArea->GetAutocomplete(autocomplete);
    if (autocomplete.EqualsLiteral("off")) {
      continue;
    }
    nsAutoString id;
    textArea->GetId(id);
    if (id.IsEmpty() && (aFormData.xpath().Length() > kMaxTraversedXPaths)) {
      continue;
    }
    nsString value;
    textArea->GetValue(value);
    // In order to reduce XPath generation (which is slow), we only save data
    // for form fields that have been changed. (cf. bug 537289)
    if (textArea->AttrValueIs(kNameSpaceID_None, nsGkAtoms::value, value,
                              eCaseMatters)) {
      continue;
    }

    size += AppendEntry(textArea, id, TextField{value}, aFormData);
  }

  return size;
}

static uint32_t CollectInputElement(Document* aDocument,
                                    sessionstore::FormData& aFormData) {
  uint32_t size = 0;
  RefPtr<ContentList> inputlist =
      NS_GetContentList(aDocument, kNameSpaceID_XHTML, u"input"_ns);
  uint32_t length = inputlist->Length();
  for (uint32_t i = 0; i < length; ++i) {
    MOZ_ASSERT(inputlist->Item(i), "null item in node list!");
    if (const auto* formControl =
            nsIFormControl::FromNodeOrNull(inputlist->Item(i))) {
      auto controlType = formControl->ControlType();
      if (controlType == FormControlType::InputPassword ||
          controlType == FormControlType::InputHidden ||
          controlType == FormControlType::InputButton ||
          controlType == FormControlType::InputImage ||
          controlType == FormControlType::InputSubmit ||
          controlType == FormControlType::InputReset) {
        continue;
      }
    }
    RefPtr<HTMLInputElement> input =
        HTMLInputElement::FromNodeOrNull(inputlist->Item(i));
    if (!input || !nsContentUtils::IsAutocompleteEnabled(input)) {
      continue;
    }
    nsAutoString id;
    input->GetId(id);
    if (id.IsEmpty() && (aFormData.xpath().Length() > kMaxTraversedXPaths)) {
      continue;
    }
    Nullable<AutocompleteInfo> aInfo;
    input->GetAutocompleteInfo(aInfo);
    if (!aInfo.IsNull() && !aInfo.Value().mCanAutomaticallyPersist) {
      continue;
    }

    FormEntryValue value;
    if (input->ControlType() == FormControlType::InputCheckbox ||
        input->ControlType() == FormControlType::InputRadio) {
      bool checked = input->Checked();
      if (checked == input->DefaultChecked()) {
        continue;
      }
      size += AppendEntry(input, id, Checkbox{checked}, aFormData);
    } else if (input->ControlType() == FormControlType::InputFile) {
      IgnoredErrorResult rv;
      sessionstore::FileList file;
      input->MozGetFileNameArray(file.valueList(), rv);
      if (rv.Failed() || file.valueList().IsEmpty()) {
        continue;
      }
      size += AppendEntry(input, id, file, aFormData);
    } else {
      TextField field;
      input->GetValue(field.value(), CallerType::System);
      auto& value = field.value();
      // In order to reduce XPath generation (which is slow), we only save data
      // for form fields that have been changed. (cf. bug 537289)
      // Also, don't want to collect credit card number.
      if (value.IsEmpty() || IsValidCCNumber(value) ||
          input->HasBeenTypePassword() ||
          input->AttrValueIs(kNameSpaceID_None, nsGkAtoms::value, value,
                             eCaseMatters)) {
        continue;
      }
      size += AppendEntry(input, id, field, aFormData);
    }
  }

  return size;
}

static uint32_t CollectSelectElement(Document* aDocument,
                                     sessionstore::FormData& aFormData) {
  uint32_t size = 0;
  RefPtr<ContentList> selectlist =
      NS_GetContentList(aDocument, kNameSpaceID_XHTML, u"select"_ns);
  uint32_t length = selectlist->Length();
  for (uint32_t i = 0; i < length; ++i) {
    MOZ_ASSERT(selectlist->Item(i), "null item in node list!");
    RefPtr<HTMLSelectElement> select =
        HTMLSelectElement::FromNodeOrNull(selectlist->Item(i));
    if (!select) {
      continue;
    }
    nsAutoString id;
    select->GetId(id);
    if (id.IsEmpty() && (aFormData.xpath().Length() > kMaxTraversedXPaths)) {
      continue;
    }
    AutocompleteInfo aInfo;
    select->GetAutocompleteInfo(aInfo);
    if (!aInfo.mCanAutomaticallyPersist) {
      continue;
    }

    if (!select->Multiple()) {
      HTMLOptionsCollection* options = select->GetOptions();
      if (!options) {
        continue;
      }

      uint32_t numOptions = options->Length();
      int32_t defaultIndex = 0;
      for (uint32_t idx = 0; idx < numOptions; idx++) {
        HTMLOptionElement* option = options->ItemAsOption(idx);
        if (option->DefaultSelected()) {
          defaultIndex = option->Index();
        }
      }

      int32_t selectedIndex = select->SelectedIndex();
      if (selectedIndex == defaultIndex || selectedIndex < 0) {
        continue;
      }

      DOMString selectVal;
      select->GetValue(selectVal);
      size += AppendEntry(select, id,
                          SingleSelect{static_cast<uint32_t>(selectedIndex),
                                       std::move(selectVal)},
                          aFormData);
    } else {
      HTMLOptionsCollection* options = select->GetOptions();
      if (!options) {
        continue;
      }
      bool hasDefaultValue = true;
      nsTArray<nsString> selectslist;
      uint32_t numOptions = options->Length();
      for (uint32_t idx = 0; idx < numOptions; idx++) {
        HTMLOptionElement* option = options->ItemAsOption(idx);
        bool selected = option->Selected();

        hasDefaultValue =
            hasDefaultValue && (selected == option->DefaultSelected());

        if (!selected) {
          continue;
        }
        option->GetValue(*selectslist.AppendElement());
      }
      // In order to reduce XPath generation (which is slow), we only save data
      // for form fields that have been changed. (cf. bug 537289)
      if (hasDefaultValue) {
        continue;
      }

      size += AppendEntry(select, id, MultipleSelect{selectslist}, aFormData);
    }
  }

  return size;
}

static already_AddRefed<ContentList> GetFormAssociatedCustomElements(
    nsINode* aRootNode) {
  MOZ_ASSERT(aRootNode, "Content list has to have a root");

  auto matchFunc = [](Element* aElement, int32_t aNamespace, nsAtom* aAtom,
                      void* aData) -> bool {
    return aElement->HasCustomElementData() &&
           aElement->GetCustomElementData()->IsFormAssociated();
  };
  RefPtr<ContentList> list =
      new ContentList(aRootNode, matchFunc, nullptr, nullptr);
  return list.forget();
}

static uint32_t CollectFormAssociatedCustomElement(
    Document* aDocument, sessionstore::FormData& aFormData) {
  uint32_t size = 0;
  RefPtr<ContentList> faceList = GetFormAssociatedCustomElements(aDocument);
  uint32_t length = faceList->Length();
  for (uint32_t i = 0; i < length; ++i) {
    MOZ_ASSERT(faceList->Item(i), "null item in node list!");
    RefPtr<Element> element = faceList->Item(i);

    nsAutoString id;
    element->GetId(id);
    if (id.IsEmpty() && (aFormData.xpath().Length() > kMaxTraversedXPaths)) {
      continue;
    }

    const auto* internals =
        element->GetCustomElementData()->GetElementInternals();
    auto formState = internals->GetFormState();
    auto formValue = internals->GetFormSubmissionValue();
    if (formState.IsNull() && formValue.IsNull()) {
      continue;
    }

    CustomElementTuple entry;
    entry.value() = nsContentUtils::ConvertToCustomElementFormValue(formValue);
    entry.state() = nsContentUtils::ConvertToCustomElementFormValue(formState);
    size += AppendEntry(element, id, entry, aFormData);
  }
  return size;
}

/* static */
uint32_t SessionStoreUtils::CollectFormData(Document* aDocument,
                                            sessionstore::FormData& aFormData) {
  MOZ_DIAGNOSTIC_ASSERT(aDocument);
  uint32_t size = 0;
  size += CollectTextAreaElement(aDocument, aFormData);
  size += CollectInputElement(aDocument, aFormData);
  size += CollectSelectElement(aDocument, aFormData);
  size += CollectFormAssociatedCustomElement(aDocument, aFormData);

  aFormData.hasData() =
      !aFormData.id().IsEmpty() || !aFormData.xpath().IsEmpty();

  return size;
}

/* static */
template <typename... ArgsT>
void SessionStoreUtils::CollectFromTextAreaElement(Document& aDocument,
                                                   uint16_t& aGeneratedCount,
                                                   ArgsT&&... args) {
  RefPtr<ContentList> textlist =
      NS_GetContentList(&aDocument, kNameSpaceID_XHTML, u"textarea"_ns);
  uint32_t length = textlist->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    MOZ_ASSERT(textlist->Item(i), "null item in node list!");

    HTMLTextAreaElement* textArea =
        HTMLTextAreaElement::FromNodeOrNull(textlist->Item(i));
    if (!textArea) {
      continue;
    }
    DOMString autocomplete;
    textArea->GetAutocomplete(autocomplete);
    if (autocomplete.EqualsLiteral("off")) {
      continue;
    }
    nsAutoString id;
    textArea->GetId(id);
    if (id.IsEmpty() && (aGeneratedCount > kMaxTraversedXPaths)) {
      continue;
    }
    nsString value;
    textArea->GetValue(value);
    // In order to reduce XPath generation (which is slow), we only save data
    // for form fields that have been changed. (cf. bug 537289)
    if (textArea->AttrValueIs(kNameSpaceID_None, nsGkAtoms::value, value,
                              eCaseMatters)) {
      continue;
    }
    AppendValueToCollectedData(textArea, id, value, aGeneratedCount, args...);
  }
}

/* static */
template <typename... ArgsT>
void SessionStoreUtils::CollectFromInputElement(Document& aDocument,
                                                uint16_t& aGeneratedCount,
                                                ArgsT&&... args) {
  RefPtr<ContentList> inputlist =
      NS_GetContentList(&aDocument, kNameSpaceID_XHTML, u"input"_ns);
  uint32_t length = inputlist->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    MOZ_ASSERT(inputlist->Item(i), "null item in node list!");
    if (const auto* formControl =
            nsIFormControl::FromNodeOrNull(inputlist->Item(i))) {
      auto controlType = formControl->ControlType();
      if (controlType == FormControlType::InputPassword ||
          controlType == FormControlType::InputHidden ||
          controlType == FormControlType::InputButton ||
          controlType == FormControlType::InputImage ||
          controlType == FormControlType::InputSubmit ||
          controlType == FormControlType::InputReset) {
        continue;
      }
    }
    RefPtr<HTMLInputElement> input =
        HTMLInputElement::FromNodeOrNull(inputlist->Item(i));
    if (!input || !nsContentUtils::IsAutocompleteEnabled(input)) {
      continue;
    }
    nsAutoString id;
    input->GetId(id);
    if (id.IsEmpty() && (aGeneratedCount > kMaxTraversedXPaths)) {
      continue;
    }
    Nullable<AutocompleteInfo> aInfo;
    input->GetAutocompleteInfo(aInfo);
    if (!aInfo.IsNull() && !aInfo.Value().mCanAutomaticallyPersist) {
      continue;
    }

    if (input->ControlType() == FormControlType::InputCheckbox ||
        input->ControlType() == FormControlType::InputRadio) {
      bool checked = input->Checked();
      if (checked == input->DefaultChecked()) {
        continue;
      }
      AppendValueToCollectedData(input, id, checked, aGeneratedCount, args...);
    } else if (input->ControlType() == FormControlType::InputFile) {
      IgnoredErrorResult rv;
      nsTArray<nsString> result;
      input->MozGetFileNameArray(result, rv);
      if (rv.Failed() || result.Length() == 0) {
        continue;
      }
      AppendValueToCollectedData(input, id, u"file"_ns, result, aGeneratedCount,
                                 args...);
    } else {
      nsString value;
      input->GetValue(value, CallerType::System);
      // In order to reduce XPath generation (which is slow), we only save data
      // for form fields that have been changed. (cf. bug 537289)
      // Also, don't want to collect credit card number.
      if (value.IsEmpty() || IsValidCCNumber(value) ||
          input->HasBeenTypePassword() ||
          input->AttrValueIs(kNameSpaceID_None, nsGkAtoms::value, value,
                             eCaseMatters)) {
        continue;
      }
      AppendValueToCollectedData(aDocument, input, id, value, aGeneratedCount,
                                 args...);
    }
  }
}

/* static */
template <typename... ArgsT>
void SessionStoreUtils::CollectFromSelectElement(Document& aDocument,
                                                 uint16_t& aGeneratedCount,
                                                 ArgsT&&... args) {
  RefPtr<ContentList> selectlist =
      NS_GetContentList(&aDocument, kNameSpaceID_XHTML, u"select"_ns);
  uint32_t length = selectlist->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    MOZ_ASSERT(selectlist->Item(i), "null item in node list!");
    RefPtr<HTMLSelectElement> select =
        HTMLSelectElement::FromNodeOrNull(selectlist->Item(i));
    if (!select) {
      continue;
    }
    nsAutoString id;
    select->GetId(id);
    if (id.IsEmpty() && (aGeneratedCount > kMaxTraversedXPaths)) {
      continue;
    }
    AutocompleteInfo aInfo;
    select->GetAutocompleteInfo(aInfo);
    if (!aInfo.mCanAutomaticallyPersist) {
      continue;
    }
    nsAutoCString value;
    if (!select->Multiple()) {
      // <select>s without the multiple attribute are hard to determine the
      // default value, so assume we don't have the default.
      CollectedNonMultipleSelectValue val;
      val.mSelectedIndex = select->SelectedIndex();
      select->GetValue(val.mValue);
      AppendValueToCollectedData(select, id, val, aGeneratedCount, args...);
    } else {
      // <select>s with the multiple attribute are easier to determine the
      // default value since each <option> has a defaultSelected property
      HTMLOptionsCollection* options = select->GetOptions();
      if (!options) {
        continue;
      }
      bool hasDefaultValue = true;
      nsTArray<nsString> selectslist;
      uint32_t numOptions = options->Length();
      for (uint32_t idx = 0; idx < numOptions; idx++) {
        HTMLOptionElement* option = options->ItemAsOption(idx);
        bool selected = option->Selected();
        if (!selected) {
          continue;
        }
        option->GetValue(*selectslist.AppendElement());
        hasDefaultValue =
            hasDefaultValue && (selected == option->DefaultSelected());
      }
      // In order to reduce XPath generation (which is slow), we only save data
      // for form fields that have been changed. (cf. bug 537289)
      if (hasDefaultValue) {
        continue;
      }

      AppendValueToCollectedData(select, id, u"multipleSelect"_ns, selectslist,
                                 aGeneratedCount, std::forward<ArgsT>(args)...);
    }
  }
}

/* static */
template <typename... ArgsT>
void SessionStoreUtils::CollectFromFormAssociatedCustomElement(
    Document& aDocument, uint16_t& aGeneratedCount, ArgsT&&... args) {
  RefPtr<ContentList> faceList = GetFormAssociatedCustomElements(&aDocument);
  uint32_t length = faceList->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    MOZ_ASSERT(faceList->Item(i), "null item in node list!");
    RefPtr<Element> element = faceList->Item(i);

    nsAutoString id;
    element->GetId(id);
    if (id.IsEmpty() && (aGeneratedCount > kMaxTraversedXPaths)) {
      continue;
    }

    auto* internals = element->GetCustomElementData()->GetElementInternals();
    const auto& state = internals->GetFormState();
    const auto& value = internals->GetFormSubmissionValue();
    if (state.IsNull() && value.IsNull()) {
      continue;
    }

    AppendValueToCollectedData(element, id, value, state, aGeneratedCount,
                               args...);
  }
}

static void CollectCurrentFormData(JSContext* aCx, Document& aDocument,
                                   Nullable<CollectedData>& aRetVal) {
  uint16_t generatedCount = 0;
  /* textarea element */
  SessionStoreUtils::CollectFromTextAreaElement(aDocument, generatedCount,
                                                aRetVal);
  /* input element */
  SessionStoreUtils::CollectFromInputElement(aDocument, generatedCount, aCx,
                                             aRetVal);
  /* select element */
  SessionStoreUtils::CollectFromSelectElement(aDocument, generatedCount, aCx,
                                              aRetVal);
  /* form-associated custom element */
  SessionStoreUtils::CollectFromFormAssociatedCustomElement(
      aDocument, generatedCount, aCx, aRetVal);

  Element* bodyElement = aDocument.GetBody();
  if (bodyElement && bodyElement->IsInDesignMode()) {
    bodyElement->GetInnerHTML(aRetVal.SetValue().mInnerHTML.Construct(),
                              IgnoreErrors());
  }

  if (aRetVal.IsNull()) {
    return;
  }

  // Store the frame's current URL with its form data so that we can compare
  // it when restoring data to not inject form data into the wrong document.
  nsIURI* uri = aDocument.GetDocumentURI();
  if (uri) {
    uri->GetSpecIgnoringRef(aRetVal.SetValue().mUrl.Construct());
  }
}

MOZ_CAN_RUN_SCRIPT
static void SetElementAsString(Element* aElement, const nsAString& aValue) {
  IgnoredErrorResult rv;
  if (auto* textArea = HTMLTextAreaElement::FromNode(aElement)) {
    // Known live because `aElement` is known live.
    MOZ_KnownLive(textArea)->SetValue(aValue, rv);
    if (!rv.Failed()) {
      nsContentUtils::DispatchInputEvent(aElement);
    }
    return;
  }
  if (auto* input = HTMLInputElement::FromNode(aElement)) {
    input->SetValue(aValue, CallerType::NonSystem, rv);
    if (!rv.Failed()) {
      nsContentUtils::DispatchInputEvent(aElement);
    }
    return;
  }
}

MOZ_CAN_RUN_SCRIPT
static void SetElementAsBool(Element* aElement, bool aValue) {
  HTMLInputElement* input = HTMLInputElement::FromNode(aElement);
  if (input) {
    bool checked = input->Checked();
    if (aValue != checked) {
      input->SetChecked(aValue);
      nsContentUtils::DispatchInputEvent(aElement);
    }
  }
}

MOZ_CAN_RUN_SCRIPT
static void SetElementAsFiles(HTMLInputElement* aElement,
                              const CollectedFileListValue& aValue) {
  IgnoredErrorResult rv;
  aElement->MozSetFileNameArray(aValue.mFileList, rv);
  if (rv.Failed()) {
    return;
  }
  nsContentUtils::DispatchInputEvent(aElement);
}

MOZ_CAN_RUN_SCRIPT
static void SetElementAsSelect(HTMLSelectElement* aElement,
                               const CollectedNonMultipleSelectValue& aValue) {
  HTMLOptionsCollection* options = aElement->GetOptions();
  if (!options) {
    return;
  }
  int32_t selectIdx = options->SelectedIndex();
  if (selectIdx >= 0) {
    nsAutoString selectOptionVal;
    options->ItemAsOption(selectIdx)->GetValue(selectOptionVal);
    if (aValue.mValue.Equals(selectOptionVal)) {
      return;
    }
  }
  uint32_t numOptions = options->Length();
  for (uint32_t idx = 0; idx < numOptions; idx++) {
    HTMLOptionElement* option = options->ItemAsOption(idx);
    nsAutoString optionValue;
    option->GetValue(optionValue);
    if (aValue.mValue.Equals(optionValue)) {
      aElement->SetSelectedIndex(idx);
      nsContentUtils::DispatchInputEvent(aElement);
    }
  }
}

MOZ_CAN_RUN_SCRIPT
static void SetElementAsMultiSelect(HTMLSelectElement* aElement,
                                    const nsTArray<nsString>& aValueArray) {
  bool fireEvent = false;
  HTMLOptionsCollection* options = aElement->GetOptions();
  if (!options) {
    return;
  }
  uint32_t numOptions = options->Length();
  for (uint32_t idx = 0; idx < numOptions; idx++) {
    HTMLOptionElement* option = options->ItemAsOption(idx);
    nsAutoString optionValue;
    option->GetValue(optionValue);
    for (uint32_t i = 0, l = aValueArray.Length(); i < l; ++i) {
      if (optionValue.Equals(aValueArray[i])) {
        option->SetSelected(true);
        if (!option->DefaultSelected()) {
          fireEvent = true;
        }
      }
    }
  }
  if (fireEvent) {
    nsContentUtils::DispatchInputEvent(aElement);
  }
}

MOZ_CAN_RUN_SCRIPT
static void SetElementAsObject(JSContext* aCx, Element* aElement,
                               JS::Handle<JS::Value> aObject) {
  RefPtr<HTMLInputElement> input = HTMLInputElement::FromNode(aElement);
  if (input) {
    if (input->ControlType() == FormControlType::InputFile) {
      CollectedFileListValue value;
      if (value.Init(aCx, aObject)) {
        SetElementAsFiles(input, value);
      } else {
        JS_ClearPendingException(aCx);
      }
    }
    return;
  }
  RefPtr<HTMLSelectElement> select = HTMLSelectElement::FromNode(aElement);
  if (select) {
    // For Single Select Element
    if (!select->Multiple()) {
      CollectedNonMultipleSelectValue value;
      if (value.Init(aCx, aObject)) {
        SetElementAsSelect(select, value);
      } else {
        JS_ClearPendingException(aCx);
      }
      return;
    }

    // For Multiple Selects Element
    bool isArray = false;
    JS::IsArrayObject(aCx, aObject, &isArray);
    if (!isArray) {
      return;
    }
    JS::Rooted<JSObject*> arrayObj(aCx, &aObject.toObject());
    uint32_t arrayLength = 0;
    if (!JS::GetArrayLength(aCx, arrayObj, &arrayLength)) {
      JS_ClearPendingException(aCx);
      return;
    }
    nsTArray<nsString> array(arrayLength);
    for (uint32_t arrayIdx = 0; arrayIdx < arrayLength; arrayIdx++) {
      JS::Rooted<JS::Value> element(aCx);
      if (!JS_GetElement(aCx, arrayObj, arrayIdx, &element)) {
        JS_ClearPendingException(aCx);
        return;
      }
      if (!element.isString()) {
        return;
      }
      nsAutoJSString value;
      if (!value.init(aCx, element)) {
        JS_ClearPendingException(aCx);
        return;
      }
      array.AppendElement(value);
    }
    SetElementAsMultiSelect(select, array);
  }

  // For Form-Associated Custom Element:
  if (!aObject.isObject()) {
    // Don't restore null values.
    return;
  }

  auto* data = aElement->GetCustomElementData();
  if (!data || !data->IsFormAssociated()) {
    return;
  }
  auto* internals = data->GetElementInternals();

  CollectedCustomElementValue value;
  if (!value.Init(aCx, aObject)) {
    JS_ClearPendingException(aCx);
    return;
  }
  internals->RestoreFormValue(std::move(value.mValue), std::move(value.mState));
}

MOZ_CAN_RUN_SCRIPT
static void SetSessionData(JSContext* aCx, Element* aElement,
                           JS::MutableHandle<JS::Value> aObject) {
  nsAutoString data;
  if (nsContentUtils::StringifyJSON(aCx, aObject, data,
                                    UndefinedIsNullStringLiteral)) {
    SetElementAsString(aElement, data);
  } else {
    JS_ClearPendingException(aCx);
  }
}

MOZ_CAN_RUN_SCRIPT static void SetInnerHTML(Document& aDocument,
                                            const nsAString& aInnerHTML) {
  RefPtr<Element> bodyElement = aDocument.GetBody();
  if (bodyElement && bodyElement->IsInDesignMode()) {
    IgnoredErrorResult rv;
    bodyElement->SetInnerHTMLTrusted(aInnerHTML, aDocument.NodePrincipal(), rv);
    if (!rv.Failed()) {
      nsContentUtils::DispatchInputEvent(bodyElement);
    }
  }
}

class FormDataParseContext : public txIParseContext {
 public:
  explicit FormDataParseContext(bool aCaseInsensitive)
      : mIsCaseInsensitive(aCaseInsensitive) {}

  int32_t resolveNamespacePrefix(nsAtom* aPrefix) override {
    if (aPrefix == nsGkAtoms::xul) {
      return kNameSpaceID_XUL;
    }

    MOZ_ASSERT(nsDependentAtomString(aPrefix).EqualsLiteral("xhtml"));
    return kNameSpaceID_XHTML;
  }

  nsresult resolveFunctionCall(nsAtom* aName, int32_t aID,
                               FunctionCall** aFunction) override {
    return NS_ERROR_XPATH_UNKNOWN_FUNCTION;
  }

  bool caseInsensitiveNameTests() override { return mIsCaseInsensitive; }

  void SetErrorOffset(uint32_t aOffset) override {}

 private:
  bool mIsCaseInsensitive;
};

static Element* FindNodeByXPath(Document& aDocument,
                                const nsAString& aExpression) {
  FormDataParseContext parsingContext(aDocument.IsHTMLDocument());
  IgnoredErrorResult rv;
  UniquePtr<XPathExpression> expression(
      aDocument.XPathEvaluator()->CreateExpression(aExpression, &parsingContext,
                                                   &aDocument, rv));
  if (rv.Failed()) {
    return nullptr;
  }
  RefPtr<XPathResult> result = expression->Evaluate(
      aDocument, XPathResult::FIRST_ORDERED_NODE_TYPE, nullptr, rv);
  if (rv.Failed()) {
    return nullptr;
  }
  return Element::FromNodeOrNull(result->GetSingleNodeValue(rv));
}

/* static */
bool SessionStoreUtils::RestoreFormData(const GlobalObject& aGlobal,
                                        Document& aDocument,
                                        const CollectedData& aData) {
  if (!aData.mUrl.WasPassed()) {
    return true;
  }

  // Don't restore any data for the given frame if the URL
  // stored in the form data doesn't match its current URL.
  nsAutoCString url;
  (void)aDocument.GetDocumentURI()->GetSpecIgnoringRef(url);
  if (!aData.mUrl.Value().Equals(url)) {
    return false;
  }

  using Change = SessionStoreChangeListener::Change;
  SessionStoreChangeListener::CollectSessionStoreData(
      aDocument.GetWindowContext(), EnumSet<Change>(Change::Input));

  if (aData.mInnerHTML.WasPassed()) {
    SetInnerHTML(aDocument, aData.mInnerHTML.Value());
  }
  if (aData.mId.WasPassed()) {
    for (auto& entry : aData.mId.Value().Entries()) {
      RefPtr<Element> node = aDocument.GetElementById(entry.mKey);
      if (node == nullptr) {
        continue;
      }
      if (entry.mValue.IsString()) {
        SetElementAsString(node, entry.mValue.GetAsString());
      } else if (entry.mValue.IsBoolean()) {
        SetElementAsBool(node, entry.mValue.GetAsBoolean());
      } else {
        // For about:{sessionrestore,welcomeback} we saved the field as JSON to
        // avoid nested instances causing humongous sessionstore.js files.
        // cf. bug 467409
        JSContext* cx = aGlobal.Context();
        if (entry.mKey.EqualsLiteral("sessionData")) {
          if (url.EqualsLiteral("about:sessionrestore") ||
              url.EqualsLiteral("about:welcomeback")) {
            JS::Rooted<JS::Value> object(
                cx, JS::ObjectValue(*entry.mValue.GetAsObject()));
            SetSessionData(cx, node, &object);
            continue;
          }
        }
        JS::Rooted<JS::Value> object(
            cx, JS::ObjectValue(*entry.mValue.GetAsObject()));
        SetElementAsObject(cx, node, object);
      }
    }
  }

  if (aData.mXpath.WasPassed()) {
    for (auto& entry : aData.mXpath.Value().Entries()) {
      RefPtr<Element> node = FindNodeByXPath(aDocument, entry.mKey);
      if (node == nullptr) {
        continue;
      }
      if (entry.mValue.IsString()) {
        SetElementAsString(node, entry.mValue.GetAsString());
      } else if (entry.mValue.IsBoolean()) {
        SetElementAsBool(node, entry.mValue.GetAsBoolean());
      } else {
        JS::Rooted<JS::Value> object(
            aGlobal.Context(), JS::ObjectValue(*entry.mValue.GetAsObject()));
        SetElementAsObject(aGlobal.Context(), node, object);
      }
    }
  }

  return true;
}

MOZ_CAN_RUN_SCRIPT
void RestoreFormEntry(Element* aNode, const FormEntryValue& aValue) {
  using Type = sessionstore::FormEntryValue::Type;
  switch (aValue.type()) {
    case Type::TCheckbox:
      SetElementAsBool(aNode, aValue.get_Checkbox().value());
      break;
    case Type::TTextField:
      SetElementAsString(aNode, aValue.get_TextField().value());
      break;
    case Type::TFileList: {
      if (RefPtr<HTMLInputElement> input = HTMLInputElement::FromNode(aNode);
          input && input->ControlType() == FormControlType::InputFile) {
        CollectedFileListValue value;
        value.mFileList = aValue.get_FileList().valueList().Clone();
        SetElementAsFiles(input, value);
      }
      break;
    }
    case Type::TSingleSelect: {
      if (RefPtr<HTMLSelectElement> select = HTMLSelectElement::FromNode(aNode);
          select && !select->Multiple()) {
        CollectedNonMultipleSelectValue value;
        value.mSelectedIndex = aValue.get_SingleSelect().index();
        value.mValue = aValue.get_SingleSelect().value();
        SetElementAsSelect(select, value);
      }
      break;
    }
    case Type::TMultipleSelect: {
      if (RefPtr<HTMLSelectElement> select = HTMLSelectElement::FromNode(aNode);
          select && select->Multiple()) {
        SetElementAsMultiSelect(select,
                                aValue.get_MultipleSelect().valueList());
      }
      break;
    }
    case Type::TCustomElementTuple: {
      const auto* data = aNode->GetCustomElementData();
      if (!data || !data->IsFormAssociated()) {
        return;
      }
      auto* internals = data->GetElementInternals();
      nsCOMPtr<nsIGlobalObject> global = aNode->GetRelevantGlobal();
      internals->RestoreFormValue(
          nsContentUtils::ExtractFormAssociatedCustomElementValue(
              global, aValue.get_CustomElementTuple().value()),
          nsContentUtils::ExtractFormAssociatedCustomElementValue(
              global, aValue.get_CustomElementTuple().state()));
      break;
    }
    default:
      MOZ_ASSERT_UNREACHABLE();
  }
}

/* static */
void SessionStoreUtils::RestoreFormData(
    Document& aDocument, const nsString& aInnerHTML,
    const nsTArray<SessionStoreRestoreData::Entry>& aEntries) {
  using Change = SessionStoreChangeListener::Change;
  SessionStoreChangeListener::CollectSessionStoreData(
      aDocument.GetWindowContext(), EnumSet<Change>(Change::Input));

  if (!aInnerHTML.IsEmpty()) {
    SetInnerHTML(aDocument, aInnerHTML);
  }

  for (const auto& entry : aEntries) {
    RefPtr<Element> node = entry.mIsXPath
                               ? FindNodeByXPath(aDocument, entry.mData.id())
                               : aDocument.GetElementById(entry.mData.id());
    if (node) {
      RestoreFormEntry(node, entry.mData.value());
    }
  }
}

typedef void (*CollectorFunc)(JSContext* aCx, Document& aDocument,
                              Nullable<CollectedData>& aRetVal);

/**
 * A function that will recursively call |CollectorFunc| to collect data for all
 * non-dynamic frames in the current frame/docShell tree.
 */
static void CollectFrameTreeData(JSContext* aCx,
                                 BrowsingContext* aBrowsingContext,
                                 Nullable<CollectedData>& aRetVal,
                                 CollectorFunc aFunc) {
  if (aBrowsingContext->CreatedDynamically()) {
    return;
  }

  nsPIDOMWindowOuter* window = aBrowsingContext->GetDOMWindow();
  if (!window || !window->GetDocShell()) {
    return;
  }

  Document* document = window->GetExtantDoc();
  if (!document) {
    return;
  }

  /* Collect data from current frame */
  aFunc(aCx, *document, aRetVal);

  /* Collect data from all child frame */
  nsTArray<JSObject*> childrenData;
  SequenceRooter<JSObject*> rooter(aCx, &childrenData);
  uint32_t trailingNullCounter = 0;

  // This is not going to work for fission. Bug 1572084 for tracking it.
  for (auto& child : aBrowsingContext->Children()) {
    NullableRootedDictionary<CollectedData> data(aCx);
    CollectFrameTreeData(aCx, child, data, aFunc);
    if (data.IsNull()) {
      childrenData.AppendElement(nullptr);
      trailingNullCounter++;
      continue;
    }
    JS::Rooted<JS::Value> jsval(aCx);
    if (!ToJSValue(aCx, data.SetValue(), &jsval)) {
      JS_ClearPendingException(aCx);
      continue;
    }
    childrenData.AppendElement(&jsval.toObject());
    trailingNullCounter = 0;
  }

  if (trailingNullCounter != childrenData.Length()) {
    childrenData.TruncateLength(childrenData.Length() - trailingNullCounter);
    aRetVal.SetValue().mChildren.Construct() = std::move(childrenData);
  }
}

/* static */ void SessionStoreUtils::CollectScrollPosition(
    const GlobalObject& aGlobal, WindowProxyHolder& aWindow,
    Nullable<CollectedData>& aRetVal) {
  CollectFrameTreeData(aGlobal.Context(), aWindow.get(), aRetVal,
                       CollectCurrentScrollPosition);
}

/* static */ void SessionStoreUtils::CollectFormData(
    const GlobalObject& aGlobal, WindowProxyHolder& aWindow,
    Nullable<CollectedData>& aRetVal) {
  CollectFrameTreeData(aGlobal.Context(), aWindow.get(), aRetVal,
                       CollectCurrentFormData);
}

/* static */ void SessionStoreUtils::ComposeInputData(
    const nsTArray<CollectedInputDataValue>& aData, InputElementData& ret) {
  nsTArray<int> selectedIndex, valueIdx;
  nsTArray<nsString> id, selectVal, strVal, type;
  nsTArray<bool> boolVal;

  for (const CollectedInputDataValue& data : aData) {
    id.AppendElement(data.id);
    type.AppendElement(data.type);

    if (data.value.is<mozilla::dom::CollectedNonMultipleSelectValue>()) {
      valueIdx.AppendElement(selectVal.Length());
      selectedIndex.AppendElement(
          data.value.as<mozilla::dom::CollectedNonMultipleSelectValue>()
              .mSelectedIndex);
      selectVal.AppendElement(
          data.value.as<mozilla::dom::CollectedNonMultipleSelectValue>()
              .mValue);
    } else if (data.value.is<CopyableTArray<nsString>>()) {
      // The first valueIdx is "index of the first string value"
      valueIdx.AppendElement(strVal.Length());
      strVal.AppendElements(data.value.as<CopyableTArray<nsString>>());
      // The second valueIdx is "index of the last string value" + 1
      id.AppendElement(data.id);
      type.AppendElement(data.type);
      valueIdx.AppendElement(strVal.Length());
    } else if (data.value.is<nsString>()) {
      valueIdx.AppendElement(strVal.Length());
      strVal.AppendElement(data.value.as<nsString>());
    } else if (data.type.EqualsLiteral("bool")) {
      valueIdx.AppendElement(boolVal.Length());
      boolVal.AppendElement(data.value.as<bool>());
    }
  }

  if (selectedIndex.Length() != 0) {
    ret.mSelectedIndex.Construct(std::move(selectedIndex));
  }
  if (valueIdx.Length() != 0) {
    ret.mValueIdx.Construct(std::move(valueIdx));
  }
  if (id.Length() != 0) {
    ret.mId.Construct(std::move(id));
  }
  if (selectVal.Length() != 0) {
    ret.mSelectVal.Construct(std::move(selectVal));
  }
  if (strVal.Length() != 0) {
    ret.mStrVal.Construct(std::move(strVal));
  }
  if (type.Length() != 0) {
    ret.mType.Construct(std::move(type));
  }
  if (boolVal.Length() != 0) {
    ret.mBoolVal.Construct(std::move(boolVal));
  }
}

already_AddRefed<nsISessionStoreRestoreData>
SessionStoreUtils::ConstructSessionStoreRestoreData(
    const GlobalObject& aGlobal) {
  nsCOMPtr<nsISessionStoreRestoreData> data = new SessionStoreRestoreData();
  return data.forget();
}

/* static */
already_AddRefed<Promise> SessionStoreUtils::InitializeRestore(
    const GlobalObject& aGlobal, CanonicalBrowsingContext& aContext,
    nsISessionStoreRestoreData* aData, ErrorResult& aError) {
  MOZ_DIAGNOSTIC_ASSERT(aContext.IsTop());

  MOZ_DIAGNOSTIC_ASSERT(aData);
  nsCOMPtr<SessionStoreRestoreData> data = do_QueryInterface(aData);
  aContext.SetRestoreData(data, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  nsCOMPtr<nsISHistory> shistory = aContext.GetSessionHistory();
  MOZ_DIAGNOSTIC_ASSERT(shistory);
  shistory->ReloadCurrentEntry();

  return aContext.GetRestorePromise();
}

/* static */
void SessionStoreUtils::RestoreDocShellState(
    nsIDocShell* aDocShell, const DocShellRestoreState& aState) {
  if (aDocShell) {
    nsCOMPtr<nsIURI> currentUri;
    nsDocShell::Cast(aDocShell)->GetCurrentURI(getter_AddRefs(currentUri));
    if (aState.URI() && (!currentUri || currentUri->SchemeIs("about"))) {
      aDocShell->SetCurrentURIForSessionStore(aState.URI());
    }
    RestoreDocShellCapabilities(aDocShell, aState.docShellCaps());
  }
}

/* static */
already_AddRefed<Promise> SessionStoreUtils::RestoreDocShellState(
    const GlobalObject& aGlobal, CanonicalBrowsingContext& aContext,
    const nsACString& aURL, const nsCString& aDocShellCaps,
    ErrorResult& aError) {
  MOZ_RELEASE_ASSERT(aContext.IsTop());

  WindowGlobalParent* wgp = aContext.GetCurrentWindowGlobal();
  if (!wgp) {
    aError.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  MOZ_DIAGNOSTIC_ASSERT(global);

  RefPtr<Promise> promise = Promise::Create(global, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  nsCOMPtr<nsIURI> uri;
  if (!aURL.IsEmpty()) {
    if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), aURL))) {
      aError.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
  }

  bool allowJavascript = true;
  for (const nsACString& token :
       nsCCharSeparatedTokenizer(aDocShellCaps, ',').ToRange()) {
    if (token.EqualsLiteral("Javascript")) {
      allowJavascript = false;
    }
  }

  (void)aContext.SetAllowJavascript(allowJavascript);

  DocShellRestoreState state = {uri, aDocShellCaps};

  // TODO (anny): Investigate removing this roundtrip.
  wgp->SendRestoreDocShellState(state)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [promise](void) { promise->MaybeResolveWithUndefined(); },
      [promise](void) { promise->MaybeRejectWithUndefined(); });

  return promise.forget();
}

/* static */
void SessionStoreUtils::RestoreSessionStorageFromParent(
    const GlobalObject& aGlobal, const CanonicalBrowsingContext& aContext,
    const Record<nsCString, Record<nsString, nsString>>& aSessionStorage) {
  nsTArray<SSCacheCopy> cacheInitList;
  for (const auto& originEntry : aSessionStorage.Entries()) {
    nsCOMPtr<nsIPrincipal> storagePrincipal =
        BasePrincipal::CreateContentPrincipal(originEntry.mKey);

    nsCString originKey;
    nsresult rv = storagePrincipal->GetStorageOriginKey(originKey);
    if (NS_FAILED(rv)) {
      continue;
    }

    SSCacheCopy& cacheInit = *cacheInitList.AppendElement();

    cacheInit.originKey() = originKey;
    PrincipalToPrincipalInfo(storagePrincipal, &cacheInit.principalInfo());

    for (const auto& entry : originEntry.mValue.Entries()) {
      SSSetItemInfo& setItemInfo = *cacheInit.data().AppendElement();
      setItemInfo.key() = entry.mKey;
      setItemInfo.value() = entry.mValue;
    }
  }

  BackgroundSessionStorageManager::LoadData(aContext.Id(), cacheInitList);
}

/* static */
nsresult SessionStoreUtils::ConstructFormDataValues(
    JSContext* aCx, const nsTArray<sessionstore::FormEntry>& aValues,
    nsTArray<Record<nsString, OwningStringOrBooleanOrObject>::EntryType>&
        aEntries,
    bool aParseSessionData) {
  using EntryType = Record<nsString, OwningStringOrBooleanOrObject>::EntryType;

  if (!aEntries.SetCapacity(aValues.Length(), fallible)) {
    return NS_ERROR_FAILURE;
  }

  for (const auto& value : aValues) {
    EntryType* entry = aEntries.AppendElement();

    using Type = sessionstore::FormEntryValue::Type;
    switch (value.value().type()) {
      case Type::TCheckbox:
        entry->mValue.SetAsBoolean() = value.value().get_Checkbox().value();
        break;
      case Type::TTextField: {
        if (aParseSessionData && value.id() == u"sessionData"_ns) {
          JS::Rooted<JS::Value> jsval(aCx);
          const auto& fieldValue = value.value().get_TextField().value();
          if (!JS_ParseJSON(aCx, fieldValue.get(), fieldValue.Length(),
                            &jsval) ||
              !jsval.isObject()) {
            return NS_ERROR_FAILURE;
          }
          entry->mValue.SetAsObject() = &jsval.toObject();
        } else {
          entry->mValue.SetAsString() = value.value().get_TextField().value();
        }
        break;
      }
      case Type::TFileList: {
        CollectedFileListValue file;
        file.mFileList = value.value().get_FileList().valueList().Clone();

        JS::Rooted<JS::Value> jsval(aCx);
        if (!ToJSValue(aCx, file, &jsval) || !jsval.isObject()) {
          return NS_ERROR_FAILURE;
        }
        entry->mValue.SetAsObject() = &jsval.toObject();
        break;
      }
      case Type::TSingleSelect: {
        CollectedNonMultipleSelectValue select;
        select.mSelectedIndex = value.value().get_SingleSelect().index();
        select.mValue = value.value().get_SingleSelect().value();

        JS::Rooted<JS::Value> jsval(aCx);
        if (!ToJSValue(aCx, select, &jsval) || !jsval.isObject()) {
          return NS_ERROR_FAILURE;
        }
        entry->mValue.SetAsObject() = &jsval.toObject();
        break;
      }
      case Type::TMultipleSelect: {
        JS::Rooted<JS::Value> jsval(aCx);
        if (!ToJSValue(aCx, value.value().get_MultipleSelect().valueList(),
                       &jsval) ||
            !jsval.isObject()) {
          return NS_ERROR_FAILURE;
        }
        entry->mValue.SetAsObject() = &jsval.toObject();
        break;
      }
      case Type::TCustomElementTuple: {
        nsCOMPtr<nsIGlobalObject> global;
        JS::Rooted<JSObject*> globalObject(aCx, JS::CurrentGlobalOrNull(aCx));
        if (NS_WARN_IF(!globalObject)) {
          break;
        }
        global = xpc::NativeGlobal(globalObject);
        if (NS_WARN_IF(!global)) {
          break;
        }

        auto formState =
            nsContentUtils::ExtractFormAssociatedCustomElementValue(
                global, value.value().get_CustomElementTuple().state());
        auto formValue =
            nsContentUtils::ExtractFormAssociatedCustomElementValue(
                global, value.value().get_CustomElementTuple().value());
        MOZ_ASSERT(!formValue.IsNull() || !formState.IsNull(),
                   "Shouldn't be storing null values!");

        CollectedCustomElementValue val;
        val.mValue = formValue;
        val.mState = formState;
        JS::Rooted<JS::Value> jsval(aCx);
        if (!ToJSValue(aCx, val, &jsval)) {
          return NS_ERROR_FAILURE;
        }
        entry->mValue.SetAsObject() = &jsval.toObject();
        break;
      }
      default:
        break;
    }

    entry->mKey = value.id();
  }

  return NS_OK;
}

static nsresult ConstructSessionStorageValue(
    const nsTArray<SSSetItemInfo>& aValues,
    Record<nsString, nsString>& aRecord) {
  auto& entries = aRecord.Entries();
  for (const auto& value : aValues) {
    auto entry = entries.AppendElement();
    entry->mKey = value.key();
    entry->mValue = value.value();
  }

  return NS_OK;
}

/* static */
nsresult SessionStoreUtils::ConstructSessionStorageValues(
    CanonicalBrowsingContext* aBrowsingContext,
    const nsTArray<SSCacheCopy>& aValues,
    Record<nsCString, Record<nsString, nsString>>& aRecord) {
  if (!aRecord.Entries().SetCapacity(aValues.Length(), fallible)) {
    return NS_ERROR_FAILURE;
  }

  for (const auto& value : aValues) {
    auto storagePrincipal = PrincipalInfoToPrincipal(value.principalInfo());
    if (storagePrincipal.isErr()) {
      continue;
    }

    auto entry = aRecord.Entries().AppendElement();

    if (!entry->mValue.Entries().SetCapacity(value.data().Length(), fallible)) {
      return NS_ERROR_FAILURE;
    }

    if (NS_FAILED(storagePrincipal.inspect()->GetOrigin(entry->mKey))) {
      return NS_ERROR_FAILURE;
    }

    ConstructSessionStorageValue(value.data(), entry->mValue);
  }

  return NS_OK;
}

/* static */
bool SessionStoreUtils::CopyProperty(JSContext* aCx, JS::Handle<JSObject*> aDst,
                                     JS::Handle<JSObject*> aSrc,
                                     const nsAString& aName) {
  JS::Rooted<JS::PropertyKey> name(aCx);
  const char16_t* data;
  size_t length = aName.GetData(&data);

  if (!JS_CharsToId(aCx, JS::TwoByteChars(data, length), &name)) {
    return false;
  }

  bool found = false;
  if (!JS_HasPropertyById(aCx, aSrc, name, &found) || !found) {
    return true;
  }

  JS::Rooted<JS::Value> value(aCx);
  if (!JS_GetPropertyById(aCx, aSrc, name, &value)) {
    return false;
  }

  if (value.isNullOrUndefined()) {
    return true;
  }

  return JS_DefinePropertyById(aCx, aDst, name, value, JSPROP_ENUMERATE);
}

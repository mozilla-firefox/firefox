/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EditorSpellCheck.h"

#include "EditorBase.h"            // for EditorBase
#include "HTMLEditor.h"            // for HTMLEditor
#include "TextServicesDocument.h"  // for TextServicesDocument

#include "mozilla/dom/Element.h"  // for Element
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/StaticRange.h"
#include "mozilla/intl/Locale.h"         // for mozilla::intl::Locale
#include "mozilla/intl/LocaleService.h"  // for retrieving app locale
#include "mozilla/intl/OSPreferences.h"  // for mozilla::intl::OSPreferences
#include "mozilla/Logging.h"             // for mozilla::LazyLogModule
#include "mozilla/mozalloc.h"            // for operator delete, etc
#include "mozilla/mozSpellChecker.h"     // for mozSpellChecker
#include "mozilla/Preferences.h"         // for Preferences

#include "nsAString.h"                // for nsAString::IsEmpty, etc
#include "nsComponentManagerUtils.h"  // for do_CreateInstance
#include "nsDebug.h"                  // for NS_ENSURE_TRUE, etc
#include "nsDependentSubstring.h"     // for Substring
#include "nsError.h"                  // for NS_ERROR_NOT_INITIALIZED, etc
#include "nsIContent.h"               // for nsIContent
#include "nsIContentPrefService2.h"   // for nsIContentPrefService2, etc
#include "mozilla/dom/Document.h"     // for Document
#include "nsIEditor.h"                // for nsIEditor
#include "nsILoadContext.h"
#include "nsISupports.h"            // for nsISupports
#include "nsISupportsUtils.h"       // for NS_ADDREF
#include "nsIURI.h"                 // for nsIURI
#include "nsThreadUtils.h"          // for GetMainThreadSerialEventTarget
#include "nsVariant.h"              // for nsIWritableVariant, etc
#include "nsLiteralString.h"        // for NS_LITERAL_STRING, etc
#include "nsReadableUtils.h"        // for ToNewUnicode, EmptyString, etc
#include "nsServiceManagerUtils.h"  // for do_GetService
#include "nsString.h"               // for nsAutoString, nsString, etc
#include "nsStringFwd.h"            // for nsAFlatString
#include "nsStyleUtil.h"            // for nsStyleUtil
#include "nsXULAppAPI.h"            // for XRE_GetProcessType
#include "nsCharSeparatedTokenizer.h"

namespace mozilla {

using namespace dom;
using intl::LocaleService;
using intl::OSPreferences;

static mozilla::LazyLogModule sEditorSpellChecker("EditorSpellChecker");

class UpdateDictionaryHolder {
 private:
  EditorSpellCheck* mSpellCheck;

 public:
  explicit UpdateDictionaryHolder(EditorSpellCheck* esc) : mSpellCheck(esc) {
    if (mSpellCheck) {
      mSpellCheck->BeginUpdateDictionary();
    }
  }

  ~UpdateDictionaryHolder() {
    if (mSpellCheck) {
      mSpellCheck->EndUpdateDictionary();
    }
  }
};

#define CPS_PREF_NAME u"spellcheck.lang"_ns

/**
 * Gets the URI of aEditor's document.
 */
static nsIURI* GetDocumentURI(EditorBase* aEditor) {
  MOZ_ASSERT(aEditor);

  Document* doc = aEditor->AsEditorBase()->GetDocument();
  if (NS_WARN_IF(!doc)) {
    return nullptr;
  }

  return doc->GetDocumentURI();
}

static nsILoadContext* GetLoadContext(nsIEditor* aEditor) {
  Document* doc = aEditor->AsEditorBase()->GetDocument();
  if (NS_WARN_IF(!doc)) {
    return nullptr;
  }

  return doc->GetLoadContext();
}

static nsCString DictionariesToString(
    const nsTArray<nsCString>& aDictionaries) {
  nsCString asString;
  for (const auto& dictionary : aDictionaries) {
    asString.Append(dictionary);
    asString.Append(',');
  }
  return asString;
}

static void StringToDictionaries(const nsCString& aString,
                                 nsTArray<nsCString>& aDictionaries) {
  nsTArray<nsCString> asDictionaries;
  for (const nsACString& token :
       nsCCharSeparatedTokenizer(aString, ',').ToRange()) {
    if (token.IsEmpty()) {
      continue;
    }
    aDictionaries.AppendElement(token);
  }
}

/**
 * Fetches the dictionary stored in content prefs and maintains state during the
 * fetch, which is asynchronous.
 */
class DictionaryFetcher final : public nsIContentPrefCallback2 {
 public:
  NS_DECL_ISUPPORTS

  DictionaryFetcher(EditorSpellCheck* aSpellCheck,
                    nsIEditorSpellCheckCallback* aCallback, uint32_t aGroup)
      : mCallback(aCallback), mGroup(aGroup), mSpellCheck(aSpellCheck) {}

  NS_IMETHOD Fetch(nsIEditor* aEditor);

  NS_IMETHOD HandleResult(nsIContentPref* aPref) override {
    nsCOMPtr<nsIVariant> value;
    nsresult rv = aPref->GetValue(getter_AddRefs(value));
    NS_ENSURE_SUCCESS(rv, rv);
    nsCString asString;
    value->GetAsACString(asString);
    StringToDictionaries(asString, mDictionaries);
    return NS_OK;
  }

  NS_IMETHOD HandleCompletion(uint16_t /*reason*/) override {
    mSpellCheck->DictionaryFetched(this);
    return NS_OK;
  }

  NS_IMETHOD HandleError(nsresult /*error*/) override { return NS_OK; }

  nsCOMPtr<nsIEditorSpellCheckCallback> mCallback;
  uint32_t mGroup;
  RefPtr<nsAtom> mRootContentLang;
  RefPtr<nsAtom> mRootDocContentLang;
  nsTArray<nsCString> mDictionaries;

 private:
  ~DictionaryFetcher() = default;

  RefPtr<EditorSpellCheck> mSpellCheck;
};

NS_IMPL_ISUPPORTS(DictionaryFetcher, nsIContentPrefCallback2)

class ContentPrefInitializerRunnable final : public Runnable {
 public:
  ContentPrefInitializerRunnable(nsIEditor* aEditor,
                                 nsIContentPrefCallback2* aCallback)
      : Runnable("ContentPrefInitializerRunnable"),
        mEditorBase(aEditor->AsEditorBase()),
        mCallback(aCallback) {}

  NS_IMETHOD Run() override {
    if (mEditorBase->Destroyed()) {
      mCallback->HandleError(NS_ERROR_NOT_AVAILABLE);
      return NS_OK;
    }

    nsCOMPtr<nsIContentPrefService2> contentPrefService =
        do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
    if (NS_WARN_IF(!contentPrefService)) {
      mCallback->HandleError(NS_ERROR_NOT_AVAILABLE);
      return NS_OK;
    }

    nsCOMPtr<nsIURI> docUri = GetDocumentURI(mEditorBase);
    if (NS_WARN_IF(!docUri)) {
      mCallback->HandleError(NS_ERROR_FAILURE);
      return NS_OK;
    }

    nsAutoCString docUriSpec;
    nsresult rv = docUri->GetSpec(docUriSpec);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mCallback->HandleError(rv);
      return NS_OK;
    }

    rv = contentPrefService->GetByDomainAndName(
        NS_ConvertUTF8toUTF16(docUriSpec), CPS_PREF_NAME,
        GetLoadContext(mEditorBase), mCallback);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mCallback->HandleError(rv);
      return NS_OK;
    }
    return NS_OK;
  }

 private:
  RefPtr<EditorBase> mEditorBase;
  nsCOMPtr<nsIContentPrefCallback2> mCallback;
};

NS_IMETHODIMP
DictionaryFetcher::Fetch(nsIEditor* aEditor) {
  NS_ENSURE_ARG_POINTER(aEditor);

  nsCOMPtr<nsIRunnable> runnable =
      new ContentPrefInitializerRunnable(aEditor, this);
  NS_DispatchToCurrentThreadQueue(runnable.forget(), 1000,
                                  EventQueuePriority::Idle);

  return NS_OK;
}

/**
 * Stores the current dictionary for aEditor's document URL.
 */
static nsresult StoreCurrentDictionaries(
    EditorBase* aEditorBase, const nsTArray<nsCString>& aDictionaries) {
  NS_ENSURE_ARG_POINTER(aEditorBase);

  nsresult rv;

  nsCOMPtr<nsIURI> docUri = GetDocumentURI(aEditorBase);
  if (NS_WARN_IF(!docUri)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString docUriSpec;
  rv = docUri->GetSpec(docUriSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr prefValue = MakeRefPtr<nsVariant>();

  nsCString asString = DictionariesToString(aDictionaries);
  prefValue->SetAsAString(NS_ConvertUTF8toUTF16(asString));

  nsCOMPtr<nsIContentPrefService2> contentPrefService =
      do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(contentPrefService, NS_ERROR_NOT_INITIALIZED);

  return contentPrefService->Set(NS_ConvertUTF8toUTF16(docUriSpec),
                                 CPS_PREF_NAME, prefValue,
                                 GetLoadContext(aEditorBase), nullptr);
}

/**
 * Forgets the current dictionary stored for aEditor's document URL.
 */
static nsresult ClearCurrentDictionaries(EditorBase* aEditorBase) {
  NS_ENSURE_ARG_POINTER(aEditorBase);

  nsresult rv;

  nsCOMPtr<nsIURI> docUri = GetDocumentURI(aEditorBase);
  if (NS_WARN_IF(!docUri)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString docUriSpec;
  rv = docUri->GetSpec(docUriSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIContentPrefService2> contentPrefService =
      do_GetService(NS_CONTENT_PREF_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(contentPrefService, NS_ERROR_NOT_INITIALIZED);

  return contentPrefService->RemoveByDomainAndName(
      NS_ConvertUTF8toUTF16(docUriSpec), CPS_PREF_NAME,
      GetLoadContext(aEditorBase), nullptr);
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(EditorSpellCheck)
NS_IMPL_CYCLE_COLLECTING_RELEASE(EditorSpellCheck)

NS_INTERFACE_MAP_BEGIN(EditorSpellCheck)
  NS_INTERFACE_MAP_ENTRY(nsIEditorSpellCheck)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIEditorSpellCheck)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(EditorSpellCheck)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(EditorSpellCheck, mEditor, mSpellChecker)

EditorSpellCheck::EditorSpellCheck()
    : mTxtSrvFilterType(0),
      mSuggestedWordIndex(0),
      mDictionaryFetcherGroup(0),
      mUpdateDictionaryRunning(false) {}

EditorSpellCheck::~EditorSpellCheck() {
  // Make sure we blow the spellchecker away, just in
  // case it hasn't been destroyed already.
  mSpellChecker = nullptr;
}

mozSpellChecker* EditorSpellCheck::GetSpellChecker() { return mSpellChecker; }

// The problem is that if the spell checker does not exist, we can not tell
// which dictionaries are installed. This function works around the problem,
// allowing callers to ask if we can spell check without actually doing so (and
// enabling or disabling UI as necessary). This just creates a spellcheck
// object if needed and asks it for the dictionary list.
NS_IMETHODIMP
EditorSpellCheck::CanSpellCheck(bool* aCanSpellCheck) {
  RefPtr<mozSpellChecker> spellChecker = mSpellChecker;
  if (!spellChecker) {
    spellChecker = mozSpellChecker::Create();
    MOZ_ASSERT(spellChecker);
  }
  nsTArray<nsCString> dictList;
  nsresult rv = spellChecker->GetDictionaryList(&dictList);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  *aCanSpellCheck = !dictList.IsEmpty();
  return NS_OK;
}

// Instances of this class can be used as either runnables or RAII helpers.
class CallbackCaller final : public Runnable {
 public:
  explicit CallbackCaller(nsIEditorSpellCheckCallback* aCallback)
      : mozilla::Runnable("CallbackCaller"), mCallback(aCallback) {}

  ~CallbackCaller() { Run(); }

  NS_IMETHOD Run() override {
    if (mCallback) {
      mCallback->EditorSpellCheckDone();
      mCallback = nullptr;
    }
    return NS_OK;
  }

 private:
  nsCOMPtr<nsIEditorSpellCheckCallback> mCallback;
};

NS_IMETHODIMP
EditorSpellCheck::InitSpellChecker(nsIEditor* aEditor,
                                   bool aEnableSelectionChecking,
                                   nsIEditorSpellCheckCallback* aCallback) {
  NS_ENSURE_TRUE(aEditor, NS_ERROR_NULL_POINTER);
  mEditor = aEditor->AsEditorBase();

  RefPtr<Document> doc = mEditor->GetDocument();
  if (NS_WARN_IF(!doc)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;

  // We can spell check with any editor type
  RefPtr textServicesDocument = MakeRefPtr<TextServicesDocument>();
  textServicesDocument->SetFilterType(mTxtSrvFilterType);

  // EditorBase::AddEditActionListener() needs to access mSpellChecker and
  // mSpellChecker->GetTextServicesDocument().  Therefore, we need to
  // initialize them before calling TextServicesDocument::InitWithEditor()
  // since it calls EditorBase::AddEditActionListener().
  mSpellChecker = mozSpellChecker::Create();
  MOZ_ASSERT(mSpellChecker);
  rv = mSpellChecker->SetDocument(textServicesDocument, true);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Pass the editor to the text services document
  rv = textServicesDocument->InitWithEditor(aEditor);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aEnableSelectionChecking) {
    // Find out if the section is collapsed or not.
    // If it isn't, we want to spellcheck just the selection.

    RefPtr<Selection> selection;
    aEditor->GetSelection(getter_AddRefs(selection));
    if (NS_WARN_IF(!selection)) {
      return NS_ERROR_FAILURE;
    }

    if (selection->RangeCount()) {
      RefPtr<const dom::Range> range = selection->GetRangeAt(0);
      NS_ENSURE_STATE(range);

      if (!range->Collapsed()) {
        // We don't want to touch the range in the selection,
        // so create a new copy of it.
        RefPtr<StaticRange> staticRange =
            StaticRange::Create(range, IgnoreErrors());
        if (NS_WARN_IF(!staticRange)) {
          return NS_ERROR_FAILURE;
        }

        // Make sure the new range spans complete words.
        rv = textServicesDocument->ExpandRangeToWordBoundaries(staticRange);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }

        // Now tell the text services that you only want
        // to iterate over the text in this range.
        rv = textServicesDocument->SetExtent(staticRange);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    }
  }
  // do not fail if UpdateCurrentDictionary fails because this method may
  // succeed later.
  rv = UpdateCurrentDictionary(aCallback);
  if (NS_FAILED(rv) && aCallback) {
    // However, if it does fail, we still need to call the callback since we
    // discard the failure.  Do it asynchronously so that the caller is always
    // guaranteed async behavior.
    RefPtr caller = MakeRefPtr<CallbackCaller>(aCallback);
    rv = doc->Dispatch(caller.forget());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP
EditorSpellCheck::GetNextMisspelledWord(nsAString& aNextMisspelledWord) {
  MOZ_LOG(sEditorSpellChecker, LogLevel::Debug, ("%s", __FUNCTION__));

  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  DeleteSuggestedWordList();
  // Beware! This may flush notifications via synchronous
  // ScrollSelectionIntoView.
  RefPtr<mozSpellChecker> spellChecker(mSpellChecker);
  return spellChecker->NextMisspelledWord(aNextMisspelledWord,
                                          mSuggestedWordList);
}

NS_IMETHODIMP
EditorSpellCheck::GetSuggestedWord(nsAString& aSuggestedWord) {
  // XXX This is buggy if mSuggestedWordList.Length() is over INT32_MAX.
  if (mSuggestedWordIndex < static_cast<int32_t>(mSuggestedWordList.Length())) {
    aSuggestedWord = mSuggestedWordList[mSuggestedWordIndex];
    mSuggestedWordIndex++;
  } else {
    // A blank string signals that there are no more strings
    aSuggestedWord.Truncate();
  }
  return NS_OK;
}

NS_IMETHODIMP
EditorSpellCheck::CheckCurrentWord(const nsAString& aSuggestedWord,
                                   bool* aIsMisspelled) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  DeleteSuggestedWordList();
  return mSpellChecker->CheckWord(aSuggestedWord, aIsMisspelled,
                                  &mSuggestedWordList);
}

NS_IMETHODIMP
EditorSpellCheck::Suggest(const nsAString& aSuggestedWord, uint32_t aCount,
                          JSContext* aCx, Promise** aPromise) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_UNEXPECTED;
  }

  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(globalObject, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  mSpellChecker->Suggest(aSuggestedWord, aCount)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [promise](const CopyableTArray<nsString>& aSuggestions) {
            promise->MaybeResolve(aSuggestions);
          },
          [promise](nsresult /*aError*/) {
            promise->MaybeReject(NS_ERROR_FAILURE);
          });

  promise.forget(aPromise);
  return NS_OK;
}

RefPtr<CheckWordPromise> EditorSpellCheck::CheckCurrentWordsNoSuggest(
    const nsTArray<nsString>& aSuggestedWords) {
  if (NS_WARN_IF(!mSpellChecker)) {
    return CheckWordPromise::CreateAndReject(NS_ERROR_NOT_INITIALIZED,
                                             __func__);
  }

  return mSpellChecker->CheckWords(aSuggestedWords);
}

NS_IMETHODIMP
EditorSpellCheck::ReplaceWord(const nsAString& aMisspelledWord,
                              const nsAString& aReplaceWord,
                              bool aAllOccurrences) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  RefPtr<mozSpellChecker> spellChecker(mSpellChecker);
  return spellChecker->Replace(aMisspelledWord, aReplaceWord, aAllOccurrences);
}

NS_IMETHODIMP
EditorSpellCheck::IgnoreWordAllOccurrences(const nsAString& aWord) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->IgnoreAll(aWord);
}

NS_IMETHODIMP
EditorSpellCheck::AddWordToDictionary(const nsAString& aWord) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->AddWordToPersonalDictionary(aWord);
}

NS_IMETHODIMP
EditorSpellCheck::RemoveWordFromDictionary(const nsAString& aWord) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->RemoveWordFromPersonalDictionary(aWord);
}

NS_IMETHODIMP
EditorSpellCheck::GetDictionaryList(nsTArray<nsCString>& aList) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  return mSpellChecker->GetDictionaryList(&aList);
}

NS_IMETHODIMP
EditorSpellCheck::GetCurrentDictionaries(nsTArray<nsCString>& aDictionaries) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);
  return mSpellChecker->GetCurrentDictionaries(aDictionaries);
}

NS_IMETHODIMP
EditorSpellCheck::SetCurrentDictionaries(
    const nsTArray<nsCString>& aDictionaries, JSContext* aCx,
    Promise** aPromise) {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  RefPtr<EditorSpellCheck> kungFuDeathGrip = this;

  // The purpose of mUpdateDictionaryRunning is to avoid doing all of this if
  // UpdateCurrentDictionary's helper method DictionaryFetched, which calls us,
  // is on the stack. In other words: Only do this, if the user manually
  // selected a dictionary to use.
  if (!mUpdateDictionaryRunning) {
    // Ignore pending dictionary fetchers by increasing this number.
    mDictionaryFetcherGroup++;

    uint32_t flags = 0;
    mEditor->GetFlags(&flags);
    if (!(flags & nsIEditor::eEditorMailMask)) {
      bool contentPrefMatchesUserPref = true;
      // Check if aDictionaries has the same languages as mPreferredLangs.
      if (!aDictionaries.IsEmpty()) {
        if (aDictionaries.Length() != mPreferredLangs.Length()) {
          contentPrefMatchesUserPref = false;
        } else {
          for (const auto& dictName : aDictionaries) {
            if (mPreferredLangs.IndexOf(dictName) ==
                nsTArray<nsCString>::NoIndex) {
              contentPrefMatchesUserPref = false;
              break;
            }
          }
        }
      }
      if (!contentPrefMatchesUserPref) {
        // When user sets dictionary manually, we store this value associated
        // with editor url, if it doesn't match the document language exactly.
        // For example on "en" sites, we need to store "en-GB", otherwise
        // the language might jump back to en-US although the user explicitly
        // chose otherwise.
        StoreCurrentDictionaries(mEditor, aDictionaries);
        MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
                ("%s, Writing content preferences for \"%s\"", __FUNCTION__,
                 DictionariesToString(aDictionaries).get()));
      } else {
        // If user sets a dictionary matching the language defined by
        // document, we consider content pref has been canceled, and we clear
        // it.
        ClearCurrentDictionaries(mEditor);
        MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
                ("%s, Clearing content preferences for \"%s\"", __FUNCTION__,
                 DictionariesToString(aDictionaries).get()));
      }

      // Also store it in as a preference, so we can use it as a fallback.
      // We don't want this for mail composer because it uses
      // "spellchecker.dictionary" as a preference.
      //
      // XXX: Prefs can only be set in the parent process, so this condition is
      // necessary to stop libpref from throwing errors. But this should
      // probably be handled in a better way.
      if (XRE_IsParentProcess()) {
        nsCString asString = DictionariesToString(aDictionaries);
        Preferences::SetCString("spellchecker.dictionary", asString);
        MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
                ("%s, Possibly storing spellchecker.dictionary \"%s\"",
                 __FUNCTION__, asString.get()));
      }
    } else {
      MOZ_ASSERT(flags & nsIEditor::eEditorMailMask);
      // Since the mail editor can only influence the language selection by the
      // html lang attribute, set the content-language document to persist
      // multi language selections.
      // XXX Why doesn't here use the document of the editor directly?
      nsCOMPtr<nsIContent> anonymousDivOrEditingHost;
      if (HTMLEditor* htmlEditor = mEditor->GetAsHTMLEditor()) {
        anonymousDivOrEditingHost = htmlEditor->ComputeEditingHost();
      } else {
        anonymousDivOrEditingHost = mEditor->GetRoot();
      }
      // If lang is available, we should respect it for spellchecker. But if not
      // available or cannot access editing host, we don't know better language
      // for content. But since this is mail composer, we will also respect pref
      // (dictionary.spellchecker), application locale and system locale, if not
      // available.
      if (anonymousDivOrEditingHost) {
        RefPtr<Document> ownerDoc = anonymousDivOrEditingHost->OwnerDoc();
        Document* parentDoc = ownerDoc->GetInProcessParentDocument();
        if (parentDoc) {
          parentDoc->SetHeaderData(
              nsGkAtoms::headerContentLanguage,
              NS_ConvertUTF8toUTF16(DictionariesToString(aDictionaries)));
        }
      }
    }
  }

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_UNEXPECTED;
  }

  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(globalObject, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  mSpellChecker->SetCurrentDictionaries(aDictionaries)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [promise]() { promise->MaybeResolveWithUndefined(); },
          [promise](nsresult /*aError*/) {
            promise->MaybeReject(NS_ERROR_FAILURE);
          });

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
EditorSpellCheck::UninitSpellChecker() {
  NS_ENSURE_TRUE(mSpellChecker, NS_ERROR_NOT_INITIALIZED);

  // Cleanup - kill the spell checker
  DeleteSuggestedWordList();
  mDictionaryFetcherGroup++;
  mSpellChecker = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
EditorSpellCheck::SetFilterType(uint32_t aFilterType) {
  mTxtSrvFilterType = aFilterType;
  return NS_OK;
}

nsresult EditorSpellCheck::DeleteSuggestedWordList() {
  mSuggestedWordList.Clear();
  mSuggestedWordIndex = 0;
  return NS_OK;
}

NS_IMETHODIMP
EditorSpellCheck::UpdateCurrentDictionary(
    nsIEditorSpellCheckCallback* aCallback) {
  if (NS_WARN_IF(!mSpellChecker)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv;

  RefPtr<EditorSpellCheck> kungFuDeathGrip = this;

  // Get language with html5 algorithm
  const RefPtr<Element> rootEditableElement =
      [](const EditorBase& aEditorBase) -> Element* {
    if (!aEditorBase.IsHTMLEditor()) {
      return aEditorBase.GetRoot();
    }
    if (aEditorBase.IsMailEditor()) {
      // Shouldn't run spellcheck in a mail editor without focus
      // (bug 1507543)
      // XXX Why doesn't here use the document of the editor directly?
      Element* const editingHost =
          aEditorBase.AsHTMLEditor()->ComputeEditingHost();
      if (!editingHost) {
        return nullptr;
      }
      // Try to get topmost document's document element for embedded mail
      // editor (bug 967494)
      Document* parentDoc =
          editingHost->OwnerDoc()->GetInProcessParentDocument();
      if (!parentDoc) {
        return editingHost;
      }
      return parentDoc->GetDocumentElement();
    }
    return aEditorBase.AsHTMLEditor()->GetFocusedElement();
  }(*mEditor);

  if (!rootEditableElement) {
    return NS_ERROR_FAILURE;
  }

  RefPtr fetcher =
      MakeRefPtr<DictionaryFetcher>(this, aCallback, mDictionaryFetcherGroup);
  fetcher->mRootContentLang = [&] {
    if (rootEditableElement->IsInNativeAnonymousSubtree()) {
      if (auto* host =
              rootEditableElement
                  ->GetClosestNativeAnonymousSubtreeRootParentOrHost()) {
        // Let text editors inherit the language of the parent content across
        // the shadow tree.
        return host->GetLang();
      }
    }
    return rootEditableElement->GetLang();
  }();
  RefPtr<Document> doc = rootEditableElement->GetComposedDoc();
  NS_ENSURE_STATE(doc);
  fetcher->mRootDocContentLang = doc->GetContentLanguage();

  rv = fetcher->Fetch(mEditor);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

// Helper function that iterates over the list of dictionaries and sets the one
// that matches based on a given comparison type.
bool EditorSpellCheck::BuildDictionaryList(const nsACString& aDictName,
                                           const nsTArray<nsCString>& aDictList,
                                           enum dictCompare aCompareType,
                                           nsTArray<nsCString>& aOutList) {
  for (const auto& dictStr : aDictList) {
    bool equals = false;
    switch (aCompareType) {
      case DICT_NORMAL_COMPARE:
        equals = aDictName.Equals(dictStr);
        break;
      case DICT_COMPARE_CASE_INSENSITIVE:
        equals = aDictName.Equals(dictStr, nsCaseInsensitiveCStringComparator);
        break;
      case DICT_COMPARE_DASHMATCH:
        equals = nsStyleUtil::DashMatchCompare(NS_ConvertUTF8toUTF16(dictStr),
                                               NS_ConvertUTF8toUTF16(aDictName),
                                               eIgnoreCase);
        break;
    }
    if (equals) {
      // Avoid adding duplicates to aOutList.
      if (aOutList.IndexOf(dictStr) == nsTArray<nsCString>::NoIndex) {
        aOutList.AppendElement(dictStr);
      }
      MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
              ("%s, \"%s\" is matched", __FUNCTION__, dictStr.get()));
      // We always break here. We tried to set the dictionary to an existing
      // dictionary from the list. This must work, if it doesn't, there is
      // no point trying another one.
      return true;
    }
  }
  return false;
}

nsresult EditorSpellCheck::DictionaryFetched(DictionaryFetcher* aFetcher) {
  MOZ_ASSERT(aFetcher);
  RefPtr<EditorSpellCheck> kungFuDeathGrip = this;

  BeginUpdateDictionary();

  if (aFetcher->mGroup < mDictionaryFetcherGroup) {
    // SetCurrentDictionary was called after the fetch started.  Don't overwrite
    // that dictionary with the fetched one.
    EndUpdateDictionary();
    if (aFetcher->mCallback) {
      aFetcher->mCallback->EditorSpellCheckDone();
    }
    return NS_OK;
  }

  /*
   * We try to derive the dictionary to use based on the following priorities:
   * 1) Content preference, so the language the user set for the site before.
   *    (Introduced in bug 678842 and corrected in bug 717433.)
   * 2) Language set by the website, or any other dictionary that partly
   *    matches that. (Introduced in bug 338427.)
   *    Eg. if the website is "en-GB", a user who only has "en-US" will get
   *    that. If the website is generic "en", the user will get one of the
   *    "en-*" installed. If application locale or system locale is "en-*",
   *    we get it. If others, it is (almost) random.
   *    However, we prefer what is stored in "spellchecker.dictionary",
   *    so if the user chose "en-AU" before, they will get "en-AU" on a plain
   *    "en" site. (Introduced in bug 682564.)
   *    If the site has multiple languages declared in its Content-Language
   *    header and there is no more specific lang tag in HTML, we try to
   *    enable a dictionary for every content language.
   * 3) The value of "spellchecker.dictionary" which reflects a previous
   *    language choice of the user (on another site).
   *    (This was the original behaviour before the aforementioned bugs
   *    landed).
   * 4) The user's locale.
   * 5) Use the current dictionary that is currently set.
   * 6) The content of the "LANG" environment variable (if set).
   * 7) The first spell check dictionary installed.
   */

  // Get the language from the element or its closest parent according to:
  // https://html.spec.whatwg.org/#attr-lang
  // This is used in SetCurrentDictionaries.
  nsCString contentLangs;
  // Reset mPreferredLangs so we only get the current state.
  mPreferredLangs.Clear();
  if (aFetcher->mRootContentLang) {
    aFetcher->mRootContentLang->ToUTF8String(contentLangs);
  }
  MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
          ("%s, mPreferredLangs (element) \"%s\"", __FUNCTION__,
           contentLangs.get()));
  if (!contentLangs.IsEmpty()) {
    mPreferredLangs.AppendElement(contentLangs);
  } else {
    // If no luck, try the "Content-Language" header.
    if (aFetcher->mRootDocContentLang) {
      aFetcher->mRootDocContentLang->ToUTF8String(contentLangs);
    }
    MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
            ("%s, mPreferredLangs (content-language) \"%s\"", __FUNCTION__,
             contentLangs.get()));
    StringToDictionaries(contentLangs, mPreferredLangs);
  }

  // We obtain a list of available dictionaries.
  AutoTArray<nsCString, 8> dictList;
  nsresult rv = mSpellChecker->GetDictionaryList(&dictList);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    EndUpdateDictionary();
    if (aFetcher->mCallback) {
      aFetcher->mCallback->EditorSpellCheckDone();
    }
    return rv;
  }

  // Priority 1:
  // If we successfully fetched a dictionary from content prefs, do not go
  // further. Use this exact dictionary.
  // Don't use content preferences for editor with eEditorMailMask flag.
  nsAutoCString dictName;
  uint32_t flags;
  mEditor->GetFlags(&flags);
  if (!(flags & nsIEditor::eEditorMailMask)) {
    if (!aFetcher->mDictionaries.IsEmpty()) {
      RefPtr<EditorSpellCheck> self = this;
      RefPtr<DictionaryFetcher> fetcher = aFetcher;
      mSpellChecker->SetCurrentDictionaries(aFetcher->mDictionaries)
          ->Then(
              GetMainThreadSerialEventTarget(), __func__,
              [self, fetcher]() {
                MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
                        ("DictionaryFetched, Assigned from content "
                         "preferences \"%s\"",
                         DictionariesToString(fetcher->mDictionaries).get()));
                // We take an early exit here, so let's not forget to clear
                // the word list.
                self->DeleteSuggestedWordList();

                self->EndUpdateDictionary();
                if (fetcher->mCallback) {
                  fetcher->mCallback->EditorSpellCheckDone();
                }
              },
              [self, fetcher](nsresult aError) {
                if (aError == NS_ERROR_ABORT) {
                  return;
                }
                // May be dictionary was uninstalled ?
                // Clear the content preference and continue.
                ClearCurrentDictionaries(self->mEditor);

                // Priority 2 or later will handled by the following
                self->SetFallbackDictionary(fetcher);
              });
      return NS_OK;
    }
  }
  SetFallbackDictionary(aFetcher);
  return NS_OK;
}

void EditorSpellCheck::SetDictionarySucceeded(DictionaryFetcher* aFetcher) {
  DeleteSuggestedWordList();
  EndUpdateDictionary();
  if (aFetcher->mCallback) {
    aFetcher->mCallback->EditorSpellCheckDone();
  }
}

void EditorSpellCheck::SetFallbackDictionary(DictionaryFetcher* aFetcher) {
  MOZ_ASSERT(mUpdateDictionaryRunning);

  AutoTArray<nsCString, 6> tryDictList;

  // We obtain a list of available dictionaries.
  AutoTArray<nsCString, 8> dictList;
  nsresult rv = mSpellChecker->GetDictionaryList(&dictList);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    EndUpdateDictionary();
    if (aFetcher->mCallback) {
      aFetcher->mCallback->EditorSpellCheckDone();
    }
    return;
  }

  // Priority 2:
  // After checking the content preferences, we use the languages of the element
  // or document.

  // Get the preference value.
  nsAutoCString prefDictionariesAsString;
  Preferences::GetLocalizedCString("spellchecker.dictionary",
                                   prefDictionariesAsString);
  nsTArray<nsCString> prefDictionaries;
  StringToDictionaries(prefDictionariesAsString, prefDictionaries);

  nsAutoCString appLocaleStr;
  // We pick one dictionary for every language that the element or document
  // indicates it contains.
  for (const auto& dictName : mPreferredLangs) {
    // RFC 5646 explicitly states that matches should be case-insensitive.
    if (BuildDictionaryList(dictName, dictList, DICT_COMPARE_CASE_INSENSITIVE,
                            tryDictList)) {
      MOZ_LOG(
          sEditorSpellChecker, LogLevel::Debug,
          ("%s, Trying \"%s\" from element/doc", __FUNCTION__, dictName.get()));
      continue;
    }

    // Required dictionary was not available. Try to get a dictionary
    // matching at least language part of dictName.
    mozilla::intl::Locale loc;
    if (mozilla::intl::LocaleParser::TryParse(dictName, loc).isOk() &&
        loc.Canonicalize().isOk()) {
      Span<const char> language = loc.Language().Span();
      nsAutoCString langCode(language.data(), language.size());

      // Try dictionary.spellchecker preference, if it starts with langCode,
      // so we don't just get any random dictionary matching the language.
      bool didAppend = false;
      for (const auto& dictionary : prefDictionaries) {
        if (nsStyleUtil::DashMatchCompare(NS_ConvertUTF8toUTF16(dictionary),
                                          NS_ConvertUTF8toUTF16(langCode),
                                          eCaseMatters)) {
          MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
                  ("%s, Trying preference value \"%s\" since it matches "
                   "language code",
                   __FUNCTION__, dictionary.get()));
          if (BuildDictionaryList(dictionary, dictList,
                                  DICT_COMPARE_CASE_INSENSITIVE, tryDictList)) {
            didAppend = true;
            break;
          }
        }
      }
      if (didAppend) {
        continue;
      }

      // Use the application locale dictionary when the required language
      // equals applocation locale language.
      LocaleService::GetInstance()->GetAppLocaleAsBCP47(appLocaleStr);
      if (!appLocaleStr.IsEmpty()) {
        mozilla::intl::Locale appLoc;
        auto result =
            mozilla::intl::LocaleParser::TryParse(appLocaleStr, appLoc);
        if (result.isOk() && appLoc.Canonicalize().isOk() &&
            loc.Language().Span() == appLoc.Language().Span()) {
          if (BuildDictionaryList(appLocaleStr, dictList,
                                  DICT_COMPARE_CASE_INSENSITIVE, tryDictList)) {
            continue;
          }
        }
      }

      // Use the system locale dictionary when the required language equlas
      // system locale language.
      nsAutoCString sysLocaleStr;
      OSPreferences::GetInstance()->GetSystemLocale(sysLocaleStr);
      if (!sysLocaleStr.IsEmpty()) {
        mozilla::intl::Locale sysLoc;
        auto result =
            mozilla::intl::LocaleParser::TryParse(sysLocaleStr, sysLoc);
        if (result.isOk() && sysLoc.Canonicalize().isOk() &&
            loc.Language().Span() == sysLoc.Language().Span()) {
          if (BuildDictionaryList(sysLocaleStr, dictList,
                                  DICT_COMPARE_CASE_INSENSITIVE, tryDictList)) {
            continue;
          }
        }
      }

      // Use any dictionary with the required language.
      MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
              ("%s, Trying to find match for language code \"%s\"",
               __FUNCTION__, langCode.get()));
      BuildDictionaryList(langCode, dictList, DICT_COMPARE_DASHMATCH,
                          tryDictList);
    }
  }

  RefPtr<EditorSpellCheck> self = this;
  RefPtr<DictionaryFetcher> fetcher = aFetcher;
  RefPtr<GenericPromise> promise;

  if (tryDictList.IsEmpty()) {
    // Proceed to priority 3 if the list of dictionaries is empty.
    promise = GenericPromise::CreateAndReject(NS_ERROR_INVALID_ARG, __func__);
  } else {
    promise = mSpellChecker->SetCurrentDictionaries(tryDictList);
  }

  // If an error was thrown while setting the dictionary, just
  // fail silently so that the spellchecker dialog is allowed to come
  // up. The user can manually reset the language to their choice on
  // the dialog if it is wrong.
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self, fetcher]() { self->SetDictionarySucceeded(fetcher); },
      [prefDictionaries = prefDictionaries.Clone(), dictList = dictList.Clone(),
       self, fetcher]() {
        // Build tryDictList with dictionaries for priorities 4 through 7.
        // We'll use this list if there is no user preference or trying
        // the user preference fails.
        AutoTArray<nsCString, 6> tryDictList;

        // Priority 4:
        // As next fallback, try the current locale.
        nsAutoCString appLocaleStr;
        LocaleService::GetInstance()->GetAppLocaleAsBCP47(appLocaleStr);
        MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
                ("SetFallbackDictionary, Trying application locale \"%s\"",
                 appLocaleStr.get()));
        self->BuildDictionaryList(appLocaleStr, dictList,
                                  DICT_COMPARE_CASE_INSENSITIVE, tryDictList);

        // Priority 5:
        // If we have a current dictionary and we don't have no item in try
        // list, don't try anything else.
        nsTArray<nsCString> currentDictionaries;
        self->GetCurrentDictionaries(currentDictionaries);
        if (!currentDictionaries.IsEmpty() && tryDictList.IsEmpty()) {
          MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
                  ("SetFallbackDictionary, Retrieved current dictionary \"%s\"",
                   DictionariesToString(currentDictionaries).get()));
          self->EndUpdateDictionary();
          if (fetcher->mCallback) {
            fetcher->mCallback->EditorSpellCheckDone();
          }
          return;
        }

        // Priority 6:
        // Try to get current dictionary from environment variable LANG.
        // LANG = language[_territory][.charset]
        char* env_lang = getenv("LANG");
        if (env_lang) {
          nsAutoCString lang(env_lang);
          // Strip trailing charset, if there is any.
          int32_t dot_pos = lang.FindChar('.');
          if (dot_pos != -1) {
            lang = Substring(lang, 0, dot_pos);
          }

          int32_t underScore = lang.FindChar('_');
          if (underScore != -1) {
            lang.Replace(underScore, 1, '-');
            MOZ_LOG(
                sEditorSpellChecker, LogLevel::Debug,
                ("SetFallbackDictionary, Trying LANG form environment \"%s\"",
                 lang.get()));
            self->BuildDictionaryList(
                lang, dictList, DICT_COMPARE_CASE_INSENSITIVE, tryDictList);
          }
        }

        // Priority 7:
        // If it does not work, pick the first one.
        if (!dictList.IsEmpty()) {
          self->BuildDictionaryList(dictList[0], dictList, DICT_NORMAL_COMPARE,
                                    tryDictList);
          MOZ_LOG(sEditorSpellChecker, LogLevel::Debug,
                  ("SetFallbackDictionary, Trying first of list \"%s\"",
                   dictList[0].get()));
        }

        // Priority 3:
        // If the document didn't supply a dictionary or the setting
        // failed, try the user preference next.
        if (!prefDictionaries.IsEmpty()) {
          self->mSpellChecker->SetCurrentDictionaries(prefDictionaries)
              ->Then(
                  GetMainThreadSerialEventTarget(), __func__,
                  [self, fetcher]() { self->SetDictionarySucceeded(fetcher); },
                  // Priority 3 failed, we'll use the list we built of
                  // priorities 4 to 7.
                  [tryDictList = tryDictList.Clone(), self, fetcher]() {
                    self->mSpellChecker
                        ->SetCurrentDictionaryFromList(tryDictList)
                        ->Then(GetMainThreadSerialEventTarget(), __func__,
                               [self, fetcher]() {
                                 self->SetDictionarySucceeded(fetcher);
                               });
                  });
        } else {
          // We don't have a user preference, so we'll try the list we
          // built of priorities 4 to 7.
          self->mSpellChecker->SetCurrentDictionaryFromList(tryDictList)
              ->Then(
                  GetMainThreadSerialEventTarget(), __func__,
                  [self, fetcher]() { self->SetDictionarySucceeded(fetcher); });
        }
      });
}

}  // namespace mozilla

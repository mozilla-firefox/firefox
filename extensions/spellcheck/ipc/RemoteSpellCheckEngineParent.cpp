/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteSpellCheckEngineParent.h"
#include "mozilla/mozSpellChecker.h"
#include "nsServiceManagerUtils.h"

namespace mozilla {

RemoteSpellcheckEngineParent::RemoteSpellcheckEngineParent() {
  mSpellChecker = mozSpellChecker::Create();
}

RemoteSpellcheckEngineParent::~RemoteSpellcheckEngineParent() = default;

mozilla::ipc::IPCResult RemoteSpellcheckEngineParent::RecvSetDictionary(
    const nsACString& aDictionary, bool* success) {
  nsresult rv = mSpellChecker->SetCurrentDictionary(aDictionary);
  *success = NS_SUCCEEDED(rv);
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteSpellcheckEngineParent::RecvSetDictionaries(
    const nsTArray<nsCString>& aDictionaries,
    SetDictionariesResolver&& aResolve) {
  mSpellChecker->SetCurrentDictionaries(aDictionaries)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [aResolve]() { aResolve(true); }, [aResolve]() { aResolve(false); });
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteSpellcheckEngineParent::RecvSetDictionaryFromList(
    nsTArray<nsCString>&& aList, SetDictionaryFromListResolver&& aResolve) {
  for (auto& dictionary : aList) {
    nsresult rv = mSpellChecker->SetCurrentDictionary(dictionary);
    if (NS_SUCCEEDED(rv)) {
      aResolve(std::tuple<const bool&, const nsACString&>(true, dictionary));
      return IPC_OK();
    }
  }
  aResolve(std::tuple<const bool&, const nsACString&>(false, ""_ns));
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteSpellcheckEngineParent::RecvCheckAsync(
    nsTArray<nsString>&& aWords, CheckAsyncResolver&& aResolve) {
  nsTArray<bool> misspells;
  misspells.SetCapacity(aWords.Length());
  for (auto& word : aWords) {
    bool misspelled;
    nsresult rv = mSpellChecker->CheckWord(word, &misspelled, nullptr);
    // If CheckWord failed, we can't tell whether the word is correctly spelled
    if (NS_FAILED(rv)) {
      misspelled = false;
    }
    misspells.AppendElement(misspelled);
  }
  aResolve(std::move(misspells));
  return IPC_OK();
}

mozilla::ipc::IPCResult RemoteSpellcheckEngineParent::RecvSuggest(
    const nsAString& aWord, uint32_t aCount, SuggestResolver&& aResolve) {
  nsTArray<nsString> suggestions;
  mSpellChecker->Suggest(aWord, aCount)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [aResolve](CopyableTArray<nsString> aSuggestions) {
            aResolve(std::move(aSuggestions));
          },
          [aResolve](nsresult /*aError*/) {
            // No suggestions due to error
            nsTArray<nsString> suggestions;
            aResolve(std::move(suggestions));
          });
  return IPC_OK();
}

void RemoteSpellcheckEngineParent::ActorDestroy(ActorDestroyReason aWhy) {}

}  // namespace mozilla

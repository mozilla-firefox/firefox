/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteSpellCheckEngineChild.h"

namespace mozilla {

RemoteSpellcheckEngineChild::RemoteSpellcheckEngineChild(
    mozSpellChecker* aOwner)
    : mOwner(aOwner) {}

RemoteSpellcheckEngineChild::~RemoteSpellcheckEngineChild() {
  // null out the owner's SpellcheckEngineChild to prevent state corruption
  // during shutdown
  if (mOwner) {
    mOwner->DeleteRemoteEngine();
  }
}

void RemoteSpellcheckEngineChild::Destroy() {
  if (mOwner) {
    mOwner->DeleteRemoteEngine();
    mOwner = nullptr;
  }
  (void)RemoteSpellcheckEngineChild::Send__delete__(this);
}

RefPtr<GenericPromise> RemoteSpellcheckEngineChild::SetCurrentDictionaries(
    const nsTArray<nsCString>& aDictionaries) {
  RefPtr<mozSpellChecker> spellChecker = mOwner;

  return SendSetDictionaries(aDictionaries)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [spellChecker, dictionaries = aDictionaries.Clone()](bool&& aParam) {
            if (aParam) {
              spellChecker->mCurrentDictionaries = dictionaries.Clone();
              return GenericPromise::CreateAndResolve(true, __func__);
            }
            spellChecker->mCurrentDictionaries.Clear();
            return GenericPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                   __func__);
          },
          [spellChecker](ResponseRejectReason&& /*aReason*/) {
            spellChecker->mCurrentDictionaries.Clear();
            return GenericPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                   __func__);
          });
}

RefPtr<GenericPromise>
RemoteSpellcheckEngineChild::SetCurrentDictionaryFromList(
    const nsTArray<nsCString>& aList) {
  RefPtr<mozSpellChecker> spellChecker = mOwner;

  return SendSetDictionaryFromList(aList)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [spellChecker](std::tuple<bool, nsCString>&& aParam) {
        if (!std::get<0>(aParam)) {
          spellChecker->mCurrentDictionaries.Clear();
          return GenericPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                 __func__);
        }
        spellChecker->mCurrentDictionaries.Clear();
        spellChecker->mCurrentDictionaries.AppendElement(
            std::move(std::get<1>(aParam)));
        return GenericPromise::CreateAndResolve(true, __func__);
      },
      [spellChecker](ResponseRejectReason&& /*aReason*/) {
        spellChecker->mCurrentDictionaries.Clear();
        return GenericPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                               __func__);
      });
}

RefPtr<CheckWordPromise> RemoteSpellcheckEngineChild::CheckWords(
    const nsTArray<nsString>& aWords) {
  RefPtr<mozSpellChecker> kungFuDeathGrip = mOwner;

  return SendCheckAsync(aWords)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [kungFuDeathGrip](nsTArray<bool>&& aIsMisspelled) {
        return CheckWordPromise::CreateAndResolve(std::move(aIsMisspelled),
                                                  __func__);
      },
      [kungFuDeathGrip](mozilla::ipc::ResponseRejectReason&& /*aReason*/) {
        return CheckWordPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                                 __func__);
      });
}

}  // namespace mozilla

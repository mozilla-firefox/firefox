/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_TextGenerator_h
#define mozilla_dom_TextGenerator_h

#include "js/TypeDecls.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/TextGeneratorBinding.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIGlobalObject.h"
#include "nsWrapperCache.h"

namespace mozilla::hwinference {
class TextGenerationParent;
}  // namespace mozilla::hwinference

namespace mozilla::dom {

class Blob;
class Promise;

// Chrome-JS wrapper over one PTextGeneration actor. Main thread only. Live
// until terminate(), or lost if the inference process goes away first; the
// two are told apart in what generate() rejects with.
class TextGenerator final : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(TextGenerator)

  static already_AddRefed<dom::Promise> Create(
      const dom::GlobalObject& aGlobal, dom::Blob& aModel,
      const dom::TextGeneratorCreateOptions& aOptions, ErrorResult& aRv);

  already_AddRefed<dom::Promise> Generate(
      const dom::TextGenerationRequest& aRequest,
      const dom::Optional<OwningNonNull<dom::TextGenerationDeltaCallback>>&
          aOnDelta,
      ErrorResult& aRv);

  void Clear(ErrorResult& aRv);
  void Cancel();
  void Terminate();

  nsIGlobalObject* GetParentObject() const { return mGlobal; }
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

 private:
  TextGenerator(nsIGlobalObject* aGlobal,
                RefPtr<hwinference::TextGenerationParent> aActor,
                const nsACString& aFeatureId);
  ~TextGenerator();

  void OnGenerateSettled();
  // The process went away under a generator that was not terminated.
  bool IsLost() const;

  nsCOMPtr<nsIGlobalObject> mGlobal;
  RefPtr<hwinference::TextGenerationParent> mActor;
  // Names the feature in profiler markers.
  const nsCString mFeatureId;
  bool mGenerateInFlight = false;
  bool mTerminated = false;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_TextGenerator_h

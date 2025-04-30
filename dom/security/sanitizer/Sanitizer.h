/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_Sanitizer_h
#define mozilla_dom_Sanitizer_h

#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/SanitizerBinding.h"
#include "mozilla/dom/SanitizerTypes.h"
#include "mozilla/dom/StaticAtomSet.h"
#include "nsString.h"
#include "nsIGlobalObject.h"
#include "nsIParserUtils.h"

// XXX(Bug 1673929) This is not really needed here, but the generated
// SanitizerBinding.cpp needs it and does not include it.
#include "mozilla/dom/Document.h"

class nsISupports;

namespace mozilla {

class ErrorResult;

namespace dom {

class GlobalObject;

class Sanitizer final : public nsISupports, public nsWrapperCache {
  explicit Sanitizer(nsIGlobalObject* aGlobal) : mGlobal(aGlobal) {
    MOZ_ASSERT(aGlobal);
  }

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Sanitizer);

  nsIGlobalObject* GetParentObject() const { return mGlobal; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<Sanitizer> GetInstance(
      nsIGlobalObject* aGlobal,
      const OwningSanitizerOrSanitizerConfigOrSanitizerPresets& aOptions,
      bool aSafe, ErrorResult& aRv);

  // WebIDL
  static already_AddRefed<Sanitizer> Constructor(
      const GlobalObject& aGlobal,
      const SanitizerConfigOrSanitizerPresets& aConfig, ErrorResult& aRv);

  void Get(SanitizerConfig& aConfig);

  template <typename SanitizerElementWithAttributes>
  void AllowElement(const SanitizerElementWithAttributes& aElement);
  template <typename SanitizerElement>
  void RemoveElement(const SanitizerElement& aElement);
  template <typename SanitizerElement>
  void ReplaceElementWithChildren(const SanitizerElement& aElement);
  template <typename SanitizerAttribute>
  void AllowAttribute(const SanitizerAttribute& aAttribute);
  template <typename SanitizerAttribute>
  void RemoveAttribute(const SanitizerAttribute& aAttribute);
  void SetComments(bool aAllow);
  void SetDataAttributes(bool aAllow);
  void RemoveUnsafe();

  /**
   * Sanitizes a node in place. This assumes that the node
   * belongs but an inert document.
   *
   * @param aNode Node to be sanitized in place
   */

  void Sanitize(nsINode* aNode, bool aSafe, ErrorResult& aRv);

 private:
  ~Sanitizer() = default;

  void SetDefaultConfig();
  void SetConfig(const SanitizerConfig& aConfig,
                 bool aAllowCommentsAndDataAttributes, ErrorResult& aRv);

  void MaybeMaterializeDefaultConfig();

  void RemoveElementCanonical(sanitizer::CanonicalName&& aElement);
  void RemoveAttributeCanonical(sanitizer::CanonicalName&& aAttribute);

  template <bool IsDefaultConfig>
  void SanitizeChildren(nsINode* aNode, bool aSafe);
  void SanitizeAttributes(Element* aChild,
                          const sanitizer::CanonicalName& aElementName,
                          bool aSafe);
  void SanitizeDefaultConfigAttributes(Element* aChild,
                                       StaticAtomSet* aElementAttributes,
                                       bool aSafe);

  /**
   * Logs localized message to either content console or browser console
   * @param aName              Localization key
   * @param aParams            Localization parameters
   * @param aFlags             Logging Flag (see nsIScriptError)
   */
  void LogLocalizedString(const char* aName, const nsTArray<nsString>& aParams,
                          uint32_t aFlags);

  /**
   * Logs localized message to either content console or browser console
   * @param aMessage           Message to log
   * @param aFlags             Logging Flag (see nsIScriptError)
   * @param aInnerWindowID     Inner Window ID (Logged on browser console if 0)
   * @param aFromPrivateWindow If from private window
   */
  static void LogMessage(const nsAString& aMessage, uint32_t aFlags,
                         uint64_t aInnerWindowID, bool aFromPrivateWindow);

  void AssertNoLists() {
    MOZ_ASSERT(mElements.IsEmpty());
    MOZ_ASSERT(mRemoveElements.IsEmpty());
    MOZ_ASSERT(mReplaceWithChildrenElements.IsEmpty());
    MOZ_ASSERT(mAttributes.IsEmpty());
    MOZ_ASSERT(mRemoveAttributes.IsEmpty());
  }

  RefPtr<nsIGlobalObject> mGlobal;

  sanitizer::ListSet<sanitizer::CanonicalElementWithAttributes> mElements;
  sanitizer::ListSet<sanitizer::CanonicalName> mRemoveElements;
  sanitizer::ListSet<sanitizer::CanonicalName> mReplaceWithChildrenElements;

  sanitizer::ListSet<sanitizer::CanonicalName> mAttributes;
  sanitizer::ListSet<sanitizer::CanonicalName> mRemoveAttributes;

  bool mComments = false;
  bool mDataAttributes = false;
  // Optimization: This sanitizer has a lazy default config. None
  // of the element lists will be used.
  bool mIsDefaultConfig = false;
};
}  // namespace dom
}  // namespace mozilla

#endif  // ifndef mozilla_dom_Sanitizer_h

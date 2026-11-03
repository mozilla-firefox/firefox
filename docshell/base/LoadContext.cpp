/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/LoadContext.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ScriptSettings.h"  // for AutoJSAPI
#include "mozilla/dom/BrowsingContext.h"
#include "nsContentUtils.h"
#include "xpcpublic.h"

namespace mozilla {

NS_IMPL_ISUPPORTS(LoadContext, nsILoadContext, nsIInterfaceRequestor)

LoadContext::LoadContext(const IPC::SerializedLoadContext& aToCopy,
                         dom::Element* aTopFrameElement,
                         OriginAttributes& aAttrs)
    : mTopFrameElement(do_GetWeakReference(aTopFrameElement)),
      mIsContent(aToCopy.mIsContent),
      mUseRemoteTabs(aToCopy.mUseRemoteTabs),
      mUseRemoteSubframes(aToCopy.mUseRemoteSubframes),
      mUseTrackingProtection(aToCopy.mUseTrackingProtection),
#ifdef DEBUG
      mIsNotNull(aToCopy.mIsNotNull),
#endif
      mOriginAttributes(aAttrs) {
}

LoadContext::LoadContext(OriginAttributes& aAttrs)
    : mTopFrameElement(nullptr),
      mIsContent(false),
      mUseRemoteTabs(false),
      mUseRemoteSubframes(false),
#ifdef DEBUG
      mIsNotNull(true),
#endif
      mOriginAttributes(aAttrs) {
  // This constructor does not have a base load context, so we need to decide
  // the mUseTrackingProtection based on prefs.
  mUseTrackingProtection =
      AntiTrackingUtils::IsTrackingProtectionEnabled(UsePrivateBrowsing());
}

LoadContext::LoadContext(nsIPrincipal* aPrincipal,
                         nsILoadContext* aOptionalBase)
    : mTopFrameElement(nullptr),
      mIsContent(true),
      mUseRemoteTabs(false),
      mUseRemoteSubframes(false),
      mUseTrackingProtection(false),
#ifdef DEBUG
      mIsNotNull(true),
#endif
      mOriginAttributes(aPrincipal->OriginAttributesRef()) {
  if (!aOptionalBase) {
    // Fallback to prefs to determine if tracking protection is enabled.
    mUseTrackingProtection =
        AntiTrackingUtils::IsTrackingProtectionEnabled(UsePrivateBrowsing());

    return;
  }

  MOZ_ALWAYS_SUCCEEDS(aOptionalBase->GetIsContent(&mIsContent));
  MOZ_ALWAYS_SUCCEEDS(aOptionalBase->GetUseRemoteTabs(&mUseRemoteTabs));
  MOZ_ALWAYS_SUCCEEDS(
      aOptionalBase->GetUseRemoteSubframes(&mUseRemoteSubframes));
  MOZ_ALWAYS_SUCCEEDS(
      aOptionalBase->GetUseTrackingProtection(&mUseTrackingProtection));
}

LoadContext::~LoadContext() = default;

//-----------------------------------------------------------------------------
// LoadContext::nsILoadContext
//-----------------------------------------------------------------------------

NS_IMETHODIMP
LoadContext::GetAssociatedWindow(mozIDOMWindowProxy**) {
  MOZ_ASSERT(mIsNotNull);

  // can't support this in the parent process
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
LoadContext::GetTopWindow(mozIDOMWindowProxy**) {
  MOZ_ASSERT(mIsNotNull);

  // can't support this in the parent process
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
LoadContext::GetTopFrameElement(dom::Element** aElement) {
  nsCOMPtr<dom::Element> element = do_QueryReferent(mTopFrameElement);
  element.forget(aElement);
  return NS_OK;
}

NS_IMETHODIMP
LoadContext::GetIsContent(bool* aIsContent) {
  MOZ_ASSERT(mIsNotNull);

  NS_ENSURE_ARG_POINTER(aIsContent);

  *aIsContent = mIsContent;
  return NS_OK;
}

NS_IMETHODIMP
LoadContext::GetUsePrivateBrowsing(bool* aUsePrivateBrowsing) {
  MOZ_ASSERT(mIsNotNull);

  NS_ENSURE_ARG_POINTER(aUsePrivateBrowsing);

  *aUsePrivateBrowsing = mOriginAttributes.IsPrivateBrowsing();
  return NS_OK;
}

NS_IMETHODIMP
LoadContext::SetUsePrivateBrowsing(bool /*aUsePrivateBrowsing*/) {
  MOZ_ASSERT(mIsNotNull);

  // We shouldn't need this on parent...
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
LoadContext::SetPrivateBrowsing(bool /*aUsePrivateBrowsing*/) {
  MOZ_ASSERT(mIsNotNull);

  // We shouldn't need this on parent...
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
LoadContext::GetUseRemoteTabs(bool* aUseRemoteTabs) {
  MOZ_ASSERT(mIsNotNull);

  NS_ENSURE_ARG_POINTER(aUseRemoteTabs);

  *aUseRemoteTabs = mUseRemoteTabs;
  return NS_OK;
}

NS_IMETHODIMP
LoadContext::SetRemoteTabs(bool /*aUseRemoteTabs*/) {
  MOZ_ASSERT(mIsNotNull);

  // We shouldn't need this on parent...
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
LoadContext::GetUseRemoteSubframes(bool* aUseRemoteSubframes) {
  MOZ_ASSERT(mIsNotNull);

  NS_ENSURE_ARG_POINTER(aUseRemoteSubframes);

  *aUseRemoteSubframes = mUseRemoteSubframes;
  return NS_OK;
}

NS_IMETHODIMP
LoadContext::SetRemoteSubframes(bool /*aUseRemoteSubframes*/) {
  MOZ_ASSERT(mIsNotNull);

  // We shouldn't need this on parent...
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
LoadContext::GetScriptableOriginAttributes(
    JSContext* aCx, JS::MutableHandle<JS::Value> aAttrs) {
  bool ok = ToJSValue(aCx, mOriginAttributes, aAttrs);
  NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);
  return NS_OK;
}

NS_IMETHODIMP_(void)
LoadContext::GetOriginAttributes(mozilla::OriginAttributes& aAttrs) {
  aAttrs = mOriginAttributes;
}

NS_IMETHODIMP
LoadContext::GetUseTrackingProtection(bool* aUseTrackingProtection) {
  MOZ_ASSERT(mIsNotNull);

  NS_ENSURE_ARG_POINTER(aUseTrackingProtection);

  *aUseTrackingProtection = mUseTrackingProtection;
  return NS_OK;
}

NS_IMETHODIMP
LoadContext::SetUseTrackingProtection(bool /*aUseTrackingProtection*/) {
  MOZ_ASSERT_UNREACHABLE("Should only be set through nsDocShell");

  return NS_ERROR_UNEXPECTED;
}

//-----------------------------------------------------------------------------
// LoadContext::nsIInterfaceRequestor
//-----------------------------------------------------------------------------
NS_IMETHODIMP
LoadContext::GetInterface(const nsIID& aIID, void** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;

  if (aIID.Equals(NS_GET_IID(nsILoadContext))) {
    *aResult = static_cast<nsILoadContext*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  return NS_NOINTERFACE;
}

static already_AddRefed<nsILoadContext> CreateInstance(bool aPrivate) {
  OriginAttributes oa;
  oa.mPrivateBrowsingId = aPrivate ? 1 : 0;

  nsCOMPtr<nsILoadContext> lc = new LoadContext(oa);

  return lc.forget();
}

already_AddRefed<nsILoadContext> CreateLoadContext() {
  return CreateInstance(false);
}

already_AddRefed<nsILoadContext> CreatePrivateLoadContext() {
  return CreateInstance(true);
}

}  // namespace mozilla

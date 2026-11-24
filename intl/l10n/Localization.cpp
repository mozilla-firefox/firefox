/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Localization.h"
#include "nsIObserverService.h"
#include "xpcpublic.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/PromiseNativeHandler.h"

#define INTL_APP_LOCALES_CHANGED "intl:app-locales-changed"
#define L10N_PSEUDO_PREF "intl.l10n.pseudo"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::intl;

static const char* kObservedPrefs[] = {L10N_PSEUDO_PREF, nullptr};

static nsTArray<ffi::L10nKey> ConvertFromL10nKeys(
    const Sequence<OwningUTF8StringOrL10nIdArgs>& aKeys) {
  nsTArray<ffi::L10nKey> l10nKeys(aKeys.Length());

  for (const auto& entry : aKeys) {
    if (entry.IsUTF8String()) {
      const auto& id = entry.GetAsUTF8String();
      ffi::L10nKey* key = l10nKeys.AppendElement();
      key->id = &id;
    } else {
      const auto& e = entry.GetAsL10nIdArgs();
      ffi::L10nKey* key = l10nKeys.AppendElement();
      key->id = &e.mId;
      if (!e.mArgs.IsNull()) {
        FluentBundle::ConvertArgs(e.mArgs.Value(), key->args);
      }
    }
  }

  return l10nKeys;
}

[[nodiscard]] static bool ConvertToAttributeNameValue(
    const nsTArray<ffi::L10nAttribute>& aAttributes,
    FallibleTArray<AttributeNameValue>& aValues) {
  if (!aValues.SetCapacity(aAttributes.Length(), fallible)) {
    return false;
  }
  for (const auto& attr : aAttributes) {
    auto* cvtAttr = aValues.AppendElement(fallible);
    MOZ_ASSERT(cvtAttr, "SetCapacity didn't set enough capacity somehow?");
    cvtAttr->mName = attr.name;
    cvtAttr->mValue = attr.value;
  }
  return true;
}

[[nodiscard]] static bool ConvertToL10nMessages(
    const nsTArray<ffi::OptionalL10nMessage>& aMessages,
    nsTArray<Nullable<L10nMessage>>& aOut) {
  if (!aOut.SetCapacity(aMessages.Length(), fallible)) {
    return false;
  }

  for (const auto& entry : aMessages) {
    Nullable<L10nMessage>* msg = aOut.AppendElement(fallible);
    MOZ_ASSERT(msg, "SetCapacity didn't set enough capacity somehow?");

    if (!entry.is_present) {
      continue;
    }

    L10nMessage& m = msg->SetValue();
    if (!entry.message.value.IsVoid()) {
      m.mValue = entry.message.value;
    }
    if (!entry.message.attributes.IsEmpty()) {
      auto& value = m.mAttributes.SetValue();
      if (!ConvertToAttributeNameValue(entry.message.attributes, value)) {
        return false;
      }
    }
  }

  return true;
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(Localization)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Localization)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Localization)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
NS_INTERFACE_MAP_END
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_WEAK(Localization, mGlobal)

/* static */
already_AddRefed<Localization> Localization::Create(
    const nsTArray<nsCString>& aResourceIds, bool aIsSync) {
  return MakeAndAddRef<Localization>(aResourceIds, aIsSync);
}

/* static */
already_AddRefed<Localization> Localization::Create(
    const nsTArray<nsCString>& aResourceIds, bool aIsSync,
    const nsTArray<nsCString>& aLocales) {
  return MakeAndAddRef<Localization>(aResourceIds, aIsSync, aLocales);
}

/* static */
already_AddRefed<Localization> Localization::Create(
    const nsTArray<ffi::GeckoResourceId>& aResourceIds, bool aIsSync) {
  return MakeAndAddRef<Localization>(aResourceIds, aIsSync);
}

Localization::Localization(const nsTArray<nsCString>& aResIds, bool aIsSync) {
  auto ffiResourceIds{L10nRegistry::ResourceIdsToFFI(aResIds)};
  ffi::localization_new(&ffiResourceIds, aIsSync, nullptr,
                        getter_AddRefs(mRaw));

  RegisterObservers();
}

Localization::Localization(const nsTArray<nsCString>& aResIds, bool aIsSync,
                           const nsTArray<nsCString>& aLocales) {
  auto ffiResourceIds{L10nRegistry::ResourceIdsToFFI(aResIds)};
  ffi::localization_new_with_locales(&ffiResourceIds, aIsSync, nullptr,
                                     &aLocales, getter_AddRefs(mRaw));
}

Localization::Localization(const nsTArray<ffi::GeckoResourceId>& aResIds,
                           bool aIsSync) {
  ffi::localization_new(&aResIds, aIsSync, nullptr, getter_AddRefs(mRaw));

  RegisterObservers();
}

Localization::Localization(nsIGlobalObject* aGlobal,
                           const nsTArray<nsCString>& aResIds, bool aIsSync)
    : mGlobal(aGlobal) {
  nsTArray<ffi::GeckoResourceId> resourceIds{
      L10nRegistry::ResourceIdsToFFI(aResIds)};
  ffi::localization_new(&resourceIds, aIsSync, nullptr, getter_AddRefs(mRaw));

  RegisterObservers();
}

Localization::Localization(nsIGlobalObject* aGlobal, bool aIsSync)
    : mGlobal(aGlobal) {
  nsTArray<ffi::GeckoResourceId> resIds;
  ffi::localization_new(&resIds, aIsSync, nullptr, getter_AddRefs(mRaw));

  RegisterObservers();
}

Localization::Localization(nsIGlobalObject* aGlobal, bool aIsSync,
                           const ffi::LocalizationRc* aRaw)
    : mGlobal(aGlobal), mRaw(aRaw) {
  RegisterObservers();
}

Localization::Localization(nsIGlobalObject* aGlobal, bool aIsSync,
                           const nsTArray<nsCString>& aLocales)
    : mGlobal(aGlobal) {
  nsTArray<ffi::GeckoResourceId> resIds;
  ffi::localization_new_with_locales(&resIds, aIsSync, nullptr, &aLocales,
                                     getter_AddRefs(mRaw));
}

/* static */
bool Localization::IsAPIEnabled(JSContext* aCx, JSObject* aObject) {
  JS::Rooted<JSObject*> obj(aCx, aObject);
  return Document::DocumentSupportsL10n(aCx, obj) ||
         IsChromeOrUAWidget(aCx, obj);
}

already_AddRefed<Localization> Localization::Constructor(
    const GlobalObject& aGlobal,
    const Sequence<OwningUTF8StringOrResourceId>& aResourceIds, bool aIsSync,
    const Optional<NonNull<L10nRegistry>>& aRegistry,
    const Optional<Sequence<nsCString>>& aLocales, ErrorResult& aRv) {
  auto ffiResourceIds{L10nRegistry::ResourceIdsToFFI(aResourceIds)};
  Maybe<nsTArray<nsCString>> locales;

  if (aLocales.WasPassed()) {
    locales.emplace();
    locales->SetCapacity(aLocales.Value().Length());
    for (const auto& locale : aLocales.Value()) {
      locales->AppendElement(locale);
    }
  }

  RefPtr<const ffi::LocalizationRc> raw;

  bool result = ffi::localization_new_with_locales(
      &ffiResourceIds, aIsSync,
      aRegistry.WasPassed() ? aRegistry.Value().Raw() : nullptr,
      locales.ptrOr(nullptr), getter_AddRefs(raw));

  if (!result) {
    aRv.ThrowInvalidStateError(
        "Failed to create the Localization. Check the locales arguments.");
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());

  return do_AddRef(new Localization(global, aIsSync, raw));
}

JSObject* Localization::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return Localization_Binding::Wrap(aCx, this, aGivenProto);
}

Localization::~Localization() = default;

NS_IMETHODIMP
Localization::Observe(nsISupports* aSubject, const char* aTopic,
                      const char16_t* aData) {
  if (!strcmp(aTopic, INTL_APP_LOCALES_CHANGED)) {
    OnChange();
  } else {
    MOZ_ASSERT(!strcmp("nsPref:changed", aTopic));
    nsDependentString pref(aData);
    if (pref.EqualsLiteral(L10N_PSEUDO_PREF)) {
      OnChange();
    }
  }

  return NS_OK;
}

void Localization::RegisterObservers() {
  DebugOnly<nsresult> rv = Preferences::AddWeakObservers(this, kObservedPrefs);
  MOZ_ASSERT(NS_SUCCEEDED(rv), "Adding observers failed.");

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->AddObserver(this, INTL_APP_LOCALES_CHANGED, true);
  }
}

void Localization::OnChange() { ffi::localization_on_change(mRaw.get()); }

void Localization::AddResourceId(const ffi::GeckoResourceId& aResourceId) {
  ffi::localization_add_res_id(mRaw.get(), &aResourceId);
}
void Localization::AddResourceId(const nsCString& aResourceId) {
  auto ffiResourceId{L10nRegistry::ResourceIdToFFI(aResourceId)};
  AddResourceId(ffiResourceId);
}
void Localization::AddResourceId(
    const dom::OwningUTF8StringOrResourceId& aResourceId) {
  auto ffiResourceId{L10nRegistry::ResourceIdToFFI(aResourceId)};
  AddResourceId(ffiResourceId);
}

uint32_t Localization::RemoveResourceId(
    const ffi::GeckoResourceId& aResourceId) {
  return ffi::localization_remove_res_id(mRaw.get(), &aResourceId);
}
uint32_t Localization::RemoveResourceId(const nsCString& aResourceId) {
  auto ffiResourceId{L10nRegistry::ResourceIdToFFI(aResourceId)};
  return RemoveResourceId(ffiResourceId);
}
uint32_t Localization::RemoveResourceId(
    const dom::OwningUTF8StringOrResourceId& aResourceId) {
  auto ffiResourceId{L10nRegistry::ResourceIdToFFI(aResourceId)};
  return RemoveResourceId(ffiResourceId);
}

void Localization::AddResourceIds(
    const nsTArray<dom::OwningUTF8StringOrResourceId>& aResourceIds) {
  auto ffiResourceIds{L10nRegistry::ResourceIdsToFFI(aResourceIds)};
  ffi::localization_add_res_ids(mRaw.get(), &ffiResourceIds);
}

uint32_t Localization::RemoveResourceIds(
    const nsTArray<dom::OwningUTF8StringOrResourceId>& aResourceIds) {
  auto ffiResourceIds{L10nRegistry::ResourceIdsToFFI(aResourceIds)};
  return ffi::localization_remove_res_ids(mRaw.get(), &ffiResourceIds);
}

static nsTArray<ffi::L10nArg> ConvertFluentArgs(
    const nsTArray<FluentArg>& aArgs) {
  nsTArray<ffi::L10nArg> l10nArgs(aArgs.Length());
  for (const FluentArg& arg : aArgs) {
    l10nArgs.AppendElement(ffi::L10nArg{
        &arg.mName, arg.mValue.match(
                        [](const nsCString& aValue) {
                          return ffi::FluentArgument::String(&aValue);
                        },
                        [](double aValue) {
                          return ffi::FluentArgument::Double_(aValue);
                        })});
  }
  return l10nArgs;
}

already_AddRefed<Promise> Localization::FormatValue(
    const nsACString& aId, const Optional<L10nArgs>& aArgs, ErrorResult& aRv) {
  nsTArray<ffi::L10nArg> l10nArgs;

  if (aArgs.WasPassed()) {
    const L10nArgs& args = aArgs.Value();
    FluentBundle::ConvertArgs(args, l10nArgs);
  }

  return FormatValueInternal(aId, l10nArgs, aRv);
}

already_AddRefed<Promise> Localization::FormatValue(
    const nsACString& aId, const nsTArray<FluentArg>& aArgs, ErrorResult& aRv) {
  return FormatValueInternal(aId, ConvertFluentArgs(aArgs), aRv);
}

already_AddRefed<Promise> Localization::FormatValueInternal(
    const nsACString& aId, const nsTArray<ffi::L10nArg>& aArgs,
    ErrorResult& aRv) {
  RefPtr<Promise> promise = Promise::Create(mGlobal, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  ffi::localization_format_value(
      mRaw.get(), &aId, &aArgs, promise,
      [](const Promise* aPromise, const nsACString* aValue,
         const nsTArray<nsCString>* aErrors) {
        Promise* promise = const_cast<Promise*>(aPromise);

        ErrorResult rv;
        if (MaybeReportErrorsToGecko(*aErrors, rv,
                                     promise->GetParentObject())) {
          promise->MaybeReject(std::move(rv));
        } else {
          promise->MaybeResolve(aValue);
        }
      });

  return MaybeWrapPromise(promise);
}

already_AddRefed<Promise> Localization::FormatValue(const nsACString& aId,
                                                    ErrorResult& aRv) {
  return FormatValue(aId, Optional<L10nArgs>(), aRv);
}

already_AddRefed<Promise> Localization::FormatValues(
    const Sequence<OwningUTF8StringOrL10nIdArgs>& aKeys, ErrorResult& aRv) {
  nsTArray<ffi::L10nKey> l10nKeys = ConvertFromL10nKeys(aKeys);

  RefPtr<Promise> promise = Promise::Create(mGlobal, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  ffi::localization_format_values(
      mRaw.get(), &l10nKeys, promise,
      // callback function which will be invoked by the rust code, passing the
      // promise back in.
      [](const Promise* aPromise, const nsTArray<nsCString>* aValues,
         const nsTArray<nsCString>* aErrors) {
        Promise* promise = const_cast<Promise*>(aPromise);

        ErrorResult rv;
        if (MaybeReportErrorsToGecko(*aErrors, rv,
                                     promise->GetParentObject())) {
          promise->MaybeReject(std::move(rv));
        } else {
          promise->MaybeResolve(*aValues);
        }
      });

  return MaybeWrapPromise(promise);
}

already_AddRefed<Promise> Localization::FormatMessages(
    const Sequence<OwningUTF8StringOrL10nIdArgs>& aKeys, ErrorResult& aRv) {
  auto l10nKeys = ConvertFromL10nKeys(aKeys);

  RefPtr<Promise> promise = Promise::Create(mGlobal, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  ffi::localization_format_messages(
      mRaw.get(), &l10nKeys, promise,
      // callback function which will be invoked by the rust code, passing the
      // promise back in.
      [](const Promise* aPromise,
         const nsTArray<ffi::OptionalL10nMessage>* aRaw,
         const nsTArray<nsCString>* aErrors) {
        Promise* promise = const_cast<Promise*>(aPromise);

        ErrorResult rv;
        if (MaybeReportErrorsToGecko(*aErrors, rv,
                                     promise->GetParentObject())) {
          promise->MaybeReject(std::move(rv));
        } else {
          nsTArray<Nullable<L10nMessage>> messages;
          if (!ConvertToL10nMessages(*aRaw, messages)) {
            promise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
          } else {
            promise->MaybeResolve(std::move(messages));
          }
        }
      });

  return MaybeWrapPromise(promise);
}

void Localization::FormatValueSync(const nsACString& aId,
                                   const Optional<L10nArgs>& aArgs,
                                   nsACString& aRetVal, ErrorResult& aRv) {
  nsTArray<ffi::L10nArg> l10nArgs;

  if (aArgs.WasPassed()) {
    const L10nArgs& args = aArgs.Value();
    FluentBundle::ConvertArgs(args, l10nArgs);
  }

  FormatValueSyncInternal(aId, l10nArgs, aRetVal, aRv);
}

void Localization::FormatValueSync(const nsACString& aId,
                                   const nsTArray<FluentArg>& aArgs,
                                   nsACString& aRetVal, ErrorResult& aRv) {
  FormatValueSyncInternal(aId, ConvertFluentArgs(aArgs), aRetVal, aRv);
}

void Localization::FormatValueSyncInternal(const nsACString& aId,
                                           const nsTArray<ffi::L10nArg>& aArgs,
                                           nsACString& aRetVal,
                                           ErrorResult& aRv) {
  nsTArray<nsCString> errors;
  bool rv = ffi::localization_format_value_sync(mRaw.get(), &aId, &aArgs,
                                                &aRetVal, &errors);

  if (rv) {
    MaybeReportErrorsToGecko(errors, aRv, GetParentObject());
  } else {
    aRv.ThrowInvalidStateError(
        "Can't use formatValueSync when state is async.");
  }
}

void Localization::FormatValueSync(const nsACString& aId, nsACString& aRetVal,
                                   ErrorResult& aRv) {
  FormatValueSync(aId, Optional<L10nArgs>(), aRetVal, aRv);
}

void Localization::FormatValuesSync(
    const Sequence<OwningUTF8StringOrL10nIdArgs>& aKeys,
    nsTArray<nsCString>& aRetVal, ErrorResult& aRv) {
  nsTArray<ffi::L10nKey> l10nKeys(aKeys.Length());
  nsTArray<nsCString> errors;

  for (const auto& entry : aKeys) {
    if (entry.IsUTF8String()) {
      const auto& id = entry.GetAsUTF8String();
      nsTArray<ffi::L10nArg> l10nArgs;
      ffi::L10nKey* key = l10nKeys.AppendElement();
      key->id = &id;
    } else {
      const auto& e = entry.GetAsL10nIdArgs();
      nsTArray<ffi::L10nArg> l10nArgs;
      ffi::L10nKey* key = l10nKeys.AppendElement();
      key->id = &e.mId;
      if (!e.mArgs.IsNull()) {
        FluentBundle::ConvertArgs(e.mArgs.Value(), key->args);
      }
    }
  }

  bool rv = ffi::localization_format_values_sync(mRaw.get(), &l10nKeys,
                                                 &aRetVal, &errors);

  if (rv) {
    MaybeReportErrorsToGecko(errors, aRv, GetParentObject());
  } else {
    aRv.ThrowInvalidStateError(
        "Can't use formatValuesSync when state is async.");
  }
}

void Localization::FormatMessagesSync(
    const Sequence<OwningUTF8StringOrL10nIdArgs>& aKeys,
    nsTArray<Nullable<L10nMessage>>& aRetVal, ErrorResult& aRv) {
  nsTArray<ffi::L10nKey> l10nKeys(aKeys.Length());
  nsTArray<nsCString> errors;

  for (const auto& entry : aKeys) {
    if (entry.IsUTF8String()) {
      const auto& id = entry.GetAsUTF8String();
      nsTArray<ffi::L10nArg> l10nArgs;
      ffi::L10nKey* key = l10nKeys.AppendElement();
      key->id = &id;
    } else {
      const auto& e = entry.GetAsL10nIdArgs();
      nsTArray<ffi::L10nArg> l10nArgs;
      ffi::L10nKey* key = l10nKeys.AppendElement();
      key->id = &e.mId;
      if (!e.mArgs.IsNull()) {
        FluentBundle::ConvertArgs(e.mArgs.Value(), key->args);
      }
    }
  }

  nsTArray<ffi::OptionalL10nMessage> result(l10nKeys.Length());

  bool rv = ffi::localization_format_messages_sync(mRaw.get(), &l10nKeys,
                                                   &result, &errors);

  if (!rv) {
    return aRv.ThrowInvalidStateError(
        "Can't use formatMessagesSync when state is async.");
  }
  MaybeReportErrorsToGecko(errors, aRv, GetParentObject());
  if (aRv.Failed()) {
    return;
  }
  if (!ConvertToL10nMessages(result, aRetVal)) {
    return aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
  }
}

void Localization::SetAsync() { ffi::localization_set_async(mRaw.get()); }
bool Localization::IsSync() { return ffi::localization_is_sync(mRaw.get()); }

/**
 * PromiseResolver is a PromiseNativeHandler used
 * by MaybeWrapPromise method.
 */
class PromiseResolver final : public PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  explicit PromiseResolver(Promise* aPromise) : mPromise(aPromise) {}
  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override;
  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override;

 protected:
  virtual ~PromiseResolver();

  RefPtr<Promise> mPromise;
};

NS_INTERFACE_MAP_BEGIN(PromiseResolver)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(PromiseResolver)
NS_IMPL_RELEASE(PromiseResolver)

void PromiseResolver::ResolvedCallback(JSContext* aCx,
                                       JS::Handle<JS::Value> aValue,
                                       ErrorResult& aRv) {
  mPromise->MaybeResolveWithClone(aCx, aValue);
}

void PromiseResolver::RejectedCallback(JSContext* aCx,
                                       JS::Handle<JS::Value> aValue,
                                       ErrorResult& aRv) {
  mPromise->MaybeRejectWithClone(aCx, aValue);
}

PromiseResolver::~PromiseResolver() { mPromise = nullptr; }

/**
 * MaybeWrapPromise is a helper method used by Localization
 * API methods to clone the value returned by a promise
 * into a new context.
 *
 * This allows for a promise from a privileged context
 * to be returned into an unprivileged document.
 *
 * This method is only used for promises that carry values.
 */
already_AddRefed<Promise> Localization::MaybeWrapPromise(
    Promise* aInnerPromise) {
  MOZ_ASSERT(aInnerPromise->State() == Promise::PromiseState::Pending);
  // For system principal we don't need to wrap the
  // result promise at all.
  nsIPrincipal* principal = mGlobal->PrincipalOrNull();
  if (principal && principal->IsSystemPrincipal()) {
    return do_AddRef(aInnerPromise);
  }

  IgnoredErrorResult result;
  RefPtr<Promise> docPromise = Promise::Create(mGlobal, result);
  if (NS_WARN_IF(result.Failed())) {
    return nullptr;
  }

  auto resolver = MakeRefPtr<PromiseResolver>(docPromise);
  aInnerPromise->AppendNativeHandler(resolver);
  return docPromise.forget();
}

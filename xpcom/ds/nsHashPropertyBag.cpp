/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHashPropertyBag.h"

#include <utility>

#include "mozilla/SimpleEnumerator.h"
#include "nsArray.h"
#include "nsArrayEnumerator.h"
#include "nsIProperty.h"
#include "nsIVariant.h"
#include "nsThreadUtils.h"
#include "nsVariant.h"

using mozilla::MakeRefPtr;
using mozilla::SimpleEnumerator;

extern "C" {

// This function uses C linkage because it's exposed to Rust to support the
// `HashPropertyBag` wrapper in the `storage_variant` crate.
void NS_NewHashPropertyBag(nsIWritablePropertyBag** aBag) {
  MakeRefPtr<nsHashPropertyBag>().forget(aBag);
}

}  // extern "C"

/*
 * nsHashPropertyBagBase implementation.
 */

NS_IMETHODIMP
nsHashPropertyBagBase::HasKey(const nsAString& aName, bool* aResult) {
  *aResult = mPropertyHash.Get(aName, nullptr);
  return NS_OK;
}

NS_IMETHODIMP
nsHashPropertyBagBase::Get(const nsAString& aName, nsIVariant** aResult) {
  if (!mPropertyHash.Get(aName, aResult)) {
    *aResult = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHashPropertyBagBase::GetProperty(const nsAString& aName,
                                   nsIVariant** aResult) {
  bool isFound = mPropertyHash.Get(aName, aResult);
  if (!isFound) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHashPropertyBagBase::SetProperty(const nsAString& aName, nsIVariant* aValue) {
  if (NS_WARN_IF(!aValue)) {
    return NS_ERROR_INVALID_ARG;
  }

  mPropertyHash.InsertOrUpdate(aName, aValue);

  return NS_OK;
}

NS_IMETHODIMP
nsHashPropertyBagBase::DeleteProperty(const nsAString& aName) {
  return mPropertyHash.Remove(aName) ? NS_OK : NS_ERROR_FAILURE;
}

//
// nsSimpleProperty class and impl; used for GetEnumerator
//

class nsSimpleProperty final : public nsIProperty {
  ~nsSimpleProperty() = default;

 public:
  nsSimpleProperty(const nsAString& aName, nsIVariant* aValue)
      : mName(aName), mValue(aValue) {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPROPERTY
 protected:
  nsString mName;
  nsCOMPtr<nsIVariant> mValue;
};

NS_IMPL_ISUPPORTS(nsSimpleProperty, nsIProperty)

NS_IMETHODIMP
nsSimpleProperty::GetName(nsAString& aName) {
  aName.Assign(mName);
  return NS_OK;
}

NS_IMETHODIMP
nsSimpleProperty::GetValue(nsIVariant** aValue) {
  NS_IF_ADDREF(*aValue = mValue);
  return NS_OK;
}

// end nsSimpleProperty

NS_IMETHODIMP
nsHashPropertyBagBase::GetEnumerator(nsISimpleEnumerator** aResult) {
  nsCOMPtr<nsIMutableArray> propertyArray = nsArray::Create();
  if (!propertyArray) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (auto iter = mPropertyHash.Iter(); !iter.Done(); iter.Next()) {
    const nsAString& key = iter.Key();
    nsIVariant* data = iter.UserData();
    nsSimpleProperty* sprop = new nsSimpleProperty(key, data);
    propertyArray->AppendElement(sprop);
  }

  return NS_NewArrayEnumerator(aResult, propertyArray, NS_GET_IID(nsIProperty));
}

#define IMPL_GETSETPROPERTY_AS(Name, Type)                          \
  NS_IMETHODIMP                                                     \
  nsHashPropertyBagBase::GetPropertyAs##Name(const nsAString& prop, \
                                             Type* _retval) {       \
    nsIVariant* v = mPropertyHash.GetWeak(prop);                    \
    if (!v) return NS_ERROR_NOT_AVAILABLE;                          \
    return v->GetAs##Name(_retval);                                 \
  }                                                                 \
                                                                    \
  NS_IMETHODIMP                                                     \
  nsHashPropertyBagBase::SetPropertyAs##Name(const nsAString& prop, \
                                             Type value) {          \
    nsCOMPtr<nsIWritableVariant> var = new nsVariant();             \
    var->SetAs##Name(value);                                        \
    return SetProperty(prop, var);                                  \
  }

IMPL_GETSETPROPERTY_AS(Int32, int32_t)
IMPL_GETSETPROPERTY_AS(Uint32, uint32_t)
IMPL_GETSETPROPERTY_AS(Int64, int64_t)
IMPL_GETSETPROPERTY_AS(Uint64, uint64_t)
IMPL_GETSETPROPERTY_AS(Double, double)
IMPL_GETSETPROPERTY_AS(Bool, bool)

NS_IMETHODIMP
nsHashPropertyBagBase::GetPropertyAsAString(const nsAString& aProp,
                                            nsAString& aResult) {
  nsIVariant* v = mPropertyHash.GetWeak(aProp);
  if (!v) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return v->GetAsAString(aResult);
}

NS_IMETHODIMP
nsHashPropertyBagBase::GetPropertyAsACString(const nsAString& aProp,
                                             nsACString& aResult) {
  nsIVariant* v = mPropertyHash.GetWeak(aProp);
  if (!v) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return v->GetAsACString(aResult);
}

NS_IMETHODIMP
nsHashPropertyBagBase::GetPropertyAsAUTF8String(const nsAString& aProp,
                                                nsACString& aResult) {
  nsIVariant* v = mPropertyHash.GetWeak(aProp);
  if (!v) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return v->GetAsAUTF8String(aResult);
}

NS_IMETHODIMP
nsHashPropertyBagBase::GetPropertyAsInterface(const nsAString& aProp,
                                              const nsIID& aIID,
                                              void** aResult) {
  nsIVariant* v = mPropertyHash.GetWeak(aProp);
  if (!v) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsCOMPtr<nsISupports> val;
  nsresult rv = v->GetAsISupports(getter_AddRefs(val));
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!val) {
    // We have a value, but it's null
    *aResult = nullptr;
    return NS_OK;
  }
  return val->QueryInterface(aIID, aResult);
}

NS_IMETHODIMP
nsHashPropertyBagBase::SetPropertyAsAString(const nsAString& aProp,
                                            const nsAString& aValue) {
  nsCOMPtr<nsIWritableVariant> var = new nsVariant();
  var->SetAsAString(aValue);
  return SetProperty(aProp, var);
}

NS_IMETHODIMP
nsHashPropertyBagBase::SetPropertyAsACString(const nsAString& aProp,
                                             const nsACString& aValue) {
  nsCOMPtr<nsIWritableVariant> var = new nsVariant();
  var->SetAsACString(aValue);
  return SetProperty(aProp, var);
}

NS_IMETHODIMP
nsHashPropertyBagBase::SetPropertyAsAUTF8String(const nsAString& aProp,
                                                const nsACString& aValue) {
  nsCOMPtr<nsIWritableVariant> var = new nsVariant();
  var->SetAsAUTF8String(aValue);
  return SetProperty(aProp, var);
}

NS_IMETHODIMP
nsHashPropertyBagBase::SetPropertyAsInterface(const nsAString& aProp,
                                              nsISupports* aValue) {
  nsCOMPtr<nsIWritableVariant> var = new nsVariant();
  var->SetAsISupports(aValue);
  return SetProperty(aProp, var);
}

void nsHashPropertyBagBase::CopyFrom(const nsHashPropertyBagBase* aOther) {
  for (const auto& entry : aOther->mPropertyHash) {
    SetProperty(entry.GetKey(), entry.GetRawPtr());
  }
}

void nsHashPropertyBagBase::CopyFrom(nsIPropertyBag* aOther) {
  CopyFrom(this, aOther);
}

/* static */ void nsHashPropertyBagBase::CopyFrom(nsIWritablePropertyBag* aTo,
                                                  nsIPropertyBag* aFrom) {
  if (aTo && aFrom) {
    nsCOMPtr<nsISimpleEnumerator> enumerator;
    if (NS_SUCCEEDED(aFrom->GetEnumerator(getter_AddRefs(enumerator)))) {
      for (auto& property : SimpleEnumerator<nsIProperty>(enumerator)) {
        nsString name;
        nsCOMPtr<nsIVariant> value;
        (void)NS_WARN_IF(NS_FAILED(property->GetName(name)));
        (void)NS_WARN_IF(NS_FAILED(property->GetValue(getter_AddRefs(value))));
        (void)NS_WARN_IF(NS_FAILED(aTo->SetProperty(std::move(name), value)));
      }
    } else {
      NS_WARNING("Unable to copy nsIPropertyBag");
    }
  }
}

nsresult nsGetProperty::operator()(const nsIID& aIID,
                                   void** aInstancePtr) const {
  nsresult rv;

  if (mPropBag) {
    rv = mPropBag->GetPropertyAsInterface(mPropName, aIID, aInstancePtr);
  } else {
    rv = NS_ERROR_NULL_POINTER;
    *aInstancePtr = nullptr;
  }

  if (mErrorPtr) {
    *mErrorPtr = rv;
  }
  return rv;
}

/*
 * nsHashPropertyBag implementation.
 */

NS_IMPL_ADDREF(nsHashPropertyBag)
NS_IMPL_RELEASE(nsHashPropertyBag)

NS_INTERFACE_MAP_BEGIN(nsHashPropertyBag)
  NS_INTERFACE_MAP_ENTRY(nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIPropertyBag, nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY(nsIPropertyBag2)
  NS_INTERFACE_MAP_ENTRY(nsIWritablePropertyBag2)
NS_INTERFACE_MAP_END

/*
 * We need to ensure that the hashtable is destroyed on the main thread, as
 * the nsIVariant values are main-thread only objects.
 */
class ProxyHashtableDestructor final : public mozilla::Runnable {
 public:
  using HashtableType = nsInterfaceHashtable<nsStringHashKey, nsIVariant>;
  explicit ProxyHashtableDestructor(HashtableType&& aTable)
      : mozilla::Runnable("ProxyHashtableDestructor"),
        mPropertyHash(std::move(aTable)) {}

  NS_IMETHODIMP
  Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    HashtableType table(std::move(mPropertyHash));
    return NS_OK;
  }

 private:
  HashtableType mPropertyHash;
};

nsHashPropertyBag::~nsHashPropertyBag() {
  if (!NS_IsMainThread()) {
    RefPtr runnable =
        MakeRefPtr<ProxyHashtableDestructor>(std::move(mPropertyHash));
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(runnable));
  }
}

/*
 * nsHashPropertyBagOMT implementation
 */
NS_IMPL_ADDREF(nsHashPropertyBagOMT)
NS_IMPL_RELEASE(nsHashPropertyBagOMT)

NS_INTERFACE_MAP_BEGIN(nsHashPropertyBagOMT)
  NS_INTERFACE_MAP_ENTRY(nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIPropertyBag, nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY(nsIPropertyBag2)
  NS_INTERFACE_MAP_ENTRY(nsIWritablePropertyBag2)
NS_INTERFACE_MAP_END

nsHashPropertyBagOMT::nsHashPropertyBagOMT() {
  // nsHashPropertyBagOMT is supposed to be used off-main thread. If you need a
  // single threaded property bag on the main thread, you should consider using
  // nsHashPropertyBagCC instead, to prevent leaks.
  MOZ_ASSERT(!NS_IsMainThread());
}

/*
 * nsHashPropertyBagCC implementation.
 */

NS_IMPL_CYCLE_COLLECTION(nsHashPropertyBagCC, mPropertyHash)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsHashPropertyBagCC)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsHashPropertyBagCC)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsHashPropertyBagCC)
  NS_INTERFACE_MAP_ENTRY(nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIPropertyBag, nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWritablePropertyBag)
  NS_INTERFACE_MAP_ENTRY(nsIPropertyBag2)
  NS_INTERFACE_MAP_ENTRY(nsIWritablePropertyBag2)
NS_INTERFACE_MAP_END

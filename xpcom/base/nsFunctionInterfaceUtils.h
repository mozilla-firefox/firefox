/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsFunctionInterfaceUtils_h
#define nsFunctionInterfaceUtils_h

#include "mozilla/RefPtr.h"
#include "nsINamed.h"
#include "nsISupports.h"
#include "nsTLiteralString.h"

class nsFunctionInterfaceBase : public nsINamed {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSINAMED

  explicit nsFunctionInterfaceBase(const nsLiteralCString& aName)
      : mName(aName) {}

 protected:
  virtual ~nsFunctionInterfaceBase() = default;

 private:
  const nsLiteralCString mName;
};

template <class F, class I>
class nsFunctionInterface : public nsFunctionInterfaceBase, public I {
 public:
  explicit nsFunctionInterface(const nsLiteralCString& aName, F&& aInvocable)
      : nsFunctionInterfaceBase(aName),
        mInvocable(std::forward<F>(aInvocable)) {}

  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) final {
    return nsFunctionInterfaceBase::AddRef();
  }
  NS_IMETHOD_(MozExternalRefCountType) Release(void) final {
    return nsFunctionInterfaceBase::Release();
  }
  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aPtr) final {
    if (aIID.Equals(NS_GET_IID(I))) {
      *aPtr = do_AddRef(static_cast<I*>(this)).take();
      return NS_OK;
    }
    return nsFunctionInterfaceBase::QueryInterface(aIID, aPtr);
  }

 protected:
  virtual ~nsFunctionInterface() = default;

  // Wrapper around std::invoke which converts `void` return types to `NS_OK`,
  // as a small QOL improvement for infallible callback implementations.
  template <class... Args>
  nsresult Invoke(Args&&... aArgs) {
    using ReturnType = std::invoke_result_t<std::decay_t<F>&, Args...>;
    if constexpr (std::is_void_v<ReturnType>) {
      std::invoke(mInvocable, std::forward<Args>(aArgs)...);
      return NS_OK;
    } else {
      return std::invoke(mInvocable, std::forward<Args>(aArgs)...);
    }
  }

  MOZ_NO_UNIQUE_ADDRESS std::decay_t<F> mInvocable;
};

#endif  // nsFunctionInterfaceUtils_h

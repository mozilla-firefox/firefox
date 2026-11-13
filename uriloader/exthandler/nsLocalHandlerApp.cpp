/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsLocalHandlerApp.h"
#include "nsIURI.h"
#include "nsIProcess.h"
#include "nsComponentManagerUtils.h"
#include "mozilla/dom/Promise.h"
#include "nsProxyRelease.h"
#ifdef MOZ_WIDGET_GTK
#  include "nsIGIOService.h"
#  include "nsServiceManagerUtils.h"
#endif

// XXX why does nsMIMEInfoImpl have a threadsafe nsISupports?  do we need one
// here too?
NS_IMPL_ISUPPORTS(nsLocalHandlerApp, nsILocalHandlerApp, nsIHandlerApp)

using namespace mozilla;

////////////////////////////////////////////////////////////////////////////////
//// nsIHandlerApp

NS_IMETHODIMP nsLocalHandlerApp::GetName(nsAString& aName) {
  if (mName.IsEmpty() && mExecutable) {
    // Don't want to cache this, just in case someone resets the app
    // without changing the description....
    mExecutable->GetLeafName(aName);
  } else {
    aName.Assign(mName);
  }

  return NS_OK;
}

/**
 * This method returns a std::function that will be executed on a thread other
 * than the main thread. To facilitate things, it should effectively be a global
 * function that does not maintain a reference to the this pointer. There should
 * be no reference to any objects that will be shared across threads. Sub-class
 * implementations should make local copies of everything they need and capture
 * those in the callback.
 */
std::function<nsresult(nsString&)>
nsLocalHandlerApp::GetPrettyNameOnNonMainThreadCallback() {
  nsString name;

  // Calculate the name now, on the main thread, so as to avoid
  // doing anything with the this pointer on the other thread
  auto result = GetName(name);

  return [name, result](nsString& aName) -> nsresult {
    aName = name;
    return result;
  };
}

NS_IMETHODIMP
nsLocalHandlerApp::PrettyNameAsync(JSContext* aCx, dom::Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aPromise);

  *aPromise = nullptr;

  if (!mExecutable) {
    return NS_ERROR_FAILURE;
  }

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult err;
  RefPtr<dom::Promise> outer = dom::Promise::Create(global, err);
  if (NS_WARN_IF(err.Failed())) {
    return err.StealNSResult();
  }

  outer.forget(aPromise);

  nsAutoString executablePath;
  nsresult result = mExecutable->GetPath(executablePath);

  if (NS_FAILED(result) || executablePath.IsEmpty()) {
    (*aPromise)->MaybeReject(result);
    return NS_OK;
  }

  nsMainThreadPtrHandle<dom::Promise> promiseHolder(
      new nsMainThreadPtrHolder<dom::Promise>(
          "nsLocalHandlerApp::prettyExecutableName Promise", *aPromise));

  auto prettyNameGetter = GetPrettyNameOnNonMainThreadCallback();

  result = NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          __func__,
          [promiseHolder /* can't move this because if the dispatch fails, we
                            call reject on the promiseHolder */
           ,
           prettyNameGetter = std::move(prettyNameGetter)]() mutable -> void {
            nsAutoString prettyExecutableName;

            nsresult result = prettyNameGetter(prettyExecutableName);

            DebugOnly<nsresult> rv =
                NS_DispatchToMainThread(NS_NewRunnableFunction(
                    __func__,
                    [promiseHolder = std::move(promiseHolder),
                     prettyExecutableName = std::move(prettyExecutableName),
                     result]() {
                      if (NS_FAILED(result)) {
                        promiseHolder.get()->MaybeReject(result);
                      } else {
                        promiseHolder.get()->MaybeResolve(prettyExecutableName);
                      }
                    }));
            NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                 "NS_DispatchToMainThread failed");
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  if (NS_FAILED(result)) {
    promiseHolder.get()->MaybeReject(result);
  }

  return NS_OK;
}

NS_IMETHODIMP nsLocalHandlerApp::SetName(const nsAString& aName) {
  mName.Assign(aName);

  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::SetDetailedDescription(const nsAString& aDescription) {
  mDetailedDescription.Assign(aDescription);

  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::GetDetailedDescription(nsAString& aDescription) {
  aDescription.Assign(mDetailedDescription);

  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::Equals(nsIHandlerApp* aHandlerApp, bool* _retval) {
  NS_ENSURE_ARG_POINTER(aHandlerApp);

  *_retval = false;

  // If the handler app isn't a local handler app, then it's not the same app.
  nsCOMPtr<nsILocalHandlerApp> localHandlerApp = do_QueryInterface(aHandlerApp);
  if (!localHandlerApp) return NS_OK;

  // If either handler app doesn't have an executable, then they aren't
  // the same app.
  nsCOMPtr<nsIFile> executable;
  nsresult rv = localHandlerApp->GetExecutable(getter_AddRefs(executable));
  if (NS_FAILED(rv)) return rv;

  // Equality for two empty nsIHandlerApp
  if (!executable && !mExecutable) {
    *_retval = true;
    return NS_OK;
  }

  // At least one is set so they are not equal
  if (!mExecutable || !executable) return NS_OK;

  // Check the command line parameter list lengths
  uint32_t len;
  localHandlerApp->GetParameterCount(&len);
  if (mParameters.Length() != len) return NS_OK;

  // Check the command line params lists
  for (uint32_t idx = 0; idx < mParameters.Length(); idx++) {
    nsAutoString param;
    if (NS_FAILED(localHandlerApp->GetParameter(idx, param)) ||
        !param.Equals(mParameters[idx]))
      return NS_OK;
  }

  return executable->Equals(mExecutable, _retval);
}

NS_IMETHODIMP
nsLocalHandlerApp::LaunchWithURI(
    nsIURI* aURI, mozilla::dom::BrowsingContext* aBrowsingContext) {
#ifdef MOZ_WIDGET_GTK
  // Launch through GIO, as handler applications from desktop entries are, so
  // that the handler receives an activation token on Wayland. When GIO cannot
  // run the executable, nsIProcess reports the error.
  nsCOMPtr<nsIGIOService> gio = do_GetService(NS_GIOSERVICE_CONTRACTID);
  if (gio && mExecutable) {
    nsAutoString name;
    GetName(name);
    nsCOMPtr<nsIGIOMimeApp> app;
    if (NS_SUCCEEDED(gio->CreateAppFromExecutable(mExecutable,
                                                  NS_ConvertUTF16toUTF8(name),
                                                  true, getter_AddRefs(app)))) {
      return app->LaunchWithURI(aURI, aBrowsingContext);
    }
  }
#endif

  // pass the entire URI to the handler.
  nsAutoCString spec;
  aURI->GetAsciiSpec(spec);
  return LaunchWithIProcess(spec);
}

nsresult nsLocalHandlerApp::LaunchWithIProcess(const nsCString& aArg) {
  nsresult rv;
  nsCOMPtr<nsIProcess> process = do_CreateInstance(NS_PROCESS_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return rv;

  if (NS_FAILED(rv = process->Init(mExecutable))) return rv;

  const char* string = aArg.get();

  return process->Run(false, &string, 1);
}

////////////////////////////////////////////////////////////////////////////////
//// nsILocalHandlerApp

NS_IMETHODIMP
nsLocalHandlerApp::GetExecutable(nsIFile** aExecutable) {
  NS_IF_ADDREF(*aExecutable = mExecutable);
  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::SetExecutable(nsIFile* aExecutable) {
  mExecutable = aExecutable;
  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::GetParameterCount(uint32_t* aParameterCount) {
  *aParameterCount = mParameters.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::ClearParameters() {
  mParameters.Clear();
  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::AppendParameter(const nsAString& aParam) {
  mParameters.AppendElement(aParam);
  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::GetParameter(uint32_t parameterIndex, nsAString& _retval) {
  if (mParameters.Length() <= parameterIndex) return NS_ERROR_INVALID_ARG;

  _retval.Assign(mParameters[parameterIndex]);
  return NS_OK;
}

NS_IMETHODIMP
nsLocalHandlerApp::ParameterExists(const nsAString& aParam, bool* _retval) {
  *_retval = mParameters.Contains(aParam);
  return NS_OK;
}

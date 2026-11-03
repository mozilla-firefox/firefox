/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Dialog services for PIP.
 */

#include "nsNSSDialogs.h"

#include "mozIDOMWindow.h"
#include "mozilla/intl/Localization.h"
#include "nsArray.h"
#include "nsComponentManagerUtils.h"
#include "nsEmbedCID.h"
#include "nsHashPropertyBag.h"
#include "nsIDialogParamBlock.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPromptService.h"
#include "nsIWindowWatcher.h"
#include "nsIX509Cert.h"
#include "nsIX509CertDB.h"
#include "nsNSSDialogHelper.h"
#include "nsPromiseFlatString.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsVariant.h"

nsNSSDialogs::nsNSSDialogs() = default;

nsNSSDialogs::~nsNSSDialogs() = default;

NS_IMPL_ISUPPORTS(nsNSSDialogs, nsICertificateDialogs)

NS_IMETHODIMP
nsNSSDialogs::ConfirmDownloadCACert(nsIInterfaceRequestor* ctx,
                                    nsIX509Cert* cert,
                                    /*out*/ uint32_t* trust,
                                    /*out*/ bool* importConfirmed) {
  // |ctx| is allowed to be null.
  NS_ENSURE_ARG(cert);
  NS_ENSURE_ARG(trust);
  NS_ENSURE_ARG(importConfirmed);

  nsCOMPtr<nsIMutableArray> argArray = nsArrayBase::Create();
  if (!argArray) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = argArray->AppendElement(cert);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIWritablePropertyBag2> retVals = new nsHashPropertyBag();
  rv = argArray->AppendElement(retVals);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Get the parent window for the dialog
  nsCOMPtr<mozIDOMWindowProxy> parent = do_GetInterface(ctx);
  rv = nsNSSDialogHelper::openDialog(
      parent, "chrome://pippki/content/downloadcert.xhtml", argArray);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = retVals->GetPropertyAsBool(u"importConfirmed"_ns, importConfirmed);
  if (NS_FAILED(rv)) {
    return rv;
  }

  *trust = nsIX509CertDB::UNTRUSTED;
  if (!*importConfirmed) {
    return NS_OK;
  }

  bool trustForSSL = false;
  rv = retVals->GetPropertyAsBool(u"trustForSSL"_ns, &trustForSSL);
  if (NS_FAILED(rv)) {
    return rv;
  }
  bool trustForEmail = false;
  rv = retVals->GetPropertyAsBool(u"trustForEmail"_ns, &trustForEmail);
  if (NS_FAILED(rv)) {
    return rv;
  }

  *trust |= trustForSSL ? nsIX509CertDB::TRUSTED_SSL : 0;
  *trust |= trustForEmail ? nsIX509CertDB::TRUSTED_EMAIL : 0;

  return NS_OK;
}

NS_IMETHODIMP
nsNSSDialogs::SetPKCS12FilePassword(nsIInterfaceRequestor* ctx,
                                    /*out*/ nsAString& password,
                                    /*out*/ bool* confirmedPassword) {
  // |ctx| is allowed to be null.
  NS_ENSURE_ARG(confirmedPassword);

  // Get the parent window for the dialog
  nsCOMPtr<mozIDOMWindowProxy> parent = do_GetInterface(ctx);
  nsCOMPtr<nsIWritablePropertyBag2> retVals = new nsHashPropertyBag();
  nsresult rv = nsNSSDialogHelper::openDialog(
      parent, "chrome://pippki/content/setp12password.xhtml", retVals);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = retVals->GetPropertyAsBool(u"confirmedPassword"_ns, confirmedPassword);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (!*confirmedPassword) {
    return NS_OK;
  }

  return retVals->GetPropertyAsAString(u"password"_ns, password);
}

NS_IMETHODIMP
nsNSSDialogs::GetPKCS12FilePassword(nsIInterfaceRequestor* ctx,
                                    nsAString& _password, bool* _retval) {
  *_retval = false;

  nsCOMPtr<nsIPromptService> promptSvc(
      do_GetService(NS_PROMPTSERVICE_CONTRACTID));
  if (!promptSvc) {
    return NS_ERROR_FAILURE;
  }

  nsTArray<nsCString> resIds = {"security/pippki/pippki.ftl"_ns};
  RefPtr<mozilla::intl::Localization> l10n =
      mozilla::intl::Localization::Create(resIds, true);
  nsAutoCString msg;
  mozilla::ErrorResult error;
  l10n->FormatValueSync("pippki-get-pkcs12-password-message"_ns, {}, msg,
                        error);
  if (error.Failed()) {
    return error.StealNSResult();
  }

  // Get the parent window for the dialog
  nsCOMPtr<mozIDOMWindowProxy> parent = do_GetInterface(ctx);
  char16_t* pwTemp = nullptr;
  nsresult rv = promptSvc->PromptPassword(
      parent, nullptr, NS_ConvertUTF8toUTF16(msg).get(), &pwTemp, _retval);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (*_retval) {
    _password.Assign(pwTemp);
    free(pwTemp);
  }

  return NS_OK;
}

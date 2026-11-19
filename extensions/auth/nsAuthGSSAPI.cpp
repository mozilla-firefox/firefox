/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// GSSAPI Authentication Support Module
//
// Described by IETF Internet draft: draft-brezak-kerberos-http-00.txt
// (formerly draft-brezak-spnego-http-04.txt)
//
// Also described here:
// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dnsecure/html/http-sso-1.asp
//
//

#include "mozilla/Atomics.h"
#include "mozilla/IntegerPrintfMacros.h"

#include "nsCOMPtr.h"
#include "nsNativeCharsetUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/SharedLibrary.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/SecurityManagerSslMetrics.h"

#include "nsAuthGSSAPI.h"

#ifdef XP_MACOSX
#  include <Kerberos/Kerberos.h>
#endif

#ifdef XP_MACOSX
typedef KLStatus (*KLCacheHasValidTickets_type)(KLPrincipal, KLKerberosVersion,
                                                KLBoolean*, KLPrincipal*,
                                                char**);
#endif

#if defined(HAVE_RES_NINIT)
#  include <sys/types.h>
#  include <netinet/in.h>
#  include <arpa/nameser.h>
#  include <resolv.h>
#endif

using namespace mozilla;

//-----------------------------------------------------------------------------

// We define GSS_C_NT_HOSTBASED_SERVICE explicitly since it may be referenced
// by by a different name depending on the implementation of gss but always
// has the same value

static gss_OID_desc gss_c_nt_hostbased_service = {
    10, (void*)"\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x04"};

static const char kNegotiateAuthGssLib[] = "network.negotiate-auth.gsslib";

static struct GSSFunction {
  const char* str;
  PRFuncPtr func;
} gssFuncs[] = {{"gss_display_status", nullptr},
                {"gss_init_sec_context", nullptr},
                {"gss_indicate_mechs", nullptr},
                {"gss_release_oid_set", nullptr},
                {"gss_delete_sec_context", nullptr},
                {"gss_import_name", nullptr},
                {"gss_release_buffer", nullptr},
                {"gss_release_name", nullptr},
                {"gss_wrap", nullptr},
                {"gss_unwrap", nullptr}};

static PRLibrary* gssLibrary = nullptr;

#define gss_display_status_ptr ((gss_display_status_type) * gssFuncs[0].func)
#define gss_init_sec_context_ptr \
  ((gss_init_sec_context_type) * gssFuncs[1].func)
#define gss_indicate_mechs_ptr ((gss_indicate_mechs_type) * gssFuncs[2].func)
#define gss_release_oid_set_ptr ((gss_release_oid_set_type) * gssFuncs[3].func)
#define gss_delete_sec_context_ptr \
  ((gss_delete_sec_context_type) * gssFuncs[4].func)
#define gss_import_name_ptr ((gss_import_name_type) * gssFuncs[5].func)
#define gss_release_buffer_ptr ((gss_release_buffer_type) * gssFuncs[6].func)
#define gss_release_name_ptr ((gss_release_name_type) * gssFuncs[7].func)
#define gss_wrap_ptr ((gss_wrap_type) * gssFuncs[8].func)
#define gss_unwrap_ptr ((gss_unwrap_type) * gssFuncs[9].func)

#ifdef XP_MACOSX
static PRFuncPtr KLCacheHasValidTicketsPtr;
#  define KLCacheHasValidTickets_ptr \
    ((KLCacheHasValidTickets_type) * KLCacheHasValidTicketsPtr)
#endif

static nsresult gssInit() {
#ifdef XP_WIN
  nsAutoString libPathU;
  Preferences::GetString(kNegotiateAuthGssLib, libPathU);
  NS_ConvertUTF16toUTF8 libPath(libPathU);
#else
  nsAutoCString libPath;
  Preferences::GetCString(kNegotiateAuthGssLib, libPath);
#endif

  PRLibrary* lib = nullptr;

  if (!libPath.IsEmpty()) {
    LOG(("Attempting to load user specified library [%s]\n", libPath.get()));
#ifdef XP_WIN
    lib = LoadLibraryWithFlags(libPathU.get());
#else
    lib = LoadLibraryWithFlags(libPath.get());
#endif
  } else {
#ifdef XP_WIN
#  ifdef _WIN64
    constexpr auto kLibName = u"gssapi64.dll"_ns;
#  else
    constexpr auto kLibName = u"gssapi32.dll"_ns;
#  endif

    lib = LoadLibraryWithFlags(kLibName.get());
#elif defined(__OpenBSD__)
    /* OpenBSD doesn't register inter-library dependencies in basesystem
     * libs therefor we need to load all the libraries gssapi depends on,
     * in the correct order and with LD_GLOBAL for GSSAPI auth to work
     * fine.
     */

    const char* const verLibNames[] = {
        "libasn1.so",    "libcrypto.so", "libroken.so", "libheimbase.so",
        "libcom_err.so", "libkrb5.so",   "libgssapi.so"};

    PRLibSpec libSpec;
    for (size_t i = 0; i < std::size(verLibNames); ++i) {
      libSpec.type = PR_LibSpec_Pathname;
      libSpec.value.pathname = verLibNames[i];
      lib = PR_LoadLibraryWithFlags(libSpec, PR_LD_GLOBAL);
    }

#else

    const char* const libNames[] = {"gss", "gssapi_krb5", "gssapi"};

    const char* const verLibNames[] = {
        "libgssapi_krb5.so.2", /* MIT - FC, Suse10, Debian */
        "libgssapi.so.4",      /* Heimdal - Suse10, MDK */
        "libgssapi.so.1"       /* Heimdal - Suse9, CITI - FC, MDK, Suse10*/
    };

    for (size_t i = 0; i < std::size(verLibNames) && !lib; ++i) {
      lib = PR_LoadLibrary(verLibNames[i]);

      /* The CITI libgssapi library calls exit() during
       * initialization if it's not correctly configured. Try to
       * ensure that we never use this library for our GSSAPI
       * support, as its just a wrapper library, anyway.
       * See Bugzilla #325433
       */
      if (lib && PR_FindFunctionSymbol(lib, "internal_krb5_gss_initialize") &&
          PR_FindFunctionSymbol(lib, "gssd_pname_to_uid")) {
        LOG(("CITI libgssapi found, which calls exit(). Skipping\n"));
        PR_UnloadLibrary(lib);
        lib = nullptr;
      }
    }

    for (size_t i = 0; i < std::size(libNames) && !lib; ++i) {
      char* libName = PR_GetLibraryName(nullptr, libNames[i]);
      if (libName) {
        lib = PR_LoadLibrary(libName);
        PR_FreeLibraryName(libName);

        if (lib && PR_FindFunctionSymbol(lib, "internal_krb5_gss_initialize") &&
            PR_FindFunctionSymbol(lib, "gssd_pname_to_uid")) {
          LOG(("CITI libgssapi found, which calls exit(). Skipping\n"));
          PR_UnloadLibrary(lib);
          lib = nullptr;
        }
      }
    }
#endif
  }

  if (!lib) {
    LOG(("Fail to load gssapi library\n"));
    return NS_ERROR_FAILURE;
  }

  LOG(("Attempting to load gss functions\n"));

  for (auto& gssFunc : gssFuncs) {
    gssFunc.func = PR_FindFunctionSymbol(lib, gssFunc.str);
    if (!gssFunc.func) {
      LOG(("Fail to load %s function from gssapi library\n", gssFunc.str));
      PR_UnloadLibrary(lib);
      return NS_ERROR_FAILURE;
    }
  }
#ifdef XP_MACOSX
  if (libPath.IsEmpty() &&
      StaticPrefs::network_negotiate_auth_using_native_gsslib() &&
      !(KLCacheHasValidTicketsPtr =
            PR_FindFunctionSymbol(lib, "KLCacheHasValidTickets"))) {
    LOG(("Fail to load KLCacheHasValidTickets function from gssapi library\n"));
    PR_UnloadLibrary(lib);
    return NS_ERROR_FAILURE;
  }
#endif

  gssLibrary = lib;
  return NS_OK;
}

// Generate proper GSSAPI error messages from the major and
// minor status codes.
void LogGssError(OM_uint32 maj_stat, OM_uint32 min_stat, const char* prefix) {
  if (!MOZ_LOG_TEST(gNegotiateLog, LogLevel::Debug)) {
    return;
  }

  OM_uint32 new_stat;
  OM_uint32 msg_ctx = 0;
  gss_buffer_desc status1_string;
  gss_buffer_desc status2_string;
  OM_uint32 ret;
  nsAutoCString errorStr;
  errorStr.Assign(prefix);

  if (!gssLibrary) return;

  errorStr += ": ";
  do {
    ret = gss_display_status_ptr(&new_stat, maj_stat, GSS_C_GSS_CODE,
                                 GSS_C_NULL_OID, &msg_ctx, &status1_string);
    errorStr.Append((const char*)status1_string.value, status1_string.length);
    gss_release_buffer_ptr(&new_stat, &status1_string);

    errorStr += '\n';
    ret = gss_display_status_ptr(&new_stat, min_stat, GSS_C_MECH_CODE,
                                 GSS_C_NULL_OID, &msg_ctx, &status2_string);
    errorStr.Append((const char*)status2_string.value, status2_string.length);
    errorStr += '\n';
  } while (!GSS_ERROR(ret) && msg_ctx != 0);

  LOG(("%s\n", errorStr.get()));
}

//-----------------------------------------------------------------------------

nsAuthGSSAPI::nsAuthGSSAPI(pType package) : mServiceFlags(REQ_DEFAULT) {
  OM_uint32 minstat;
  OM_uint32 majstat;
  gss_OID_set mech_set;
  gss_OID item;

  unsigned int i;
  static gss_OID_desc gss_krb5_mech_oid_desc = {
      9, (void*)"\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"};
  static gss_OID_desc gss_spnego_mech_oid_desc = {
      6, (void*)"\x2b\x06\x01\x05\x05\x02"};

  LOG(("entering nsAuthGSSAPI::nsAuthGSSAPI()\n"));

  mComplete = false;

  if (!gssLibrary && NS_FAILED(gssInit())) return;

  mCtx = GSS_C_NO_CONTEXT;
  mMechOID = &gss_krb5_mech_oid_desc;

  // if the type is kerberos we accept it as default
  // and exit

  if (package == PACKAGE_TYPE_KERBEROS) return;

  // Now, look at the list of supported mechanisms,
  // if SPNEGO is found, then use it.
  // Otherwise, set the desired mechanism to
  // GSS_C_NO_OID and let the system try to use
  // the default mechanism.
  //
  // Using Kerberos directly (instead of negotiating
  // with SPNEGO) may work in some cases depending
  // on how smart the server side is.

  majstat = gss_indicate_mechs_ptr(&minstat, &mech_set);
  if (GSS_ERROR(majstat)) return;

  if (mech_set) {
    for (i = 0; i < mech_set->count; i++) {
      item = &mech_set->elements[i];
      if (item->length == gss_spnego_mech_oid_desc.length &&
          !memcmp(item->elements, gss_spnego_mech_oid_desc.elements,
                  item->length)) {
        // ok, we found it
        mMechOID = &gss_spnego_mech_oid_desc;
        break;
      }
    }
    gss_release_oid_set_ptr(&minstat, &mech_set);
  }
}

void nsAuthGSSAPI::Reset() {
  if (gssLibrary && mCtx != GSS_C_NO_CONTEXT) {
    OM_uint32 minor_status;
    gss_delete_sec_context_ptr(&minor_status, &mCtx, GSS_C_NO_BUFFER);
  }
  mCtx = GSS_C_NO_CONTEXT;
  mComplete = false;
}

/* static */
void nsAuthGSSAPI::Shutdown() {
  if (gssLibrary) {
    PR_UnloadLibrary(gssLibrary);
    gssLibrary = nullptr;
  }
}

/* Limitations apply to this class's thread safety. See the header file */
NS_IMPL_ISUPPORTS(nsAuthGSSAPI, nsIAuthModule)

NS_IMETHODIMP
nsAuthGSSAPI::Init(const nsACString& serviceName, uint32_t serviceFlags,
                   const nsAString& domain, const nsAString& username,
                   const nsAString& password) {
  // we don't expect to be passed any user credentials
  NS_ASSERTION(domain.IsEmpty() && username.IsEmpty() && password.IsEmpty(),
               "unexpected credentials");

  // it's critial that the caller supply a service name to be used
  NS_ENSURE_TRUE(!serviceName.IsEmpty(), NS_ERROR_INVALID_ARG);

  LOG(("entering nsAuthGSSAPI::Init()\n"));

  if (!gssLibrary) return NS_ERROR_NOT_INITIALIZED;

  mServiceName = serviceName;
  mServiceFlags = serviceFlags;

  // Init runs on background threads, so several auths may reach this
  // concurrently.
  static mozilla::Atomic<bool> sTelemetrySent{false};
  if (sTelemetrySent.compareExchange(false, true)) {
    mozilla::glean::security::ntlm_module_used.AccumulateSingleSample(
        serviceFlags & nsIAuthModule::REQ_PROXY_AUTH
            ? NTLM_MODULE_KERBEROS_PROXY
            : NTLM_MODULE_KERBEROS_DIRECT);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAuthGSSAPI::InitAsync(const nsACString& /*aServiceName*/,
                        uint32_t /*aServiceFlags*/,
                        const nsAString& /*aDomain*/,
                        const nsAString& /*aUsername*/,
                        const nsAString& /*aPassword*/,
                        nsIAuthModuleInitCallback* /*aCallback*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsAuthGSSAPI::GetNextToken(const void* inToken, uint32_t inTokenLen,
                           void** outToken, uint32_t* outTokenLen) {
  OM_uint32 major_status, minor_status;
  OM_uint32 req_flags = 0;
  gss_buffer_desc input_token = GSS_C_EMPTY_BUFFER;
  gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
  gss_buffer_t in_token_ptr = GSS_C_NO_BUFFER;
  gss_name_t server;
  nsAutoCString userbuf;
  nsresult rv;

  LOG(("entering nsAuthGSSAPI::GetNextToken()\n"));

  if (!gssLibrary) return NS_ERROR_NOT_INITIALIZED;

  // If they've called us again after we're complete, reset to start afresh.
  if (mComplete) Reset();

  if (mServiceFlags & REQ_DELEGATE) req_flags |= GSS_C_DELEG_FLAG;

  if (mServiceFlags & REQ_MUTUAL_AUTH) req_flags |= GSS_C_MUTUAL_FLAG;

  input_token.value = (void*)mServiceName.get();
  input_token.length = mServiceName.Length() + 1;

#if defined(HAVE_RES_NINIT)
  res_ninit(&_res);
#endif
  major_status = gss_import_name_ptr(&minor_status, &input_token,
                                     &gss_c_nt_hostbased_service, &server);
  input_token.value = nullptr;
  input_token.length = 0;
  if (GSS_ERROR(major_status)) {
    LogGssError(major_status, minor_status, "gss_import_name() failed");
    return NS_ERROR_FAILURE;
  }

  if (inToken) {
    input_token.length = inTokenLen;
    input_token.value = (void*)inToken;
    in_token_ptr = &input_token;
  } else if (mCtx != GSS_C_NO_CONTEXT) {
    // If there is no input token, then we are starting a new
    // authentication sequence.  If we have already initialized our
    // security context, then we're in trouble because it means that the
    // first sequence failed.  We need to bail or else we might end up in
    // an infinite loop.
    LOG(("Cannot restart authentication sequence!"));
    return NS_ERROR_UNEXPECTED;
  }

#if defined(XP_MACOSX)
  // Suppress Kerberos prompts to get credentials.  See bug 240643.
  // We can only use Mac OS X specific kerb functions if we are using
  // the native lib
  KLBoolean found;
  bool doingMailTask = mServiceName.Find("imap@") ||
                       mServiceName.Find("pop@") ||
                       mServiceName.Find("smtp@") || mServiceName.Find("ldap@");

  if (!doingMailTask &&
      (KLCacheHasValidTicketsPtr &&
       (KLCacheHasValidTickets_ptr(nullptr, kerberosVersion_V5, &found, nullptr,
                                   nullptr) != klNoErr ||
        !found))) {
    major_status = GSS_S_FAILURE;
    minor_status = 0;
  } else
#endif /* XP_MACOSX */
    major_status = gss_init_sec_context_ptr(
        &minor_status, GSS_C_NO_CREDENTIAL, &mCtx, server, mMechOID, req_flags,
        GSS_C_INDEFINITE, GSS_C_NO_CHANNEL_BINDINGS, in_token_ptr, nullptr,
        &output_token, nullptr, nullptr);

  if (GSS_ERROR(major_status)) {
    LogGssError(major_status, minor_status, "gss_init_sec_context() failed");
    Reset();
    rv = NS_ERROR_FAILURE;
    goto end;
  }
  if (major_status == GSS_S_COMPLETE) {
    // Mark ourselves as being complete, so that if we're called again
    // we know to start afresh.
    mComplete = true;
  } else if (major_status == GSS_S_CONTINUE_NEEDED) {
    //
    // The important thing is that we do NOT reset the
    // context here because it will be needed on the
    // next call.
    //
  }

  *outTokenLen = output_token.length;
  if (output_token.length != 0) {
    *outToken = moz_xmemdup(output_token.value, output_token.length);
  } else {
    *outToken = nullptr;
  }

  gss_release_buffer_ptr(&minor_status, &output_token);

  if (major_status == GSS_S_COMPLETE) {
    rv = NS_SUCCESS_AUTH_FINISHED;
  } else {
    rv = NS_OK;
  }

end:
  gss_release_name_ptr(&minor_status, &server);

  LOG(("  leaving nsAuthGSSAPI::GetNextToken [rv=%" PRIx32 "]",
       static_cast<uint32_t>(rv)));
  return rv;
}

NS_IMETHODIMP
nsAuthGSSAPI::Unwrap(const void* inToken, uint32_t inTokenLen, void** outToken,
                     uint32_t* outTokenLen) {
  OM_uint32 major_status, minor_status;

  gss_buffer_desc input_token;
  gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;

  input_token.value = (void*)inToken;
  input_token.length = inTokenLen;

  major_status = gss_unwrap_ptr(&minor_status, mCtx, &input_token,
                                &output_token, nullptr, nullptr);
  if (GSS_ERROR(major_status)) {
    LogGssError(major_status, minor_status, "gss_unwrap() failed");
    Reset();
    gss_release_buffer_ptr(&minor_status, &output_token);
    return NS_ERROR_FAILURE;
  }

  *outTokenLen = output_token.length;

  if (output_token.length) {
    *outToken = moz_xmemdup(output_token.value, output_token.length);
  } else {
    *outToken = nullptr;
  }

  gss_release_buffer_ptr(&minor_status, &output_token);

  return NS_OK;
}

NS_IMETHODIMP
nsAuthGSSAPI::Wrap(const void* inToken, uint32_t inTokenLen, bool confidential,
                   void** outToken, uint32_t* outTokenLen) {
  OM_uint32 major_status, minor_status;

  gss_buffer_desc input_token;
  gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;

  input_token.value = (void*)inToken;
  input_token.length = inTokenLen;

  major_status =
      gss_wrap_ptr(&minor_status, mCtx, confidential, GSS_C_QOP_DEFAULT,
                   &input_token, nullptr, &output_token);

  if (GSS_ERROR(major_status)) {
    LogGssError(major_status, minor_status, "gss_wrap() failed");
    Reset();
    gss_release_buffer_ptr(&minor_status, &output_token);
    return NS_ERROR_FAILURE;
  }

  *outTokenLen = output_token.length;

  /* it is not possible for output_token.length to be zero */
  *outToken = moz_xmemdup(output_token.value, output_token.length);
  gss_release_buffer_ptr(&minor_status, &output_token);

  return NS_OK;
}

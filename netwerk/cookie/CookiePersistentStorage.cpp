/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Cookie.h"
#include "CookieCommons.h"
#include "CookieLogging.h"
#include "CookiePersistentStorage.h"
#include "CookieService.h"
#include "CookieValidation.h"

#include "mozilla/Components.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/FileUtils.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/ScopeExit.h"
#include "mozIStorageAsyncStatement.h"
#include "mozIStorageError.h"
#include "mozIStorageFunction.h"
#include "mozIStorageService.h"
#include "mozStorageHelper.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsICookieNotification.h"
#include "nsIEffectiveTLDService.h"
#include "nsILineInputStream.h"
#include "nsIURIMutator.h"
#include "nsNetUtil.h"
#include "nsVariant.h"
#include "prprf.h"

constexpr auto COOKIES_SCHEMA_VERSION = 17;

// parameter indexes; see |Read|
constexpr auto IDX_NAME = 0;
constexpr auto IDX_VALUE = 1;
constexpr auto IDX_HOST = 2;
constexpr auto IDX_PATH = 3;
constexpr auto IDX_EXPIRY_INMSEC = 4;
constexpr auto IDX_LAST_ACCESSED_INUSEC = 5;
constexpr auto IDX_CREATION_TIME_INUSEC = 6;
constexpr auto IDX_SECURE = 7;
constexpr auto IDX_HTTPONLY = 8;
constexpr auto IDX_ORIGIN_ATTRIBUTES = 9;
constexpr auto IDX_SAME_SITE = 10;
constexpr auto IDX_SCHEME_MAP = 11;
constexpr auto IDX_PARTITIONED_ATTRIBUTE_SET = 12;
constexpr auto IDX_UPDATE_TIME_INUSEC = 13;

#define COOKIES_FILE "cookies.sqlite"

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS_INHERITED(CookiePersistentStorage, CookieStorage,
                            nsIAsyncShutdownBlocker)

namespace {

void BindCookieParameters(mozIStorageBindingParamsArray* aParamsArray,
                          const CookieKey& aKey, const Cookie* aCookie) {
  NS_ASSERTION(aParamsArray,
               "Null params array passed to BindCookieParameters!");
  NS_ASSERTION(aCookie, "Null cookie passed to BindCookieParameters!");

  // Use the asynchronous binding methods to ensure that we do not acquire the
  // database lock.
  nsCOMPtr<mozIStorageBindingParams> params;
  DebugOnly<nsresult> rv =
      aParamsArray->NewBindingParams(getter_AddRefs(params));
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  nsAutoCString suffix;
  aKey.mOriginAttributes.CreateSuffix(suffix);
  rv = params->BindUTF8StringByName("originAttributes"_ns, suffix);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindUTF8StringByName("name"_ns, aCookie->Name());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindUTF8StringByName("value"_ns, aCookie->Value());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindUTF8StringByName("host"_ns, aCookie->Host());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindUTF8StringByName("path"_ns, aCookie->Path());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindInt64ByName("expiry"_ns, aCookie->ExpiryInMSec());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv =
      params->BindInt64ByName("lastAccessed"_ns, aCookie->LastAccessedInUSec());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv =
      params->BindInt64ByName("creationTime"_ns, aCookie->CreationTimeInUSec());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindInt32ByName("isSecure"_ns, aCookie->IsSecure());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindInt32ByName("isHttpOnly"_ns, aCookie->IsHttpOnly());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindInt32ByName("sameSite"_ns, aCookie->SameSite());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindInt32ByName("schemeMap"_ns, aCookie->SchemeMap());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindInt32ByName("isPartitionedAttributeSet"_ns,
                               aCookie->RawIsPartitioned());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindInt64ByName("updateTime"_ns, aCookie->UpdateTimeInUSec());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // Bind the params to the array.
  rv = aParamsArray->AddParams(params);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

class ConvertAppIdToOriginAttrsSQLFunction final : public mozIStorageFunction {
  ~ConvertAppIdToOriginAttrsSQLFunction() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

NS_IMPL_ISUPPORTS(ConvertAppIdToOriginAttrsSQLFunction, mozIStorageFunction);

NS_IMETHODIMP
ConvertAppIdToOriginAttrsSQLFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  nsresult rv;
  OriginAttributes attrs;
  nsAutoCString suffix;
  attrs.CreateSuffix(suffix);

  RefPtr<nsVariant> outVar(new nsVariant());
  rv = outVar->SetAsAUTF8String(suffix);
  NS_ENSURE_SUCCESS(rv, rv);

  outVar.forget(aResult);
  return NS_OK;
}

class SetAppIdFromOriginAttributesSQLFunction final
    : public mozIStorageFunction {
  ~SetAppIdFromOriginAttributesSQLFunction() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

NS_IMPL_ISUPPORTS(SetAppIdFromOriginAttributesSQLFunction, mozIStorageFunction);

NS_IMETHODIMP
SetAppIdFromOriginAttributesSQLFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  nsresult rv;
  nsAutoCString suffix;
  OriginAttributes attrs;

  rv = aFunctionArguments->GetUTF8String(0, suffix);
  NS_ENSURE_SUCCESS(rv, rv);
  bool success = attrs.PopulateFromSuffix(suffix);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  RefPtr<nsVariant> outVar(new nsVariant());
  rv = outVar->SetAsInt32(0);  // deprecated appId!
  NS_ENSURE_SUCCESS(rv, rv);

  outVar.forget(aResult);
  return NS_OK;
}

class SetInBrowserFromOriginAttributesSQLFunction final
    : public mozIStorageFunction {
  ~SetInBrowserFromOriginAttributesSQLFunction() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

NS_IMPL_ISUPPORTS(SetInBrowserFromOriginAttributesSQLFunction,
                  mozIStorageFunction);

NS_IMETHODIMP
SetInBrowserFromOriginAttributesSQLFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  nsresult rv;
  nsAutoCString suffix;
  OriginAttributes attrs;

  rv = aFunctionArguments->GetUTF8String(0, suffix);
  NS_ENSURE_SUCCESS(rv, rv);
  bool success = attrs.PopulateFromSuffix(suffix);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  RefPtr<nsVariant> outVar(new nsVariant());
  rv = outVar->SetAsInt32(false);
  NS_ENSURE_SUCCESS(rv, rv);

  outVar.forget(aResult);
  return NS_OK;
}

class FetchPartitionKeyFromOAsSQLFunction final : public mozIStorageFunction {
  ~FetchPartitionKeyFromOAsSQLFunction() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

NS_IMPL_ISUPPORTS(FetchPartitionKeyFromOAsSQLFunction, mozIStorageFunction);

NS_IMETHODIMP
FetchPartitionKeyFromOAsSQLFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  nsresult rv;

  nsAutoCString suffix;
  rv = aFunctionArguments->GetUTF8String(0, suffix);
  NS_ENSURE_SUCCESS(rv, rv);

  OriginAttributes attrsFromSuffix;
  bool success = attrsFromSuffix.PopulateFromSuffix(suffix);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  RefPtr<nsVariant> outVar(new nsVariant());
  rv = outVar->SetAsAString(attrsFromSuffix.mPartitionKey);
  NS_ENSURE_SUCCESS(rv, rv);

  outVar.forget(aResult);

  return NS_OK;
}

class UpdateOAsWithPartitionHostSQLFunction final : public mozIStorageFunction {
  ~UpdateOAsWithPartitionHostSQLFunction() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

NS_IMPL_ISUPPORTS(UpdateOAsWithPartitionHostSQLFunction, mozIStorageFunction);

NS_IMETHODIMP
UpdateOAsWithPartitionHostSQLFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  nsresult rv;

  nsAutoCString formattedOriginAttributes;
  rv = aFunctionArguments->GetUTF8String(0, formattedOriginAttributes);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString partitionKeyHost;
  rv = aFunctionArguments->GetUTF8String(1, partitionKeyHost);
  NS_ENSURE_SUCCESS(rv, rv);

  OriginAttributes attrsFromSuffix;
  bool success = attrsFromSuffix.PopulateFromSuffix(formattedOriginAttributes);
  // On failure, do not alter the OA.
  if (!success) {
    RefPtr<nsVariant> outVar(new nsVariant());
    rv = outVar->SetAsACString(formattedOriginAttributes);
    NS_ENSURE_SUCCESS(rv, rv);
    outVar.forget(aResult);
    return NS_OK;
  }

  // This is a bit hacky. However, CHIPS cookies can only be set in secure
  // contexts. So, the scheme has to be https.
  nsAutoCString schemeHost;
  schemeHost.AssignLiteral("https://");

  if (*partitionKeyHost.get() == '.') {
    schemeHost.Append(nsDependentCSubstring(partitionKeyHost, 1));
  } else {
    schemeHost.Append(partitionKeyHost);
  }

  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), schemeHost);
  // On failure, do not alter the OA.
  if (NS_FAILED(rv)) {
    RefPtr<nsVariant> outVar(new nsVariant());
    rv = outVar->SetAsACString(formattedOriginAttributes);
    NS_ENSURE_SUCCESS(rv, rv);
    outVar.forget(aResult);
    return NS_OK;
  }

  attrsFromSuffix.SetPartitionKey(uri, false);
  attrsFromSuffix.CreateSuffix(formattedOriginAttributes);

  RefPtr<nsVariant> outVar(new nsVariant());
  rv = outVar->SetAsACString(formattedOriginAttributes);
  NS_ENSURE_SUCCESS(rv, rv);
  outVar.forget(aResult);
  return NS_OK;
}

/******************************************************************************
 * DBListenerErrorHandler impl:
 * Parent class for our async storage listeners that handles the logging of
 * errors.
 ******************************************************************************/
class DBListenerErrorHandler : public mozIStorageStatementCallback {
 protected:
  explicit DBListenerErrorHandler(CookiePersistentStorage* dbState)
      : mStorage(dbState) {}
  RefPtr<CookiePersistentStorage> mStorage;
  virtual const char* GetOpType() = 0;

 public:
  NS_IMETHOD HandleError(mozIStorageError* aError) override {
    if (MOZ_LOG_TEST(gCookieLog, LogLevel::Warning)) {
      int32_t result = -1;
      aError->GetResult(&result);

      nsAutoCString message;
      aError->GetMessage(message);
      COOKIE_LOGSTRING(
          LogLevel::Warning,
          ("DBListenerErrorHandler::HandleError(): Error %d occurred while "
           "performing operation '%s' with message '%s'; rebuilding database.",
           result, GetOpType(), message.get()));
    }

    // Rebuild the database.
    mStorage->HandleCorruptDB();

    return NS_OK;
  }
};

/******************************************************************************
 * InsertCookieDBListener impl:
 * mozIStorageStatementCallback used to track asynchronous insertion operations.
 ******************************************************************************/
class InsertCookieDBListener final : public DBListenerErrorHandler {
 private:
  const char* GetOpType() override { return "INSERT"; }

  ~InsertCookieDBListener() = default;

 public:
  NS_DECL_ISUPPORTS

  explicit InsertCookieDBListener(CookiePersistentStorage* dbState)
      : DBListenerErrorHandler(dbState) {}
  NS_IMETHOD HandleResult(mozIStorageResultSet* /*aResultSet*/) override {
    MOZ_ASSERT_UNREACHABLE(
        "Unexpected call to "
        "InsertCookieDBListener::HandleResult");
    return NS_OK;
  }
  NS_IMETHOD HandleCompletion(uint16_t aReason) override {
    // If we were rebuilding the db and we succeeded, make our mCorruptFlag say
    // so.
    if (mStorage->GetCorruptFlag() == CookiePersistentStorage::REBUILDING &&
        aReason == mozIStorageStatementCallback::REASON_FINISHED) {
      COOKIE_LOGSTRING(
          LogLevel::Debug,
          ("InsertCookieDBListener::HandleCompletion(): rebuild complete"));
      mStorage->SetCorruptFlag(CookiePersistentStorage::OK);
    }

    // This notification is just for testing.
    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    if (os) {
      os->NotifyObservers(nullptr, "cookie-saved-on-disk", nullptr);
    }

    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(InsertCookieDBListener, mozIStorageStatementCallback)

/******************************************************************************
 * UpdateCookieDBListener impl:
 * mozIStorageStatementCallback used to track asynchronous update operations.
 ******************************************************************************/
class UpdateCookieDBListener final : public DBListenerErrorHandler {
 private:
  const char* GetOpType() override { return "UPDATE"; }

  ~UpdateCookieDBListener() = default;

 public:
  NS_DECL_ISUPPORTS

  explicit UpdateCookieDBListener(CookiePersistentStorage* dbState)
      : DBListenerErrorHandler(dbState) {}
  NS_IMETHOD HandleResult(mozIStorageResultSet* /*aResultSet*/) override {
    MOZ_ASSERT_UNREACHABLE(
        "Unexpected call to "
        "UpdateCookieDBListener::HandleResult");
    return NS_OK;
  }
  NS_IMETHOD HandleCompletion(uint16_t /*aReason*/) override { return NS_OK; }
};

NS_IMPL_ISUPPORTS(UpdateCookieDBListener, mozIStorageStatementCallback)

/******************************************************************************
 * RemoveCookieDBListener impl:
 * mozIStorageStatementCallback used to track asynchronous removal operations.
 ******************************************************************************/
class RemoveCookieDBListener final : public DBListenerErrorHandler {
 private:
  const char* GetOpType() override { return "REMOVE"; }

  ~RemoveCookieDBListener() = default;

 public:
  NS_DECL_ISUPPORTS

  explicit RemoveCookieDBListener(CookiePersistentStorage* dbState)
      : DBListenerErrorHandler(dbState) {}
  NS_IMETHOD HandleResult(mozIStorageResultSet* /*aResultSet*/) override {
    MOZ_ASSERT_UNREACHABLE(
        "Unexpected call to "
        "RemoveCookieDBListener::HandleResult");
    return NS_OK;
  }
  NS_IMETHOD HandleCompletion(uint16_t /*aReason*/) override { return NS_OK; }
};

NS_IMPL_ISUPPORTS(RemoveCookieDBListener, mozIStorageStatementCallback)

/******************************************************************************
 * CloseCookieDBListener imp:
 * Static mozIStorageCompletionCallback used to notify when the database is
 * successfully closed.
 ******************************************************************************/
class CloseCookieDBListener final : public mozIStorageCompletionCallback {
  ~CloseCookieDBListener() = default;

 public:
  explicit CloseCookieDBListener(CookiePersistentStorage* dbState)
      : mStorage(dbState) {}
  RefPtr<CookiePersistentStorage> mStorage;
  NS_DECL_ISUPPORTS

  NS_IMETHOD Complete(nsresult /*status*/, nsISupports* /*value*/) override {
    mStorage->HandleDBClosed();
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(CloseCookieDBListener, mozIStorageCompletionCallback)

static nsLiteralCString ValidationErrorToLabel(
    nsICookieValidation::ValidationError aError) {
  switch (aError) {
    case nsICookieValidation::eOK:
      return "eOK"_ns;
    case nsICookieValidation::eRejectedEmptyNameAndValue:
      return "eRejectedEmptyNameAndValue"_ns;
    case nsICookieValidation::eRejectedNameValueOversize:
      return "eRejectedNameValueOversize"_ns;
    case nsICookieValidation::eRejectedInvalidCharName:
      return "eRejectedInvalidCharName"_ns;
    case nsICookieValidation::eRejectedInvalidCharValue:
      return "eRejectedInvalidCharValue"_ns;
    case nsICookieValidation::eRejectedInvalidPath:
      return "eRejectedInvalidPath"_ns;
    case nsICookieValidation::eRejectedInvalidDomain:
      return "eRejectedInvalidDomain"_ns;
    case nsICookieValidation::eRejectedInvalidPrefix:
      return "eRejectedInvalidPrefix"_ns;
    case nsICookieValidation::eRejectedNoneRequiresSecure:
      return "eRejectedNoneRequiresSecure"_ns;
    case nsICookieValidation::eRejectedPartitionedRequiresSecure:
      return "eRejectedPartitionedRequiresSecure"_ns;
    case nsICookieValidation::eRejectedHttpOnlyButFromScript:
      return "eRejectedHttpOnlyButFromScript"_ns;
    case nsICookieValidation::eRejectedSecureButNonHttps:
      return "eRejectedSecureButNonHttps"_ns;
    case nsICookieValidation::eRejectedForNonSameSiteness:
      return "eRejectedForNonSameSiteness"_ns;
    case nsICookieValidation::eRejectedAttributePathOversize:
      return "eRejectedAttributePathOversize"_ns;
    case nsICookieValidation::eRejectedAttributeDomainOversize:
      return "eRejectedAttributeDomainOversize"_ns;
    case nsICookieValidation::eRejectedAttributeExpiryOversize:
      return "eRejectedAttributeExpiryOversize"_ns;
    default:
      return "eOK"_ns;
  }
}

}  // namespace

// static
already_AddRefed<CookiePersistentStorage> CookiePersistentStorage::Create() {
  RefPtr<CookiePersistentStorage> storage = new CookiePersistentStorage();
  storage->Init();
  storage->Activate();

  return storage.forget();
}

CookiePersistentStorage::CookiePersistentStorage()
    : mMonitor("CookiePersistentStorage"),
      mInitialized(false),
      mCorruptFlag(OK) {}

void CookiePersistentStorage::NotifyChangedInternal(
    nsICookieNotification* aNotification, bool aOldCookieIsSession) {
  MOZ_ASSERT(aNotification);
  // Notify for topic "session-cookie-changed" to update the copy of session
  // cookies in session restore component.

  nsICookieNotification::Action action = aNotification->GetAction();

  // Filter out notifications for individual non-session cookies.
  if (action == nsICookieNotification::COOKIE_CHANGED ||
      action == nsICookieNotification::COOKIE_DELETED ||
      action == nsICookieNotification::COOKIE_ADDED) {
    nsCOMPtr<nsICookie> xpcCookie;
    DebugOnly<nsresult> rv =
        aNotification->GetCookie(getter_AddRefs(xpcCookie));
    MOZ_ASSERT(NS_SUCCEEDED(rv) && xpcCookie);
    const Cookie& cookie = xpcCookie->AsCookie();
    if (!cookie.IsSession() && !aOldCookieIsSession) {
      return;
    }
  }

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    os->NotifyObservers(aNotification, "session-cookie-changed", u"");
  }
}

void CookiePersistentStorage::RemoveAllInternal() {
  // clear the cookie file
  if (mDBConn) {
    nsCOMPtr<mozIStorageAsyncStatement> stmt;
    nsresult rv = mDBConn->CreateAsyncStatement("DELETE FROM moz_cookies"_ns,
                                                getter_AddRefs(stmt));
    if (NS_SUCCEEDED(rv)) {
      nsCOMPtr<mozIStoragePendingStatement> handle;
      rv = stmt->ExecuteAsync(mRemoveListener, getter_AddRefs(handle));
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    } else {
      // Recreate the database.
      COOKIE_LOGSTRING(LogLevel::Debug,
                       ("RemoveAll(): corruption detected with rv 0x%" PRIx32,
                        static_cast<uint32_t>(rv)));
      HandleCorruptDB();
    }
  }
}

void CookiePersistentStorage::HandleCorruptDB() {
  COOKIE_LOGSTRING(
      LogLevel::Debug,
      ("HandleCorruptDB(): CookieStorage %p has mCorruptFlag %u", this,
       static_cast<unsigned>(static_cast<CorruptFlag>(mCorruptFlag))));

  // Mark the database corrupt, so the close listener can begin reconstructing
  // it.
  switch (mCorruptFlag) {
    case OK: {
      // Move to 'closing' state.
      mCorruptFlag = CLOSING_FOR_REBUILD;

      CleanupCachedStatements();
      mDBConn->AsyncClose(mCloseListener);
      CleanupDBConnection();
      break;
    }
    case CLOSING_FOR_REBUILD: {
      // We had an error while waiting for close completion. That's OK, just
      // ignore it -- we're rebuilding anyway.
      return;
    }
    case REBUILDING: {
      // We had an error while rebuilding the DB. Game over. Close the database
      // and let the close handler do nothing; then we'll move it out of the
      // way.
      CleanupCachedStatements();
      if (mDBConn) {
        mDBConn->AsyncClose(mCloseListener);
      }
      CleanupDBConnection();
      break;
    }
  }
}

void CookiePersistentStorage::RemoveCookiesWithOriginAttributes(
    const OriginAttributesPattern& aPattern, const nsACString& aBaseDomain) {
  mozStorageTransaction transaction(mDBConn, false);

  // XXX Handle the error, bug 1696130.
  (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

  CookieStorage::RemoveCookiesWithOriginAttributes(aPattern, aBaseDomain);

  DebugOnly<nsresult> rv = transaction.Commit();
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

void CookiePersistentStorage::RemoveCookiesFromExactHost(
    const nsACString& aHost, const nsACString& aBaseDomain,
    const OriginAttributesPattern& aPattern) {
  mozStorageTransaction transaction(mDBConn, false);

  // XXX Handle the error, bug 1696130.
  (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

  CookieStorage::RemoveCookiesFromExactHost(aHost, aBaseDomain, aPattern);

  DebugOnly<nsresult> rv = transaction.Commit();
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

void CookiePersistentStorage::RemoveCookieFromDB(const Cookie& aCookie) {
  // if it's a non-session cookie, remove it from the db
  if (aCookie.IsSession() || !mDBConn) {
    return;
  }

  nsCOMPtr<mozIStorageBindingParamsArray> paramsArray;
  mStmtDelete->NewBindingParamsArray(getter_AddRefs(paramsArray));

  PrepareCookieRemoval(aCookie, paramsArray);

  DebugOnly<nsresult> rv = mStmtDelete->BindParameters(paramsArray);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  nsCOMPtr<mozIStoragePendingStatement> handle;
  rv = mStmtDelete->ExecuteAsync(mRemoveListener, getter_AddRefs(handle));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

void CookiePersistentStorage::PrepareCookieRemoval(
    const Cookie& aCookie, mozIStorageBindingParamsArray* aParamsArray) {
  // if it's a non-session cookie, remove it from the db
  if (aCookie.IsSession() || !mDBConn) {
    return;
  }

  nsCOMPtr<mozIStorageBindingParams> params;
  aParamsArray->NewBindingParams(getter_AddRefs(params));

  DebugOnly<nsresult> rv =
      params->BindUTF8StringByName("name"_ns, aCookie.Name());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindUTF8StringByName("host"_ns, aCookie.Host());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = params->BindUTF8StringByName("path"_ns, aCookie.Path());
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  nsAutoCString suffix;
  aCookie.OriginAttributesRef().CreateSuffix(suffix);
  rv = params->BindUTF8StringByName("originAttributes"_ns, suffix);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = aParamsArray->AddParams(params);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

// Null out the statements.
// This must be done before closing the connection.
void CookiePersistentStorage::CleanupCachedStatements() {
  mStmtInsert = nullptr;
  mStmtDelete = nullptr;
  mStmtUpdate = nullptr;
}

// Null out the listeners, and the database connection itself. This
// will not null out the statements, cancel a pending read or
// asynchronously close the connection -- these must be done
// beforehand if necessary.
void CookiePersistentStorage::CleanupDBConnection() {
  MOZ_ASSERT(!mStmtInsert, "mStmtInsert has been cleaned up");
  MOZ_ASSERT(!mStmtDelete, "mStmtDelete has been cleaned up");
  MOZ_ASSERT(!mStmtUpdate, "mStmtUpdate has been cleaned up");

  // Null out the database connections. If 'mDBConn' has not been used for any
  // asynchronous operations yet, this will synchronously close it; otherwise,
  // it's expected that the caller has performed an AsyncClose prior.
  mDBConn = nullptr;

  // Manually null out our listeners. This is necessary because they hold a
  // strong ref to the CookieStorage itself. They'll stay alive until whatever
  // statements are still executing complete.
  mInsertListener = nullptr;
  mUpdateListener = nullptr;
  mRemoveListener = nullptr;
  mCloseListener = nullptr;
}

void CookiePersistentStorage::Close() {
  if (mThread) {
    mThread->Shutdown();
    mThread = nullptr;
  }

  // Cleanup cached statements before we can close anything.
  CleanupCachedStatements();

  if (mDBConn) {
    // Asynchronously close the connection. We will null it below.
    // The shutdown blocker will be removed in HandleDBClosed().
    mDBConn->AsyncClose(mCloseListener);
  } else {
    RemoveShutdownBlocker();
  }

  CleanupDBConnection();

  mInitialized = false;
  mInitializedDBConn = false;
}

void CookiePersistentStorage::StoreCookie(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    Cookie* aCookie) {
  // if it's a non-session cookie and hasn't just been read from the db, write
  // it out.
  if (aCookie->IsSession() || !mDBConn) {
    return;
  }

  nsCOMPtr<mozIStorageBindingParamsArray> paramsArray;
  mStmtInsert->NewBindingParamsArray(getter_AddRefs(paramsArray));

  CookieKey key(aBaseDomain, aOriginAttributes);
  BindCookieParameters(paramsArray, key, aCookie);

  MaybeStoreCookiesToDB(paramsArray);
}

void CookiePersistentStorage::MaybeStoreCookiesToDB(
    mozIStorageBindingParamsArray* aParamsArray) {
  if (!aParamsArray) {
    return;
  }

  uint32_t length;
  aParamsArray->GetLength(&length);
  if (!length) {
    return;
  }

  DebugOnly<nsresult> rv = mStmtInsert->BindParameters(aParamsArray);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  nsCOMPtr<mozIStoragePendingStatement> handle;
  rv = mStmtInsert->ExecuteAsync(mInsertListener, getter_AddRefs(handle));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

void CookiePersistentStorage::StaleCookies(
    const nsTArray<RefPtr<Cookie>>& aCookieList, int64_t aCurrentTimeInUsec) {
  // Create an array of parameters to bind to our update statement. Batching
  // is OK here since we're updating cookies with no interleaved operations.
  nsCOMPtr<mozIStorageBindingParamsArray> paramsArray;
  mozIStorageAsyncStatement* stmt = mStmtUpdate;
  if (mDBConn) {
    stmt->NewBindingParamsArray(getter_AddRefs(paramsArray));
  }

  int32_t count = aCookieList.Length();
  for (int32_t i = 0; i < count; ++i) {
    Cookie* cookie = aCookieList.ElementAt(i);

    if (cookie->IsStale()) {
      UpdateCookieInList(cookie, aCurrentTimeInUsec, paramsArray);
    }
  }
  // Update the database now if necessary.
  if (paramsArray) {
    uint32_t length;
    paramsArray->GetLength(&length);
    if (length) {
      DebugOnly<nsresult> rv = stmt->BindParameters(paramsArray);
      MOZ_ASSERT(NS_SUCCEEDED(rv));

      nsCOMPtr<mozIStoragePendingStatement> handle;
      rv = stmt->ExecuteAsync(mUpdateListener, getter_AddRefs(handle));
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  }
}

void CookiePersistentStorage::UpdateCookieInList(
    Cookie* aCookie, int64_t aLastAccessedInUSec,
    mozIStorageBindingParamsArray* aParamsArray) {
  MOZ_ASSERT(aCookie);

  // udpate the lastAccessedInUSec timestamp
  aCookie->SetLastAccessedInUSec(aLastAccessedInUSec);

  // if it's a non-session cookie, update it in the db too
  if (!aCookie->IsSession() && aParamsArray) {
    // Create our params holder.
    nsCOMPtr<mozIStorageBindingParams> params;
    aParamsArray->NewBindingParams(getter_AddRefs(params));

    // Bind our parameters.
    DebugOnly<nsresult> rv =
        params->BindInt64ByName("lastAccessed"_ns, aLastAccessedInUSec);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    rv = params->BindUTF8StringByName("name"_ns, aCookie->Name());
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    rv = params->BindUTF8StringByName("host"_ns, aCookie->Host());
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    rv = params->BindUTF8StringByName("path"_ns, aCookie->Path());
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    nsAutoCString suffix;
    aCookie->OriginAttributesRef().CreateSuffix(suffix);
    rv = params->BindUTF8StringByName("originAttributes"_ns, suffix);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    // Add our bound parameters to the array.
    rv = aParamsArray->AddParams(params);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }
}

void CookiePersistentStorage::DeleteFromDB(
    mozIStorageBindingParamsArray* aParamsArray) {
  uint32_t length;
  aParamsArray->GetLength(&length);
  if (length) {
    DebugOnly<nsresult> rv = mStmtDelete->BindParameters(aParamsArray);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    nsCOMPtr<mozIStoragePendingStatement> handle;
    rv = mStmtDelete->ExecuteAsync(mRemoveListener, getter_AddRefs(handle));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }
}

// nsIAsyncShutdownBlocker

NS_IMETHODIMP
CookiePersistentStorage::BlockShutdown(nsIAsyncShutdownClient* aClient) {
  Close();
  return NS_OK;
}

NS_IMETHODIMP
CookiePersistentStorage::GetName(nsAString& aName) {
  aName.AssignLiteral("CookiePersistentStorage: cookies.sqlite closing");
  return NS_OK;
}

NS_IMETHODIMP
CookiePersistentStorage::GetState(nsIPropertyBag** aState) {
  *aState = nullptr;
  return NS_OK;
}

void CookiePersistentStorage::RemoveShutdownBlocker() {
  if (mShutdownBarrier) {
    mShutdownBarrier->RemoveBlocker(this);
    mShutdownBarrier = nullptr;
  }
}

void CookiePersistentStorage::Activate() {
  MOZ_ASSERT(!mThread, "already have a cookie thread");

  mStorageService = do_GetService("@mozilla.org/storage/service;1");
  MOZ_ASSERT(mStorageService);

  mTLDService = do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);
  MOZ_ASSERT(mTLDService);

  // Get our cookie file.
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(mCookieFile));
  if (NS_FAILED(rv)) {
    // We've already set up our CookieStorages appropriately; nothing more to
    // do.
    COOKIE_LOGSTRING(LogLevel::Warning,
                     ("InitCookieStorages(): couldn't get cookie file"));

    mInitializedDBConn = true;
    mInitialized = true;
    return;
  }

  mCookieFile->AppendNative(nsLiteralCString(COOKIES_FILE));

  nsCOMPtr<nsIAsyncShutdownService> svc = components::AsyncShutdown::Service();
  if (svc) {
    nsCOMPtr<nsIAsyncShutdownClient> client;
    svc->GetProfileBeforeChange(getter_AddRefs(client));
    if (client) {
      mShutdownBarrier = client;
      client->AddBlocker(this, NS_LITERAL_STRING_FROM_CSTRING(__FILE__),
                         __LINE__, u""_ns);
    }
  }

  // Create the placeholder URI on the main thread before dispatching to the
  // Cookie thread. NS_NewURI must not be called from background threads as it
  // triggers XPCOM service initialization.
  if (NS_FAILED(
          NS_NewURI(getter_AddRefs(mPlaceholderURI), "https://example.com"))) {
    MOZ_ASSERT_UNREACHABLE(
        "Failed to create placeholder URI for cookie validation");
    mInitializedDBConn = true;
    mInitialized = true;
    return;
  }

  NS_ENSURE_SUCCESS_VOID(NS_NewNamedThread("Cookie", getter_AddRefs(mThread)));

  RefPtr<CookiePersistentStorage> self = this;
  nsCOMPtr<nsIRunnable> runnable =
      NS_NewRunnableFunction("CookiePersistentStorage::Activate", [self] {
        MonitorAutoLock lock(self->mMonitor);

        // Attempt to open and read the database. If TryInitDB() returns
        // RESULT_RETRY, do so.
        OpenDBResult result = self->TryInitDB(false);
        if (result == RESULT_RETRY) {
          // Database may be corrupt. Synchronously close the connection, clean
          // up the default CookieStorage, and try again.
          COOKIE_LOGSTRING(LogLevel::Warning,
                           ("InitCookieStorages(): retrying TryInitDB()"));
          self->CleanupCachedStatements();
          self->CleanupDBConnection();
          result = self->TryInitDB(true);
          if (result == RESULT_RETRY) {
            // We're done. Change the code to failure so we clean up below.
            result = RESULT_FAILURE;
          }
        }

        if (result == RESULT_FAILURE) {
          COOKIE_LOGSTRING(
              LogLevel::Warning,
              ("InitCookieStorages(): TryInitDB() failed, closing connection"));

          // Connection failure is unrecoverable. Clean up our connection. We
          // can run fine without persistent storage -- e.g. if there's no
          // profile.
          self->CleanupCachedStatements();
          self->CleanupDBConnection();

          // No need to initialize mDBConn
          self->mInitializedDBConn = true;
        }

        self->mInitialized = true;

        NS_DispatchToMainThread(
            NS_NewRunnableFunction("CookiePersistentStorage::InitDBConn",
                                   [self] { self->InitDBConn(); }));
        self->mMonitor.Notify();
      });

  mThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
}

/* Attempt to open and read the database. If 'aRecreateDB' is true, try to
 * move the existing database file out of the way and create a new one.
 *
 * @returns RESULT_OK if opening or creating the database succeeded;
 *          RESULT_RETRY if the database cannot be opened, is corrupt, or some
 *          other failure occurred that might be resolved by recreating the
 *          database; or RESULT_FAILED if there was an unrecoverable error and
 *          we must run without a database.
 *
 * If RESULT_RETRY or RESULT_FAILED is returned, the caller should perform
 * cleanup of the default CookieStorage.
 */
CookiePersistentStorage::OpenDBResult CookiePersistentStorage::TryInitDB(
    bool aRecreateDB) {
  NS_ASSERTION(!mDBConn, "nonnull mDBConn");
  NS_ASSERTION(!mStmtInsert, "nonnull mStmtInsert");
  NS_ASSERTION(!mInsertListener, "nonnull mInsertListener");
  NS_ASSERTION(!mSyncConn, "nonnull mSyncConn");
  NS_ASSERTION(NS_GetCurrentThread() == mThread, "non cookie thread");

  // Ditch an existing db, if we've been told to (i.e. it's corrupt). We don't
  // want to delete it outright, since it may be useful for debugging purposes,
  // so we move it out of the way.
  nsresult rv;
  if (aRecreateDB) {
    nsCOMPtr<nsIFile> backupFile;
    mCookieFile->Clone(getter_AddRefs(backupFile));
    rv = backupFile->MoveToNative(nullptr,
                                  nsLiteralCString(COOKIES_FILE ".bak"));
    NS_ENSURE_SUCCESS(rv, RESULT_FAILURE);
  }

  // This block provides scope for the Telemetry AutoTimer
  {
    auto timer = glean::network_cookies::sqlite_open_readahead.Measure();
    ReadAheadFile(mCookieFile);

    // open a connection to the cookie database, and only cache our connection
    // and statements upon success. The connection is opened unshared to
    // eliminate cache contention between the main and background threads.
    rv = mStorageService->OpenUnsharedDatabase(
        mCookieFile, mozIStorageService::CONNECTION_DEFAULT,
        getter_AddRefs(mSyncConn));
    if (NS_FAILED(rv)) {
      const char* errorName = mozilla::GetStaticErrorName(rv);
      glean::network_cookies::open_error
          .Get(nsDependentCString(errorName ? errorName : "unknown"))
          .Add(1);
      if (rv == NS_ERROR_FILE_NO_DEVICE_SPACE ||
          rv == NS_ERROR_FILE_ACCESS_DENIED) {
        return RESULT_FAILURE;
      }
      return RESULT_RETRY;
    }
  }

  auto guard = MakeScopeExit([&] { mSyncConn = nullptr; });

  bool tableExists = false;
  mSyncConn->TableExists("moz_cookies"_ns, &tableExists);
  if (!tableExists) {
    rv = CreateTable();
    NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

  } else {
    // table already exists; check the schema version before reading
    int32_t dbSchemaVersion;
    rv = mSyncConn->GetSchemaVersion(&dbSchemaVersion);
    NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

    // Start a transaction for the whole migration block.
    mozStorageTransaction transaction(mSyncConn, true);

    // XXX Handle the error, bug 1696130.
    (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

    switch (dbSchemaVersion) {
      // Upgrading.
      // Every time you increment the database schema, you need to implement
      // the upgrading code from the previous version to the new one. If
      // migration fails for any reason, it's a bug -- so we return RESULT_RETRY
      // such that the original database will be saved, in the hopes that we
      // might one day see it and fix it.
      case 1: {
        // Add the lastAccessed column to the table.
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies ADD lastAccessed INTEGER"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);
      }
        // Fall through to the next upgrade.
        [[fallthrough]];

      case 2: {
        // Add the baseDomain column and index to the table.
        rv = mSyncConn->ExecuteSimpleSQL(
            "ALTER TABLE moz_cookies ADD baseDomain TEXT"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Compute the baseDomains for the table. This must be done eagerly
        // otherwise we won't be able to synchronously read in individual
        // domains on demand.
        const int64_t SCHEMA2_IDX_ID = 0;
        const int64_t SCHEMA2_IDX_HOST = 1;
        nsCOMPtr<mozIStorageStatement> select;
        rv = mSyncConn->CreateStatement("SELECT id, host FROM moz_cookies"_ns,
                                        getter_AddRefs(select));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        nsCOMPtr<mozIStorageStatement> update;
        rv = mSyncConn->CreateStatement(
            nsLiteralCString("UPDATE moz_cookies SET baseDomain = "
                             ":baseDomain WHERE id = :id"),
            getter_AddRefs(update));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        nsCString baseDomain;
        nsCString host;
        bool hasResult;
        while (true) {
          rv = select->ExecuteStep(&hasResult);
          NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

          if (!hasResult) {
            break;
          }

          int64_t id = select->AsInt64(SCHEMA2_IDX_ID);
          select->GetUTF8String(SCHEMA2_IDX_HOST, host);

          rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host,
                                                    baseDomain);
          NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

          mozStorageStatementScoper scoper(update);

          rv = update->BindUTF8StringByName("baseDomain"_ns, baseDomain);
          MOZ_ASSERT(NS_SUCCEEDED(rv));
          rv = update->BindInt64ByName("id"_ns, id);
          MOZ_ASSERT(NS_SUCCEEDED(rv));

          rv = update->ExecuteStep(&hasResult);
          NS_ENSURE_SUCCESS(rv, RESULT_RETRY);
        }

        // Create an index on baseDomain.
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "CREATE INDEX moz_basedomain ON moz_cookies (baseDomain)"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);
      }
        // Fall through to the next upgrade.
        [[fallthrough]];

      case 3: {
        // Add the creationTime column to the table, and create a unique index
        // on (name, host, path). Before we do this, we have to purge the table
        // of expired cookies such that we know that the (name, host, path)
        // index is truly unique -- otherwise we can't create the index. Note
        // that we can't just execute a statement to delete all rows where the
        // expiry column is in the past -- doing so would rely on the clock
        // (both now and when previous cookies were set) being monotonic.

        // Select the whole table, and order by the fields we're interested in.
        // This means we can simply do a linear traversal of the results and
        // check for duplicates as we go.
        const int64_t SCHEMA3_IDX_ID = 0;
        const int64_t SCHEMA3_IDX_NAME = 1;
        const int64_t SCHEMA3_IDX_HOST = 2;
        const int64_t SCHEMA3_IDX_PATH = 3;
        nsCOMPtr<mozIStorageStatement> select;
        rv = mSyncConn->CreateStatement(
            nsLiteralCString(
                "SELECT id, name, host, path FROM moz_cookies "
                "ORDER BY name ASC, host ASC, path ASC, expiry ASC"),
            getter_AddRefs(select));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        nsCOMPtr<mozIStorageStatement> deleteExpired;
        rv = mSyncConn->CreateStatement(
            "DELETE FROM moz_cookies WHERE id = :id"_ns,
            getter_AddRefs(deleteExpired));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Read the first row.
        bool hasResult;
        rv = select->ExecuteStep(&hasResult);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        if (hasResult) {
          nsCString name1;
          nsCString host1;
          nsCString path1;
          int64_t id1 = select->AsInt64(SCHEMA3_IDX_ID);
          select->GetUTF8String(SCHEMA3_IDX_NAME, name1);
          select->GetUTF8String(SCHEMA3_IDX_HOST, host1);
          select->GetUTF8String(SCHEMA3_IDX_PATH, path1);

          nsCString name2;
          nsCString host2;
          nsCString path2;
          while (true) {
            // Read the second row.
            rv = select->ExecuteStep(&hasResult);
            NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

            if (!hasResult) {
              break;
            }

            int64_t id2 = select->AsInt64(SCHEMA3_IDX_ID);
            select->GetUTF8String(SCHEMA3_IDX_NAME, name2);
            select->GetUTF8String(SCHEMA3_IDX_HOST, host2);
            select->GetUTF8String(SCHEMA3_IDX_PATH, path2);

            // If the two rows match in (name, host, path), we know the earlier
            // row has an earlier expiry time. Delete it.
            if (name1 == name2 && host1 == host2 && path1 == path2) {
              mozStorageStatementScoper scoper(deleteExpired);

              rv = deleteExpired->BindInt64ByName("id"_ns, id1);
              MOZ_ASSERT(NS_SUCCEEDED(rv));

              rv = deleteExpired->ExecuteStep(&hasResult);
              NS_ENSURE_SUCCESS(rv, RESULT_RETRY);
            }

            // Make the second row the first for the next iteration.
            name1 = name2;
            host1 = host2;
            path1 = path2;
            id1 = id2;
          }
        }

        // Add the creationTime column to the table.
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies ADD creationTime INTEGER"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Copy the id of each row into the new creationTime column.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("UPDATE moz_cookies SET creationTime = "
                             "(SELECT id WHERE id = moz_cookies.id)"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Create a unique index on (name, host, path) to allow fast lookup.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("CREATE UNIQUE INDEX moz_uniqueid "
                             "ON moz_cookies (name, host, path)"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);
      }
        // Fall through to the next upgrade.
        [[fallthrough]];

      case 4: {
        // We need to add appId/inBrowserElement, plus change a constraint on
        // the table (unique entries now include appId/inBrowserElement):
        // this requires creating a new table and copying the data to it.  We
        // then rename the new table to the old name.
        //
        // Why we made this change: appId/inBrowserElement allow "cookie jars"
        // for Firefox OS. We create a separate cookie namespace per {appId,
        // inBrowserElement}.  When upgrading, we convert existing cookies
        // (which imply we're on desktop/mobile) to use {0, false}, as that is
        // the only namespace used by a non-Firefox-OS implementation.

        // Rename existing table
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies RENAME TO moz_cookies_old"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Drop existing index (CreateTable will create new one for new table)
        rv = mSyncConn->ExecuteSimpleSQL("DROP INDEX moz_basedomain"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Create new table (with new fields and new unique constraint)
        rv = CreateTableForSchemaVersion5();
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Copy data from old table, using appId/inBrowser=0 for existing rows
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "INSERT INTO moz_cookies "
            "(baseDomain, appId, inBrowserElement, name, value, host, path, "
            "expiry,"
            " lastAccessed, creationTime, isSecure, isHttpOnly) "
            "SELECT baseDomain, 0, 0, name, value, host, path, expiry,"
            " lastAccessed, creationTime, isSecure, isHttpOnly "
            "FROM moz_cookies_old"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Drop old table
        rv = mSyncConn->ExecuteSimpleSQL("DROP TABLE moz_cookies_old"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 5"));
      }
        // Fall through to the next upgrade.
        [[fallthrough]];

      case 5: {
        // Change in the version: Replace the columns |appId| and
        // |inBrowserElement| by a single column |originAttributes|.
        //
        // Why we made this change: FxOS new security model (NSec) encapsulates
        // "appId/inIsolatedMozBrowser" in nsIPrincipal::originAttributes to
        // make it easier to modify the contents of this structure in the
        // future.
        //
        // We do the migration in several steps:
        // 1. Rename the old table.
        // 2. Create a new table.
        // 3. Copy data from the old table to the new table; convert appId and
        //    inBrowserElement to originAttributes in the meantime.

        // Rename existing table.
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies RENAME TO moz_cookies_old"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Drop existing index (CreateTable will create new one for new table).
        rv = mSyncConn->ExecuteSimpleSQL("DROP INDEX moz_basedomain"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Create new table with new fields and new unique constraint.
        rv = CreateTableForSchemaVersion6();
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Copy data from old table without the two deprecated columns appId and
        // inBrowserElement.
        nsCOMPtr<mozIStorageFunction> convertToOriginAttrs(
            new ConvertAppIdToOriginAttrsSQLFunction());
        NS_ENSURE_TRUE(convertToOriginAttrs, RESULT_RETRY);

        constexpr auto convertToOriginAttrsName =
            "CONVERT_TO_ORIGIN_ATTRIBUTES"_ns;

        rv = mSyncConn->CreateFunction(convertToOriginAttrsName, 2,
                                       convertToOriginAttrs);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "INSERT INTO moz_cookies "
            "(baseDomain, originAttributes, name, value, host, path, expiry,"
            " lastAccessed, creationTime, isSecure, isHttpOnly) "
            "SELECT baseDomain, "
            " CONVERT_TO_ORIGIN_ATTRIBUTES(appId, inBrowserElement),"
            " name, value, host, path, expiry, lastAccessed, creationTime, "
            " isSecure, isHttpOnly "
            "FROM moz_cookies_old"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        rv = mSyncConn->RemoveFunction(convertToOriginAttrsName);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Drop old table
        rv = mSyncConn->ExecuteSimpleSQL("DROP TABLE moz_cookies_old"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 6"));
      }
        [[fallthrough]];

      case 6: {
        // We made a mistake in schema version 6. We cannot remove expected
        // columns of any version (checked in the default case) from cookie
        // database, because doing this would destroy the possibility of
        // downgrading database.
        //
        // This version simply restores appId and inBrowserElement columns in
        // order to fix downgrading issue even though these two columns are no
        // longer used in the latest schema.
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies ADD appId INTEGER DEFAULT 0;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies ADD inBrowserElement INTEGER DEFAULT 0;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Compute and populate the values of appId and inBrwoserElement from
        // originAttributes.
        nsCOMPtr<mozIStorageFunction> setAppId(
            new SetAppIdFromOriginAttributesSQLFunction());
        NS_ENSURE_TRUE(setAppId, RESULT_RETRY);

        constexpr auto setAppIdName = "SET_APP_ID"_ns;

        rv = mSyncConn->CreateFunction(setAppIdName, 1, setAppId);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        nsCOMPtr<mozIStorageFunction> setInBrowser(
            new SetInBrowserFromOriginAttributesSQLFunction());
        NS_ENSURE_TRUE(setInBrowser, RESULT_RETRY);

        constexpr auto setInBrowserName = "SET_IN_BROWSER"_ns;

        rv = mSyncConn->CreateFunction(setInBrowserName, 1, setInBrowser);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "UPDATE moz_cookies SET appId = SET_APP_ID(originAttributes), "
            "inBrowserElement = SET_IN_BROWSER(originAttributes);"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        rv = mSyncConn->RemoveFunction(setAppIdName);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        rv = mSyncConn->RemoveFunction(setInBrowserName);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 7"));
      }
        [[fallthrough]];

      case 7: {
        // Remove the appId field from moz_cookies.
        //
        // Unfortunately sqlite doesn't support dropping columns using ALTER
        // TABLE, so we need to go through the procedure documented in
        // https://www.sqlite.org/lang_altertable.html.

        // Drop existing index
        rv = mSyncConn->ExecuteSimpleSQL("DROP INDEX moz_basedomain"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Create a new_moz_cookies table without the appId field.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("CREATE TABLE new_moz_cookies("
                             "id INTEGER PRIMARY KEY, "
                             "baseDomain TEXT, "
                             "originAttributes TEXT NOT NULL DEFAULT '', "
                             "name TEXT, "
                             "value TEXT, "
                             "host TEXT, "
                             "path TEXT, "
                             "expiry INTEGER, "
                             "lastAccessed INTEGER, "
                             "creationTime INTEGER, "
                             "isSecure INTEGER, "
                             "isHttpOnly INTEGER, "
                             "inBrowserElement INTEGER DEFAULT 0, "
                             "CONSTRAINT moz_uniqueid UNIQUE (name, host, "
                             "path, originAttributes)"
                             ")"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Move the data over.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("INSERT INTO new_moz_cookies ("
                             "id, "
                             "baseDomain, "
                             "originAttributes, "
                             "name, "
                             "value, "
                             "host, "
                             "path, "
                             "expiry, "
                             "lastAccessed, "
                             "creationTime, "
                             "isSecure, "
                             "isHttpOnly, "
                             "inBrowserElement "
                             ") SELECT "
                             "id, "
                             "baseDomain, "
                             "originAttributes, "
                             "name, "
                             "value, "
                             "host, "
                             "path, "
                             "expiry, "
                             "lastAccessed, "
                             "creationTime, "
                             "isSecure, "
                             "isHttpOnly, "
                             "inBrowserElement "
                             "FROM moz_cookies;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Drop the old table
        rv = mSyncConn->ExecuteSimpleSQL("DROP TABLE moz_cookies;"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Rename new_moz_cookies to moz_cookies.
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE new_moz_cookies RENAME TO moz_cookies;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Recreate our index.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("CREATE INDEX moz_basedomain ON moz_cookies "
                             "(baseDomain, originAttributes)"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 8"));
      }
        [[fallthrough]];

      case 8: {
        // Add the sameSite column to the table.
        rv = mSyncConn->ExecuteSimpleSQL(
            "ALTER TABLE moz_cookies ADD sameSite INTEGER"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 9"));
      }
        [[fallthrough]];

      case 9: {
        // Add the rawSameSite column to the table.
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies ADD rawSameSite INTEGER"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Copy the current sameSite value into rawSameSite.
        rv = mSyncConn->ExecuteSimpleSQL(
            "UPDATE moz_cookies SET rawSameSite = sameSite"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 10"));
      }
        [[fallthrough]];

      case 10: {
        // Rename existing table
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies RENAME TO moz_cookies_old"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Create a new moz_cookies table without the baseDomain field.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("CREATE TABLE moz_cookies("
                             "id INTEGER PRIMARY KEY, "
                             "originAttributes TEXT NOT NULL DEFAULT '', "
                             "name TEXT, "
                             "value TEXT, "
                             "host TEXT, "
                             "path TEXT, "
                             "expiry INTEGER, "
                             "lastAccessed INTEGER, "
                             "creationTime INTEGER, "
                             "isSecure INTEGER, "
                             "isHttpOnly INTEGER, "
                             "inBrowserElement INTEGER DEFAULT 0, "
                             "sameSite INTEGER DEFAULT 0, "
                             "rawSameSite INTEGER DEFAULT 0, "
                             "CONSTRAINT moz_uniqueid UNIQUE (name, host, "
                             "path, originAttributes)"
                             ")"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Move the data over.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("INSERT INTO moz_cookies ("
                             "id, "
                             "originAttributes, "
                             "name, "
                             "value, "
                             "host, "
                             "path, "
                             "expiry, "
                             "lastAccessed, "
                             "creationTime, "
                             "isSecure, "
                             "isHttpOnly, "
                             "inBrowserElement, "
                             "sameSite, "
                             "rawSameSite "
                             ") SELECT "
                             "id, "
                             "originAttributes, "
                             "name, "
                             "value, "
                             "host, "
                             "path, "
                             "expiry, "
                             "lastAccessed, "
                             "creationTime, "
                             "isSecure, "
                             "isHttpOnly, "
                             "inBrowserElement, "
                             "sameSite, "
                             "rawSameSite "
                             "FROM moz_cookies_old "
                             "WHERE baseDomain NOTNULL;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Drop the old table
        rv = mSyncConn->ExecuteSimpleSQL("DROP TABLE moz_cookies_old;"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // Drop the moz_basedomain index from the database (if it hasn't been
        // removed already by removing the table).
        rv = mSyncConn->ExecuteSimpleSQL(
            "DROP INDEX IF EXISTS moz_basedomain;"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 11"));
      }
        [[fallthrough]];

      case 11: {
        // Add the schemeMap column to the table.
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies ADD schemeMap INTEGER DEFAULT 0;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 12"));
      }
        [[fallthrough]];

      case 12: {
        // Add the isPartitionedAttributeSet column to the table.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("ALTER TABLE moz_cookies ADD "
                             "isPartitionedAttributeSet INTEGER DEFAULT 0;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        COOKIE_LOGSTRING(LogLevel::Debug,
                         ("Upgraded database to schema version 13"));

        [[fallthrough]];
      }

      case 13: {
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("UPDATE moz_cookies SET expiry = unixepoch() + "
                             "34560000 WHERE expiry > unixepoch() + 34560000"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        [[fallthrough]];
      }

      case 14: {
        nsCOMPtr<mozIStorageStatement> update;
        rv = mSyncConn->CreateStatement(
            nsLiteralCString("UPDATE moz_cookies SET sameSite = "
                             ":unsetValue WHERE sameSite = :laxValue AND "
                             "rawSameSite = :noneValue"),
            getter_AddRefs(update));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        mozStorageStatementScoper scoper(update);

        rv =
            update->BindInt32ByName("unsetValue"_ns, nsICookie::SAMESITE_UNSET);
        MOZ_ASSERT(NS_SUCCEEDED(rv));

        rv = update->BindInt32ByName("laxValue"_ns, nsICookie::SAMESITE_LAX);
        MOZ_ASSERT(NS_SUCCEEDED(rv));

        rv = update->BindInt32ByName("noneValue"_ns, nsICookie::SAMESITE_NONE);
        MOZ_ASSERT(NS_SUCCEEDED(rv));

        bool hasResult;
        rv = update->ExecuteStep(&hasResult);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_cookies DROP COLUMN rawSameSite;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        [[fallthrough]];
      }

      case 15: {
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("UPDATE moz_cookies SET expiry = expiry * 1000;"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        [[fallthrough]];
      }

      case 16: {
        // Add the updateTime column to the table.
        rv = mSyncConn->ExecuteSimpleSQL(
            nsLiteralCString("ALTER TABLE moz_cookies ADD updateTime INTEGER"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // OK so... this is tricky because we have to guess the creationTime
        rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
            "UPDATE moz_cookies SET updateTime = CAST(strftime('%s','now') AS "
            "INTEGER) * 1000000"));
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        // No more upgrades. Update the schema version.
        rv = mSyncConn->SetSchemaVersion(COOKIES_SCHEMA_VERSION);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        [[fallthrough]];
      }

      case COOKIES_SCHEMA_VERSION:
        break;

      case 0: {
        NS_WARNING("couldn't get schema version!");

        // the table may be usable; someone might've just clobbered the schema
        // version. we can treat this case like a downgrade using the codepath
        // below, by verifying the columns we care about are all there. for now,
        // re-set the schema version in the db, in case the checks succeed (if
        // they don't, we're dropping the table anyway).
        rv = mSyncConn->SetSchemaVersion(COOKIES_SCHEMA_VERSION);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);
      }
        // fall through to downgrade check
        [[fallthrough]];

      // downgrading.
      // if columns have been added to the table, we can still use the ones we
      // understand safely. if columns have been deleted or altered, just
      // blow away the table and start from scratch! if you change the way
      // a column is interpreted, make sure you also change its name so this
      // check will catch it.
      default: {
        // check if all the expected columns exist
        nsCOMPtr<mozIStorageStatement> stmt;
        rv = mSyncConn->CreateStatement(
            nsLiteralCString("SELECT "
                             "id, "
                             "originAttributes, "
                             "name, "
                             "value, "
                             "host, "
                             "path, "
                             "expiry, "
                             "lastAccessed, "
                             "creationTime, "
                             "isSecure, "
                             "isHttpOnly, "
                             "sameSite, "
                             "schemeMap, "
                             "isPartitionedAttributeSet "
                             "FROM moz_cookies"),
            getter_AddRefs(stmt));
        if (NS_SUCCEEDED(rv)) {
          break;
        }

        // our columns aren't there - drop the table!
        rv = mSyncConn->ExecuteSimpleSQL("DROP TABLE moz_cookies"_ns);
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

        rv = CreateTable();
        NS_ENSURE_SUCCESS(rv, RESULT_RETRY);
      } break;
    }
  }

  // if we deleted a corrupt db, don't attempt to import - return now
  if (aRecreateDB) {
    return RESULT_OK;
  }

  if (StaticPrefs::network_cookie_CHIPS_enabled() &&
      StaticPrefs::network_cookie_CHIPS_lastMigrateDatabase() <
          StaticPrefs::network_cookie_CHIPS_migrateDatabaseTarget()) {
    CookiePersistentStorage::MoveUnpartitionedChipsCookies();
  }

  // check whether to import or just read in the db
  if (tableExists) {
    return Read();
  }

  return RESULT_OK;
}

void CookiePersistentStorage::MoveUnpartitionedChipsCookies() {
  nsCOMPtr<mozIStorageFunction> fetchPartitionKeyFromOAs(
      new FetchPartitionKeyFromOAsSQLFunction());
  NS_ENSURE_TRUE_VOID(fetchPartitionKeyFromOAs);

  constexpr auto fetchPartitionKeyFromOAsName =
      "FETCH_PARTITIONKEY_FROM_OAS"_ns;

  nsresult rv = mSyncConn->CreateFunction(fetchPartitionKeyFromOAsName, 1,
                                          fetchPartitionKeyFromOAs);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<mozIStorageFunction> updateOAsWithPartitionHost(
      new UpdateOAsWithPartitionHostSQLFunction());
  NS_ENSURE_TRUE_VOID(updateOAsWithPartitionHost);

  constexpr auto updateOAsWithPartitionHostName =
      "UPDATE_OAS_WITH_PARTITION_HOST"_ns;

  rv = mSyncConn->CreateFunction(updateOAsWithPartitionHostName, 2,
                                 updateOAsWithPartitionHost);
  NS_ENSURE_SUCCESS_VOID(rv);

  // Move all cookies with the Partitioned attribute set into their first-party
  // partitioned storage by updating the origin attributes. Overwrite any
  // existing cookies that may already be there.
  rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
      "UPDATE OR REPLACE moz_cookies  "
      "SET originAttributes = UPDATE_OAS_WITH_PARTITION_HOST(originAttributes, "
      "host) "
      "WHERE FETCH_PARTITIONKEY_FROM_OAS(originAttributes) = '' "
      "AND isPartitionedAttributeSet = 1;"));
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = mSyncConn->RemoveFunction(fetchPartitionKeyFromOAsName);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = mSyncConn->RemoveFunction(updateOAsWithPartitionHostName);
  NS_ENSURE_SUCCESS_VOID(rv);
}

void CookiePersistentStorage::RebuildCorruptDB() {
  NS_ASSERTION(!mDBConn, "shouldn't have an open db connection");
  NS_ASSERTION(mCorruptFlag == CookiePersistentStorage::CLOSING_FOR_REBUILD,
               "should be in CLOSING_FOR_REBUILD state");

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();

  mCorruptFlag = CookiePersistentStorage::REBUILDING;

  COOKIE_LOGSTRING(LogLevel::Debug,
                   ("RebuildCorruptDB(): creating new database"));

  RefPtr<CookiePersistentStorage> self = this;
  nsCOMPtr<nsIRunnable> runnable =
      NS_NewRunnableFunction("RebuildCorruptDB.TryInitDB", [self] {
        // The database has been closed, and we're ready to rebuild. Open a
        // connection.
        OpenDBResult result = self->TryInitDB(true);

        nsCOMPtr<nsIRunnable> innerRunnable = NS_NewRunnableFunction(
            "RebuildCorruptDB.TryInitDBComplete", [self, result] {
              nsCOMPtr<nsIObserverService> os = services::GetObserverService();
              if (result != RESULT_OK) {
                // We're done. Reset our DB connection and statements, and
                // notify of closure.
                COOKIE_LOGSTRING(
                    LogLevel::Warning,
                    ("RebuildCorruptDB(): TryInitDB() failed with result %u",
                     result));
                self->CleanupCachedStatements();
                self->CleanupDBConnection();
                self->mCorruptFlag = CookiePersistentStorage::OK;
                if (os) {
                  os->NotifyObservers(nullptr, "cookie-db-closed", nullptr);
                }
                return;
              }

              // Notify observers that we're beginning the rebuild.
              if (os) {
                os->NotifyObservers(nullptr, "cookie-db-rebuilding", nullptr);
              }

              self->InitDBConnInternal();

              // Enumerate the hash, and add cookies to the params array.
              mozIStorageAsyncStatement* stmt = self->mStmtInsert;
              nsCOMPtr<mozIStorageBindingParamsArray> paramsArray;
              stmt->NewBindingParamsArray(getter_AddRefs(paramsArray));
              for (auto iter = self->mHostTable.Iter(); !iter.Done();
                   iter.Next()) {
                CookieEntry* entry = iter.Get();

                const CookieEntry::ArrayType& cookies = entry->GetCookies();
                for (CookieEntry::IndexType i = 0; i < cookies.Length(); ++i) {
                  Cookie* cookie = cookies[i];

                  if (!cookie->IsSession()) {
                    BindCookieParameters(paramsArray, CookieKey(entry), cookie);
                  }
                }
              }

              // Make sure we've got something to write. If we don't, we're
              // done.
              uint32_t length;
              paramsArray->GetLength(&length);
              if (length == 0) {
                COOKIE_LOGSTRING(
                    LogLevel::Debug,
                    ("RebuildCorruptDB(): nothing to write, rebuild complete"));
                self->mCorruptFlag = CookiePersistentStorage::OK;
                return;
              }

              self->MaybeStoreCookiesToDB(paramsArray);
            });
        NS_DispatchToMainThread(innerRunnable);
      });
  mThread->Dispatch(runnable, NS_DISPATCH_NORMAL);
}

void CookiePersistentStorage::HandleDBClosed() {
  COOKIE_LOGSTRING(LogLevel::Debug,
                   ("HandleDBClosed(): CookieStorage %p closed", this));

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();

  switch (mCorruptFlag) {
    case CookiePersistentStorage::OK: {
      // Database is healthy. Notify of closure.
      if (os) {
        os->NotifyObservers(nullptr, "cookie-db-closed", nullptr);
      }
      RemoveShutdownBlocker();
      break;
    }
    case CookiePersistentStorage::CLOSING_FOR_REBUILD: {
      // Our close finished. Start the rebuild, and notify of db closure later.
      RebuildCorruptDB();
      break;
    }
    case CookiePersistentStorage::REBUILDING: {
      // We encountered an error during rebuild, closed the database, and now
      // here we are. We already have a 'cookies.sqlite.bak' from the original
      // dead database; we don't want to overwrite it, so let's move this one to
      // 'cookies.sqlite.bak-rebuild'.
      nsCOMPtr<nsIFile> backupFile;
      mCookieFile->Clone(getter_AddRefs(backupFile));
      nsresult rv = backupFile->MoveToNative(
          nullptr, nsLiteralCString(COOKIES_FILE ".bak-rebuild"));

      COOKIE_LOGSTRING(LogLevel::Warning,
                       ("HandleDBClosed(): CookieStorage %p encountered error "
                        "rebuilding db; move to "
                        "'cookies.sqlite.bak-rebuild' gave rv 0x%" PRIx32,
                        this, static_cast<uint32_t>(rv)));
      if (os) {
        os->NotifyObservers(nullptr, "cookie-db-closed", nullptr);
      }
      RemoveShutdownBlocker();
      break;
    }
  }
}

CookiePersistentStorage::OpenDBResult CookiePersistentStorage::Read() {
  MOZ_ASSERT(NS_GetCurrentThread() == mThread);

  // Read in the data synchronously.
  // see IDX_NAME, etc. for parameter indexes
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv =
      mSyncConn->CreateStatement(nsLiteralCString("SELECT "
                                                  "name, "
                                                  "value, "
                                                  "host, "
                                                  "path, "
                                                  "expiry, "
                                                  "lastAccessed, "
                                                  "creationTime, "
                                                  "isSecure, "
                                                  "isHttpOnly, "
                                                  "originAttributes, "
                                                  "sameSite, "
                                                  "schemeMap, "
                                                  "isPartitionedAttributeSet, "
                                                  "updateTime "
                                                  "FROM moz_cookies"),
                                 getter_AddRefs(stmt));

  NS_ENSURE_SUCCESS(rv, RESULT_RETRY);

  if (NS_WARN_IF(!mReadArray.IsEmpty())) {
    mReadArray.Clear();
  }
  mReadArray.SetCapacity(kMaxNumberOfCookies);
  mCleanupArray.Clear();

  nsCString baseDomain;
  nsCString name;
  nsCString value;
  nsCString host;
  nsCString path;
  bool hasResult;
  while (true) {
    rv = stmt->ExecuteStep(&hasResult);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mReadArray.Clear();
      return RESULT_RETRY;
    }

    if (!hasResult) {
      break;
    }

    stmt->GetUTF8String(IDX_HOST, host);

    rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host, baseDomain);
    if (NS_FAILED(rv)) {
      COOKIE_LOGSTRING(LogLevel::Debug,
                       ("Read(): Ignoring invalid host '%s'", host.get()));
      continue;
    }

    nsAutoCString suffix;
    OriginAttributes attrs;
    stmt->GetUTF8String(IDX_ORIGIN_ATTRIBUTES, suffix);
    // If PopulateFromSuffix failed we just ignore the OA attributes
    // that we don't support
    (void)attrs.PopulateFromSuffix(suffix);

    UniquePtr<CookieStruct> cookieStruct = GetCookieFromRow(stmt);

    // Filter cookies with newly invalid hostnames (e.g. legacy DB values that
    // no longer pass URL parsing).
    nsCOMPtr<nsIURI> validatedUri;
    if (NS_FAILED(NS_MutateURI(mPlaceholderURI)
                      .SetHost(host)
                      .Finalize(validatedUri))) {
      COOKIE_LOGSTRING(
          LogLevel::Debug,
          ("Read(): Queueing cookie with newly invalid hostname '%s' for "
           "removal from DB",
           host.get()));
      CookieDomainTuple* cleanupTuple = mCleanupArray.AppendElement();
      cleanupTuple->key = CookieKey(baseDomain, attrs);
      cleanupTuple->originAttributes = attrs;
      cleanupTuple->cookie = Cookie::Create(*cookieStruct, attrs);
      continue;
    }

    // When the valueless_cookie pref is enabled, discard legacy nameless
    // cookies (name="") that were stored under the old behavior.
    // Under the new behavior these cookies are unreachable since new cookies
    // without '=' are stored with the token as name, not value.
    if (StaticPrefs::network_cookie_valueless_cookie() &&
        cookieStruct->name().IsEmpty()) {
      CookieDomainTuple* cleanupTuple = mCleanupArray.AppendElement();
      cleanupTuple->key = CookieKey(baseDomain, attrs);
      cleanupTuple->originAttributes = attrs;
      cleanupTuple->cookie = Cookie::Create(*cookieStruct, attrs);
      continue;
    }

    // Create the Cookie on the background thread. CreateValidated fixes up
    // any timestamps in the DB that are far in the future.
    RefPtr<Cookie> cookie = Cookie::CreateValidated(*cookieStruct, attrs);

    // Clean up invalid first-party partitioned cookies without the
    // 'Partitioned' attribute. Use Cookie::Create to preserve the original DB
    // timestamps for the deletion query.
    if (CookieCommons::IsFirstPartyPartitionedCookieWithoutCHIPS(
            cookie, baseDomain, attrs)) {
      CookieDomainTuple* cleanupTuple = mCleanupArray.AppendElement();
      cleanupTuple->key = CookieKey(baseDomain, attrs);
      cleanupTuple->originAttributes = attrs;
      cleanupTuple->cookie = Cookie::Create(*cookieStruct, attrs);
      continue;
    }

    MOZ_ASSERT(!cookie->IsSession());
    CookieDomainTuple* tuple = mReadArray.AppendElement();
    tuple->key = CookieKey(baseDomain, attrs);
    tuple->originAttributes = attrs;
    tuple->cookie = std::move(cookie);
  }

  COOKIE_LOGSTRING(LogLevel::Debug,
                   ("Read(): %zu cookies read", mReadArray.Length()));

  return RESULT_OK;
}

// Extract data from a single result row and create an Cookie.
UniquePtr<CookieStruct> CookiePersistentStorage::GetCookieFromRow(
    mozIStorageStatement* aRow) {
  nsCString name;
  nsCString value;
  nsCString host;
  nsCString path;
  DebugOnly<nsresult> rv = aRow->GetUTF8String(IDX_NAME, name);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = aRow->GetUTF8String(IDX_VALUE, value);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = aRow->GetUTF8String(IDX_HOST, host);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = aRow->GetUTF8String(IDX_PATH, path);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  int64_t expiryInMSec = aRow->AsInt64(IDX_EXPIRY_INMSEC);
  int64_t lastAccessedInUSec = aRow->AsInt64(IDX_LAST_ACCESSED_INUSEC);
  int64_t creationTimeInUSec = aRow->AsInt64(IDX_CREATION_TIME_INUSEC);
  int64_t updateTimeInUSec = aRow->AsInt64(IDX_UPDATE_TIME_INUSEC);
  bool isSecure = 0 != aRow->AsInt32(IDX_SECURE);
  bool isHttpOnly = 0 != aRow->AsInt32(IDX_HTTPONLY);
  int32_t sameSite = aRow->AsInt32(IDX_SAME_SITE);
  int32_t schemeMap = aRow->AsInt32(IDX_SCHEME_MAP);
  bool isPartitionedAttributeSet =
      0 != aRow->AsInt32(IDX_PARTITIONED_ATTRIBUTE_SET);

  // Create a new constCookie and assign the data.
  return MakeUnique<CookieStruct>(
      name, value, host, path, expiryInMSec, lastAccessedInUSec,
      creationTimeInUSec, updateTimeInUSec, isHttpOnly, false, isSecure,
      isPartitionedAttributeSet, sameSite,
      static_cast<nsICookie::schemeType>(schemeMap));
}

void CookiePersistentStorage::EnsureInitialized() {
  MOZ_ASSERT(NS_IsMainThread());

  TimeStamp startBlockTime;

  if (!mInitialized) {
    startBlockTime = TimeStamp::Now();
    MonitorAutoLock lock(mMonitor);

    while (!mInitialized) {
      mMonitor.Wait();
    }
  } else if (!mEndInitDBConn.IsNull()) {
    // We didn't block main thread, and here comes the first cookie request.
    // Collect how close we're going to block main thread.
    TimeStamp now = TimeStamp::Now();
    mozilla::glean::networking::sqlite_cookies_time_to_block_main_thread
        .AccumulateRawDuration(now - mEndInitDBConn);
    PROFILER_MARKER_UNTYPED("sqlite_cookies_time_to_block_main_thread", NETWORK,
                            MarkerTiming::Interval(mEndInitDBConn, now));
    // Nullify the timestamp so we don't accumulate this telemetry probe again.
    mEndInitDBConn = TimeStamp();
    return;
  } else if (mInitializedDBConn) {
    // Already fully initialized; telemetry was recorded on a prior call.
    return;
  } else {
    // A request comes while we finished cookie thread task and InitDBConn is
    // on the way from cookie thread to main thread. We run it synchronously
    // below, blocking the main thread.
    startBlockTime = TimeStamp::Now();
  }

  // Main thread is blocked: either we just waited on the monitor or the
  // dispatched InitDBConn runnable hasn't fired yet. Run it synchronously
  // (the dispatched runnable will no-op via InitDBConn's own guard) and
  // record the full blocking duration.
  InitDBConn();
  mEndInitDBConn = TimeStamp();

  TimeStamp endBlockTime = TimeStamp::Now();
  mozilla::glean::networking::sqlite_cookies_block_main_thread
      .AccumulateRawDuration(endBlockTime - startBlockTime);
  mozilla::glean::networking::sqlite_cookies_time_to_block_main_thread
      .AccumulateRawDuration(TimeDuration::Zero());
  PROFILER_MARKER_UNTYPED("sqlite_cookies_block_main_thread", NETWORK,
                          MarkerTiming::Interval(startBlockTime, endBlockTime));
}

void CookiePersistentStorage::InitDBConn() {
  MOZ_ASSERT(NS_IsMainThread());

  // We should skip InitDBConn if we close profile during initializing
  // CookieStorages and then InitDBConn is called after we close the
  // CookieStorages.
  if (!mInitialized || mInitializedDBConn) {
    return;
  }

  TimeStamp startInitDBConn = TimeStamp::Now();
  auto markerGuard = MakeScopeExit([&] {
    PROFILER_MARKER_UNTYPED(
        "CookiePersistentStorage::InitDBConn", NETWORK,
        MarkerTiming::IntervalUntilNowFrom(startInitDBConn));
  });

  // Cookies rejected during Read() on the Cookie thread (invalid hostname or
  // invalid CHIPS attribution) need to be removed from the DB now that the
  // connection is ready. Cookies are already fully constructed.
  nsTArray<RefPtr<Cookie>> cleanupCookies;
  for (auto& tuple : mCleanupArray) {
    COOKIE_LOGSTRING(LogLevel::Debug,
                     ("InitDBConn(): Removing invalid cookie from db: '%s'",
                      tuple.cookie->Host().get()));
    cleanupCookies.AppendElement(tuple.cookie);
  }
  mCleanupArray.Clear();

  // Cookies are already created and validated on the Cookie background thread.
  for (auto& tuple : mReadArray) {
    MOZ_ASSERT(!tuple.cookie->IsSession());

    AddCookieToList(tuple.key.mBaseDomain, tuple.key.mOriginAttributes,
                    tuple.cookie);
  }

  if (NS_FAILED(InitDBConnInternal())) {
    COOKIE_LOGSTRING(LogLevel::Warning,
                     ("InitDBConn(): retrying InitDBConnInternal()"));
    CleanupCachedStatements();
    CleanupDBConnection();
    if (NS_FAILED(InitDBConnInternal())) {
      COOKIE_LOGSTRING(
          LogLevel::Warning,
          ("InitDBConn(): InitDBConnInternal() failed, closing connection"));

      // Game over, clean the connections.
      CleanupCachedStatements();
      CleanupDBConnection();
    }
  }
  mInitializedDBConn = true;

  COOKIE_LOGSTRING(LogLevel::Debug,
                   ("InitDBConn(): mInitializedDBConn = true"));
  mEndInitDBConn = TimeStamp::Now();

  for (const auto& cookie : cleanupCookies) {
    RemoveCookieFromDB(*cookie);
  }

  // We will have migrated CHIPS cookies if the pref is set, and .unset it
  // to prevent duplicated work. This has to happen in the main thread though,
  // so we waited to this point.
  if (StaticPrefs::network_cookie_CHIPS_enabled()) {
    Preferences::SetUint(
        "network.cookie.CHIPS.lastMigrateDatabase",
        StaticPrefs::network_cookie_CHIPS_migrateDatabaseTarget());
  }

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    os->NotifyObservers(nullptr, "cookie-db-read", nullptr);
    mReadArray.Clear();
  }

  if (StaticPrefs::network_cookie_validation_lastEpoch() <
      StaticPrefs::network_cookie_validation_epoch()) {
    nsCOMPtr<nsIRunnable> idleRunnable = NS_NewRunnableFunction(
        "CookiePersistentStorage::RecordValidationTelemetry",
        [self = RefPtr{this}]() { self->RecordValidationTelemetry(); });
    (void)NS_DispatchToMainThreadQueue(do_AddRef(idleRunnable),
                                       EventQueuePriority::Idle);
  }
}

nsresult CookiePersistentStorage::InitDBConnInternal() {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = mStorageService->OpenUnsharedDatabase(
      mCookieFile, mozIStorageService::CONNECTION_DEFAULT,
      getter_AddRefs(mDBConn));
  NS_ENSURE_SUCCESS(rv, rv);

  // Set up our listeners.
  mInsertListener = new InsertCookieDBListener(this);
  mUpdateListener = new UpdateCookieDBListener(this);
  mRemoveListener = new RemoveCookieDBListener(this);
  mCloseListener = new CloseCookieDBListener(this);

  // Grow cookie db in 512KB increments
  mDBConn->SetGrowthIncrement(512 * 1024, ""_ns);

  // make operations on the table asynchronous, for performance
  mDBConn->ExecuteSimpleSQL("PRAGMA synchronous = OFF"_ns);

  // Use write-ahead-logging for performance. We cap the autocheckpoint limit at
  // 16 pages (around 500KB).
  mDBConn->ExecuteSimpleSQL(nsLiteralCString(MOZ_STORAGE_UNIQUIFY_QUERY_STR
                                             "PRAGMA journal_mode = WAL"));
  mDBConn->ExecuteSimpleSQL("PRAGMA wal_autocheckpoint = 16"_ns);

  // cache frequently used statements (for insertion, deletion, and updating)
  rv = mDBConn->CreateAsyncStatement(
      nsLiteralCString("INSERT INTO moz_cookies ("
                       "originAttributes, "
                       "name, "
                       "value, "
                       "host, "
                       "path, "
                       "expiry, "
                       "lastAccessed, "
                       "creationTime, "
                       "isSecure, "
                       "isHttpOnly, "
                       "sameSite, "
                       "schemeMap, "
                       "isPartitionedAttributeSet, "
                       "updateTime "
                       ") VALUES ("
                       ":originAttributes, "
                       ":name, "
                       ":value, "
                       ":host, "
                       ":path, "
                       ":expiry, "
                       ":lastAccessed, "
                       ":creationTime, "
                       ":isSecure, "
                       ":isHttpOnly, "
                       ":sameSite, "
                       ":schemeMap, "
                       ":isPartitionedAttributeSet, "
                       ":updateTime "
                       ")"),
      getter_AddRefs(mStmtInsert));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->CreateAsyncStatement(
      nsLiteralCString("DELETE FROM moz_cookies "
                       "WHERE name = :name AND host = :host AND path = :path "
                       "AND originAttributes = :originAttributes"),
      getter_AddRefs(mStmtDelete));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDBConn->CreateAsyncStatement(
      nsLiteralCString("UPDATE moz_cookies SET lastAccessed = :lastAccessed "
                       "WHERE name = :name AND host = :host AND path = :path "
                       "AND originAttributes = :originAttributes"),
      getter_AddRefs(mStmtUpdate));
  return rv;
}

// Sets the schema version and creates the moz_cookies table.
nsresult CookiePersistentStorage::CreateTableWorker(const char* aName) {
  // Create the table.
  // We default originAttributes to empty string: this is so if users revert to
  // an older Firefox version that doesn't know about this field, any cookies
  // set will still work once they upgrade back.
  nsAutoCString command("CREATE TABLE ");
  command.Append(aName);
  command.AppendLiteral(
      " ("
      "id INTEGER PRIMARY KEY, "
      "originAttributes TEXT NOT NULL DEFAULT '', "
      "name TEXT, "
      "value TEXT, "
      "host TEXT, "
      "path TEXT, "
      "expiry INTEGER, "
      "lastAccessed INTEGER, "
      "creationTime INTEGER, "
      "isSecure INTEGER, "
      "isHttpOnly INTEGER, "
      "inBrowserElement INTEGER DEFAULT 0, "
      "sameSite INTEGER DEFAULT 0, "
      "schemeMap INTEGER DEFAULT 0, "
      "isPartitionedAttributeSet INTEGER DEFAULT 0, "
      "updateTime INTEGER, "
      "CONSTRAINT moz_uniqueid UNIQUE (name, host, path, originAttributes)"
      ")");
  return mSyncConn->ExecuteSimpleSQL(command);
}

// Sets the schema version and creates the moz_cookies table.
nsresult CookiePersistentStorage::CreateTable() {
  // Set the schema version, before creating the table.
  nsresult rv = mSyncConn->SetSchemaVersion(COOKIES_SCHEMA_VERSION);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = CreateTableWorker("moz_cookies");
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

// Sets the schema version and creates the moz_cookies table.
nsresult CookiePersistentStorage::CreateTableForSchemaVersion6() {
  // Set the schema version, before creating the table.
  nsresult rv = mSyncConn->SetSchemaVersion(6);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Create the table.
  // We default originAttributes to empty string: this is so if users revert to
  // an older Firefox version that doesn't know about this field, any cookies
  // set will still work once they upgrade back.
  rv = mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
      "CREATE TABLE moz_cookies ("
      "id INTEGER PRIMARY KEY, "
      "baseDomain TEXT, "
      "originAttributes TEXT NOT NULL DEFAULT '', "
      "name TEXT, "
      "value TEXT, "
      "host TEXT, "
      "path TEXT, "
      "expiry INTEGER, "
      "lastAccessed INTEGER, "
      "creationTime INTEGER, "
      "isSecure INTEGER, "
      "isHttpOnly INTEGER, "
      "CONSTRAINT moz_uniqueid UNIQUE (name, host, path, originAttributes)"
      ")"));
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Create an index on baseDomain.
  return mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
      "CREATE INDEX moz_basedomain ON moz_cookies (baseDomain, "
      "originAttributes)"));
}

// Sets the schema version and creates the moz_cookies table.
nsresult CookiePersistentStorage::CreateTableForSchemaVersion5() {
  // Set the schema version, before creating the table.
  nsresult rv = mSyncConn->SetSchemaVersion(5);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Create the table. We default appId/inBrowserElement to 0: this is so if
  // users revert to an older Firefox version that doesn't know about these
  // fields, any cookies set will still work once they upgrade back.
  rv = mSyncConn->ExecuteSimpleSQL(
      nsLiteralCString("CREATE TABLE moz_cookies ("
                       "id INTEGER PRIMARY KEY, "
                       "baseDomain TEXT, "
                       "appId INTEGER DEFAULT 0, "
                       "inBrowserElement INTEGER DEFAULT 0, "
                       "name TEXT, "
                       "value TEXT, "
                       "host TEXT, "
                       "path TEXT, "
                       "expiry INTEGER, "
                       "lastAccessed INTEGER, "
                       "creationTime INTEGER, "
                       "isSecure INTEGER, "
                       "isHttpOnly INTEGER, "
                       "CONSTRAINT moz_uniqueid UNIQUE (name, host, path, "
                       "appId, inBrowserElement)"
                       ")"));
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Create an index on baseDomain.
  return mSyncConn->ExecuteSimpleSQL(nsLiteralCString(
      "CREATE INDEX moz_basedomain ON moz_cookies (baseDomain, "
      "appId, "
      "inBrowserElement)"));
}

nsresult CookiePersistentStorage::RunInTransaction(
    nsICookieTransactionCallback* aCallback) {
  if (NS_WARN_IF(!mDBConn)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mozStorageTransaction transaction(mDBConn, true);

  // XXX Handle the error, bug 1696130.
  (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

  if (NS_FAILED(aCallback->Callback())) {
    (void)transaction.Rollback();
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

// purges expired and old cookies in a batch operation.
already_AddRefed<nsIArray> CookiePersistentStorage::PurgeCookies(
    int64_t aCurrentTimeInUsec, uint16_t aMaxNumberOfCookies,
    int64_t aCookiePurgeAge) {
  // Create a params array to batch the removals. This is OK here because
  // all the removals are in order, and there are no interleaved additions.
  nsCOMPtr<mozIStorageBindingParamsArray> paramsArray;
  if (mDBConn) {
    mStmtDelete->NewBindingParamsArray(getter_AddRefs(paramsArray));
  }

  RefPtr<CookiePersistentStorage> self = this;

  return PurgeCookiesWithCallbacks(
      aCurrentTimeInUsec, aMaxNumberOfCookies, aCookiePurgeAge,
      [paramsArray, self](const CookieListIter& aIter) {
        self->PrepareCookieRemoval(*aIter.Cookie(), paramsArray);
        self->RemoveCookieFromListInternal(aIter);
      },
      [paramsArray, self]() {
        if (paramsArray) {
          self->DeleteFromDB(paramsArray);
        }
      });
}

void CookiePersistentStorage::CollectCookieJarSizeData() {
  COOKIE_LOGSTRING(LogLevel::Debug,
                   ("CookiePersistentStorage::CollectCookieJarSizeData"));

  uint32_t sumPartitioned = 0;
  uint32_t sumUnpartitioned = 0;
  for (const auto& cookieEntry : mHostTable) {
    if (cookieEntry.IsPartitioned()) {
      uint16_t cePartitioned = cookieEntry.GetCookies().Length();
      sumPartitioned += cePartitioned;
      mozilla::glean::networking::cookie_count_part_by_key
          .AccumulateSingleSample(cePartitioned);
    } else {
      uint16_t ceUnpartitioned = cookieEntry.GetCookies().Length();
      sumUnpartitioned += ceUnpartitioned;
      mozilla::glean::networking::cookie_count_unpart_by_key
          .AccumulateSingleSample(ceUnpartitioned);
    }
  }

  mozilla::glean::networking::cookie_count_total.AccumulateSingleSample(
      mCookieCount);
  mozilla::glean::networking::cookie_count_partitioned.AccumulateSingleSample(
      sumPartitioned);
  mozilla::glean::networking::cookie_count_unpartitioned.AccumulateSingleSample(
      sumUnpartitioned);
}

// NOTE: if you modify this function and want it to run again on next startup
// for existing profiles, bump network.cookie.validation.epoch.
void CookiePersistentStorage::RecordValidationTelemetry() {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<CookieService> cs = CookieService::GetSingleton();
  if (!cs) {
    // We are shutting down, or something bad is happening.
    return;
  }

  struct CookieToAddOrRemove {
    nsCString mBaseDomain;
    OriginAttributes mOriginAttributes;
    RefPtr<Cookie> mCookie;
  };

  nsTArray<CookieToAddOrRemove> listToAdd;
  nsTArray<CookieToAddOrRemove> listToRemove;

  for (const auto& entry : mHostTable) {
    const CookieEntry::ArrayType& cookies = entry.GetCookies();
    for (CookieEntry::IndexType i = 0; i < cookies.Length(); ++i) {
      Cookie* cookie = cookies[i];

      RefPtr<CookieValidation> validation =
          CookieValidation::Validate(cookie->ToIPC());
      mozilla::glean::networking::cookie_db_validation
          .Get(ValidationErrorToLabel(validation->Result()))
          .Add(1);

      // We are unable to recover from all the possible errors. Let's fix the
      // most common ones.
      switch (validation->Result()) {
        case nsICookieValidation::eRejectedNoneRequiresSecure: {
          RefPtr<Cookie> newCookie =
              Cookie::Create(cookie->ToIPC(), entry.mOriginAttributes);
          MOZ_ASSERT(newCookie);

          newCookie->SetSameSite(nsICookie::SAMESITE_UNSET);
          newCookie->SetCreationTimeInUSec(cookie->CreationTimeInUSec());

          listToAdd.AppendElement(CookieToAddOrRemove{
              entry.mBaseDomain, entry.mOriginAttributes, newCookie});
          break;
        }

        case nsICookieValidation::eRejectedAttributeExpiryOversize: {
          RefPtr<Cookie> newCookie =
              Cookie::Create(cookie->ToIPC(), entry.mOriginAttributes);
          MOZ_ASSERT(newCookie);

          int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;

          newCookie->SetExpiryInMSec(CookieCommons::MaybeCapExpiry(
              currentTimeInMSec, cookie->ExpiryInMSec()));
          newCookie->SetCreationTimeInUSec(cookie->CreationTimeInUSec());

          listToAdd.AppendElement(CookieToAddOrRemove{
              entry.mBaseDomain, entry.mOriginAttributes, newCookie});
          break;
        }

        case nsICookieValidation::eRejectedEmptyNameAndValue:
          [[fallthrough]];
        case nsICookieValidation::eRejectedInvalidCharName:
          [[fallthrough]];
        case nsICookieValidation::eRejectedInvalidCharValue:
          listToRemove.AppendElement(CookieToAddOrRemove{
              entry.mBaseDomain, entry.mOriginAttributes, cookie});
          break;

        default:
          // Nothing to do here.
          break;
      }
    }
  }

  for (CookieToAddOrRemove& data : listToAdd) {
    AddCookie(nullptr, data.mBaseDomain, data.mOriginAttributes, data.mCookie,
              data.mCookie->CreationTimeInUSec(), nullptr, VoidCString(), true,
              !data.mOriginAttributes.mPartitionKey.IsEmpty(), nullptr,
              nullptr);
  }

  for (CookieToAddOrRemove& data : listToRemove) {
    RemoveCookie(data.mBaseDomain, data.mOriginAttributes, data.mCookie->Host(),
                 data.mCookie->Name(), data.mCookie->Path(),
                 /* is http: */ true, nullptr);
  }

  // Only mark this epoch as complete if no fixes were needed. If we did fix
  // cookies, re-scan on next startup to confirm the database is now stable.
  if (listToAdd.IsEmpty() && listToRemove.IsEmpty()) {
    Preferences::SetUint("network.cookie.validation.lastEpoch",
                         StaticPrefs::network_cookie_validation_epoch());
  }

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    os->NotifyObservers(nullptr, "cookies-validated", nullptr);
  }
}

}  // namespace net
}  // namespace mozilla

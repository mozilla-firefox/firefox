/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <limits.h>
#include <stdio.h>

#include "nsError.h"
#include "nsThreadUtils.h"
#include "nsIClassInfoImpl.h"
#include "Variant.h"

#include "mozIStorageError.h"

#include "mozStorageBindingParams.h"
#include "mozStorageConnection.h"
#include "mozStorageStatementJSHelper.h"
#include "mozStoragePrivateHelpers.h"
#include "mozStorageStatementParams.h"
#include "mozStorageStatementRow.h"
#include "mozStorageStatement.h"

#include "mozilla/Logging.h"
#include "mozilla/Printf.h"
#include "mozilla/ProfilerLabels.h"

extern mozilla::LazyLogModule gStorageLog;

namespace mozilla {
namespace storage {

////////////////////////////////////////////////////////////////////////////////
//// nsIClassInfo

NS_IMPL_CI_INTERFACE_GETTER(Statement, mozIStorageStatement,
                            mozIStorageBaseStatement, mozIStorageBindingParams,
                            mozIStorageValueArray,
                            mozilla::storage::StorageBaseStatementInternal)

class StatementClassInfo : public nsIClassInfo {
 public:
  constexpr StatementClassInfo() = default;

  NS_DECL_ISUPPORTS_INHERITED

  NS_IMETHOD
  GetInterfaces(nsTArray<nsIID>& _array) override {
    return NS_CI_INTERFACE_GETTER_NAME(Statement)(_array);
  }

  NS_IMETHOD
  GetScriptableHelper(nsIXPCScriptable** _helper) override {
    static StatementJSHelper sJSHelper;
    *_helper = &sJSHelper;
    return NS_OK;
  }

  NS_IMETHOD
  GetContractID(nsACString& aContractID) override {
    aContractID.SetIsVoid(true);
    return NS_OK;
  }

  NS_IMETHOD
  GetClassDescription(nsACString& aDesc) override {
    aDesc.SetIsVoid(true);
    return NS_OK;
  }

  NS_IMETHOD
  GetClassID(nsCID** _id) override {
    *_id = nullptr;
    return NS_OK;
  }

  NS_IMETHOD
  GetFlags(uint32_t* _flags) override {
    *_flags = 0;
    return NS_OK;
  }

  NS_IMETHOD
  GetClassIDNoAlloc(nsCID* _cid) override { return NS_ERROR_NOT_AVAILABLE; }
};

NS_IMETHODIMP_(MozExternalRefCountType) StatementClassInfo::AddRef() {
  return 2;
}
NS_IMETHODIMP_(MozExternalRefCountType) StatementClassInfo::Release() {
  return 1;
}
NS_IMPL_QUERY_INTERFACE(StatementClassInfo, nsIClassInfo)

static StatementClassInfo sStatementClassInfo;

////////////////////////////////////////////////////////////////////////////////
//// Statement

Statement::Statement()
    : mDBStatement(nullptr),
      mParamCount(0),
      mResultColumnCount(0),
      mExecuting(false),
      mQueryStatusRecorded(false),
      mHasExecuted(false) {}

nsresult Statement::initialize(Connection* aDBConnection,
                               sqlite3* aNativeConnection,
                               const nsACString& aSQLStatement) {
  MOZ_ASSERT(aDBConnection, "No database connection given!");
  MOZ_ASSERT(aDBConnection->isConnectionReadyOnThisThread(),
             "Database connection should be valid");
  MOZ_ASSERT(!mDBStatement, "Statement already initialized!");
  MOZ_ASSERT(aNativeConnection, "No native connection given!");

  int srv = aDBConnection->prepareStatement(
      aNativeConnection, PromiseFlatCString(aSQLStatement), &mDBStatement);
  if (srv != SQLITE_OK) {
    MOZ_LOG(gStorageLog, LogLevel::Error,
            ("Sqlite statement prepare error: %d '%s'", srv,
             ::sqlite3_errmsg(aNativeConnection)));
    MOZ_LOG(gStorageLog, LogLevel::Error,
            ("Statement was: '%s'", PromiseFlatCString(aSQLStatement).get()));

    aDBConnection->RecordQueryStatus(srv);
    mQueryStatusRecorded = true;
    return convertResultCode(srv);
  }

  MOZ_LOG(gStorageLog, LogLevel::Debug,
          ("Initialized statement '%s' (0x%p)",
           PromiseFlatCString(aSQLStatement).get(), mDBStatement));

  mDBConnection = aDBConnection;
  mNativeConnection = aNativeConnection;
  mParamCount = ::sqlite3_bind_parameter_count(mDBStatement);
  mResultColumnCount = ::sqlite3_column_count(mDBStatement);
  mColumnNames.Clear();

  nsCString* columnNames = mColumnNames.AppendElements(mResultColumnCount);
  for (uint32_t i = 0; i < mResultColumnCount; i++) {
    const char* name = ::sqlite3_column_name(mDBStatement, i);
    columnNames[i].Assign(name);
  }

#ifdef DEBUG
  // We want to try and test for LIKE and that consumers are using
  // escapeStringForLIKE instead of just trusting user input.  The idea to
  // check to see if they are binding a parameter after like instead of just
  // using a string.  We only do this in debug builds because it's expensive!
  nsACString::const_iterator start, end, e;
  aSQLStatement.BeginReading(start);
  aSQLStatement.EndReading(end);
  e = end;
  while (::CaseInsensitiveFindInReadable(" LIKE"_ns, start, e)) {
    // We have a LIKE in here, so we perform our tests
    // FindInReadable moves the iterator, so we have to get a new one for
    // each test we perform.
    nsACString::const_iterator s1, s2, s3;
    s1 = s2 = s3 = start;

    if (!(::CaseInsensitiveFindInReadable(" LIKE ?"_ns, s1, end) ||
          ::CaseInsensitiveFindInReadable(" LIKE :"_ns, s2, end) ||
          ::CaseInsensitiveFindInReadable(" LIKE @"_ns, s3, end))) {
      // At this point, we didn't find a LIKE statement followed by ?, :,
      // or @, all of which are valid characters for binding a parameter.
      // We will warn the consumer that they may not be safely using LIKE.
      NS_WARNING(
          "Unsafe use of LIKE detected!  Please ensure that you "
          "are using mozIStorageStatement::escapeStringForLIKE "
          "and that you are binding that result to the statement "
          "to prevent SQL injection attacks.");
    }

    // resetting start and e
    start = e;
    e = end;
  }
#endif

  return NS_OK;
}

mozIStorageBindingParams* Statement::getParams() {
  nsresult rv;

  // If we do not have an array object yet, make it.
  if (!mParamsArray) {
    nsCOMPtr<mozIStorageBindingParamsArray> array;
    rv = NewBindingParamsArray(getter_AddRefs(array));
    NS_ENSURE_SUCCESS(rv, nullptr);

    mParamsArray = static_cast<BindingParamsArray*>(array.get());
  }

  // If there isn't already any rows added, we'll have to add one to use.
  if (mParamsArray->length() == 0) {
    RefPtr<BindingParams> params(new BindingParams(mParamsArray, this));
    NS_ENSURE_TRUE(params, nullptr);

    rv = mParamsArray->AddParams(params);
    NS_ENSURE_SUCCESS(rv, nullptr);

    // We have to unlock our params because AddParams locks them.  This is safe
    // because no reference to the params object was, or ever will be given out.
    params->unlock(this);

    // We also want to lock our array at this point - we don't want anything to
    // be added to it.  Nothing has, or will ever get a reference to it, but we
    // will get additional safety checks via assertions by doing this.
    mParamsArray->lock();
  }

  return *mParamsArray->begin();
}

void Statement::MaybeRecordQueryStatus(int srv, bool isResetting) {
  // If the statement hasn't been executed synchronously since it was last reset
  // or created then there is no need to record anything. Asynchronous
  // statements have their status tracked and recorded by StatementData.
  if (!mHasExecuted) {
    return;
  }

  if (!isResetting && !isErrorCode(srv)) {
    // Non-errors will be recorded when finalizing.
    return;
  }

  // We only record a status if no status has been recorded previously.
  if (!mQueryStatusRecorded && mDBConnection) {
    mDBConnection->RecordQueryStatus(srv);
  }

  // Allow another status to be recorded if we are resetting this statement.
  mQueryStatusRecorded = !isResetting;
}

Statement::~Statement() {
  MOZ_DIAGNOSTIC_ASSERT(mRefCnt == 1, "Unexpected refcount at destruction");
  MOZ_ASSERT(NS_IsMainThread() || (mStatementParamsHolder == nullptr &&
                                   mStatementRowHolder == nullptr),
             "Must be destroyed on the main thread if JS helpers exist");
  internalFinalize(/* aDestructing */ true);
}

////////////////////////////////////////////////////////////////////////////////
//// nsISupports

NS_IMPL_ADDREF(Statement)
NS_IMPL_RELEASE_WITH_DESTROY(Statement, Destroy())

void Statement::Destroy() {
  // The JS helpers keep raw pointers to us that are cleared when their holders
  // are released on the main thread. Delete there, so that happens as part of
  // our destruction rather than after it.
  if (!NS_IsMainThread() &&
      (mStatementParamsHolder != nullptr || mStatementRowHolder != nullptr)) {
    // Unlike AsyncStatement::Destroy(), there is no need to call
    // destructorAsyncFinalize() here, it is invoked by internalFinalize() from
    // ~Statement.
    NS_PROXY_DELETE_TO_MAIN_THREAD(Statement);
    return;
  }
  delete this;
}

NS_INTERFACE_MAP_BEGIN(Statement)
  NS_INTERFACE_MAP_ENTRY(mozIStorageStatement)
  NS_INTERFACE_MAP_ENTRY(mozIStorageBaseStatement)
  NS_INTERFACE_MAP_ENTRY(mozIStorageBindingParams)
  NS_INTERFACE_MAP_ENTRY(mozIStorageValueArray)
  NS_INTERFACE_MAP_ENTRY(mozilla::storage::StorageBaseStatementInternal)
  if (aIID.Equals(NS_GET_IID(nsIClassInfo))) {
    foundInterface = static_cast<nsIClassInfo*>(&sStatementClassInfo);
  } else
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, mozIStorageStatement)
NS_INTERFACE_MAP_END

////////////////////////////////////////////////////////////////////////////////
//// StorageBaseStatementInternal

Connection* Statement::getOwner() { return mDBConnection; }

int Statement::getAsyncStatement(sqlite3_stmt** _stmt) {
  // If we have no statement, we shouldn't be calling this method!
  NS_ASSERTION(mDBStatement != nullptr, "We have no statement to clone!");

  // If we do not yet have a cached async statement, clone our statement now.
  if (!mAsyncStatement) {
    nsDependentCString sql(::sqlite3_sql(mDBStatement));
    int rc = mDBConnection->prepareStatement(mNativeConnection, sql,
                                             &mAsyncStatement);
    if (rc != SQLITE_OK) {
      mDBConnection->RecordQueryStatus(rc);
      *_stmt = nullptr;
      return rc;
    }

    MOZ_LOG(gStorageLog, LogLevel::Debug,
            ("Cloned statement 0x%p to 0x%p", mDBStatement, mAsyncStatement));
  }

  *_stmt = mAsyncStatement;
  return SQLITE_OK;
}

nsresult Statement::getAsynchronousStatementData(StatementData& _data) {
  if (!mDBStatement) return NS_ERROR_UNEXPECTED;

  sqlite3_stmt* stmt;
  int rc = getAsyncStatement(&stmt);
  if (rc != SQLITE_OK) return convertResultCode(rc);

  _data = StatementData(stmt, bindingParamsArray(), this);

  return NS_OK;
}

already_AddRefed<mozIStorageBindingParams> Statement::newBindingParams(
    mozIStorageBindingParamsArray* aOwner) {
  nsCOMPtr<mozIStorageBindingParams> params = new BindingParams(aOwner, this);
  return params.forget();
}

////////////////////////////////////////////////////////////////////////////////
//// mozIStorageStatement

// proxy to StorageBaseStatementInternal using its define helper.
MIXIN_IMPL_STORAGEBASESTATEMENTINTERNAL(Statement, (void)0;)

NS_IMETHODIMP
Statement::Clone(mozIStorageStatement** _statement) {
  RefPtr<Statement> statement(new Statement());
  NS_ENSURE_TRUE(statement, NS_ERROR_OUT_OF_MEMORY);

  nsAutoCString sql(::sqlite3_sql(mDBStatement));
  nsresult rv = statement->initialize(mDBConnection, mNativeConnection, sql);
  NS_ENSURE_SUCCESS(rv, rv);

  statement.forget(_statement);
  return NS_OK;
}

NS_IMETHODIMP
Statement::Finalize() {
  internalFinalize(false);
  return NS_OK;
}

void Statement::internalFinalize(bool aDestructing) {
  if (!mDBStatement) {
    return;
  }

  int srv = SQLITE_OK;

  {
    // If the statement ends up being finalized twice, the second finalization
    // would apply to a dangling pointer and may cause unexpected consequences.
    // Thus we must be sure that the connection state won't change during this
    // operation, to avoid racing with finalizations made by the closing
    // connection.  See Connection::internalClose().
    MutexAutoLock lockedScope(mDBConnection->sharedAsyncExecutionMutex);
    if (!mDBConnection->isClosed(lockedScope)) {
      MOZ_LOG(gStorageLog, LogLevel::Debug,
              ("Finalizing statement '%s' during garbage-collection",
               ::sqlite3_sql(mDBStatement)));
      // Note this is not the result of finalization, it's the result from the
      // last time the statement was evaluated.
      srv = ::sqlite3_finalize(mDBStatement);
    }
#ifdef DEBUG
    else {
      // The database connection is closed. The sqlite
      // statement has either been finalized already by the connection
      // or is about to be finalized by the connection.
      //
      // Finalizing it here would be useless and segfaultish.
      //
      // Note that we can't display the statement itself, as the data structure
      // is not valid anymore. However, the address shown here should help
      // developers correlate with the more complete debug message triggered
      // by AsyncClose().

      SmprintfPointer msg = ::mozilla::Smprintf(
          "SQL statement (%p) should have been finalized"
          " before garbage-collection. For more details on this statement, set"
          " NSPR_LOG_MESSAGES=mozStorage:5 .",
          mDBStatement);
      NS_WARNING(msg.get());

      // Use %s so we aren't exposing random strings to printf interpolation.
      MOZ_LOG(gStorageLog, LogLevel::Warning, ("%s", msg.get()));
    }
#endif  // DEBUG
  }

  // This will be a no-op if the status has already been recorded or if this
  // statement has not been executed. Async statements have their status
  // tracked and recorded in StatementData.
  MaybeRecordQueryStatus(srv, true);

  mDBStatement = nullptr;

  if (mAsyncStatement) {
    // If the destructor called us, there are no pending async statements (they
    // hold a reference to us) and we can/must just kill the statement directly.
    if (aDestructing)
      destructorAsyncFinalize();
    else
      asyncFinalize();
  }

  // Release the holders, so they can release the reference to us.
  mStatementParamsHolder = nullptr;
  mStatementRowHolder = nullptr;
}

NS_IMETHODIMP
Statement::GetParameterCount(uint32_t* _parameterCount) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  *_parameterCount = mParamCount;
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetParameterName(uint32_t aParamIndex, nsACString& _name) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;
  ENSURE_INDEX_VALUE(aParamIndex, mParamCount);

  const char* name =
      ::sqlite3_bind_parameter_name(mDBStatement, aParamIndex + 1);
  if (name == nullptr) {
    // this thing had no name, so fake one
    nsAutoCString fakeName(":");
    fakeName.AppendInt(aParamIndex);
    _name.Assign(fakeName);
  } else {
    _name.Assign(nsDependentCString(name));
  }

  return NS_OK;
}

NS_IMETHODIMP
Statement::GetParameterIndex(const nsACString& aName, uint32_t* _index) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  // We do not accept any forms of names other than ":name", but we need to add
  // the colon for SQLite.
  nsAutoCString name(":");
  name.Append(aName);
  int ind = ::sqlite3_bind_parameter_index(mDBStatement, name.get());
  if (ind == 0)  // Named parameter not found.
    return NS_ERROR_INVALID_ARG;

  *_index = ind - 1;  // SQLite indexes are 1-based, we are 0-based.

  return NS_OK;
}

NS_IMETHODIMP
Statement::GetColumnCount(uint32_t* _columnCount) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  *_columnCount = mResultColumnCount;
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetColumnName(uint32_t aColumnIndex, nsACString& _name) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;
  ENSURE_INDEX_VALUE(aColumnIndex, mResultColumnCount);

  const char* cname = ::sqlite3_column_name(mDBStatement, aColumnIndex);
  _name.Assign(nsDependentCString(cname));

  return NS_OK;
}

NS_IMETHODIMP
Statement::GetColumnIndex(const nsACString& aName, uint32_t* _index) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  // Surprisingly enough, SQLite doesn't provide an API for this.  We have to
  // determine it ourselves sadly.
  for (uint32_t i = 0; i < mResultColumnCount; i++) {
    if (mColumnNames[i].Equals(aName)) {
      *_index = i;
      return NS_OK;
    }
  }

  return NS_ERROR_INVALID_ARG;
}

NS_IMETHODIMP
Statement::Reset() {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

#ifdef DEBUG
  MOZ_LOG(gStorageLog, LogLevel::Debug,
          ("Resetting statement: '%s'", ::sqlite3_sql(mDBStatement)));

  checkAndLogStatementPerformance(mDBStatement);
#endif

  mParamsArray = nullptr;
  (void)sqlite3_reset(mDBStatement);
  (void)sqlite3_clear_bindings(mDBStatement);

  mExecuting = false;

  // This will be a no-op if the status has already been recorded or if this
  // statement has not been executed. Async statements have their status
  // tracked and recorded in StatementData.
  MaybeRecordQueryStatus(SQLITE_OK, true);
  mHasExecuted = false;

  return NS_OK;
}

NS_IMETHODIMP
Statement::BindParameters(mozIStorageBindingParamsArray* aParameters) {
  NS_ENSURE_ARG_POINTER(aParameters);

  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  BindingParamsArray* array = static_cast<BindingParamsArray*>(aParameters);
  if (array->getOwner() != this) return NS_ERROR_UNEXPECTED;

  if (array->length() == 0) return NS_ERROR_UNEXPECTED;

  mParamsArray = array;
  mParamsArray->lock();

  return NS_OK;
}

NS_IMETHODIMP
Statement::Execute() {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  bool ret;
  nsresult rv = ExecuteStep(&ret);
  nsresult rv2 = Reset();

  return NS_FAILED(rv) ? rv : rv2;
}

NS_IMETHODIMP
Statement::ExecuteStep(bool* _moreResults) {
  AUTO_PROFILER_LABEL("Statement::ExecuteStep", OTHER);

  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  // Bind any parameters first before executing.
  if (mParamsArray) {
    // If we have more than one row of parameters to bind, they shouldn't be
    // calling this method (and instead use executeAsync).
    if (mParamsArray->length() != 1) return NS_ERROR_UNEXPECTED;

    BindingParamsArray::iterator row = mParamsArray->begin();
    nsCOMPtr<IStorageBindingParamsInternal> bindingInternal =
        do_QueryInterface(*row);
    nsCOMPtr<mozIStorageError> error = bindingInternal->bind(mDBStatement);
    if (error) {
      int32_t srv;
      (void)error->GetResult(&srv);
      return convertResultCode(srv);
    }

    // We have bound, so now we can clear our array.
    mParamsArray = nullptr;
  }
  int srv = mDBConnection->stepStatement(mNativeConnection, mDBStatement);
  mHasExecuted = true;
  MaybeRecordQueryStatus(srv);

  if (srv != SQLITE_ROW && srv != SQLITE_DONE &&
      MOZ_LOG_TEST(gStorageLog, LogLevel::Debug)) {
    nsAutoCString errStr;
    (void)mDBConnection->GetLastErrorString(errStr);
    MOZ_LOG(gStorageLog, LogLevel::Debug,
            ("Statement::ExecuteStep error: %s", errStr.get()));
  }

  // SQLITE_ROW and SQLITE_DONE are non-errors
  if (srv == SQLITE_ROW) {
    // we got a row back
    mExecuting = true;
    *_moreResults = true;
    return NS_OK;
  } else if (srv == SQLITE_DONE) {
    // statement is done (no row returned)
    mExecuting = false;
    *_moreResults = false;
    return NS_OK;
  } else if (srv == SQLITE_BUSY || srv == SQLITE_MISUSE) {
    mExecuting = false;
  } else if (mExecuting) {
    MOZ_LOG(gStorageLog, LogLevel::Error,
            ("SQLite error after mExecuting was true!"));
    mExecuting = false;
  }

  return convertResultCode(srv);
}

NS_IMETHODIMP
Statement::GetState(int32_t* _state) {
  if (!mDBStatement)
    *_state = MOZ_STORAGE_STATEMENT_INVALID;
  else if (mExecuting)
    *_state = MOZ_STORAGE_STATEMENT_EXECUTING;
  else
    *_state = MOZ_STORAGE_STATEMENT_READY;

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// mozIStorageValueArray (now part of mozIStorageStatement too)

NS_IMETHODIMP
Statement::GetNumEntries(uint32_t* _length) {
  *_length = mResultColumnCount;
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetTypeOfIndex(uint32_t aIndex, int32_t* _type) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  ENSURE_INDEX_VALUE(aIndex, mResultColumnCount);

  if (!mExecuting) return NS_ERROR_UNEXPECTED;

  int t = ::sqlite3_column_type(mDBStatement, aIndex);
  switch (t) {
    case SQLITE_INTEGER:
      *_type = mozIStorageStatement::VALUE_TYPE_INTEGER;
      break;
    case SQLITE_FLOAT:
      *_type = mozIStorageStatement::VALUE_TYPE_FLOAT;
      break;
    case SQLITE_TEXT:
      *_type = mozIStorageStatement::VALUE_TYPE_TEXT;
      break;
    case SQLITE_BLOB:
      *_type = mozIStorageStatement::VALUE_TYPE_BLOB;
      break;
    case SQLITE_NULL:
      *_type = mozIStorageStatement::VALUE_TYPE_NULL;
      break;
    default:
      return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
Statement::GetInt32(uint32_t aIndex, int32_t* _value) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  ENSURE_INDEX_VALUE(aIndex, mResultColumnCount);

  if (!mExecuting) return NS_ERROR_UNEXPECTED;

  *_value = ::sqlite3_column_int(mDBStatement, aIndex);
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetInt64(uint32_t aIndex, int64_t* _value) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  ENSURE_INDEX_VALUE(aIndex, mResultColumnCount);

  if (!mExecuting) return NS_ERROR_UNEXPECTED;

  *_value = ::sqlite3_column_int64(mDBStatement, aIndex);

  return NS_OK;
}

NS_IMETHODIMP
Statement::GetDouble(uint32_t aIndex, double* _value) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  ENSURE_INDEX_VALUE(aIndex, mResultColumnCount);

  if (!mExecuting) return NS_ERROR_UNEXPECTED;

  *_value = ::sqlite3_column_double(mDBStatement, aIndex);

  return NS_OK;
}

NS_IMETHODIMP
Statement::GetUTF8String(uint32_t aIndex, nsACString& _value) {
  // Get type of Index will check aIndex for us, so we don't have to.
  int32_t type;
  nsresult rv = GetTypeOfIndex(aIndex, &type);
  NS_ENSURE_SUCCESS(rv, rv);
  if (type == mozIStorageStatement::VALUE_TYPE_NULL) {
    // NULL columns should have IsVoid set to distinguish them from the empty
    // string.
    _value.SetIsVoid(true);
  } else {
    const char* value = reinterpret_cast<const char*>(
        ::sqlite3_column_text(mDBStatement, aIndex));
    _value.Assign(value, ::sqlite3_column_bytes(mDBStatement, aIndex));
  }
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetString(uint32_t aIndex, nsAString& _value) {
  // Get type of Index will check aIndex for us, so we don't have to.
  int32_t type;
  nsresult rv = GetTypeOfIndex(aIndex, &type);
  NS_ENSURE_SUCCESS(rv, rv);
  if (type == mozIStorageStatement::VALUE_TYPE_NULL) {
    // NULL columns should have IsVoid set to distinguish them from the empty
    // string.
    _value.SetIsVoid(true);
  } else {
    const char16_t* value = static_cast<const char16_t*>(
        ::sqlite3_column_text16(mDBStatement, aIndex));
    _value.Assign(value, ::sqlite3_column_bytes16(mDBStatement, aIndex) /
                             sizeof(char16_t));
  }
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetVariant(uint32_t aIndex, nsIVariant** _value) {
  if (!mDBStatement) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  ENSURE_INDEX_VALUE(aIndex, mResultColumnCount);

  if (!mExecuting) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIVariant> variant;
  int type = ::sqlite3_column_type(mDBStatement, aIndex);
  switch (type) {
    case SQLITE_INTEGER:
      variant =
          new IntegerVariant(::sqlite3_column_int64(mDBStatement, aIndex));
      break;
    case SQLITE_FLOAT:
      variant = new FloatVariant(::sqlite3_column_double(mDBStatement, aIndex));
      break;
    case SQLITE_TEXT: {
      const char16_t* value = static_cast<const char16_t*>(
          ::sqlite3_column_text16(mDBStatement, aIndex));
      nsDependentString str(
          value,
          ::sqlite3_column_bytes16(mDBStatement, aIndex) / sizeof(char16_t));
      variant = new TextVariant(str);
      break;
    }
    case SQLITE_NULL:
      variant = new NullVariant();
      break;
    case SQLITE_BLOB: {
      int size = ::sqlite3_column_bytes(mDBStatement, aIndex);
      const void* data = ::sqlite3_column_blob(mDBStatement, aIndex);
      variant = new BlobVariant(std::pair<const void*, int>(data, size));
      break;
    }
  }
  NS_ENSURE_TRUE(variant, NS_ERROR_UNEXPECTED);

  variant.forget(_value);
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetBlob(uint32_t aIndex, uint32_t* _size, uint8_t** _blob) {
  if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;

  ENSURE_INDEX_VALUE(aIndex, mResultColumnCount);

  if (!mExecuting) return NS_ERROR_UNEXPECTED;

  int size = ::sqlite3_column_bytes(mDBStatement, aIndex);
  void* blob = nullptr;
  if (size) {
    blob = moz_xmemdup(::sqlite3_column_blob(mDBStatement, aIndex), size);
  }

  *_blob = static_cast<uint8_t*>(blob);
  *_size = size;
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetBlobAsString(uint32_t aIndex, nsAString& aValue) {
  return DoGetBlobAsString(this, aIndex, aValue);
}

NS_IMETHODIMP
Statement::GetBlobAsUTF8String(uint32_t aIndex, nsACString& aValue) {
  return DoGetBlobAsString(this, aIndex, aValue);
}

NS_IMETHODIMP
Statement::GetSharedUTF8String(uint32_t aIndex, uint32_t* _byteLength,
                               const char** _value) {
  *_value = reinterpret_cast<const char*>(
      ::sqlite3_column_text(mDBStatement, aIndex));
  if (_byteLength) {
    *_byteLength = ::sqlite3_column_bytes(mDBStatement, aIndex);
  }
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetSharedString(uint32_t aIndex, uint32_t* _byteLength,
                           const char16_t** _value) {
  *_value = static_cast<const char16_t*>(
      ::sqlite3_column_text16(mDBStatement, aIndex));
  if (_byteLength) {
    *_byteLength = ::sqlite3_column_bytes16(mDBStatement, aIndex);
  }
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetSharedBlob(uint32_t aIndex, uint32_t* _byteLength,
                         const uint8_t** _blob) {
  *_blob =
      static_cast<const uint8_t*>(::sqlite3_column_blob(mDBStatement, aIndex));
  if (_byteLength) {
    *_byteLength = ::sqlite3_column_bytes(mDBStatement, aIndex);
  }
  return NS_OK;
}

NS_IMETHODIMP
Statement::GetIsNull(uint32_t aIndex, bool* _isNull) {
  // Get type of Index will check aIndex for us, so we don't have to.
  int32_t type;
  nsresult rv = GetTypeOfIndex(aIndex, &type);
  NS_ENSURE_SUCCESS(rv, rv);
  *_isNull = (type == mozIStorageStatement::VALUE_TYPE_NULL);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// mozIStorageBindingParams

BOILERPLATE_BIND_PROXIES(Statement,
                         if (!mDBStatement) return NS_ERROR_NOT_INITIALIZED;)

}  // namespace storage
}  // namespace mozilla

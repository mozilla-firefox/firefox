/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/Connection.h"

#include "mozStorageHelper.h"
#include "mozilla/dom/cache/DBSchema.h"

namespace mozilla::dom::cache {

using mozilla::dom::quota::QuotaObject;

NS_IMPL_ISUPPORTS(cache::Connection, mozIStorageAsyncConnection,
                  mozIStorageConnection);

Connection::Connection(mozIStorageConnection* aBase)
    : mBase(aBase), mClosed(false) {
  MOZ_DIAGNOSTIC_ASSERT(mBase);
}

Connection::~Connection() {
  NS_ASSERT_OWNINGTHREAD(Connection);
  MOZ_ALWAYS_SUCCEEDS(Close());
}

NS_IMETHODIMP
Connection::Close() {
  NS_ASSERT_OWNINGTHREAD(Connection);

  if (mClosed) {
    return NS_OK;
  }
  mClosed = true;

  // If we are closing here, then Cache must not have a transaction
  // open anywhere else.  This may fail if storage is corrupted.
  (void)NS_WARN_IF(NS_FAILED(db::IncrementalVacuum(*this)));

  return mBase->Close();
}

// The following methods are all boilerplate that either forward to the
// base connection or block the method.  All the async execution methods
// are blocked because Cache does not use them and they would require more
// work to wrap properly.

// mozIStorageAsyncConnection methods

NS_IMETHODIMP
Connection::AsyncVacuum(mozIStorageCompletionCallback*, bool, int32_t) {
  // async methods are not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Connection::AsyncClose(mozIStorageCompletionCallback*) {
  // async methods are not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Connection::SpinningSynchronousClose() {
  // not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Connection::AsyncClone(bool, mozIStorageCompletionCallback*) {
  // async methods are not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Connection::GetDatabaseFile(nsIFile** aFileOut) {
  return mBase->GetDatabaseFile(aFileOut);
}

NS_IMETHODIMP
Connection::CreateAsyncStatement(const nsACString&,
                                 mozIStorageAsyncStatement**) {
  // async methods are not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Connection::ExecuteAsync(const nsTArray<RefPtr<mozIStorageBaseStatement>>&,
                         mozIStorageStatementCallback*,
                         mozIStoragePendingStatement**) {
  // async methods are not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Connection::ExecuteSimpleSQLAsync(const nsACString&,
                                  mozIStorageStatementCallback*,
                                  mozIStoragePendingStatement**) {
  // async methods are not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Connection::CreateFunction(const nsACString& aFunctionName,
                           int32_t aNumArguments,
                           mozIStorageFunction* aFunction) {
  // async methods are not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Connection::RemoveFunction(const nsACString& aFunctionName) {
  return mBase->RemoveFunction(aFunctionName);
}

NS_IMETHODIMP
Connection::SetProgressHandler(int32_t aGranularity,
                               mozIStorageProgressHandler* aHandler,
                               mozIStorageProgressHandler** aHandlerOut) {
  return mBase->SetProgressHandler(aGranularity, aHandler, aHandlerOut);
}

NS_IMETHODIMP
Connection::RemoveProgressHandler(mozIStorageProgressHandler** aHandlerOut) {
  return mBase->RemoveProgressHandler(aHandlerOut);
}

// mozIStorageConnection methods

NS_IMETHODIMP
Connection::Clone(bool aReadOnly, mozIStorageConnection** aConnectionOut) {
  nsCOMPtr<mozIStorageConnection> conn;
  nsresult rv = mBase->Clone(aReadOnly, getter_AddRefs(conn));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<mozIStorageConnection> wrapped = new Connection(conn);
  wrapped.forget(aConnectionOut);

  return rv;
}

NS_IMETHODIMP
Connection::Interrupt() { return mBase->Interrupt(); }

NS_IMETHODIMP
Connection::GetDefaultPageSize(int32_t* aSizeOut) {
  return mBase->GetDefaultPageSize(aSizeOut);
}

NS_IMETHODIMP
Connection::GetConnectionReady(bool* aReadyOut) {
  return mBase->GetConnectionReady(aReadyOut);
}

NS_IMETHODIMP
Connection::GetLastInsertRowID(int64_t* aRowIdOut) {
  return mBase->GetLastInsertRowID(aRowIdOut);
}

NS_IMETHODIMP
Connection::GetAffectedRows(int32_t* aCountOut) {
  return mBase->GetAffectedRows(aCountOut);
}

NS_IMETHODIMP
Connection::GetLastError(int32_t* aErrorOut) {
  return mBase->GetLastError(aErrorOut);
}

NS_IMETHODIMP
Connection::GetLastErrorString(nsACString& aErrorOut) {
  return mBase->GetLastErrorString(aErrorOut);
}

NS_IMETHODIMP
Connection::GetSchemaVersion(int32_t* aVersionOut) {
  return mBase->GetSchemaVersion(aVersionOut);
}

NS_IMETHODIMP
Connection::SetSchemaVersion(int32_t aVersion) {
  return mBase->SetSchemaVersion(aVersion);
}

NS_IMETHODIMP
Connection::CreateStatement(const nsACString& aQuery,
                            mozIStorageStatement** aStatementOut) {
  return mBase->CreateStatement(aQuery, aStatementOut);
}

NS_IMETHODIMP
Connection::ExecuteSimpleSQL(const nsACString& aQuery) {
  return mBase->ExecuteSimpleSQL(aQuery);
}

NS_IMETHODIMP
Connection::TableExists(const nsACString& aTableName, bool* aExistsOut) {
  return mBase->TableExists(aTableName, aExistsOut);
}

NS_IMETHODIMP
Connection::IndexExists(const nsACString& aIndexName, bool* aExistsOut) {
  return mBase->IndexExists(aIndexName, aExistsOut);
}

NS_IMETHODIMP
Connection::GetTransactionInProgress(bool* aResultOut) {
  return mBase->GetTransactionInProgress(aResultOut);
}

NS_IMETHODIMP
Connection::GetDefaultTransactionType(int32_t* aResultOut) {
  return mBase->GetDefaultTransactionType(aResultOut);
}

NS_IMETHODIMP
Connection::SetDefaultTransactionType(int32_t aType) {
  return mBase->SetDefaultTransactionType(aType);
}

NS_IMETHODIMP
Connection::GetVariableLimit(int32_t* aResultOut) {
  return mBase->GetVariableLimit(aResultOut);
}

NS_IMETHODIMP
Connection::SetVariableLimit(int32_t aLimit) {
  return mBase->SetVariableLimit(aLimit);
}

NS_IMETHODIMP
Connection::BeginTransaction() { return mBase->BeginTransaction(); }

NS_IMETHODIMP
Connection::CommitTransaction() { return mBase->CommitTransaction(); }

NS_IMETHODIMP
Connection::RollbackTransaction() { return mBase->RollbackTransaction(); }

NS_IMETHODIMP
Connection::CreateTable(const char* aTable, const char* aSchema) {
  return mBase->CreateTable(aTable, aSchema);
}

NS_IMETHODIMP
Connection::AttachDatabase(const char* aPath, const char* aName,
                           mozIStorageStatementCallback* aCallback,
                           mozIStoragePendingStatement** _handle) {
  return mBase->AttachDatabase(aPath, aName, aCallback, _handle);
}

NS_IMETHODIMP
Connection::SetGrowthIncrement(int32_t aIncrement,
                               const nsACString& aDatabase) {
  return mBase->SetGrowthIncrement(aIncrement, aDatabase);
}

NS_IMETHODIMP
Connection::LoadExtension(const nsACString& aExtensionName,
                          mozIStorageCompletionCallback* aCallback) {
  return mBase->LoadExtension(aExtensionName, aCallback);
}
NS_IMETHODIMP
Connection::EnableModule(const nsACString& aModule) {
  return mBase->EnableModule(aModule);
}

NS_IMETHODIMP
Connection::DisableCheckpointOnClose() {
  return mBase->DisableCheckpointOnClose();
}

NS_IMETHODIMP
Connection::GetQuotaObjects(QuotaObject** aDatabaseQuotaObject,
                            QuotaObject** aJournalQuotaObject) {
  return mBase->GetQuotaObjects(aDatabaseQuotaObject, aJournalQuotaObject);
}

mozilla::storage::SQLiteMutex& Connection::GetSharedDBMutex() {
  return mBase->GetSharedDBMutex();
}

uint32_t Connection::GetTransactionNestingLevel(
    const mozilla::storage::SQLiteMutexAutoLock& aProofOfLock) {
  return mBase->GetTransactionNestingLevel(aProofOfLock);
}

uint32_t Connection::IncreaseTransactionNestingLevel(
    const mozilla::storage::SQLiteMutexAutoLock& aProofOfLock) {
  return mBase->IncreaseTransactionNestingLevel(aProofOfLock);
}

uint32_t Connection::DecreaseTransactionNestingLevel(
    const mozilla::storage::SQLiteMutexAutoLock& aProofOfLock) {
  return mBase->DecreaseTransactionNestingLevel(aProofOfLock);
}

NS_IMETHODIMP
Connection::BackupToFileAsync(nsIFile* aDestinationFile,
                              mozIStorageCompletionCallback* aCallback,
                              uint32_t aPagesPerStep, uint32_t aStepDelayMs) {
  // async methods are not supported
  return NS_ERROR_NOT_IMPLEMENTED;
}

}  // namespace mozilla::dom::cache

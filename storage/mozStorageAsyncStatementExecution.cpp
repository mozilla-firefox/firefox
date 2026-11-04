/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "sqlite3.h"

#include "mozIStorageStatementCallback.h"
#include "mozStorageBindingParams.h"
#include "mozStorageHelper.h"
#include "mozStorageResultSet.h"
#include "mozStorageRow.h"
#include "mozStorageConnection.h"
#include "mozStorageError.h"
#include "mozStoragePrivateHelpers.h"
#include "mozStorageStatementData.h"
#include "mozStorageAsyncStatementExecution.h"

#include "mozilla/DebugOnly.h"

#include "mozilla/Logging.h"
extern mozilla::LazyLogModule gStorageLog;

namespace mozilla {
namespace storage {

/**
 * The following constants help batch rows into result sets.
 * MAX_MILLISECONDS_BETWEEN_RESULTS was chosen because any user-based task that
 * takes less than 200 milliseconds is considered to feel instantaneous to end
 * users.  MAX_ROWS_PER_RESULT was arbitrarily chosen to reduce the number of
 * dispatches to calling thread, while also providing reasonably-sized sets of
 * data for consumers.  Both of these constants are used because we assume that
 * consumers are trying to avoid blocking their execution thread for long
 * periods of time, and dispatching many small events to the calling thread will
 * end up blocking it.
 */
#define MAX_MILLISECONDS_BETWEEN_RESULTS 75
#define MAX_ROWS_PER_RESULT 15

////////////////////////////////////////////////////////////////////////////////
//// AsyncExecuteStatements

/* static */
nsresult AsyncExecuteStatements::execute(
    StatementDataArray&& aStatements, Connection* aConnection,
    sqlite3* aNativeConnection, mozIStorageStatementCallback* aCallback,
    mozIStoragePendingStatement** _stmt) {
  // Create our event to run in the background
  RefPtr<AsyncExecuteStatements> event = new AsyncExecuteStatements(
      std::move(aStatements), aConnection, aNativeConnection, aCallback);
  NS_ENSURE_TRUE(event, NS_ERROR_OUT_OF_MEMORY);

  // Dispatch it to the background
  nsIEventTarget* target = aConnection->getAsyncExecutionTarget();

  // If we don't have a valid target, this is a bug somewhere else. In the past,
  // this assert found cases where a Run method would schedule a new statement
  // without checking if asyncClose had been called. The caller must prevent
  // that from happening or, if the work is not critical, just avoid creating
  // the new statement during shutdown. See bug 718449 for an example.
  MOZ_ASSERT(target);
  if (!target) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv = target->Dispatch(event, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  // Return it as the pending statement object and track it.
  event.forget(_stmt);
  return NS_OK;
}

AsyncExecuteStatements::AsyncExecuteStatements(
    StatementDataArray&& aStatements, Connection* aConnection,
    sqlite3* aNativeConnection, mozIStorageStatementCallback* aCallback)
    : Runnable("AsyncExecuteStatements"),
      mStatements(std::move(aStatements)),
      mConnection(aConnection),
      mNativeConnection(aNativeConnection),
      mHasTransaction(false),
      mCallback(aCallback),
      mCallingThread(::do_GetCurrentThread()),
      mMaxWait(
          TimeDuration::FromMilliseconds(MAX_MILLISECONDS_BETWEEN_RESULTS)),
      mIntervalStart(TimeStamp::Now()),
      mState(PENDING),
      mCancelRequested(false),
      mMutex(aConnection->sharedAsyncExecutionMutex),
      mDBMutex(aConnection->sharedDBMutex) {
  NS_ASSERTION(mStatements.Length(), "We weren't given any statements!");
}

AsyncExecuteStatements::~AsyncExecuteStatements() {
  MOZ_ASSERT(!mCallback, "Never called the Completion callback!");
  MOZ_ASSERT(!mHasTransaction, "There should be no transaction at this point");
  if (mCallback) {
    NS_ProxyRelease("AsyncExecuteStatements::mCallback", mCallingThread,
                    mCallback.forget());
  }
}

bool AsyncExecuteStatements::shouldNotify() {
#ifdef DEBUG
  mMutex.AssertNotCurrentThreadOwns();

  bool onCallingThread = false;
  (void)mCallingThread->IsOnCurrentThread(&onCallingThread);
  NS_ASSERTION(onCallingThread, "runEvent not running on the calling thread!");
#endif

  // We do not need to acquire mMutex here because it can only ever be written
  // to on the calling thread, and the only thread that can call us is the
  // calling thread, so we know that our access is serialized.
  return !mCancelRequested;
}

bool AsyncExecuteStatements::bindExecuteAndProcessStatement(
    StatementData& aData, bool aLastStatement) {
  mMutex.AssertNotCurrentThreadOwns();

  sqlite3_stmt* aStatement = nullptr;
  // This cannot fail; we are only called if it's available.
  (void)aData.getSqliteStatement(&aStatement);
  MOZ_DIAGNOSTIC_ASSERT(
      aStatement,
      "bindExecuteAndProcessStatement called without an initialized statement");
  BindingParamsArray* paramsArray(aData);

  // Iterate through all of our parameters, bind them, and execute.
  bool continueProcessing = true;
  BindingParamsArray::iterator itr = paramsArray->begin();
  BindingParamsArray::iterator end = paramsArray->end();
  while (itr != end && continueProcessing) {
    // Bind the data to our statement.
    nsCOMPtr<IStorageBindingParamsInternal> bindingInternal =
        do_QueryInterface(*itr);
    nsCOMPtr<mozIStorageError> error = bindingInternal->bind(aStatement);
    if (error) {
      // Set our error state.
      mState = ERROR;

      // And notify.
      (void)notifyError(error);
      return false;
    }

    // Advance our iterator, execute, and then process the statement.
    itr++;
    bool lastStatement = aLastStatement && itr == end;
    continueProcessing = executeAndProcessStatement(aData, lastStatement);

    // Always reset our statement.
    (void)::sqlite3_reset(aStatement);
  }

  return continueProcessing;
}

bool AsyncExecuteStatements::executeAndProcessStatement(StatementData& aData,
                                                        bool aLastStatement) {
  mMutex.AssertNotCurrentThreadOwns();

  sqlite3_stmt* aStatement = nullptr;
  // This cannot fail; we are only called if it's available.
  (void)aData.getSqliteStatement(&aStatement);
  MOZ_DIAGNOSTIC_ASSERT(
      aStatement,
      "executeAndProcessStatement called without an initialized statement");

  // Execute our statement
  bool hasResults;
  do {
    hasResults = executeStatement(aData);

    // If we had an error, bail.
    if (mState == ERROR || mState == CANCELED) return false;

    // If we have been canceled, there is no point in going on...
    {
      MutexAutoLock lockedScope(mMutex);
      if (mCancelRequested) {
        mState = CANCELED;
        return false;
      }
    }

    // Build our result set and notify if we got anything back and have a
    // callback to notify.
    if (mCallback && hasResults &&
        NS_FAILED(buildAndNotifyResults(aStatement))) {
      // We had an error notifying, so we notify on error and stop processing.
      mState = ERROR;

      // Notify, and stop processing statements.
      (void)notifyError(mozIStorageError::ERROR,
                        "An error occurred while notifying about results");

      return false;
    }
  } while (hasResults);

  if (MOZ_LOG_TEST(gStorageLog, LogLevel::Warning)) {
    // Check to make sure that this statement was smart about what it did.
    checkAndLogStatementPerformance(aStatement);
  }

  // If we are done, we need to set our state accordingly while we still hold
  // our mutex.  We would have already returned if we were canceled or had
  // an error at this point.
  if (aLastStatement) mState = COMPLETED;

  return true;
}

bool AsyncExecuteStatements::executeStatement(StatementData& aData) {
  mMutex.AssertNotCurrentThreadOwns();

  sqlite3_stmt* aStatement = nullptr;
  // This cannot fail; we are only called if it's available.
  (void)aData.getSqliteStatement(&aStatement);
  MOZ_DIAGNOSTIC_ASSERT(
      aStatement, "executeStatement called without an initialized statement");

  bool busyRetry = false;
  while (true) {
    if (busyRetry) {
      // Yield, and try again
      (void)PR_Sleep(PR_INTERVAL_NO_WAIT);

      // Check for cancellation before retrying
      {
        MutexAutoLock lockedScope(mMutex);
        if (mCancelRequested) {
          mState = CANCELED;
          return false;
        }
      }
    }

    // lock the sqlite mutex so sqlite3_errmsg cannot change
    SQLiteMutexAutoLock lockedScope(mDBMutex);

    // Stepping clears an interrupt requested while we were retrying, so check
    // for it before retrying.
    if (busyRetry && ::sqlite3_is_interrupted(mNativeConnection)) {
      mState = CANCELED;
      return false;
    }

    int rc = mConnection->stepStatement(mNativeConnection, aStatement);

    // Retry on SQLITE_BUSY, unless the transaction snapshot is stale. In that
    // case retrying can never succeed, and the transaction must be rolled back.
    if (rc == SQLITE_BUSY &&
        ::sqlite3_extended_errcode(mNativeConnection) != SQLITE_BUSY_SNAPSHOT) {
      ::sqlite3_reset(aStatement);
      busyRetry = true;
      continue;
    }

    aData.MaybeRecordQueryStatus(rc);

    // Stop if we have no more results.
    if (rc == SQLITE_DONE) {
      return false;
    }

    // If we got results, we can return now.
    if (rc == SQLITE_ROW) {
      return true;
    }

    if (rc == SQLITE_INTERRUPT) {
      mState = CANCELED;
      return false;
    }

    // Set an error state.
    mState = ERROR;

    // Construct the error message before giving up the mutex (which we cannot
    // hold during the call to notifyError).
    nsCOMPtr<mozIStorageError> errorObj(
        new Error(rc, ::sqlite3_errmsg(mNativeConnection)));
    // We cannot hold the DB mutex while calling notifyError.
    SQLiteMutexAutoUnlock unlockedScope(mDBMutex);
    (void)notifyError(errorObj);

    // Finally, indicate that we should stop processing.
    return false;
  }
}

nsresult AsyncExecuteStatements::buildAndNotifyResults(
    sqlite3_stmt* aStatement) {
  NS_ASSERTION(mCallback, "Trying to dispatch results without a callback!");
  mMutex.AssertNotCurrentThreadOwns();

  // Build result object if we need it.
  if (!mResultSet) mResultSet = new ResultSet();
  NS_ENSURE_TRUE(mResultSet, NS_ERROR_OUT_OF_MEMORY);

  RefPtr<Row> row(new Row());
  NS_ENSURE_TRUE(row, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = row->initialize(aStatement);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mResultSet->add(row);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we have hit our maximum number of allowed results, or if we have hit
  // the maximum amount of time we want to wait for results, notify the
  // calling thread about it.
  TimeStamp now = TimeStamp::Now();
  TimeDuration delta = now - mIntervalStart;
  if (mResultSet->rows() >= MAX_ROWS_PER_RESULT || delta > mMaxWait) {
    // Notify the caller
    rv = notifyResults();
    if (NS_FAILED(rv)) return NS_OK;  // we'll try again with the next result

    // Reset our start time
    mIntervalStart = now;
  }

  return NS_OK;
}

nsresult AsyncExecuteStatements::notifyComplete() {
  mMutex.AssertNotCurrentThreadOwns();
  NS_ASSERTION(mState != PENDING,
               "Still in a pending state when calling Complete!");

  // Reset our statements before we try to commit or rollback.  If we are
  // canceling and have statements that think they have pending work, the
  // rollback will fail.
  for (uint32_t i = 0; i < mStatements.Length(); i++) mStatements[i].reset();

  // Release references to the statement data as soon as possible. If this
  // is the last reference, statements will be finalized immediately on the
  // async thread, hence avoiding several bounces between threads and possible
  // race conditions with AsyncClose().
  mStatements.Clear();

  // Handle our transaction, if we have one
  if (mHasTransaction) {
    SQLiteMutexAutoLock lockedScope(mDBMutex);
    if (mState == COMPLETED) {
      nsresult rv = mConnection->commitTransactionInternal(lockedScope,
                                                           mNativeConnection);
      if (NS_FAILED(rv)) {
        mState = ERROR;
        // We cannot hold the DB mutex while calling notifyError.
        SQLiteMutexAutoUnlock unlockedScope(mDBMutex);
        (void)notifyError(mozIStorageError::ERROR,
                          "Transaction failed to commit");
      }
    } else {
      DebugOnly<nsresult> rv = mConnection->rollbackTransactionInternal(
          lockedScope, mNativeConnection);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Transaction failed to rollback");
    }
    mHasTransaction = false;
  }

  // This will take ownership of mCallback and make sure its destruction will
  // happen on the owner thread.
  (void)mCallingThread->Dispatch(
      NewRunnableMethod("AsyncExecuteStatements::notifyCompleteOnCallingThread",
                        this,
                        &AsyncExecuteStatements::notifyCompleteOnCallingThread),
      NS_DISPATCH_NORMAL);

  return NS_OK;
}

nsresult AsyncExecuteStatements::notifyCompleteOnCallingThread() {
  MOZ_ASSERT(mCallingThread->IsOnCurrentThread());
  // Take ownership of mCallback and responsibility for freeing it when we
  // release it.  Any notifyResultsOnCallingThread and
  // notifyErrorOnCallingThread calls on the stack spinning the event loop have
  // guaranteed their safety by creating their own strong reference before
  // invoking the callback.
  nsCOMPtr<mozIStorageStatementCallback> callback = std::move(mCallback);
  if (callback) {
    (void)callback->HandleCompletion(mState);
  }
  return NS_OK;
}

nsresult AsyncExecuteStatements::notifyError(int32_t aErrorCode,
                                             const char* aMessage) {
  mMutex.AssertNotCurrentThreadOwns();
  mDBMutex.assertNotCurrentThreadOwns();

  if (!mCallback) return NS_OK;

  nsCOMPtr<mozIStorageError> errorObj(new Error(aErrorCode, aMessage));
  NS_ENSURE_TRUE(errorObj, NS_ERROR_OUT_OF_MEMORY);

  return notifyError(errorObj);
}

nsresult AsyncExecuteStatements::notifyError(mozIStorageError* aError) {
  mMutex.AssertNotCurrentThreadOwns();
  mDBMutex.assertNotCurrentThreadOwns();

  if (!mCallback) return NS_OK;

  (void)mCallingThread->Dispatch(
      NewRunnableMethod<nsCOMPtr<mozIStorageError>>(
          "AsyncExecuteStatements::notifyErrorOnCallingThread", this,
          &AsyncExecuteStatements::notifyErrorOnCallingThread, aError),
      NS_DISPATCH_NORMAL);

  return NS_OK;
}

nsresult AsyncExecuteStatements::notifyErrorOnCallingThread(
    mozIStorageError* aError) {
  MOZ_ASSERT(mCallingThread->IsOnCurrentThread());
  // Acquire our own strong reference so that if the callback spins a nested
  // event loop and notifyCompleteOnCallingThread is executed, forgetting
  // mCallback, we still have a valid/strong reference that won't be freed until
  // we exit.
  nsCOMPtr<mozIStorageStatementCallback> callback = mCallback;
  if (shouldNotify() && callback) {
    (void)callback->HandleError(aError);
  }
  return NS_OK;
}

nsresult AsyncExecuteStatements::notifyResults() {
  mMutex.AssertNotCurrentThreadOwns();
  MOZ_ASSERT(mCallback, "notifyResults called without a callback!");

  // This takes ownership of mResultSet, a new one will be generated in
  // buildAndNotifyResults() when further results will arrive.
  (void)mCallingThread->Dispatch(
      NewRunnableMethod<RefPtr<ResultSet>>(
          "AsyncExecuteStatements::notifyResultsOnCallingThread", this,
          &AsyncExecuteStatements::notifyResultsOnCallingThread,
          mResultSet.forget()),
      NS_DISPATCH_NORMAL);

  return NS_OK;
}

nsresult AsyncExecuteStatements::notifyResultsOnCallingThread(
    ResultSet* aResultSet) {
  MOZ_ASSERT(mCallingThread->IsOnCurrentThread());
  // Acquire our own strong reference so that if the callback spins a nested
  // event loop and notifyCompleteOnCallingThread is executed, forgetting
  // mCallback, we still have a valid/strong reference that won't be freed until
  // we exit.
  nsCOMPtr<mozIStorageStatementCallback> callback = mCallback;
  if (shouldNotify() && callback) {
    (void)callback->HandleResult(aResultSet);
  }
  return NS_OK;
}

NS_IMPL_ISUPPORTS_INHERITED(AsyncExecuteStatements, Runnable,
                            mozIStoragePendingStatement)

bool AsyncExecuteStatements::statementsNeedTransaction() {
  // If there is more than one write statement, run in a transaction.
  // Additionally, if we have only one statement but it needs a transaction, due
  // to multiple BindingParams, we will wrap it in one.
  for (uint32_t i = 0, transactionsCount = 0; i < mStatements.Length(); ++i) {
    transactionsCount += mStatements[i].needsTransaction();
    if (transactionsCount > 1) {
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
//// mozIStoragePendingStatement

NS_IMETHODIMP
AsyncExecuteStatements::Cancel() {
#ifdef DEBUG
  bool onCallingThread = false;
  (void)mCallingThread->IsOnCurrentThread(&onCallingThread);
  NS_ASSERTION(onCallingThread, "Not canceling from the calling thread!");
#endif

  // If we have already canceled, we have an error, but always indicate that
  // we are trying to cancel.
  NS_ENSURE_FALSE(mCancelRequested, NS_ERROR_UNEXPECTED);

  {
    MutexAutoLock lockedScope(mMutex);

    // We need to indicate that we want to try and cancel now.
    mCancelRequested = true;
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// nsIRunnable

NS_IMETHODIMP
AsyncExecuteStatements::Run() {
  MOZ_ASSERT(mConnection->isConnectionReadyOnThisThread());

  // Do not run if we have been canceled.
  {
    MutexAutoLock lockedScope(mMutex);
    if (mCancelRequested) mState = CANCELED;
  }
  if (mState == CANCELED) return notifyComplete();

  if (statementsNeedTransaction()) {
    SQLiteMutexAutoLock lockedScope(mDBMutex);
    if (!mConnection->transactionInProgress(lockedScope, mNativeConnection)) {
      if (NS_SUCCEEDED(mConnection->beginTransactionInternal(
              lockedScope, mNativeConnection,
              mozIStorageConnection::TRANSACTION_IMMEDIATE))) {
        mHasTransaction = true;
      }
#ifdef DEBUG
      else {
        NS_WARNING("Unable to create a transaction for async execution.");
      }
#endif
    }
  }

  // Execute each statement, giving the callback results if it returns any.
  for (uint32_t i = 0; i < mStatements.Length(); i++) {
    bool finished = (i == (mStatements.Length() - 1));

    sqlite3_stmt* stmt;
    {  // lock the sqlite mutex so sqlite3_errmsg cannot change
      SQLiteMutexAutoLock lockedScope(mDBMutex);

      int rc = mStatements[i].getSqliteStatement(&stmt);
      if (rc != SQLITE_OK) {
        // Set our error state.
        mState = ERROR;

        // Build the error object; can't call notifyError with the lock held
        nsCOMPtr<mozIStorageError> errorObj(
            new Error(rc, ::sqlite3_errmsg(mNativeConnection)));
        {
          // We cannot hold the DB mutex and call notifyError.
          SQLiteMutexAutoUnlock unlockedScope(mDBMutex);
          (void)notifyError(errorObj);
        }
        break;
      }
    }

    // If we have parameters to bind, bind them, execute, and process.
    if (mStatements[i].hasParametersToBeBound()) {
      if (!bindExecuteAndProcessStatement(mStatements[i], finished)) break;
    }
    // Otherwise, just execute and process the statement.
    else if (!executeAndProcessStatement(mStatements[i], finished)) {
      break;
    }
  }

  // If we still have results that we haven't notified about, take care of
  // them now.
  if (mResultSet) (void)notifyResults();

  // Notify about completion
  return notifyComplete();
}

}  // namespace storage
}  // namespace mozilla

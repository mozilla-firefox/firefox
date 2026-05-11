/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_storage_Connection_h
#define mozilla_storage_Connection_h

#include "nsCOMPtr.h"
#include "mozilla/Atomics.h"
#include "mozilla/Mutex.h"
#include "nsProxyRelease.h"
#include "nsThreadUtils.h"
#include "nsIInterfaceRequestor.h"

#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "mozIStorageProgressHandler.h"
#include "SQLiteMutex.h"
#include "mozIStorageConnection.h"
#include "mozStorageService.h"
#include "mozIStorageAsyncConnection.h"
#include "mozIStorageCompletionCallback.h"

#include "mozilla/Attributes.h"

#include "sqlite3.h"

class nsIFile;
class nsIFileURL;
class nsIEventTarget;
class nsISerialEventTarget;
class nsIThread;

namespace mozilla::storage {

class Connection final : public mozIStorageConnection,
                         public nsIInterfaceRequestor {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_MOZISTORAGEASYNCCONNECTION
  NS_DECL_MOZISTORAGECONNECTION
  NS_DECL_NSIINTERFACEREQUESTOR

  /**
   * Indicates if a database operation is synchronous or asynchronous.
   *
   * - Async operations may be called from any thread for all connections.
   * - Sync operations may be called from any thread for sync connections, and
   *   from background threads for async connections.
   */
  enum ConnectionOperation { ASYNCHRONOUS, SYNCHRONOUS };

  /**
   * Structure used to describe user functions on the database connection.
   */
  struct FunctionInfo {
    nsCOMPtr<mozIStorageFunction> function;
    int32_t numArgs;
  };

  /**
   * @param aService
   *        Pointer to the storage service.  Held onto for the lifetime of the
   *        connection.
   * @param aFlags
   *        The flags to pass to sqlite3_open_v2.
   * @param aSupportedOperations
   *        The operation types supported on this connection. All connections
   *        implement both the async (`mozIStorageAsyncConnection`) and sync
   *        (`mozIStorageConnection`) interfaces, but async connections may not
   *        call sync operations from the main thread.
   * @param aInterruptible
   *        If |true|, the pending operations can be interrupted by invokind the
   *        Interrupt() method.
   *        If |false|, method Interrupt() must not be used.
   * @param aIgnoreLockingMode
   *        If |true|, ignore locks in force on the file. Only usable with
   *        read-only connections. Defaults to false.
   *        Use with extreme caution. If sqlite ignores locks, reads may fail
   *        indicating database corruption (the database won't actually be
   *        corrupt) or produce wrong results without any indication that has
   *        happened.
   */
  Connection(Service* aService, int aFlags,
             ConnectionOperation aSupportedOperations,
             const nsCString& aTelemetryFilename, bool aInterruptible = false,
             bool aIgnoreLockingMode = false, bool aOpenNotExclusive = false);

  /**
   * Creates the connection to an in-memory database.
   */
  nsresult initialize(const nsACString& aStorageKey, const nsACString& aName);

  /**
   * Creates the connection to the database.
   *
   * @param aDatabaseFile
   *        The nsIFile of the location of the database to open, or create if it
   *        does not exist.
   */
  nsresult initialize(nsIFile* aDatabaseFile);

  /**
   * Creates the connection to the database.
   *
   * @param aFileURL
   *        The nsIFileURL of the location of the database to open, or create if
   * it does not exist.
   */
  nsresult initialize(nsIFileURL* aFileURL);

  /**
   * Same as initialize, but to be used on the async thread.
   */
  nsresult initializeOnAsyncThread(nsIFile* aStorageFile);

  /**
   * Fetches runtime status information for this connection.
   *
   * @param aStatusOption One of the SQLITE_DBSTATUS options defined at
   *        http://www.sqlite.org/c3ref/c_dbstatus_options.html
   * @param [optional] aMaxValue if provided, will be set to the highest
   *        istantaneous value.
   * @return the current value for the specified option.
   */
  int32_t getSqliteRuntimeStatus(int32_t aStatusOption,
                                 int32_t* aMaxValue = nullptr);
  /**
   * Registers/unregisters a commit hook callback.
   *
   * @param aCallbackFn a callback function to be invoked on transactions
   *        commit.  Pass nullptr to unregister the current callback.
   * @param [optional] aData if provided, will be passed to the callback.
   * @see http://sqlite.org/c3ref/commit_hook.html
   */
  void setCommitHook(int (*aCallbackFn)(void*), void* aData = nullptr) {
    MOZ_ASSERT(mDBConn, "A connection must exist at this point");
    ::sqlite3_commit_hook(mDBConn, aCallbackFn, aData);
  };

  /**
   * Gets autocommit status.
   */
  bool getAutocommit() {
    return mDBConn && static_cast<bool>(::sqlite3_get_autocommit(mDBConn));
  };

  /**
   * Lazily creates and returns a background execution thread.  In the future,
   * the thread may be re-claimed if left idle, so you should call this
   * method just before you dispatch and not save the reference.
   *
   * This must be called from the opener thread.
   *
   * @return an event target suitable for asynchronous statement execution.
   * @note This method will return null once AsyncClose() has been called.
   */
  nsIEventTarget* getAsyncExecutionTarget();

  /**
   * Mutex used by asynchronous statements to protect state.  The mutex is
   * declared on the connection object because there is no contention between
   * asynchronous statements (they are serialized on mAsyncExecutionThread).
   * Currently protects:
   *  - Connection.mAsyncExecutionThreadShuttingDown
   *  - Connection.mConnectionClosed
   *  - AsyncExecuteStatements.mCancelRequested
   *  - Connection.mLoadedExtensions
   */
  Mutex sharedAsyncExecutionMutex MOZ_UNANNOTATED;

  /**
   * Wraps the mutex that SQLite gives us from sqlite3_db_mutex.  This is public
   * because we already expose the sqlite3* native connection and proper
   * operation of the deadlock detector requires everyone to use the same single
   * SQLiteMutex instance for correctness.
   */
  SQLiteMutex sharedDBMutex;

  /**
   * References the event target this database was opened on.
   */
  const nsCOMPtr<nsISerialEventTarget> eventTargetOpenedOn;

  /**
   * Closes the SQLite database, and warns about any non-finalized statements.
   */
  nsresult internalClose(sqlite3* aNativeconnection);

  /**
   * Shuts down the passed-in async thread.
   */
  void shutdownAsyncThread();

  /**
   * Obtains the filename of the connection.  Useful for logging.
   */
  nsCString getFilename();

  /**
   * Creates an sqlite3 prepared statement object from an SQL string.
   *
   * @param aNativeConnection
   *        The underlying Sqlite connection to prepare the statement with.
   * @param aSQL
   *        The SQL statement string to compile.
   * @param _stmt
   *        New sqlite3_stmt object.
   * @return the result from sqlite3_prepare_v2.
   */
  int prepareStatement(sqlite3* aNativeConnection, const nsCString& aSQL,
                       sqlite3_stmt** _stmt);

  /**
   * Performs a sqlite3_step on aStatement, while properly handling
   * SQLITE_LOCKED when not on the main thread by waiting until we are notified.
   *
   * @param aNativeConnection
   *        The underlying Sqlite connection to step the statement with.
   * @param aStatement
   *        A pointer to a sqlite3_stmt object.
   * @return the result from sqlite3_step.
   */
  int stepStatement(sqlite3* aNativeConnection, sqlite3_stmt* aStatement);

  /**
   * Raw connection transaction management.
   *
   * @see BeginTransactionAs, CommitTransaction, RollbackTransaction.
   */
  nsresult beginTransactionInternal(
      const SQLiteMutexAutoLock& aProofOfLock, sqlite3* aNativeConnection,
      int32_t aTransactionType = TRANSACTION_DEFERRED);
  nsresult commitTransactionInternal(const SQLiteMutexAutoLock& aProofOfLock,
                                     sqlite3* aNativeConnection);
  nsresult rollbackTransactionInternal(const SQLiteMutexAutoLock& aProofOfLock,
                                       sqlite3* aNativeConnection);

  /**
   * Indicates if this database connection is open.
   */
  inline bool connectionReady() { return mDBConn != nullptr; }

  /**
   * Indicates if this database connection has an open transaction. Because
   * multiple threads can execute statements on the same connection, this method
   * requires proof that the caller is holding `sharedDBMutex`.
   *
   * Per the SQLite docs, `sqlite3_get_autocommit` returns 0 if autocommit mode
   * is disabled. `BEGIN` disables autocommit mode, and `COMMIT`, `ROLLBACK`, or
   * an automatic rollback re-enables it.
   */
  // aNativeConnection overload is the canonical implementation; the no-arg
  // overload delegates to it using the shared mDBConn. Async callers pass
  // their own captured native connection pointer to avoid racing with
  // AsyncClose() which can null mDBConn on the main thread.
  inline bool transactionInProgress(const SQLiteMutexAutoLock& aProofOfLock,
                                    sqlite3* aNativeConnection) {
    return aNativeConnection &&
           !static_cast<bool>(::sqlite3_get_autocommit(aNativeConnection));
  }
  inline bool transactionInProgress(const SQLiteMutexAutoLock& aProofOfLock) {
    return transactionInProgress(aProofOfLock, mDBConn);
  }

  /**
   * Indicates if this database connection supports the given operation.
   *
   * @param  aOperationType
   *         The operation type, sync or async.
   * @return `true` if the operation is supported, `false` otherwise.
   */
  bool operationSupported(ConnectionOperation aOperationType);

  /**
   * Thread-aware version of connectionReady, results per caller's thread are:
   *  - owner thread: Same as connectionReady().  True means we have a valid,
   *    un-closed database connection and it's not going away until you invoke
   *    Close() or AsyncClose().
   *  - async thread: Returns true at all times because you can't schedule
   *    runnables against the async thread after AsyncClose() has been called.
   *    Therefore, the connection is still around if your code is running.
   *  - any other thread: Race-prone Lies!  If you are main-thread code in
   *    mozStorageService iterating over the list of connections, you need to
   *    acquire the sharedAsyncExecutionMutex for the connection, invoke
   *    connectionReady() while holding it, and then continue to hold it while
   *    you do whatever you need to do.  This is because of off-main-thread
   *    consumers like dom/cache and IndexedDB and other QuotaManager clients.
   */
  bool isConnectionReadyOnThisThread();

  /**
   * True if this connection has inited shutdown.
   */
  bool isClosing();

  /**
   * True if the underlying connection is closed.
   * Any sqlite resources may be lost when this returns true, so nothing should
   * try to use them.
   * This locks on sharedAsyncExecutionMutex.
   */
  bool isClosed();

  /**
   * Same as isClosed(), but takes a proof-of-lock instead of locking
   * internally.
   */
  bool isClosed(MutexAutoLock& lock);

  /**
   * True if the async execution thread is alive and able to be used (i.e., it
   * is not in the process of shutting down.)
   *
   * This must be called from the opener thread.
   */
  bool isAsyncExecutionThreadAvailable();

  nsresult initializeClone(Connection* aClone, bool aReadOnly);

  /**
   * Records a status from a sqlite statement.
   *
   * @param srv The sqlite result for the failure or SQLITE_OK.
   */
  void RecordQueryStatus(int srv);

  /**
   * Returns the number of pages in the free list that can be removed.
   *
   * A database may use chunked growth to reduce filesystem fragmentation, then
   * Sqlite will allocate and release multiple pages in chunks. We want to
   * preserve the chunked space to reduce the likelihood of fragmentation,
   * releasing free pages only when there's a large amount of them. This can be
   * used to decide if it's worth vacuuming the database and how many pages can
   * be vacuumed in case of incremental vacuum.
   * Note this returns 0, and asserts, in case of errors.
   */
  int32_t RemovablePagesInFreeList(const nsACString& aSchemaName);

  /**
   * Whether the statement currently running on the helper thread can be
   * interrupted.
   */
  Atomic<bool> mIsStatementOnHelperThreadInterruptible;

 private:
  ~Connection();
  nsresult initializeInternal();
  void initializeFailed();

  /**
   * Records the status of an attempt to load a sqlite database to telemetry.
   *
   * @param rv The state of the load, success or failure.
   */
  void RecordOpenStatus(nsresult rv);

  /**
   * Sets the database into a closed state so no further actions can be
   * performed.
   *
   * @note mDBConn is set to nullptr in this method.
   */
  nsresult setClosedState();

  /**
   * Helper for calls to sqlite3_exec. Reports long delays to Telemetry.
   *
   * @param aNativeConnection
   *        The underlying Sqlite connection to execute the query with.
   * @param aSqlString
   *        SQL string to execute
   * @return the result from sqlite3_exec.
   */
  int executeSql(sqlite3* aNativeConnection, const char* aSqlString);

  /**
   * Describes a certain primitive type in the database.
   *
   * Possible Values Are:
   *  INDEX - To check for the existence of an index
   *  TABLE - To check for the existence of a table
   */
  enum DatabaseElementType { INDEX, TABLE };

  /**
   * Determines if the specified primitive exists.
   *
   * @param aElementType
   *        The type of element to check the existence of
   * @param aElementName
   *        The name of the element to check for
   * @returns true if element exists, false otherwise
   */
  nsresult databaseElementExists(enum DatabaseElementType aElementType,
                                 const nsACString& aElementName, bool* _exists);

  bool findFunctionByInstance(mozIStorageFunction* aInstance);

  static int sProgressHelper(void* aArg);
  // Generic progress handler
  // Dispatch call to registered progress handler,
  // if there is one. Do nothing in other cases.
  int progressHandler();

  /**
   * Like `operationSupported`, but throws (and, in a debug build, asserts) if
   * the operation is unsupported.
   */
  nsresult ensureOperationSupported(ConnectionOperation aOperationType);

  sqlite3* mDBConn;
  nsCString mStorageKey;
  nsCString mName;
  nsCOMPtr<nsIFileURL> mFileURL;
  nsCOMPtr<nsIFile> mDatabaseFile;

  /**
   * Lazily created thread for asynchronous statement execution.  Consumers
   * should use getAsyncExecutionTarget rather than directly accessing this
   * field.
   *
   * This must be modified only on the opener thread.
   */
  nsCOMPtr<nsIThread> mAsyncExecutionThread;

  /**
   * The filename that will be reported to telemetry for this connection. By
   * default this will be the leaf of the path to the database file.
   */
  nsCString mTelemetryFilename;

  /**
   * Stores the default behavior for all transactions run on this connection.
   */
  mozilla::Atomic<int32_t> mDefaultTransactionType;

  /**
   * Used to trigger cleanup logic only the first time our refcount hits 1.  We
   * may trigger a failsafe Close() that invokes SpinningSynchronousClose()
   * which invokes AsyncClose() which may bump our refcount back up to 2 (and
   * which will then fall back down to 1 again).  It's also possible that the
   * Service may bump our refcount back above 1 if getConnections() runs before
   * we invoke unregisterConnection().
   */
  mozilla::Atomic<bool> mDestroying;

  /**
   * Stores the mapping of a given function by name to its instance.  Access is
   * protected by sharedDBMutex.
   */
  nsTHashMap<nsCStringHashKey, FunctionInfo> mFunctions;

  /**
   * Stores the registered progress handler for the database connection.  Access
   * is protected by sharedDBMutex.
   */
  nsCOMPtr<mozIStorageProgressHandler> mProgressHandler;

  // This is here for two reasons: 1) It's used to make sure that the
  // connections do not outlive the service.  2) Our custom collating functions
  // call its localeCompareStrings() method.
  RefPtr<Service> mStorageService;

  nsresult synchronousClose();

  /**
   * Stores the flags we passed to sqlite3_open_v2.
   */
  const int mFlags;

  uint32_t mTransactionNestingLevel;

  /**
   * Indicates which operations are supported on this connection.
   */
  const ConnectionOperation mSupportedOperations;

  /**
   * Stores whether this connection is interruptible.
   */
  const bool mInterruptible;

  /**
   * Stores whether we should ask sqlite3_open_v2 to ignore locking.
   */
  const bool mIgnoreLockingMode;

  /**
   * Stores whether we should ask sqlite3_open_v2 to open without an exclusive
   * lock.
   */
  const bool mOpenNotExclusive;

  /**
   * Set to true by Close() or AsyncClose() prior to shutdown.
   *
   * If false, we guarantee both that the underlying sqlite3 database
   * connection is still open and that getAsyncExecutionTarget() can
   * return a thread. Once true, either the sqlite3 database
   * connection is being shutdown or it has been
   * shutdown. Additionally, once true, getAsyncExecutionTarget()
   * returns null.
   *
   * This variable should be accessed while holding the
   * sharedAsyncExecutionMutex.
   */
  bool mAsyncExecutionThreadShuttingDown;

  /**
   * Set to true just prior to calling sqlite3_close on the
   * connection.
   *
   * This variable should be accessed while holding the
   * sharedAsyncExecutionMutex.
   */
  bool mConnectionClosed;

  /**
   * Stores the growth increment chunk size, set through SetGrowthIncrement().
   */
  Atomic<int32_t> mGrowthChunkSize;

  /**
   * Stores a list of the SQLite extensions loaded for this connections.
   * This is used to properly clone the connection.
   * @note Hold sharedAsyncExecutionMutex while using this.
   */
  nsTHashSet<nsCString> mLoadedExtensions
      MOZ_GUARDED_BY(sharedAsyncExecutionMutex);
};

/**
 * A Runnable designed to call a mozIStorageCompletionCallback on
 * the appropriate thread.
 */
class CallbackComplete final : public Runnable {
 public:
  /**
   * @param aValue The result to pass to the callback. It must
   *               already be owned by the main thread.
   * @param aCallback The callback. It must already be owned by the
   *                  main thread.
   */
  CallbackComplete(nsresult aStatus, nsISupports* aValue,
                   already_AddRefed<mozIStorageCompletionCallback> aCallback)
      : Runnable("storage::CallbackComplete"),
        mStatus(aStatus),
        mValue(aValue),
        mCallback(aCallback) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    nsresult rv = mCallback->Complete(mStatus, mValue);

    // Ensure that we release on the main thread
    mValue = nullptr;
    mCallback = nullptr;
    return rv;
  }

 private:
  nsresult mStatus;
  nsCOMPtr<nsISupports> mValue;
  // This is a RefPtr<T> and not a nsCOMPtr<T> because
  // nsCOMP<T> would cause an off-main thread QI, which
  // is not a good idea (and crashes XPConnect).
  RefPtr<mozIStorageCompletionCallback> mCallback;
};

}  // namespace mozilla::storage

/**
 * Casting Connection to nsISupports is ambiguous.
 * This method handles that.
 */
inline nsISupports* ToSupports(mozilla::storage::Connection* p) {
  return NS_ISUPPORTS_CAST(mozIStorageAsyncConnection*, p);
}

#endif  // mozilla_storage_Connection_h

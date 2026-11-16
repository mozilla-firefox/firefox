/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/DBAction.h"

#include "mozIStorageConnection.h"
#include "mozIStorageService.h"
#include "mozStorageCID.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/GeckoTrace.h"
#include "mozilla/dom/cache/Connection.h"
#include "mozilla/dom/cache/DBSchema.h"
#include "mozilla/dom/cache/FileUtils.h"
#include "mozilla/dom/cache/QuotaClient.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/net/nsFileProtocolHandler.h"
#include "nsIFile.h"
#include "nsIFileURL.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"

namespace mozilla::dom::cache {

using mozilla::dom::quota::CloneFileAndAppend;
using mozilla::dom::quota::IsDatabaseCorruptionError;

namespace {

nsresult WipeDatabase(const CacheDirectoryMetadata& aDirectoryMetadata,
                      nsIFile& aDBFile) {
  QM_TRY_INSPECT(const auto& dbDir, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                        nsCOMPtr<nsIFile>, aDBFile, GetParent));

  QM_TRY(MOZ_TO_RESULT(RemoveNsIFile(aDirectoryMetadata, aDBFile)));

  // Note, the -wal journal file will be automatically deleted by sqlite when
  // the new database is created.  No need to explicitly delete it here.

  // Delete the morgue as well.
  QM_TRY(MOZ_TO_RESULT(BodyDeleteDir(aDirectoryMetadata, *dbDir)));

  QM_TRY(MOZ_TO_RESULT(WipePaddingFile(aDirectoryMetadata, dbDir)));

  return NS_OK;
}

}  // namespace

DBAction::DBAction(Mode aMode) : mMode(aMode) {}

DBAction::~DBAction() = default;

void DBAction::RunOnTarget(
    SafeRefPtr<Resolver> aResolver,
    const Maybe<CacheDirectoryMetadata>& aDirectoryMetadata,
    Data* aOptionalData, const Maybe<CipherKey>& aMaybeCipherKey) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aResolver);
  MOZ_DIAGNOSTIC_ASSERT(aDirectoryMetadata);
  MOZ_DIAGNOSTIC_ASSERT(aDirectoryMetadata->mDir);

  if (IsCanceled() || AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownQM)) {
    aResolver->Resolve(NS_ERROR_ABORT);
    return;
  }

  const auto resolveErr = [&aResolver](const nsresult rv) {
    aResolver->Resolve(rv);
  };

  QM_TRY_INSPECT(const auto& dbDir,
                 CloneFileAndAppend(*(aDirectoryMetadata->mDir), u"cache"_ns),
                 QM_VOID, resolveErr);

  nsCOMPtr<mozIStorageConnection> conn;

  // Attempt to reuse the connection opened by a previous Action.
  if (aOptionalData) {
    conn = aOptionalData->GetConnection();
  }

  // If there is no previous Action, then we must open one.
  if (!conn) {
    QM_TRY_INSPECT(const auto& rawConn,
                   OpenConnection(*aDirectoryMetadata, *dbDir, aMaybeCipherKey),
                   QM_VOID, resolveErr);
    MOZ_DIAGNOSTIC_ASSERT(rawConn);

    // Use our special wrapped connection class. This will let us perform
    // some clean up operations once the connection is closed.
    conn = new Connection(rawConn);
    // Save this connection in the shared Data object so later Actions can use
    // it. This avoids opening a new connection and running cleanup operations
    // for every Action.
    if (aOptionalData) {
      aOptionalData->SetConnection(conn);
    }
  }

  RunWithDBOnTarget(std::move(aResolver), *aDirectoryMetadata, dbDir, conn);
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult> DBAction::OpenConnection(
    const CacheDirectoryMetadata& aDirectoryMetadata, nsIFile& aDBDir,
    const Maybe<CipherKey>& aMaybeCipherKey) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aDirectoryMetadata.mDirectoryLockId >= 0);

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aDBDir, Exists));

  if (!exists) {
    QM_TRY(OkIf(mMode == Create), Err(NS_ERROR_FILE_NOT_FOUND));
    QM_TRY(MOZ_TO_RESULT(aDBDir.Create(nsIFile::DIRECTORY_TYPE, 0755)));
  }

  QM_TRY_INSPECT(const auto& dbFile,
                 CloneFileAndAppend(aDBDir, kCachesSQLiteFilename));

  QM_TRY_RETURN(OpenDBConnection(aDirectoryMetadata, *dbFile, aMaybeCipherKey));
}

SyncDBAction::SyncDBAction(Mode aMode) : DBAction(aMode) {}

SyncDBAction::~SyncDBAction() = default;

void SyncDBAction::RunWithDBOnTarget(
    SafeRefPtr<Resolver> aResolver,
    const CacheDirectoryMetadata& aDirectoryMetadata, nsIFile* aDBDir,
    mozIStorageConnection* aConn) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aResolver);
  MOZ_DIAGNOSTIC_ASSERT(aDBDir);
  MOZ_DIAGNOSTIC_ASSERT(aConn);

  nsresult rv = RunSyncWithDBOnTarget(aDirectoryMetadata, aDBDir, aConn);
  aResolver->Resolve(rv);
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult> OpenDBConnection(
    const CacheDirectoryMetadata& aDirectoryMetadata, nsIFile& aDBFile,
    const Maybe<CipherKey>& aMaybeCipherKey) {
  GECKO_TRACE_SCOPE("dom::cache", "OpenDBConnection");

  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aDirectoryMetadata.mDirectoryLockId >= -1);
  MOZ_DIAGNOSTIC_ASSERT_IF(aDirectoryMetadata.mIsPrivate, aMaybeCipherKey);

  // Use our default file:// protocol handler directly to construct the database
  // URL.  This avoids any problems if a plugin registers a custom file://
  // handler.  If such a custom handler used javascript, then we would have a
  // bad time running off the main thread here.
  auto handler = MakeRefPtr<nsFileProtocolHandler>();
  QM_TRY(MOZ_TO_RESULT(handler->Init()));

  QM_TRY_INSPECT(const auto& mutator, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                          nsCOMPtr<nsIURIMutator>, handler,
                                          NewFileURIMutator, &aDBFile));

  const nsCString directoryLockIdClause =
      "&directoryLockId="_ns +
      IntToCString(aDirectoryMetadata.mDirectoryLockId);

  const auto keyClause = [&aMaybeCipherKey] {
    nsAutoCString keyClause;
    if (aMaybeCipherKey) {
      keyClause.AssignLiteral("&key=");
      for (uint8_t byte : CipherStrategy::SerializeKey(*aMaybeCipherKey)) {
        keyClause.AppendPrintf("%02x", byte);
      }
    }
    return keyClause;
  }();

  nsCOMPtr<nsIFileURL> dbFileUrl;
  QM_TRY(MOZ_TO_RESULT(
      NS_MutateURI(mutator)
          .SetQuery("cache=private"_ns + directoryLockIdClause + keyClause)
          .Finalize(dbFileUrl)));

  QM_TRY_INSPECT(const auto& storageService,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID),
                 Err(NS_ERROR_UNEXPECTED));

  QM_TRY_UNWRAP(
      auto conn,
      QM_OR_ELSE_WARN_IF(
          // Expression.
          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
              nsCOMPtr<mozIStorageConnection>, storageService,
              OpenDatabaseWithFileURL, dbFileUrl, ""_ns,
              mozIStorageService::CONNECTION_DEFAULT),
          // Predicate.
          IsDatabaseCorruptionError,
          // Fallback.
          ([&aDirectoryMetadata, &aDBFile, &storageService,
            &dbFileUrl](const nsresult rv)
               -> Result<nsCOMPtr<mozIStorageConnection>, nsresult> {
            NS_WARNING("Cache database corrupted. Recreating empty database.");

            // There is nothing else we can do to recover.  Also, this data
            // can be deleted by QuotaManager at any time anyways.
            QM_TRY(MOZ_TO_RESULT(WipeDatabase(aDirectoryMetadata, aDBFile)));

            QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                nsCOMPtr<mozIStorageConnection>, storageService,
                OpenDatabaseWithFileURL, dbFileUrl, ""_ns,
                mozIStorageService::CONNECTION_DEFAULT));
          })));

  // Check the schema to make sure it is not too old.
  QM_TRY_INSPECT(const int32_t& schemaVersion,
                 MOZ_TO_RESULT_INVOKE_MEMBER(conn, GetSchemaVersion));
  if (schemaVersion > 0 && schemaVersion < db::kFirstShippedSchemaVersion) {
    // Close existing connection before wiping database.
    conn = nullptr;

    QM_TRY(MOZ_TO_RESULT(WipeDatabase(aDirectoryMetadata, aDBFile)));

    QM_TRY_UNWRAP(conn, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                            nsCOMPtr<mozIStorageConnection>, storageService,
                            OpenDatabaseWithFileURL, dbFileUrl, ""_ns,
                            mozIStorageService::CONNECTION_DEFAULT));
  }

  QM_TRY(MOZ_TO_RESULT(db::InitializeConnection(*conn)));

  return conn;
}

}  // namespace mozilla::dom::cache

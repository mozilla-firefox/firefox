/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/DBSchema.h"

#include "ipc/IPCMessageUtils.h"
#include "mozIStorageConnection.h"
#include "mozIStorageFunction.h"
#include "mozIStorageStatement.h"
#include "mozStorageHelper.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/dom/HeadersBinding.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/ResponseBinding.h"
#include "mozilla/dom/cache/CacheCommon.h"
#include "mozilla/dom/cache/CacheTypes.h"
#include "mozilla/dom/cache/FileUtils.h"
#include "mozilla/dom/cache/SavedTypes.h"
#include "mozilla/dom/cache/TypeUtils.h"
#include "mozilla/dom/cache/Types.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/psm/TransportSecurityInfo.h"
#include "mozilla/storage/Variant.h"
#include "nsCOMPtr.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsComponentManagerUtils.h"
#include "nsHttp.h"
#include "nsIContentPolicy.h"
#include "nsICryptoHash.h"
#include "nsIURI.h"
#include "nsNetCID.h"
#include "nsPrintfCString.h"
#include "nsTArray.h"

namespace mozilla::dom::cache::db {
const int32_t kFirstShippedSchemaVersion = 15;
namespace {
// ## Firefox 57 Cache API v25/v26/v27 Schema Hack Info
// ### Overview
// In Firefox 57 we introduced Cache API schema version 26 and Quota Manager
// schema v3 to support tracking padding for opaque responses.  Unfortunately,
// Firefox 57 is a big release that may potentially result in users downgrading
// to Firefox 56 due to 57 retiring add-ons.  These schema changes have the
// unfortunate side-effect of causing QuotaManager and all its clients to break
// if the user downgrades to 56.  In order to avoid making a bad situation
// worse, we're now retrofitting 57 so that Firefox 56 won't freak out.
//
// ### Implementation
// We're introducing a new schema version 27 that uses an on-disk schema version
// of v25.  We differentiate v25 from v27 by the presence of the column added
// by v26.  This translates to:
// - v25: on-disk schema=25, no "response_padding_size" column in table
//   "entries".
// - v26: on-disk schema=26, yes "response_padding_size" column in table
//   "entries".
// - v27: on-disk schema=25, yes "response_padding_size" column in table
//   "entries".
//
// ### Fallout
// Firefox 57 is happy because it sees schema 27 and everything is as it
// expects.
//
// Firefox 56 non-DEBUG build is fine/happy, but DEBUG builds will not be.
// - Our QuotaClient will invoke `NS_WARNING("Unknown Cache file found!");`
//   at QuotaManager init time.  This is harmless but annoying and potentially
//   misleading.
// - The DEBUG-only Validate() call will error out whenever an attempt is made
//   to open a DOM Cache database because it will notice the schema is broken
//   and there is no attempt at recovery.
//
const int32_t kHackyDowngradeSchemaVersion = 25;
const int32_t kHackyPaddingSizePresentVersion = 27;
//
// Update this whenever the DB schema is changed.
const int32_t kLatestSchemaVersion = 33;
// ---------
// The following constants define the SQL schema.  These are defined in the
// same order the SQL should be executed in CreateOrMigrateSchema().  They are
// broken out as constants for convenient use in validation and migration.
// ---------
// The caches table is the single source of truth about what Cache
// objects exist for the origin.  The contents of the Cache are stored
// in the entries table that references back to caches.
//
// The caches table is also referenced from storage.  Rows in storage
// represent named Cache objects.  There are cases, however, where
// a Cache can still exist, but not be in a named Storage.  For example,
// when content is still using the Cache after CacheStorage::Delete()
// has been run.
//
// For now, the caches table mainly exists for data integrity with
// foreign keys, but could be expanded to contain additional cache object
// information.
//
// AUTOINCREMENT is necessary to prevent CacheId values from being reused.
const char kTableCaches[] =
    "CREATE TABLE caches ("
    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT "
    ")";

// Security blobs are quite large and duplicated for every Response from
// the same https origin.  This table is used to de-duplicate this data.
const char kTableSecurityInfo[] =
    "CREATE TABLE security_info ("
    "id INTEGER NOT NULL PRIMARY KEY, "
    "hash BLOB NOT NULL, "  // first 8-bytes of the sha1 hash of data column
    "data BLOB NOT NULL, "  // full security info data, usually a few KB
    "refcount INTEGER NOT NULL"
    ")";

// Index the smaller hash value instead of the large security data blob.
const char kIndexSecurityInfoHash[] =
    "CREATE INDEX security_info_hash_index ON security_info (hash)";

const char kTableEntries[] =
    "CREATE TABLE entries ("
    "id INTEGER NOT NULL PRIMARY KEY, "
    "request_method TEXT NOT NULL, "
    "request_url_no_query TEXT NOT NULL, "
    "request_url_no_query_hash BLOB NOT NULL, "  // first 8-bytes of sha1 hash
    "request_url_query TEXT NOT NULL, "
    "request_url_query_hash BLOB NOT NULL, "  // first 8-bytes of sha1 hash
    "request_referrer TEXT NOT NULL, "
    "request_headers_guard INTEGER NOT NULL, "
    "request_mode INTEGER NOT NULL, "
    "request_credentials INTEGER NOT NULL, "
    "request_contentpolicytype INTEGER NOT NULL, "
    "request_cache INTEGER NOT NULL, "
    "request_body_id TEXT NULL, "
    "response_type INTEGER NOT NULL, "
    "response_status INTEGER NOT NULL, "
    "response_status_text TEXT NOT NULL, "
    "response_headers_guard INTEGER NOT NULL, "
    "response_body_id TEXT NULL, "
    "response_security_info_id INTEGER NULL REFERENCES security_info(id), "
    "response_principal_info TEXT NOT NULL, "
    "cache_id INTEGER NOT NULL REFERENCES caches(id) ON DELETE CASCADE, "
    "request_redirect INTEGER NOT NULL, "
    "request_referrer_policy INTEGER NOT NULL, "
    "request_integrity TEXT NOT NULL, "
    "request_url_fragment TEXT NOT NULL, "
    "response_padding_size INTEGER NULL, "
    "request_body_disk_size INTEGER NULL, "
    "response_body_disk_size INTEGER NULL, "
    "response_credentials INTEGER NOT NULL, "
    "pending_deletion INTEGER NOT NULL DEFAULT 0 "
    // New columns must be added at the end of table to migrate and
    // validate properly.
    ")";
// Create an index to support the QueryCache() matching algorithm.  This
// needs to quickly find entries in a given Cache that match the request
// URL.  The url query is separated in order to support the ignoreSearch
// option.  Finally, we index hashes of the URL values instead of the
// actual strings to avoid excessive disk bloat.  The index will duplicate
// the contents of the columsn in the index.  The hash index will prune
// the vast majority of values from the query result so that normal
// scanning only has to be done on a few values to find an exact URL match.
const char kIndexEntriesRequest[] =
    "CREATE INDEX entries_request_match_index "
    "ON entries (cache_id, request_url_no_query_hash, "
    "request_url_query_hash)";

// Index to support the queries on entries pending deletion.
const char kIndexEntriesPendingDeletion[] =
    "CREATE INDEX entries_pending_deletion_index "
    "ON entries (pending_deletion) WHERE pending_deletion = 1";

const char kTableRequestHeaders[] =
    "CREATE TABLE request_headers ("
    "name TEXT NOT NULL, "
    "value TEXT NOT NULL, "
    "entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE"
    ")";

const char kTableResponseHeaders[] =
    "CREATE TABLE response_headers ("
    "name TEXT NOT NULL, "
    "value TEXT NOT NULL, "
    "entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE"
    ")";

// Index for fast lookup of headers by entry_id, particularly for vary checks
const char kIndexResponseHeadersEntryId[] =
    "CREATE INDEX response_headers_entry_id_index "
    "ON response_headers (entry_id, name)";

const char kTableResponseUrlList[] =
    "CREATE TABLE response_url_list ("
    "url TEXT NOT NULL, "
    "entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE"
    ")";

// NOTE: key allows NULL below since that is how "" is represented
//       in a BLOB column.  We use BLOB to avoid encoding issues
//       with storing DOMStrings.
const char kTableStorage[] =
    "CREATE TABLE storage ("
    "namespace INTEGER NOT NULL, "
    "key BLOB NULL, "
    "cache_id INTEGER NOT NULL REFERENCES caches(id), "
    "PRIMARY KEY(namespace, key) "
    ")";

const char kTableUsageInfo[] =
    "CREATE TABLE usage_info ("
    "id INTEGER NOT NULL PRIMARY KEY, "
    "total_disk_usage INTEGER NOT NULL "
    ")";

const char kTriggerEntriesInsert[] =
    "CREATE TRIGGER entries_insert_trigger "
    "AFTER INSERT ON entries "
    "FOR EACH ROW "
    "BEGIN "
    "UPDATE usage_info SET total_disk_usage = total_disk_usage + "
    "ifnull(NEW.request_body_disk_size, 0) + "
    "ifnull(NEW.response_body_disk_size, 0) "
    "WHERE usage_info.id = 1; "
    "END";

const char kTriggerEntriesUpdate[] =
    "CREATE TRIGGER entries_update_trigger "
    "AFTER UPDATE ON entries "
    "FOR EACH ROW "
    "BEGIN "
    "UPDATE usage_info SET total_disk_usage = total_disk_usage - "
    "ifnull(OLD.request_body_disk_size, 0) + "
    "ifnull(NEW.request_body_disk_size, 0) - "
    "ifnull(OLD.response_body_disk_size, 0) + "
    "ifnull(NEW.response_body_disk_size, 0) "
    "WHERE usage_info.id = 1; "
    "END";

const char kTriggerEntriesDelete[] =
    "CREATE TRIGGER entries_delete_trigger "
    "AFTER DELETE ON entries "
    "FOR EACH ROW "
    "BEGIN "
    "UPDATE usage_info SET total_disk_usage = total_disk_usage - "
    "ifnull(OLD.request_body_disk_size, 0) - "
    "ifnull(OLD.response_body_disk_size, 0) "
    "WHERE usage_info.id = 1; "
    "END";

// ---------
// End schema definition
// ---------

const uint32_t kMaxEntriesPerStatement = 255;

const uint32_t kPageSize = 4 * 1024;

// Grow the database in chunks to reduce fragmentation
const uint32_t kGrowthSize = 32 * 1024;
const uint32_t kGrowthPages = kGrowthSize / kPageSize;
static_assert(kGrowthSize % kPageSize == 0,
              "Growth size must be multiple of page size");

// Only release free pages when we have more than this limit
const int32_t kMaxFreePages = kGrowthPages;

// Limit WAL journal to a reasonable size
const uint32_t kWalAutoCheckpointSize = 512 * 1024;
const uint32_t kWalAutoCheckpointPages = kWalAutoCheckpointSize / kPageSize;
static_assert(kWalAutoCheckpointSize % kPageSize == 0,
              "WAL checkpoint size must be multiple of page size");

}  // namespace

// If any of the static_asserts below fail, it means that you have changed
// the corresponding WebIDL enum in a way that may be incompatible with the
// existing data stored in the DOM Cache.  You would need to update the Cache
// database schema accordingly and adjust the failing static_assert.
static_assert(int(HeadersGuardEnum::None) == 0 &&
                  int(HeadersGuardEnum::Request) == 1 &&
                  int(HeadersGuardEnum::Request_no_cors) == 2 &&
                  int(HeadersGuardEnum::Response) == 3 &&
                  int(HeadersGuardEnum::Immutable) == 4 &&
                  ContiguousEnumSize<HeadersGuardEnum>::value == 5,
              "HeadersGuardEnum values are as expected");
static_assert(int(ReferrerPolicy::_empty) == 0 &&
                  int(ReferrerPolicy::No_referrer) == 1 &&
                  int(ReferrerPolicy::No_referrer_when_downgrade) == 2 &&
                  int(ReferrerPolicy::Origin) == 3 &&
                  int(ReferrerPolicy::Origin_when_cross_origin) == 4 &&
                  int(ReferrerPolicy::Unsafe_url) == 5 &&
                  int(ReferrerPolicy::Same_origin) == 6 &&
                  int(ReferrerPolicy::Strict_origin) == 7 &&
                  int(ReferrerPolicy::Strict_origin_when_cross_origin) == 8 &&
                  ContiguousEnumSize<ReferrerPolicy>::value == 9,
              "ReferrerPolicy values are as expected");
static_assert(int(RequestMode::Same_origin) == 0 &&
                  int(RequestMode::No_cors) == 1 &&
                  int(RequestMode::Cors) == 2 &&
                  int(RequestMode::Navigate) == 3 &&
                  ContiguousEnumSize<RequestMode>::value == 4,
              "RequestMode values are as expected");
static_assert(int(RequestCredentials::Omit) == 0 &&
                  int(RequestCredentials::Same_origin) == 1 &&
                  int(RequestCredentials::Include) == 2 &&
                  ContiguousEnumSize<RequestCredentials>::value == 3,
              "RequestCredentials values are as expected");
static_assert(int(RequestCache::Default) == 0 &&
                  int(RequestCache::No_store) == 1 &&
                  int(RequestCache::Reload) == 2 &&
                  int(RequestCache::No_cache) == 3 &&
                  int(RequestCache::Force_cache) == 4 &&
                  int(RequestCache::Only_if_cached) == 5 &&
                  ContiguousEnumSize<RequestCache>::value == 6,
              "RequestCache values are as expected");
static_assert(int(RequestRedirect::Follow) == 0 &&
                  int(RequestRedirect::Error) == 1 &&
                  int(RequestRedirect::Manual) == 2 &&
                  ContiguousEnumSize<RequestRedirect>::value == 3,
              "RequestRedirect values are as expected");
static_assert(int(ResponseType::Basic) == 0 && int(ResponseType::Cors) == 1 &&
                  int(ResponseType::Default) == 2 &&
                  int(ResponseType::Error) == 3 &&
                  int(ResponseType::Opaque) == 4 &&
                  int(ResponseType::Opaqueredirect) == 5 &&
                  ContiguousEnumSize<ResponseType>::value == 6,
              "ResponseType values are as expected");

// If the static_asserts below fails, it means that you have changed the
// Namespace enum in a way that may be incompatible with the existing data
// stored in the DOM Cache.  You would need to update the Cache database schema
// accordingly and adjust the failing static_assert.
static_assert(DEFAULT_NAMESPACE == 0 && CHROME_ONLY_NAMESPACE == 1 &&
                  NUMBER_OF_NAMESPACES == 2,
              "Namespace values are as expected");

// If the static_asserts below fails, it means that you have changed the
// nsContentPolicy enum in a way that may be incompatible with the existing data
// stored in the DOM Cache.  You would need to update the Cache database schema
// accordingly and adjust the failing static_assert.
static_assert(
    nsIContentPolicy::TYPE_INVALID == 0 && nsIContentPolicy::TYPE_OTHER == 1 &&
        nsIContentPolicy::TYPE_SCRIPT == 2 &&
        nsIContentPolicy::TYPE_IMAGE == 3 &&
        nsIContentPolicy::TYPE_STYLESHEET == 4 &&
        nsIContentPolicy::TYPE_OBJECT == 5 &&
        nsIContentPolicy::TYPE_DOCUMENT == 6 &&
        nsIContentPolicy::TYPE_SUBDOCUMENT == 7 &&
        nsIContentPolicy::TYPE_PING == 10 &&
        nsIContentPolicy::TYPE_XMLHTTPREQUEST == 11 &&
        nsIContentPolicy::TYPE_DTD == 13 && nsIContentPolicy::TYPE_FONT == 14 &&
        nsIContentPolicy::TYPE_MEDIA == 15 &&
        nsIContentPolicy::TYPE_WEBSOCKET == 16 &&
        nsIContentPolicy::TYPE_CSP_REPORT == 17 &&
        nsIContentPolicy::TYPE_XSLT == 18 &&
        nsIContentPolicy::TYPE_BEACON == 19 &&
        nsIContentPolicy::TYPE_FETCH == 20 &&
        nsIContentPolicy::TYPE_IMAGESET == 21 &&
        nsIContentPolicy::TYPE_WEB_MANIFEST == 22 &&
        nsIContentPolicy::TYPE_INTERNAL_SCRIPT == 23 &&
        nsIContentPolicy::TYPE_INTERNAL_WORKER == 24 &&
        nsIContentPolicy::TYPE_INTERNAL_SHARED_WORKER == 25 &&
        nsIContentPolicy::TYPE_INTERNAL_EMBED == 26 &&
        nsIContentPolicy::TYPE_INTERNAL_OBJECT == 27 &&
        nsIContentPolicy::TYPE_INTERNAL_FRAME == 28 &&
        nsIContentPolicy::TYPE_INTERNAL_IFRAME == 29 &&
        nsIContentPolicy::TYPE_INTERNAL_AUDIO == 30 &&
        nsIContentPolicy::TYPE_INTERNAL_VIDEO == 31 &&
        nsIContentPolicy::TYPE_INTERNAL_TRACK == 32 &&
        nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_ASYNC == 33 &&
        nsIContentPolicy::TYPE_INTERNAL_EVENTSOURCE == 34 &&
        nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER == 35 &&
        nsIContentPolicy::TYPE_INTERNAL_SCRIPT_PRELOAD == 36 &&
        nsIContentPolicy::TYPE_INTERNAL_IMAGE == 37 &&
        nsIContentPolicy::TYPE_INTERNAL_IMAGE_PRELOAD == 38 &&
        nsIContentPolicy::TYPE_INTERNAL_STYLESHEET == 39 &&
        nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD == 40 &&
        nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON == 41 &&
        nsIContentPolicy::TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS == 42 &&
        nsIContentPolicy::TYPE_SAVEAS_DOWNLOAD == 43 &&
        nsIContentPolicy::TYPE_SPECULATIVE == 44 &&
        nsIContentPolicy::TYPE_INTERNAL_MODULE == 45 &&
        nsIContentPolicy::TYPE_INTERNAL_MODULE_PRELOAD == 46 &&
        nsIContentPolicy::TYPE_INTERNAL_DTD == 47 &&
        nsIContentPolicy::TYPE_INTERNAL_FORCE_ALLOWED_DTD == 48 &&
        nsIContentPolicy::TYPE_INTERNAL_AUDIOWORKLET == 49 &&
        nsIContentPolicy::TYPE_INTERNAL_PAINTWORKLET == 50 &&
        nsIContentPolicy::TYPE_INTERNAL_FONT_PRELOAD == 51 &&
        nsIContentPolicy::TYPE_INTERNAL_CHROMEUTILS_COMPILED_SCRIPT == 52 &&
        nsIContentPolicy::TYPE_INTERNAL_FRAME_MESSAGEMANAGER_SCRIPT == 53 &&
        nsIContentPolicy::TYPE_INTERNAL_FETCH_PRELOAD == 54 &&
        nsIContentPolicy::TYPE_UA_FONT == 55 &&
        nsIContentPolicy::TYPE_WEB_IDENTITY == 57 &&
        nsIContentPolicy::TYPE_INTERNAL_WORKER_STATIC_MODULE == 58 &&
        nsIContentPolicy::TYPE_WEB_TRANSPORT == 59 &&
        nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_SYNC == 60 &&
        nsIContentPolicy::TYPE_INTERNAL_EXTERNAL_RESOURCE == 61 &&
        nsIContentPolicy::TYPE_JSON == 62 &&
        nsIContentPolicy::TYPE_INTERNAL_JSON_PRELOAD == 63 &&
        nsIContentPolicy::TYPE_INTERNAL_IMAGE_NOTIFICATION == 64 &&
        nsIContentPolicy::TYPE_TEXT == 65 &&
        nsIContentPolicy::TYPE_INTERNAL_TEXT_PRELOAD == 66 &&
        nsIContentPolicy::TYPE_INTERNAL_SPECULATION_RULES == 67,
    "nsContentPolicyType values are as expected");

namespace {

using EntryId = int32_t;

struct IdCount {
  explicit IdCount(int32_t aId) : mId(aId), mCount(1) {}
  int32_t mId;
  int32_t mCount;
};

using EntryIds = AutoTArray<EntryId, 256>;

static Result<EntryIds, nsresult> QueryAll(mozIStorageConnection& aConn,
                                           CacheId aCacheId);
static Result<EntryIds, nsresult> QueryCache(mozIStorageConnection& aConn,
                                             CacheId aCacheId,
                                             const CacheRequest& aRequest,
                                             const CacheQueryParams& aParams,
                                             uint32_t aMaxResults = UINT32_MAX);
static Result<bool, nsresult> MatchByVaryHeader(mozIStorageConnection& aConn,
                                                const CacheRequest& aRequest,
                                                EntryId entryId);
// Returns a success tuple containing the deleted body ids, deleted security ids
// and deleted padding size.
static Result<std::tuple<nsTArray<nsID>, AutoTArray<IdCount, 16>, int64_t>,
              nsresult>
DeleteEntries(mozIStorageConnection& aConn,
              const nsTArray<EntryId>& aEntryIdList);

static Result<std::tuple<nsTArray<nsID>, AutoTArray<IdCount, 16>, int64_t>,
              nsresult>
DeleteAllCacheEntries(mozIStorageConnection& aConn, CacheId& aCacheId);

static nsresult DeleteOrphanedEntriesWithoutBodies(
    mozIStorageConnection& aConn);

static Result<int32_t, nsresult> InsertSecurityInfo(
    mozIStorageConnection& aConn, nsICryptoHash& aCrypto,
    nsITransportSecurityInfo* aSecurityInfo);
static nsresult DeleteSecurityInfo(mozIStorageConnection& aConn, int32_t aId,
                                   int32_t aCount);
static nsresult DeleteSecurityInfoList(
    mozIStorageConnection& aConn,
    const nsTArray<IdCount>& aDeletedStorageIdList);
static nsresult InsertEntry(mozIStorageConnection& aConn, CacheId aCacheId,
                            const CacheRequest& aRequest,
                            const nsID* aRequestBodyId,
                            const CacheResponse& aResponse,
                            const nsID* aResponseBodyId);
static Result<SavedResponse, nsresult> ReadResponse(
    mozIStorageConnection& aConn, EntryId aEntryId);
static Result<SavedRequest, nsresult> ReadRequest(mozIStorageConnection& aConn,
                                                  EntryId aEntryId);

static void AppendListParamsToQuery(nsACString& aQuery, size_t aLen);
static nsresult BindListParamsToQuery(mozIStorageStatement& aState,
                                      const Span<const EntryId>& aEntryIdList);
static nsresult BindId(mozIStorageStatement& aState, const nsACString& aName,
                       const nsID* aId);
static Result<nsID, nsresult> ExtractId(mozIStorageStatement& aState,
                                        uint32_t aPos);
static Result<NotNull<nsCOMPtr<mozIStorageStatement>>, nsresult>
CreateAndBindKeyStatement(mozIStorageConnection& aConn,
                          const char* aQueryFormat, const nsAString& aKey);
static Result<nsAutoCString, nsresult> HashCString(nsICryptoHash& aCrypto,
                                                   const nsACString& aIn);
Result<int32_t, nsresult> GetEffectiveSchemaVersion(
    mozIStorageConnection& aConn);
nsresult Validate(mozIStorageConnection& aConn);
nsresult Migrate(nsIFile& aDBDir, mozIStorageConnection& aConn);
}  // namespace

class MOZ_RAII AutoDisableForeignKeyChecking {
 public:
  explicit AutoDisableForeignKeyChecking(mozIStorageConnection* aConn)
      : mConn(aConn), mForeignKeyCheckingDisabled(false) {
    QM_TRY_INSPECT(const auto& state,
                   quota::CreateAndExecuteSingleStepStatement(
                       *mConn, "PRAGMA foreign_keys;"_ns),
                   QM_VOID);

    QM_TRY_INSPECT(const int32_t& mode,
                   MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 0), QM_VOID);

    if (mode) {
      QM_WARNONLY_TRY(MOZ_TO_RESULT(mConn->ExecuteSimpleSQL(
                                        "PRAGMA foreign_keys = OFF;"_ns))
                          .andThen([this](const auto) -> Result<Ok, nsresult> {
                            mForeignKeyCheckingDisabled = true;
                            return Ok{};
                          }));
    }
  }

  ~AutoDisableForeignKeyChecking() {
    if (mForeignKeyCheckingDisabled) {
      QM_WARNONLY_TRY(QM_TO_RESULT(
          mConn->ExecuteSimpleSQL("PRAGMA foreign_keys = ON;"_ns)));
    }
  }

 private:
  nsCOMPtr<mozIStorageConnection> mConn;
  bool mForeignKeyCheckingDisabled;
};

nsresult CreateOrMigrateSchema(nsIFile& aDBDir, mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_UNWRAP(int32_t schemaVersion, GetEffectiveSchemaVersion(aConn));

  if (schemaVersion == kLatestSchemaVersion) {
    // We already have the correct schema version.  Validate it matches
    // our expected schema and then proceed.
    QM_TRY(MOZ_TO_RESULT(Validate(aConn)));

    return NS_OK;
  }

  // Turn off checking foreign keys before starting a transaction, and restore
  // it once we're done.
  AutoDisableForeignKeyChecking restoreForeignKeyChecking(&aConn);
  mozStorageTransaction trans(&aConn, false,
                              mozIStorageConnection::TRANSACTION_IMMEDIATE);

  QM_TRY(MOZ_TO_RESULT(trans.Start()));

  const bool migrating = schemaVersion != 0;

  if (migrating) {
    // A schema exists, but its not the current version.  Attempt to
    // migrate it to our new schema.
    QM_TRY(MOZ_TO_RESULT(Migrate(aDBDir, aConn)));
  } else {
    // There is no schema installed.  Create the database from scratch.
    QM_TRY(
        MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(nsLiteralCString(kTableCaches))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kTableSecurityInfo))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kIndexSecurityInfoHash))));
    QM_TRY(
        MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(nsLiteralCString(kTableEntries))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kIndexEntriesRequest))));
    QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
        nsLiteralCString(kIndexEntriesPendingDeletion))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kTableRequestHeaders))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kTableResponseHeaders))));
    QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
        nsLiteralCString(kIndexResponseHeadersEntryId))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kTableResponseUrlList))));
    QM_TRY(
        MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(nsLiteralCString(kTableStorage))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kTableUsageInfo))));
    QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
        nsLiteralCString("INSERT INTO usage_info VALUES(1, 0);"))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kTriggerEntriesInsert))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kTriggerEntriesUpdate))));
    QM_TRY(MOZ_TO_RESULT(
        aConn.ExecuteSimpleSQL(nsLiteralCString(kTriggerEntriesDelete))));
    QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(kLatestSchemaVersion)));
    QM_TRY_UNWRAP(schemaVersion, GetEffectiveSchemaVersion(aConn));
  }

  QM_TRY(MOZ_TO_RESULT(Validate(aConn)));
  QM_TRY(MOZ_TO_RESULT(trans.Commit()));

  if (migrating) {
    // Migrations happen infrequently and reflect a chance in DB structure.
    // This is a good time to rebuild the database.  It also helps catch
    // if a new migration is incorrect by fast failing on the corruption.
    // Unfortunately, this must be performed outside of the transaction.

    QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL("VACUUM"_ns)));
  }

  return NS_OK;
}

nsresult InitializeConnection(mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  // This function needs to perform per-connection initialization tasks that
  // need to happen regardless of the schema.

  // Note, the default encoding of UTF-8 is preferred.  mozStorage does all
  // the work necessary to convert UTF-16 nsString values for us.  We don't
  // need ordering and the binary equality operations are correct.  So, do
  // NOT set PRAGMA encoding to UTF-16.

  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(nsPrintfCString(
      // Use a smaller page size to improve perf/footprint; default is too large
      "PRAGMA page_size = %u; "
      // Enable auto_vacuum; this must happen after page_size and before WAL
      "PRAGMA auto_vacuum = INCREMENTAL; "
      "PRAGMA foreign_keys = ON; ",
      kPageSize))));

  // Limit fragmentation by growing the database by many pages at once.
  QM_TRY(QM_OR_ELSE_WARN_IF(
      // Expression.
      MOZ_TO_RESULT(aConn.SetGrowthIncrement(kGrowthSize, ""_ns)),
      // Predicate.
      IsSpecificError<NS_ERROR_FILE_TOO_BIG>,
      // Fallback.
      ErrToDefaultOk<>));

  // Enable WAL journaling.  This must be performed in a separate transaction
  // after changing the page_size and enabling auto_vacuum.
  // Note there is a default journal_size_limit set by mozStorage.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(nsPrintfCString(
      // WAL journal can grow to given number of *pages*
      "PRAGMA wal_autocheckpoint = %u; "
      // WAL must be enabled at the end to allow page size to be changed, etc.
      "PRAGMA journal_mode = WAL; ",
      kWalAutoCheckpointPages))));

  // Verify that we successfully set the vacuum mode to incremental.  It
  // is very easy to put the database in a state where the auto_vacuum
  // pragma above fails silently.
#ifdef DEBUG
  {
    QM_TRY_INSPECT(const auto& state,
                   quota::CreateAndExecuteSingleStepStatement(
                       aConn, "PRAGMA auto_vacuum;"_ns));

    QM_TRY_INSPECT(const int32_t& mode,
                   MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 0));

    // integer value 2 is incremental mode
    QM_TRY(OkIf(mode == 2), NS_ERROR_UNEXPECTED);
  }
#endif

  return NS_OK;
}

Result<CacheId, nsresult> CreateCacheId(mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL("INSERT INTO caches DEFAULT VALUES;"_ns)));

  QM_TRY_INSPECT(const auto& state,
                 quota::CreateAndExecuteSingleStepStatement<
                     quota::SingleStepResult::ReturnNullIfNoResult>(
                     aConn, "SELECT last_insert_rowid()"_ns));

  QM_TRY(OkIf(state), Err(NS_ERROR_UNEXPECTED));

  QM_TRY_INSPECT(const CacheId& id,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt64, 0));

  return id;
}

Result<DeletionInfo, nsresult> DeleteCacheId(mozIStorageConnection& aConn,
                                             CacheId aCacheId) {
  MOZ_ASSERT(!NS_IsMainThread());

  // XXX only deletedBodyIdList needs to be non-const
  QM_TRY_UNWRAP(
      (auto [deletedBodyIdList, deletedSecurityIdList, deletedPaddingSize]),
      DeleteAllCacheEntries(aConn, aCacheId));

  QM_TRY(MOZ_TO_RESULT(DeleteSecurityInfoList(aConn, deletedSecurityIdList)));

  // Delete the remainder of the cache using cascade semantics, unless entries
  // pending deletion still reference it. In that case the row outlives the
  // deletion until the next context open, where DeleteEmptyOrphanedCaches()
  // removes it: at runtime, the row cannot be told apart from an empty
  // orphaned cache that content still references.
  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "DELETE FROM caches WHERE id=:id "
                     "AND NOT EXISTS (SELECT 1 FROM entries "
                     "WHERE cache_id=:id);"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("id"_ns, aCacheId)));

  QM_TRY(MOZ_TO_RESULT(state->Execute()));

  return DeletionInfo{std::move(deletedBodyIdList), deletedPaddingSize};
}

Result<AutoTArray<CacheId, 8>, nsresult> FindOrphanedCacheIds(
    mozIStorageConnection& aConn) {
  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "SELECT id FROM caches "
                     "WHERE id NOT IN (SELECT cache_id from storage);"_ns));

  QM_TRY_RETURN(
      (quota::CollectElementsWhileHasResultTyped<AutoTArray<CacheId, 8>>(
          *state, [](auto& stmt) {
            QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 0));
          })));
}

nsresult DeleteEmptyOrphanedCaches(mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "DELETE FROM caches WHERE id NOT IN (SELECT cache_id FROM storage) "
      "AND NOT EXISTS (SELECT 1 FROM entries WHERE cache_id=caches.id);"_ns)));

  return NS_OK;
}

nsresult DeleteAllOrphanedEntries(mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  // entries_delete_trigger subtracts the body disk sizes from usage_info, and
  // dependent records are removed via ON DELETE CASCADE.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "DELETE FROM entries WHERE pending_deletion=1;"_ns)));

  return NS_OK;
}

Result<OrphanedBodies, nsresult> FindOrphanedBodies(
    mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "SELECT request_body_id, response_body_id, "
                     "ifnull(request_body_disk_size, 0) + "
                     "ifnull(response_body_disk_size, 0) "
                     "FROM entries WHERE pending_deletion=1;"_ns));

  OrphanedBodies orphanedBodies;
  QM_TRY(quota::CollectWhileHasResult(
      *state, [&orphanedBodies](auto& stmt) -> Result<Ok, nsresult> {
        // extract up to 2 nsID structs per row
        for (uint32_t i = 0; i < 2; ++i) {
          QM_TRY_INSPECT(const bool& isNull,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetIsNull, i));
          if (!isNull) {
            QM_TRY_INSPECT(const auto& id, ExtractId(stmt, i));
            orphanedBodies.mBodyIdList.AppendElement(id);
          }
        }

        QM_TRY_INSPECT(const int64_t& diskSize,
                       MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 2));
        MOZ_ASSERT(diskSize >= 0);
        orphanedBodies.mDiskSize += diskSize;

        return Ok{};
      }));

  return orphanedBodies;
}

namespace {
// Helper function for the SQL update for deleting orphan bodies.
// Used by DeleteOrphanedBodyIds. Returns the disk size the database recorded
// for the detached bodies.
Result<int64_t, nsresult> DeleteOrphanedBodyIdsDb(
    mozIStorageConnection& aConn, const nsLiteralCString& aBodyType,
    const nsTArray<nsCString>& aBodyIds) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aBodyType == "request_body" || aBodyType == "response_body");

  const nsCString whereClause = "WHERE pending_deletion=1 AND "_ns + aBodyType +
                                "_id IN carray(:ids);"_ns;

  // Sum what entries_update_trigger will subtract from usage_info below.
  QM_TRY_INSPECT(
      const auto& sumState,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
          "SELECT ifnull(SUM("_ns + aBodyType + "_disk_size), 0) "_ns +
              "FROM entries "_ns + whereClause));
  QM_TRY(MOZ_TO_RESULT(
      sumState->BindArrayOfUTF8StringsByName("ids"_ns, aBodyIds)));
  QM_TRY_INSPECT(const DebugOnly<bool> hasResult,
                 MOZ_TO_RESULT_INVOKE_MEMBER(sumState, ExecuteStep));
  MOZ_ASSERT(hasResult);
  QM_TRY_INSPECT(const int64_t& diskSize,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*sumState, GetInt64, 0));
  MOZ_ASSERT(diskSize >= 0);

  // Detaching a body from its entry makes entries_update_trigger subtract its
  // disk size from usage_info.
  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "UPDATE entries SET "_ns + aBodyType + "_id=NULL, "_ns +
                         aBodyType + "_disk_size=0 "_ns + whereClause));
  QM_TRY(
      MOZ_TO_RESULT(state->BindArrayOfUTF8StringsByName("ids"_ns, aBodyIds)));
  QM_TRY(MOZ_TO_RESULT(state->Execute()));

  return diskSize;
}
}  // namespace

Result<int64_t, nsresult> DeleteOrphanedBodyIds(
    mozIStorageConnection& aConn, const nsTArray<nsID>& aBodyIdList) {
  MOZ_ASSERT(!NS_IsMainThread());

  int64_t diskSize = 0;

  if (!aBodyIdList.IsEmpty()) {
    nsTArray<nsCString> bodyIds(aBodyIdList.Length());
    for (const nsID& bodyId : aBodyIdList) {
      bodyIds.AppendElement(bodyId.ToString().get());
    }

    QM_TRY_INSPECT(const int64_t& requestDiskSize,
                   DeleteOrphanedBodyIdsDb(aConn, "request_body"_ns, bodyIds));
    QM_TRY_INSPECT(const int64_t& responseDiskSize,
                   DeleteOrphanedBodyIdsDb(aConn, "response_body"_ns, bodyIds));

    diskSize = requestDiskSize + responseDiskSize;
  }

  QM_TRY(MOZ_TO_RESULT(DeleteOrphanedEntriesWithoutBodies(aConn)));

  return diskSize;
}

Result<int64_t, nsresult> FindOverallPaddingSize(mozIStorageConnection& aConn) {
  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "SELECT response_padding_size FROM entries "
                     "WHERE response_padding_size IS NOT NULL;"_ns));

  int64_t overallPaddingSize = 0;

  QM_TRY(quota::CollectWhileHasResult(
      *state, [&overallPaddingSize](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const int64_t& padding_size,
                       MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 0));

        MOZ_DIAGNOSTIC_ASSERT(padding_size >= 0);
        MOZ_DIAGNOSTIC_ASSERT(INT64_MAX - padding_size >= overallPaddingSize);
        overallPaddingSize += padding_size;

        return Ok{};
      }));

  return overallPaddingSize;
}

Result<int64_t, nsresult> GetTotalDiskUsage(mozIStorageConnection& aConn) {
  QM_TRY_INSPECT(
      const auto& state,
      quota::CreateAndExecuteSingleStepStatement(
          aConn, "SELECT total_disk_usage FROM usage_info WHERE id = 1;"_ns));

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt64, 0));
}

Result<nsTHashSet<nsID>, nsresult> GetKnownBodyIds(
    mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  int32_t numEntries = 0;
  {
    QM_TRY_INSPECT(const auto& cnt,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "SELECT COUNT(*) FROM entries;"_ns));

    QM_TRY(quota::CollectWhileHasResult(
        *cnt, [&numEntries](auto& stmt) -> Result<Ok, nsresult> {
          QM_TRY(MOZ_TO_RESULT(stmt.GetInt32(0, &numEntries)));

          return Ok{};
        }));
  }

  // Each row can have 0 to 2 nsID values, prepare for the maximum.
  nsTHashSet<nsID> idSet(numEntries * 2);

  QM_TRY_INSPECT(
      const auto& state,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
          "SELECT request_body_id, response_body_id FROM entries;"_ns));

  QM_TRY(quota::CollectWhileHasResult(
      *state, [&idSet](auto& stmt) -> Result<Ok, nsresult> {
        // extract 0 to 2 nsID structs per row
        for (uint32_t i = 0; i < 2; ++i) {
          QM_TRY_INSPECT(const bool& isNull,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetIsNull, i));

          if (!isNull) {
            QM_TRY_INSPECT(const auto& id, ExtractId(stmt, i));

            idSet.Insert(id);
          }
        }

        return Ok{};
      }));

  return std::move(idSet);
}

Result<Maybe<SavedResponse>, nsresult> CacheMatch(
    mozIStorageConnection& aConn, CacheId aCacheId,
    const CacheRequest& aRequest, const CacheQueryParams& aParams) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(const auto& matches,
                 QueryCache(aConn, aCacheId, aRequest, aParams, 1));

  if (matches.IsEmpty()) {
    return Maybe<SavedResponse>();
  }

  QM_TRY_UNWRAP(auto response, ReadResponse(aConn, matches[0]));

  response.mCacheId = aCacheId;

  return Some(std::move(response));
}

Result<nsTArray<SavedResponse>, nsresult> CacheMatchAll(
    mozIStorageConnection& aConn, CacheId aCacheId,
    const Maybe<CacheRequest>& aMaybeRequest, const CacheQueryParams& aParams) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(
      const auto& matches, ([&aConn, aCacheId, &aMaybeRequest, &aParams] {
        if (aMaybeRequest.isNothing()) {
          QM_TRY_RETURN(QueryAll(aConn, aCacheId));
        }

        QM_TRY_RETURN(
            QueryCache(aConn, aCacheId, aMaybeRequest.ref(), aParams));
      }()));

  // TODO: replace this with a bulk load using SQL IN clause (bug 1110458)
  QM_TRY_RETURN(TransformIntoNewArrayAbortOnErr(
      matches,
      [&aConn, aCacheId](const auto match) -> Result<SavedResponse, nsresult> {
        QM_TRY_UNWRAP(auto savedResponse, ReadResponse(aConn, match));

        savedResponse.mCacheId = aCacheId;
        return savedResponse;
      },
      fallible));
}

Result<DeletionInfo, nsresult> CachePut(mozIStorageConnection& aConn,
                                        CacheId aCacheId,
                                        const CacheRequest& aRequest,
                                        const nsID* aRequestBodyId,
                                        const CacheResponse& aResponse,
                                        const nsID* aResponseBodyId) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(
      const auto& matches,
      QueryCache(aConn, aCacheId, aRequest,
                 CacheQueryParams(false, false, false, false, u""_ns)));

  // XXX only deletedBodyIdList needs to be non-const
  QM_TRY_UNWRAP(
      (auto [deletedBodyIdList, deletedSecurityIdList, deletedPaddingSize]),
      DeleteEntries(aConn, matches));

  QM_TRY(MOZ_TO_RESULT(InsertEntry(aConn, aCacheId, aRequest, aRequestBodyId,
                                   aResponse, aResponseBodyId)));

  // Delete the security values after doing the insert to avoid churning
  // the security table when its not necessary.
  QM_TRY(MOZ_TO_RESULT(DeleteSecurityInfoList(aConn, deletedSecurityIdList)));

  return DeletionInfo{std::move(deletedBodyIdList), deletedPaddingSize};
}

Result<Maybe<DeletionInfo>, nsresult> CacheDelete(
    mozIStorageConnection& aConn, CacheId aCacheId,
    const CacheRequest& aRequest, const CacheQueryParams& aParams) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(const auto& matches,
                 QueryCache(aConn, aCacheId, aRequest, aParams));

  if (matches.IsEmpty()) {
    return Maybe<DeletionInfo>();
  }

  // XXX only deletedBodyIdList needs to be non-const
  QM_TRY_UNWRAP(
      (auto [deletedBodyIdList, deletedSecurityIdList, deletedPaddingSize]),
      DeleteEntries(aConn, matches));

  QM_TRY(MOZ_TO_RESULT(DeleteSecurityInfoList(aConn, deletedSecurityIdList)));

  return Some(DeletionInfo{std::move(deletedBodyIdList), deletedPaddingSize});
}

Result<nsTArray<SavedRequest>, nsresult> CacheKeys(
    mozIStorageConnection& aConn, CacheId aCacheId,
    const Maybe<CacheRequest>& aMaybeRequest, const CacheQueryParams& aParams) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(
      const auto& matches, ([&aConn, aCacheId, &aMaybeRequest, &aParams] {
        if (aMaybeRequest.isNothing()) {
          QM_TRY_RETURN(QueryAll(aConn, aCacheId));
        }

        QM_TRY_RETURN(
            QueryCache(aConn, aCacheId, aMaybeRequest.ref(), aParams));
      }()));

  // TODO: replace this with a bulk load using SQL IN clause (bug 1110458)
  QM_TRY_RETURN(TransformIntoNewArrayAbortOnErr(
      matches,
      [&aConn, aCacheId](const auto match) -> Result<SavedRequest, nsresult> {
        QM_TRY_UNWRAP(auto savedRequest, ReadRequest(aConn, match));

        savedRequest.mCacheId = aCacheId;
        return savedRequest;
      },
      fallible));
}

Result<Maybe<SavedResponse>, nsresult> StorageMatch(
    mozIStorageConnection& aConn, Namespace aNamespace,
    const CacheRequest& aRequest, const CacheQueryParams& aParams) {
  MOZ_ASSERT(!NS_IsMainThread());

  // If we are given a cache to check, then simply find its cache ID
  // and perform the match.
  if (aParams.cacheNameSet()) {
    QM_TRY_INSPECT(const auto& maybeCacheId,
                   StorageGetCacheId(aConn, aNamespace, aParams.cacheName()));
    if (maybeCacheId.isNothing()) {
      return Maybe<SavedResponse>();
    }

    return CacheMatch(aConn, maybeCacheId.ref(), aRequest, aParams);
  }

  // Otherwise we need to get a list of all the cache IDs in this namespace.

  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "SELECT cache_id FROM storage WHERE "
                     "namespace=:namespace ORDER BY rowid;"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("namespace"_ns, aNamespace)));

  QM_TRY_INSPECT(
      const auto& cacheIdList,
      (quota::CollectElementsWhileHasResultTyped<AutoTArray<CacheId, 32>>(
          *state, [](auto& stmt) {
            QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 0));
          })));

  // Now try to find a match in each cache in order
  for (const auto cacheId : cacheIdList) {
    QM_TRY_UNWRAP(auto matchedResponse,
                  CacheMatch(aConn, cacheId, aRequest, aParams));

    if (matchedResponse.isSome()) {
      return matchedResponse;
    }
  }

  return Maybe<SavedResponse>();
}

Result<Maybe<CacheId>, nsresult> StorageGetCacheId(mozIStorageConnection& aConn,
                                                   Namespace aNamespace,
                                                   const nsAString& aKey) {
  MOZ_ASSERT(!NS_IsMainThread());

  // How we constrain the key column depends on the value of our key.  Use
  // a format string for the query and let CreateAndBindKeyStatement() fill
  // it in for us.
  const char* const query =
      "SELECT cache_id FROM storage "
      "WHERE namespace=:namespace AND %s "
      "ORDER BY rowid;";

  QM_TRY_INSPECT(const auto& state,
                 CreateAndBindKeyStatement(aConn, query, aKey));

  QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("namespace"_ns, aNamespace)));

  QM_TRY_INSPECT(const bool& hasMoreData,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*state, ExecuteStep));

  if (!hasMoreData) {
    return Maybe<CacheId>();
  }

  QM_TRY_RETURN(
      MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt64, 0).map(Some<CacheId>));
}

nsresult StoragePutCache(mozIStorageConnection& aConn, Namespace aNamespace,
                         const nsAString& aKey, CacheId aCacheId) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "INSERT INTO storage (namespace, key, cache_id) "
                     "VALUES (:namespace, :key, :cache_id);"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("namespace"_ns, aNamespace)));
  QM_TRY(MOZ_TO_RESULT(state->BindStringAsBlobByName("key"_ns, aKey)));
  QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("cache_id"_ns, aCacheId)));
  QM_TRY(MOZ_TO_RESULT(state->Execute()));

  return NS_OK;
}

nsresult StorageForgetCache(mozIStorageConnection& aConn, Namespace aNamespace,
                            const nsAString& aKey) {
  MOZ_ASSERT(!NS_IsMainThread());

  // How we constrain the key column depends on the value of our key.  Use
  // a format string for the query and let CreateAndBindKeyStatement() fill
  // it in for us.
  const char* const query =
      "DELETE FROM storage WHERE namespace=:namespace AND %s;";

  QM_TRY_INSPECT(const auto& state,
                 CreateAndBindKeyStatement(aConn, query, aKey));

  QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("namespace"_ns, aNamespace)));

  QM_TRY(MOZ_TO_RESULT(state->Execute()));

  return NS_OK;
}

Result<nsTArray<nsString>, nsresult> StorageGetKeys(
    mozIStorageConnection& aConn, Namespace aNamespace) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(
      const auto& state,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
          "SELECT key FROM storage WHERE namespace=:namespace ORDER BY rowid;"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("namespace"_ns, aNamespace)));

  QM_TRY_RETURN(quota::CollectElementsWhileHasResult(*state, [](auto& stmt) {
    QM_TRY_RETURN(
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, stmt, GetBlobAsString, 0));
  }));
}

namespace {

Result<EntryIds, nsresult> QueryAll(mozIStorageConnection& aConn,
                                    CacheId aCacheId) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "SELECT id FROM entries WHERE cache_id=:cache_id "
                     "AND pending_deletion=0 ORDER BY id;"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("cache_id"_ns, aCacheId)));

  QM_TRY_RETURN((quota::CollectElementsWhileHasResultTyped<EntryIds>(
      *state, [](auto& stmt) {
        QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 0));
      })));
}

Result<EntryIds, nsresult> QueryCache(mozIStorageConnection& aConn,
                                      CacheId aCacheId,
                                      const CacheRequest& aRequest,
                                      const CacheQueryParams& aParams,
                                      uint32_t aMaxResults) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aMaxResults > 0);

  if (!aParams.ignoreMethod() &&
      !aRequest.method().LowerCaseEqualsLiteral("get")) {
    return Result<EntryIds, nsresult>{std::in_place};
  }

  // We use truncated SHA-1 hashes (64 bits) in the WHERE clause for O(log n)
  // index lookup performance. The full URL strings are retrieved in the SELECT
  // and verified post-query to handle the extremely rare case of hash
  // collisions. This approach avoids O(n) string comparisons in the SQL query
  // while maintaining correctness.
  //
  // We use a simplified query that avoids the expensive LEFT JOIN, GROUP BY,
  // and COUNT() operations. The vary count check is done separately only when
  // needed, avoiding ~300μs overhead per query.
  nsAutoCString query(
      "SELECT id, response_type, request_url_no_query, request_url_query "
      "FROM entries "
      "WHERE entries.cache_id=:cache_id "
      "AND entries.pending_deletion=0 "
      "AND entries.request_url_no_query_hash=:url_no_query_hash ");

  if (!aParams.ignoreSearch()) {
    query.AppendLiteral("AND entries.request_url_query_hash=:url_query_hash ");
  }

  query.AppendLiteral("ORDER BY entries.id;");

  QM_TRY_INSPECT(const auto& state, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                        nsCOMPtr<mozIStorageStatement>, aConn,
                                        CreateStatement, query));

  QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("cache_id"_ns, aCacheId)));

  QM_TRY_INSPECT(const auto& crypto,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<nsICryptoHash>,
                                         MOZ_SELECT_OVERLOAD(do_CreateInstance),
                                         NS_CRYPTO_HASH_CONTRACTID));

  QM_TRY_INSPECT(const auto& urlWithoutQueryHash,
                 HashCString(*crypto, aRequest.urlWithoutQuery()));

  QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringAsBlobByName("url_no_query_hash"_ns,
                                                         urlWithoutQueryHash)));

  if (!aParams.ignoreSearch()) {
    QM_TRY_INSPECT(const auto& urlQueryHash,
                   HashCString(*crypto, aRequest.urlQuery()));

    QM_TRY(MOZ_TO_RESULT(
        state->BindUTF8StringAsBlobByName("url_query_hash"_ns, urlQueryHash)));
  }

  EntryIds entryIdList;

  QM_TRY(CollectWhile(
      [&state, &entryIdList, aMaxResults]() -> Result<bool, nsresult> {
        if (entryIdList.Length() == aMaxResults) {
          return false;
        }
        QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(state, ExecuteStep));
      },
      [&state, &entryIdList, &aParams, &aConn,
       &aRequest]() -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const EntryId& entryId,
                       MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 0));

        QM_TRY_INSPECT(const int32_t& responseType,
                       MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 1));

        QM_TRY_INSPECT(const auto& cachedUrlNoQuery,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, state,
                                                         GetUTF8String, 2));

        QM_TRY_INSPECT(const auto& cachedUrlQuery,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, state,
                                                         GetUTF8String, 3));

        // Verify URLs match to handle potential hash collisions. We use
        // truncated SHA-1 (64 bits) for indexing performance, but verify the
        // full URL post-query to ensure correctness. Hash collisions are
        // extremely rare (~1 in 92 million for 20k entries) but possible.
        if (!aRequest.urlWithoutQuery().Equals(cachedUrlNoQuery)) {
          return Ok{};
        }

        if (!aParams.ignoreSearch() &&
            !aRequest.urlQuery().Equals(cachedUrlQuery)) {
          return Ok{};
        }

        auto ignoreVary =
            aParams.ignoreVary() ||
            responseType == static_cast<int>(ResponseType::Opaque);

        if (!ignoreVary) {
          QM_TRY_INSPECT(const bool& matchedByVary,
                         MatchByVaryHeader(aConn, aRequest, entryId));
          if (!matchedByVary) {
            return Ok{};
          }
        }

        entryIdList.AppendElement(entryId);

        return Ok{};
      }));

  return entryIdList;
}

Result<bool, nsresult> MatchByVaryHeader(mozIStorageConnection& aConn,
                                         const CacheRequest& aRequest,
                                         EntryId entryId) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(
      const auto& varyValues,
      ([&aConn, entryId]() -> Result<AutoTArray<nsCString, 8>, nsresult> {
        QM_TRY_INSPECT(
            const auto& state,
            MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                "SELECT value FROM response_headers "
                "WHERE name='vary' COLLATE NOCASE "
                "AND entry_id=:entry_id;"_ns));

        QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("entry_id"_ns, entryId)));

        QM_TRY_RETURN((
            quota::CollectElementsWhileHasResultTyped<AutoTArray<nsCString, 8>>(
                *state, [](auto& stmt) {
                  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                      nsCString, stmt, GetUTF8String, 0));
                })));
      }()));

  // If there are no vary headers, the request matches
  if (varyValues.IsEmpty()) {
    return true;
  }

  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "SELECT name, value FROM request_headers "
                     "WHERE entry_id=:entry_id;"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("entry_id"_ns, entryId)));

  RefPtr<InternalHeaders> cachedHeaders =
      new InternalHeaders(HeadersGuardEnum::None);

  QM_TRY(quota::CollectWhileHasResult(
      *state, [&cachedHeaders](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const auto& name,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                         GetUTF8String, 0));
        QM_TRY_INSPECT(const auto& value,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                         GetUTF8String, 1));

        ErrorResult errorResult;

        cachedHeaders->Append(name, value, errorResult);
        if (errorResult.Failed()) {
          return Err(errorResult.StealNSResult());
        }

        return Ok{};
      }));

  RefPtr<InternalHeaders> queryHeaders =
      TypeUtils::ToInternalHeaders(aRequest.headers());

  // Assume the vary headers match until we find a conflict
  bool varyHeadersMatch = true;

  for (const auto& varyValue : varyValues) {
    // Extract the header names inside the Vary header value.
    bool bailOut = false;
    for (const nsACString& header :
         nsCCharSeparatedTokenizer(varyValue, NS_HTTP_HEADER_SEP).ToRange()) {
      MOZ_DIAGNOSTIC_ASSERT(!header.EqualsLiteral("*"),
                            "We should have already caught this in "
                            "TypeUtils::ToPCacheResponseWithoutBody()");

      ErrorResult errorResult;
      nsAutoCString queryValue;
      queryHeaders->Get(header, queryValue, errorResult);
      if (errorResult.Failed()) {
        errorResult.SuppressException();
        MOZ_DIAGNOSTIC_ASSERT(queryValue.IsEmpty());
      }

      nsAutoCString cachedValue;
      cachedHeaders->Get(header, cachedValue, errorResult);
      if (errorResult.Failed()) {
        errorResult.SuppressException();
        MOZ_DIAGNOSTIC_ASSERT(cachedValue.IsEmpty());
      }

      if (queryValue != cachedValue) {
        varyHeadersMatch = false;
        bailOut = true;
        break;
      }
    }

    if (bailOut) {
      break;
    }
  }

  return varyHeadersMatch;
}

static nsresult SelectAndDeleteEntriesInternal(
    mozIStorageConnection& aConn, const Span<const EntryId>& aEntryIdList,
    nsTArray<nsID>& aDeletedBodyIdListOut,
    nsTArray<IdCount>& aDeletedSecurityIdListOut,
    int64_t& aDeletedPaddingSizeOut) {
  nsAutoCString query(
      "SELECT "
      "request_body_id, "
      "response_body_id, "
      "response_security_info_id, "
      "response_padding_size "
      "FROM entries WHERE id IN (");

  AppendListParamsToQuery(query, aEntryIdList.Length());
  query.AppendLiteral(")");

  QM_TRY_INSPECT(const auto& state, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                        nsCOMPtr<mozIStorageStatement>, aConn,
                                        CreateStatement, query));

  QM_TRY(MOZ_TO_RESULT(BindListParamsToQuery(*state, aEntryIdList)));

  int64_t overallPaddingSize = 0;

  QM_TRY(quota::CollectWhileHasResult(
      *state,
      [&overallPaddingSize, &aDeletedBodyIdListOut,
       &aDeletedSecurityIdListOut](auto& stmt) -> Result<Ok, nsresult> {
        // extract 0 to 2 nsID structs per row
        for (uint32_t i = 0; i < 2; ++i) {
          QM_TRY_INSPECT(const bool& isNull,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetIsNull, i));

          if (!isNull) {
            QM_TRY_INSPECT(const auto& id, ExtractId(stmt, i));

            aDeletedBodyIdListOut.AppendElement(id);
          }
        }

        {  // and then a possible third entry for the security id
          QM_TRY_INSPECT(const bool& isNull,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetIsNull, 2));

          if (!isNull) {
            QM_TRY_INSPECT(const int32_t& securityId,
                           MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 2));

            // XXXtt: Consider using map for aDeletedSecuityIdListOut.
            auto foundIt =
                std::find_if(aDeletedSecurityIdListOut.begin(),
                             aDeletedSecurityIdListOut.end(),
                             [securityId](const auto& deletedSecurityId) {
                               return deletedSecurityId.mId == securityId;
                             });

            if (foundIt == aDeletedSecurityIdListOut.end()) {
              // Add a new entry for this ID with a count of 1, if it's not in
              // the list
              aDeletedSecurityIdListOut.AppendElement(IdCount(securityId));
            } else {
              // Otherwise, increment the count for this ID
              foundIt->mCount += 1;
            }
          }
        }

        {
          // It's possible to have null padding size for non-opaque response
          QM_TRY_INSPECT(const bool& isNull,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetIsNull, 3));

          if (!isNull) {
            QM_TRY_INSPECT(const int64_t& paddingSize,
                           MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 3));

            MOZ_DIAGNOSTIC_ASSERT(paddingSize >= 0);
            MOZ_DIAGNOSTIC_ASSERT(INT64_MAX - overallPaddingSize >=
                                  paddingSize);
            overallPaddingSize += paddingSize;
          }
        }

        return Ok{};
      }));

  aDeletedPaddingSizeOut += overallPaddingSize;

  // The entries are only marked as pending deletion, since content may still
  // be reading their body files. They are deleted by DeleteOrphanedBodyIds()
  // once the files are, and dependent records are removed via ON DELETE
  // CASCADE at that point.

  query =
      "UPDATE entries SET pending_deletion=1, "
      "response_security_info_id=NULL, response_padding_size=0 "
      "WHERE id IN ("_ns;
  AppendListParamsToQuery(query, aEntryIdList.Length());
  query.AppendLiteral(")");

  {
    QM_TRY_INSPECT(const auto& state, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                          nsCOMPtr<mozIStorageStatement>, aConn,
                                          CreateStatement, query));

    QM_TRY(MOZ_TO_RESULT(BindListParamsToQuery(*state, aEntryIdList)));

    QM_TRY(MOZ_TO_RESULT(state->Execute()));
  }

  return NS_OK;
}

static nsresult DeleteEntriesInternal(
    mozIStorageConnection& aConn, const nsTArray<EntryId>& aEntryIdList,
    nsTArray<nsID>& aDeletedBodyIdListOut,
    nsTArray<IdCount>& aDeletedSecurityIdListOut,
    int64_t& aDeletedPaddingSizeOut, uint32_t aPos, uint32_t aLen) {
  MOZ_ASSERT(!NS_IsMainThread());

  if (aEntryIdList.IsEmpty()) {
    return NS_OK;
  }

  MOZ_DIAGNOSTIC_ASSERT(aPos < aEntryIdList.Length());

  auto remaining = aLen;
  uint32_t currPos = 0;

  do {
    // Sqlite limits the number of entries allowed for an IN clause,
    // so split up larger operations.
    auto currLen = std::min(kMaxEntriesPerStatement, remaining);

    QM_TRY(MOZ_TO_RESULT(SelectAndDeleteEntriesInternal(
        aConn, Span<const EntryId>(aEntryIdList.Elements() + currPos, currLen),
        aDeletedBodyIdListOut, aDeletedSecurityIdListOut,
        aDeletedPaddingSizeOut)));

    remaining -= currLen;
    currPos += currLen;

  } while (remaining > 0);

  QM_TRY(MOZ_TO_RESULT(DeleteOrphanedEntriesWithoutBodies(aConn)));

  return NS_OK;
}

Result<std::tuple<nsTArray<nsID>, AutoTArray<IdCount, 16>, int64_t>, nsresult>
DeleteEntries(mozIStorageConnection& aConn,
              const nsTArray<EntryId>& aEntryIdList) {
  auto result =
      std::make_tuple(nsTArray<nsID>{}, AutoTArray<IdCount, 16>{}, int64_t{0});

  QM_TRY(MOZ_TO_RESULT(DeleteEntriesInternal(
      aConn, aEntryIdList, std::get<0>(result), std::get<1>(result),
      std::get<2>(result), 0, aEntryIdList.Length())));

  return result;
}

Result<std::tuple<nsTArray<nsID>, AutoTArray<IdCount, 16>, int64_t>, nsresult>
DeleteAllCacheEntries(mozIStorageConnection& aConn, CacheId& aCacheId) {
  auto result =
      std::make_tuple(nsTArray<nsID>{}, AutoTArray<IdCount, 16>{}, int64_t{0});
  auto& deletedBodyIdList = std::get<0>(result);
  auto& deletedSecurityIdList = std::get<1>(result);
  auto& deletedPaddingSize = std::get<2>(result);

  // XXX: We could create a query string with aggregation that would generate
  // a single summary result such that we don't have to go through each row
  // just to aggregate the result. This method could become much more
  // performant.
  //
  // The columns could look like:
  //
  // GROUP_CONCAT(request_body_id || ',' || response_body_id),
  // GROUP_CONCAT(response_security_info_id),
  // SUM(response_padding_size)
  //
  // strtok the result row to generate the desired output to fill-in
  // deletedBodyIdList, deletedSecurityIdList and deletedPaddingSize
  //
  // I am not sure about the memory requirements for such operation;
  // it will all depend upon the result filtered by the `cache_id`.
  nsAutoCString query(
      "SELECT "
      "request_body_id, "
      "response_body_id, "
      "response_security_info_id, "
      "response_padding_size "
      "FROM entries WHERE cache_id=:cache_id AND pending_deletion=0 "
      "ORDER BY id;"_ns);

  QM_TRY_INSPECT(const auto& state, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                        nsCOMPtr<mozIStorageStatement>, aConn,
                                        CreateStatement, query));

  QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("cache_id"_ns, aCacheId)));

  QM_TRY(quota::CollectWhileHasResult(
      *state,
      [&deletedPaddingSize, &deletedBodyIdList,
       &deletedSecurityIdList](auto& stmt) -> Result<Ok, nsresult> {
        // extract 0 to 2 nsID structs per row
        for (uint32_t i = 0; i < 2; ++i) {
          QM_TRY_INSPECT(const bool& isNull,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetIsNull, i));

          if (!isNull) {
            QM_TRY_INSPECT(const auto& id, ExtractId(stmt, i));

            deletedBodyIdList.AppendElement(id);
          }
        }

        {  // and then a possible third entry for the security id
          QM_TRY_INSPECT(const bool& isNull,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetIsNull, 2));

          if (!isNull) {
            QM_TRY_INSPECT(const int32_t& securityId,
                           MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 2));

            // XXXtt: Consider using map for aDeletedSecuityIdListOut.
            auto foundIt = std::find_if(
                deletedSecurityIdList.begin(), deletedSecurityIdList.end(),
                [securityId](const auto& deletedSecurityId) {
                  return deletedSecurityId.mId == securityId;
                });

            if (foundIt == deletedSecurityIdList.end()) {
              // Add a new entry for this ID with a count of 1, if it's not in
              // the list
              deletedSecurityIdList.AppendElement(IdCount(securityId));
            } else {
              // Otherwise, increment the count for this ID
              foundIt->mCount += 1;
            }
          }
        }

        {
          // It's possible to have null padding size for non-opaque response
          QM_TRY_INSPECT(const bool& isNull,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetIsNull, 3));

          if (!isNull) {
            QM_TRY_INSPECT(const int64_t& paddingSize,
                           MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 3));

            MOZ_DIAGNOSTIC_ASSERT(paddingSize >= 0);

            // Assert overflow
            MOZ_DIAGNOSTIC_ASSERT(INT64_MAX - deletedPaddingSize >=
                                  paddingSize);

            deletedPaddingSize += paddingSize;
          }
        }

        return Ok{};
      }));

  // Mark the entries as pending deletion. Do not delete them yet, since content
  // may still be reading their body files.
  // See SelectAndDeleteEntriesInternal() for details.
  query =
      "UPDATE entries SET pending_deletion=1, "
      "response_security_info_id=NULL, response_padding_size=0 "
      "WHERE cache_id=:cache_id AND pending_deletion=0;"_ns;

  {
    QM_TRY_INSPECT(const auto& state, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                          nsCOMPtr<mozIStorageStatement>, aConn,
                                          CreateStatement, query));

    QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("cache_id"_ns, aCacheId)));
    QM_TRY(MOZ_TO_RESULT(state->Execute()));
  }

  QM_TRY(MOZ_TO_RESULT(DeleteOrphanedEntriesWithoutBodies(aConn)));

  return result;
}

nsresult DeleteOrphanedEntriesWithoutBodies(mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Dependent records removed via ON DELETE CASCADE
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "DELETE FROM entries WHERE pending_deletion=1 "
      "AND request_body_id IS NULL AND response_body_id IS NULL;"_ns)));

  return NS_OK;
}

Result<int32_t, nsresult> InsertSecurityInfo(
    mozIStorageConnection& aConn, nsICryptoHash& aCrypto,
    nsITransportSecurityInfo* aSecurityInfo) {
  MOZ_DIAGNOSTIC_ASSERT(aSecurityInfo);
  if (!aSecurityInfo) {
    return Err(NS_ERROR_FAILURE);
  }
  nsCString data;
  nsresult rv = aSecurityInfo->ToString(data);
  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  // We want to use an index to find existing security blobs, but indexing
  // the full blob would be quite expensive.  Instead, we index a small
  // hash value.  Calculate this hash as the first 8 bytes of the SHA1 of
  // the full data.
  QM_TRY_INSPECT(const auto& hash, HashCString(aCrypto, data));

  // Next, search for an existing entry for this blob by comparing the hash
  // value first and then the full data.  SQLite is smart enough to use
  // the index on the hash to search the table before doing the expensive
  // comparison of the large data column.  (This was verified with EXPLAIN.)
  QM_TRY_INSPECT(
      const auto& selectStmt,
      quota::CreateAndExecuteSingleStepStatement<
          quota::SingleStepResult::ReturnNullIfNoResult>(
          aConn,
          // Note that hash and data are blobs, but we can use = here since the
          // columns are NOT NULL.
          "SELECT id, refcount FROM security_info WHERE hash=:hash AND "
          "data=:data;"_ns,
          [&hash, &data](auto& state) -> Result<Ok, nsresult> {
            QM_TRY(MOZ_TO_RESULT(
                state.BindUTF8StringAsBlobByName("hash"_ns, hash)));
            QM_TRY(MOZ_TO_RESULT(
                state.BindUTF8StringAsBlobByName("data"_ns, data)));

            return Ok{};
          }));

  // This security info blob is already in the database
  if (selectStmt) {
    // get the existing security blob id to return
    QM_TRY_INSPECT(const int32_t& id,
                   MOZ_TO_RESULT_INVOKE_MEMBER(selectStmt, GetInt32, 0));
    QM_TRY_INSPECT(const int32_t& refcount,
                   MOZ_TO_RESULT_INVOKE_MEMBER(selectStmt, GetInt32, 1));

    // But first, update the refcount in the database.
    QM_TRY_INSPECT(
        const auto& state,
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
            nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
            "UPDATE security_info SET refcount=:refcount WHERE id=:id;"_ns));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("refcount"_ns, refcount + 1)));
    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("id"_ns, id)));
    QM_TRY(MOZ_TO_RESULT(state->Execute()));

    return id;
  }

  // This is a new security info blob.  Create a new row in the security table
  // with an initial refcount of 1.
  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "INSERT INTO security_info (hash, data, refcount) "
                     "VALUES (:hash, :data, 1);"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringAsBlobByName("hash"_ns, hash)));
  QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringAsBlobByName("data"_ns, data)));
  QM_TRY(MOZ_TO_RESULT(state->Execute()));

  {
    QM_TRY_INSPECT(const auto& state,
                   quota::CreateAndExecuteSingleStepStatement(
                       aConn, "SELECT last_insert_rowid()"_ns));

    QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 0));
  }
}

nsresult DeleteSecurityInfo(mozIStorageConnection& aConn, int32_t aId,
                            int32_t aCount) {
  // First, we need to determine the current refcount for this security blob.
  QM_TRY_INSPECT(
      const int32_t& refcount, ([&aConn, aId]() -> Result<int32_t, nsresult> {
        QM_TRY_INSPECT(
            const auto& state,
            quota::CreateAndExecuteSingleStepStatement(
                aConn, "SELECT refcount FROM security_info WHERE id=:id;"_ns,
                [aId](auto& state) -> Result<Ok, nsresult> {
                  QM_TRY(MOZ_TO_RESULT(state.BindInt32ByName("id"_ns, aId)));
                  return Ok{};
                }));

        QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 0));
      }()));

  MOZ_ASSERT_DEBUG_OR_FUZZING(refcount >= aCount);

  // Next, calculate the new refcount
  int32_t newCount = refcount - aCount;

  // If the last reference to this security blob was removed we can
  // just remove the entire row.
  if (newCount == 0) {
    QM_TRY_INSPECT(const auto& state,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "DELETE FROM security_info WHERE id=:id;"_ns));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("id"_ns, aId)));
    QM_TRY(MOZ_TO_RESULT(state->Execute()));

    return NS_OK;
  }

  // Otherwise update the refcount in the table to reflect the reduced
  // number of references to the security blob.
  QM_TRY_INSPECT(
      const auto& state,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
          "UPDATE security_info SET refcount=:refcount WHERE id=:id;"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("refcount"_ns, newCount)));
  QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("id"_ns, aId)));
  QM_TRY(MOZ_TO_RESULT(state->Execute()));

  return NS_OK;
}

nsresult DeleteSecurityInfoList(
    mozIStorageConnection& aConn,
    const nsTArray<IdCount>& aDeletedStorageIdList) {
  for (const auto& deletedStorageId : aDeletedStorageIdList) {
    QM_TRY(MOZ_TO_RESULT(DeleteSecurityInfo(aConn, deletedStorageId.mId,
                                            deletedStorageId.mCount)));
  }

  return NS_OK;
}

nsresult InsertEntry(mozIStorageConnection& aConn, CacheId aCacheId,
                     const CacheRequest& aRequest, const nsID* aRequestBodyId,
                     const CacheResponse& aResponse,
                     const nsID* aResponseBodyId) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(const auto& crypto,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<nsICryptoHash>,
                                         MOZ_SELECT_OVERLOAD(do_CreateInstance),
                                         NS_CRYPTO_HASH_CONTRACTID));

  int32_t securityId = -1;
  if (aResponse.securityInfo()) {
    QM_TRY_UNWRAP(securityId,
                  InsertSecurityInfo(aConn, *crypto, aResponse.securityInfo()));
  }

  {
    QM_TRY_INSPECT(const auto& state,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "INSERT INTO entries ("
                       "request_method, "
                       "request_url_no_query, "
                       "request_url_no_query_hash, "
                       "request_url_query, "
                       "request_url_query_hash, "
                       "request_url_fragment, "
                       "request_referrer, "
                       "request_referrer_policy, "
                       "request_headers_guard, "
                       "request_mode, "
                       "request_credentials, "
                       "request_contentpolicytype, "
                       "request_cache, "
                       "request_redirect, "
                       "request_integrity, "
                       "request_body_id, "
                       "request_body_disk_size, "
                       "response_type, "
                       "response_status, "
                       "response_status_text, "
                       "response_headers_guard, "
                       "response_body_id, "
                       "response_body_disk_size, "
                       "response_security_info_id, "
                       "response_principal_info, "
                       "response_padding_size, "
                       "response_credentials, "
                       "cache_id "
                       ") VALUES ("
                       ":request_method, "
                       ":request_url_no_query, "
                       ":request_url_no_query_hash, "
                       ":request_url_query, "
                       ":request_url_query_hash, "
                       ":request_url_fragment, "
                       ":request_referrer, "
                       ":request_referrer_policy, "
                       ":request_headers_guard, "
                       ":request_mode, "
                       ":request_credentials, "
                       ":request_contentpolicytype, "
                       ":request_cache, "
                       ":request_redirect, "
                       ":request_integrity, "
                       ":request_body_id, "
                       ":request_body_disk_size, "
                       ":response_type, "
                       ":response_status, "
                       ":response_status_text, "
                       ":response_headers_guard, "
                       ":response_body_id, "
                       ":response_body_disk_size, "
                       ":response_security_info_id, "
                       ":response_principal_info, "
                       ":response_padding_size, "
                       ":response_credentials, "
                       ":cache_id "
                       ");"_ns));

    QM_TRY(MOZ_TO_RESULT(
        state->BindUTF8StringByName("request_method"_ns, aRequest.method())));

    QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringByName(
        "request_url_no_query"_ns, aRequest.urlWithoutQuery())));

    QM_TRY_INSPECT(const auto& urlWithoutQueryHash,
                   HashCString(*crypto, aRequest.urlWithoutQuery()));

    QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringAsBlobByName(
        "request_url_no_query_hash"_ns, urlWithoutQueryHash)));

    QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringByName("request_url_query"_ns,
                                                     aRequest.urlQuery())));

    QM_TRY_INSPECT(const auto& urlQueryHash,
                   HashCString(*crypto, aRequest.urlQuery()));

    QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringAsBlobByName(
        "request_url_query_hash"_ns, urlQueryHash)));

    QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringByName("request_url_fragment"_ns,
                                                     aRequest.urlFragment())));

    QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringByName("request_referrer"_ns,
                                                     aRequest.referrer())));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName(
        "request_referrer_policy"_ns,
        static_cast<int32_t>(aRequest.referrerPolicy()))));

    QM_TRY(MOZ_TO_RESULT(
        state->BindInt32ByName("request_headers_guard"_ns,
                               static_cast<int32_t>(aRequest.headersGuard()))));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName(
        "request_mode"_ns, static_cast<int32_t>(aRequest.mode()))));

    QM_TRY(MOZ_TO_RESULT(
        state->BindInt32ByName("request_credentials"_ns,
                               static_cast<int32_t>(aRequest.credentials()))));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName(
        "request_contentpolicytype"_ns,
        static_cast<int32_t>(aRequest.contentPolicyType()))));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName(
        "request_cache"_ns, static_cast<int32_t>(aRequest.requestCache()))));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName(
        "request_redirect"_ns,
        static_cast<int32_t>(aRequest.requestRedirect()))));

    QM_TRY(MOZ_TO_RESULT(
        state->BindStringByName("request_integrity"_ns, aRequest.integrity())));

    QM_TRY(MOZ_TO_RESULT(BindId(*state, "request_body_id"_ns, aRequestBodyId)));

    QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("request_body_disk_size"_ns,
                                                aRequest.bodyDiskSize())));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName(
        "response_type"_ns, static_cast<int32_t>(aResponse.type()))));

    QM_TRY(MOZ_TO_RESULT(
        state->BindInt32ByName("response_status"_ns, aResponse.status())));

    QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringByName("response_status_text"_ns,
                                                     aResponse.statusText())));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName(
        "response_headers_guard"_ns,
        static_cast<int32_t>(aResponse.headersGuard()))));

    QM_TRY(
        MOZ_TO_RESULT(BindId(*state, "response_body_id"_ns, aResponseBodyId)));

    QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("response_body_disk_size"_ns,
                                                aResponse.bodyDiskSize())));

    if (!aResponse.securityInfo()) {
      QM_TRY(
          MOZ_TO_RESULT(state->BindNullByName("response_security_info_id"_ns)));
    } else {
      QM_TRY(MOZ_TO_RESULT(
          state->BindInt32ByName("response_security_info_id"_ns, securityId)));
    }

    nsAutoCString serializedInfo;
    // We only allow content serviceworkers right now.
    if (aResponse.principalInfo().isSome()) {
      const mozilla::ipc::PrincipalInfo& principalInfo =
          aResponse.principalInfo().ref();
      MOZ_DIAGNOSTIC_ASSERT(principalInfo.type() ==
                            mozilla::ipc::PrincipalInfo::TContentPrincipalInfo);
      const mozilla::ipc::ContentPrincipalInfo& cInfo =
          principalInfo.get_ContentPrincipalInfo();

      serializedInfo.Append(cInfo.spec());

      nsAutoCString suffix;
      cInfo.attrs().CreateSuffix(suffix);
      serializedInfo.Append(suffix);
    }

    QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringByName(
        "response_principal_info"_ns, serializedInfo)));

    if (aResponse.paddingSize() == InternalResponse::UNKNOWN_PADDING_SIZE) {
      MOZ_DIAGNOSTIC_ASSERT(aResponse.type() != ResponseType::Opaque);
      QM_TRY(MOZ_TO_RESULT(state->BindNullByName("response_padding_size"_ns)));
    } else {
      MOZ_DIAGNOSTIC_ASSERT(aResponse.paddingSize() >= 0);
      MOZ_DIAGNOSTIC_ASSERT(aResponse.type() == ResponseType::Opaque);

      QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("response_padding_size"_ns,
                                                  aResponse.paddingSize())));
    }

    QM_TRY(MOZ_TO_RESULT(
        state->BindInt32ByName("response_credentials"_ns,
                               static_cast<int32_t>(aResponse.credentials()))));

    QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName("cache_id"_ns, aCacheId)));

    QM_TRY(MOZ_TO_RESULT(state->Execute()));
  }

  QM_TRY_INSPECT(
      const int32_t& entryId, ([&aConn]() -> Result<int32_t, nsresult> {
        QM_TRY_INSPECT(const auto& state,
                       quota::CreateAndExecuteSingleStepStatement(
                           aConn, "SELECT last_insert_rowid()"_ns));

        QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 0));
      }()));

  {
    QM_TRY_INSPECT(const auto& state,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "INSERT INTO request_headers ("
                       "name, "
                       "value, "
                       "entry_id "
                       ") VALUES (:name, :value, :entry_id)"_ns));

    for (const auto& requestHeader : aRequest.headers()) {
      QM_TRY(MOZ_TO_RESULT(
          state->BindUTF8StringByName("name"_ns, requestHeader.name())));

      QM_TRY(MOZ_TO_RESULT(
          state->BindUTF8StringByName("value"_ns, requestHeader.value())));

      QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("entry_id"_ns, entryId)));

      QM_TRY(MOZ_TO_RESULT(state->Execute()));
    }
  }

  {
    QM_TRY_INSPECT(const auto& state,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "INSERT INTO response_headers ("
                       "name, "
                       "value, "
                       "entry_id "
                       ") VALUES (:name, :value, :entry_id)"_ns));

    for (const auto& responseHeader : aResponse.headers()) {
      QM_TRY(MOZ_TO_RESULT(
          state->BindUTF8StringByName("name"_ns, responseHeader.name())));
      QM_TRY(MOZ_TO_RESULT(
          state->BindUTF8StringByName("value"_ns, responseHeader.value())));
      QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("entry_id"_ns, entryId)));
      QM_TRY(MOZ_TO_RESULT(state->Execute()));
    }
  }

  {
    QM_TRY_INSPECT(const auto& state,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "INSERT INTO response_url_list ("
                       "url, "
                       "entry_id "
                       ") VALUES (:url, :entry_id)"_ns));

    for (const auto& responseUrl : aResponse.urlList()) {
      QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringByName(
          "url"_ns, responseUrl->GetSpecOrDefault())));
      QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("entry_id"_ns, entryId)));
      QM_TRY(MOZ_TO_RESULT(state->Execute()));
    }
  }

  return NS_OK;
}

/**
 * Gets a HeadersEntry from a storage statement by retrieving the first column
 * as the name and the second column as the value.
 */
Result<HeadersEntry, nsresult> GetHeadersEntryFromStatement(
    mozIStorageStatement& aStmt) {
  HeadersEntry header;

  QM_TRY_UNWRAP(header.name(), MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                   nsCString, aStmt, GetUTF8String, 0));
  QM_TRY_UNWRAP(header.value(), MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                    nsCString, aStmt, GetUTF8String, 1));

  return header;
}

Result<SavedResponse, nsresult> ReadResponse(mozIStorageConnection& aConn,
                                             EntryId aEntryId) {
  MOZ_ASSERT(!NS_IsMainThread());

  SavedResponse savedResponse;

  QM_TRY_INSPECT(
      const auto& state,
      quota::CreateAndExecuteSingleStepStatement(
          aConn,
          "SELECT "
          "entries.response_type, "
          "entries.response_status, "
          "entries.response_status_text, "
          "entries.response_headers_guard, "
          "entries.response_body_id, "
          "entries.response_principal_info, "
          "entries.response_padding_size, "
          "security_info.data, "
          "entries.response_credentials "
          "FROM entries "
          "LEFT OUTER JOIN security_info "
          "ON entries.response_security_info_id=security_info.id "
          "WHERE entries.id=:id;"_ns,
          [aEntryId](auto& state) -> Result<Ok, nsresult> {
            QM_TRY(MOZ_TO_RESULT(state.BindInt32ByName("id"_ns, aEntryId)));

            return Ok{};
          }));

  QM_TRY_INSPECT(const int32_t& type,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 0));
  savedResponse.mValue.type() = static_cast<ResponseType>(type);

  QM_TRY_INSPECT(const int32_t& status,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 1));
  savedResponse.mValue.status() = static_cast<uint32_t>(status);

  QM_TRY(MOZ_TO_RESULT(
      state->GetUTF8String(2, savedResponse.mValue.statusText())));

  QM_TRY_INSPECT(const int32_t& guard,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 3));
  savedResponse.mValue.headersGuard() = static_cast<HeadersGuardEnum>(guard);

  QM_TRY_INSPECT(const bool& nullBody,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetIsNull, 4));
  savedResponse.mHasBodyId = !nullBody;

  if (savedResponse.mHasBodyId) {
    QM_TRY_UNWRAP(savedResponse.mBodyId, ExtractId(*state, 4));
  }

  QM_TRY_INSPECT(const auto& serializedInfo,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, *state,
                                                   GetUTF8String, 5));

  savedResponse.mValue.principalInfo() = Nothing();
  if (!serializedInfo.IsEmpty()) {
    nsAutoCString specNoSuffix;
    OriginAttributes attrs;
    if (!attrs.PopulateFromOrigin(serializedInfo, specNoSuffix)) {
      NS_WARNING("Something went wrong parsing a serialized principal!");
      return Err(NS_ERROR_FAILURE);
    }

    nsCOMPtr<nsIURI> url;
    QM_TRY(MOZ_TO_RESULT(NS_NewURI(getter_AddRefs(url), specNoSuffix)));

#ifdef DEBUG
    nsAutoCString scheme;
    QM_TRY(MOZ_TO_RESULT(url->GetScheme(scheme)));

    MOZ_ASSERT(
        scheme == "http" || scheme == "https" || scheme == "file" ||
        // A cached response entry may have a moz-extension principal if:
        //
        // - This is an extension background service worker. The response for
        //   the main script is expected tobe a moz-extension content principal
        //   (the pref "extensions.backgroundServiceWorker.enabled" must be
        //   enabled, if the pref is toggled to false at runtime then any
        //   service worker registered for a moz-extension principal will be
        //   unregistered on the next startup).
        //
        // - An extension is redirecting a script being imported info a worker
        //   created from a regular webpage to a web-accessible extension
        //   script. The reponse for these redirects will have a moz-extension
        //   principal. Although extensions can attempt to redirect the main
        //   script of service workers, this will always cause the install
        //   process to fail.
        scheme == "moz-extension");
#endif

    nsCOMPtr<nsIPrincipal> principal =
        BasePrincipal::CreateContentPrincipal(url, attrs);
    if (!principal) {
      return Err(NS_ERROR_NULL_POINTER);
    }

    nsCString origin;
    QM_TRY(MOZ_TO_RESULT(principal->GetOriginNoSuffix(origin)));

    nsCString baseDomain;
    QM_TRY(MOZ_TO_RESULT(principal->GetBaseDomain(baseDomain)));

    savedResponse.mValue.principalInfo() =
        Some(mozilla::ipc::ContentPrincipalInfo(attrs, origin, specNoSuffix,
                                                Nothing(), baseDomain));
  }

  QM_TRY_INSPECT(const bool& nullPadding,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetIsNull, 6));

  if (nullPadding) {
    MOZ_DIAGNOSTIC_ASSERT(savedResponse.mValue.type() != ResponseType::Opaque);
    savedResponse.mValue.paddingSize() = InternalResponse::UNKNOWN_PADDING_SIZE;
  } else {
    MOZ_DIAGNOSTIC_ASSERT(savedResponse.mValue.type() == ResponseType::Opaque);
    QM_TRY_INSPECT(const int64_t& paddingSize,
                   MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt64, 6));

    MOZ_DIAGNOSTIC_ASSERT(paddingSize >= 0);
    savedResponse.mValue.paddingSize() = paddingSize;
  }

  nsCString data;
  QM_TRY(MOZ_TO_RESULT(state->GetBlobAsUTF8String(7, data)));
  if (!data.IsEmpty()) {
    nsCOMPtr<nsITransportSecurityInfo> securityInfo;
    nsresult rv = mozilla::psm::TransportSecurityInfo::Read(
        data, getter_AddRefs(securityInfo));
    if (NS_FAILED(rv)) {
      return Err(rv);
    }
    if (!securityInfo) {
      return Err(NS_ERROR_FAILURE);
    }
    savedResponse.mValue.securityInfo() = securityInfo.forget();
  }

  QM_TRY_INSPECT(const int32_t& credentials,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 8));
  savedResponse.mValue.credentials() =
      static_cast<RequestCredentials>(credentials);

  {
    QM_TRY_INSPECT(const auto& state,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "SELECT "
                       "name, "
                       "value "
                       "FROM response_headers "
                       "WHERE entry_id=:entry_id;"_ns));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("entry_id"_ns, aEntryId)));

    QM_TRY_UNWRAP(savedResponse.mValue.headers(),
                  quota::CollectElementsWhileHasResult(
                      *state, GetHeadersEntryFromStatement));
  }

  {
    // NOTE: We explicitly filter out empty URLs from the response_url_list
    // table here. While recent versions should never add these to the table,
    // the migration to version 21 is a bit naive, and would directly copy
    // response_url into the table, even if it was "". We want to treat this
    // case as an empty response_url_list.
    QM_TRY_INSPECT(const auto& state,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "SELECT "
                       "url "
                       "FROM response_url_list "
                       "WHERE entry_id=:entry_id AND length(url) > 0;"_ns));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("entry_id"_ns, aEntryId)));

    QM_TRY_UNWRAP(
        savedResponse.mValue.urlList(),
        quota::CollectElementsWhileHasResult(
            *state,
            [](auto& stmt) -> Result<NotNull<RefPtr<nsIURI>>, nsresult> {
              QM_TRY_INSPECT(const auto& spec,
                             MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                 nsCString, stmt, GetUTF8String, 0));

              RefPtr<nsIURI> url;
              QM_TRY(MOZ_TO_RESULT(NS_NewURI(getter_AddRefs(url), spec)));
              return WrapNotNull(url);
            }));
  }

  return savedResponse;
}

Result<SavedRequest, nsresult> ReadRequest(mozIStorageConnection& aConn,
                                           EntryId aEntryId) {
  MOZ_ASSERT(!NS_IsMainThread());

  SavedRequest savedRequest;

  QM_TRY_INSPECT(
      const auto& state,
      quota::CreateAndExecuteSingleStepStatement<
          quota::SingleStepResult::ReturnNullIfNoResult>(
          aConn,
          "SELECT "
          "request_method, "
          "request_url_no_query, "
          "request_url_query, "
          "request_url_fragment, "
          "request_referrer, "
          "request_referrer_policy, "
          "request_headers_guard, "
          "request_mode, "
          "request_credentials, "
          "request_contentpolicytype, "
          "request_cache, "
          "request_redirect, "
          "request_integrity, "
          "request_body_id "
          "FROM entries "
          "WHERE id=:id;"_ns,
          [aEntryId](auto& state) -> Result<Ok, nsresult> {
            QM_TRY(MOZ_TO_RESULT(state.BindInt32ByName("id"_ns, aEntryId)));

            return Ok{};
          }));

  QM_TRY(OkIf(state), Err(NS_ERROR_UNEXPECTED));

  QM_TRY(MOZ_TO_RESULT(state->GetUTF8String(0, savedRequest.mValue.method())));
  QM_TRY(MOZ_TO_RESULT(
      state->GetUTF8String(1, savedRequest.mValue.urlWithoutQuery())));
  QM_TRY(
      MOZ_TO_RESULT(state->GetUTF8String(2, savedRequest.mValue.urlQuery())));
  QM_TRY(MOZ_TO_RESULT(
      state->GetUTF8String(3, savedRequest.mValue.urlFragment())));
  QM_TRY(
      MOZ_TO_RESULT(state->GetUTF8String(4, savedRequest.mValue.referrer())));

  QM_TRY_INSPECT(const int32_t& referrerPolicy,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 5));
  savedRequest.mValue.referrerPolicy() =
      static_cast<ReferrerPolicy>(referrerPolicy);

  QM_TRY_INSPECT(const int32_t& guard,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 6));
  savedRequest.mValue.headersGuard() = static_cast<HeadersGuardEnum>(guard);

  QM_TRY_INSPECT(const int32_t& mode,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 7));
  savedRequest.mValue.mode() = static_cast<RequestMode>(mode);

  QM_TRY_INSPECT(const int32_t& credentials,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 8));
  savedRequest.mValue.credentials() =
      static_cast<RequestCredentials>(credentials);

  QM_TRY_INSPECT(const int32_t& requestContentPolicyType,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 9));
  savedRequest.mValue.contentPolicyType() =
      static_cast<nsContentPolicyType>(requestContentPolicyType);

  QM_TRY_INSPECT(const int32_t& requestCache,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 10));
  savedRequest.mValue.requestCache() = static_cast<RequestCache>(requestCache);

  QM_TRY_INSPECT(const int32_t& requestRedirect,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetInt32, 11));
  savedRequest.mValue.requestRedirect() =
      static_cast<RequestRedirect>(requestRedirect);

  QM_TRY(MOZ_TO_RESULT(state->GetString(12, savedRequest.mValue.integrity())));

  QM_TRY_INSPECT(const bool& nullBody,
                 MOZ_TO_RESULT_INVOKE_MEMBER(state, GetIsNull, 13));
  savedRequest.mHasBodyId = !nullBody;
  if (savedRequest.mHasBodyId) {
    QM_TRY_UNWRAP(savedRequest.mBodyId, ExtractId(*state, 13));
  }

  {
    QM_TRY_INSPECT(const auto& state,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                       nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                       "SELECT "
                       "name, "
                       "value "
                       "FROM request_headers "
                       "WHERE entry_id=:entry_id;"_ns));

    QM_TRY(MOZ_TO_RESULT(state->BindInt32ByName("entry_id"_ns, aEntryId)));

    QM_TRY_UNWRAP(savedRequest.mValue.headers(),
                  quota::CollectElementsWhileHasResult(
                      *state, GetHeadersEntryFromStatement));
  }

  return savedRequest;
}

void AppendListParamsToQuery(nsACString& aQuery, size_t aLen) {
  MOZ_ASSERT(!NS_IsMainThread());

  aQuery.SetCapacity(aQuery.Length() + (aLen - 1) * 2 + 2);
  aQuery.AppendLiteral("?");
  for (size_t i = 1; i < aLen; ++i) {
    aQuery.AppendLiteral(",?");
  }
}

nsresult BindListParamsToQuery(mozIStorageStatement& aState,
                               const Span<const EntryId>& aEntryIdList) {
  MOZ_ASSERT(!NS_IsMainThread());
  for (size_t i = 0, n = aEntryIdList.Length(); i < n; ++i) {
    QM_TRY(MOZ_TO_RESULT(aState.BindInt32ByIndex(i, aEntryIdList[i])));
  }
  return NS_OK;
}

nsresult BindId(mozIStorageStatement& aState, const nsACString& aName,
                const nsID* aId) {
  MOZ_ASSERT(!NS_IsMainThread());

  if (!aId) {
    QM_TRY(MOZ_TO_RESULT(aState.BindNullByName(aName)));
    return NS_OK;
  }

  char idBuf[NSID_LENGTH];
  aId->ToProvidedString(idBuf);
  QM_TRY(MOZ_TO_RESULT(
      aState.BindUTF8StringByName(aName, nsDependentCString(idBuf))));

  return NS_OK;
}

Result<nsID, nsresult> ExtractId(mozIStorageStatement& aState, uint32_t aPos) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_INSPECT(const auto& idString,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, aState,
                                                   GetUTF8String, aPos));

  nsID id;
  QM_TRY(OkIf(id.Parse(idString)), Err(NS_ERROR_UNEXPECTED));

  return id;
}

Result<NotNull<nsCOMPtr<mozIStorageStatement>>, nsresult>
CreateAndBindKeyStatement(mozIStorageConnection& aConn,
                          const char* const aQueryFormat,
                          const nsAString& aKey) {
  MOZ_DIAGNOSTIC_ASSERT(aQueryFormat);

  // The key is stored as a blob to avoid encoding issues.  An empty string
  // is mapped to NULL for blobs.  Normally we would just write the query
  // as "key IS :key" to do the proper NULL checking, but that prevents
  // sqlite from using the key index.  Therefore use "IS NULL" explicitly
  // if the key is empty, otherwise use "=:key" so that sqlite uses the
  // index.

  QM_TRY_UNWRAP(
      auto state,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
          nsPrintfCString(aQueryFormat,
                          aKey.IsEmpty() ? "key IS NULL" : "key=:key")));

  if (!aKey.IsEmpty()) {
    QM_TRY(MOZ_TO_RESULT(state->BindStringAsBlobByName("key"_ns, aKey)));
  }

  return WrapNotNull(std::move(state));
}

Result<nsAutoCString, nsresult> HashCString(nsICryptoHash& aCrypto,
                                            const nsACString& aIn) {
  QM_TRY(MOZ_TO_RESULT(aCrypto.Init(nsICryptoHash::SHA1)));

  QM_TRY(MOZ_TO_RESULT(aCrypto.Update(
      reinterpret_cast<const uint8_t*>(aIn.BeginReading()), aIn.Length())));

  nsAutoCString fullHash;
  QM_TRY(MOZ_TO_RESULT(aCrypto.Finish(false /* based64 result */, fullHash)));

  return Result<nsAutoCString, nsresult>{std::in_place,
                                         Substring(fullHash, 0, 8)};
}

}  // namespace

nsresult IncrementalVacuum(mozIStorageConnection& aConn) {
  // Determine how much free space is in the database.
  QM_TRY_INSPECT(const auto& state, quota::CreateAndExecuteSingleStepStatement(
                                        aConn, "PRAGMA freelist_count;"_ns));

  QM_TRY_INSPECT(const int32_t& freePages,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 0));

  // We have a relatively small page size, so we want to be careful to avoid
  // fragmentation.  We already use a growth incremental which will cause
  // sqlite to allocate and release multiple pages at the same time.  We can
  // further reduce fragmentation by making our allocated chunks a bit
  // "sticky".  This is done by creating some hysteresis where we allocate
  // pages/chunks as soon as we need them, but we only release pages/chunks
  // when we have a large amount of free space.  This helps with the case
  // where a page is adding and remove resources causing it to dip back and
  // forth across a chunk boundary.
  //
  // So only proceed with releasing pages if we have more than our constant
  // threshold.
  if (freePages <= kMaxFreePages) {
    return NS_OK;
  }

  // Release the excess pages back to the sqlite VFS.  This may also release
  // chunks of multiple pages back to the OS.
  const int32_t pagesToRelease = freePages - kMaxFreePages;

  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      nsPrintfCString("PRAGMA incremental_vacuum(%d);", pagesToRelease))));

  // Verify that our incremental vacuum actually did something
#ifdef DEBUG
  {
    QM_TRY_INSPECT(const auto& state,
                   quota::CreateAndExecuteSingleStepStatement(
                       aConn, "PRAGMA freelist_count;"_ns));

    QM_TRY_INSPECT(const int32_t& freePages,
                   MOZ_TO_RESULT_INVOKE_MEMBER(*state, GetInt32, 0));

    MOZ_ASSERT(freePages <= kMaxFreePages);
  }
#endif

  return NS_OK;
}

namespace {

// Wrapper around mozIStorageConnection::GetSchemaVersion() that compensates
// for hacky downgrade schema version tricks.  See the block comments for
// kHackyDowngradeSchemaVersion and kHackyPaddingSizePresentVersion.
Result<int32_t, nsresult> GetEffectiveSchemaVersion(
    mozIStorageConnection& aConn) {
  QM_TRY_INSPECT(const int32_t& schemaVersion,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aConn, GetSchemaVersion));

  if (schemaVersion == kHackyDowngradeSchemaVersion) {
    // This is the special case.  Check for the existence of the
    // "response_padding_size" colum in table "entries".
    //
    // (pragma_table_info is a table-valued function format variant of
    // "PRAGMA table_info" supported since SQLite 3.16.0.  Firefox 53 shipped
    // was the first release with this functionality, shipping 3.16.2.)
    //
    // If there are any result rows, then the column is present.
    QM_TRY_INSPECT(const bool& hasColumn,
                   quota::CreateAndExecuteSingleStepStatement<
                       quota::SingleStepResult::ReturnNullIfNoResult>(
                       aConn,
                       "SELECT name FROM pragma_table_info('entries') WHERE "
                       "name = 'response_padding_size'"_ns));

    if (hasColumn) {
      return kHackyPaddingSizePresentVersion;
    }
  }

  return schemaVersion;
}

#ifdef DEBUG
struct Expect {
  // Expect exact SQL
  Expect(const char* aName, const char* aType, const char* aSql)
      : mName(aName), mType(aType), mSql(aSql), mIgnoreSql(false) {}

  // Ignore SQL
  Expect(const char* aName, const char* aType)
      : mName(aName), mType(aType), mIgnoreSql(true) {}

  const nsCString mName;
  const nsCString mType;
  const nsCString mSql;
  const bool mIgnoreSql;
};
#endif

nsresult Validate(mozIStorageConnection& aConn) {
  QM_TRY_INSPECT(const int32_t& schemaVersion,
                 GetEffectiveSchemaVersion(aConn));
  QM_TRY(OkIf(schemaVersion == kLatestSchemaVersion),
         schemaVersion > kLatestSchemaVersion ? NS_ERROR_DOM_NOT_SUPPORTED_ERR
                                              : NS_ERROR_FILE_CORRUPTED);

#ifdef DEBUG
  // This is the schema we expect the database at the latest version to
  // contain.  Update this list if you add a new table or index.
  const Expect expects[] = {
      Expect("caches", "table", kTableCaches),
      Expect("sqlite_sequence", "table"),  // auto-gen by sqlite
      Expect("security_info", "table", kTableSecurityInfo),
      Expect("security_info_hash_index", "index", kIndexSecurityInfoHash),
      Expect("entries", "table", kTableEntries),
      Expect("entries_request_match_index", "index", kIndexEntriesRequest),
      Expect("entries_pending_deletion_index", "index",
             kIndexEntriesPendingDeletion),
      Expect("request_headers", "table", kTableRequestHeaders),
      Expect("response_headers", "table", kTableResponseHeaders),
      Expect("response_headers_entry_id_index", "index",
             kIndexResponseHeadersEntryId),
      Expect("response_url_list", "table", kTableResponseUrlList),
      Expect("storage", "table", kTableStorage),
      Expect("sqlite_autoindex_storage_1", "index"),  // auto-gen by sqlite
      Expect("usage_info", "table", kTableUsageInfo),
      Expect("entries_insert_trigger", "trigger", kTriggerEntriesInsert),
      Expect("entries_update_trigger", "trigger", kTriggerEntriesUpdate),
      Expect("entries_delete_trigger", "trigger", kTriggerEntriesDelete),
  };

  // Read the schema from the sqlite_master table and compare.
  QM_TRY_INSPECT(const auto& state,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
                     "SELECT name, type, sql FROM sqlite_master;"_ns));

  QM_TRY(quota::CollectWhileHasResult(
      *state, [&expects](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const auto& name,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, stmt,
                                                         GetUTF8String, 0));
        QM_TRY_INSPECT(const auto& type,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, stmt,
                                                         GetUTF8String, 1));
        QM_TRY_INSPECT(const auto& sql,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, stmt,
                                                         GetUTF8String, 2));

        bool foundMatch = false;
        for (const auto& expect : expects) {
          if (name == expect.mName) {
            if (type != expect.mType) {
              NS_WARNING(
                  nsPrintfCString("Unexpected type for Cache schema entry %s",
                                  name.get())
                      .get());
              return Err(NS_ERROR_FAILURE);
            }

            if (!expect.mIgnoreSql && sql != expect.mSql) {
              NS_WARNING(
                  nsPrintfCString("Unexpected SQL for Cache schema entry %s",
                                  name.get())
                      .get());
              return Err(NS_ERROR_FAILURE);
            }

            foundMatch = true;
            break;
          }
        }

        if (NS_WARN_IF(!foundMatch)) {
          NS_WARNING(
              nsPrintfCString("Unexpected schema entry %s in Cache database",
                              name.get())
                  .get());
          return Err(NS_ERROR_FAILURE);
        }

        return Ok{};
      }));
#endif

  return NS_OK;
}

// -----
// Schema migration code
// -----

using MigrationFunc = nsresult (*)(nsIFile&, mozIStorageConnection&, bool&);
struct Migration {
  int32_t mFromVersion;
  MigrationFunc mFunc;
};

// Declare migration functions here.  Each function should upgrade
// the version by a single increment.  Don't skip versions.
nsresult MigrateFrom15To16(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom16To17(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom17To18(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom18To19(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom19To20(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom20To21(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom21To22(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom22To23(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom23To24(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom24To25(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom25To26(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom26To27(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom27To28(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom28To29(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom29To30(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom30To31(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom31To32(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
nsresult MigrateFrom32To33(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema);
// Configure migration functions to run for the given starting version.
constexpr Migration sMigrationList[] = {
    Migration{15, MigrateFrom15To16}, Migration{16, MigrateFrom16To17},
    Migration{17, MigrateFrom17To18}, Migration{18, MigrateFrom18To19},
    Migration{19, MigrateFrom19To20}, Migration{20, MigrateFrom20To21},
    Migration{21, MigrateFrom21To22}, Migration{22, MigrateFrom22To23},
    Migration{23, MigrateFrom23To24}, Migration{24, MigrateFrom24To25},
    Migration{25, MigrateFrom25To26}, Migration{26, MigrateFrom26To27},
    Migration{27, MigrateFrom27To28}, Migration{28, MigrateFrom28To29},
    Migration{29, MigrateFrom29To30}, Migration{30, MigrateFrom30To31},
    Migration{31, MigrateFrom31To32}, Migration{32, MigrateFrom32To33}};

constexpr bool IsMigrationListValid() {
  int32_t expected = kFirstShippedSchemaVersion;
  for (const auto& migration : sMigrationList) {
    if (migration.mFromVersion != expected || !migration.mFunc) {
      return false;
    }
    ++expected;
  }
  return expected == kLatestSchemaVersion;
}

static_assert(IsMigrationListValid(),
              "sMigrationList must be contiguous, ordered, and go from "
              "kFirstShippedSchemaVersion to kLatestSchemaVersion");

nsresult RewriteEntriesSchema(mozIStorageConnection& aConn) {
  QM_TRY(
      MOZ_TO_RESULT(aConn.ExecuteSimpleSQL("PRAGMA writable_schema = ON"_ns)));

  QM_TRY_INSPECT(
      const auto& state,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
          "UPDATE sqlite_master SET sql=:sql WHERE name='entries'"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindUTF8StringByName(
      "sql"_ns, nsDependentCString(kTableEntries))));
  QM_TRY(MOZ_TO_RESULT(state->Execute()));

  QM_TRY(
      MOZ_TO_RESULT(aConn.ExecuteSimpleSQL("PRAGMA writable_schema = OFF"_ns)));

  return NS_OK;
}

nsresult Migrate(nsIFile& aDBDir, mozIStorageConnection& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY_UNWRAP(int32_t currentVersion, GetEffectiveSchemaVersion(aConn));

  bool rewriteSchema = false;

  while (currentVersion < kLatestSchemaVersion) {
    // Wiping old databases is handled in DBAction because it requires
    // making a whole new mozIStorageConnection.  Make sure we don't
    // accidentally get here for one of those old databases.
    MOZ_DIAGNOSTIC_ASSERT(currentVersion >= kFirstShippedSchemaVersion);

    for (const auto& migration : sMigrationList) {
      if (migration.mFromVersion == currentVersion) {
        bool shouldRewrite = false;
        QM_TRY(MOZ_TO_RESULT(migration.mFunc(aDBDir, aConn, shouldRewrite)));
        if (shouldRewrite) {
          rewriteSchema = true;
        }
        break;
      }
    }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    int32_t lastVersion = currentVersion;
#endif
    QM_TRY_UNWRAP(currentVersion, GetEffectiveSchemaVersion(aConn));

    MOZ_DIAGNOSTIC_ASSERT(currentVersion > lastVersion);
  }

  // Don't release assert this since people do sometimes share profiles
  // across schema versions.  Our check in Validate() will catch it.
  MOZ_ASSERT(currentVersion >= kLatestSchemaVersion);

  nsresult rv = NS_OK;
  if (rewriteSchema) {
    // Now overwrite the master SQL for the entries table to remove the column
    // default value.  This is also necessary for our Validate() method to
    // pass on this database.
    rv = RewriteEntriesSchema(aConn);
  }

  return rv;
}

nsresult MigrateFrom15To16(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Add the request_redirect column with a default value of "follow".  Note,
  // we only use a default value here because its required by ALTER TABLE and
  // we need to apply the default "follow" to existing records in the table.
  // We don't actually want to keep the default in the schema for future
  // INSERTs.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN request_redirect INTEGER NOT NULL DEFAULT 0"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(16)));

  aRewriteSchema = true;

  return NS_OK;
}

nsresult MigrateFrom16To17(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // This migration path removes the response_redirected and
  // response_redirected_url columns from the entries table.  sqlite doesn't
  // support removing a column from a table using ALTER TABLE, so we need to
  // create a new table without those columns, fill it up with the existing
  // data, and then drop the original table and rename the new one to the old
  // one.

  // Create a new_entries table with the new fields as of version 17.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "CREATE TABLE new_entries ("
      "id INTEGER NOT NULL PRIMARY KEY, "
      "request_method TEXT NOT NULL, "
      "request_url_no_query TEXT NOT NULL, "
      "request_url_no_query_hash BLOB NOT NULL, "
      "request_url_query TEXT NOT NULL, "
      "request_url_query_hash BLOB NOT NULL, "
      "request_referrer TEXT NOT NULL, "
      "request_headers_guard INTEGER NOT NULL, "
      "request_mode INTEGER NOT NULL, "
      "request_credentials INTEGER NOT NULL, "
      "request_contentpolicytype INTEGER NOT NULL, "
      "request_cache INTEGER NOT NULL, "
      "request_body_id TEXT NULL, "
      "response_type INTEGER NOT NULL, "
      "response_url TEXT NOT NULL, "
      "response_status INTEGER NOT NULL, "
      "response_status_text TEXT NOT NULL, "
      "response_headers_guard INTEGER NOT NULL, "
      "response_body_id TEXT NULL, "
      "response_security_info_id INTEGER NULL REFERENCES security_info(id), "
      "response_principal_info TEXT NOT NULL, "
      "cache_id INTEGER NOT NULL REFERENCES caches(id) ON DELETE CASCADE, "
      "request_redirect INTEGER NOT NULL"
      ")"_ns)));

  // Copy all of the data to the newly created table.
  QM_TRY(
      MOZ_TO_RESULT(aConn.ExecuteSimpleSQL("INSERT INTO new_entries ("
                                           "id, "
                                           "request_method, "
                                           "request_url_no_query, "
                                           "request_url_no_query_hash, "
                                           "request_url_query, "
                                           "request_url_query_hash, "
                                           "request_referrer, "
                                           "request_headers_guard, "
                                           "request_mode, "
                                           "request_credentials, "
                                           "request_contentpolicytype, "
                                           "request_cache, "
                                           "request_redirect, "
                                           "request_body_id, "
                                           "response_type, "
                                           "response_url, "
                                           "response_status, "
                                           "response_status_text, "
                                           "response_headers_guard, "
                                           "response_body_id, "
                                           "response_security_info_id, "
                                           "response_principal_info, "
                                           "cache_id "
                                           ") SELECT "
                                           "id, "
                                           "request_method, "
                                           "request_url_no_query, "
                                           "request_url_no_query_hash, "
                                           "request_url_query, "
                                           "request_url_query_hash, "
                                           "request_referrer, "
                                           "request_headers_guard, "
                                           "request_mode, "
                                           "request_credentials, "
                                           "request_contentpolicytype, "
                                           "request_cache, "
                                           "request_redirect, "
                                           "request_body_id, "
                                           "response_type, "
                                           "response_url, "
                                           "response_status, "
                                           "response_status_text, "
                                           "response_headers_guard, "
                                           "response_body_id, "
                                           "response_security_info_id, "
                                           "response_principal_info, "
                                           "cache_id "
                                           "FROM entries;"_ns)));

  // Remove the old table.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL("DROP TABLE entries;"_ns)));

  // Rename new_entries to entries.
  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL("ALTER TABLE new_entries RENAME to entries;"_ns)));

  // Now, recreate our indices.
  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL(nsDependentCString(kIndexEntriesRequest))));

  // Revalidate the foreign key constraints, and ensure that there are no
  // violations.
  QM_TRY_INSPECT(const bool& hasResult,
                 quota::CreateAndExecuteSingleStepStatement<
                     quota::SingleStepResult::ReturnNullIfNoResult>(
                     aConn, "PRAGMA foreign_key_check;"_ns));

  QM_TRY(OkIf(!hasResult), NS_ERROR_FAILURE);

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(17)));

  return NS_OK;
}

nsresult MigrateFrom17To18(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // This migration is needed in order to remove "only-if-cached" RequestCache
  // values from the database.  This enum value was removed from the spec in
  // https://github.com/whatwg/fetch/issues/39 but we unfortunately happily
  // accepted this value in the Request constructor.
  //
  // There is no good value to upgrade this to, so we just stick to "default".

  static_assert(int(RequestCache::Default) == 0,
                "This is where the 0 below comes from!");
  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL("UPDATE entries SET request_cache = 0 "
                             "WHERE request_cache = 5;"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(18)));

  return NS_OK;
}

nsresult MigrateFrom18To19(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // This migration is needed in order to update the RequestMode values for
  // Request objects corresponding to a navigation content policy type to
  // "navigate".

  static_assert(int(nsIContentPolicy::TYPE_DOCUMENT) == 6 &&
                    int(nsIContentPolicy::TYPE_SUBDOCUMENT) == 7 &&
                    int(nsIContentPolicy::TYPE_INTERNAL_FRAME) == 28 &&
                    int(nsIContentPolicy::TYPE_INTERNAL_IFRAME) == 29 &&
                    int(RequestMode::Navigate) == 3,
                "This is where the numbers below come from!");
  // 8 is former TYPE_REFRESH.

  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "UPDATE entries SET request_mode = 3 "
      "WHERE request_contentpolicytype IN (6, 7, 28, 29, 8);"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(19)));

  return NS_OK;
}

nsresult MigrateFrom19To20(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Add the request_referrer_policy column with a default value of
  // "no-referrer-when-downgrade".  Note, we only use a default value here
  // because its required by ALTER TABLE and we need to apply the default
  // "no-referrer-when-downgrade" to existing records in the table. We don't
  // actually want to keep the default in the schema for future INSERTs.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN request_referrer_policy INTEGER NOT NULL DEFAULT 2"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(20)));

  aRewriteSchema = true;

  return NS_OK;
}

nsresult MigrateFrom20To21(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // This migration creates response_url_list table to store response_url and
  // removes the response_url column from the entries table.
  // sqlite doesn't support removing a column from a table using ALTER TABLE,
  // so we need to create a new table without those columns, fill it up with the
  // existing data, and then drop the original table and rename the new one to
  // the old one.

  // Create a new_entries table with the new fields as of version 21.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "CREATE TABLE new_entries ("
      "id INTEGER NOT NULL PRIMARY KEY, "
      "request_method TEXT NOT NULL, "
      "request_url_no_query TEXT NOT NULL, "
      "request_url_no_query_hash BLOB NOT NULL, "
      "request_url_query TEXT NOT NULL, "
      "request_url_query_hash BLOB NOT NULL, "
      "request_referrer TEXT NOT NULL, "
      "request_headers_guard INTEGER NOT NULL, "
      "request_mode INTEGER NOT NULL, "
      "request_credentials INTEGER NOT NULL, "
      "request_contentpolicytype INTEGER NOT NULL, "
      "request_cache INTEGER NOT NULL, "
      "request_body_id TEXT NULL, "
      "response_type INTEGER NOT NULL, "
      "response_status INTEGER NOT NULL, "
      "response_status_text TEXT NOT NULL, "
      "response_headers_guard INTEGER NOT NULL, "
      "response_body_id TEXT NULL, "
      "response_security_info_id INTEGER NULL REFERENCES security_info(id), "
      "response_principal_info TEXT NOT NULL, "
      "cache_id INTEGER NOT NULL REFERENCES caches(id) ON DELETE CASCADE, "
      "request_redirect INTEGER NOT NULL, "
      "request_referrer_policy INTEGER NOT NULL"
      ")"_ns)));

  // Create a response_url_list table with the new fields as of version 21.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "CREATE TABLE response_url_list ("
      "url TEXT NOT NULL, "
      "entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE"
      ")"_ns)));

  // Copy all of the data to the newly created entries table.
  QM_TRY(
      MOZ_TO_RESULT(aConn.ExecuteSimpleSQL("INSERT INTO new_entries ("
                                           "id, "
                                           "request_method, "
                                           "request_url_no_query, "
                                           "request_url_no_query_hash, "
                                           "request_url_query, "
                                           "request_url_query_hash, "
                                           "request_referrer, "
                                           "request_headers_guard, "
                                           "request_mode, "
                                           "request_credentials, "
                                           "request_contentpolicytype, "
                                           "request_cache, "
                                           "request_redirect, "
                                           "request_referrer_policy, "
                                           "request_body_id, "
                                           "response_type, "
                                           "response_status, "
                                           "response_status_text, "
                                           "response_headers_guard, "
                                           "response_body_id, "
                                           "response_security_info_id, "
                                           "response_principal_info, "
                                           "cache_id "
                                           ") SELECT "
                                           "id, "
                                           "request_method, "
                                           "request_url_no_query, "
                                           "request_url_no_query_hash, "
                                           "request_url_query, "
                                           "request_url_query_hash, "
                                           "request_referrer, "
                                           "request_headers_guard, "
                                           "request_mode, "
                                           "request_credentials, "
                                           "request_contentpolicytype, "
                                           "request_cache, "
                                           "request_redirect, "
                                           "request_referrer_policy, "
                                           "request_body_id, "
                                           "response_type, "
                                           "response_status, "
                                           "response_status_text, "
                                           "response_headers_guard, "
                                           "response_body_id, "
                                           "response_security_info_id, "
                                           "response_principal_info, "
                                           "cache_id "
                                           "FROM entries;"_ns)));

  // Copy reponse_url to the newly created response_url_list table.
  QM_TRY(
      MOZ_TO_RESULT(aConn.ExecuteSimpleSQL("INSERT INTO response_url_list ("
                                           "url, "
                                           "entry_id "
                                           ") SELECT "
                                           "response_url, "
                                           "id "
                                           "FROM entries;"_ns)));

  // Remove the old table.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL("DROP TABLE entries;"_ns)));

  // Rename new_entries to entries.
  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL("ALTER TABLE new_entries RENAME to entries;"_ns)));

  // Now, recreate our indices.
  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL(nsLiteralCString(kIndexEntriesRequest))));

  // Revalidate the foreign key constraints, and ensure that there are no
  // violations.
  QM_TRY_INSPECT(const bool& hasResult,
                 quota::CreateAndExecuteSingleStepStatement<
                     quota::SingleStepResult::ReturnNullIfNoResult>(
                     aConn, "PRAGMA foreign_key_check;"_ns));

  QM_TRY(OkIf(!hasResult), NS_ERROR_FAILURE);

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(21)));

  aRewriteSchema = true;

  return NS_OK;
}

nsresult MigrateFrom21To22(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Add the request_integrity column.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN request_integrity TEXT NOT NULL DEFAULT '';"_ns)));

  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL("UPDATE entries SET request_integrity = '';"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(22)));

  aRewriteSchema = true;

  return NS_OK;
}

nsresult MigrateFrom22To23(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // The only change between 22 and 23 was a different snappy compression
  // format, but it's backwards-compatible.
  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(23)));

  return NS_OK;
}

nsresult MigrateFrom23To24(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Add the request_url_fragment column.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN request_url_fragment TEXT NOT NULL DEFAULT ''"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(24)));

  aRewriteSchema = true;

  return NS_OK;
}

nsresult MigrateFrom24To25(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // The only change between 24 and 25 was a new nsIContentPolicy type.
  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(25)));

  return NS_OK;
}

nsresult MigrateFrom25To26(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Add the response_padding_size column.
  // Note: only opaque repsonse should be non-null interger.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN response_padding_size INTEGER NULL "_ns)));

  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL("UPDATE entries SET response_padding_size = 0 "
                             "WHERE response_type = 4"_ns  // opaque response
                             )));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(26)));

  aRewriteSchema = true;

  return NS_OK;
}

nsresult MigrateFrom26To27(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(kHackyDowngradeSchemaVersion)));

  return NS_OK;
}

nsresult MigrateFrom27To28(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // In Bug 1264178, we added a column request_integrity into table entries.
  // However, at that time, the default value for the existing rows is NULL
  // which against the statement in kTableEntries. Thus, we need to have another
  // upgrade to update these values to an empty string.
  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL("UPDATE entries SET request_integrity = '' "
                             "WHERE request_integrity is NULL;"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(28)));

  return NS_OK;
}

class BodyDiskSizeGetterFunction final : public mozIStorageFunction {
 public:
  explicit BodyDiskSizeGetterFunction(nsCOMPtr<nsIFile> aDBDir)
      : mDBDir(std::move(aDBDir)), mTotalDiskUsage(0) {}

  NS_DECL_ISUPPORTS

  int64_t TotalDiskUsage() const { return mTotalDiskUsage; }

 private:
  ~BodyDiskSizeGetterFunction() = default;

  NS_IMETHOD
  OnFunctionCall(mozIStorageValueArray* aArguments,
                 nsIVariant** aResult) override {
    MOZ_ASSERT(aArguments);
    MOZ_ASSERT(aResult);

    AUTO_PROFILER_LABEL("BodyDiskSizeGetterFunction::OnFunctionCall", DOM);

    uint32_t argc;
    QM_TRY(MOZ_TO_RESULT(aArguments->GetNumEntries(&argc)));

    if (argc != 1) {
      NS_WARNING("Don't call me with the wrong number of arguments!");
      return NS_ERROR_UNEXPECTED;
    }

    int32_t type;
    QM_TRY(MOZ_TO_RESULT(aArguments->GetTypeOfIndex(0, &type)));

    if (type == mozIStorageStatement::VALUE_TYPE_NULL) {
      nsCOMPtr<nsIVariant> result = new mozilla::storage::NullVariant();

      result.forget(aResult);
      return NS_OK;
    }

    if (type != mozIStorageStatement::VALUE_TYPE_TEXT) {
      NS_WARNING("Don't call me with the wrong type of arguments!");
      return NS_ERROR_UNEXPECTED;
    }

    QM_TRY_INSPECT(const auto& idString,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, aArguments,
                                                     GetUTF8String, 0));

    nsID id{};
    QM_TRY(OkIf(id.Parse(idString)), Err(NS_ERROR_UNEXPECTED));

    QM_TRY_INSPECT(
        const auto& fileSize,
        QM_OR_ELSE_WARN_IF(
            // Expression.
            GetBodyDiskSize(*mDBDir, id),
            // Predicate.
            ([](const nsresult rv) { return rv == NS_ERROR_FILE_NOT_FOUND; }),
            // Fallback. If the file does no longer exist, treat
            // it as 0-sized.
            (ErrToOk<0, int64_t>)));

    CheckedInt64 totalDiskUsage = mTotalDiskUsage + fileSize;
    mTotalDiskUsage =
        totalDiskUsage.isValid() ? totalDiskUsage.value() : INT64_MAX;

    nsCOMPtr<nsIVariant> result =
        new mozilla::storage::IntegerVariant(fileSize);

    result.forget(aResult);
    return NS_OK;
  }

  nsCOMPtr<nsIFile> mDBDir;
  int64_t mTotalDiskUsage;
};

NS_IMPL_ISUPPORTS(BodyDiskSizeGetterFunction, mozIStorageFunction)

nsresult MigrateFrom28To29(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN request_body_disk_size INTEGER NULL;"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN response_body_disk_size INTEGER NULL;"_ns)));

  RefPtr<BodyDiskSizeGetterFunction> bodyDiskSizeGetter =
      new BodyDiskSizeGetterFunction(&aDBDir);

  constexpr auto bodyDiskSizeGetterName = "get_body_disk_size"_ns;

  QM_TRY(MOZ_TO_RESULT(
      aConn.CreateFunction(bodyDiskSizeGetterName, 1, bodyDiskSizeGetter)));

  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "UPDATE entries SET "
      "request_body_disk_size = get_body_disk_size(request_body_id), "
      "response_body_disk_size = get_body_disk_size(response_body_id);"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.RemoveFunction(bodyDiskSizeGetterName)));

  QM_TRY(
      MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(nsLiteralCString(kTableUsageInfo))));

  QM_TRY_INSPECT(
      const auto& state,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConn, CreateStatement,
          "INSERT INTO usage_info VALUES(1, :total_disk_usage);"_ns));

  QM_TRY(MOZ_TO_RESULT(state->BindInt64ByName(
      "total_disk_usage"_ns, bodyDiskSizeGetter->TotalDiskUsage())));

  QM_TRY(MOZ_TO_RESULT(state->Execute()));

  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL(nsLiteralCString(kTriggerEntriesInsert))));

  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL(nsLiteralCString(kTriggerEntriesUpdate))));

  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL(nsLiteralCString(kTriggerEntriesDelete))));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(29)));

  aRewriteSchema = true;

  return NS_OK;
}

nsresult MigrateFrom29To30(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Add an index on response_headers(entry_id, name) to dramatically speed up
  // vary header lookups during cache matching operations.
  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL(nsLiteralCString(kIndexResponseHeadersEntryId))));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(30)));

  return NS_OK;
}

nsresult MigrateFrom30To31(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Existing response objects would be getting credential type set to 2,
  // which means that these response objects can no longer be loaded by
  // credentialless cross-origin embedders which is fine; a network fetch
  // would need to be performed for such objects.
  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN response_credentials INTEGER NOT NULL DEFAULT 2;"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(31)));

  aRewriteSchema = true;

  return NS_OK;
}

nsresult MigrateFrom31To32(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Remove the unused response_headers_name_index
  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL("DROP INDEX response_headers_name_index "_ns)));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(32)));

  return NS_OK;
}

nsresult MigrateFrom32To33(nsIFile& aDBDir, mozIStorageConnection& aConn,
                           bool& aRewriteSchema) {
  MOZ_ASSERT(!NS_IsMainThread());

  QM_TRY(MOZ_TO_RESULT(aConn.ExecuteSimpleSQL(
      "ALTER TABLE entries "
      "ADD COLUMN pending_deletion INTEGER NOT NULL DEFAULT 0;"_ns)));

  QM_TRY(MOZ_TO_RESULT(
      aConn.ExecuteSimpleSQL(nsLiteralCString(kIndexEntriesPendingDeletion))));

  QM_TRY(MOZ_TO_RESULT(aConn.SetSchemaVersion(33)));

  aRewriteSchema = true;

  return NS_OK;
}

}  // anonymous namespace
}  // namespace mozilla::dom::cache::db

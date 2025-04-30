/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Don't manually modify this line, as it is automatically replaced on merge day
// by the gecko_migration.py script.
export const WEAVE_VERSION = "1.142.0";

// Sync Server API version that the client supports.
export const SYNC_API_VERSION = "1.5";

// Version of the data format this client supports. The data format describes
// how records are packaged; this is separate from the Server API version and
// the per-engine cleartext formats.
export const STORAGE_VERSION = 5;
export const PREFS_BRANCH = "services.sync.";

// Put in [] because those aren't allowed in a collection name.
export const DEFAULT_KEYBUNDLE_NAME = "[default]";

// Key dimensions.
export const SYNC_KEY_ENCODED_LENGTH = 26;
export const SYNC_KEY_DECODED_LENGTH = 16;

export const NO_SYNC_NODE_INTERVAL = 10 * 60 * 1000; // 10 minutes

export const MAX_ERROR_COUNT_BEFORE_BACKOFF = 3;

// Backoff intervals
export const MINIMUM_BACKOFF_INTERVAL = 15 * 60 * 1000; // 15 minutes
export const MAXIMUM_BACKOFF_INTERVAL = 8 * 60 * 60 * 1000; // 8 hours

// HMAC event handling timeout.
// 10 minutes = a compromise between the multi-desktop sync interval
// and the mobile sync interval.
export const HMAC_EVENT_INTERVAL = 600000;

// How long to wait between sync attempts if the Master Password is locked.
export const MASTER_PASSWORD_LOCKED_RETRY_INTERVAL = 15 * 60 * 1000; // 15 minutes

// 50 is hardcoded here because of URL length restrictions.
// (GUIDs can be up to 64 chars long.)
// Individual engines can set different values for their limit if their
// identifiers are shorter.
export const DEFAULT_GUID_FETCH_BATCH_SIZE = 50;

// Default batch size for download batching
// (how many records are fetched at a time from the server when batching is used).
export const DEFAULT_DOWNLOAD_BATCH_SIZE = 1000;

// score thresholds for early syncs
export const SINGLE_USER_THRESHOLD = 1000;
export const MULTI_DEVICE_THRESHOLD = 300;

// Other score increment constants
export const SCORE_INCREMENT_SMALL = 1;
export const SCORE_INCREMENT_MEDIUM = 10;

// Instant sync score increment
export const SCORE_INCREMENT_XLARGE = 300 + 1; //MULTI_DEVICE_THRESHOLD + 1

// Delay before incrementing global score
export const SCORE_UPDATE_DELAY = 100;

// Delay for the back observer debouncer. This is chosen to be longer than any
// observed spurious idle/back events and short enough to pre-empt user activity.
export const IDLE_OBSERVER_BACK_DELAY = 100;

// Duplicate URI_LENGTH_MAX from Places (from nsNavHistory.h), used to discard
// tabs with huge uris during tab sync.
export const URI_LENGTH_MAX = 65536;

export const MAX_HISTORY_UPLOAD = 5000;
export const MAX_HISTORY_DOWNLOAD = 5000;

// Top-level statuses
export const STATUS_OK = "success.status_ok";
export const SYNC_FAILED = "error.sync.failed";
export const LOGIN_FAILED = "error.login.failed";
export const SYNC_FAILED_PARTIAL = "error.sync.failed_partial";
export const CLIENT_NOT_CONFIGURED = "service.client_not_configured";
export const STATUS_DISABLED = "service.disabled";
export const MASTER_PASSWORD_LOCKED = "service.master_password_locked";

// success states
export const LOGIN_SUCCEEDED = "success.login";
export const SYNC_SUCCEEDED = "success.sync";
export const ENGINE_SUCCEEDED = "success.engine";

// login failure status codes
export const LOGIN_FAILED_NO_USERNAME = "error.login.reason.no_username";
export const LOGIN_FAILED_NO_PASSPHRASE = "error.login.reason.no_recoverykey";
export const LOGIN_FAILED_NETWORK_ERROR = "error.login.reason.network";
export const LOGIN_FAILED_SERVER_ERROR = "error.login.reason.server";
export const LOGIN_FAILED_INVALID_PASSPHRASE = "error.login.reason.recoverykey";
export const LOGIN_FAILED_LOGIN_REJECTED = "error.login.reason.account";

// sync failure status codes
export const METARECORD_DOWNLOAD_FAIL =
  "error.sync.reason.metarecord_download_fail";
export const VERSION_OUT_OF_DATE = "error.sync.reason.version_out_of_date";
export const CREDENTIALS_CHANGED = "error.sync.reason.credentials_changed";
export const ABORT_SYNC_COMMAND = "aborting sync, process commands said so";
export const NO_SYNC_NODE_FOUND = "error.sync.reason.no_node_found";
export const OVER_QUOTA = "error.sync.reason.over_quota";
export const SERVER_MAINTENANCE = "error.sync.reason.serverMaintenance";

export const RESPONSE_OVER_QUOTA = "14";

// engine failure status codes
export const ENGINE_UPLOAD_FAIL = "error.engine.reason.record_upload_fail";
export const ENGINE_DOWNLOAD_FAIL = "error.engine.reason.record_download_fail";
export const ENGINE_UNKNOWN_FAIL = "error.engine.reason.unknown_fail";
export const ENGINE_APPLY_FAIL = "error.engine.reason.apply_fail";
// an upload failure where the batch was interrupted with a 412
export const ENGINE_BATCH_INTERRUPTED = "error.engine.reason.batch_interrupted";

// Ways that a sync can be disabled (messages only to be printed in debug log)
export const kSyncMasterPasswordLocked =
  "User elected to leave Primary Password locked";
export const kSyncWeaveDisabled = "Weave is disabled";
export const kSyncNetworkOffline = "Network is offline";
export const kSyncBackoffNotMet =
  "Trying to sync before the server said it's okay";
export const kFirstSyncChoiceNotMade =
  "User has not selected an action for first sync";
export const kSyncNotConfigured = "Sync is not configured";
export const kFirefoxShuttingDown = "Firefox is about to shut down";

export const DEVICE_TYPE_DESKTOP = "desktop";
export const DEVICE_TYPE_MOBILE = "mobile";
export const DEVICE_TYPE_TABLET = "tablet";

export const SQLITE_MAX_VARIABLE_NUMBER = 999;

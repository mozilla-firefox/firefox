/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { Log } from "resource://gre/modules/Log.sys.mjs";
import { LogManager } from "resource://gre/modules/LogManager.sys.mjs";

// loglevel should be one of "Fatal", "Error", "Warn", "Info", "Config",
// "Debug", "Trace" or "All". If none is specified, "Debug" will be used by
// default.  Note "Debug" is usually appropriate so that when this log is
// included in the Sync file logs we get verbose output.
const PREF_LOG_LEVEL = "identity.fxaccounts.loglevel";

// A pref that can be set so "sensitive" information (eg, personally
// identifiable info, credentials, etc) will be logged.
const PREF_LOG_SENSITIVE_DETAILS = "identity.fxaccounts.log.sensitive";

export let log = Log.repository.getLogger("FirefoxAccounts");
log.manageLevelFromPref(PREF_LOG_LEVEL);

let logs = [
  "Sync",
  "Services.Common",
  "FirefoxAccounts",
  "Hawk",
  "browserwindow.syncui",
  "BookmarkSyncUtils",
  "addons.xpi",
];

// For legacy reasons, the log manager still thinks it's part of sync.
export let logManager = new LogManager({
  prefRoot: "services.sync.",
  logNames: logs,
  logFilePrefix: "sync",
  logFileSubDirectoryEntries: ["weave", "logs"],
  testTopicPrefix: "services-tests:common:log-manager:",
});

// A boolean to indicate if personally identifiable information (or anything
// else sensitive, such as credentials) should be logged.
export let logPII = () =>
  Services.prefs.getBoolPref(PREF_LOG_SENSITIVE_DETAILS, false);

export let FXACCOUNTS_PERMISSION = "firefox-accounts";

export let DATA_FORMAT_VERSION = 1;
export let DEFAULT_STORAGE_FILENAME = "signedInUser.json";

export let OAUTH_TOKEN_FOR_SYNC_LIFETIME_SECONDS = 3600 * 6; // 6 hours
export let ATTACHED_CLIENTS_CACHE_DURATION = 4 * 60 * 60 * 1000; // 4 hours in ms

// After we start polling for account verification, we stop polling when this
// many milliseconds have elapsed.
export let POLL_SESSION = 1000 * 60 * 20; // 20 minutes

// Observer notifications.
export let ONLOGIN_NOTIFICATION = "fxaccounts:onlogin";
export let ONVERIFIED_NOTIFICATION = "fxaccounts:onverified";
export let ONLOGOUT_NOTIFICATION = "fxaccounts:onlogout";
export let ON_PRELOGOUT_NOTIFICATION = "fxaccounts:on_pre_logout";
// Internal to services/fxaccounts only
export let ON_DEVICE_CONNECTED_NOTIFICATION = "fxaccounts:device_connected";
export let ON_DEVICE_DISCONNECTED_NOTIFICATION =
  "fxaccounts:device_disconnected";
export let ON_PROFILE_UPDATED_NOTIFICATION = "fxaccounts:profile_updated"; // Push
export let ON_PASSWORD_CHANGED_NOTIFICATION = "fxaccounts:password_changed";
export let ON_PASSWORD_RESET_NOTIFICATION = "fxaccounts:password_reset";
export let ON_ACCOUNT_DESTROYED_NOTIFICATION = "fxaccounts:account_destroyed";
export let ON_COLLECTION_CHANGED_NOTIFICATION = "sync:collection_changed";
export let ON_COMMAND_RECEIVED_NOTIFICATION = "fxaccounts:command_received";
export let ON_SERVICE_ENABLED_NOTIFICATION = "fxaccounts:service_enabled";

export let FXA_PUSH_SCOPE_ACCOUNT_UPDATE = "chrome://fxa-device-update";

export let ON_PROFILE_CHANGE_NOTIFICATION = "fxaccounts:profilechange"; // WebChannel
export let ON_ACCOUNT_STATE_CHANGE_NOTIFICATION = "fxaccounts:statechange";
export let ON_NEW_DEVICE_ID = "fxaccounts:new_device_id";
export let ON_DEVICELIST_UPDATED = "fxaccounts:devicelist_updated";

// The common prefix for all commands.
export let COMMAND_PREFIX = "https://identity.mozilla.com/cmd/";

// The commands we support - only the _TAIL values are recorded in telemetry.
export let COMMAND_SENDTAB_TAIL = "open-uri";
export let COMMAND_SENDTAB = COMMAND_PREFIX + COMMAND_SENDTAB_TAIL;
// A command to close a tab on this device
export let COMMAND_CLOSETAB_TAIL = "close-uri/v1";
export let COMMAND_CLOSETAB = COMMAND_PREFIX + COMMAND_CLOSETAB_TAIL;

// OAuth
export let CLIENT_IS_THUNDERBIRD = AppConstants.MOZ_APP_NAME == "thunderbird";
let FX_OAUTH_CLIENT_ID = "5882386c6d801776";
let TB_OAUTH_CLIENT_ID = "8269bacd7bbc7f80";
export let OAUTH_CLIENT_ID = CLIENT_IS_THUNDERBIRD
  ? TB_OAUTH_CLIENT_ID
  : FX_OAUTH_CLIENT_ID;
export let SCOPE_PROFILE = "profile";
export let SCOPE_PROFILE_WRITE = "profile:write";
export let SCOPE_PROFILE_UID = "profile:uid";
export let SCOPE_SMART_WINDOW = "https://identity.mozilla.com/apps/smartwindow";

// Sync scope in Firefox.
export let SCOPE_OLD_SYNC = "https://identity.mozilla.com/apps/oldsync";
// Sync scope in Thunderbird.
let SCOPE_THUNDERBIRD_SYNC = "https://identity.thunderbird.net/apps/sync";
// The scope to use for sync, depending on the current application.
export let SCOPE_APP_SYNC = CLIENT_IS_THUNDERBIRD
  ? SCOPE_THUNDERBIRD_SYNC
  : SCOPE_OLD_SYNC;

// This scope was previously used to calculate a telemetry tracking identifier for
// the account, but that system has since been decommissioned. It's here entirely
// so that we can remove the corresponding key from storage if present. We should
// be safe to remove it after some sensible period of time has elapsed to allow
// most clients to update; ref Bug 1697596.
export let DEPRECATED_SCOPE_ECOSYSTEM_TELEMETRY =
  "https://identity.mozilla.com/ids/ecosystem_telemetry";

// OAuth metadata for other Firefox-related services that we might need to know about
// in order to provide an enhanced user experience.
export let FX_MONITOR_OAUTH_CLIENT_ID = "802d56ef2a9af9fa";
export let FX_RELAY_OAUTH_CLIENT_ID = "9ebfe2c2f9ea3c58";
export let VPN_OAUTH_CLIENT_ID = "e6eb0d1e856335fc";

// UI Requests.
export let UI_REQUEST_SIGN_IN_FLOW = "signInFlow";
export let UI_REQUEST_REFRESH_AUTH = "refreshAuthentication";

// Firefox Accounts WebChannel ID
export let WEBCHANNEL_ID = "account_updates";

// WebChannel commands
export let COMMAND_PAIR_HEARTBEAT = "fxaccounts:pair_heartbeat";
export let COMMAND_PAIR_SUPP_METADATA = "fxaccounts:pair_supplicant_metadata";
export let COMMAND_PAIR_AUTHORIZE = "fxaccounts:pair_authorize";
export let COMMAND_PAIR_DECLINE = "fxaccounts:pair_decline";
export let COMMAND_PAIR_COMPLETE = "fxaccounts:pair_complete";
// Asks the browser to start an OAuth flow on behalf of a pairing supplicant
// and to hand the resulting OAuth parameters back to FxA.
export let COMMAND_PAIR_OAUTH_START = "fxaccounts:pair_oauth_start";
// Asks the browser, acting as the pairing authority, to grant an OAuth
// authorization code for the supplicant's OAuth parameters.
export let COMMAND_PAIR_OAUTH_FINISH = "fxaccounts:pair_oauth_finish";

export let COMMAND_PROFILE_CHANGE = "profile:change";
export let COMMAND_CAN_LINK_ACCOUNT = "fxaccounts:can_link_account";
export let COMMAND_LOGIN = "fxaccounts:login";
export let COMMAND_OAUTH = "fxaccounts:oauth_login";
export let COMMAND_OAUTH_FLOW_IS_ACTIVE = "fxaccounts:oauth_flow_is_active";
export let COMMAND_OAUTH_FLOW_BEGIN = "fxaccounts:oauth_flow_begin";
export let COMMAND_LOGOUT = "fxaccounts:logout";
export let COMMAND_DELETE = "fxaccounts:delete";
export let COMMAND_SYNC_PREFERENCES = "fxaccounts:sync_preferences";
export let COMMAND_CHANGE_PASSWORD = "fxaccounts:change_password";
export let COMMAND_FXA_STATUS = "fxaccounts:fxa_status";
export let COMMAND_PAIR_PREFERENCES = "fxaccounts:pair_preferences";
export let COMMAND_FIREFOX_VIEW = "fxaccounts:firefox_view";

// The pref branch where any prefs which relate to a specific account should
// be stored. This branch will be reset on account signout and signin.
export let PREF_ACCOUNT_ROOT = "identity.fxaccounts.account.";

// Where we store the hashed uid of the previous user.
export let PREF_LAST_FXA_USER_UID =
  "identity.fxaccounts.lastSignedInUserIdHash";
// Where we used to store the hashed email of the previous user. We now store the
// uid, but need to migrate from this.
export let PREF_LAST_FXA_USER_EMAIL =
  "identity.fxaccounts.lastSignedInUserHash";
export let PREF_REMOTE_PAIRING_URI = "identity.fxaccounts.remote.pairing.uri";

// Server errno.
// From https://github.com/mozilla/fxa-auth-server/blob/master/docs/api.md#response-format
export let ERRNO_ACCOUNT_ALREADY_EXISTS = 101;
export let ERRNO_ACCOUNT_DOES_NOT_EXIST = 102;
export let ERRNO_INCORRECT_PASSWORD = 103;
export let ERRNO_UNVERIFIED_ACCOUNT = 104;
export let ERRNO_INVALID_VERIFICATION_CODE = 105;
export let ERRNO_NOT_VALID_JSON_BODY = 106;
export let ERRNO_INVALID_BODY_PARAMETERS = 107;
export let ERRNO_MISSING_BODY_PARAMETERS = 108;
export let ERRNO_INVALID_REQUEST_SIGNATURE = 109;
export let ERRNO_INVALID_AUTH_TOKEN = 110;
export let ERRNO_INVALID_AUTH_TIMESTAMP = 111;
export let ERRNO_MISSING_CONTENT_LENGTH = 112;
export let ERRNO_REQUEST_BODY_TOO_LARGE = 113;
export let ERRNO_TOO_MANY_CLIENT_REQUESTS = 114;
export let ERRNO_INVALID_AUTH_NONCE = 115;
export let ERRNO_ENDPOINT_NO_LONGER_SUPPORTED = 116;
export let ERRNO_INCORRECT_LOGIN_METHOD = 117;
export let ERRNO_INCORRECT_KEY_RETRIEVAL_METHOD = 118;
export let ERRNO_INCORRECT_API_VERSION = 119;
export let ERRNO_INCORRECT_EMAIL_CASE = 120;
export let ERRNO_ACCOUNT_LOCKED = 121;
export let ERRNO_ACCOUNT_UNLOCKED = 122;
export let ERRNO_UNKNOWN_DEVICE = 123;
export let ERRNO_DEVICE_SESSION_CONFLICT = 124;
export let ERRNO_SERVICE_TEMP_UNAVAILABLE = 201;
export let ERRNO_PARSE = 997;
export let ERRNO_NETWORK = 998;
export let ERRNO_UNKNOWN_ERROR = 999;

// Offset oauth server errnos so they don't conflict with auth server errnos
export let OAUTH_SERVER_ERRNO_OFFSET = 1000;

// OAuth Server errno.
export let ERRNO_UNKNOWN_CLIENT_ID = 101 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_INCORRECT_CLIENT_SECRET = 102 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_INCORRECT_REDIRECT_URI = 103 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_INVALID_FXA_ASSERTION = 104 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_UNKNOWN_CODE = 105 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_INCORRECT_CODE = 106 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_EXPIRED_CODE = 107 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_OAUTH_INVALID_TOKEN = 108 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_INVALID_REQUEST_PARAM = 109 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_INVALID_RESPONSE_TYPE = 110 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_UNAUTHORIZED = 111 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_FORBIDDEN = 112 + OAUTH_SERVER_ERRNO_OFFSET;
export let ERRNO_INVALID_CONTENT_TYPE = 113 + OAUTH_SERVER_ERRNO_OFFSET;

// Errors.
export let ERROR_ACCOUNT_ALREADY_EXISTS = "ACCOUNT_ALREADY_EXISTS";
export let ERROR_ACCOUNT_DOES_NOT_EXIST = "ACCOUNT_DOES_NOT_EXIST ";
export let ERROR_ACCOUNT_LOCKED = "ACCOUNT_LOCKED";
export let ERROR_ACCOUNT_UNLOCKED = "ACCOUNT_UNLOCKED";
export let ERROR_ALREADY_SIGNED_IN_USER = "ALREADY_SIGNED_IN_USER";
export let ERROR_DEVICE_SESSION_CONFLICT = "DEVICE_SESSION_CONFLICT";
export let ERROR_ENDPOINT_NO_LONGER_SUPPORTED = "ENDPOINT_NO_LONGER_SUPPORTED";
export let ERROR_INCORRECT_API_VERSION = "INCORRECT_API_VERSION";
export let ERROR_INCORRECT_EMAIL_CASE = "INCORRECT_EMAIL_CASE";
export let ERROR_INCORRECT_KEY_RETRIEVAL_METHOD =
  "INCORRECT_KEY_RETRIEVAL_METHOD";
export let ERROR_INCORRECT_LOGIN_METHOD = "INCORRECT_LOGIN_METHOD";
export let ERROR_INVALID_EMAIL = "INVALID_EMAIL";
export let ERROR_INVALID_AUDIENCE = "INVALID_AUDIENCE";
export let ERROR_INVALID_AUTH_TOKEN = "INVALID_AUTH_TOKEN";
export let ERROR_INVALID_AUTH_TIMESTAMP = "INVALID_AUTH_TIMESTAMP";
export let ERROR_INVALID_AUTH_NONCE = "INVALID_AUTH_NONCE";
export let ERROR_INVALID_BODY_PARAMETERS = "INVALID_BODY_PARAMETERS";
export let ERROR_INVALID_PASSWORD = "INVALID_PASSWORD";
export let ERROR_INVALID_VERIFICATION_CODE = "INVALID_VERIFICATION_CODE";
export let ERROR_INVALID_REFRESH_AUTH_VALUE = "INVALID_REFRESH_AUTH_VALUE";
export let ERROR_INVALID_REQUEST_SIGNATURE = "INVALID_REQUEST_SIGNATURE";
export let ERROR_INTERNAL_INVALID_USER = "INTERNAL_ERROR_INVALID_USER";
export let ERROR_MISSING_BODY_PARAMETERS = "MISSING_BODY_PARAMETERS";
export let ERROR_MISSING_CONTENT_LENGTH = "MISSING_CONTENT_LENGTH";
export let ERROR_NO_TOKEN_SESSION = "NO_TOKEN_SESSION";
export let ERROR_NO_SILENT_REFRESH_AUTH = "NO_SILENT_REFRESH_AUTH";
export let ERROR_NOT_VALID_JSON_BODY = "NOT_VALID_JSON_BODY";
export let ERROR_OFFLINE = "OFFLINE";
export let ERROR_PERMISSION_DENIED = "PERMISSION_DENIED";
export let ERROR_REQUEST_BODY_TOO_LARGE = "REQUEST_BODY_TOO_LARGE";
export let ERROR_SERVER_ERROR = "SERVER_ERROR";
export let ERROR_SYNC_DISABLED = "SYNC_DISABLED";
export let ERROR_TOO_MANY_CLIENT_REQUESTS = "TOO_MANY_CLIENT_REQUESTS";
export let ERROR_SERVICE_TEMP_UNAVAILABLE = "SERVICE_TEMPORARY_UNAVAILABLE";
export let ERROR_UI_ERROR = "UI_ERROR";
export let ERROR_UI_REQUEST = "UI_REQUEST";
export let ERROR_PARSE = "PARSE_ERROR";
export let ERROR_NETWORK = "NETWORK_ERROR";
export let ERROR_UNKNOWN = "UNKNOWN_ERROR";
export let ERROR_UNKNOWN_DEVICE = "UNKNOWN_DEVICE";
export let ERROR_UNVERIFIED_ACCOUNT = "UNVERIFIED_ACCOUNT";

// OAuth errors.
export let ERROR_UNKNOWN_CLIENT_ID = "UNKNOWN_CLIENT_ID";
export let ERROR_INCORRECT_CLIENT_SECRET = "INCORRECT_CLIENT_SECRET";
export let ERROR_INCORRECT_REDIRECT_URI = "INCORRECT_REDIRECT_URI";
export let ERROR_INVALID_FXA_ASSERTION = "INVALID_FXA_ASSERTION";
export let ERROR_UNKNOWN_CODE = "UNKNOWN_CODE";
export let ERROR_INCORRECT_CODE = "INCORRECT_CODE";
export let ERROR_EXPIRED_CODE = "EXPIRED_CODE";
export let ERROR_OAUTH_INVALID_TOKEN = "OAUTH_INVALID_TOKEN";
export let ERROR_INVALID_REQUEST_PARAM = "INVALID_REQUEST_PARAM";
export let ERROR_INVALID_RESPONSE_TYPE = "INVALID_RESPONSE_TYPE";
export let ERROR_UNAUTHORIZED = "UNAUTHORIZED";
export let ERROR_FORBIDDEN = "FORBIDDEN";
export let ERROR_INVALID_CONTENT_TYPE = "INVALID_CONTENT_TYPE";

// Additional generic error classes for external consumers
export let ERROR_NO_ACCOUNT = "NO_ACCOUNT";
export let ERROR_AUTH_ERROR = "AUTH_ERROR";
export let ERROR_INVALID_PARAMETER = "INVALID_PARAMETER";

// Status code errors
export let ERROR_CODE_METHOD_NOT_ALLOWED = 405;
export let ERROR_MSG_METHOD_NOT_ALLOWED = "METHOD_NOT_ALLOWED";

// FxAccounts has the ability to "split" the credentials between a plain-text
// JSON file in the profile dir and in the login manager.
// In order to prevent new fields accidentally ending up in the "wrong" place,
// all fields stored are listed here.

// The fields we save in the plaintext JSON.
// See bug 1013064 comments 23-25 for why the sessionToken is "safe"
export let FXA_PWDMGR_PLAINTEXT_FIELDS = new Set([
  "email",
  "verified",
  "authAt",
  "sessionToken",
  "uid",
  "oauthTokens",
  "profile",
  "device",
  "profileCache",
  "encryptedSendTabKeys",
  "encryptedCloseTabKeys",
  "requestedServices", // info about services which should be configured, used during oauth flows.
]);

// Fields we store in secure storage if it exists.
export let FXA_PWDMGR_SECURE_FIELDS = new Set([
  "keyFetchToken",
  "unwrapBKey",
  "scopedKeys",
]);

// An allowlist of fields that remain in storage when the user needs to
// reauthenticate. All other fields will be removed.
export let FXA_PWDMGR_REAUTH_ALLOWLIST = new Set([
  "email",
  "uid",
  "profile",
  "device",
  "verified",
]);

// The pseudo-host we use in the login manager
export let FXA_PWDMGR_HOST = "chrome://FirefoxAccounts";
// The realm we use in the login manager.
export let FXA_PWDMGR_REALM = "Firefox Accounts credentials";

// Error matching.
export let SERVER_ERRNO_TO_ERROR = {
  [ERRNO_ACCOUNT_ALREADY_EXISTS]: ERROR_ACCOUNT_ALREADY_EXISTS,
  [ERRNO_ACCOUNT_DOES_NOT_EXIST]: ERROR_ACCOUNT_DOES_NOT_EXIST,
  [ERRNO_INCORRECT_PASSWORD]: ERROR_INVALID_PASSWORD,
  [ERRNO_UNVERIFIED_ACCOUNT]: ERROR_UNVERIFIED_ACCOUNT,
  [ERRNO_INVALID_VERIFICATION_CODE]: ERROR_INVALID_VERIFICATION_CODE,
  [ERRNO_NOT_VALID_JSON_BODY]: ERROR_NOT_VALID_JSON_BODY,
  [ERRNO_INVALID_BODY_PARAMETERS]: ERROR_INVALID_BODY_PARAMETERS,
  [ERRNO_MISSING_BODY_PARAMETERS]: ERROR_MISSING_BODY_PARAMETERS,
  [ERRNO_INVALID_REQUEST_SIGNATURE]: ERROR_INVALID_REQUEST_SIGNATURE,
  [ERRNO_INVALID_AUTH_TOKEN]: ERROR_INVALID_AUTH_TOKEN,
  [ERRNO_INVALID_AUTH_TIMESTAMP]: ERROR_INVALID_AUTH_TIMESTAMP,
  [ERRNO_MISSING_CONTENT_LENGTH]: ERROR_MISSING_CONTENT_LENGTH,
  [ERRNO_REQUEST_BODY_TOO_LARGE]: ERROR_REQUEST_BODY_TOO_LARGE,
  [ERRNO_TOO_MANY_CLIENT_REQUESTS]: ERROR_TOO_MANY_CLIENT_REQUESTS,
  [ERRNO_INVALID_AUTH_NONCE]: ERROR_INVALID_AUTH_NONCE,
  [ERRNO_ENDPOINT_NO_LONGER_SUPPORTED]: ERROR_ENDPOINT_NO_LONGER_SUPPORTED,
  [ERRNO_INCORRECT_LOGIN_METHOD]: ERROR_INCORRECT_LOGIN_METHOD,
  [ERRNO_INCORRECT_KEY_RETRIEVAL_METHOD]: ERROR_INCORRECT_KEY_RETRIEVAL_METHOD,
  [ERRNO_INCORRECT_API_VERSION]: ERROR_INCORRECT_API_VERSION,
  [ERRNO_INCORRECT_EMAIL_CASE]: ERROR_INCORRECT_EMAIL_CASE,
  [ERRNO_ACCOUNT_LOCKED]: ERROR_ACCOUNT_LOCKED,
  [ERRNO_ACCOUNT_UNLOCKED]: ERROR_ACCOUNT_UNLOCKED,
  [ERRNO_UNKNOWN_DEVICE]: ERROR_UNKNOWN_DEVICE,
  [ERRNO_DEVICE_SESSION_CONFLICT]: ERROR_DEVICE_SESSION_CONFLICT,
  [ERRNO_SERVICE_TEMP_UNAVAILABLE]: ERROR_SERVICE_TEMP_UNAVAILABLE,
  [ERRNO_UNKNOWN_ERROR]: ERROR_UNKNOWN,
  [ERRNO_NETWORK]: ERROR_NETWORK,
  // oauth
  [ERRNO_UNKNOWN_CLIENT_ID]: ERROR_UNKNOWN_CLIENT_ID,
  [ERRNO_INCORRECT_CLIENT_SECRET]: ERROR_INCORRECT_CLIENT_SECRET,
  [ERRNO_INCORRECT_REDIRECT_URI]: ERROR_INCORRECT_REDIRECT_URI,
  [ERRNO_INVALID_FXA_ASSERTION]: ERROR_INVALID_FXA_ASSERTION,
  [ERRNO_UNKNOWN_CODE]: ERROR_UNKNOWN_CODE,
  [ERRNO_INCORRECT_CODE]: ERROR_INCORRECT_CODE,
  [ERRNO_EXPIRED_CODE]: ERROR_EXPIRED_CODE,
  [ERRNO_OAUTH_INVALID_TOKEN]: ERROR_OAUTH_INVALID_TOKEN,
  [ERRNO_INVALID_REQUEST_PARAM]: ERROR_INVALID_REQUEST_PARAM,
  [ERRNO_INVALID_RESPONSE_TYPE]: ERROR_INVALID_RESPONSE_TYPE,
  [ERRNO_UNAUTHORIZED]: ERROR_UNAUTHORIZED,
  [ERRNO_FORBIDDEN]: ERROR_FORBIDDEN,
  [ERRNO_INVALID_CONTENT_TYPE]: ERROR_INVALID_CONTENT_TYPE,
};

// Map internal errors to more generic error classes for consumers
export let ERROR_TO_GENERAL_ERROR_CLASS = {
  [ERROR_ACCOUNT_ALREADY_EXISTS]: ERROR_AUTH_ERROR,
  [ERROR_ACCOUNT_DOES_NOT_EXIST]: ERROR_AUTH_ERROR,
  [ERROR_ACCOUNT_LOCKED]: ERROR_AUTH_ERROR,
  [ERROR_ACCOUNT_UNLOCKED]: ERROR_AUTH_ERROR,
  [ERROR_ALREADY_SIGNED_IN_USER]: ERROR_AUTH_ERROR,
  [ERROR_DEVICE_SESSION_CONFLICT]: ERROR_AUTH_ERROR,
  [ERROR_ENDPOINT_NO_LONGER_SUPPORTED]: ERROR_AUTH_ERROR,
  [ERROR_INCORRECT_API_VERSION]: ERROR_AUTH_ERROR,
  [ERROR_INCORRECT_EMAIL_CASE]: ERROR_AUTH_ERROR,
  [ERROR_INCORRECT_KEY_RETRIEVAL_METHOD]: ERROR_AUTH_ERROR,
  [ERROR_INCORRECT_LOGIN_METHOD]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_EMAIL]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_AUDIENCE]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_AUTH_TOKEN]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_AUTH_TIMESTAMP]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_AUTH_NONCE]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_BODY_PARAMETERS]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_PASSWORD]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_VERIFICATION_CODE]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_REFRESH_AUTH_VALUE]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_REQUEST_SIGNATURE]: ERROR_AUTH_ERROR,
  [ERROR_INTERNAL_INVALID_USER]: ERROR_AUTH_ERROR,
  [ERROR_MISSING_BODY_PARAMETERS]: ERROR_AUTH_ERROR,
  [ERROR_MISSING_CONTENT_LENGTH]: ERROR_AUTH_ERROR,
  [ERROR_NO_TOKEN_SESSION]: ERROR_AUTH_ERROR,
  [ERROR_NO_SILENT_REFRESH_AUTH]: ERROR_AUTH_ERROR,
  [ERROR_NOT_VALID_JSON_BODY]: ERROR_AUTH_ERROR,
  [ERROR_PERMISSION_DENIED]: ERROR_AUTH_ERROR,
  [ERROR_REQUEST_BODY_TOO_LARGE]: ERROR_AUTH_ERROR,
  [ERROR_UNKNOWN_DEVICE]: ERROR_AUTH_ERROR,
  [ERROR_UNVERIFIED_ACCOUNT]: ERROR_AUTH_ERROR,
  [ERROR_UI_ERROR]: ERROR_AUTH_ERROR,
  [ERROR_UI_REQUEST]: ERROR_AUTH_ERROR,
  [ERROR_OFFLINE]: ERROR_NETWORK,
  [ERROR_SERVER_ERROR]: ERROR_NETWORK,
  [ERROR_TOO_MANY_CLIENT_REQUESTS]: ERROR_NETWORK,
  [ERROR_SERVICE_TEMP_UNAVAILABLE]: ERROR_NETWORK,
  [ERROR_PARSE]: ERROR_NETWORK,
  [ERROR_NETWORK]: ERROR_NETWORK,

  // oauth
  [ERROR_INCORRECT_CLIENT_SECRET]: ERROR_AUTH_ERROR,
  [ERROR_INCORRECT_REDIRECT_URI]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_FXA_ASSERTION]: ERROR_AUTH_ERROR,
  [ERROR_UNKNOWN_CODE]: ERROR_AUTH_ERROR,
  [ERROR_INCORRECT_CODE]: ERROR_AUTH_ERROR,
  [ERROR_EXPIRED_CODE]: ERROR_AUTH_ERROR,
  [ERROR_OAUTH_INVALID_TOKEN]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_REQUEST_PARAM]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_RESPONSE_TYPE]: ERROR_AUTH_ERROR,
  [ERROR_UNAUTHORIZED]: ERROR_AUTH_ERROR,
  [ERROR_FORBIDDEN]: ERROR_AUTH_ERROR,
  [ERROR_INVALID_CONTENT_TYPE]: ERROR_AUTH_ERROR,
};

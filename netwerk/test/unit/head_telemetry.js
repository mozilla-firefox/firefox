/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

"use strict";

var HandshakeTelemetryHelpers = {
  FLAVORS: ["", "_FIRST_TRY", "_CONSERVATIVE", "_ECH", "_ECH_GREASE"],

  // All known NSPR and NSS results. See the definition of tls_handshake.result
  // in security/manager/ssl/metrics.yaml.
  RESULTS: [
    "Success",
    "PR_Write_returned_0",
    "PR_OUT_OF_MEMORY_ERROR",
    "PR_BAD_DESCRIPTOR_ERROR",
    "PR_WOULD_BLOCK_ERROR",
    "PR_ACCESS_FAULT_ERROR",
    "PR_INVALID_METHOD_ERROR",
    "PR_ILLEGAL_ACCESS_ERROR",
    "PR_UNKNOWN_ERROR",
    "PR_PENDING_INTERRUPT_ERROR",
    "PR_NOT_IMPLEMENTED_ERROR",
    "PR_IO_ERROR",
    "PR_IO_TIMEOUT_ERROR",
    "PR_IO_PENDING_ERROR",
    "PR_DIRECTORY_OPEN_ERROR",
    "PR_INVALID_ARGUMENT_ERROR",
    "PR_ADDRESS_NOT_AVAILABLE_ERROR",
    "PR_ADDRESS_NOT_SUPPORTED_ERROR",
    "PR_IS_CONNECTED_ERROR",
    "PR_BAD_ADDRESS_ERROR",
    "PR_ADDRESS_IN_USE_ERROR",
    "PR_CONNECT_REFUSED_ERROR",
    "PR_NETWORK_UNREACHABLE_ERROR",
    "PR_CONNECT_TIMEOUT_ERROR",
    "PR_NOT_CONNECTED_ERROR",
    "PR_LOAD_LIBRARY_ERROR",
    "PR_UNLOAD_LIBRARY_ERROR",
    "PR_FIND_SYMBOL_ERROR",
    "PR_INSUFFICIENT_RESOURCES_ERROR",
    "PR_DIRECTORY_LOOKUP_ERROR",
    "PR_TPD_RANGE_ERROR",
    "PR_PROC_DESC_TABLE_FULL_ERROR",
    "PR_SYS_DESC_TABLE_FULL_ERROR",
    "PR_NOT_SOCKET_ERROR",
    "PR_NOT_TCP_SOCKET_ERROR",
    "PR_SOCKET_ADDRESS_IS_BOUND_ERROR",
    "PR_NO_ACCESS_RIGHTS_ERROR",
    "PR_OPERATION_NOT_SUPPORTED_ERROR",
    "PR_PROTOCOL_NOT_SUPPORTED_ERROR",
    "PR_REMOTE_FILE_ERROR",
    "PR_BUFFER_OVERFLOW_ERROR",
    "PR_CONNECT_RESET_ERROR",
    "PR_RANGE_ERROR",
    "PR_DEADLOCK_ERROR",
    "PR_FILE_IS_LOCKED_ERROR",
    "PR_FILE_TOO_BIG_ERROR",
    "PR_NO_DEVICE_SPACE_ERROR",
    "PR_PIPE_ERROR",
    "PR_NO_SEEK_DEVICE_ERROR",
    "PR_IS_DIRECTORY_ERROR",
    "PR_LOOP_ERROR",
    "PR_NAME_TOO_LONG_ERROR",
    "PR_FILE_NOT_FOUND_ERROR",
    "PR_NOT_DIRECTORY_ERROR",
    "PR_READ_ONLY_FILESYSTEM_ERROR",
    "PR_DIRECTORY_NOT_EMPTY_ERROR",
    "PR_FILESYSTEM_MOUNTED_ERROR",
    "PR_NOT_SAME_DEVICE_ERROR",
    "PR_DIRECTORY_CORRUPTED_ERROR",
    "PR_FILE_EXISTS_ERROR",
    "PR_MAX_DIRECTORY_ENTRIES_ERROR",
    "PR_INVALID_DEVICE_STATE_ERROR",
    "PR_DEVICE_IS_LOCKED_ERROR",
    "PR_NO_MORE_FILES_ERROR",
    "PR_END_OF_FILE_ERROR",
    "PR_FILE_SEEK_ERROR",
    "PR_FILE_IS_BUSY_ERROR",
    "PR_OPERATION_ABORTED_ERROR",
    "PR_IN_PROGRESS_ERROR",
    "PR_ALREADY_INITIATED_ERROR",
    "PR_GROUP_EMPTY_ERROR",
    "PR_INVALID_STATE_ERROR",
    "PR_NETWORK_DOWN_ERROR",
    "PR_SOCKET_SHUTDOWN_ERROR",
    "PR_CONNECT_ABORTED_ERROR",
    "PR_HOST_UNREACHABLE_ERROR",
    "PR_LIBRARY_NOT_LOADED_ERROR",
    "PR_CALL_ONCE_ERROR",
    "SSL_ERROR_EXPORT_ONLY_SERVER",
    "SSL_ERROR_US_ONLY_SERVER",
    "SSL_ERROR_NO_CYPHER_OVERLAP",
    "SSL_ERROR_NO_CERTIFICATE",
    "SSL_ERROR_BAD_CERTIFICATE",
    "SSL_ERROR_UNUSED_5",
    "SSL_ERROR_BAD_CLIENT",
    "SSL_ERROR_BAD_SERVER",
    "SSL_ERROR_UNSUPPORTED_CERTIFICATE_TYPE",
    "SSL_ERROR_UNSUPPORTED_VERSION",
    "SSL_ERROR_UNUSED_10",
    "SSL_ERROR_WRONG_CERTIFICATE",
    "SSL_ERROR_BAD_CERT_DOMAIN",
    "SSL_ERROR_POST_WARNING",
    "SSL_ERROR_SSL2_DISABLED",
    "SSL_ERROR_BAD_MAC_READ",
    "SSL_ERROR_BAD_MAC_ALERT",
    "SSL_ERROR_BAD_CERT_ALERT",
    "SSL_ERROR_REVOKED_CERT_ALERT",
    "SSL_ERROR_EXPIRED_CERT_ALERT",
    "SSL_ERROR_SSL_DISABLED",
    "SSL_ERROR_FORTEZZA_PQG",
    "SSL_ERROR_UNKNOWN_CIPHER_SUITE",
    "SSL_ERROR_NO_CIPHERS_SUPPORTED",
    "SSL_ERROR_BAD_BLOCK_PADDING",
    "SSL_ERROR_RX_RECORD_TOO_LONG",
    "SSL_ERROR_TX_RECORD_TOO_LONG",
    "SSL_ERROR_RX_MALFORMED_HELLO_REQUEST",
    "SSL_ERROR_RX_MALFORMED_CLIENT_HELLO",
    "SSL_ERROR_RX_MALFORMED_SERVER_HELLO",
    "SSL_ERROR_RX_MALFORMED_CERTIFICATE",
    "SSL_ERROR_RX_MALFORMED_SERVER_KEY_EXCH",
    "SSL_ERROR_RX_MALFORMED_CERT_REQUEST",
    "SSL_ERROR_RX_MALFORMED_HELLO_DONE",
    "SSL_ERROR_RX_MALFORMED_CERT_VERIFY",
    "SSL_ERROR_RX_MALFORMED_CLIENT_KEY_EXCH",
    "SSL_ERROR_RX_MALFORMED_FINISHED",
    "SSL_ERROR_RX_MALFORMED_CHANGE_CIPHER",
    "SSL_ERROR_RX_MALFORMED_ALERT",
    "SSL_ERROR_RX_MALFORMED_HANDSHAKE",
    "SSL_ERROR_RX_MALFORMED_APPLICATION_DATA",
    "SSL_ERROR_RX_UNEXPECTED_HELLO_REQUEST",
    "SSL_ERROR_RX_UNEXPECTED_CLIENT_HELLO",
    "SSL_ERROR_RX_UNEXPECTED_SERVER_HELLO",
    "SSL_ERROR_RX_UNEXPECTED_CERTIFICATE",
    "SSL_ERROR_RX_UNEXPECTED_SERVER_KEY_EXCH",
    "SSL_ERROR_RX_UNEXPECTED_CERT_REQUEST",
    "SSL_ERROR_RX_UNEXPECTED_HELLO_DONE",
    "SSL_ERROR_RX_UNEXPECTED_CERT_VERIFY",
    "SSL_ERROR_RX_UNEXPECTED_CLIENT_KEY_EXCH",
    "SSL_ERROR_RX_UNEXPECTED_FINISHED",
    "SSL_ERROR_RX_UNEXPECTED_CHANGE_CIPHER",
    "SSL_ERROR_RX_UNEXPECTED_ALERT",
    "SSL_ERROR_RX_UNEXPECTED_HANDSHAKE",
    "SSL_ERROR_RX_UNEXPECTED_APPLICATION_DATA",
    "SSL_ERROR_RX_UNKNOWN_RECORD_TYPE",
    "SSL_ERROR_RX_UNKNOWN_HANDSHAKE",
    "SSL_ERROR_RX_UNKNOWN_ALERT",
    "SSL_ERROR_CLOSE_NOTIFY_ALERT",
    "SSL_ERROR_HANDSHAKE_UNEXPECTED_ALERT",
    "SSL_ERROR_DECOMPRESSION_FAILURE_ALERT",
    "SSL_ERROR_HANDSHAKE_FAILURE_ALERT",
    "SSL_ERROR_ILLEGAL_PARAMETER_ALERT",
    "SSL_ERROR_UNSUPPORTED_CERT_ALERT",
    "SSL_ERROR_CERTIFICATE_UNKNOWN_ALERT",
    "SSL_ERROR_GENERATE_RANDOM_FAILURE",
    "SSL_ERROR_SIGN_HASHES_FAILURE",
    "SSL_ERROR_EXTRACT_PUBLIC_KEY_FAILURE",
    "SSL_ERROR_SERVER_KEY_EXCHANGE_FAILURE",
    "SSL_ERROR_CLIENT_KEY_EXCHANGE_FAILURE",
    "SSL_ERROR_ENCRYPTION_FAILURE",
    "SSL_ERROR_DECRYPTION_FAILURE",
    "SSL_ERROR_SOCKET_WRITE_FAILURE",
    "SSL_ERROR_MD5_DIGEST_FAILURE",
    "SSL_ERROR_SHA_DIGEST_FAILURE",
    "SSL_ERROR_MAC_COMPUTATION_FAILURE",
    "SSL_ERROR_SYM_KEY_CONTEXT_FAILURE",
    "SSL_ERROR_SYM_KEY_UNWRAP_FAILURE",
    "SSL_ERROR_PUB_KEY_SIZE_LIMIT_EXCEEDED",
    "SSL_ERROR_IV_PARAM_FAILURE",
    "SSL_ERROR_INIT_CIPHER_SUITE_FAILURE",
    "SSL_ERROR_SESSION_KEY_GEN_FAILURE",
    "SSL_ERROR_NO_SERVER_KEY_FOR_ALG",
    "SSL_ERROR_TOKEN_INSERTION_REMOVAL",
    "SSL_ERROR_TOKEN_SLOT_NOT_FOUND",
    "SSL_ERROR_NO_COMPRESSION_OVERLAP",
    "SSL_ERROR_HANDSHAKE_NOT_COMPLETED",
    "SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE",
    "SSL_ERROR_CERT_KEA_MISMATCH",
    "SSL_ERROR_NO_TRUSTED_SSL_CLIENT_CA",
    "SSL_ERROR_SESSION_NOT_FOUND",
    "SSL_ERROR_DECRYPTION_FAILED_ALERT",
    "SSL_ERROR_RECORD_OVERFLOW_ALERT",
    "SSL_ERROR_UNKNOWN_CA_ALERT",
    "SSL_ERROR_ACCESS_DENIED_ALERT",
    "SSL_ERROR_DECODE_ERROR_ALERT",
    "SSL_ERROR_DECRYPT_ERROR_ALERT",
    "SSL_ERROR_EXPORT_RESTRICTION_ALERT",
    "SSL_ERROR_PROTOCOL_VERSION_ALERT",
    "SSL_ERROR_INSUFFICIENT_SECURITY_ALERT",
    "SSL_ERROR_INTERNAL_ERROR_ALERT",
    "SSL_ERROR_USER_CANCELED_ALERT",
    "SSL_ERROR_NO_RENEGOTIATION_ALERT",
    "SSL_ERROR_SERVER_CACHE_NOT_CONFIGURED",
    "SSL_ERROR_UNSUPPORTED_EXTENSION_ALERT",
    "SSL_ERROR_CERTIFICATE_UNOBTAINABLE_ALERT",
    "SSL_ERROR_UNRECOGNIZED_NAME_ALERT",
    "SSL_ERROR_BAD_CERT_STATUS_RESPONSE_ALERT",
    "SSL_ERROR_BAD_CERT_HASH_VALUE_ALERT",
    "SSL_ERROR_RX_UNEXPECTED_NEW_SESSION_TICKET",
    "SSL_ERROR_RX_MALFORMED_NEW_SESSION_TICKET",
    "SSL_ERROR_DECOMPRESSION_FAILURE",
    "SSL_ERROR_RENEGOTIATION_NOT_ALLOWED",
    "SSL_ERROR_UNSAFE_NEGOTIATION",
    "SSL_ERROR_RX_UNEXPECTED_UNCOMPRESSED_RECORD",
    "SSL_ERROR_WEAK_SERVER_EPHEMERAL_DH_KEY",
    "SSL_ERROR_NEXT_PROTOCOL_DATA_INVALID",
    "SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_SSL2",
    "SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_SERVERS",
    "SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_CLIENTS",
    "SSL_ERROR_INVALID_VERSION_RANGE",
    "SSL_ERROR_CIPHER_DISALLOWED_FOR_VERSION",
    "SSL_ERROR_RX_MALFORMED_HELLO_VERIFY_REQUEST",
    "SSL_ERROR_RX_UNEXPECTED_HELLO_VERIFY_REQUEST",
    "SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_VERSION",
    "SSL_ERROR_RX_UNEXPECTED_CERT_STATUS",
    "SSL_ERROR_UNSUPPORTED_HASH_ALGORITHM",
    "SSL_ERROR_DIGEST_FAILURE",
    "SSL_ERROR_INCORRECT_SIGNATURE_ALGORITHM",
    "SSL_ERROR_NEXT_PROTOCOL_NO_CALLBACK",
    "SSL_ERROR_NEXT_PROTOCOL_NO_PROTOCOL",
    "SSL_ERROR_INAPPROPRIATE_FALLBACK_ALERT",
    "SSL_ERROR_WEAK_SERVER_CERT_KEY",
    "SSL_ERROR_RX_SHORT_DTLS_READ",
    "SSL_ERROR_NO_SUPPORTED_SIGNATURE_ALGORITHM",
    "SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM",
    "SSL_ERROR_MISSING_EXTENDED_MASTER_SECRET",
    "SSL_ERROR_UNEXPECTED_EXTENDED_MASTER_SECRET",
    "SSL_ERROR_RX_MALFORMED_KEY_SHARE",
    "SSL_ERROR_MISSING_KEY_SHARE",
    "SSL_ERROR_RX_MALFORMED_ECDHE_KEY_SHARE",
    "SSL_ERROR_RX_MALFORMED_DHE_KEY_SHARE",
    "SSL_ERROR_RX_UNEXPECTED_ENCRYPTED_EXTENSIONS",
    "SSL_ERROR_MISSING_EXTENSION_ALERT",
    "SSL_ERROR_KEY_EXCHANGE_FAILURE",
    "SSL_ERROR_EXTENSION_DISALLOWED_FOR_VERSION",
    "SSL_ERROR_RX_MALFORMED_ENCRYPTED_EXTENSIONS",
    "SSL_ERROR_MALFORMED_PRE_SHARED_KEY",
    "SSL_ERROR_MALFORMED_EARLY_DATA",
    "SSL_ERROR_END_OF_EARLY_DATA_ALERT",
    "SSL_ERROR_MISSING_ALPN_EXTENSION",
    "SSL_ERROR_RX_UNEXPECTED_EXTENSION",
    "SSL_ERROR_MISSING_SUPPORTED_GROUPS_EXTENSION",
    "SSL_ERROR_TOO_MANY_RECORDS",
    "SSL_ERROR_RX_UNEXPECTED_HELLO_RETRY_REQUEST",
    "SSL_ERROR_RX_MALFORMED_HELLO_RETRY_REQUEST",
    "SSL_ERROR_BAD_2ND_CLIENT_HELLO",
    "SSL_ERROR_MISSING_SIGNATURE_ALGORITHMS_EXTENSION",
    "SSL_ERROR_MALFORMED_PSK_KEY_EXCHANGE_MODES",
    "SSL_ERROR_MISSING_PSK_KEY_EXCHANGE_MODES",
    "SSL_ERROR_DOWNGRADE_WITH_EARLY_DATA",
    "SSL_ERROR_TOO_MUCH_EARLY_DATA",
    "SSL_ERROR_RX_UNEXPECTED_END_OF_EARLY_DATA",
    "SSL_ERROR_RX_MALFORMED_END_OF_EARLY_DATA",
    "SSL_ERROR_UNSUPPORTED_EXPERIMENTAL_API",
    "SSL_ERROR_APPLICATION_ABORT",
    "SSL_ERROR_APP_CALLBACK_ERROR",
    "SSL_ERROR_NO_TIMERS_FOUND",
    "SSL_ERROR_MISSING_COOKIE_EXTENSION",
    "SSL_ERROR_RX_UNEXPECTED_KEY_UPDATE",
    "SSL_ERROR_RX_MALFORMED_KEY_UPDATE",
    "SSL_ERROR_TOO_MANY_KEY_UPDATES",
    "SSL_ERROR_HANDSHAKE_FAILED",
    "SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR",
    "SSL_ERROR_RX_MALFORMED_DTLS_ACK",
    "SSL_ERROR_DH_KEY_TOO_LONG",
    "SSL_ERROR_RX_MALFORMED_ESNI_KEYS",
    "SSL_ERROR_RX_MALFORMED_ESNI_EXTENSION",
    "SSL_ERROR_MISSING_ESNI_EXTENSION",
    "SSL_ERROR_RX_UNEXPECTED_RECORD_TYPE",
    "SSL_ERROR_MISSING_POST_HANDSHAKE_AUTH_EXTENSION",
    "SSL_ERROR_RX_CERTIFICATE_REQUIRED_ALERT",
    "SSL_ERROR_DC_CERT_VERIFY_ALG_MISMATCH",
    "SSL_ERROR_DC_BAD_SIGNATURE",
    "SSL_ERROR_DC_INVALID_KEY_USAGE",
    "SSL_ERROR_DC_EXPIRED",
    "SSL_ERROR_DC_INAPPROPRIATE_VALIDITY_PERIOD",
    "SSL_ERROR_FEATURE_DISABLED",
    "SSL_ERROR_ECH_RETRY_WITH_ECH",
    "SSL_ERROR_ECH_RETRY_WITHOUT_ECH",
    "SSL_ERROR_ECH_FAILED",
    "SSL_ERROR_ECH_REQUIRED_ALERT",
    "SSL_ERROR_RX_MALFORMED_HYBRID_KEY_SHARE",
    "SEC_ERROR_IO",
    "SEC_ERROR_LIBRARY_FAILURE",
    "SEC_ERROR_BAD_DATA",
    "SEC_ERROR_OUTPUT_LEN",
    "SEC_ERROR_INPUT_LEN",
    "SEC_ERROR_INVALID_ARGS",
    "SEC_ERROR_INVALID_ALGORITHM",
    "SEC_ERROR_INVALID_AVA",
    "SEC_ERROR_INVALID_TIME",
    "SEC_ERROR_BAD_DER",
    "SEC_ERROR_BAD_SIGNATURE",
    "SEC_ERROR_EXPIRED_CERTIFICATE",
    "SEC_ERROR_REVOKED_CERTIFICATE",
    "SEC_ERROR_UNKNOWN_ISSUER",
    "SEC_ERROR_BAD_KEY",
    "SEC_ERROR_BAD_PASSWORD",
    "SEC_ERROR_RETRY_PASSWORD",
    "SEC_ERROR_NO_NODELOCK",
    "SEC_ERROR_BAD_DATABASE",
    "SEC_ERROR_NO_MEMORY",
    "SEC_ERROR_UNTRUSTED_ISSUER",
    "SEC_ERROR_UNTRUSTED_CERT",
    "SEC_ERROR_DUPLICATE_CERT",
    "SEC_ERROR_DUPLICATE_CERT_NAME",
    "SEC_ERROR_ADDING_CERT",
    "SEC_ERROR_FILING_KEY",
    "SEC_ERROR_NO_KEY",
    "SEC_ERROR_CERT_VALID",
    "SEC_ERROR_CERT_NOT_VALID",
    "SEC_ERROR_CERT_NO_RESPONSE",
    "SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE",
    "SEC_ERROR_CRL_EXPIRED",
    "SEC_ERROR_CRL_BAD_SIGNATURE",
    "SEC_ERROR_CRL_INVALID",
    "SEC_ERROR_EXTENSION_VALUE_INVALID",
    "SEC_ERROR_EXTENSION_NOT_FOUND",
    "SEC_ERROR_CA_CERT_INVALID",
    "SEC_ERROR_PATH_LEN_CONSTRAINT_INVALID",
    "SEC_ERROR_CERT_USAGES_INVALID",
    "SEC_INTERNAL_ONLY",
    "SEC_ERROR_INVALID_KEY",
    "SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION",
    "SEC_ERROR_OLD_CRL",
    "SEC_ERROR_NO_EMAIL_CERT",
    "SEC_ERROR_NO_RECIPIENT_CERTS_QUERY",
    "SEC_ERROR_NOT_A_RECIPIENT",
    "SEC_ERROR_PKCS7_KEYALG_MISMATCH",
    "SEC_ERROR_PKCS7_BAD_SIGNATURE",
    "SEC_ERROR_UNSUPPORTED_KEYALG",
    "SEC_ERROR_DECRYPTION_DISALLOWED",
    "XP_SEC_FORTEZZA_BAD_CARD",
    "XP_SEC_FORTEZZA_NO_CARD",
    "XP_SEC_FORTEZZA_NONE_SELECTED",
    "XP_SEC_FORTEZZA_MORE_INFO",
    "XP_SEC_FORTEZZA_PERSON_NOT_FOUND",
    "XP_SEC_FORTEZZA_NO_MORE_INFO",
    "XP_SEC_FORTEZZA_BAD_PIN",
    "XP_SEC_FORTEZZA_PERSON_ERROR",
    "SEC_ERROR_NO_KRL",
    "SEC_ERROR_KRL_EXPIRED",
    "SEC_ERROR_KRL_BAD_SIGNATURE",
    "SEC_ERROR_REVOKED_KEY",
    "SEC_ERROR_KRL_INVALID",
    "SEC_ERROR_NEED_RANDOM",
    "SEC_ERROR_NO_MODULE",
    "SEC_ERROR_NO_TOKEN",
    "SEC_ERROR_READ_ONLY",
    "SEC_ERROR_NO_SLOT_SELECTED",
    "SEC_ERROR_CERT_NICKNAME_COLLISION",
    "SEC_ERROR_KEY_NICKNAME_COLLISION",
    "SEC_ERROR_SAFE_NOT_CREATED",
    "SEC_ERROR_BAGGAGE_NOT_CREATED",
    "XP_JAVA_REMOVE_PRINCIPAL_ERROR",
    "XP_JAVA_DELETE_PRIVILEGE_ERROR",
    "XP_JAVA_CERT_NOT_EXISTS_ERROR",
    "SEC_ERROR_BAD_EXPORT_ALGORITHM",
    "SEC_ERROR_EXPORTING_CERTIFICATES",
    "SEC_ERROR_IMPORTING_CERTIFICATES",
    "SEC_ERROR_PKCS12_DECODING_PFX",
    "SEC_ERROR_PKCS12_INVALID_MAC",
    "SEC_ERROR_PKCS12_UNSUPPORTED_MAC_ALGORITHM",
    "SEC_ERROR_PKCS12_UNSUPPORTED_TRANSPORT_MODE",
    "SEC_ERROR_PKCS12_CORRUPT_PFX_STRUCTURE",
    "SEC_ERROR_PKCS12_UNSUPPORTED_PBE_ALGORITHM",
    "SEC_ERROR_PKCS12_UNSUPPORTED_VERSION",
    "SEC_ERROR_PKCS12_PRIVACY_PASSWORD_INCORRECT",
    "SEC_ERROR_PKCS12_CERT_COLLISION",
    "SEC_ERROR_USER_CANCELLED",
    "SEC_ERROR_PKCS12_DUPLICATE_DATA",
    "SEC_ERROR_MESSAGE_SEND_ABORTED",
    "SEC_ERROR_INADEQUATE_KEY_USAGE",
    "SEC_ERROR_INADEQUATE_CERT_TYPE",
    "SEC_ERROR_CERT_ADDR_MISMATCH",
    "SEC_ERROR_PKCS12_UNABLE_TO_IMPORT_KEY",
    "SEC_ERROR_PKCS12_IMPORTING_CERT_CHAIN",
    "SEC_ERROR_PKCS12_UNABLE_TO_LOCATE_OBJECT_BY_NAME",
    "SEC_ERROR_PKCS12_UNABLE_TO_EXPORT_KEY",
    "SEC_ERROR_PKCS12_UNABLE_TO_WRITE",
    "SEC_ERROR_PKCS12_UNABLE_TO_READ",
    "SEC_ERROR_PKCS12_KEY_DATABASE_NOT_INITIALIZED",
    "SEC_ERROR_KEYGEN_FAIL",
    "SEC_ERROR_INVALID_PASSWORD",
    "SEC_ERROR_RETRY_OLD_PASSWORD",
    "SEC_ERROR_BAD_NICKNAME",
    "SEC_ERROR_NOT_FORTEZZA_ISSUER",
    "SEC_ERROR_CANNOT_MOVE_SENSITIVE_KEY",
    "SEC_ERROR_JS_INVALID_MODULE_NAME",
    "SEC_ERROR_JS_INVALID_DLL",
    "SEC_ERROR_JS_ADD_MOD_FAILURE",
    "SEC_ERROR_JS_DEL_MOD_FAILURE",
    "SEC_ERROR_OLD_KRL",
    "SEC_ERROR_CKL_CONFLICT",
    "SEC_ERROR_CERT_NOT_IN_NAME_SPACE",
    "SEC_ERROR_KRL_NOT_YET_VALID",
    "SEC_ERROR_CRL_NOT_YET_VALID",
    "SEC_ERROR_UNKNOWN_CERT",
    "SEC_ERROR_UNKNOWN_SIGNER",
    "SEC_ERROR_CERT_BAD_ACCESS_LOCATION",
    "SEC_ERROR_OCSP_UNKNOWN_RESPONSE_TYPE",
    "SEC_ERROR_OCSP_BAD_HTTP_RESPONSE",
    "SEC_ERROR_OCSP_MALFORMED_REQUEST",
    "SEC_ERROR_OCSP_SERVER_ERROR",
    "SEC_ERROR_OCSP_TRY_SERVER_LATER",
    "SEC_ERROR_OCSP_REQUEST_NEEDS_SIG",
    "SEC_ERROR_OCSP_UNAUTHORIZED_REQUEST",
    "SEC_ERROR_OCSP_UNKNOWN_RESPONSE_STATUS",
    "SEC_ERROR_OCSP_UNKNOWN_CERT",
    "SEC_ERROR_OCSP_NOT_ENABLED",
    "SEC_ERROR_OCSP_NO_DEFAULT_RESPONDER",
    "SEC_ERROR_OCSP_MALFORMED_RESPONSE",
    "SEC_ERROR_OCSP_UNAUTHORIZED_RESPONSE",
    "SEC_ERROR_OCSP_FUTURE_RESPONSE",
    "SEC_ERROR_OCSP_OLD_RESPONSE",
    "SEC_ERROR_DIGEST_NOT_FOUND",
    "SEC_ERROR_UNSUPPORTED_MESSAGE_TYPE",
    "SEC_ERROR_MODULE_STUCK",
    "SEC_ERROR_BAD_TEMPLATE",
    "SEC_ERROR_CRL_NOT_FOUND",
    "SEC_ERROR_REUSED_ISSUER_AND_SERIAL",
    "SEC_ERROR_BUSY",
    "SEC_ERROR_EXTRA_INPUT",
    "SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE",
    "SEC_ERROR_UNSUPPORTED_EC_POINT_FORM",
    "SEC_ERROR_UNRECOGNIZED_OID",
    "SEC_ERROR_OCSP_INVALID_SIGNING_CERT",
    "SEC_ERROR_REVOKED_CERTIFICATE_CRL",
    "SEC_ERROR_REVOKED_CERTIFICATE_OCSP",
    "SEC_ERROR_CRL_INVALID_VERSION",
    "SEC_ERROR_CRL_V1_CRITICAL_EXTENSION",
    "SEC_ERROR_CRL_UNKNOWN_CRITICAL_EXTENSION",
    "SEC_ERROR_UNKNOWN_OBJECT_TYPE",
    "SEC_ERROR_INCOMPATIBLE_PKCS11",
    "SEC_ERROR_NO_EVENT",
    "SEC_ERROR_CRL_ALREADY_EXISTS",
    "SEC_ERROR_NOT_INITIALIZED",
    "SEC_ERROR_TOKEN_NOT_LOGGED_IN",
    "SEC_ERROR_OCSP_RESPONDER_CERT_INVALID",
    "SEC_ERROR_OCSP_BAD_SIGNATURE",
    "SEC_ERROR_OUT_OF_SEARCH_LIMITS",
    "SEC_ERROR_INVALID_POLICY_MAPPING",
    "SEC_ERROR_POLICY_VALIDATION_FAILED",
    "SEC_ERROR_UNKNOWN_AIA_LOCATION_TYPE",
    "SEC_ERROR_BAD_HTTP_RESPONSE",
    "SEC_ERROR_BAD_LDAP_RESPONSE",
    "SEC_ERROR_FAILED_TO_ENCODE_DATA",
    "SEC_ERROR_BAD_INFO_ACCESS_LOCATION",
    "SEC_ERROR_LIBPKIX_INTERNAL",
    "SEC_ERROR_PKCS11_GENERAL_ERROR",
    "SEC_ERROR_PKCS11_FUNCTION_FAILED",
    "SEC_ERROR_PKCS11_DEVICE_ERROR",
    "SEC_ERROR_BAD_INFO_ACCESS_METHOD",
    "SEC_ERROR_CRL_IMPORT_FAILED",
    "SEC_ERROR_EXPIRED_PASSWORD",
    "SEC_ERROR_LOCKED_PASSWORD",
    "SEC_ERROR_UNKNOWN_PKCS11_ERROR",
    "SEC_ERROR_BAD_CRL_DP_URL",
    "SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED",
    "SEC_ERROR_LEGACY_DATABASE",
    "SEC_ERROR_APPLICATION_CALLBACK_ERROR",
    "SEC_ERROR_INVALID_STATE",
    "SEC_ERROR_POLICY_LOCKED",
    "SEC_ERROR_SIGNATURE_ALGORITHM_DISABLED",
    "SEC_ERROR_ALGORITHM_MISMATCH",
    "SEC_ERROR_CERTIFICATE_COMPRESSION_ALGORITHM_NOT_SUPPORTED",
    "SEC_ERROR_UNEXPECTED_COMPRESSED_CERTIFICATE",
    "MOZILLA_PKIX_ERROR_KEY_PINNING_FAILURE",
    "MOZILLA_PKIX_ERROR_CA_CERT_USED_AS_END_ENTITY",
    "MOZILLA_PKIX_ERROR_INADEQUATE_KEY_SIZE",
    "MOZILLA_PKIX_ERROR_V1_CERT_USED_AS_CA",
    "MOZILLA_PKIX_ERROR_NO_RFC822NAME_MATCH",
    "MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE",
    "MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE",
    "MOZILLA_PKIX_ERROR_SIGNATURE_ALGORITHM_MISMATCH",
    "MOZILLA_PKIX_ERROR_OCSP_RESPONSE_FOR_CERT_MISSING",
    "MOZILLA_PKIX_ERROR_VALIDITY_TOO_LONG",
    "MOZILLA_PKIX_ERROR_REQUIRED_TLS_FEATURE_MISSING",
    "MOZILLA_PKIX_ERROR_INVALID_INTEGER_ENCODING",
    "MOZILLA_PKIX_ERROR_EMPTY_ISSUER_NAME",
    "MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED",
    "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT",
    "MOZILLA_PKIX_ERROR_MITM_DETECTED",
    "MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY",
    "MOZILLA_PKIX_ERROR_ISSUER_NO_LONGER_TRUSTED",
    "__other__",
  ],

  _baseline: null,
  _fogInitialized: false,

  /**
   * Gathers the current values of the Glean dual labeled counter
   * metric tls_handshake.result where the key is the given flavor.
   */
  resultMetricValues(flavor) {
    let metrics = { values: {} };
    let key;
    switch (flavor) {
      case "":
        key = "all";
        break;
      case "_FIRST_TRY":
        key = "first_try";
        break;
      case "_CONSERVATIVE":
        key = "conservative";
        break;
      case "_ECH":
        key = "ech";
        break;
      case "_ECH_GREASE":
        key = "ech_grease";
        break;
      default:
        Assert.ok(false, `Unknown result flavor: ${flavor}`);
        return null;
    }
    for (let category of this.RESULTS) {
      metrics.values[category] = Glean.tlsHandshake.result
        .get(key, category)
        .testGetValue();
    }
    return metrics;
  },

  /**
   * Returns the Glean ssl.time_until_ready_* metric for a given flavor.
   */
  timingMetric(flavor) {
    switch (flavor) {
      case "":
        return Glean.ssl.timeUntilReady;
      case "_FIRST_TRY":
        return Glean.ssl.timeUntilReadyFirstTry;
      case "_CONSERVATIVE":
        return Glean.ssl.timeUntilReadyConservative;
      case "_ECH":
        return Glean.ssl.timeUntilReadyEch;
      case "_ECH_GREASE":
        return Glean.ssl.timeUntilReadyEchGrease;
      default:
        Assert.ok(false, `Unknown timing flavor: ${flavor}`);
        return null;
    }
  },

  /**
   * Computes the per-bucket difference between two distribution snapshots.
   * Returns a values object containing only non-zero deltas.
   */
  _deltaValues(current, baseline) {
    let result = {};
    let currentValues = current?.values ?? {};
    let baselineValues = baseline?.values ?? {};
    let allKeys = new Set([
      ...Object.keys(currentValues),
      ...Object.keys(baselineValues),
    ]);
    for (let k of allKeys) {
      let delta = (currentValues[k] ?? 0) - (baselineValues[k] ?? 0);
      if (delta !== 0) {
        result[k] = delta;
      }
    }
    return result;
  },

  /**
   * Returns a delta snapshot for a result metric since the last
   * resetHistograms() call. The returned object has a .values property
   * compatible with assertHistogramMap.
   */
  resultDelta(flavor) {
    let current = this.resultMetricValues(flavor);
    let baseline = this._baseline?.[`result${flavor}`];
    return { values: this._deltaValues(current, baseline) };
  },

  /**
   * Returns the delta sample count for a timing metric since the last
   * resetHistograms() call.
   */
  timingDeltaCount(flavor) {
    let current = this.timingMetric(flavor).testGetValue();
    let baseline = this._baseline?.[`timing${flavor}`];
    return (current?.count ?? 0) - (baseline?.count ?? 0);
  },

  /**
   * Assert that the distribution values match expected entries exactly.
   * All non-expected bucket entries must be zero.
   *
   * @param {object} snapshot A distribution snapshot with a .values property.
   * @param {Map} expectedEntries Map of bucket index (string) to expected count.
   */
  assertHistogramMap(snapshot, expectedEntries) {
    let values = JSON.parse(JSON.stringify(snapshot));
    for (let [Tk, Tv] of expectedEntries.entries()) {
      let found = false;
      for (let [i, val] of Object.entries(values.values)) {
        if (i == Tk) {
          found = true;
          Assert.equal(val, Tv, `expected counts should match at index ${i}`);
          values.values[i] = 0; // Reset the value
        }
      }
      Assert.ok(found, `Should have found an entry at index ${Tk}`);
    }
    for (let k in values.values) {
      Assert.equal(
        values.values[k],
        0,
        `Should NOT have found an entry at index ${k} of value ${values.values[k]}`
      );
    }
  },

  /**
   * Snapshots current metric values as a baseline. Subsequent check methods
   * compare against this baseline, measuring only what changed.
   */
  resetHistograms() {
    if (!this._fogInitialized) {
      Services.fog.initializeFOG();
      this._fogInitialized = true;
    }
    info("Snapshotting TLS handshake metric baseline");
    this._baseline = {};
    for (let f of this.FLAVORS) {
      this._baseline[`result${f}`] = this.resultMetricValues(f);
      this._baseline[`timing${f}`] = this.timingMetric(f).testGetValue();
    }
  },

  /**
   * Checks that all TLS Handshake metrics of a particular flavor have
   * exactly resultCount new entries for the resultCode and no other new
   * entries since the last resetHistograms() call.
   *
   * @param {Array} flavors An array of strings corresponding to which types
   *                        of metrics should have entries. See
   *                        HandshakeTelemetryHelpers.FLAVORS.
   * @param {string} resultString The expected result string, see sslerr.h.
   *                              "Success" is success, all others are errors.
   * @param {number} resultCount The number of handshake results expected.
   */
  checkEntry(flavors, resultString, resultCount) {
    for (let f of flavors) {
      let delta = this.resultDelta(f);
      info(
        `checkEntry: result${f} expecting ${resultCount}x result ${resultString}, ` +
          `got delta: ${JSON.stringify(delta.values)}`
      );
      this.assertHistogramMap(delta, new Map([[resultString, resultCount]]));
    }

    // Timing metrics should only contain values if we expected success.
    for (let f of flavors) {
      let deltaCount = this.timingDeltaCount(f);
      let expectedCount = resultString === "Success" ? resultCount : 0;
      info(
        `checkEntry: timing${f} expecting delta count=${expectedCount}, ` +
          `got delta count=${deltaCount}`
      );
      Assert.strictEqual(
        deltaCount,
        expectedCount,
        resultString === "Success"
          ? "Timing entry count correct"
          : "No timing entries expected"
      );
    }
  },

  checkSuccess(flavors, resultCount = 1) {
    this.checkEntry(flavors, "Success", resultCount);
  },

  checkEmpty(flavors) {
    for (let f of flavors) {
      let resultDelta = this.resultDelta(f);
      let resultDeltaCount = Object.values(resultDelta.values).reduce(
        (a, b) => a + b,
        0
      );
      info(
        `checkEmpty: result${f} expecting no new entries, ` +
          `got delta count=${resultDeltaCount}`
      );
      Assert.strictEqual(
        resultDeltaCount,
        0,
        `No new result entries expected for '${f}'. Delta: ${JSON.stringify(
          resultDelta.values
        )}`
      );
      let timingDeltaCount = this.timingDeltaCount(f);
      info(
        `checkEmpty: timing${f} expecting no new entries, ` +
          `got delta count=${timingDeltaCount}`
      );
      Assert.strictEqual(
        timingDeltaCount,
        0,
        `No new timing entries expected for '${f}'.`
      );
    }
  },
};

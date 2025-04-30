/* this source code form is subject to the terms of the mozilla public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A mapping of error message names to external documentation. Any error message
 * included here will be displayed alongside its link in the web console.
 */

"use strict";

// Worker contexts do not support Services; in that case we have to rely
// on the support URL redirection.

loader.lazyGetter(this, "supportBaseURL", () => {
  // Fallback URL used for worker targets, as well as when app.support.baseURL
  // cannot be formatted.
  let url = "https://support.mozilla.org/kb/";

  if (!isWorker) {
    try {
      // formatURLPref might throw if tokens used in app.support.baseURL
      // are not available for the current binary. See Bug 1755626.
      url = Services.urlFormatter.formatURLPref("app.support.baseURL");
    } catch (e) {
      console.warn(
        `Failed to format app.support.baseURL, falling back to ${url} (${e.message})`
      );
    }
  }
  return url;
});

const baseErrorURL =
  "https://developer.mozilla.org/docs/Web/JavaScript/Reference/Errors/";
const params =
  "?utm_source=mozilla&utm_medium=firefox-console-errors&utm_campaign=default";

const ErrorDocs = {
  JSMSG_READ_ONLY: "Read-only",
  JSMSG_BAD_ARRAY_LENGTH: "Invalid_array_length",
  JSMSG_NEGATIVE_REPETITION_COUNT: "Negative_repetition_count",
  JSMSG_RESULTING_STRING_TOO_LARGE: "Resulting_string_too_large",
  JSMSG_BAD_RADIX: "Bad_radix",
  JSMSG_PRECISION_RANGE: "Precision_range",
  JSMSG_STMT_AFTER_RETURN: "Stmt_after_return",
  JSMSG_NOT_A_CODEPOINT: "Not_a_codepoint",
  JSMSG_BAD_SORT_ARG: "Array_sort_argument",
  JSMSG_UNEXPECTED_TYPE: "Unexpected_type",
  JSMSG_NOT_DEFINED: "Not_defined",
  JSMSG_NOT_FUNCTION: "Not_a_function",
  JSMSG_EQUAL_AS_ASSIGN: "Equal_as_assign",
  JSMSG_UNDEFINED_PROP: "Undefined_prop",
  JSMSG_DEPRECATED_PRAGMA: "Deprecated_source_map_pragma",
  JSMSG_DEPRECATED_USAGE: "Deprecated_caller_or_arguments_usage",
  JSMSG_CANT_DELETE: "Cant_delete",
  JSMSG_VAR_HIDES_ARG: "Var_hides_argument",
  JSMSG_JSON_BAD_PARSE: "JSON_bad_parse",
  JSMSG_UNDECLARED_VAR: "Undeclared_var",
  JSMSG_UNEXPECTED_TOKEN: "Unexpected_token",
  JSMSG_BAD_OCTAL: "Bad_octal",
  JSMSG_PROPERTY_ACCESS_DENIED: "Property_access_denied",
  JSMSG_NO_PROPERTIES: "No_properties",
  JSMSG_ALREADY_HAS_PRAGMA: "Already_has_pragma",
  JSMSG_BAD_RETURN_OR_YIELD: "Bad_return_or_yield",
  JSMSG_UNEXPECTED_TOKEN_NO_EXPECT: "Missing_semicolon_before_statement",
  JSMSG_OVER_RECURSED: "Too_much_recursion",
  JSMSG_BRACKET_AFTER_LIST: "Missing_bracket_after_list",
  JSMSG_PAREN_AFTER_ARGS: "Missing_parenthesis_after_argument_list",
  JSMSG_MORE_ARGS_NEEDED: "More_arguments_needed",
  JSMSG_BAD_LEFTSIDE_OF_ASS: "Invalid_assignment_left-hand_side",
  JSMSG_UNTERMINATED_STRING: "Unterminated_string_literal",
  JSMSG_NOT_CONSTRUCTOR: "Not_a_constructor",
  JSMSG_CURLY_AFTER_LIST: "Missing_curly_after_property_list",
  JSMSG_DEPRECATED_FOR_EACH: "For-each-in_loops_are_deprecated",
  JSMSG_STRICT_NON_SIMPLE_PARAMS: "Strict_Non_Simple_Params",
  JSMSG_DEAD_OBJECT: "Dead_object",
  JSMSG_OBJECT_REQUIRED: "No_non-null_object",
  JSMSG_IDSTART_AFTER_NUMBER: "Identifier_after_number",
  JSMSG_DEPRECATED_EXPR_CLOSURE: "Deprecated_expression_closures",
  JSMSG_ILLEGAL_CHARACTER: "Illegal_character",
  JSMSG_BAD_REGEXP_FLAG: "Bad_regexp_flag",
  JSMSG_INVALID_FOR_IN_DECL_WITH_INIT: "Invalid_for-in_initializer",
  JSMSG_CANT_REDEFINE_PROP: "Cant_redefine_property",
  JSMSG_COLON_AFTER_ID: "Missing_colon_after_property_id",
  JSMSG_IN_NOT_OBJECT: "in_operator_no_object",
  JSMSG_CURLY_AFTER_BODY: "Missing_curly_after_function_body",
  JSMSG_NAME_AFTER_DOT: "Missing_name_after_dot_operator",
  JSMSG_DEPRECATED_OCTAL: "Deprecated_octal",
  JSMSG_PAREN_AFTER_COND: "Missing_parenthesis_after_condition",
  JSMSG_JSON_CYCLIC_VALUE: "Cyclic_object_value",
  JSMSG_NO_VARIABLE_NAME: "No_variable_name",
  JSMSG_UNNAMED_FUNCTION_STMT: "Unnamed_function_statement",
  JSMSG_CANT_DEFINE_PROP_OBJECT_NOT_EXTENSIBLE:
    "Cant_define_property_object_not_extensible",
  JSMSG_TYPED_ARRAY_BAD_ARGS: "Typed_array_invalid_arguments",
  JSMSG_GETTER_ONLY: "Getter_only",
  JSMSG_INVALID_DATE: "Invalid_date",
  JSMSG_DEPRECATED_STRING_METHOD: "Deprecated_String_generics",
  JSMSG_RESERVED_ID: "Reserved_identifier",
  JSMSG_BAD_CONST_ASSIGN: "Invalid_const_assignment",
  JSMSG_BAD_CONST_DECL: "Missing_initializer_in_const",
  JSMSG_OF_AFTER_FOR_LOOP_DECL: "Invalid_for-of_initializer",
  JSMSG_BAD_URI: "Malformed_URI",
  JSMSG_DEPRECATED_DELETE_OPERAND: "Delete_in_strict_mode",
  JSMSG_MISSING_FORMAL: "Missing_formal_parameter",
  JSMSG_CANT_TRUNCATE_ARRAY: "Non_configurable_array_element",
  JSMSG_INCOMPATIBLE_PROTO: "Called_on_incompatible_type",
  JSMSG_INCOMPATIBLE_METHOD: "Called_on_incompatible_type",
  JSMSG_BAD_INSTANCEOF_RHS: "invalid_right_hand_side_instanceof_operand",
  JSMSG_EMPTY_ARRAY_REDUCE: "Reduce_of_empty_array_with_no_initial_value",
  JSMSG_NOT_ITERABLE: "is_not_iterable",
  JSMSG_PROPERTY_FAIL: "cant_access_property",
  JSMSG_PROPERTY_FAIL_EXPR: "cant_access_property",
  JSMSG_REDECLARED_VAR: "Redeclared_parameter",
  JSMSG_MISMATCHED_PLACEMENT: "Mismatched placement",
  JSMSG_SET_NON_OBJECT_RECEIVER: "Cant_assign_to_property",
};

const MIXED_CONTENT_LEARN_MORE =
  "https://developer.mozilla.org/docs/Web/Security/Mixed_content";
const TRACKING_PROTECTION_LEARN_MORE =
  "https://developer.mozilla.org/Firefox/Privacy/Guides/Tracking_Protection";
const INSECURE_PASSWORDS_LEARN_MORE =
  "https://developer.mozilla.org/docs/Web/Security/Insecure_passwords";
const PUBLIC_KEY_PINS_LEARN_MORE =
  "https://developer.mozilla.org/docs/Web/Security/Certificate_Transparency";
const STRICT_TRANSPORT_SECURITY_LEARN_MORE =
  "https://developer.mozilla.org/docs/Web/HTTP/Reference/Headers/Strict-Transport-Security";
const MIME_TYPE_MISMATCH_LEARN_MORE =
  "https://developer.mozilla.org/docs/Web/HTTP/Reference/Headers/X-Content-Type-Options";
const SOURCE_MAP_LEARN_MORE =
  "https://firefox-source-docs.mozilla.org/devtools-user/debugger/source_map_errors/";
const TLS_LEARN_MORE =
  "https://blog.mozilla.org/security/2018/10/15/removing-old-versions-of-tls/";
const X_FRAME_OPTIONS_LEARN_MORE =
  "https://developer.mozilla.org/docs/Web/HTTP/Reference/Headers/X-Frame-Options";
const REQUEST_STORAGE_ACCESS_LEARN_MORE =
  "https://developer.mozilla.org/docs/Web/API/Document/requestStorageAccess";
const DOCTYPE_MODES_LEARN_MORE =
  "https://developer.mozilla.org/docs/Web/HTML/Guides/Quirks_Mode_and_Standards_Mode";
const BOUNCE_TRACKING_PROTECTION_LEARN_MORE =
  "https://firefox-source-docs.mozilla.org/toolkit/components/antitracking/anti-tracking/bounce-tracking-protection/index.html";

const ErrorCategories = {
  "X-Frame-Options": X_FRAME_OPTIONS_LEARN_MORE,
  "Insecure Password Field": INSECURE_PASSWORDS_LEARN_MORE,
  "Mixed Content Message": MIXED_CONTENT_LEARN_MORE,
  "Mixed Content Blocker": MIXED_CONTENT_LEARN_MORE,
  "Invalid HPKP Headers": PUBLIC_KEY_PINS_LEARN_MORE,
  "Invalid HSTS Headers": STRICT_TRANSPORT_SECURITY_LEARN_MORE,
  "Tracking Protection": TRACKING_PROTECTION_LEARN_MORE,
  MIMEMISMATCH: MIME_TYPE_MISMATCH_LEARN_MORE,
  "source map": SOURCE_MAP_LEARN_MORE,
  TLS: TLS_LEARN_MORE,
  requestStorageAccess: REQUEST_STORAGE_ACCESS_LEARN_MORE,
  HTTPSOnly: supportBaseURL + "https-only-prefs",
  HTTPSFirst: supportBaseURL + "https-first",
  HTML_PARSER__DOCTYPE: DOCTYPE_MODES_LEARN_MORE,
  bounceTrackingProtection: BOUNCE_TRACKING_PROTECTION_LEARN_MORE,
};

const baseCorsErrorUrl =
  "https://developer.mozilla.org/docs/Web/HTTP/Guides/CORS/Errors/";
const corsParams =
  "?utm_source=devtools&utm_medium=firefox-cors-errors&utm_campaign=default";
const CorsErrorDocs = {
  CORSDisabled: "CORSDisabled",
  CORSDidNotSucceed2: "CORSDidNotSucceed",
  CORSOriginHeaderNotAdded: "CORSOriginHeaderNotAdded",
  CORSExternalRedirectNotAllowed: "CORSExternalRedirectNotAllowed",
  CORSRequestNotHttp: "CORSRequestNotHttp",
  CORSMissingAllowOrigin2: "CORSMissingAllowOrigin",
  CORSMultipleAllowOriginNotAllowed: "CORSMultipleAllowOriginNotAllowed",
  CORSAllowOriginNotMatchingOrigin: "CORSAllowOriginNotMatchingOrigin",
  CORSNotSupportingCredentials: "CORSNotSupportingCredentials",
  CORSMethodNotFound: "CORSMethodNotFound",
  CORSMissingAllowCredentials: "CORSMissingAllowCredentials",
  CORSPreflightDidNotSucceed3: "CORSPreflightDidNotSucceed",
  CORSInvalidAllowMethod: "CORSInvalidAllowMethod",
  CORSInvalidAllowHeader: "CORSInvalidAllowHeader",
  CORSMissingAllowHeaderFromPreflight2: "CORSMissingAllowHeaderFromPreflight",
};

const baseStorageAccessPolicyErrorUrl =
  "https://developer.mozilla.org/docs/Web/Privacy/Guides/Storage_Access_Policy/Errors/";
const storageAccessPolicyParams =
  "?utm_source=devtools&utm_medium=firefox-cookie-errors&utm_campaign=default";
const StorageAccessPolicyErrorDocs = {
  cookieBlockedPermission: "CookieBlockedByPermission",
  cookieBlockedTracker: "CookieBlockedTracker",
  cookieBlockedAll: "CookieBlockedAll",
  cookieBlockedForeign: "CookieBlockedForeign",
  cookiePartitionedForeign: "CookiePartitionedForeign",
};

exports.GetURL = error => {
  if (!error) {
    return undefined;
  }

  const doc = ErrorDocs[error.errorMessageName];
  if (doc) {
    return baseErrorURL + doc + params;
  }

  const corsDoc = CorsErrorDocs[error.category];
  if (corsDoc) {
    return baseCorsErrorUrl + corsDoc + corsParams;
  }

  const storageAccessPolicyDoc = StorageAccessPolicyErrorDocs[error.category];
  if (storageAccessPolicyDoc) {
    return (
      baseStorageAccessPolicyErrorUrl +
      storageAccessPolicyDoc +
      storageAccessPolicyParams
    );
  }

  const categoryURL = ErrorCategories[error.category];
  if (categoryURL) {
    return categoryURL + params;
  }
  return undefined;
};

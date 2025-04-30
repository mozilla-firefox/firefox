/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

// TODO(Bug 1803370): allow extension to exceed the GUARANTEED_MINIMUM_STATIC_RULES limit.
//
// The maximum number of static rules exceeding the per-extension
// GUARANTEED_MINIMUM_STATIC_RULES across every extensions.
//
// const MAX_GLOBAL_NUMBER_OF_STATIC_RULES = 300000;

const lazy = XPCOMUtils.declareLazy({
  GUARANTEED_MINIMUM_STATIC_RULES: {
    pref: "extensions.dnr.guaranteed_minimum_static_rules",
    default: 30000,
  },
  MAX_NUMBER_OF_STATIC_RULESETS: {
    pref: "extensions.dnr.max_number_of_static_rulesets",
    default: 100,
  },
  MAX_NUMBER_OF_ENABLED_STATIC_RULESETS: {
    pref: "extensions.dnr.max_number_of_enabled_static_rulesets",
    default: 20,
  },
  MAX_NUMBER_OF_DISABLED_STATIC_RULES: {
    pref: "extensions.dnr.max_number_of_disabled_static_rules",
    default: 5000,
  },
  /**
   * NOTE: this limit may be increased in the future, see
   * https://bugzilla.mozilla.org/show_bug.cgi?id=1894119
   */
  MAX_NUMBER_OF_DYNAMIC_RULES: {
    pref: "extensions.dnr.max_number_of_dynamic_rules",
    default: 5000,
  },
  MAX_NUMBER_OF_SESSION_RULES: {
    pref: "extensions.dnr.max_number_of_session_rules",
    default: 5000,
  },
  // TODO bug 1821033: Bump limit after optimizing regexFilter.
  MAX_NUMBER_OF_REGEX_RULES: {
    pref: "extensions.dnr.max_number_of_regex_rules",
    default: 1000,
  },
});

export const ExtensionDNRLimits = {
  /**
   * The minimum number of static rules guaranteed to an extension across its
   * enabled static rulesets. Any rules above this limit will count towards the
   * global static rule limit.
   */
  get GUARANTEED_MINIMUM_STATIC_RULES() {
    return lazy.GUARANTEED_MINIMUM_STATIC_RULES;
  },

  /**
   * The maximum number of static Rulesets an extension can specify as part of
   * the "rule_resources" manifest key.
   */
  get MAX_NUMBER_OF_STATIC_RULESETS() {
    return lazy.MAX_NUMBER_OF_STATIC_RULESETS;
  },

  /**
   * The maximum number of static Rulesets an extension can enable at any one
   * time.
   */
  get MAX_NUMBER_OF_ENABLED_STATIC_RULESETS() {
    return lazy.MAX_NUMBER_OF_ENABLED_STATIC_RULESETS;
  },

  /**
   * The maximum number of static rules that can be disabled on each static
   * ruleset.
   */
  get MAX_NUMBER_OF_DISABLED_STATIC_RULES() {
    return lazy.MAX_NUMBER_OF_DISABLED_STATIC_RULES;
  },

  /**
   * The maximum number of dynamic rules an extension can add.
   */
  get MAX_NUMBER_OF_DYNAMIC_RULES() {
    return lazy.MAX_NUMBER_OF_DYNAMIC_RULES;
  },

  /**
   * The maximum number of session rules an extension can add.
   */
  get MAX_NUMBER_OF_SESSION_RULES() {
    return lazy.MAX_NUMBER_OF_SESSION_RULES;
  },

  /**
   * The maximum number of regular expression rules that an extension can add.
   * Session, dynamic and static rules have their own quota.
   */
  get MAX_NUMBER_OF_REGEX_RULES() {
    return lazy.MAX_NUMBER_OF_REGEX_RULES;
  },
};

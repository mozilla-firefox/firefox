/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

// Each extension that uses DNR has one RuleManager. All registered RuleManagers
// are checked whenever a network request occurs. Individual extensions may
// occasionally modify their rules (e.g. via the updateSessionRules API).
const gRuleManagers = [];

/**
 * Whenever a request occurs, the rules of each RuleManager are matched against
 * the request to determine the final action to take. The RequestEvaluator class
 * is responsible for evaluating rules, and its behavior is described below.
 *
 * Short version:
 * Find the highest-priority rule that matches the given request. If the
 * request is not canceled, all matching allowAllRequests and modifyHeaders
 * actions are returned.
 *
 * Longer version:
 * Unless stated otherwise, the explanation below describes the behavior within
 * an extension.
 * An extension can specify rules, optionally in multiple rulesets. The ability
 * to have multiple ruleset exists to support bulk updates of rules. Rulesets
 * are NOT independent - rules from different rulesets can affect each other.
 *
 * When multiple rules match, the order between rules are defined as follows:
 * - Ruleset precedence: session > dynamic > static (order from manifest.json).
 * - Rules in ruleset precedence: ordered by rule.id, lowest (numeric) ID first.
 * - Across all rules+rulesets: highest rule.priority (default 1) first,
 *                              action precedence if rule priority are the same.
 *
 * The primary documented way for extensions to describe precedence is by
 * specifying rule.priority. Between same-priority rules, their precedence is
 * dependent on the rule action. The ruleset/rule ID precedence is only used to
 * have a defined ordering if multiple rules have the same priority+action.
 *
 * Rule actions have the following order of precedence and meaning:
 * - "allow" can be used to ignore other same-or-lower-priority rules.
 * - "allowAllRequests" (for main_frame / sub_frame resourceTypes only) has the
 *      same effect as allow, but also applies to (future) subresource loads in
 *      the document (including descendant frames) generated from the request.
 * - "block" cancels the matched request.
 * - "upgradeScheme" upgrades the scheme of the request.
 * - "redirect" redirects the request.
 * - "modifyHeaders" rewrites request/response headers.
 *
 * The matched rules are evaluated in two passes:
 * 1. findMatchingRules():
 *    Find the highest-priority rule(s), and choose the action with the highest
 *    precedence (across all rulesets, any action except modifyHeaders).
 *    This also accounts for any allowAllRequests from an ancestor frame.
 *
 * 2. getMatchingModifyHeadersRules():
 *    Find matching rules with the "modifyHeaders" action, minus ignored rules.
 *    Reaching this step implies that the request was not canceled, so either
 *    the first step did not yield a rule, or the rule action is "allow" or
 *    "allowAllRequests" (i.e. ignore same-or-lower-priority rules).
 *
 * If an extension does not have sufficient permissions for the action, the
 * resulting action is ignored.
 *
 * The above describes the evaluation within one extension. When a sequence of
 * (multiple) extensions is given, they may return conflicting actions in the
 * first pass. This is resolved by choosing the action with the following order
 * of precedence, in RequestEvaluator.evaluateRequest():
 *  - block
 *  - redirect / upgradeScheme
 *  - allow / allowAllRequests
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionDNRLimits: "resource://gre/modules/ExtensionDNRLimits.sys.mjs",
  ExtensionDNRStore: "resource://gre/modules/ExtensionDNRStore.sys.mjs",
  WebRequest: "resource://gre/modules/WebRequest.sys.mjs",

  gMatchRequestsFromOtherExtensions: {
    pref: "extensions.dnr.match_requests_from_other_extensions",
    default: false,
  },
});

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const { DefaultWeakMap, ExtensionError } = ExtensionUtils;

// As documented above:
// Ruleset precedence: session > dynamic > static (order from manifest.json).
const PRECEDENCE_SESSION_RULESET = 1;
const PRECEDENCE_DYNAMIC_RULESET = 2;
const PRECEDENCE_STATIC_RULESETS_BASE = 3;

// The RuleCondition class represents a rule's "condition" type as described in
// schemas/declarative_net_request.json. This class exists to allow the JS
// engine to use one Shape for all Rule instances.
class RuleCondition {
  #compiledUrlFilter;
  #compiledRegexFilter;

  constructor(cond) {
    this.urlFilter = cond.urlFilter;
    this.regexFilter = cond.regexFilter;
    this.isUrlFilterCaseSensitive = cond.isUrlFilterCaseSensitive;
    this.initiatorDomains = cond.initiatorDomains;
    this.excludedInitiatorDomains = cond.excludedInitiatorDomains;
    this.requestDomains = cond.requestDomains;
    this.excludedRequestDomains = cond.excludedRequestDomains;
    this.resourceTypes = cond.resourceTypes;
    this.excludedResourceTypes = cond.excludedResourceTypes;
    this.requestMethods = cond.requestMethods;
    this.excludedRequestMethods = cond.excludedRequestMethods;
    this.domainType = cond.domainType;
    this.tabIds = cond.tabIds;
    this.excludedTabIds = cond.excludedTabIds;
  }

  // See CompiledUrlFilter for documentation.
  urlFilterMatches(requestDataForUrlFilter) {
    if (!this.#compiledUrlFilter) {
      // eslint-disable-next-line no-use-before-define
      this.#compiledUrlFilter = new CompiledUrlFilter(
        this.urlFilter,
        this.isUrlFilterCaseSensitive
      );
    }
    return this.#compiledUrlFilter.matchesRequest(requestDataForUrlFilter);
  }

  // Used for testing regexFilter matches in RuleEvaluator.#matchRuleCondition
  // and to get redirect URL from regexSubstitution in applyRegexSubstitution.
  getCompiledRegexFilter() {
    return this.#compiledRegexFilter;
  }

  // RuleValidator compiles regexFilter before this Rule class is instantiated.
  // To avoid unnecessarily compiling it again, the result is assigned here.
  setCompiledRegexFilter(compiledRegexFilter) {
    this.#compiledRegexFilter = compiledRegexFilter;
  }
}

export class Rule {
  constructor(rule) {
    this.id = rule.id;
    this.priority = rule.priority;
    this.condition = new RuleCondition(rule.condition);
    this.action = rule.action;
  }

  // The precedence of rules within an extension. This method is frequently
  // used during the first pass of the RequestEvaluator.
  actionPrecedence() {
    switch (this.action.type) {
      case "allow":
        return 1; // Highest precedence.
      case "allowAllRequests":
        return 2;
      case "block":
        return 3;
      case "upgradeScheme":
        return 4;
      case "redirect":
        return 5;
      case "modifyHeaders":
        return 6;
      default:
        throw new Error(`Unexpected action type: ${this.action.type}`);
    }
  }

  isAllowOrAllowAllRequestsAction() {
    const type = this.action.type;
    return type === "allow" || type === "allowAllRequests";
  }
}

class Ruleset {
  /**
   * @typedef {number} integer
   *
   * @param {string} rulesetId - extension-defined ruleset ID.
   * @param {integer} rulesetPrecedence
   * @param {Rule[]} rules - extension-defined rules
   * @param {Set<Rule> | null} disabledRuleIds - An optional set of disabled rule ids
   * @param {RuleManager} ruleManager - owner of this ruleset.
   */
  constructor(
    rulesetId,
    rulesetPrecedence,
    rules,
    disabledRuleIds,
    ruleManager
  ) {
    this.id = rulesetId;
    this.rulesetPrecedence = rulesetPrecedence;
    this.rules = rules;
    this.disabledRuleIds = disabledRuleIds;
    // For use by MatchedRule.
    this.ruleManager = ruleManager;
  }
}

/**
 * @param {string} uriQuery - The query of a nsIURI to transform.
 * @param {object} queryTransform - The value of the
 *   Rule.action.redirect.transform.queryTransform property as defined in
 *   declarative_net_request.json.
 * @returns {string} The uriQuery with the queryTransform applied to it.
 */
function applyQueryTransform(uriQuery, queryTransform) {
  // URLSearchParams cannot be applied to the full query string, because that
  // API formats the full query string using form-urlencoding. But the input
  // may be in a different format. So we try to only modify matched params.

  function urlencode(s) {
    // Encode in application/x-www-form-urlencoded format.
    // The only JS API to do that is URLSearchParams. encodeURIComponent is not
    // the same, it differs in how it handles " " ("%20") and "!'()~" (raw).
    // But urlencoded space should be "+" and the latter be "%21%27%28%29%7E".
    return new URLSearchParams({ s }).toString().slice(2);
  }
  if (!uriQuery.length && !queryTransform.addOrReplaceParams) {
    // Nothing to do.
    return "";
  }
  const removeParamsSet = new Set(queryTransform.removeParams?.map(urlencode));
  const addParams = (queryTransform.addOrReplaceParams || []).map(orig => ({
    normalizedKey: urlencode(orig.key),
    orig,
  }));
  const finalParams = [];
  if (uriQuery.length) {
    for (let part of uriQuery.split("&")) {
      let key = part.split("=", 1)[0];
      if (removeParamsSet.has(key)) {
        continue;
      }
      let i = addParams.findIndex(p => p.normalizedKey === key);
      if (i !== -1) {
        // Replace found param with the key-value from addOrReplaceParams.
        finalParams.push(`${key}=${urlencode(addParams[i].orig.value)}`);
        // Omit param so that a future search for the same key can find the next
        // specified key-value pair, if any. And to prevent the already-used
        // key-value pairs from being appended after the loop.
        addParams.splice(i, 1);
      } else {
        finalParams.push(part);
      }
    }
  }
  // Append remaining, unused key-value pairs.
  for (let { normalizedKey, orig } of addParams) {
    if (!orig.replaceOnly) {
      finalParams.push(`${normalizedKey}=${urlencode(orig.value)}`);
    }
  }
  return finalParams.length ? `?${finalParams.join("&")}` : "";
}

/**
 * @param {nsIURI} uri - Usually a http(s) URL.
 * @param {object} transform - The value of the Rule.action.redirect.transform
 *   property as defined in declarative_net_request.json.
 * @returns {nsIURI} uri - The new URL.
 * @throws if the transformation is invalid.
 */
function applyURLTransform(uri, transform) {
  let mut = uri.mutate();
  if (transform.scheme) {
    // Note: declarative_net_request.json only allows http(s)/moz-extension:.
    mut.setScheme(transform.scheme);
    if (uri.port !== -1 || transform.port) {
      // If the URI contains a port or transform.port was specified, the default
      // port is significant. So we must set it in that case.
      if (transform.scheme === "https") {
        mut.QueryInterface(Ci.nsIStandardURLMutator).setDefaultPort(443);
      } else if (transform.scheme === "http") {
        mut.QueryInterface(Ci.nsIStandardURLMutator).setDefaultPort(80);
      }
    }
  }
  if (transform.username != null) {
    mut.setUsername(transform.username);
  }
  if (transform.password != null) {
    mut.setPassword(transform.password);
  }
  if (transform.host != null) {
    mut.setHost(transform.host);
  }
  if (transform.port != null) {
    // The caller ensures that transform.port is a string consisting of digits
    // only. When it is an empty string, it should be cleared (-1).
    mut.setPort(transform.port || -1);
  }
  if (transform.path != null) {
    mut.setFilePath(transform.path);
  }
  if (transform.query != null) {
    mut.setQuery(transform.query);
  } else if (transform.queryTransform) {
    mut.setQuery(applyQueryTransform(uri.query, transform.queryTransform));
  }
  if (transform.fragment != null) {
    mut.setRef(transform.fragment);
  }
  return mut.finalize();
}

/**
 * @param {nsIURI} uri - Usually a http(s) URL.
 * @param {MatchedRule} matchedRule - The matched rule with a regexFilter
 *   condition and regexSubstitution action.
 * @returns {nsIURI} The new URL derived from the regexSubstitution combined
 *   with capturing group from regexFilter applied to the input uri.
 * @throws if the resulting URL is an invalid redirect target.
 */
function applyRegexSubstitution(uri, matchedRule) {
  const rule = matchedRule.rule;
  const extension = matchedRule.ruleManager.extension;
  const regexSubstitution = rule.action.redirect.regexSubstitution;
  const compiledRegexFilter = rule.condition.getCompiledRegexFilter();
  // This method being called implies that regexFilter matched, so |matches| is
  // always non-null, i.e. an array of string/undefined values.
  const matches = compiledRegexFilter.exec(uri.spec);

  let redirectUrl = regexSubstitution.replace(/\\(.)/g, (_, char) => {
    // #checkActionRedirect ensures that every \ is followed by a \ or digit.
    return char === "\\" ? char : (matches[char] ?? "");
  });

  // Throws if the URL is invalid:
  let redirectUri;
  try {
    redirectUri = Services.io.newURI(redirectUrl);
  } catch (e) {
    throw new Error(
      `Extension ${extension.id} tried to redirect to an invalid URL: ${redirectUrl}`
    );
  }
  if (!extension.checkLoadURI(redirectUri, { dontReportErrors: true })) {
    throw new Error(
      `Extension ${extension.id} may not redirect to: ${redirectUrl}`
    );
  }
  return redirectUri;
}

/**
 * An urlFilter is a string pattern to match a canonical http(s) URL.
 * urlFilter matches anywhere in the string, unless an anchor is present:
 * - ||... ("Domain name anchor") - domain or subdomain starts with ...
 * - |... ("Left anchor") - URL starts with ...
 * - ...| ("Right anchor") - URL ends with ...
 *
 * Other than the anchors, the following special characters exist:
 * - ^ = end of URL, or any char except: alphanum _ - . % ("Separator")
 * - * = any number of characters ("Wildcard")
 *
 * Ambiguous cases (undocumented but actual Chrome behavior):
 * - Plain "||" is a domain name anchor, not left + empty + right anchor.
 * - "^" repeated at end of pattern: "^" matches end of URL only once.
 * - "^|" at end of pattern: "^" is allowed to match end of URL.
 *
 * Implementation details:
 * - CompiledUrlFilter's constructor (+#initializeUrlFilter) extracts the
 *   actual urlFilter and anchors, for matching against URLs later.
 * - RequestDataForUrlFilter class precomputes the URL / domain anchors to
 *   support matching more efficiently.
 * - CompiledUrlFilter's matchesRequest(request) checks whether the request is
 *   actually matched, using the precomputed information.
 *
 * The class was designed to minimize the number of string allocations during
 * request evaluation, because the matchesRequest method may be called very
 * often for every network request.
 */
class CompiledUrlFilter {
  #isUrlFilterCaseSensitive;
  #urlFilterParts; // = parts of urlFilter, minus anchors, split at "*".
  // isAnchorLeft and isAnchorDomain are mutually exclusive.
  #isAnchorLeft = false;
  #isAnchorDomain = false;
  #isAnchorRight = false;
  #isTrailingSeparator = false; // Whether urlFilter ends with "^".

  /**
   * @param {string} urlFilter - non-empty urlFilter
   * @param {boolean} [isUrlFilterCaseSensitive]
   */
  constructor(urlFilter, isUrlFilterCaseSensitive) {
    this.#isUrlFilterCaseSensitive = isUrlFilterCaseSensitive;
    this.#initializeUrlFilter(urlFilter, isUrlFilterCaseSensitive);
  }

  #initializeUrlFilter(urlFilter, isUrlFilterCaseSensitive) {
    let start = 0;
    let end = urlFilter.length;

    // First, trim the anchors off urlFilter.
    if (urlFilter[0] === "|") {
      if (urlFilter[1] === "|") {
        start = 2;
        this.#isAnchorDomain = true;
        // ^ will not revert to false below, because "||*" is already rejected
        // by RuleValidator's #checkCondUrlFilterAndRegexFilter method.
      } else {
        start = 1;
        this.#isAnchorLeft = true; // may revert to false below.
      }
    }
    if (end > start && urlFilter[end - 1] === "|") {
      --end;
      this.#isAnchorRight = true; // may revert to false below.
    }

    // Skip unnecessary wildcards, and adjust meaningless anchors accordingly:
    // "|*" and "*|" are not effective anchors, they could have been omitted.
    while (start < end && urlFilter[start] === "*") {
      ++start;
      this.#isAnchorLeft = false;
    }
    while (end > start && urlFilter[end - 1] === "*") {
      --end;
      this.#isAnchorRight = false;
    }

    // Special-case the last "^", so that the matching algorithm can rely on
    // the simple assumption that a "^" in the filter matches exactly one char:
    // The "^" at the end of the pattern is specified to match either one char
    // as usual, or as an anchor for the end of the URL (i.e. zero characters).
    this.#isTrailingSeparator = urlFilter[end - 1] === "^";

    let urlFilterWithoutAnchors = urlFilter.slice(start, end);
    if (!isUrlFilterCaseSensitive) {
      urlFilterWithoutAnchors = urlFilterWithoutAnchors.toLowerCase();
    }
    this.#urlFilterParts = urlFilterWithoutAnchors.split("*");
  }

  /**
   * Tests whether |request| matches the urlFilter.
   *
   * @param {RequestDataForUrlFilter} requestDataForUrlFilter
   * @returns {boolean} Whether the condition matches the URL.
   */
  matchesRequest(requestDataForUrlFilter) {
    const url = requestDataForUrlFilter.getUrl(this.#isUrlFilterCaseSensitive);
    const domainAnchors = requestDataForUrlFilter.domainAnchors;

    const urlFilterParts = this.#urlFilterParts;

    const REAL_END_OF_URL = url.length - 1; // minus trailing "^"

    // atUrlIndex is the position after the most recently matched part.
    // If a match is not found, it is -1 and we should return false.
    let atUrlIndex = 0;

    // The head always exists, potentially even an empty string.
    const head = urlFilterParts[0];
    if (this.#isAnchorLeft) {
      if (!this.#startsWithPart(head, url, 0)) {
        return false;
      }
      atUrlIndex = head.length;
    } else if (this.#isAnchorDomain) {
      atUrlIndex = this.#indexAfterDomainPart(head, url, domainAnchors);
    } else {
      atUrlIndex = this.#indexAfterPart(head, url, 0);
    }

    let previouslyAtUrlIndex = 0;
    for (let i = 1; i < urlFilterParts.length && atUrlIndex !== -1; ++i) {
      previouslyAtUrlIndex = atUrlIndex;
      atUrlIndex = this.#indexAfterPart(urlFilterParts[i], url, atUrlIndex);
    }
    if (atUrlIndex === -1) {
      return false;
    }
    if (atUrlIndex === url.length) {
      // We always append a "^" to the URL, so if the match is at the end of the
      // URL (REAL_END_OF_URL), only accept if the pattern ended with a "^".
      return this.#isTrailingSeparator;
    }
    if (!this.#isAnchorRight || atUrlIndex === REAL_END_OF_URL) {
      // Either not interested in the end, or already at the end of the URL.
      return true;
    }

    // #isAnchorRight is true but we are not at the end of the URL.
    // Backtrack once, to retry the last pattern (tail) with the end of the URL.

    const tail = urlFilterParts[urlFilterParts.length - 1];
    // The expected offset where the tail should be located.
    const expectedTailIndex = REAL_END_OF_URL - tail.length;
    // If #isTrailingSeparator is true, then accept the URL's trailing "^".
    const expectedTailIndexPlus1 = expectedTailIndex + 1;
    if (urlFilterParts.length === 1) {
      if (this.#isAnchorLeft) {
        // If matched, we would have returned at the REAL_END_OF_URL checks.
        return false;
      }
      if (this.#isAnchorDomain) {
        // The tail must be exactly at one of the domain anchors.
        return (
          (domainAnchors.includes(expectedTailIndex) &&
            this.#startsWithPart(tail, url, expectedTailIndex)) ||
          (this.#isTrailingSeparator &&
            domainAnchors.includes(expectedTailIndexPlus1) &&
            this.#startsWithPart(tail, url, expectedTailIndexPlus1))
        );
      }
      // head has no left/domain anchor, fall through.
    }
    // The tail is not left/domain anchored, accept it as long as it did not
    // overlap with an already-matched part of the URL.
    return (
      (expectedTailIndex > previouslyAtUrlIndex &&
        this.#startsWithPart(tail, url, expectedTailIndex)) ||
      (this.#isTrailingSeparator &&
        expectedTailIndexPlus1 > previouslyAtUrlIndex &&
        this.#startsWithPart(tail, url, expectedTailIndexPlus1))
    );
  }

  // Whether a character should match "^" in an urlFilter.
  // The "match end of URL" meaning of "^" is covered by #isTrailingSeparator.
  static #regexIsSep = /[^A-Za-z0-9_\-.%]/;

  #matchPartAt(part, url, urlIndex, sepStart) {
    if (sepStart === -1) {
      // Fast path.
      return url.startsWith(part, urlIndex);
    }
    if (urlIndex + part.length > url.length) {
      return false;
    }
    for (let i = 0; i < part.length; ++i) {
      let partChar = part[i];
      let urlChar = url[urlIndex + i];
      if (
        partChar !== urlChar &&
        (partChar !== "^" || !CompiledUrlFilter.#regexIsSep.test(urlChar))
      ) {
        return false;
      }
    }
    return true;
  }

  #startsWithPart(part, url, urlIndex) {
    const sepStart = part.indexOf("^");
    return this.#matchPartAt(part, url, urlIndex, sepStart);
  }

  #indexAfterPart(part, url, urlIndex) {
    let sepStart = part.indexOf("^");
    if (sepStart === -1) {
      // Fast path.
      let i = url.indexOf(part, urlIndex);
      return i === -1 ? i : i + part.length;
    }
    let maxUrlIndex = url.length - part.length;
    for (let i = urlIndex; i <= maxUrlIndex; ++i) {
      if (this.#matchPartAt(part, url, i, sepStart)) {
        return i + part.length;
      }
    }
    return -1;
  }

  #indexAfterDomainPart(part, url, domainAnchors) {
    const sepStart = part.indexOf("^");
    for (let offset of domainAnchors) {
      if (this.#matchPartAt(part, url, offset, sepStart)) {
        return offset + part.length;
      }
    }
    return -1;
  }
}

// See CompiledUrlFilter for documentation of RequestDataForUrlFilter.
class RequestDataForUrlFilter {
  /**
   * @param {string} requestURIspec - The URL to match against.
   */
  constructor(requestURIspec) {
    // "^" is appended, see CompiledUrlFilter's #initializeUrlFilter.
    this.urlAnyCase = requestURIspec + "^";
    this.urlLowerCase = this.urlAnyCase.toLowerCase();
    // For "||..." (Domain name anchor): where (sub)domains start in the URL.
    this.domainAnchors = this.#getDomainAnchors(this.urlAnyCase);
  }

  getUrl(isUrlFilterCaseSensitive) {
    return isUrlFilterCaseSensitive ? this.urlAnyCase : this.urlLowerCase;
  }

  #getDomainAnchors(url) {
    let hostStart = url.indexOf("://") + 3;
    let hostEnd = url.indexOf("/", hostStart);
    let userpassEnd = url.lastIndexOf("@", hostEnd) + 1;
    if (userpassEnd) {
      hostStart = userpassEnd;
    }
    let host = url.slice(hostStart, hostEnd);
    let domainAnchors = [hostStart];
    let offset = 0;
    // Find all offsets after ".". If not found, -1 + 1 = 0, and the loop ends.
    while ((offset = host.indexOf(".", offset) + 1)) {
      domainAnchors.push(hostStart + offset);
    }
    return domainAnchors;
  }
}

function compileRegexFilter(regexFilter, isUrlFilterCaseSensitive) {
  // TODO bug 1821033: Restrict supported regex to avoid perf issues. For
  // discussion on the desired syntax, see
  // https://github.com/w3c/webextensions/issues/344
  return new RegExp(regexFilter, isUrlFilterCaseSensitive ? "" : "i");
}

class ModifyHeadersBase {
  // Map<string,MatchedRule> - The first MatchedRule that modified the header.
  // After modifying a header, it cannot be modified further, with the exception
  // of the "append" operation, provided that they are from the same extension.
  #alreadyModifiedMap = new Map();
  // Set<string> - The list of headers allowed to be modified with "append",
  // despite having been modified. Allowed for "set"/"append", not for "remove".
  #appendStillAllowed = new Set();

  /**
   * @param {ChannelWrapper} channel
   */
  constructor(channel) {
    this.channel = channel;
  }

  /**
   * @param {MatchedRule} _matchedRule
   * @returns {object[]}
   */
  headerActionsFor(_matchedRule) {
    throw new Error("Not implemented.");
  }

  /**
   * @param {MatchedRule} _matchedrule
   * @param {string} _name
   * @param {string} _value
   * @param {boolean} _merge
   */
  setHeaderImpl(_matchedrule, _name, _value, _merge) {
    throw new Error("Not implemented.");
  }

  /** @param {MatchedRule[]} matchedRules */
  applyModifyHeaders(matchedRules) {
    for (const matchedRule of matchedRules) {
      for (const headerAction of this.headerActionsFor(matchedRule)) {
        const { header: name, operation, value } = headerAction;
        if (!this.#isOperationAllowed(name, operation, matchedRule)) {
          continue;
        }
        let ok;
        switch (operation) {
          case "set":
            ok = this.setHeader(matchedRule, name, value, /* merge */ false);
            if (ok) {
              this.#appendStillAllowed.add(name);
            }
            break;
          case "append":
            ok = this.setHeader(matchedRule, name, value, /* merge */ true);
            if (ok) {
              this.#appendStillAllowed.add(name);
            }
            break;
          case "remove":
            ok = this.setHeader(matchedRule, name, "", /* merge */ false);
            // Note: removal is final, so we don't add to #appendStillAllowed.
            break;
        }
        if (ok) {
          this.#alreadyModifiedMap.set(name, matchedRule);
        }
      }
    }
  }

  #isOperationAllowed(name, operation, matchedRule) {
    const modifiedBy = this.#alreadyModifiedMap.get(name);
    if (!modifiedBy) {
      return true;
    }
    if (
      operation === "append" &&
      this.#appendStillAllowed.has(name) &&
      matchedRule.ruleManager === modifiedBy.ruleManager
    ) {
      return true;
    }
    // TODO bug 1803369: dev experience improvement: consider logging when
    // a header modification was rejected.
    return false;
  }

  setHeader(matchedRule, name, value, merge) {
    try {
      this.setHeaderImpl(matchedRule, name, value, merge);
      return true;
    } catch (e) {
      const extension = matchedRule.ruleManager.extension;
      extension.logger.error(
        `Failed to apply modifyHeaders action to header "${name}" (DNR rule id ${matchedRule.rule.id} from ruleset "${matchedRule.ruleset.id}"): ${e}`
      );
    }
    return false;
  }

  // kName should already be in lower case.
  isHeaderNameEqual(name, kName) {
    return name.length === kName.length && name.toLowerCase() === kName;
  }
}

class ModifyRequestHeaders extends ModifyHeadersBase {
  static maybeApplyModifyHeaders(channel, matchedRules) {
    matchedRules = matchedRules.filter(mr => {
      const action = mr.rule.action;
      return action.type === "modifyHeaders" && action.requestHeaders?.length;
    });
    if (matchedRules.length) {
      new ModifyRequestHeaders(channel).applyModifyHeaders(matchedRules);
    }
  }

  /** @param {MatchedRule} matchedRule */
  headerActionsFor(matchedRule) {
    return matchedRule.rule.action.requestHeaders;
  }

  setHeaderImpl(matchedRule, name, value, merge) {
    if (this.isHeaderNameEqual(name, "host")) {
      this.#checkHostHeader(matchedRule, value);
    }
    if (merge && value && this.isHeaderNameEqual(name, "cookie")) {
      // By default, headers are merged with ",". But Cookie should use "; ".
      // HTTP/1.1 allowed only one Cookie header, but HTTP/2.0 allows multiple,
      // but recommends concatenation on one line. Relevant RFCs:
      // - https://www.rfc-editor.org/rfc/rfc6265#section-5.4
      // - https://www.rfc-editor.org/rfc/rfc7540#section-8.1.2.5
      // Consistent with Firefox internals, we ensure that there is at most one
      // Cookie header, by overwriting the previous one, if any.
      let existingCookie = this.channel.getRequestHeader("cookie");
      if (existingCookie) {
        value = existingCookie + "; " + value;
        merge = false;
      }
    }
    this.channel.setRequestHeader(name, value, merge);
  }

  #checkHostHeader(matchedRule, value) {
    let uri = Services.io.newURI(`https://${value}/`);
    let { policy } = matchedRule.ruleManager.extension;

    if (!policy.allowedOrigins.matches(uri)) {
      throw new Error(
        `Unable to set host header, url missing from permissions.`
      );
    }

    if (WebExtensionPolicy.isRestrictedURI(uri)) {
      throw new Error(`Unable to set host header to restricted url.`);
    }
  }
}

class ModifyResponseHeaders extends ModifyHeadersBase {
  static maybeApplyModifyHeaders(channel, matchedRules) {
    matchedRules = matchedRules.filter(mr => {
      const action = mr.rule.action;
      return action.type === "modifyHeaders" && action.responseHeaders?.length;
    });
    if (matchedRules.length) {
      new ModifyResponseHeaders(channel).applyModifyHeaders(matchedRules);
    }
  }

  headerActionsFor(matchedRule) {
    return matchedRule.rule.action.responseHeaders;
  }

  setHeaderImpl(matchedRule, name, value, merge) {
    this.channel.setResponseHeader(name, value, merge);
  }
}

class RuleValidator {
  constructor(alreadyValidatedRules, { isSessionRuleset = false } = {}) {
    this.rulesMap = new Map(alreadyValidatedRules.map(r => [r.id, r]));
    this.failures = [];
    this.isSessionRuleset = isSessionRuleset;
  }

  /**
   * Static method used to deserialize Rule class instances from a plain
   * js object rule as serialized implicitly by aomStartup.encodeBlob
   * when we store the rules into the startup cache file.
   *
   * @param {object} rule
   * @returns {Rule}
   */
  static deserializeRule(rule) {
    const newRule = new Rule(rule);
    if (newRule.condition.regexFilter) {
      newRule.condition.setCompiledRegexFilter(
        compileRegexFilter(
          newRule.condition.regexFilter,
          newRule.condition.isUrlFilterCaseSensitive
        )
      );
    }
    return newRule;
  }

  removeRuleIds(ruleIds) {
    for (const ruleId of ruleIds) {
      this.rulesMap.delete(ruleId);
    }
  }

  /**
   * @param {object[]} rules - A list of objects that adhere to the Rule type
   *    from declarative_net_request.json.
   */
  addRules(rules) {
    for (const rule of rules) {
      if (this.rulesMap.has(rule.id)) {
        this.#collectInvalidRule(rule, `Duplicate rule ID: ${rule.id}`);
        continue;
      }
      // declarative_net_request.json defines basic types, such as the expected
      // object properties and (primitive) type. Trivial constraints such as
      // minimum array lengths are also expressed in the schema.
      // Anything more complex is validated here. In particular, constraints
      // involving multiple properties (e.g. mutual exclusiveness).
      //
      // The following conditions have already been validated by the schema:
      // - isUrlFilterCaseSensitive (boolean)
      // - domainType (enum string)
      // - initiatorDomains & excludedInitiatorDomains & requestDomains &
      //   excludedRequestDomains (array of string in canonicalDomain format)
      if (
        !this.#checkCondResourceTypes(rule) ||
        !this.#checkCondRequestMethods(rule) ||
        !this.#checkCondTabIds(rule) ||
        !this.#checkCondUrlFilterAndRegexFilter(rule) ||
        !this.#checkAction(rule)
      ) {
        continue;
      }

      const newRule = new Rule(rule);
      // #lastCompiledRegexFilter is set if regexFilter is set, and null
      // otherwise by the above call to #checkCondUrlFilterAndRegexFilter().
      if (this.#lastCompiledRegexFilter) {
        newRule.condition.setCompiledRegexFilter(this.#lastCompiledRegexFilter);
      }

      this.rulesMap.set(rule.id, newRule);
    }
  }

  // #checkCondUrlFilterAndRegexFilter() compiles the regexFilter to check its
  // validity. To avoid having to compile it again when the Rule (RuleCondition)
  // is constructed, we temporarily cache the result.
  #lastCompiledRegexFilter;

  // Checks: resourceTypes & excludedResourceTypes
  #checkCondResourceTypes(rule) {
    const { resourceTypes, excludedResourceTypes } = rule.condition;
    if (this.#hasOverlap(resourceTypes, excludedResourceTypes)) {
      this.#collectInvalidRule(
        rule,
        "resourceTypes and excludedResourceTypes should not overlap"
      );
      return false;
    }
    if (rule.action.type === "allowAllRequests") {
      if (!resourceTypes) {
        this.#collectInvalidRule(
          rule,
          "An allowAllRequests rule must have a non-empty resourceTypes array"
        );
        return false;
      }
      if (resourceTypes.some(r => r !== "main_frame" && r !== "sub_frame")) {
        this.#collectInvalidRule(
          rule,
          "An allowAllRequests rule may only include main_frame/sub_frame in resourceTypes"
        );
        return false;
      }
    }
    return true;
  }

  // Checks: requestMethods & excludedRequestMethods
  #checkCondRequestMethods(rule) {
    const { requestMethods, excludedRequestMethods } = rule.condition;
    if (this.#hasOverlap(requestMethods, excludedRequestMethods)) {
      this.#collectInvalidRule(
        rule,
        "requestMethods and excludedRequestMethods should not overlap"
      );
      return false;
    }
    const isInvalidRequestMethod = method => method.toLowerCase() !== method;
    if (
      requestMethods?.some(isInvalidRequestMethod) ||
      excludedRequestMethods?.some(isInvalidRequestMethod)
    ) {
      this.#collectInvalidRule(rule, "request methods must be in lower case");
      return false;
    }
    return true;
  }

  // Checks: tabIds & excludedTabIds
  #checkCondTabIds(rule) {
    const { tabIds, excludedTabIds } = rule.condition;

    if ((tabIds || excludedTabIds) && !this.isSessionRuleset) {
      this.#collectInvalidRule(
        rule,
        "tabIds and excludedTabIds can only be specified in session rules"
      );
      return false;
    }

    if (this.#hasOverlap(tabIds, excludedTabIds)) {
      this.#collectInvalidRule(
        rule,
        "tabIds and excludedTabIds should not overlap"
      );
      return false;
    }
    return true;
  }

  static #regexNonASCII = /[^\x00-\x7F]/; // eslint-disable-line no-control-regex
  static #regexDigitOrBackslash = /^[0-9\\]$/;

  // Checks: urlFilter & regexFilter
  #checkCondUrlFilterAndRegexFilter(rule) {
    const { urlFilter, regexFilter } = rule.condition;

    this.#lastCompiledRegexFilter = null;

    const checkEmptyOrNonASCII = (str, prop) => {
      if (!str) {
        this.#collectInvalidRule(rule, `${prop} should not be an empty string`);
        return false;
      }
      // Non-ASCII in URLs are always encoded in % (or punycode in domains).
      if (RuleValidator.#regexNonASCII.test(str)) {
        this.#collectInvalidRule(
          rule,
          `${prop} should not contain non-ASCII characters`
        );
        return false;
      }
      return true;
    };
    if (urlFilter != null) {
      if (regexFilter != null) {
        this.#collectInvalidRule(
          rule,
          "urlFilter and regexFilter are mutually exclusive"
        );
        return false;
      }
      if (!checkEmptyOrNonASCII(urlFilter, "urlFilter")) {
        // #collectInvalidRule already called by checkEmptyOrNonASCII.
        return false;
      }
      if (urlFilter.startsWith("||*")) {
        // Rejected because Chrome does too. '||*' is equivalent to '*'.
        this.#collectInvalidRule(rule, "urlFilter should not start with '||*'");
        return false;
      }
    } else if (regexFilter != null) {
      if (!checkEmptyOrNonASCII(regexFilter, "regexFilter")) {
        // #collectInvalidRule already called by checkEmptyOrNonASCII.
        return false;
      }
      try {
        this.#lastCompiledRegexFilter = compileRegexFilter(
          regexFilter,
          rule.condition.isUrlFilterCaseSensitive
        );
      } catch (e) {
        this.#collectInvalidRule(
          rule,
          "regexFilter is not a valid regular expression"
        );
        return false;
      }
    }
    return true;
  }

  #checkAction(rule) {
    switch (rule.action.type) {
      case "allow":
      case "allowAllRequests":
      case "block":
      case "upgradeScheme":
        // These actions have no extra properties.
        break;
      case "redirect":
        return this.#checkActionRedirect(rule);
      case "modifyHeaders":
        return this.#checkActionModifyHeaders(rule);
      default:
        // Other values are not possible because declarative_net_request.json
        // only accepts the above action types.
        throw new Error(`Unexpected action type: ${rule.action.type}`);
    }
    return true;
  }

  #checkActionRedirect(rule) {
    const { url, extensionPath, transform, regexSubstitution } =
      rule.action.redirect ?? {};
    const hasExtensionPath = extensionPath != null;
    const hasRegexSubstitution = regexSubstitution != null;
    const redirectKeyCount = // @ts-ignore trivial/noisy
      !!url + !!hasExtensionPath + !!transform + !!hasRegexSubstitution;
    if (redirectKeyCount !== 1) {
      if (redirectKeyCount === 0) {
        this.#collectInvalidRule(
          rule,
          "A redirect rule must have a non-empty action.redirect object"
        );
        return false;
      }
      // Side note: Chrome silently ignores excess keys, and skips validation
      // for ignored keys, in this order:
      // - url > extensionPath > transform > regexSubstitution
      this.#collectInvalidRule(
        rule,
        "redirect.url, redirect.extensionPath, redirect.transform and redirect.regexSubstitution are mutually exclusive"
      );
      return false;
    }

    if (hasExtensionPath && !extensionPath.startsWith("/")) {
      this.#collectInvalidRule(
        rule,
        "redirect.extensionPath should start with a '/'"
      );
      return false;
    }

    // If specified, the "url" property is described as "format": "url" in the
    // JSON schema, which ensures that the URL is a canonical form, and that
    // the extension is allowed to trigger a navigation to the URL.
    // E.g. javascript: and privileged about:-URLs cannot be navigated to, but
    // http(s) URLs can (regardless of extension permissions).
    // data:-URLs are currently blocked due to bug 1622986.

    if (transform) {
      if (transform.query != null && transform.queryTransform) {
        this.#collectInvalidRule(
          rule,
          "redirect.transform.query and redirect.transform.queryTransform are mutually exclusive"
        );
        return false;
      }
      // Most of the validation is done by nsIURIMutator via applyURLTransform.
      // nsIURIMutator is not very strict, so we perform some extra checks here
      // to reject values that are not technically valid URLs.

      if (transform.port && /\D/.test(transform.port)) {
        // nsIURIMutator's setPort takes an int, so any string will implicitly
        // be converted to a number. This part verifies that the input only
        // consists of digits. setPort will ensure that it is at most 65535.
        this.#collectInvalidRule(
          rule,
          "redirect.transform.port should be empty or an integer"
        );
        return false;
      }

      // Note: we don't verify whether transform.query starts with '/', because
      // Chrome does not require it, and nsIURIMutator prepends it if missing.

      if (transform.query && !transform.query.startsWith("?")) {
        this.#collectInvalidRule(
          rule,
          "redirect.transform.query should be empty or start with a '?'"
        );
        return false;
      }
      if (transform.fragment && !transform.fragment.startsWith("#")) {
        this.#collectInvalidRule(
          rule,
          "redirect.transform.fragment should be empty or start with a '#'"
        );
        return false;
      }
      try {
        const dummyURI = Services.io.newURI("http://dummy");
        // applyURLTransform uses nsIURIMutator to transform a URI, and throws
        // if |transform| is invalid, e.g. invalid host, port, etc.
        applyURLTransform(dummyURI, transform);
      } catch (e) {
        this.#collectInvalidRule(
          rule,
          "redirect.transform does not describe a valid URL transformation"
        );
        return false;
      }
    }

    if (hasRegexSubstitution) {
      if (!rule.condition.regexFilter) {
        this.#collectInvalidRule(
          rule,
          "redirect.regexSubstitution requires the regexFilter condition to be specified"
        );
        return false;
      }
      let i = 0;
      // i will be index after \. Loop breaks if not found (-1+1=0 = false).
      while ((i = regexSubstitution.indexOf("\\", i) + 1)) {
        let c = regexSubstitution[i++]; // may be undefined if \ is at end.
        if (c === undefined || !RuleValidator.#regexDigitOrBackslash.test(c)) {
          this.#collectInvalidRule(
            rule,
            "redirect.regexSubstitution only allows digit or \\ after \\."
          );
          return false;
        }
      }
    }

    return true;
  }

  #checkActionModifyHeaders(rule) {
    const { requestHeaders, responseHeaders } = rule.action;
    if (!requestHeaders && !responseHeaders) {
      this.#collectInvalidRule(
        rule,
        "A modifyHeaders rule must have a non-empty requestHeaders or modifyHeaders list"
      );
      return false;
    }

    const isValidModifyHeadersOp = ({ header, operation, value }) => {
      if (!header) {
        this.#collectInvalidRule(rule, "header must be non-empty");
        return false;
      }
      if (!value && (operation === "append" || operation === "set")) {
        this.#collectInvalidRule(
          rule,
          "value is required for operations append/set"
        );
        return false;
      }
      if (value && operation === "remove") {
        this.#collectInvalidRule(
          rule,
          "value must not be provided for operation remove"
        );
        return false;
      }
      return true;
    };
    if (
      (requestHeaders && !requestHeaders.every(isValidModifyHeadersOp)) ||
      (responseHeaders && !responseHeaders.every(isValidModifyHeadersOp))
    ) {
      // #collectInvalidRule already called by isValidModifyHeadersOp.
      return false;
    }
    return true;
  }

  // Conditions with a filter and an exclude-filter should reject overlapping
  // lists, because they can never simultaneously be true.
  #hasOverlap(arrayA, arrayB) {
    return arrayA && arrayB && arrayA.some(v => arrayB.includes(v));
  }

  #collectInvalidRule(rule, message) {
    this.failures.push({ rule, message });
  }

  getValidatedRules() {
    return Array.from(this.rulesMap.values());
  }

  getFailures() {
    return this.failures;
  }
}

export class RuleQuotaCounter {
  constructor(ruleLimitName) {
    this.ruleLimitName = ruleLimitName;
    this.ruleLimitRemaining = lazy.ExtensionDNRLimits[this.ruleLimitName];
    this.regexRemaining = lazy.ExtensionDNRLimits.MAX_NUMBER_OF_REGEX_RULES;
  }

  tryAddRules(rulesetId, rules) {
    if (rules.length > this.ruleLimitRemaining) {
      this.#throwQuotaError(rulesetId, "rules", this.ruleLimitName);
    }
    let regexCount = 0;
    for (let rule of rules) {
      if (rule.condition.regexFilter && ++regexCount > this.regexRemaining) {
        this.#throwQuotaError(
          rulesetId,
          "regexFilter rules",
          "MAX_NUMBER_OF_REGEX_RULES"
        );
      }
    }

    // Update counters only when there are no quota errors.
    this.ruleLimitRemaining -= rules.length;
    this.regexRemaining -= regexCount;
  }

  #throwQuotaError(rulesetId, what, limitName) {
    if (this.ruleLimitName === "GUARANTEED_MINIMUM_STATIC_RULES") {
      throw new ExtensionError(
        `Number of ${what} across all enabled static rulesets exceeds ${limitName} if ruleset "${rulesetId}" were to be enabled.`
      );
    }
    throw new ExtensionError(
      `Number of ${what} in ruleset "${rulesetId}" exceeds ${limitName}.`
    );
  }
}

/**
 * Compares two rules to determine the relative order of precedence.
 * Rules are only comparable if they are from the same extension!
 *
 * @param {Rule} ruleA
 * @param {Rule} ruleB
 * @param {Ruleset} rulesetA - the ruleset ruleA is part of.
 * @param {Ruleset} rulesetB - the ruleset ruleB is part of.
 * @returns {integer}
 *   0 if equal.
 *   <0 if ruleA comes before ruleB.
 *   >0 if ruleA comes after ruleB.
 */
function compareRule(ruleA, ruleB, rulesetA, rulesetB) {
  // Comparators: 0 if equal, >0 if a after b, <0 if a before b.
  function cmpHighestNumber(a, b) {
    return a === b ? 0 : b - a;
  }
  function cmpLowestNumber(a, b) {
    return a === b ? 0 : a - b;
  }
  return (
    // All compared operands are non-negative integers.
    cmpHighestNumber(ruleA.priority, ruleB.priority) ||
    cmpLowestNumber(ruleA.actionPrecedence(), ruleB.actionPrecedence()) ||
    // As noted in the big comment at the top of the file, the following two
    // comparisons only exist in order to have a stable ordering of rules. The
    // specific comparison is somewhat arbitrary and matches Chrome's behavior.
    // For context, see https://github.com/w3c/webextensions/issues/280
    cmpLowestNumber(rulesetA.rulesetPrecedence, rulesetB.rulesetPrecedence) ||
    cmpLowestNumber(ruleA.id, ruleB.id)
  );
}

class MatchedRule {
  /**
   * @param {Rule} rule
   * @param {Ruleset} ruleset
   */
  constructor(rule, ruleset) {
    this.rule = rule;
    this.ruleset = ruleset;
  }

  // The RuleManager that generated this MatchedRule.
  get ruleManager() {
    return this.ruleset.ruleManager;
  }
}

// tabId computation is currently not free, and depends on the initialization of
// ExtensionParent.apiManager.global (see WebRequest.getTabIdForChannelWrapper).
// Fortunately, DNR only supports tabIds in session rules, so by keeping track
// of session rules with tabIds/excludedTabIds conditions, we can find tabId
// exactly and only when necessary.
let gHasAnyTabIdConditions = false;

class RequestDetails {
  /**
   * @param {object} options
   * @param {nsIURI} options.requestURI - URL of the requested resource.
   * @param {nsIURI} [options.initiatorURI] - URL of triggering principal,
   *   provided that it is a content principal. Otherwise null.
   * @param {string} options.type - ResourceType (MozContentPolicyType).
   * @param {string} [options.method] - HTTP method
   * @param {integer} [options.tabId]
   * @param {CanonicalBrowsingContext} [options.browsingContext] - The CBC
   *   associated with the request. Typically the bc for which the subresource
   *   request is initiated, if any. For document requests, this is the parent
   *   (i.e. the parent frame for sub_frame, null for main_frame).
   */
  constructor({
    requestURI,
    initiatorURI,
    type,
    method,
    tabId,
    browsingContext,
  }) {
    this.requestURI = requestURI;
    this.initiatorURI = initiatorURI;
    this.type = type;
    this.method = method;
    this.tabId = tabId;
    this.browsingContext = browsingContext;

    let requestDomain = this.#domainFromURI(requestURI);
    let initiatorDomain = initiatorURI
      ? this.#domainFromURI(initiatorURI)
      : null;
    this.allRequestDomains =
      requestDomain && this.#getAllDomainsWithin(requestDomain);
    this.allInitiatorDomains =
      initiatorDomain && this.#getAllDomainsWithin(initiatorDomain);

    this.domainType = this.#isThirdParty(requestURI, initiatorURI)
      ? "thirdParty"
      : "firstParty";

    this.requestURIspec = requestURI.spec;
    this.requestDataForUrlFilter = new RequestDataForUrlFilter(
      this.requestURIspec
    );
  }

  #isThirdParty(requestURI, initiatorURI) {
    if (!initiatorURI) {
      // E.g. main_frame request or opaque origin.
      return true;
    }

    try {
      return (
        Services.eTLD.getBaseDomain(requestURI) !==
        Services.eTLD.getBaseDomain(initiatorURI)
      );
    } catch (err) {
      // May throw if either domain is an IP address, lacks a public suffix
      // (e.g. http://localhost or moz-extension://UUID)
      // or contains characters disallowed in URIs. Fall back:
      return (
        this.#domainFromURI(requestURI) !== this.#domainFromURI(initiatorURI)
      );
    }
  }

  static fromChannelWrapper(channel) {
    let tabId = -1;
    if (gHasAnyTabIdConditions) {
      tabId = lazy.WebRequest.getTabIdForChannelWrapper(channel);
    }
    return new RequestDetails({
      requestURI: channel.finalURI,
      // Note: originURI may be null, if missing or null principal, as desired.
      initiatorURI: channel.originURI,
      type: channel.type,
      method: channel.method.toLowerCase(),
      tabId,
      browsingContext: channel.loadInfo.browsingContext,
    });
  }

  #ancestorRequestDetails;
  get ancestorRequestDetails() {
    if (this.#ancestorRequestDetails) {
      return this.#ancestorRequestDetails;
    }
    this.#ancestorRequestDetails = [];
    if (!this.browsingContext?.ancestorsAreCurrent) {
      // this.browsingContext is set for real requests (via fromChannelWrapper).
      // It may be void for testMatchOutcome and for the ancestor requests
      // simulated below.
      //
      // ancestorsAreCurrent being false is unexpected, but could theoretically
      // happen if the request is triggered from an unloaded (sub)frame. In that
      // case we don't want to use potentially incorrect ancestor information.
      //
      // In any case, nothing left to do.
      return this.#ancestorRequestDetails;
    }
    // Reconstruct the frame hierarchy of the request's document, in order to
    // retroactively recompute the relevant matches of allowAllRequests rules.
    //
    // The allowAllRequests rule is supposedly applying to all subresource
    // requests. For non-document requests, this is usually the document if any.
    // In case of document requests, there is some ambiguity:
    // - Usually, the initiator is the parent document that created the frame.
    // - Sometimes, the initiator is a different frame or even another window.
    //
    // In RequestDetails.fromChannelWrapper, the actual initiator is used and
    // reflected in initiatorURI, but here we use the document's parent. This
    // is done because the chain of initiators is unstable (e.g. an opener can
    // navigate/unload), whereas frame ancestor chain is constant as long as
    // the leaf BrowsingContext is current. Moreover, allowAllRequests was
    // originally designed to operate on frame hierarchies (crbug.com/1038831).
    //
    // This implementation of "initiator" for "allowAllRequests" is consistent
    // with Chrome and Safari.
    for (let bc = this.browsingContext; bc; bc = bc.parent) {
      // Note: requestURI may differ from the document's initial requestURI,
      // e.g. due to same-document navigations.
      const requestURI = bc.currentURI;
      if (!requestURI.schemeIs("https") && !requestURI.schemeIs("http")) {
        // DNR is currently only hooked up to http(s) requests. Ignore other
        // URLs, e.g. about:, blob:, moz-extension:, data:, etc.
        continue;
      }
      const isTop = !bc.parent;
      const parentPrin = bc.parentWindowContext?.documentPrincipal;
      const requestDetails = new RequestDetails({
        requestURI,
        // Note: initiatorURI differs from RequestDetails.fromChannelWrapper;
        // See the above comment for more info.
        initiatorURI: parentPrin?.isContentPrincipal ? parentPrin.URI : null,
        type: isTop ? "main_frame" : "sub_frame",
        method: bc.activeSessionHistoryEntry?.hasPostData ? "post" : "get",
        tabId: this.tabId,
        // In this loop we are already explicitly accounting for ancestors, so
        // we intentionally omit browsingContext even though we have |bc|. If
        // we were to set `browsingContext: bc`, the output would be the same,
        // but be derived from unnecessarily repeated request evaluations.
        browsingContext: null,
      });
      this.#ancestorRequestDetails.unshift(requestDetails);
    }
    return this.#ancestorRequestDetails;
  }

  canExtensionModify(extension) {
    const policy = extension.policy;
    if (!policy.canAccessURI(this.requestURI)) {
      return false;
    }
    if (
      this.initiatorURI &&
      this.type !== "main_frame" &&
      this.type !== "sub_frame" &&
      !policy.canAccessURI(this.initiatorURI, false, true, true)
    ) {
      // Host permissions for the initiator is required except for navigation
      // requests: https://bugzilla.mozilla.org/show_bug.cgi?id=1825824#c2
      return false;
    }
    return true;
  }

  #domainFromURI(uri) {
    try {
      let hostname = uri.host;
      // nsIURI omits brackets from IPv6 addresses. But the canonical form of an
      // IPv6 address is with brackets, so add them.
      return hostname.includes(":") ? `[${hostname}]` : hostname;
    } catch (e) {
      // uri.host throws for some schemes (e.g. about:). In practice we won't
      // encounter this for network (via NetworkIntegration.startDNREvaluation)
      // because isRestrictedPrincipalURI filters the initiatorURI. Furthermore,
      // because only http(s) requests are observed, requestURI is http(s).
      //
      // declarativeNetRequest.testMatchOutcome can pass arbitrary URIs and thus
      // trigger the error in nsIURI::GetHost.
      Cu.reportError(e);
      return null;
    }
  }

  /**
   * @param {string} domain - The canonical representation of the host of a URL.
   * @returns {string[]} A non-empty list of the domain and all superdomains
   *   within the given domain. This may include items that are not resolvable
   *   domains, such as "com" (from input "example.com").
   */
  #getAllDomainsWithin(domain) {
    const domains = [domain];
    let i = 0;
    // Reminder: domain cannot start with a dot, nor contain consecutive dots.
    while ((i = domain.indexOf(".", i) + 1) !== 0) {
      const superdomain = domain.slice(i);
      // A full domain can end with a dot (FQDN) such as "example.com.", in
      // which case the last domain should be "com." and not "".
      if (superdomain) {
        domains.push(superdomain);
      }
    }
    return domains;
  }
}

// Domain lists in rule conditions (requestDomains, excludedRequestDomains,
// initiatorDomains, excludedInitiatorDomains) could be really long, containing
// thousands of entries. We convert them to Set for faster lookup.
const gDomainsListToSet = new DefaultWeakMap(domains => new Set(domains));

/**
 * This RequestEvaluator class's logic is documented at the top of this file.
 */
class RequestEvaluator {
  // private constructor, only used by RequestEvaluator.evaluateRequest.
  constructor(request, ruleManager) {
    this.req = request;
    this.ruleManager = ruleManager;
    this.canModify = request.canExtensionModify(ruleManager.extension);

    // These values are initialized by findMatchingRules():
    this.matchedRule = null;
    this.matchedModifyHeadersRules = [];
    this.didCheckAncestors = false;
    this.findMatchingRules();
  }

  /**
   * Finds the matched rules for the given request and extensions,
   * according to the logic documented at the top of this file.
   *
   * @param {RequestDetails} request
   * @param {RuleManager[]} ruleManagers
   *    The list of RuleManagers, ordered by importance of its extension.
   * @returns {MatchedRule[]}
   */
  static evaluateRequest(request, ruleManagers) {
    // Helper to determine precedence of rules from different extensions.
    function precedence(matchedRule) {
      switch (matchedRule.rule.action.type) {
        case "block":
          return 1;
        case "redirect":
        case "upgradeScheme":
          return 2;
        case "allow":
        case "allowAllRequests":
          return 3;
        // case "modifyHeaders": not comparable after the first pass.
        default:
          throw new Error(`Unexpected action: ${matchedRule.rule.action.type}`);
      }
    }

    let requestEvaluators = [];
    let finalMatch;
    for (let ruleManager of ruleManagers) {
      // Evaluate request with findMatchingRules():
      const requestEvaluator = new RequestEvaluator(request, ruleManager);
      // RequestEvaluator may be used after the loop when the request is
      // accepted, to collect modifyHeaders/allow/allowAllRequests actions.
      requestEvaluators.push(requestEvaluator);
      let matchedRule = requestEvaluator.matchedRule;
      if (
        matchedRule &&
        (!finalMatch || precedence(matchedRule) < precedence(finalMatch))
      ) {
        // Before choosing the matched rule as finalMatch, check whether there
        // is an allowAllRequests rule override among the ancestors.
        requestEvaluator.findAncestorRuleOverride();
        matchedRule = requestEvaluator.matchedRule;
        if (!finalMatch || precedence(matchedRule) < precedence(finalMatch)) {
          finalMatch = matchedRule;
          if (finalMatch.rule.action.type === "block") {
            break;
          }
        }
      }
    }
    if (finalMatch && !finalMatch.rule.isAllowOrAllowAllRequestsAction()) {
      // Found block/redirect/upgradeScheme, request will be replaced.
      return [finalMatch];
    }
    // Request not canceled, collect all modifyHeaders actions:
    let matchedRules = requestEvaluators
      .map(re => re.getMatchingModifyHeadersRules())
      .flat(1);

    // ... and collect the allowAllRequests actions:
    // Note: Only needed for testMatchOutcome, getMatchedRules (bug 1745765) and
    // onRuleMatchedDebug (bug 1745773). Not for regular requests, since regular
    // requests do not distinguish between no rule vs allow vs allowAllRequests.
    let finalAllowAllRequestsMatches = [];
    for (let requestEvaluator of requestEvaluators) {
      // TODO bug 1745765 / bug 1745773: Uncomment findAncestorRuleOverride()
      // when getMatchedRules() or onRuleMatchedDebug are implemented.
      // requestEvaluator.findAncestorRuleOverride();
      let matchedRule = requestEvaluator.matchedRule;
      if (matchedRule && matchedRule.rule.action.type === "allowAllRequests") {
        // Even if a different extension wins the final match, an extension
        // may want to record the "allowAllRequests" action for the future.
        finalAllowAllRequestsMatches.push(matchedRule);
      }
    }
    if (finalAllowAllRequestsMatches.length) {
      matchedRules = finalAllowAllRequestsMatches.concat(matchedRules);
    }

    // ... and collect the "allow" action. At this point, finalMatch could also
    // be a modifyHeaders or allowAllRequests action, but these would already
    // have been added to the matchedRules result before.
    if (finalMatch && finalMatch.rule.action.type === "allow") {
      matchedRules.unshift(finalMatch);
    }
    return matchedRules;
  }

  /**
   * Finds the matching rules, as documented in the comment before the class.
   */
  findMatchingRules() {
    if (!this.canModify && !this.ruleManager.hasBlockPermission) {
      // If the extension cannot apply any action, don't bother.
      return;
    }

    this.#collectMatchInRuleset(this.ruleManager.sessionRules);
    this.#collectMatchInRuleset(this.ruleManager.dynamicRules);
    for (let ruleset of this.ruleManager.enabledStaticRules) {
      this.#collectMatchInRuleset(ruleset);
    }

    if (this.matchedRule && !this.#isRuleActionAllowed(this.matchedRule.rule)) {
      this.matchedRule = null;
      // Note: this.matchedModifyHeadersRules is [] because canModify access is
      // checked before populating the list.
    }
  }

  /**
   * Find an "allowAllRequests" rule among the ancestors that may override the
   * current matchedRule and/or matchedModifyHeadersRules rules.
   */
  findAncestorRuleOverride() {
    if (this.didCheckAncestors) {
      return;
    }
    this.didCheckAncestors = true;

    if (!this.ruleManager.hasRulesWithAllowAllRequests) {
      // Optimization: Skip ancestorRequestDetails lookup and/or request
      // evaluation if there are no allowAllRequests rules.
      return;
    }

    // Now we need to check whether any of the ancestor frames had a matching
    // allowAllRequests rule. matchedRule and/or matchedModifyHeadersRules
    // results may be ignored if their priority is lower or equal to the
    // highest-priority allowAllRequests rule among the frame ancestors.
    //
    // In theory, every ancestor may potentially yield an allowAllRequests rule,
    // and should therefore be checked unconditionally. But logically, if there
    // are no existing matches, then any matching allowAllRequests rules will
    // not have any effect on the request outcome. As an optimization, we
    // therefore skip ancestor checks in this case.
    if (
      (!this.matchedRule ||
        this.matchedRule.rule.isAllowOrAllowAllRequestsAction()) &&
      !this.matchedModifyHeadersRules.length
    ) {
      // Optimization: Do not look up ancestors if no rules were matched.
      //
      // TODO bug 1745773: onRuleMatchedDebug is supposed to report when a rule
      // has been matched. To be pedantic, when there is an onRuleMatchedDebug
      // listener, the parents need to be checked unconditionally, in order to
      // report potential allowAllRequests matches among ancestors.
      // TODO bug 1745765: the above may also apply to getMatchedRules().
      return;
    }

    for (let request of this.req.ancestorRequestDetails) {
      // TODO: Optimize by only evaluating allow/allowAllRequests rules, because
      // the request being seen here implies that the request was not canceled,
      // i.e. that there were no block/redirect/upgradeScheme rules in any of
      // the ancestors (across all extensions!).
      let requestEvaluator = new RequestEvaluator(request, this.ruleManager);
      let ancestorMatchedRule = requestEvaluator.matchedRule;
      if (
        ancestorMatchedRule &&
        ancestorMatchedRule.rule.action.type === "allowAllRequests" &&
        (!this.matchedRule ||
          compareRule(
            this.matchedRule.rule,
            ancestorMatchedRule.rule,
            this.matchedRule.ruleset,
            ancestorMatchedRule.ruleset
          ) > 0)
      ) {
        // Found an allowAllRequests rule that takes precedence over whatever
        // the current rule was.
        this.matchedRule = ancestorMatchedRule;
      }
    }
  }

  /**
   * Retrieves the list of matched modifyHeaders rules that should apply.
   *
   * @returns {MatchedRule[]}
   */
  getMatchingModifyHeadersRules() {
    if (this.matchedModifyHeadersRules.length) {
      // Find parent allowAllRequests rules, if any, to make sure that we can
      // appropriately ignore same-or-lower-priority modifyHeaders rules.
      this.findAncestorRuleOverride();
    }
    // The minimum priority is 1. Defaulting to 0 = include all.
    let priorityThreshold = 0;
    if (this.matchedRule?.rule.isAllowOrAllowAllRequestsAction()) {
      priorityThreshold = this.matchedRule.rule.priority;
    }
    // Note: the result cannot be non-empty if this.matchedRule is a non-allow
    // action, because if that were to be the case, then the request would have
    // been canceled, and therefore there would not be any header to modify.
    // Even if another extension were to override the action, it could only be
    // any other non-allow action, which would still cancel the request.
    let matchedRules = this.matchedModifyHeadersRules.filter(matchedRule => {
      return matchedRule.rule.priority > priorityThreshold;
    });
    // Sort output for a deterministic order.
    // NOTE: Sorting rules at registration (in RuleManagers) would avoid the
    // need to sort here. Since the number of matched modifyHeaders rules are
    // expected to be small, we don't bother optimizing.
    matchedRules.sort((a, b) => {
      return compareRule(a.rule, b.rule, a.ruleset, b.ruleset);
    });
    return matchedRules;
  }

  /** @param {Ruleset} ruleset */
  #collectMatchInRuleset(ruleset) {
    for (let rule of ruleset.rules) {
      if (ruleset.disabledRuleIds?.has(rule.id)) {
        continue;
      }
      if (!this.#matchesRuleCondition(rule.condition)) {
        continue;
      }
      if (rule.action.type === "modifyHeaders") {
        if (this.canModify) {
          this.matchedModifyHeadersRules.push(new MatchedRule(rule, ruleset));
        }
        continue;
      }
      if (
        this.matchedRule &&
        compareRule(
          this.matchedRule.rule,
          rule,
          this.matchedRule.ruleset,
          ruleset
        ) <= 0
      ) {
        continue;
      }
      this.matchedRule = new MatchedRule(rule, ruleset);
    }
  }

  /**
   * @param {RuleCondition} cond
   * @returns {boolean} Whether the condition matched.
   */
  #matchesRuleCondition(cond) {
    if (cond.resourceTypes) {
      if (!cond.resourceTypes.includes(this.req.type)) {
        return false;
      }
    } else if (cond.excludedResourceTypes) {
      if (cond.excludedResourceTypes.includes(this.req.type)) {
        return false;
      }
    } else if (this.req.type === "main_frame") {
      // When resourceTypes/excludedResourceTypes are not specified, the
      // documented behavior is to ignore main_frame requests.
      return false;
    }

    // Check this.req.requestURI:
    if (cond.urlFilter) {
      if (!cond.urlFilterMatches(this.req.requestDataForUrlFilter)) {
        return false;
      }
    } else if (cond.regexFilter) {
      if (!cond.getCompiledRegexFilter().test(this.req.requestURIspec)) {
        return false;
      }
    }
    if (
      cond.excludedRequestDomains &&
      this.#matchesDomains(
        cond.excludedRequestDomains,
        this.req.allRequestDomains
      )
    ) {
      return false;
    }
    if (
      cond.requestDomains &&
      !this.#matchesDomains(cond.requestDomains, this.req.allRequestDomains)
    ) {
      return false;
    }
    if (
      cond.excludedInitiatorDomains &&
      // Note: unable to only match null principals (bug 1798225).
      this.req.allInitiatorDomains &&
      this.#matchesDomains(
        cond.excludedInitiatorDomains,
        this.req.allInitiatorDomains
      )
    ) {
      return false;
    }
    if (
      cond.initiatorDomains &&
      // Note: unable to only match null principals (bug 1798225).
      (!this.req.allInitiatorDomains ||
        !this.#matchesDomains(
          cond.initiatorDomains,
          this.req.allInitiatorDomains
        ))
    ) {
      return false;
    }

    if (cond.domainType && cond.domainType !== this.req.domainType) {
      return false;
    }

    if (cond.requestMethods) {
      if (!cond.requestMethods.includes(this.req.method)) {
        return false;
      }
    } else if (cond.excludedRequestMethods?.includes(this.req.method)) {
      return false;
    }

    if (cond.tabIds) {
      if (!cond.tabIds.includes(this.req.tabId)) {
        return false;
      }
    } else if (cond.excludedTabIds?.includes(this.req.tabId)) {
      return false;
    }

    return true;
  }

  /**
   * @param {string[]} domainsInCondition - A potentially long list of
   *   canonicalized domain patterns that are part of a rule condition.
   *   Canonical means punycode, no ports, and IPv6 without brackets, and not
   *   starting with a dot. May end with a dot if it is a FQDN.
   * @param {string[]} targetDomains - The list of domains and superdomains
   *   within the original URI (see #getAllDomainsWithin).
   * @returns {boolean} Whether the actual host (encoded in targetDomains) is a
   *   (sub)domain of any of the domains in the condition (domainsInCondition).
   */
  #matchesDomains(domainsInCondition, targetDomains) {
    const ruleDomainsSet = gDomainsListToSet.get(domainsInCondition);
    return targetDomains.some(domain => ruleDomainsSet.has(domain));
  }

  /**
   * @param {Rule} rule - The final rule from the first pass.
   * @returns {boolean} Whether the extension is allowed to execute the rule.
   */
  #isRuleActionAllowed(rule) {
    if (this.canModify) {
      return true;
    }
    switch (rule.action.type) {
      case "allow":
      case "allowAllRequests":
      case "block":
      case "upgradeScheme":
        return this.ruleManager.hasBlockPermission;
      case "redirect":
        return false;
      // case "modifyHeaders" is never an action for this.matchedRule.
      default:
        throw new Error(`Unexpected action type: ${rule.action.type}`);
    }
  }
}

/**
 * Checks whether a request from a document with the given URI is allowed to
 * be modified by an unprivileged extension (e.g. an extension without host
 * permissions but the "declarativeNetRequest" permission).
 * The output is comparable to WebExtensionPolicy::CanAccessURI for an extension
 * with the `<all_urls>` permission, for consistency with the webRequest API.
 *
 * @param {nsIURI} [uri] The URI of a request's loadingPrincipal. May be void
 *   if missing (e.g. top-level requests) or not a content principal.
 * @returns {boolean} Whether there is any extension that is allowed to see
 *   requests from a document with the given URI. Callers are expected to:
 *   - check system requests (and treat as true).
 *   - check WebExtensionPolicy.isRestrictedURI (and treat as true).
 */
function isRestrictedPrincipalURI(uri) {
  if (!uri) {
    // No URI, could be:
    // - System principal (caller should have checked and disallowed access).
    // - Expanded principal, typically content script in documents. If an
    //   extension content script managed to run there, that implies that an
    //   extension was able to access it.
    // - Null principal (e.g. sandboxed document, about:blank, data:).
    return false;
  }

  // An unprivileged extension with maximal host permissions has allowedOrigins
  // set to [`<all_urls>`, `moz-extension://extensions-own-uuid-here`].
  // `<all_urls>` matches PermittedSchemes from MatchPattern.cpp:
  // https://searchfox.org/mozilla-central/rev/55d5c4b9dffe5e59eb6b019c1a930ec9ada47e10/toolkit/components/extensions/MatchPattern.cpp#209-211
  // i.e. "http", "https", "ws", "wss", "file", "ftp", "data".
  // - It is not possible to have a loadingPrincipal for: ws, wss, ftp.
  // - data:-URIs always have an opaque origin, i.e. the principal is not a
  //   content principal, thus void here.
  // - The remaining schemes from `<all_urls>` are: http, https, file, data,
  //   and checked below.
  //
  // Privileged addons can also access resource: and about:, but we do not need
  // to support these now.

  // http(s) are common, and allowed, except for some restricted domains. The
  // caller is expected to check WebExtensionPolicy.isRestrictedURI.
  if (uri.schemeIs("http") || uri.schemeIs("https")) {
    return false; // Very common.
  }

  // moz-extension: is not restricted because an extension always has permission
  // to its own moz-extension:-origin. The caller is expected to verify that an
  // extension can only access its own URI.
  if (uri.schemeIs("moz-extension")) {
    return false;
  }

  // Requests from local files are intentionally allowed (bug 1621935).
  if (uri.schemeIs("file")) {
    return false;
  }

  // Anything else (e.g. resource:, about:newtab, etc.) is not allowed.
  return true;
}

const NetworkIntegration = {
  maxEvaluatedRulesCount: 0,

  register() {
    // We register via WebRequest.sys.mjs to ensure predictable ordering of DNR and
    // WebRequest behavior.
    lazy.WebRequest.setDNRHandlingEnabled(true);
  },
  unregister() {
    lazy.WebRequest.setDNRHandlingEnabled(false);
  },
  maybeUpdateTabIdChecker() {
    gHasAnyTabIdConditions = gRuleManagers.some(rm => rm.hasRulesWithTabIds);
  },

  startDNREvaluation(channel) {
    let ruleManagers = gRuleManagers;
    // TODO bug 1827422: Merge isRestrictedPrincipalURI with canModify.
    if (!channel.canModify || isRestrictedPrincipalURI(channel.documentURI)) {
      // Ignore system requests or requests to restricted domains.
      ruleManagers = [];
    }
    if (channel.loadInfo.originAttributes.privateBrowsingId > 0) {
      ruleManagers = ruleManagers.filter(
        rm => rm.extension.privateBrowsingAllowed
      );
    }
    if (ruleManagers.length && !lazy.gMatchRequestsFromOtherExtensions) {
      const policy = channel.loadInfo.loadingPrincipal?.addonPolicy;
      if (policy) {
        ruleManagers = ruleManagers.filter(
          rm => rm.extension.policy === policy
        );
      }
    }
    let matchedRules;
    if (ruleManagers.length) {
      const evaluateRulesTimerId =
        Glean.extensionsApisDnr.evaluateRulesTime.start();
      try {
        const request = RequestDetails.fromChannelWrapper(channel);
        matchedRules = RequestEvaluator.evaluateRequest(request, ruleManagers);
      } finally {
        if (evaluateRulesTimerId !== undefined) {
          Glean.extensionsApisDnr.evaluateRulesTime.stopAndAccumulate(
            evaluateRulesTimerId
          );
        }
      }
      const evaluateRulesCount = ruleManagers.reduce(
        (sum, ruleManager) => sum + ruleManager.getRulesCount(),
        0
      );
      if (evaluateRulesCount > this.maxEvaluatedRulesCount) {
        Glean.extensionsApisDnr.evaluateRulesCountMax.set(evaluateRulesCount);
        this.maxEvaluatedRulesCount = evaluateRulesCount;
      }
    }
    // Cache for later. In case of redirects, _dnrMatchedRules may exist for
    // the pre-redirect HTTP channel, and is overwritten here again.
    channel._dnrMatchedRules = matchedRules;
  },

  /**
   * Applies the actions of the DNR rules.
   *
   * @typedef {ChannelWrapper & { _dnrMatchedRules?: MatchedRule[] }}
   *          ChannelWrapperViaDNR
   *
   * @param {ChannelWrapperViaDNR} channel
   * @returns {boolean} Whether to ignore any responses from the webRequest API.
   */
  onBeforeRequest(channel) {
    let matchedRules = channel._dnrMatchedRules;
    if (!matchedRules?.length) {
      return false;
    }
    // If a matched rule closes the channel, it is the sole match.
    const finalMatch = matchedRules[0];
    switch (finalMatch.rule.action.type) {
      case "block":
        this.applyBlock(channel, finalMatch);
        return true;
      case "redirect":
        this.applyRedirect(channel, finalMatch);
        return true;
      case "upgradeScheme":
        this.applyUpgradeScheme(channel);
        return true;
    }
    // If there are multiple rules, then it may be a combination of allow,
    // allowAllRequests and/or modifyHeaders.

    // "modifyHeaders" is handled by onBeforeSendHeaders/onHeadersReceived.
    // "allow" and "allowAllRequests" require no further action now.
    // "allowAllRequests" is applied to new requests in the future (if any)
    // through RequestEvaluator's findAncestorRuleOverride().

    return false;
  },

  onBeforeSendHeaders(channel) {
    let matchedRules = channel._dnrMatchedRules;
    if (!matchedRules?.length) {
      return;
    }
    ModifyRequestHeaders.maybeApplyModifyHeaders(channel, matchedRules);
  },

  onHeadersReceived(channel) {
    let matchedRules = channel._dnrMatchedRules;
    if (!matchedRules?.length) {
      return;
    }
    ModifyResponseHeaders.maybeApplyModifyHeaders(channel, matchedRules);
  },

  applyBlock(channel, matchedRule) {
    // TODO bug 1802259: Consider a DNR-specific reason.
    channel.cancel(
      Cr.NS_ERROR_ABORT,
      Ci.nsILoadInfo.BLOCKING_REASON_EXTENSION_WEBREQUEST
    );
    const addonId = matchedRule.ruleManager.extension.id;
    let properties = channel.channel.QueryInterface(Ci.nsIWritablePropertyBag);
    properties.setProperty("cancelledByExtension", addonId);
  },

  applyUpgradeScheme(channel) {
    // Request upgrade. No-op if already secure (i.e. https).
    channel.upgradeToSecure();
  },

  applyRedirect(channel, matchedRule) {
    // Ambiguity resolution order of redirect dict keys, consistent with Chrome:
    // - url > extensionPath > transform > regexSubstitution
    const redirect = matchedRule.rule.action.redirect;
    const extension = matchedRule.ruleManager.extension;
    const preRedirectUri = channel.finalURI;
    let redirectUri;
    if (redirect.url) {
      // redirect.url already validated by checkActionRedirect.
      redirectUri = Services.io.newURI(redirect.url);
    } else if (redirect.extensionPath) {
      redirectUri = extension.baseURI
        .mutate()
        .setPathQueryRef(redirect.extensionPath)
        .finalize();
    } else if (redirect.transform) {
      redirectUri = applyURLTransform(preRedirectUri, redirect.transform);
    } else if (redirect.regexSubstitution) {
      // Note: may throw if regexSubstitution results in an invalid redirect.
      // The error propagates up to handleRequest, which will just allow the
      // request to continue.
      redirectUri = applyRegexSubstitution(preRedirectUri, matchedRule);
    } else {
      // #checkActionRedirect ensures that the redirect action is non-empty.
    }

    if (preRedirectUri.equals(redirectUri)) {
      // URL did not change. Sometimes it is a bug in the extension, but there
      // are also cases where the result is unavoidable. E.g. redirect.transform
      // with queryTransform.removeParams that does not remove anything.
      // TODO: consider logging to help with debugging.
      return;
    }

    channel.redirectTo(redirectUri);

    let properties = channel.channel.QueryInterface(Ci.nsIWritablePropertyBag);
    properties.setProperty("redirectedByExtension", extension.id);

    let origin = channel.getRequestHeader("Origin");
    if (origin) {
      channel.setResponseHeader("Access-Control-Allow-Origin", origin);
      channel.setResponseHeader("Access-Control-Allow-Credentials", "true");
      channel.setResponseHeader("Access-Control-Max-Age", "0");
    }
  },
};

class RuleManager {
  constructor(extension) {
    this.extension = extension;
    this.sessionRules = this.makeRuleset(
      "_session",
      PRECEDENCE_SESSION_RULESET
    );
    this.dynamicRules = this.makeRuleset(
      "_dynamic",
      PRECEDENCE_DYNAMIC_RULESET
    );
    this.enabledStaticRules = [];

    this.hasBlockPermission = extension.hasPermission("declarativeNetRequest");
    this.hasRulesWithTabIds = false;
    this.hasRulesWithAllowAllRequests = false;
    this.totalRulesCount = 0;
  }

  get availableStaticRuleCount() {
    return Math.max(
      lazy.ExtensionDNRLimits.GUARANTEED_MINIMUM_STATIC_RULES -
        this.enabledStaticRules.reduce(
          (acc, ruleset) => acc + ruleset.rules.length,
          0
        ),
      0
    );
  }

  get enabledStaticRulesetIds() {
    return this.enabledStaticRules.map(ruleset => ruleset.id);
  }

  makeRuleset(
    rulesetId,
    rulesetPrecedence,
    rules = [],
    disabledRuleIds = null
  ) {
    return new Ruleset(
      rulesetId,
      rulesetPrecedence,
      rules,
      disabledRuleIds,
      this
    );
  }

  setSessionRules(validatedSessionRules) {
    let oldRulesCount = this.sessionRules.rules.length;
    let newRulesCount = validatedSessionRules.length;
    this.sessionRules.rules = validatedSessionRules;
    this.totalRulesCount += newRulesCount - oldRulesCount;
    this.hasRulesWithTabIds = !!this.sessionRules.rules.find(rule => {
      return rule.condition.tabIds || rule.condition.excludedTabIds;
    });
    this.#updateAllowAllRequestRules();
    NetworkIntegration.maybeUpdateTabIdChecker();
  }

  setDynamicRules(validatedDynamicRules) {
    let oldRulesCount = this.dynamicRules.rules.length;
    let newRulesCount = validatedDynamicRules.length;
    this.dynamicRules.rules = validatedDynamicRules;
    this.totalRulesCount += newRulesCount - oldRulesCount;
    this.#updateAllowAllRequestRules();
  }

  /**
   * Set the enabled static rulesets.
   *
   * @param {Array<{ id, rules, disabledRuleIds }>} enabledStaticRulesets
   *        Array of objects including the ruleset id and rules.
   *        The order of the rulesets in the Array is expected to
   *        match the order of the rulesets in the extension manifest.
   */
  setEnabledStaticRulesets(enabledStaticRulesets) {
    const rulesets = [];
    for (const [
      idx,
      { id, rules, disabledRuleIds },
    ] of enabledStaticRulesets.entries()) {
      rulesets.push(
        this.makeRuleset(
          id,
          idx + PRECEDENCE_STATIC_RULESETS_BASE,
          rules,
          disabledRuleIds
        )
      );
    }
    const countRules = rulesets =>
      rulesets.reduce((sum, ruleset) => sum + ruleset.rules.length, 0);
    const oldRulesCount = countRules(this.enabledStaticRules);
    const newRulesCount = countRules(rulesets);
    this.enabledStaticRules = rulesets;
    this.totalRulesCount += newRulesCount - oldRulesCount;
    this.#updateAllowAllRequestRules();
  }

  /**
   * Get the session scoped rules.
   *
   * @param {Array<integer>|null} ruleIds
            Optional array of rule IDs to return. By default, all the session
            scoped rules are returned.
   */
  getSessionRules(ruleIds = null) {
    if (!ruleIds) {
      return this.sessionRules.rules;
    }

    return this.sessionRules.rules.filter(rule => ruleIds.includes(rule.id));
  }

  /**
   * Get the dynamic rules.
   *
   * @param {Array<integer>|null} ruleIds
            Optional array of rule IDs to return. By default, all the dynamic
            rules are returned.
   */
  getDynamicRules(ruleIds = null) {
    if (!ruleIds) {
      return this.dynamicRules.rules;
    }

    return this.dynamicRules.rules.filter(rule => ruleIds.includes(rule.id));
  }

  getRulesCount() {
    return this.totalRulesCount;
  }

  #updateAllowAllRequestRules() {
    const filterAAR = rule => rule.action.type === "allowAllRequests";
    this.hasRulesWithAllowAllRequests =
      this.sessionRules.rules.some(filterAAR) ||
      this.dynamicRules.rules.some(filterAAR) ||
      this.enabledStaticRules.some(ruleset => ruleset.rules.some(filterAAR));
  }
}

function getRuleManager(extension, createIfMissing = true) {
  let ruleManager = gRuleManagers.find(rm => rm.extension === extension);
  if (!ruleManager && createIfMissing) {
    if (extension.hasShutdown) {
      throw new Error(
        `Error on creating new DNR RuleManager after extension shutdown: ${extension.id}`
      );
    }
    ruleManager = new RuleManager(extension);
    // The most recently installed extension gets priority, i.e. appears at the
    // start of the gRuleManagers list. It is not yet possible to determine the
    // installation time of a given Extension, so currently the last to
    // instantiate a RuleManager claims the highest priority.
    // TODO bug 1786059: order extensions by "installation time".
    gRuleManagers.unshift(ruleManager);
    if (gRuleManagers.length === 1) {
      // The first DNR registration.
      NetworkIntegration.register();
    }
  }
  return ruleManager;
}

function clearRuleManager(extension) {
  let i = gRuleManagers.findIndex(rm => rm.extension === extension);
  if (i !== -1) {
    gRuleManagers.splice(i, 1);
    NetworkIntegration.maybeUpdateTabIdChecker();
    if (gRuleManagers.length === 0) {
      // The last DNR registration.
      NetworkIntegration.unregister();
    }
  }
}

/**
 * Finds all matching rules for a request, optionally restricted to one
 * extension. Used by declarativeNetRequest.testMatchOutcome.
 *
 * @param {object|RequestDetails} request
 * @param {Extension} [extension]
 * @returns {MatchedRule[]}
 */
function getMatchedRulesForRequest(request, extension) {
  let requestDetails = new RequestDetails(request);
  const { requestURI, initiatorURI } = requestDetails;
  let ruleManagers = gRuleManagers;
  if (extension) {
    ruleManagers = ruleManagers.filter(rm => rm.extension === extension);
  }
  if (
    // NetworkIntegration.startDNREvaluation does not check requestURI, but we
    // do that here to filter URIs that are obviously disallowed. In practice,
    // anything other than http(s) is bogus and unsupported in DNR.
    isRestrictedPrincipalURI(requestURI) ||
    // Equivalent to NetworkIntegration.startDNREvaluation's channel.canModify
    // check, which excludes system requests and restricted domains.
    WebExtensionPolicy.isRestrictedURI(requestURI) ||
    (initiatorURI && WebExtensionPolicy.isRestrictedURI(initiatorURI)) ||
    isRestrictedPrincipalURI(initiatorURI)
  ) {
    ruleManagers = [];
  }
  // While this simulated request is not really from another extension, apply
  // the same access control checks from NetworkIntegration.startDNREvaluation
  // for consistency.
  if (
    !lazy.gMatchRequestsFromOtherExtensions &&
    initiatorURI?.schemeIs("moz-extension")
  ) {
    const extUuid = initiatorURI.host;
    ruleManagers = ruleManagers.filter(rm => rm.extension.uuid === extUuid);
  }
  return RequestEvaluator.evaluateRequest(requestDetails, ruleManagers);
}

/**
 * Runs before any webRequest event is notified. Headers may be modified, but
 * the request should not be canceled (see handleRequest instead).
 *
 * @param {ChannelWrapper} channel
 * @param {string} kind - The name of the webRequest event.
 */
function beforeWebRequestEvent(channel, kind) {
  try {
    switch (kind) {
      case "onBeforeRequest":
        NetworkIntegration.startDNREvaluation(channel);
        break;
      case "onBeforeSendHeaders":
        NetworkIntegration.onBeforeSendHeaders(channel);
        break;
      case "onHeadersReceived":
        NetworkIntegration.onHeadersReceived(channel);
        break;
    }
  } catch (e) {
    Cu.reportError(e);
  }
}

/**
 * Applies matching DNR rules, some of which may potentially cancel the request.
 *
 * @param {ChannelWrapperViaDNR} channel
 * @param {string} kind - The name of the webRequest event.
 * @returns {boolean} Whether to ignore any responses from the webRequest API.
 */
function handleRequest(channel, kind) {
  try {
    if (kind === "onBeforeRequest") {
      return NetworkIntegration.onBeforeRequest(channel);
    }
  } catch (e) {
    Cu.reportError(e);
  }
  return false;
}

async function initExtension(extension) {
  // These permissions are NOT an OptionalPermission, so their status can be
  // assumed to be constant for the lifetime of the extension.
  if (
    extension.hasPermission("declarativeNetRequest") ||
    extension.hasPermission("declarativeNetRequestWithHostAccess")
  ) {
    if (extension.hasShutdown) {
      throw new Error(
        `Aborted ExtensionDNR.initExtension call, extension "${extension.id}" is not active anymore`
      );
    }
    extension.once("shutdown", () => clearRuleManager(extension));
    await lazy.ExtensionDNRStore.initExtension(extension);
  }
}

function ensureInitialized(extension) {
  return (extension._dnrReady ??= initExtension(extension));
}

function validateManifestEntry(extension) {
  const ruleResourcesArray =
    extension.manifest.declarative_net_request.rule_resources;

  const getWarningMessage = msg =>
    `Warning processing declarative_net_request: ${msg}`;

  const { MAX_NUMBER_OF_STATIC_RULESETS } = lazy.ExtensionDNRLimits;
  if (ruleResourcesArray.length > MAX_NUMBER_OF_STATIC_RULESETS) {
    extension.manifestWarning(
      getWarningMessage(
        `Static rulesets are exceeding the MAX_NUMBER_OF_STATIC_RULESETS limit (${MAX_NUMBER_OF_STATIC_RULESETS}).`
      )
    );
  }

  const seenRulesetIds = new Set();
  const seenRulesetPaths = new Set();
  const duplicatedRulesetIds = [];
  const duplicatedRulesetPaths = [];
  for (const [idx, { id, path }] of ruleResourcesArray.entries()) {
    if (seenRulesetIds.has(id)) {
      duplicatedRulesetIds.push({ idx, id });
    }
    if (seenRulesetPaths.has(path)) {
      duplicatedRulesetPaths.push({ idx, path });
    }
    seenRulesetIds.add(id);
    seenRulesetPaths.add(path);
  }

  if (duplicatedRulesetIds.length) {
    const errorDetails = duplicatedRulesetIds
      .map(({ idx, id }) => `"${id}" at index ${idx}`)
      .join(", ");
    extension.manifestWarning(
      getWarningMessage(
        `Static ruleset ids should be unique, duplicated ruleset ids: ${errorDetails}.`
      )
    );
  }

  if (duplicatedRulesetPaths.length) {
    // NOTE: technically Chrome allows duplicated paths without any manifest
    // validation warnings or errors, but if this happens it not unlikely to be
    // actually a mistake in the manifest that may have been missed.
    //
    // In Firefox we decided to allow the same behavior to avoid introducing a chrome
    // incompatibility, but we still warn about it to avoid extension developers
    // to investigate more easily issue that may be due to duplicated rulesets
    // paths.
    const errorDetails = duplicatedRulesetPaths
      .map(({ idx, path }) => `"${path}" at index ${idx}`)
      .join(", ");
    extension.manifestWarning(
      getWarningMessage(
        `Static rulesets paths are not unique, duplicated ruleset paths: ${errorDetails}.`
      )
    );
  }

  const { MAX_NUMBER_OF_ENABLED_STATIC_RULESETS } = lazy.ExtensionDNRLimits;

  const enabledRulesets = ruleResourcesArray.filter(rs => rs.enabled);
  if (enabledRulesets.length > MAX_NUMBER_OF_ENABLED_STATIC_RULESETS) {
    const exceedingRulesetIds = enabledRulesets
      .slice(MAX_NUMBER_OF_ENABLED_STATIC_RULESETS)
      .map(ruleset => `"${ruleset.id}"`)
      .join(", ");
    extension.manifestWarning(
      getWarningMessage(
        `Enabled static rulesets are exceeding the MAX_NUMBER_OF_ENABLED_STATIC_RULESETS limit (${MAX_NUMBER_OF_ENABLED_STATIC_RULESETS}): ${exceedingRulesetIds}.`
      )
    );
  }
}

async function updateEnabledStaticRulesets(extension, updateRulesetOptions) {
  await ensureInitialized(extension);
  await lazy.ExtensionDNRStore.updateEnabledStaticRulesets(
    extension,
    updateRulesetOptions
  );
}

async function updateDynamicRules(extension, updateRuleOptions) {
  await ensureInitialized(extension);
  await lazy.ExtensionDNRStore.updateDynamicRules(extension, updateRuleOptions);
}

async function updateStaticRules(extension, updateStaticRulesOptions) {
  await ensureInitialized(extension);
  await lazy.ExtensionDNRStore.updateStaticRules(
    extension,
    updateStaticRulesOptions
  );
}

async function getDisabledRuleIds(extension, rulesetId) {
  await ensureInitialized(extension);
  return lazy.ExtensionDNRStore.getDisabledRuleIds(extension, rulesetId);
}

// exports used by the DNR API implementation.
export const ExtensionDNR = {
  RuleValidator,
  RuleQuotaCounter,
  clearRuleManager,
  ensureInitialized,
  getMatchedRulesForRequest,
  getDisabledRuleIds,
  getRuleManager,
  updateDynamicRules,
  updateEnabledStaticRulesets,
  updateStaticRules,
  validateManifestEntry,
  beforeWebRequestEvent,
  handleRequest,
};

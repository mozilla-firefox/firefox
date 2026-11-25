/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExtensionDNR: "resource://gre/modules/ExtensionDNR.sys.mjs",
});

var { ExtensionError } = ExtensionUtils;

const PREF_DNR_FEEDBACK = "extensions.dnr.feedback";
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "dnrFeedbackEnabled",
  PREF_DNR_FEEDBACK,
  false
);

function ensureDNRFeedbackEnabled(apiName) {
  if (!dnrFeedbackEnabled) {
    throw new ExtensionError(
      `${apiName} is only available when the "${PREF_DNR_FEEDBACK}" preference is set to true.`
    );
  }
}

this.declarativeNetRequest = class extends ExtensionAPI {
  onManifestEntry(entryName) {
    if (entryName === "declarative_net_request") {
      ExtensionDNR.validateManifestEntry(this.extension);
    }
  }

  onShutdown() {
    ExtensionDNR.clearRuleManager(this.extension);
  }

  getAPI() {
    const { extension } = this;

    return {
      declarativeNetRequest: {
        updateDynamicRules({ removeRuleIds, addRules }) {
          return ExtensionDNR.updateDynamicRules(extension, {
            removeRuleIds,
            addRules,
          });
        },

        updateSessionRules({ removeRuleIds, addRules }) {
          const ruleManager = ExtensionDNR.getRuleManager(extension);
          let ruleValidator = new ExtensionDNR.RuleValidator(
            ruleManager.getSessionRules(),
            { isSessionRuleset: true }
          );
          if (removeRuleIds) {
            ruleValidator.removeRuleIds(removeRuleIds);
          }
          if (addRules) {
            ruleValidator.addRules(addRules);
          }
          let failures = ruleValidator.getFailures();
          if (failures.length) {
            throw new ExtensionError(failures[0].message);
          }
          let validatedRules = ruleValidator.getValidatedRules();
          let ruleQuotaCounter = new ExtensionDNR.RuleQuotaCounter(
            "MAX_NUMBER_OF_SESSION_RULES"
          );
          ruleQuotaCounter.tryAddRules("_session", validatedRules);
          ruleManager.setSessionRules(validatedRules);
        },

        async getEnabledRulesets() {
          await ExtensionDNR.ensureInitialized(extension);
          const ruleManager = ExtensionDNR.getRuleManager(extension);
          return ruleManager.enabledStaticRulesetIds;
        },

        async getAvailableStaticRuleCount() {
          await ExtensionDNR.ensureInitialized(extension);
          const ruleManager = ExtensionDNR.getRuleManager(extension);
          return ruleManager.availableStaticRuleCount;
        },

        updateEnabledRulesets({ disableRulesetIds, enableRulesetIds }) {
          return ExtensionDNR.updateEnabledStaticRulesets(extension, {
            disableRulesetIds,
            enableRulesetIds,
          });
        },

        updateStaticRules({ rulesetId, disableRuleIds, enableRuleIds }) {
          return ExtensionDNR.updateStaticRules(extension, {
            rulesetId,
            disableRuleIds,
            enableRuleIds,
          });
        },

        async getDisabledRuleIds({ rulesetId }) {
          return ExtensionDNR.getDisabledRuleIds(extension, rulesetId);
        },

        async getDynamicRules(details) {
          await ExtensionDNR.ensureInitialized(extension);
          return ExtensionDNR.getRuleManager(extension).getDynamicRules(
            details?.ruleIds
          );
        },

        getSessionRules(details) {
          // ruleManager.getSessionRules() returns an array of Rule instances.
          // When these are structurally cloned (to send them to the child),
          // the enumerable public fields of the class instances are copied to
          // plain objects, as desired.
          return ExtensionDNR.getRuleManager(extension).getSessionRules(
            details?.ruleIds
          );
        },

        isRegexSupported(regexOptions) {
          const {
            regex: regexFilter,
            isCaseSensitive: isUrlFilterCaseSensitive,
            // requireCapturing: is ignored, as it does not affect validation.
          } = regexOptions;

          let ruleValidator = new ExtensionDNR.RuleValidator([]);
          ruleValidator.addRules([
            {
              id: 1,
              condition: { regexFilter, isUrlFilterCaseSensitive },
              action: { type: "allow" },
            },
          ]);
          let failures = ruleValidator.getFailures();
          if (failures.length) {
            // While the UnsupportedRegexReason enum has more entries than just
            // "syntaxError" (e.g. also "memoryLimitExceeded"), our validation
            // is currently very permissive, and therefore the only
            // distinguishable error is "syntaxError".
            return { isSupported: false, reason: "syntaxError" };
          }
          return { isSupported: true };
        },

        async testMatchOutcome(request, options) {
          ensureDNRFeedbackEnabled("declarativeNetRequest.testMatchOutcome");
          let { url, initiator, ...req } = request;
          req.requestURI = Services.io.newURI(url);
          if (initiator) {
            req.initiatorURI = Services.io.newURI(initiator);
            if (req.initiatorURI.schemeIs("data")) {
              // data:-URIs are always opaque, i.e. a null principal. We should
              // therefore ignore them here.
              // ExtensionDNR's NetworkIntegration.startDNREvaluation does not
              // encounter data:-URIs because opaque principals are mapped to a
              // null initiatorURI. For consistency, we do the same here.
              req.initiatorURI = null;
            }
            // "initiatorURI" corresponds to the triggering principal, but the
            // loading principal ("documentURI") is used instead. The two are
            // often equal, with some exceptions. Notably, null for main_frame.
            if (req.type !== "main_frame") {
              req.documentURI = req.initiatorURI;
            }
          }
          const matchedRules = ExtensionDNR.getMatchedRulesForRequest(
            req,
            options?.includeOtherExtensions ? null : extension
          ).map(matchedRule => {
            // Converts an internal MatchedRule instance to an object described
            // by the "MatchedRule" type in declarative_net_request.json.
            const result = {
              ruleId: matchedRule.rule.id,
              rulesetId: matchedRule.ruleset.id,
            };
            if (matchedRule.ruleManager.extension !== extension) {
              result.extensionId = matchedRule.ruleManager.extension.id;
            }
            return result;
          });
          return { matchedRules };
        },
      },
    };
  }
};

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Utilities for looking up error configurations and resolving dynamic content.
 *
 * Some error configurations require runtime data (e.g., hostname, certificate
 * validity dates, connectivity status). This module provides resolver functions
 * that inject runtime context into static configurations.
 */

import {
  getErrorConfig,
  isErrorSupported,
} from "chrome://global/content/errors/error-registry.mjs";

/**
 * Resolve browser error signals to a canonical registry ID.
 *
 * Handles resolution scenarios in priority order:
 * 1. System state overrides (no connectivity, VPN failure)
 * 2. Direct registry lookup by errorCodeString (C++ error strings)
 * 3. Fallback to a shared config when errorCodeString is unregistered
 * 4. Direct registry lookup by gErrorCode (URL-derived error codes)
 *
 * @param {object} signals
 * @param {string} signals.errorCodeString - Raw error string from the security/network layer
 * @param {string} signals.gErrorCode - URL-derived error code (e= parameter)
 * @param {boolean} signals.noConnectivity - System has no network connectivity
 * @param {boolean} [signals.vpnError] - The proxy failure is the VPN's
 * @returns {string|null} Canonical registry ID, or null if the error has no
 *   Felt Privacy config.
 */
export function resolveErrorID({
  errorCodeString,
  gErrorCode,
  noConnectivity,
  vpnError = false,
}) {
  // System state overrides.
  // Currently these are all network-related.
  if (noConnectivity) {
    return "NS_ERROR_OFFLINE";
  }
  if (vpnError) {
    return "vpnFailure";
  }

  // Default: use errorCodeString if it is set and registered. This value comes
  // from getFailedCertSecurityInfo() or getNetErrorInfo() and identifies the
  // specific error.
  if (errorCodeString && isErrorSupported(errorCodeString)) {
    return errorCodeString;
  }

  // In some cases, errorCodeString will be set but not registered. This can
  // be done on purpose to allow many different yet similar errors to share a
  // config in the error registry. For example, if we see an unregistered error
  // where gErrorCode is "nssFailure2", we use the nssFailure2 registry entry.
  if (errorCodeString) {
    if (gErrorCode !== "nssFailure2" || !isErrorSupported("nssFailure2")) {
      return null;
    }
  }

  // In some cases, errorCodeString will not be set. Use the code in
  // gErrorCode, which is parsed from the URL e= parameter and set by
  // nsDocShell, as a fallback.
  if (gErrorCode && isErrorSupported(gErrorCode)) {
    return gErrorCode;
  }

  return null;
}

/**
 * Check if an error has no action the user can take to fix it.
 *
 * @param {string} id - The error id to check
 * @returns {boolean} True if the error has no user fix
 */
export function errorHasNoUserFix(id) {
  const config = getErrorConfig(id);
  return config ? config.hasNoUserFix === true : false;
}

/**
 * Check if an error is supported by the Felt Privacy v1 experience.
 *
 * @param {string} id - The error id to check
 * @returns {boolean} True if the error has a configuration
 */
export function isFeltPrivacySupported(id) {
  const config = getErrorConfig(id);
  return !!config;
}

/**
 * Resolve l10n arguments by injecting runtime context.
 *
 * @param {object | null} l10nConfig - The l10n config with { dataL10nId, dataL10nArgs }
 * @param {object} l10nArgValues - Context from the environment during runtime (hostname, errorInfo, etc.)
 * @returns {object | null} Resolved l10n config with dataL10nArgs filled in
 */
export function resolveL10nArgs(l10nConfig, l10nArgValues) {
  if (!l10nConfig?.dataL10nArgs) {
    return l10nConfig;
  }

  const values = {
    hostname: l10nArgValues.hostname,
    path: l10nArgValues.filePath,
    date: Date.now(),
    now: l10nArgValues.now ?? Date.now(),
    errorMessage: l10nArgValues.errorInfo?.errorMessage ?? "",
    errorCodeString: l10nArgValues.errorInfo?.errorCodeString ?? "",
    validHosts: l10nArgValues.domainMismatchNames ?? "",
    mitm: l10nArgValues.mitmName ?? "",
    responsestatus: l10nArgValues.errorInfo?.responseStatus ?? 0,
    responsestatustext: l10nArgValues.errorInfo?.responseStatusText ?? "",
  };

  if (typeof l10nConfig.dataL10nId === "function") {
    l10nConfig.dataL10nId = l10nConfig.dataL10nId(l10nArgValues);
  }

  for (const [key, value] of Object.entries(l10nConfig.dataL10nArgs)) {
    if (value === null || value === "") {
      l10nConfig.dataL10nArgs[key] = values[key];
    } else if (typeof value === "function") {
      l10nConfig.dataL10nArgs[key] = value(l10nArgValues);
    }
  }
  return l10nConfig;
}

/**
 * Resolve an array of l10n arguments by injecting runtime context.
 *
 * @param {Array | null} l10nConfig - The l10n config(s) with { dataL10nId, dataL10nArgs }
 * @param {object} l10nArgValues - Context from the environment during runtime (hostname, errorInfo, etc.)
 * @returns {Array | null} Resolved l10n config with dataL10nArgs filled in
 */
export function resolveManyL10nArgs(l10nConfigs, l10nArgValues) {
  if (!l10nConfigs) {
    return null;
  }
  for (let i = 0; i < l10nConfigs.length; i++) {
    l10nConfigs[i] = resolveL10nArgs(l10nConfigs[i], l10nArgValues);
  }
  return l10nConfigs;
}

/**
 * Resolve description parts by calling resolver functions for dynamic content.
 *
 * @param {Array|Function} descriptionParts - Static parts array or resolver function
 * @param {object} l10nArgValues - Context from the environment during runtime { noConnectivity, hostname, errorInfo }
 * @returns {Array} Resolved description parts
 */
export function resolveDescriptionParts(descriptionParts, l10nArgValues) {
  if (!descriptionParts) {
    return [];
  }

  if (typeof descriptionParts === "function") {
    return descriptionParts(l10nArgValues);
  }

  // Static parts - resolve any l10n args
  return descriptionParts.map(part => resolveL10nArgs(part, l10nArgValues));
}

/**
 * Resolve the advanced section configuration.
 *
 * @param {object | null} advancedConfig - The advanced section config
 * @param {object} l10nArgValues - Context from the environment during runtime
 * @returns {object | null} Resolved advanced config
 */
export function resolveAdvancedConfig(advancedConfig, l10nArgValues) {
  if (!advancedConfig) {
    return null;
  }

  const resolved = { ...advancedConfig };
  ["whyDangerous", "whatCanYouDo", "learnMore"].forEach(key => {
    if (resolved[key]) {
      resolved[key] = resolveL10nArgs(advancedConfig[key], l10nArgValues);
    }
  });
  return resolved;
}

function resolveCustomNetError(customNetError, l10nArgValues) {
  if (!customNetError) {
    return customNetError;
  }
  const resolved = { ...customNetError };
  if (typeof resolved.whatCanYouDoItems === "function") {
    resolved.whatCanYouDoItems = resolved.whatCanYouDoItems(l10nArgValues);
  }
  if (customNetError.whatCanYouDoL10nArgs) {
    const argsClone = {
      dataL10nArgs: { ...customNetError.whatCanYouDoL10nArgs },
    };
    resolveL10nArgs(argsClone, l10nArgValues);
    resolved.whatCanYouDoL10nArgs = argsClone.dataL10nArgs;
  }
  return resolved;
}

/**
 * Get a fully resolved error configuration with runtime context applied.
 *
 * @param {string} id - The error id to look up
 * @param {object} l10nArgValues - Context from the environment during runtime { hostname, errorInfo, noConnectivity, showOSXPermissionWarning, offline }
 * @returns {object} Fully resolved error configuration
 */
export function getResolvedErrorConfig(id, l10nArgValues) {
  const baseConfig = getErrorConfig(id);
  if (!baseConfig) {
    return {};
  }

  const introContentHandler = Array.isArray(baseConfig.introContent)
    ? resolveManyL10nArgs
    : resolveL10nArgs;
  return {
    ...baseConfig,
    introContent: introContentHandler(baseConfig.introContent, l10nArgValues),
    shortDescription: resolveL10nArgs(
      baseConfig.shortDescription,
      l10nArgValues
    ),
    descriptionParts: resolveDescriptionParts(
      baseConfig.descriptionParts,
      l10nArgValues
    ),
    advanced: resolveAdvancedConfig(baseConfig.advanced, l10nArgValues),
    customNetError: resolveCustomNetError(
      baseConfig.customNetError,
      l10nArgValues
    ),
  };
}

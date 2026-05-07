/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.preferences.Branch
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.FeatureFlags

/**
 * Robowolf debloat: applies Gecko prefs that silence outgoing background HTTP requests
 * to Mozilla services that fire even before the user signs in, plus a hardened set of
 * privacy defaults.
 *
 * Three pref groups are pushed at startup:
 *  - "Services" prefs (gated by [FeatureFlags.ROBOWOLF_DEBLOAT_SERVICES]) cover the
 *    captive-portal probe, network connectivity service, geo-search defaults, Normandy.
 *  - "Endpoints" prefs (gated by [FeatureFlags.ROBOWOLF_DEBLOAT_ENDPOINTS]) cover the
 *    search-engine list updater, AMO discovery / system-addon channels, and the Gecko
 *    in-app updater (redundant on Android since updates ship via the store).
 *  - "Extra privacy" prefs (gated by [FeatureFlags.ROBOWOLF_DEBLOAT_EXTRA_PRIVACY]) cover
 *    PPA, ping-centre, Beacon, hyperlink-auditing, prefetch / predictor, GMP autoupdates,
 *    region updates, plus DNT/GPC/punycode hardening.
 *
 * Gecko keeps its own pref database independent of Fenix's [Settings]. The
 * [BrowserPreferencesRuntime] entry points (exposed on [Engine]) push values into the
 * default branch so they take effect at startup without persisting in user prefs.
 */
@OptIn(ExperimentalAndroidComponentsApi::class)
object RobowolfPrivacyPrefs {

    private val logger = Logger("RobowolfPrivacyPrefs")

    /**
     * Background-noise Gecko prefs gated by [FeatureFlags.ROBOWOLF_DEBLOAT_SERVICES].
     */
    private val servicesPrefs: List<Pair<String, Boolean>> = listOf(
        // Disables the periodic HEAD request to detectportal.firefox.com.
        "network.captive-portal-service.enabled" to false,
        // Disables the periodic /success.txt connectivity probe.
        "network.connectivity-service.enabled" to false,
        // Removes geo-specific search-engine preferences fetch.
        "browser.search.geoSpecificDefaults" to false,
        // Stops Gecko's privacy-enhancing remote-config experiment system (Normandy).
        "app.normandy.enabled" to false,
        // Stops the Gecko crash-ping-on-startup heuristic.
        "toolkit.telemetry.unifiedIsOptIn" to true,
    )

    /**
     * Default-endpoint Gecko prefs gated by [FeatureFlags.ROBOWOLF_DEBLOAT_ENDPOINTS].
     * These stop background fetches to Mozilla services that fire even with no user activity.
     */
    private val endpointPrefs: List<Pair<String, Boolean>> = listOf(
        // Disable the search-engine list / configuration update fetch.
        "browser.search.update" to false,
        // Disable the system-addon updater (Mozilla can otherwise push code via this channel).
        "extensions.systemAddon.update.enabled" to false,
        // Disable the add-on discovery catalog cache (no calls for the AMO discovery pane).
        "extensions.getAddons.cache.enabled" to false,
        // Disable Gecko's own app updater - Android distributes via Play Store / F-Droid.
        "app.update.enabled" to false,
        "app.update.background.scheduling.enabled" to false,
    )

    private val endpointStringPrefs: List<Pair<String, String>> = listOf(
        // Empty out the addon discovery API URL so any code path that reaches it gets nowhere.
        "extensions.getAddons.discovery.api_url" to "",
        // Empty out the addon background update URL.
        "extensions.update.background.url" to "",
    )

    /**
     * Extra privacy hardening Gecko prefs gated by [FeatureFlags.ROBOWOLF_DEBLOAT_EXTRA_PRIVACY].
     * Curated to silence background analytics surfaces without breaking site compatibility.
     */
    private val extraPrivacyPrefs: List<Pair<String, Boolean>> = listOf(
        // Privacy-Preserving Attribution: opt out of the new ad-attribution API entirely.
        "dom.private-attribution.submission.enabled" to false,
        // ping-centre: a separate telemetry channel from Glean (used by Activity Stream/New Tab).
        "browser.ping-centre.telemetry" to false,
        // Beacon API: sites use it to send analytics on page-leave. Disabling blocks that channel.
        "beacon.enabled" to false,
        // Hyperlink auditing: the <a ping> attribute used for click-tracking by ad networks.
        "browser.send_pings" to false,
        // Network predictor / prefetch: idle background fetches that leak browsing intent.
        "network.predictor.enabled" to false,
        "network.prefetch-next" to false,
        "network.dns.disablePrefetch" to true,
        // Region detection: stops Mozilla's geo-based "set default region" updates.
        "browser.region.update.enabled" to false,
        // GMP plugin auto-update: stops silent CDM updates (Widevine, OpenH264).
        // The plugins still load if already installed; they just don't get refreshed silently.
        "media.gmp-widevinecdm.autoupdate" to false,
        "media.gmp-gmpopenh264.autoupdate" to false,
        // DNT header: send Do-Not-Track on every request. Most sites ignore it but the signal is set.
        "privacy.donottrackheader.enabled" to true,
        // GPC: Global Privacy Control. Has legal effect under CCPA/GDPR-derived laws.
        "privacy.globalprivacycontrol.enabled" to true,
        "privacy.globalprivacycontrol.functionality.enabled" to true,
        // Punycode: always show internationalized domains as punycode in the URL bar so that
        // unicode-spoofing phishing domains (e.g. Cyrillic 'а' looking like Latin 'a') are visible.
        "network.IDN_show_punycode" to true,
    )

    /**
     * Pushes the configured privacy prefs to [engine].
     * No-op when all fork flags are off.
     */
    fun applyTo(engine: Engine) {
        if (FeatureFlags.ROBOWOLF_DEBLOAT_SERVICES) {
            servicesPrefs.forEach { (pref, value) -> applyBool(engine, pref, value) }
        }
        if (FeatureFlags.ROBOWOLF_DEBLOAT_ENDPOINTS) {
            endpointPrefs.forEach { (pref, value) -> applyBool(engine, pref, value) }
            endpointStringPrefs.forEach { (pref, value) -> applyString(engine, pref, value) }
        }
        if (FeatureFlags.ROBOWOLF_DEBLOAT_EXTRA_PRIVACY) {
            extraPrivacyPrefs.forEach { (pref, value) -> applyBool(engine, pref, value) }
        }
    }

    private fun applyBool(engine: Engine, pref: String, value: Boolean) {
        engine.setBrowserPref(
            pref = pref,
            value = value,
            branch = Branch.DEFAULT,
            onSuccess = { logger.debug("Applied $pref=$value") },
            onError = { error -> logger.warn("Failed to apply $pref=$value", error) },
        )
    }

    private fun applyString(engine: Engine, pref: String, value: String) {
        engine.setBrowserPref(
            pref = pref,
            value = value,
            branch = Branch.DEFAULT,
            onSuccess = { logger.debug("Applied $pref='$value'") },
            onError = { error -> logger.warn("Failed to apply $pref='$value'", error) },
        )
    }
}

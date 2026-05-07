/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

/**
 * A single source for setting feature flags that are mostly based on build type.
 */
object FeatureFlags {

    /**
     * Enables custom extension collection feature,
     * This feature does not only depend on this flag. It requires the AMO collection override to
     * be enabled which is behind the Secret Settings.
     * */
    val customExtensionCollectionFeature = Config.channel.isNightlyOrDebug || Config.channel.isBeta

    /**
     * Pull-to-refresh allows you to pull the web content down far enough to have the page to
     * reload.
     */
    const val PULL_TO_REFRESH_ENABLED = true

    /**
     * Allows users to enable Firefox Suggest.
     */
    const val FX_SUGGEST = true

    /**
     * Disables the Onboarding feature for debug builds by default. Set this to `true` if you need
     * to access the Onboarding feature for development purposes.
     *
     * ⚠️ DO NOT MODIFY THIS FLAG IN PRODUCTION.
     */
    val onboardingFeatureEnabled = !Config.channel.isDebug

    /**
     * Enables Firefox Labs.
     */
    const val FIREFOX_LABS = false

    /**
     * Robowolf fork-level kill switch for sponsored / monetized content surfaces:
     * Pocket recommendations, Pocket sponsored stories, sponsored top-site tiles (Contile),
     * and Firefox Suggest (sponsored + non-sponsored). When true, every related feature
     * defaults to off and its feature-flag gate returns false, so a fresh install never
     * contacts Mozilla's recommendation, Contile, or Suggest endpoints by default.
     *
     * Users can still re-enable individual surfaces via the existing Settings UI; this
     * only changes defaults and the build-time feature-flag values.
     */
    const val ROBOWOLF_DEBLOAT_SPONSORED = true

    /**
     * Robowolf fork-level kill switch for Mozilla telemetry pipelines:
     * Glean technical/interaction data, the daily usage ping, Nimbus experiments
     * participation, and Adjust marketing/attribution. When true:
     *  - Fresh installs default all four to off,
     *  - Glean is initialized with `uploadEnabled = false` regardless of user pref,
     *  - The Adjust metrics service is never started.
     *
     * The user-facing toggles in Settings → Data Choices remain visible so a user
     * who explicitly opts back in still triggers the existing flows; the init-time
     * forced-off ensures that no telemetry leaves the device before the user has
     * a chance to interact with onboarding.
     */
    const val ROBOWOLF_DEBLOAT_TELEMETRY = true

    /**
     * Robowolf fork-level kill switch for outgoing background requests to Mozilla services
     * that fire even before the user signs in. When true:
     *  - Nimbus skips remote experiment fetches (no calls to firefox.settings.services.mozilla.com),
     *    but the local `initial_experiments.json` is still loaded so feature flags resolve.
     *  - Captive-portal probing is disabled at the Gecko pref level (no calls to
     *    detectportal.firefox.com on every network change).
     *  - Network connectivity service polling is disabled.
     *
     * FxA Sync and the Mozilla Push (autopush) registration are intentionally NOT touched here;
     * they only contact Mozilla after the user explicitly signs in, so the default install is
     * already silent. Disabling them would also break the Sync UI for users who actively want it.
     */
    const val ROBOWOLF_DEBLOAT_SERVICES = true

    /**
     * Robowolf fork-level kill switch for default Mozilla endpoints that Gecko polls in the
     * background even with no user activity. When true:
     *  - Search-engine list updates are disabled (no fetches from `search.config.mozilla.com`).
     *  - Add-on discovery / catalog cache / system-addon updater are disabled
     *    (no fetches from `services.addons.mozilla.org` for the discovery pane or system addons;
     *    the user-installed-addon blocklist is left ON for security).
     *  - The Gecko app-update channel is disabled (Android updates ship via Play Store / F-Droid
     *    anyway, so the Gecko-side updater is dead weight that can still phone home).
     *
     * Safe Browsing and DoH defaults are intentionally NOT touched here:
     *  - Safe Browsing list updates remain ON because disabling them strips phishing/malware
     *    protection. The Safe Browsing lookup itself is hash-prefix only.
     *  - The DoH Cloudflare/NextDNS providers default to Mozilla's TRR-program wrappers, which
     *    have stronger no-logging guarantees than the vanilla endpoints; swapping to bare
     *    Cloudflare/Quad9 would be a privacy regression, not an improvement.
     */
    const val ROBOWOLF_DEBLOAT_ENDPOINTS = true

    /**
     * Robowolf fork-level kill switch for onboarding nags, growth promos, and upsells:
     *  - The full onboarding flow on first launch (FxA sign-in card, marketing-data prompt,
     *    "set as default browser" card, etc.) is skipped.
     *  - The recurring "Set as default browser" prompt that fires on later launches is skipped.
     *  - The custom in-app rating / review prompt is disabled.
     *  - Microsurveys (Mozilla research questionnaires that pop in the toolbar) are disabled.
     *  - The continuous (multi-day) onboarding scheduler is disabled.
     *  - The IP Protection (Mozilla VPN) availability flag is forced off so the upsell
     *    surfaces never appear.
     *
     * Each underlying user-facing toggle remains; this only changes default visibility so a
     * fresh install is silent. Existing users who previously dismissed/accepted any surface
     * are unaffected by the prefs change because the gate is the FeatureFlag itself.
     */
    const val ROBOWOLF_DEBLOAT_PROMOS = true

    /**
     * Robowolf fork-level privacy hardening: pushes a curated set of Gecko prefs that turn
     * off background analytics surfaces and harden user-visible privacy posture. When true:
     *  - Privacy-Preserving Attribution (PPA) is disabled.
     *  - The "ping-centre" telemetry channel and Beacon API analytics calls are disabled.
     *  - Hyperlink auditing (<a ping>) is disabled.
     *  - Network prediction, prefetch, and DNS prefetch are disabled (no idle background fetches).
     *  - GMP plugin auto-update (Widevine, OpenH264) is disabled.
     *  - Region detection updates are disabled (no geo-based "default for your region" calls).
     *  - DNT and Global Privacy Control headers are switched on by default.
     *  - Punycode display is forced on so unicode-spoofing phishing domains are visible.
     *
     * These are pure defaults — site compatibility is not affected, only background noise
     * and signaling that has no user-visible benefit.
     */
    const val ROBOWOLF_DEBLOAT_EXTRA_PRIVACY = true

    /**
     * Robowolf fork-level privacy posture defaults — user-visible toggles that ship with a
     * stricter starting position than upstream Fenix. When true, fresh installs default to:
     *  - Enhanced Tracking Protection set to Strict (was Standard).
     *  - HTTPS-Only Mode enabled for all tabs (was off).
     *
     * The user can still drop to Standard ETP or disable HTTPS-Only in Settings; this only
     * changes the first-launch posture. Existing users who already set their own preference
     * are not affected.
     */
    const val ROBOWOLF_HARDEN_PRIVACY_DEFAULTS = true
}

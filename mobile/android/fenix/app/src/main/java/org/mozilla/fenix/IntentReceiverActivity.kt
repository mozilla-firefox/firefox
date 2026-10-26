/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.os.StrictMode
import android.view.ContextThemeWrapper
import androidx.annotation.VisibleForTesting
import mozilla.components.feature.intent.ext.sanitize
import mozilla.components.feature.intent.processing.IntentProcessor
import mozilla.components.feature.intent.processing.TabIntentProcessor.Companion.EXTRA_APP_LINK_LAUNCH_TYPE
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.EXTRA_ACTIVITY_REFERRER_CATEGORY
import mozilla.components.support.utils.EXTRA_ACTIVITY_REFERRER_PACKAGE
import mozilla.components.support.utils.INTENT_TYPE_PDF
import mozilla.components.support.utils.ext.packageManagerCompatHelper
import mozilla.components.support.utils.toSafeIntent
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.SyncAuth
import org.mozilla.fenix.HomeActivity.Companion.PRIVATE_BROWSING_MODE
import org.mozilla.fenix.components.IntentProcessorType
import org.mozilla.fenix.components.getType
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isIntentInternal
import org.mozilla.fenix.perf.MarkersActivityLifecycleCallbacks
import org.mozilla.fenix.perf.StartupTimeline
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.shortcut.NewTabShortcutIntentProcessor
import org.mozilla.fenix.shortcut.UninstallShortcutIntentProcessor

/** Processes incoming intents and sends them to the corresponding activity. */
class IntentReceiverActivity : Activity() {

    private val logger = Logger("IntentReceiverActivity")

    @VisibleForTesting
    override fun onCreate(savedInstanceState: Bundle?) {
        // DO NOT MOVE ANYTHING ABOVE THIS getProfilerTime CALL.
        val startTimeProfiler = components.core.engine.profiler?.getProfilerTime()

        // The intent property is nullable, but the rest of the code below
        // assumes it is not. If it's null, then we make a new one and open
        // the HomeActivity.
        val intent = intent?.let { Intent(it) } ?: Intent()
        intent.sanitize().stripUnwantedFlags()

        // DO NOT MOVE the app link intent launch type setting below the super.onCreate call
        // as it impacts the activity lifecycle observer and causes false launch type detection.
        // e.g. COLD launch is interpreted as WARM due to [Activity.onActivityCreated] being called
        // earlier.
        if (intent.dataString != null) { // data is null when there's no URI to load, e.g. Search widget.
            val type = components.appLinkIntentLaunchTypeProvider.getExternalIntentLaunchType(HomeActivity::class.java)
            intent.putExtra(EXTRA_APP_LINK_LAUNCH_TYPE, type)
        }

        // StrictMode violation on certain devices such as Samsung
        components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
            super.onCreate(savedInstanceState)
        }

        processIntent(intent)

        components.core.engine.profiler?.addMarker(
            MarkersActivityLifecycleCallbacks.MARKER_NAME,
            startTimeProfiler,
            "IntentReceiverActivity.onCreate",
        )
        StartupTimeline.onActivityCreateEndIntentReceiver() // DO NOT MOVE ANYTHING BELOW HERE.
    }

    fun processIntent(intent: Intent) {
        val data = intent.toSafeIntent().data
        if (intent.action == Intent.ACTION_VIEW && data != null && isFxaPairingLink(data)) {
            SyncAuth.pairingLinkOpened.record(
                SyncAuth.PairingLinkOpenedExtra(
                    campaignAttributed = components.settings.isUserPairingCampaignAttributed
                )
            )
            launchAuthCustomTab(data)
            return
        }

        // Call process for side effects, short on the first that returns true

        var private = components.settings.openLinksInAPrivateTab
        if (!private) {
            // if PRIVATE_BROWSING_MODE is already set to true, honor that
            private = intent.getBooleanExtra(PRIVATE_BROWSING_MODE, false)
        }
        intent.putExtra(PRIVATE_BROWSING_MODE, private)
        if (private) {
            Events.openedLink.record(Events.OpenedLinkExtra("PRIVATE"))
        } else {
            Events.openedLink.record(Events.OpenedLinkExtra("NORMAL"))
        }

        addReferrerInformation(intent)

        if (intent.type == INTENT_TYPE_PDF) {
            val referrerIsFenix = this.isIntentInternal()
            Events.openedExtPdf.record(Events.OpenedExtPdfExtra(referrerIsFenix))
            if (!referrerIsFenix) {
                data?.let(::persistUriReadPermission)
            }
        }

        val processor = getIntentProcessors(private).firstOrNull { it.process(intent) }
        val intentProcessorType = components.intentProcessors.getType(processor)

        if (intentProcessorType.shouldOpenToBrowser(intent)) {
            components.core.engine.speculativeCreateSession(private = private)
        }

        launch(intent, intentProcessorType)
    }

    private fun persistUriReadPermission(uri: Uri) {
        try {
            val takeFlags: Int = Intent.FLAG_GRANT_READ_URI_PERMISSION
            contentResolver.takePersistableUriPermission(uri, takeFlags)
        } catch (securityException: SecurityException) {
            logger.debug("UriPermission could not be persisted", securityException)
        }
    }

    @VisibleForTesting
    internal fun launch(intent: Intent, intentProcessorType: IntentProcessorType) {
        intent.setClassName(applicationContext, intentProcessorType.activityClassName)

        if (!intent.hasExtra(HomeActivity.OPEN_TO_BROWSER)) {
            intent.putExtra(
                HomeActivity.OPEN_TO_BROWSER,
                intentProcessorType.shouldOpenToBrowser(intent),
            )
        }
        // StrictMode violation on certain devices such as Samsung
        components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
            startActivity(intent)
        }
        finish() // must finish() after starting the other activity
    }

    /** Opens a scanned Firefox Accounts pairing link in [org.mozilla.fenix.settings.account.AuthCustomTabActivity]. */
    @VisibleForTesting
    internal fun launchAuthCustomTab(pairingUrl: Uri) {
        // Neither the receiver's transparent theme nor the application context defines colorSurface, which the custom
        // tab intent reads, so resolve it against the app theme explicitly.
        val themedContext = ContextThemeWrapper(this, R.style.NormalTheme)
        val intent = SupportUtils.createAuthCustomTabIntent(themedContext, pairingUrl.toString())
        // StrictMode violation on certain devices such as Samsung
        components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
            startActivity(intent)
        }
        finish()
    }

    private fun getIntentProcessors(private: Boolean): List<IntentProcessor> {
        val modeDependentProcessors =
            if (private) {
                listOf(
                    components.intentProcessors.privateCustomTabIntentProcessor,
                    components.intentProcessors.privateIntentProcessor,
                )
            } else {
                listOf(
                    components.intentProcessors.customTabIntentProcessor,
                    components.intentProcessors.intentProcessor,
                )
            }

        return components.intentProcessors.externalAppIntentProcessors +
            components.intentProcessors.fennecPageShortcutIntentProcessor +
            components.intentProcessors.externalDeepLinkIntentProcessor +
            components.intentProcessors.webNotificationsIntentProcessor +
            components.intentProcessors.passwordManagerIntentProcessor +
            modeDependentProcessors +
            NewTabShortcutIntentProcessor(components.settings.enableHomepageAsNewTab) +
            UninstallShortcutIntentProcessor()
    }

    private fun addReferrerInformation(intent: Intent) {
        // Pass along referrer information when possible.
        // unfortunately you can get a RuntimeException thrown from android here
        @Suppress("TooGenericExceptionCaught")
        val r =
            try {
                // NB: referrer can be spoofed by the calling application. Use with caution.
                referrer
            } catch (_: RuntimeException) {
                // this could happen if the referrer intent contains data we can't deserialize
                return
            } ?: return
        intent.putExtra(EXTRA_ACTIVITY_REFERRER_PACKAGE, r.host)
        r.host?.let { host ->
            try {
                val category = packageManagerCompatHelper.getApplicationInfoCompat(host, 0).category
                intent.putExtra(EXTRA_ACTIVITY_REFERRER_CATEGORY, category)
            } catch (_: PackageManager.NameNotFoundException) {
                // At least we tried.
            }
        }
    }
}

private fun Intent.stripUnwantedFlags() {
    // Explicitly remove the new task and clear task flags (Our browser activity is a single
    // task activity and we never want to start a second task here).
    flags = flags and Intent.FLAG_ACTIVITY_NEW_TASK.inv()
    flags = flags and Intent.FLAG_ACTIVITY_CLEAR_TASK.inv()

    // IntentReceiverActivity is started with the "excludeFromRecents" flag (set in manifest). We
    // do not want to propagate this flag from the intent receiver activity to the browser.
    flags = flags and Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS.inv()
}

private val FXA_PAIRING_HOSTS = setOf("accounts.firefox.com", "accounts.stage.mozaws.net")
private const val FXA_PAIRING_PATH_PREFIX = "/pair"

/** Whether [uri] is a Firefox Accounts pairing link on the production or stage server. */
@VisibleForTesting
internal fun isFxaPairingLink(uri: Uri): Boolean =
    uri.scheme == "https" &&
        uri.host?.lowercase() in FXA_PAIRING_HOSTS &&
        uri.path.orEmpty().startsWith(FXA_PAIRING_PATH_PREFIX)

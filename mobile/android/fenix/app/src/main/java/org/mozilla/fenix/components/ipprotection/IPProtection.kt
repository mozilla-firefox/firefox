/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.ipprotection

import android.content.Context
import androidx.lifecycle.ProcessLifecycleOwner
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.feature.ipprotection.IPProtectionFeature
import mozilla.components.feature.ipprotection.auth.gpi.IPProtectionGpiProvider
import mozilla.components.feature.ipprotection.storage.DefaultIPProtectionLocationRepository
import mozilla.components.feature.ipprotection.storage.IPProtectionStorageSynchronizer
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.middleware.IPProtectionLocationMiddleware
import mozilla.components.lib.integrity.googleplay.GooglePlayIntegrityClient
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.store.SyncStore
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.Config
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.LogMiddleware
import org.mozilla.fenix.ipprotection.IPProtectionAccountObserver
import org.mozilla.fenix.ipprotection.store.DefaultIPProtectionRepository
import org.mozilla.fenix.ipprotection.store.FenixIPProtectionEligibilityStorage
import org.mozilla.fenix.ipprotection.store.IPProtectionPreferencesMiddleware
import org.mozilla.fenix.ipprotection.store.IPProtectionSnackbarMessages
import org.mozilla.fenix.ipprotection.store.IPProtectionSnackbarMiddleware
import org.mozilla.fenix.ipprotection.store.IPProtectionTelemetryMiddleware
import org.mozilla.fenix.utils.Settings

/**
 * Auth sources for IP Protection. Bundles the providers that supply credentials to the underlying
 * [IPProtectionFeature].
 */
data class IPProtectionAuthSources(
    val fxaAccountManager: Lazy<FxaAccountManager>,
    val integrityClient: Lazy<GooglePlayIntegrityClient>,
)

/** Provides access to IP Protection related components. */
class IPProtection(
    val engine: Engine,
    val browserStore: BrowserStore,
    val syncStore: SyncStore,
    val authSources: IPProtectionAuthSources,
    val lazyAppStore: Lazy<AppStore>,
    val settings: Settings,
    val context: Context,
) {
    val ipProtectionRepository by lazy { DefaultIPProtectionRepository(settings) }

    val store by lazy {
        IPProtectionStore(
            middleware =
                listOf(
                    LogMiddleware(
                        shouldIncludeDetailedData = { Config.channel.isDebug },
                        // tag has a max line-length; the rest of the default was unhelpful.
                        logger = Logger("IPPStore"),
                    ),
                    IPProtectionSnackbarMiddleware(
                        lazyAppStore = lazyAppStore,
                        messages = snackbarMessages,
                    ),
                    IPProtectionTelemetryMiddleware(),
                    IPProtectionPreferencesMiddleware(ipProtectionRepository),
                    IPProtectionLocationMiddleware(repository = DefaultIPProtectionLocationRepository(context)),
                )
        )
    }

    val eligibilityStorage by lazy {
        FenixIPProtectionEligibilityStorage(
            browserStore = browserStore,
            sharedPref = settings.preferences,
            prefKey = context.getString(R.string.pref_key_enable_ip_protection),
            lifecycleOwner = ProcessLifecycleOwner.get(),
        )
    }

    val feature by lazy {
        // N.B. Temporary: requires a restart. Should update automatically once we support
        // switching the auth provider without a restart.
        engine.settings.ipProtectionAuthProvider = if (settings.ipProtectionUseGpi) "gpi" else "fxa"

        IPProtectionFeature(
            store = store,
            engine = engine,
            accountManager = authSources.fxaAccountManager.value,
            extraAuthProvider = IPProtectionGpiProvider(authSources.integrityClient.value),
        )
    }

    val storageSynchronizer by lazy {
        IPProtectionStorageSynchronizer(
            storage = eligibilityStorage,
            store = store,
            syncStore = syncStore,
            lazyAccountManager = authSources.fxaAccountManager,
        )
    }

    private val accountObserver by lazy { IPProtectionAccountObserver(ipProtectionRepository) }

    private val snackbarMessages by lazy {
        IPProtectionSnackbarMessages(
            connectionError = context.getString(R.string.ip_protection_connection_error_snackbar),
            locationSelectionReset = context.getString(R.string.ip_protection_location_selection_reset_snackbar),
        )
    }

    /** Initializes IP Protection related components. * */
    fun initialize() {
        feature.initialize()
        storageSynchronizer.initialize()
        authSources.fxaAccountManager.value.register(accountObserver)
    }
}

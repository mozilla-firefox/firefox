/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import mozilla.appservices.remotesettings.RemoteSettingsService
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.Engine
import mozilla.components.service.nimbus.NimbusApi
import mozilla.components.service.nimbus.messaging.FxNimbusMessaging
import mozilla.components.service.nimbus.messaging.NimbusMessagingController
import mozilla.components.service.nimbus.messaging.NimbusMessagingControllerInterface
import mozilla.components.service.nimbus.messaging.NimbusMessagingStorage
import mozilla.components.service.nimbus.messaging.OnDiskMessageMetadataStorage
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.experiments.nimbus.NimbusMessagingHelperInterface
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.experiments.createNimbus
import org.mozilla.fenix.experiments.prefhandling.NimbusGeckoPrefHandler
import org.mozilla.fenix.messaging.CustomAttributeProvider
import org.mozilla.fenix.perf.lazyMonitored

/**
 * Component group for access to Nimbus and other Nimbus services.
 */
@OptIn(ExperimentalAndroidComponentsApi::class)
class NimbusComponents(
    private val context: Context,
    private val engine: Lazy<Engine>,
    remoteSettingsService: RemoteSettingsService?,
) {

    /**
     * The main entry point for the Nimbus SDK. Note that almost all access to feature configuration
     * should be mediated through a FML generated class, e.g. [FxNimbus].
     */
    val sdk: NimbusApi by lazyMonitored {
        // When debloat is on, pass a null endpoint so NimbusBuilder skips remote fetches
        // entirely. The local `initial_experiments.json` is still loaded so feature-flag
        // resolution works offline.
        val endpoint = if (FeatureFlags.ROBOWOLF_DEBLOAT_SERVICES) {
            null
        } else {
            BuildConfig.NIMBUS_ENDPOINT
        }
        createNimbus(context, endpoint, remoteSettingsService, geckoPrefHandler)
    }

    /**
     * Bridge between Nimbus and Gecko to allow enrollment, unenrollment, and observation of
     * Gecko pref experiments.
     */
    val geckoPrefHandler by lazyMonitored {
        NimbusGeckoPrefHandler(engine = engine, nimbusApi = lazy { sdk })
    }

    /**
     * Convenience method for getting the event store from the SDK.
     *
     * Before EXP-4354, this is the main write API for recording events to drive
     * messaging, experiments and onboarding.
     *
     * Following EXP-4354, clients will not need to write these events
     * themselves.
     *
     * Read access to the event store should be done through
     * the JEXL helper available from [createJexlHelper].
     */
    val events: NimbusEventStore by lazyMonitored {
        sdk.events
    }

    /**
     * Create a new JEXL evaluator suitable for use by any feature.
     *
     * JEXL evaluator context is provided by the app and changes over time.
     *
     * For this reason, an evaluator should be not be stored or cached.
     *
     * Since it has a native peer, to avoid leaking memory, the helper's [destroy] method
     * should be called after finishing the set of evaluations.
     *
     * This can be done automatically using the interface's `use` method, e.g.
     *
     * ```
     * val isEligible = nimbus.createJexlHelper().use { helper ->
     *    expressions.all { exp -> helper.evalJexl(exp) }
     * }
     * ```
     *
     * The helper has access to all context needed to drive decisions
     * about messaging, onboarding and experimentation.
     *
     * It also has a built-in cache.
     */
    fun createJexlHelper(): NimbusMessagingHelperInterface =
        messagingStorage.createMessagingHelper()

    /**
     * The main entry point for UI surfaces to interact with (get, click, dismiss) messages
     * from the Nimbus Messaging component.
     */
    val messaging: NimbusMessagingControllerInterface by lazyMonitored {
        NimbusMessagingController(
            messagingStorage = messagingStorage,
            deepLinkScheme = BuildConfig.DEEP_LINK_SCHEME,
        )
    }

    /**
     * Low level access to the messaging component.
     *
     * The app should access this through a [mozilla.components.service.nimbus.messaging.NimbusMessagingController].
     */
    private val messagingStorage by lazyMonitored {
        NimbusMessagingStorage(
            context = context,
            metadataStorage = OnDiskMessageMetadataStorage(context),
            nimbus = sdk,
            messagingFeature = FxNimbusMessaging.features.messaging,
            attributeProvider = CustomAttributeProvider,
        )
    }
}

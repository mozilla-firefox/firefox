/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import java.io.File
import mozilla.appservices.remotesettings.RemoteSettingsService
import mozilla.components.concept.engine.Engine
import mozilla.components.service.nimbus.NimbusApi
import mozilla.components.service.nimbus.messaging.FxNimbusMessaging
import mozilla.components.service.nimbus.messaging.NimbusMessagingController
import mozilla.components.service.nimbus.messaging.NimbusMessagingControllerInterface
import mozilla.components.service.nimbus.messaging.NimbusMessagingStorage
import mozilla.components.service.nimbus.messaging.OnDiskMessageMetadataStorage
import org.mozilla.experiments.nimbus.NIMBUS_DATA_DIR
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.experiments.nimbus.NimbusMessagingHelperInterface
import org.mozilla.experiments.nimbus.internal.EnrollmentSlugs
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.experiments.nimbus.internal.getActiveEnrollments
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.experiments.createNimbus
import org.mozilla.fenix.experiments.prefhandling.NimbusGeckoPrefHandler
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.messaging.CustomAttributeProvider

/** Component group for access to Nimbus and other Nimbus services. */
class NimbusComponents(
    private val context: Context,
    private val engine: Lazy<Engine>,
    remoteSettingsService: RemoteSettingsService?,
) {

    /**
     * The main entry point for the Nimbus SDK. Note that almost all access to feature configuration should be mediated
     * through a FML generated class, e.g. [FxNimbus].
     */
    val sdk: NimbusApi by lazy {
        createNimbus(
            context,
            context.components.settings,
            BuildConfig.NIMBUS_ENDPOINT,
            remoteSettingsService,
            geckoPrefHandler,
        )
    }

    /** Bridge between Nimbus and Gecko to allow enrollment, unenrollment, and observation of Gecko pref experiments. */
    val geckoPrefHandler by lazy {
        NimbusGeckoPrefHandler(engine = engine, nimbusApi = lazy { sdk })
    }

    /**
     * Convenience method for getting the event store from the SDK.
     *
     * Before EXP-4354, this is the main write API for recording events to drive messaging, experiments and onboarding.
     *
     * Following EXP-4354, clients will not need to write these events themselves.
     *
     * Read access to the event store should be done through the JEXL helper available from [createJexlHelper].
     */
    val events: NimbusEventStore by lazy {
        sdk.events
    }

    /**
     * Create a new JEXL evaluator suitable for use by any feature.
     *
     * JEXL evaluator context is provided by the app and changes over time.
     *
     * For this reason, an evaluator should be not be stored or cached.
     *
     * Since it has a native peer, to avoid leaking memory, the helper's [destroy] method should be called after
     * finishing the set of evaluations.
     *
     * This can be done automatically using the interface's `use` method, e.g.
     *
     * ```
     * val isEligible = nimbus.createJexlHelper().use { helper ->
     *    expressions.all { exp -> helper.evalJexl(exp) }
     * }
     * ```
     *
     * The helper has access to all context needed to drive decisions about messaging, onboarding and experimentation.
     *
     * It also has a built-in cache.
     */
    fun createJexlHelper(): NimbusMessagingHelperInterface = messagingStorage.createMessagingHelper()

    /**
     * The main entry point for UI surfaces to interact with (get, click, dismiss) messages from the Nimbus Messaging
     * component.
     */
    val messaging: NimbusMessagingControllerInterface by lazy {
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
    private val messagingStorage by lazy {
        NimbusMessagingStorage(
            context = context,
            metadataStorage = OnDiskMessageMetadataStorage(context),
            nimbus = sdk,
            messagingFeature = FxNimbusMessaging.features.messaging,
            attributeProvider = CustomAttributeProvider,
        )
    }

    /**
     * Return the list of enrolled experiments for the crash reporter.
     *
     * This will not instantiate the NimbusClient and instead attempt to read from the Nimbus database directly.
     */
    fun getEnrollmentsForCrashReporter(): List<EnrollmentSlugs> {
        try {
            val dbPath = File(context.applicationInfo.dataDir, NIMBUS_DATA_DIR).path
            return getActiveEnrollments(dbPath)
        } catch (e: NimbusException) {
            return emptyList()
        }
    }
}

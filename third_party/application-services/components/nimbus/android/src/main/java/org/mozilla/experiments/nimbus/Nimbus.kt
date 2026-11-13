/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.experiments.nimbus

import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import androidx.annotation.AnyThread
import androidx.annotation.RawRes
import androidx.annotation.VisibleForTesting
import androidx.annotation.WorkerThread
import androidx.core.content.pm.PackageInfoCompat
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.remotesettings.RemoteSettingsService
import mozilla.telemetry.glean.Glean
import org.json.JSONObject
import org.mozilla.experiments.nimbus.GleanMetrics.NimbusEvents
import org.mozilla.experiments.nimbus.GleanMetrics.NimbusHealth
import org.mozilla.experiments.nimbus.GleanMetrics.Pings
import org.mozilla.experiments.nimbus.internal.AppContext
import org.mozilla.experiments.nimbus.internal.AvailableExperiment
import org.mozilla.experiments.nimbus.internal.DatabaseLoadExtraDef
import org.mozilla.experiments.nimbus.internal.DatabaseMigrationExtraDef
import org.mozilla.experiments.nimbus.internal.EnrolledExperiment
import org.mozilla.experiments.nimbus.internal.EnrollmentChangeEvent
import org.mozilla.experiments.nimbus.internal.EnrollmentChangeEventType
import org.mozilla.experiments.nimbus.internal.EnrollmentStatusExtraDef
import org.mozilla.experiments.nimbus.internal.FeatureExposureExtraDef
import org.mozilla.experiments.nimbus.internal.FeatureUpdateDispatcher
import org.mozilla.experiments.nimbus.internal.FirefoxLabsEnrollStatus
import org.mozilla.experiments.nimbus.internal.FirefoxLabsMetadata
import org.mozilla.experiments.nimbus.internal.FirefoxLabsUnenrollStatus
import org.mozilla.experiments.nimbus.internal.GeckoPrefHandler
import org.mozilla.experiments.nimbus.internal.GeckoPrefState
import org.mozilla.experiments.nimbus.internal.MalformedFeatureConfigExtraDef
import org.mozilla.experiments.nimbus.internal.MetricsHandler
import org.mozilla.experiments.nimbus.internal.NimbusClient
import org.mozilla.experiments.nimbus.internal.NimbusClientInterface
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.experiments.nimbus.internal.NimbusServerSettings
import org.mozilla.experiments.nimbus.internal.PrefUnenrollReason
import org.mozilla.experiments.nimbus.internal.PreviousGeckoPrefState
import org.mozilla.experiments.nimbus.internal.RecordedContext
import java.io.File
import java.io.IOException
import kotlin.system.measureTimeMillis

const val NIMBUS_DATA_DIR: String = "nimbus_data"

/**
 * A implementation of the [NimbusInterface] interface backed by the Nimbus SDK.
 */
@Suppress("LargeClass", "LongParameterList")
open class Nimbus(
    override val context: Context,
    override val prefs: SharedPreferences? = null,
    appInfo: NimbusAppInfo,
    coenrollingFeatureIds: List<String>,
    server: NimbusServerSettings?,
    deviceInfo: NimbusDeviceInfo,
    private val observer: NimbusInterface.Observer? = null,
    delegate: NimbusDelegate,
    private val recordedContext: RecordedContext? = null,
    private val geckoPrefHandler: GeckoPrefHandler? = null,
) : NimbusInterface {
    // An I/O scope is used for reading or writing from the Nimbus's RKV database.
    private val dbScope: CoroutineScope = delegate.dbScope

    // An I/O scope is used for getting experiments from the network.
    private val fetchScope: CoroutineScope = delegate.fetchScope

    private val updateScope: CoroutineScope? = delegate.updateScope

    private val errorReporter = delegate.errorReporter

    private val logger = delegate.logger

    private val updateDispatcher: FeatureUpdateDispatcher by lazy {
        FeatureUpdateDispatcher(updateScope)
    }

    private val metricsHandler = object : MetricsHandler {
        override fun recordDatabaseLoad(event: DatabaseLoadExtraDef) {
            NimbusEvents.databaseLoad.record(
                NimbusEvents.DatabaseLoadExtra(
                    corrupt = event.corrupt,
                    initialVersion = event.initialVersion?.toInt(),
                    error = event.error,
                    migratedVersion = event.migratedVersion?.toInt(),
                    migrationError = event.migrationError,
                ),
            )
        }
        override fun recordDatabaseMigration(event: DatabaseMigrationExtraDef) {
            NimbusEvents.databaseMigration.record(
                NimbusEvents.DatabaseMigrationExtra(
                    reason = event.reason,
                    fromVersion = event.fromVersion.toInt(),
                    toVersion = event.toVersion.toInt(),
                    error = event.error,
                ),
            )
        }
        override fun recordEnrollmentStatuses(enrollmentStatusExtras: List<EnrollmentStatusExtraDef>) {
            for (extra in enrollmentStatusExtras) {
                NimbusEvents.enrollmentStatus.record(
                    NimbusEvents.EnrollmentStatusExtra(
                        branch = extra.branch,
                        slug = extra.slug,
                        status = extra.status,
                        reason = extra.reason,
                        errorString = extra.errorString,
                        conflictSlug = extra.conflictSlug,
                    ),
                )
            }
        }

        override fun recordFeatureActivation(event: FeatureExposureExtraDef) {
            NimbusEvents.activation.record(
                NimbusEvents.ActivationExtra(
                    experiment = event.slug,
                    branch = event.branch,
                    featureId = event.featureId,
                ),
            )
        }

        override fun recordFeatureExposure(event: FeatureExposureExtraDef) {
            NimbusEvents.exposure.record(
                NimbusEvents.ExposureExtra(
                    experiment = event.slug,
                    branch = event.branch,
                    featureId = event.featureId,
                ),
            )
        }

        override fun recordMalformedFeatureConfig(event: MalformedFeatureConfigExtraDef) {
            NimbusEvents.malformedFeature.record(
                NimbusEvents.MalformedFeatureExtra(
                    experiment = event.slug,
                    branch = event.branch,
                    featureId = event.featureId,
                    partId = event.part,
                ),
            )
        }

        override fun submitTargetingContext() {
            org.mozilla.experiments.nimbus.GleanMetrics.Pings.nimbusTargetingContext.submit()
        }

        override fun recordRolloutsOptOut() {
            NimbusEvents.rolloutsOptOut.record()
        }

        override fun recordStudiesOptOut() {
            NimbusEvents.studiesOptOut.record()
        }
    }

    private val nimbusClient: NimbusClientInterface

    override var experimentParticipation: Boolean
        get() = withCatchAll("getExperimentParticipation") {
            nimbusClient.getExperimentParticipation()
        } ?: true

        set(active) {
            dbScope.launch {
                setExperimentParticipationOnThisThread(active)
            }
        }

    override var rolloutParticipation: Boolean
        get() = withCatchAll("getExperimentParticipation") {
            nimbusClient.getRolloutParticipation()
        } ?: true

        set(active) {
            dbScope.launch {
                setRolloutParticipationOnThisThread(active)
            }
        }

    init {
        NullVariables.instance.setContext(context)

        // Set the name of the native library so that we use
        // the appservices megazord for compiled code.
        System.setProperty(
            "uniffi.component.nimbus.libraryOverride",
            System.getProperty("mozilla.appservices.megazord.library", "megazord"),
        )
        // Build a File object to represent the data directory for Nimbus data
        val dataDir = File(context.applicationInfo.dataDir, NIMBUS_DATA_DIR)

        // Build Nimbus AppContext object to pass into initialize
        val experimentContext = buildExperimentContext(context, appInfo, deviceInfo)

        nimbusClient = NimbusClient(
            experimentContext,
            recordedContext,
            coenrollingFeatureIds,
            dataDir.path,
            metricsHandler,
            geckoPrefHandler,
            server,
        )
    }

    override fun getFeatureUpdateDispatcher(): FeatureUpdateDispatcher? = updateDispatcher

    // This is currently not available from the main thread.
    // see https://jira.mozilla.com/browse/SDK-191
    @WorkerThread
    override fun getActiveExperiments(): List<EnrolledExperiment> = withCatchAll("getActiveExperiments") {
        nimbusClient.getActiveExperiments()
    } ?: emptyList()

    @WorkerThread
    override fun getAvailableExperiments(): List<AvailableExperiment> = withCatchAll("getAvailableExperiments") {
        nimbusClient.getAvailableExperiments()
    } ?: emptyList()

    @AnyThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun getFeatureConfigVariablesJson(featureId: String): JSONObject? =
        @Suppress("TooGenericExceptionCaught")
        try {
            nimbusClient.getFeatureConfigVariables(featureId)?.let { JSONObject(it) }
        } catch (e: NimbusException.DatabaseNotReady) {
            NimbusHealth.cacheNotReadyForFeature.record(
                NimbusHealth.CacheNotReadyForFeatureExtra(
                    featureId = featureId,
                ),
            )
            null
        } catch (e: Throwable) {
            reportError("getFeatureConfigVariablesJson", e)
            null
        }

    private fun reportError(msg: String, e: Throwable) =
        @Suppress("TooGenericExceptionCaught")
        try {
            errorReporter("Nimbus Rust: $msg", e)
        } catch (e1: Throwable) {
            logger("Exception calling rust: $e")
            logger("Exception reporting the exception: $e1")
        }

    override fun getExperimentBranch(experimentId: String): String? = withCatchAll("getExperimentBranch") {
        nimbusClient.getExperimentBranch(experimentId)
    }

    override fun getVariables(featureId: String, recordExposureEvent: Boolean): Variables =
        getFeatureConfigVariablesJson(featureId)?.let { json ->
            if (recordExposureEvent) {
                recordExposureEvent(featureId)
            }
            JSONVariables(context, json)
        }
            ?: NullVariables.instance

    @WorkerThread
    override fun getExperimentBranches(experimentId: String): List<Branch>? = withCatchAll("getExperimentBranches") {
        nimbusClient.getExperimentBranches(experimentId)
    }

    // Method and apparatus to catch any uncaught exceptions
    @SuppressWarnings("TooGenericExceptionCaught")
    private fun <R> withCatchAll(method: String, thunk: () -> R) =
        try {
            thunk()
        } catch (e: NimbusException.DatabaseNotReady) {
            // NOOP
            null
        } catch (e: Throwable) {
            reportError(method, e)
            null
        }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun initializeOnThisThread() = withCatchAll("initialize") {
        nimbusClient.initialize()
        postEnrolmentCalculation(emptyList(), initial = true)
    }

    override fun fetchExperiments() {
        fetchScope.launch {
            fetchExperimentsOnThisThread()
        }
    }

    override fun setFetchEnabled(enabled: Boolean) {
        fetchScope.launch {
            withCatchAll("setFetchEnabled") {
                nimbusClient.setFetchEnabled(enabled)
            }
        }
    }

    override fun isFetchEnabled() = withCatchAll("isFetchEnabled") {
        nimbusClient.isFetchEnabled()
    } ?: true

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun fetchExperimentsOnThisThread() = withCatchAll("fetchExperiments") {
        try {
            NimbusHealth.fetchExperimentsTime.measure {
                nimbusClient.fetchExperiments()
            }
            updateObserver {
                it.onExperimentsFetched()
            }
        } catch (e: NimbusException.ClientException) {
            errorReporter("Error fetching experiments from endpoint", e)
        }
    }

    private fun updateObserver(updater: (NimbusInterface.Observer) -> Unit) {
        val observer = observer ?: return
        if (updateScope != null) {
            updateScope.launch {
                updater(observer)
            }
        } else {
            updater(observer)
        }
    }

    override fun applyPendingExperiments(initial: Boolean): Job =
        dbScope.launch {
            withContext(NonCancellable) {
                applyPendingExperimentsOnThisThread(initial)
            }
        }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun applyPendingExperimentsOnThisThread(initial: Boolean = false) {
        withCatchAll("applyPendingExperiments") {
            try {
                var enrollmentChangeEvents: List<EnrollmentChangeEvent>?
                val time = measureTimeMillis {
                    enrollmentChangeEvents = nimbusClient.applyPendingExperiments()
                }
                NimbusHealth.applyPendingExperimentsTime.accumulateSingleSample(time)

                // SAFETY: events is only null at declaration time and is
                // immediately assigned a non-null value inside the
                // measureTimeMillis lambda.
                postEnrolmentCalculation(enrollmentChangeEvents!!, initial)
            } catch (e: NimbusException.InvalidExperimentFormat) {
                reportError("Invalid experiment format", e)
            }
        }
    }

    override fun applyLocalExperiments(
        @RawRes file: Int,
    ): Job =
        applyLocalExperiments { loadRawResource(file) }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun applyLocalExperiments(readExperiments: suspend () -> String): Job =
        dbScope.launch {
            val payload = try {
                readExperiments()
            } catch (e: CancellationException) {
                // TODO consider reporting a glean event here.
                logger(e.stackTraceToString())
                null
            } catch (e: IOException) {
                logger(e.stackTraceToString())
                null
            }

            applyLocalExperimentsOnThisThread(payload)
        }

    override fun applyLocalExperiments(experimentsJson: String): Job {
        return dbScope.launch {
            applyLocalExperimentsOnThisThread(experimentsJson)
        }
    }

    internal suspend fun applyLocalExperimentsOnThisThread(experimentsJson: String?) {
        withContext(NonCancellable) {
            if (experimentsJson != null) {
                setExperimentsLocallyOnThisThread(experimentsJson)
                applyPendingExperimentsOnThisThread(initial = true)
            } else {
                initializeOnThisThread()
            }
        }
    }

    @WorkerThread
    private fun postEnrolmentCalculation(
        enrollmentChangeEvents: List<EnrollmentChangeEvent>,
        initial: Boolean = false,
    ) {
        val experiments = nimbusClient.getActiveExperiments()

        if (initial) {
            // During initialization we need to report the experiment status of
            // all pre-existing experiments.
            for (experiment in experiments) {
                Glean.setExperimentActive(experiment.slug, experiment.branchSlug)
            }
        }

        if (initial || enrollmentChangeEvents.isNotEmpty()) {
            // During initialization we need to inform the application when we've
            // finished applying updates, even if there is nothing enrolled.
            recordExperimentTelemetryEvents(enrollmentChangeEvents)
            updateObserver { observer ->
                observer.onUpdatesApplied(experiments)
            }
        }

        if (initial) {
            // Likewise, during initialization we need to include trigger
            // updates for all pre-existing experiments.
            val featureIds = mutableSetOf<String>()
            for (experiment in experiments) {
                for (featureId in experiment.featureIds) {
                    featureIds.add(featureId)
                }
            }

            updateDispatcher.notifyFeatures(featureIds)
        } else {
            // However, during subsequent enrollment changes we only need to
            // trigger updates to features that changed.
            updateDispatcher.notifyChanged(enrollmentChangeEvents)
        }
    }

    private fun loadRawResource(file: Int): String =
        context.resources.openRawResource(file).use {
            it.bufferedReader().readText()
        }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun setExperimentsLocallyOnThisThread(payload: String) = withCatchAll("setExperimentsLocally") {
        nimbusClient.setExperimentsLocally(payload)
    }

    override fun resetEnrollmentsDatabase() =
        dbScope.launch {
            withCatchAll("resetEnrollments") {
                nimbusClient.resetEnrollments()
            }
        }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun setExperimentParticipationOnThisThread(active: Boolean) =
        withCatchAll("setExperimentParticipation") {
            val enrollmentChangeEvents = nimbusClient.setExperimentParticipation(active)
            postEnrolmentCalculation(enrollmentChangeEvents)
        }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun setRolloutParticipationOnThisThread(active: Boolean) =
        withCatchAll("setRolloutParticipation") {
            val enrollmentChangeEvents = nimbusClient.setRolloutParticipation(active)
            postEnrolmentCalculation(enrollmentChangeEvents)
        }

    override fun optOut(experimentId: String) {
        dbScope.launch {
            optOutOnThisThread(experimentId)
        }
    }

    @AnyThread
    override fun unenrollForGeckoPref(
        geckoPrefState: GeckoPrefState,
        prefUnenrollReason: PrefUnenrollReason,
    ) {
        dbScope.launch {
            unenrollForGeckoPrefOnThisThread(geckoPrefState, prefUnenrollReason)
        }
    }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun unenrollForGeckoPrefOnThisThread(
        geckoPrefState: GeckoPrefState,
        prefUnenrollReason: PrefUnenrollReason,
    ): List<EnrollmentChangeEvent>? {
        return withCatchAll("unenrollForGeckoPref") {
            val enrollmentChangeEvents = nimbusClient.unenrollForGeckoPref(geckoPrefState, prefUnenrollReason)
            postEnrolmentCalculation(enrollmentChangeEvents)
            enrollmentChangeEvents
        }
    }

    override fun registerPreviousGeckoPrefStates(geckoPrefStates: List<GeckoPrefState>) {
        dbScope.launch {
            withCatchAll("registerPreviousGeckoPrefStates") {
                registerPreviousGeckoPrefStatesOnThisThread(geckoPrefStates)
            }
        }
    }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun registerPreviousGeckoPrefStatesOnThisThread(geckoPrefStates: List<GeckoPrefState>) {
        nimbusClient.registerPreviousGeckoPrefStates(geckoPrefStates)
    }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun getPreviousGeckoPrefStatesOnThisThread(experimentSlug: String): List<PreviousGeckoPrefState>? {
        return nimbusClient.getPreviousGeckoPrefStates(experimentSlug)
    }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun optOutOnThisThread(experimentId: String) {
        withCatchAll("optOut") {
            val enrollmentChangeEvents = nimbusClient.optOut(experimentId)
            postEnrolmentCalculation(enrollmentChangeEvents)
        }
    }

    @AnyThread
    override fun resetTelemetryIdentifiers() {
        dbScope.launch {
            resetTelemetryIdentifiersOnThisThread()
        }
    }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun resetTelemetryIdentifiersOnThisThread() {
        withCatchAll("resetTelemetryIdentifiers") {
            val enrollmentChangeEvents = nimbusClient.resetTelemetryIdentifiers()
            postEnrolmentCalculation(enrollmentChangeEvents)
        }
    }

    @AnyThread
    override fun optInWithBranch(experimentId: String, branch: String) {
        dbScope.launch {
            optInWithBranchOnThisThread(experimentId, branch)
        }
    }

    @WorkerThread
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun optInWithBranchOnThisThread(experimentId: String, branch: String) {
        withCatchAll("optIn") {
            val enrollmentChangeEvents = nimbusClient.optInWithBranch(experimentId, branch)
            postEnrolmentCalculation(enrollmentChangeEvents)
        }
    }

    override fun recordExposureEvent(featureId: String, experimentSlug: String?) {
        recordExposureOnThisThread(featureId, experimentSlug)
    }

    override fun recordMalformedConfiguration(featureId: String, partId: String) {
        recordMalformedConfigurationOnThisThread(featureId, partId)
    }

    @AnyThread
    override fun recordEvent(count: Long, eventId: String) {
        dbScope.launch {
            withCatchAll("recordEvent") {
                nimbusClient.recordEvent(eventId, count)
            }
        }
    }

    @AnyThread
    override fun recordEventOrThrow(count: Long, eventId: String): Deferred<Unit> =
        dbScope.async {
            nimbusClient.recordEvent(eventId, count)
        }

    override fun recordEventSync(count: Long, eventId: String) =
        nimbusClient.recordEvent(eventId, count)

    override fun recordPastEvent(count: Long, eventId: String, secondsAgo: Long) =
        nimbusClient.recordPastEvent(eventId, secondsAgo, count)

    override fun advanceEventTime(bySeconds: Long) =
        nimbusClient.advanceEventTime(bySeconds)

    @WorkerThread
    override fun clearEvents() {
        dbScope.launch {
            nimbusClient.clearEvents()
        }
    }

    @AnyThread
    override fun dumpStateToLog() {
        nimbusClient.dumpStateToLog()
    }

    override fun createMessageHelper(additionalContext: JSONObject?): NimbusMessagingHelper =
        NimbusMessagingHelper(
            nimbusClient.createTargetingHelper(additionalContext),
            nimbusClient.createStringHelper(additionalContext),
        )

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun recordExperimentTelemetry(experiments: List<EnrolledExperiment>) {
        // Call Glean.setExperimentActive() for each active experiment.
        experiments.forEach { experiment ->
            // For now, we will just record the experiment id and the branch id. Once we can call
            // Glean from Rust, this will move to the nimbus-sdk Rust core.
            Glean.setExperimentActive(
                experiment.slug,
                experiment.branchSlug,
            )
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun recordExperimentTelemetryEvents(enrollmentChangeEvents: List<EnrollmentChangeEvent>) {
        enrollmentChangeEvents.forEach { event ->
            when (event.change) {
                EnrollmentChangeEventType.ENROLLMENT -> {
                    NimbusEvents.enrollment.record(
                        NimbusEvents.EnrollmentExtra(
                            experiment = event.experimentSlug,
                            branch = event.branchSlug,
                        ),
                    )

                    Glean.setExperimentActive(
                        event.experimentSlug,
                        event.branchSlug,
                    )
                }

                EnrollmentChangeEventType.DISQUALIFICATION -> {
                    NimbusEvents.disqualification.record(
                        NimbusEvents.DisqualificationExtra(
                            experiment = event.experimentSlug,
                            branch = event.branchSlug,
                        ),
                    )

                    Glean.setExperimentInactive(
                        event.experimentSlug,
                    )
                }

                EnrollmentChangeEventType.UNENROLLMENT -> {
                    NimbusEvents.unenrollment.record(
                        NimbusEvents.UnenrollmentExtra(
                            experiment = event.experimentSlug,
                            branch = event.branchSlug,
                            reason = event.reason,
                        ),
                    )

                    Glean.setExperimentInactive(
                        event.experimentSlug,
                    )
                }

                EnrollmentChangeEventType.ENROLL_FAILED -> {
                    NimbusEvents.enrollFailed.record(
                        NimbusEvents.EnrollFailedExtra(
                            experiment = event.experimentSlug,
                            branch = event.branchSlug,
                            reason = event.reason,
                        ),
                    )
                }

                EnrollmentChangeEventType.UNENROLL_FAILED -> {
                    NimbusEvents.unenrollFailed.record(
                        NimbusEvents.UnenrollFailedExtra(
                            experiment = event.experimentSlug,
                            reason = event.reason,
                        ),
                    )
                }
            }
        }
    }

    // The exposure event should be recorded when the expected treatment (or no-treatment, such as
    // for a "control" branch) is applied or shown to the user.
    // If the experiment slug is known, then use that to look up the enrollment.
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    @AnyThread
    internal fun recordExposureOnThisThread(featureId: String, experimentSlug: String? = null) =
        withCatchAll("recordFeatureExposure") {
            nimbusClient.recordFeatureExposure(featureId, experimentSlug)
        }

    // The malformed feature event is recorded by app developers, if the configuration is
    // _semantically_ invalid or malformed.
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    @AnyThread
    internal fun recordMalformedConfigurationOnThisThread(featureId: String, partId: String) =
        withCatchAll("recordMalformedConfiguration") {
            nimbusClient.recordMalformedFeatureConfig(featureId, partId)
        }

    override fun getAvailableFirefoxLabs(): Deferred<List<FirefoxLabsMetadata>> {
        return dbScope.async { getAvailableFirefoxLabsOnThisThread() }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun getAvailableFirefoxLabsOnThisThread(): List<FirefoxLabsMetadata> {
        return withCatchAll("getAvailableFirefoxLabs") {
            nimbusClient.getAvailableFirefoxLabs()
        } ?: emptyList()
    }

    override fun enrollInFirefoxLab(slug: String): Deferred<FirefoxLabsEnrollStatus> {
        return dbScope.async { enrollInFirefoxLabOnThisThread(slug) }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun enrollInFirefoxLabOnThisThread(slug: String): FirefoxLabsEnrollStatus {
        return withCatchAll("enrollInFirefoxLab") {
            val result = nimbusClient.enrollInFirefoxLab(slug)
            postEnrolmentCalculation(result.enrollmentChangeEvents)

            result.status
        } ?: FirefoxLabsEnrollStatus.ERROR
    }

    override fun unenrollFromFirefoxLab(slug: String): Deferred<FirefoxLabsUnenrollStatus> {
        return dbScope.async { unenrollFromFirefoxLabOnThisThread(slug) }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun unenrollFromFirefoxLabOnThisThread(slug: String): FirefoxLabsUnenrollStatus {
        return withCatchAll("unenrollFromFirefoxLab") {
            val result = nimbusClient.unenrollFromFirefoxLab(slug)
            postEnrolmentCalculation(result.enrollmentChangeEvents)

            result.status
        } ?: FirefoxLabsUnenrollStatus.ERROR
    }

    override fun unenrollFromAllFirefoxLabs(): Deferred<Unit> {
        return dbScope.async { unenrollFromAllFirefoxLabsOnThisThread() }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun unenrollFromAllFirefoxLabsOnThisThread() {
        withCatchAll("unenrollFromAllFirefoxLabs") {
            val enrollmentChangeEvents = nimbusClient.unenrollFromAllFirefoxLabs()
            postEnrolmentCalculation(enrollmentChangeEvents)
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun buildExperimentContext(
        context: Context,
        appInfo: NimbusAppInfo,
        deviceInfo: NimbusDeviceInfo,
    ): AppContext {
        val packageInfo: PackageInfo? = try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                context.packageManager.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
            } else {
                @Suppress("DEPRECATION")
                context.packageManager.getPackageInfo(context.packageName, 0)
            }
        } catch (e: PackageManager.NameNotFoundException) {
            null
        }

        return AppContext(
            appId = context.packageName,
            appName = appInfo.appName,
            channel = appInfo.channel,
            androidSdkVersion = Build.VERSION.SDK_INT.toString(),
            appBuild = packageInfo?.let { PackageInfoCompat.getLongVersionCode(it).toString() },
            appVersion = packageInfo?.versionName,
            architecture = Build.SUPPORTED_ABIS[0],
            debugTag = null,
            deviceManufacturer = Build.MANUFACTURER,
            deviceModel = Build.MODEL,
            locale = deviceInfo.localeTag,
            os = "Android",
            osVersion = Build.VERSION.RELEASE,
            installationDate = packageInfo?.firstInstallTime,
            customTargetingAttributes = appInfo.customTargetingAttributes,
        )
    }

    /**
    * Glean pings exposed for use in Fenix tests outside this package.
    */
    public object Pings {
        public val nimbusTargetingContext = org.mozilla.experiments.nimbus.GleanMetrics.Pings.nimbusTargetingContext
    }
}

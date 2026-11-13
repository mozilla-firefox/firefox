/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.experiments.nimbus

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import androidx.annotation.RawRes
import kotlinx.coroutines.runBlocking
import mozilla.appservices.remotesettings.RemoteSettingsService
import org.mozilla.experiments.nimbus.internal.FeatureManifestInterface
import org.mozilla.experiments.nimbus.internal.GeckoPrefHandler
import org.mozilla.experiments.nimbus.internal.NimbusServerSettings
import org.mozilla.experiments.nimbus.internal.RecordedContext

private const val TIME_OUT_LOADING_EXPERIMENT_FROM_DISK_MS = 200L

/**
 * A builder for [Nimbus] singleton objects, parameterized in a declarative class.
 */
abstract class AbstractNimbusBuilder<T : NimbusInterface>(val context: Context) {

    /**
     * An optional server URL string.
     *
     * This will only be null or empty in development or testing, or in any build variant of a
     * non-Mozilla fork.
     */
    var url: String? = null

    /**
     * A closure for reporting errors from Rust.
     */
    var errorReporter: ErrorReporter = { _: String, _: Throwable -> }

    /**
     * A flag to indicate if this is being run on the first run of the app. This is used to control
     * whether the `initial_experiments` file is used to populate Nimbus.
     */
    var isFirstRun: Boolean = true

    /**
     * A optional raw resource of a file downloaded at or near build time from Remote Settings.
     */
    @RawRes
    var initialExperiments: Int? = null

    /**
     * The timeout used to wait for the loading of the `initial_experiments`.
     */
    var timeoutLoadingExperiment: Long = TIME_OUT_LOADING_EXPERIMENT_FROM_DISK_MS

    /**
     * Optional callback to be called after the creation of the nimbus object and it is ready
     * to be used.
     */
    var onCreateCallback: (T) -> Unit = {}

    /**
     * Optional callback to be called everytime experiments have been applied.
     *
     * Experiment recipes are usually fetched shortly after startup, and those pending recipes are
     * applied at the following startup.
     *
     * This is not usually needed.
     */
    var onApplyCallback: () -> Unit = {}

    /**
     * Optional callback to be called everytime experiments have been fetched.
     *
     * Experiment recipes are usually fetched shortly after startup, and those pending recipes are
     * applied at the following startup.
     *
     * This is not usually needed.
     */
    var onFetchCallback: () -> Unit = {}

    /**
     * The `object` generated from the `nimbus.fml.yaml` file and the nimbus-gradle-plugin.
     */
    var featureManifest: FeatureManifestInterface<*>? = null

    /**
     * The shared preferences used to configure the app.
     */
    var sharedPreferences: SharedPreferences? = null

    /**
     * Additional targeting context that will be recorded to Glean.
     */
    var recordedContext: RecordedContext? = null

    /**
     * Gecko preference handler that provides state information and setters for Gecko prefs.
     */
    var geckoPrefHandler: GeckoPrefHandler? = null

    /**
     * Build a [Nimbus] singleton for the given [NimbusAppInfo]. Instances built with this method
     * have been initialized, and are ready for use by the app.
     *
     * Instance have _not_ yet had [fetchExperiments()] called on it, or anything usage of the
     * network. This is to allow the networking stack to be initialized after this method is called
     * and the networking stack to be involved in experiments.
     */
    fun build(appInfo: NimbusAppInfo, serverSettings: NimbusServerSettings?): T {
        // Is the app being built locally, and the nimbus-cli
        // hasn't been used before this run.
        fun NimbusInterface.isLocalBuild() = url.isNullOrBlank() && isFetchEnabled()

        @Suppress("TooGenericExceptionCaught")
        return try {
            newNimbus(appInfo, serverSettings).apply {
                // Apply any experiment recipes we downloaded last time, or
                // if this is the first time, we load the ones bundled in the res/raw
                // directory.
                val job = if (initialExperiments != null && (isFirstRun || isLocalBuild())) {
                    applyLocalExperiments(initialExperiments!!)
                } else {
                    applyPendingExperiments(initial = true)
                }

                // We always want initialize Nimbus to happen ASAP and before any features (engine/UI)
                // have been initialized. For that reason, we use runBlocking here to avoid
                // inconsistency in the experiments.
                // We can safely do this because Nimbus does most of its work on background threads,
                // including the loading the initial experiments from disk. For this reason, we have a
                // `joinOrTimeout` to limit the blocking until `timeoutLoadingExperiment`.
                runBlocking {
                    // We only read from disk when loading first-run experiments. This is the only time
                    // that we should join and block. Otherwise, we don't want to wait.
                    job.joinOrTimeout(timeoutLoadingExperiment)
                }
                // By now, on this thread, we have a fully initialized Nimbus object, ready for use:
                // * we gave a 200ms timeout to the loading of a file from res/raw
                // * on completion or cancellation, applyPendingExperiments or initialize was
                //   called, and this thread waited for that to complete.
                featureManifest?.initialize { this }
                onCreateCallback(this)
            }
        } catch (e: Throwable) {
            // Something went wrong. We'd like not to, but stability of the app is more important than
            // failing fast here.
            errorReporter("Failed to initialize Nimbus", e)
            newNimbusDisabled()
        }
    }

    /**
     * Construct a new [NimbusInterface] object with the passed parameters.
     */
    protected abstract fun newNimbus(
        appInfo: NimbusAppInfo,
        serverSettings: NimbusServerSettings?,
    ): T

    /**
     * In the event of the error constructing or configuring a Rust backed
     * [NimbusInterface] object, then construct a dummy object.
     */
    protected abstract fun newNimbusDisabled(): T

    /**
     * Creates the [NimbusDeviceInfo] for each Nimbus object built.
     */
    protected open fun createDeviceInfo() = NimbusDeviceInfo.default()

    /**
     * Creates the [NimbusDelegate] for each Nimbus instance built.
     *
     * The delegate is the main low-level interface between the embedding apps and the SDK.
     *
     * Override this if you want to customize the threading, error reporting or logging used
     * by the [NimbusInterface] instance.
     */
    protected open fun createDelegate() = NimbusDelegate.default()

    /**
     * Creates the observer used to tie together the feature manifest and the SDK. Implementers of
     * [newNimbus] should use this to register this observer with the [NimbusInterface] object.
     */
    protected fun createObserver(): NimbusInterface.Observer =
        Observer(featureManifest, onFetchCallback, onApplyCallback)

    /**
     * Returns a list of feature ids that support coenrollment. Implementers of [newNimbus] should
     * use this to pass into the [NimbusInterface] instance.
     */
    protected fun getCoenrollingFeatureIds(): List<String> =
        featureManifest?.getCoenrollingFeatureIds() ?: listOf()
}

private class Observer(
    val featureManifest: FeatureManifestInterface<*>?,
    val onFetchCallback: () -> Unit,
    val onApplyCallback: () -> Unit,
) : NimbusInterface.Observer {
    override fun onExperimentsFetched() {
        onFetchCallback.invoke()
    }

    override fun onUpdatesApplied(updated: List<EnrolledExperiment>) {
        featureManifest?.invalidateCachedValues()
        onApplyCallback.invoke()
    }
}

class DefaultNimbusBuilder(context: Context) : AbstractNimbusBuilder<NimbusInterface>(context) {
    override fun newNimbus(appInfo: NimbusAppInfo, serverSettings: NimbusServerSettings?) =
        Nimbus(
            context,
            appInfo = appInfo,
            prefs = sharedPreferences,
            coenrollingFeatureIds = getCoenrollingFeatureIds(),
            server = serverSettings,
            deviceInfo = createDeviceInfo(),
            delegate = createDelegate(),
            observer = createObserver(),
            recordedContext = recordedContext,
            geckoPrefHandler = geckoPrefHandler,
        )

    override fun newNimbusDisabled() = NullNimbus(context)
}

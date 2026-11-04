/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus

import android.app.Application
import android.content.Context
import android.os.Build
import android.os.StrictMode
import android.util.Log.INFO
import androidx.annotation.OpenForTesting
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.os.UserManagerCompat
import androidx.emoji2.text.DefaultEmojiCompatConfig
import androidx.emoji2.text.EmojiCompat
import androidx.lifecycle.ProcessLifecycleOwner
import androidx.work.Configuration.Builder
import androidx.work.Configuration.Provider
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.SearchAction
import mozilla.components.support.AppServicesInitializer
import mozilla.components.support.AppServicesInitializer.Config as AppServiceConfig
import mozilla.components.support.base.facts.register
import mozilla.components.support.base.log.Log
import mozilla.components.support.base.log.sink.AndroidLogSink
import mozilla.components.support.ktx.android.content.isMainProcess
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.remotesettings.GlobalRemoteSettingsDependencyProvider
import mozilla.components.support.rusthttp.RustHttpConfig
import mozilla.components.support.webextensions.WebExtensionSupport
import org.mozilla.focus.biometrics.LockObserver
import org.mozilla.focus.experiments.finishNimbusInitialization
import org.mozilla.focus.ext.settings
import org.mozilla.focus.navigation.StoreLink
import org.mozilla.focus.nimbus.FocusNimbus
import org.mozilla.focus.session.VisibilityLifeCycleCallback
import org.mozilla.focus.telemetry.FactsProcessor
import org.mozilla.focus.telemetry.ProfilerMarkerFactProcessor
import org.mozilla.focus.utils.AppConstants

/** Focus application class. */
open class FocusApplication : Application(), Provider {

    /**
     * A [CoroutineScope] tied to the application's lifecycle.
     *
     * This scope is used for launching long-running or global asynchronous tasks that should survive the destruction of
     * individual activities. It uses [Dispatchers.Main] as the default dispatcher and a [SupervisorJob] to ensure that
     * a failure in one child coroutine does not cancel others.
     *
     * Note: Tasks should be scoped to the container which holds their UI. If necessary, applicationScope can be used
     * for top-level background work that must remain active for the whole duration of the application.
     */
    private val applicationScope: CoroutineScope =
        CoroutineScope(
            SupervisorJob() +
                Dispatchers.Main +
                CoroutineExceptionHandler { _, throwable ->
                    Log.log(
                        priority = Log.Priority.ERROR,
                        tag = "ApplicationScope",
                        message = "Unhandled error: ${throwable.message}",
                        throwable = throwable,
                    )
                }
        )
    protected val ioDispatcher = Dispatchers.IO

    open val components: Components by lazy { Components(this, applicationScope = applicationScope) }

    var visibilityLifeCycleCallback: VisibilityLifeCycleCallback? = null
        private set

    private val storeLink by lazy { StoreLink(components.appStore, components.store) }
    private val lockObserver by lazy { LockObserver(this, components.store, components.appStore) }

    override fun onCreate() {
        super.onCreate()

        Log.addSink(AndroidLogSink("Focus"))

        // Read buildMetadata once at startup, so that it is in memory before anything can crash.
        components.buildMetadata

        components.crashReporter.install(this)

        if (isMainProcess()) {
            initializeNimbus()

            setTheme(this)
            components.engine.warmUp()

            initializeTelemetry()

            finishSetupMegazord()

            ProfilerMarkerFactProcessor.create { components.engine.profiler }.register()

            enableStrictMode()

            visibilityLifeCycleCallback = VisibilityLifeCycleCallback(this@FocusApplication)
            registerActivityLifecycleCallbacks(visibilityLifeCycleCallback)
            registerComponentCallbacks(visibilityLifeCycleCallback)

            storeLink.start(applicationScope)

            initializeWebExtensionSupport()

            initializeRemoteSettingsSupport()

            refreshSearchEngineUpdate()

            setupLeakCanary()

            components.appStartReasonProvider.registerInAppOnCreate(this)
            components.startupActivityLog.registerInAppOnCreate(this)

            ProcessLifecycleOwner.get().lifecycle.addObserver(lockObserver)

            applicationScope.launch {
                // Initialize EmojiCompat manually.
                initializeEmojiCompat()

                // Remove stale temporary uploaded files.
                components.fileUploadsDirCleaner.cleanUploadsDirectory()

                withContext(ioDispatcher) {
                    components.settings.deleteObsoleteCookieBannerDataIfNeeded()
                }
            }
        }
    }

    private fun refreshSearchEngineUpdate() {
        components.store.dispatch(SearchAction.RefreshSearchEnginesAction)
    }

    override fun onConfigurationChanged(config: android.content.res.Configuration) {
        applicationContext.resources.configuration.uiMode = config.uiMode

        if (isMainProcess()) {
            super.onConfigurationChanged(config)
            // Update locale on main process
            LocaleManager.updateResources(this)
        } else {
            super.onConfigurationChanged(config)
        }
    }

    override fun attachBaseContext(base: Context) {
        // Sets the locale information. Other threads do not have locale aware needs
        if (base.isMainProcess()) {
            val localeAwareContext = LocaleManager.updateResources(base)
            super.attachBaseContext(localeAwareContext)
        } else {
            super.attachBaseContext(base)
        }
    }

    protected open fun setupLeakCanary() {
        // no-op, LeakCanary is disabled by default
    }

    /**
     * Updates the configuration of LeakCanary based on the provided state.
     *
     * @param isEnabled Whether LeakCanary features should be enabled.
     */
    open fun updateLeakCanaryState(isEnabled: Boolean) {
        // no-op, LeakCanary is disabled by default
    }

    protected open fun initializeNimbus() {
        beginSetupMegazord()

        // This lazily constructs the Nimbus object…
        val nimbus = components.experiments
        // … which we then can populate the feature configuration.
        FocusNimbus.initialize { nimbus }
    }

    /**
     * Initializes EmojiCompat manually on a background thread.
     *
     * By initializing manually, we avoid the startup penalty associated with the default EmojiCompat initializer's
     * ContentProvider. [DefaultEmojiCompatConfig] is used to automatically find a compatible font provider (such as
     * Google Play Services).
     *
     * @param dispatcher The [CoroutineDispatcher] on which the initialization will occur. Defaults to [ioDispatcher].
     */
    private suspend fun initializeEmojiCompat(dispatcher: CoroutineDispatcher = ioDispatcher) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) return

        withContext(dispatcher) {
            // If the device has no compatible provider (e.g. no Play Services), config will be null.
            val config = DefaultEmojiCompatConfig.create(applicationContext) ?: return@withContext

            config.setReplaceAll(true)

            config.registerInitCallback(
                object : EmojiCompat.InitCallback() {
                    override fun onInitialized() {
                        Log.log(
                            tag = "EmojiCompat",
                            message = "EmojiCompat initialization completed",
                        )
                    }

                    override fun onFailed(throwable: Throwable?) {
                        Log.log(
                            tag = "EmojiCompat",
                            throwable = throwable,
                            message = "EmojiCompat initialization failed",
                        )
                    }
                }
            )

            EmojiCompat.init(config)
        }
    }

    protected open fun initializeTelemetry() {
        components.metrics.initialize(this)
        FactsProcessor.initialize()
        if (components.settings.isDailyUsagePingEnabled) {
            components.usageReportingMetricsService.start()
        }
    }

    /**
     * Initiate Megazord sequence! Megazord Battle Mode!
     *
     * The application-services combined libraries are known as the "megazord". We use the default `full` megazord - it
     * contains everything that fenix needs, and (currently) nothing more.
     *
     * Documentation on what megazords are, and why they're needed:
     * - https://github.com/mozilla/application-services/blob/master/docs/design/megazords.md
     * - https://mozilla.github.io/application-services/docs/applications/consuming-megazord-libraries.html
     *
     * This is the initialization of the megazord without setting up networking, i.e. needing the engine for networking.
     * This should do the minimum work necessary as it is done on the main thread, early in the app startup sequence.
     */
    private fun beginSetupMegazord() {
        AppServicesInitializer.init(AppServiceConfig(components.crashReporter))
    }

    /** Finish Megazord setup sequence. */
    @OpenForTesting
    open fun finishSetupMegazord(dispatcher: CoroutineDispatcher = ioDispatcher) {
        // We need to use an unwrapped client because native components do not support private
        // requests.
        @Suppress("Deprecation") RustHttpConfig.setClient(lazy { components.client.unwrap() })

        applicationScope.launch(dispatcher) {
            // Now viaduct (the RustHttp client) is initialized we can ask Nimbus to fetch
            // experiments recipes from the server.
            finishNimbusInitialization(components.experiments)
        }
    }

    private fun initializeRemoteSettingsSupport() {
        GlobalRemoteSettingsDependencyProvider.initialize(components.remoteSettingsService)
        components.remoteSettingsSyncScheduler.registerForSync()
    }

    private fun setTheme(context: Context) {
        val settings = context.settings
        when {
            settings.lightThemeSelected -> {
                AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_NO)
            }

            settings.darkThemeSelected -> {
                AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_YES)
            }

            settings.useDefaultThemeSelected -> {
                setDefaultTheme()
            }

            // No theme setting selected, select the default value, follow device theme.
            else -> {
                setDefaultTheme()
                settings.useDefaultThemeSelected = true
            }
        }
    }

    private fun setDefaultTheme() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM)
        } else {
            AppCompatDelegate.setDefaultNightMode(AppCompatDelegate.MODE_NIGHT_AUTO_BATTERY)
        }
    }

    private fun enableStrictMode() {
        // Only enable StrictMode in debug builds
        if (AppConstants.isDevBuild) {
            val threadPolicyBuilder = StrictMode.ThreadPolicy.Builder().detectAll()
            val vmPolicyBuilder =
                StrictMode.VmPolicy.Builder()
                    .detectActivityLeaks()
                    .detectFileUriExposure()
                    .detectLeakedClosableObjects()
                    .detectLeakedRegistrationObjects()
                    .detectLeakedSqlLiteObjects()

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                vmPolicyBuilder.detectNonSdkApiUsage()
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                vmPolicyBuilder.detectUnsafeIntentLaunch()
            }

            threadPolicyBuilder.penaltyLog()
            vmPolicyBuilder.penaltyLog()

            StrictMode.setThreadPolicy(threadPolicyBuilder.build())
            StrictMode.setVmPolicy(vmPolicyBuilder.build())
        }
    }

    @VisibleForTesting
    @OpenForTesting
    internal open fun initializeWebExtensionSupport() {
        WebExtensionSupport.initialize(
            components.engine,
            components.store,
            isInPrivateBrowsingMode = { true },
            onNewTabOverride = { _, engineSession, url, selected, isPrivate ->
                components.tabsUseCases.addTab(
                    url = url,
                    selectTab = selected,
                    engineSession = engineSession,
                    private = isPrivate,
                )
            },
        )
    }

    override val workManagerConfiguration
        get() =
            Builder()
                .setMinimumLoggingLevel(INFO)
                .setInitializationExceptionHandler { throwable ->
                    if (UserManagerCompat.isUserUnlocked(this)) {
                        throw throwable
                    }
                }
                .build()
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.activity

import android.Manifest.permission.POST_NOTIFICATIONS
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.AttributeSet
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.view.ViewTreeObserver
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.ActionBar
import androidx.appcompat.widget.Toolbar
import androidx.core.content.edit
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.core.view.updateLayoutParams
import androidx.core.view.updatePadding
import androidx.lifecycle.lifecycleScope
import androidx.preference.PreferenceManager
import mozilla.components.browser.state.selector.privateTabs
import mozilla.components.concept.engine.EngineView
import mozilla.components.feature.search.widget.BaseVoiceSearchActivity
import mozilla.components.lib.auth.canUseBiometricFeature
import mozilla.components.lib.crash.Crash
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.utils.DefaultDateTimeProvider
import mozilla.components.support.utils.SafeIntent
import mozilla.components.support.utils.StatusBarUtils
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.experiments.nimbus.internal.FeatureHolder
import org.mozilla.focus.GleanMetrics.AppOpened
import org.mozilla.focus.GleanMetrics.Notifications
import org.mozilla.focus.R
import org.mozilla.focus.appreview.AppReviewUtils
import org.mozilla.focus.databinding.ActivityMainBinding
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.setNavigationIcon
import org.mozilla.focus.ext.settings
import org.mozilla.focus.ext.updateSecureWindowFlags
import org.mozilla.focus.fragment.BrowserFragment
import org.mozilla.focus.fragment.UrlInputFragment
import org.mozilla.focus.fragment.onboarding.OnboardingStorage
import org.mozilla.focus.navigation.MainActivityNavigation
import org.mozilla.focus.navigation.Navigator
import org.mozilla.focus.nimbus.FocusNimbus
import org.mozilla.focus.nimbus.Onboarding
import org.mozilla.focus.searchwidget.ExternalIntentNavigation
import org.mozilla.focus.searchwidget.SearchWidgetUtils
import org.mozilla.focus.session.IntentProcessor
import org.mozilla.focus.session.PrivateNotificationFeature
import org.mozilla.focus.shortcut.HomeScreen
import org.mozilla.focus.state.AppAction
import org.mozilla.focus.state.Screen
import org.mozilla.focus.telemetry.startuptelemetry.StartupPathProvider
import org.mozilla.focus.telemetry.startuptelemetry.StartupTypeTelemetry
import org.mozilla.focus.utils.SupportUtils
import org.mozilla.focus.utils.ViewUtils

private const val REQUEST_TIME_OUT = 2000L

/**
 * The main activity of the application, serving as the primary entry point and container for various fragments like the
 * browser and settings.
 */
@Suppress("LargeClass")
// The main entry point for the app.
open class MainActivity : EdgeToEdgeActivity() {
    private var isToolbarInflated = false
    private val intentProcessor by lazy {
        IntentProcessor(this, components.tabsUseCases, components.customTabsUseCases, components.searchUseCases)
    }
    private val onboardingStorage by lazy { OnboardingStorage(this) }
    private val navigator by lazy {
        Navigator(
            stateFlow = components.appStore.flow(),
            navigation =
                MainActivityNavigation(
                    supportFragmentManager = supportFragmentManager,
                    onboardingStorage = onboardingStorage,
                    isInPictureInPictureMode = { isInPictureInPictureMode },
                    shouldAnimateHome = ::shouldAnimateHome,
                    showStartBrowsingCfr = ::showStartBrowsingCfr,
                    onEraseAction = ::reactToEraseAction,
                ),
            scope = lifecycleScope,
        )
    }

    private val tabCount: Int
        get() = components.store.state.privateTabs.size

    private val startupPathProvider = StartupPathProvider()
    private lateinit var startupTypeTelemetry: StartupTypeTelemetry
    private var _binding: ActivityMainBinding? = null
    private val binding
        get() = _binding!!

    private lateinit var privateNotificationFeature: PrivateNotificationFeature
    private val notificationPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            when {
                granted -> {
                    privateNotificationFeature.start()
                }
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        installSplashScreen()

        updateSecureWindowFlags()

        super.onCreate(savedInstanceState)
        _binding = ActivityMainBinding.inflate(layoutInflater)
        // Checks if Activity is currently in PiP mode if launched from external intents, then exits it
        checkAndExitPiP()

        if (!isTaskRoot && intent.hasCategory(Intent.CATEGORY_LAUNCHER) && Intent.ACTION_MAIN == intent.action) {
            finish()
            return
        }

        setContentView(binding.root)

        startupPathProvider.attachOnActivityOnCreate(lifecycle, intent)
        startupTypeTelemetry =
            StartupTypeTelemetry(
                    components.startupStateProvider,
                    startupPathProvider,
                    components.applicationScope,
                )
                .apply {
                    attachOnMainActivityOnCreate(lifecycle)
                }

        val safeIntent = SafeIntent(intent)

        lifecycle.addObserver(navigator)

        if (savedInstanceState == null) {
            handleAppNavigation(safeIntent)
        }

        if (savedInstanceState == null && intent.hasExtra(HomeScreen.ADD_TO_HOMESCREEN_TAG)) {
            intentProcessor.handleNewIntent(safeIntent)
        }

        if (safeIntent.isLauncherIntent) {
            AppOpened.fromIcons.record(AppOpened.FromIconsExtra(AppOpenType.LAUNCH.type))
        }

        val launchCount = settings.getAppLaunchCount()
        PreferenceManager.getDefaultSharedPreferences(this).edit {
            putInt(getString(R.string.app_launch_count), launchCount + 1)
        }

        AppReviewUtils.showAppReview(this)

        privateNotificationFeature =
            PrivateNotificationFeature(
                    context = applicationContext,
                    browserStore = components.store,
                    crashReporter = components.crashReporter,
                    permissionRequestHandler = { requestNotificationPermission() },
                )
                .also {
                    it.start()
                }

        components.notificationsDelegate.bindToActivity(this)

        onBackPressedDispatcher.addCallback(
            this,
            object : OnBackPressedCallback(true) {
                override fun handleOnBackPressed() {
                    this@MainActivity.handleBackPressed()
                }
            },
        )
    }

    private fun requestNotificationPermission() {
        privateNotificationFeature.stop()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            notificationPermission.launch(POST_NOTIFICATIONS)
        }
    }

    private fun setSplashScreenPreDrawListener(
        safeIntent: SafeIntent,
        currentTimeProvider: () -> Long = DefaultDateTimeProvider()::currentTimeMillis,
    ) {
        val endTime = currentTimeProvider() + REQUEST_TIME_OUT
        binding.container.viewTreeObserver.addOnPreDrawListener(
            object : ViewTreeObserver.OnPreDrawListener {
                override fun onPreDraw(): Boolean {
                    return if (currentTimeProvider() >= endTime) {
                        ExternalIntentNavigation.handleAppNavigation(
                            bundle = safeIntent.extras,
                            context = this@MainActivity,
                        )
                        binding.container.viewTreeObserver.removeOnPreDrawListener(this)
                        true
                    } else {
                        false
                    }
                }
            }
        )
    }

    private fun checkAndExitPiP() {
        if (isInPictureInPictureMode && intent != null) {
            // Exit PiP mode
            moveTaskToBack(false)
            startActivity(Intent(this, this::class.java).setFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT))
        }
    }

    final override fun onUserLeaveHint() {
        if (components.notificationsDelegate.isRequestingPermission) {
            // The notification permission prompt will trigger onUserLeaveHint too.
            // We shouldn't treat this situation as user leaving.
        } else {
            val browserFragment =
                supportFragmentManager.findFragmentByTag(BrowserFragment.FRAGMENT_TAG) as BrowserFragment?
            if (browserFragment is UserInteractionHandler && browserFragment.onHomePressed()) {
                return
            }
        }
        super.onUserLeaveHint()
    }

    override fun onResume() {
        super.onResume()
        checkBiometricStillValid()
    }

    override fun onPause() {
        val fragmentManager = supportFragmentManager
        val browserFragment = fragmentManager.findFragmentByTag(BrowserFragment.FRAGMENT_TAG) as BrowserFragment?
        browserFragment?.cancelAnimation()

        val urlInputFragment = fragmentManager.findFragmentByTag(UrlInputFragment.FRAGMENT_TAG) as UrlInputFragment?
        urlInputFragment?.cancelAnimation()

        super.onPause()
    }

    @Suppress("PARAMETER_NAME_CHANGED_ON_OVERRIDE")
    override fun onNewIntent(unsafeIntent: Intent) {
        if (Crash.isCrashIntent(unsafeIntent)) {
            val browserFragment =
                supportFragmentManager.findFragmentByTag(BrowserFragment.FRAGMENT_TAG) as BrowserFragment?
            val crash = Crash.fromIntent(unsafeIntent)

            browserFragment?.handleTabCrash(crash)
        }
        startupPathProvider.onIntentReceived(intent)
        val intent = SafeIntent(unsafeIntent)

        handleAppRestoreFromBackground(intent)

        if (intent.dataString.equals(SupportUtils.OPEN_WITH_DEFAULT_BROWSER_URL)) {
            components.appStore.dispatch(AppAction.OpenSettings(page = Screen.Settings.Page.General))
            super.onNewIntent(unsafeIntent)
            return
        }

        val action = intent.action

        if (intent.hasExtra(HomeScreen.ADD_TO_HOMESCREEN_TAG)) {
            intentProcessor.handleNewIntent(intent)
        }

        if (ACTION_OPEN == action) {
            Notifications.openButtonTapped.record(NoExtras())
        }

        if (ACTION_ERASE == action) {
            processEraseAction(intent)
        }

        if (intent.isLauncherIntent) {
            AppOpened.fromIcons.record(AppOpened.FromIconsExtra(AppOpenType.RESUME.type))
        }

        super.onNewIntent(unsafeIntent)
    }

    private fun handleAppRestoreFromBackground(intent: SafeIntent) {
        if (intent.extras?.getString(BaseVoiceSearchActivity.SPEECH_PROCESSING).isNullOrEmpty()) {
            val currentScreen = components.appStore.state.screen
            when (currentScreen) {
                is Screen.Settings -> components.appStore.dispatch(AppAction.OpenSettings(page = currentScreen.page))
                is Screen.SitePermissionOptionsScreen ->
                    components.appStore.dispatch(
                        AppAction.OpenSitePermissionOptionsScreen(sitePermission = currentScreen.sitePermission)
                    )
                else -> {
                    handleAppNavigation(intent)
                }
            }
            return
        }
        handleAppNavigation(intent)
    }

    private fun handleAppNavigation(intent: SafeIntent) {
        components.appStore.dispatch(AppAction.ResetHomeAccessPoint)

        if (components.appStore.state.screen is Screen.Locked) {
            components.appStore.dispatch(AppAction.Lock(intent.extras))
        } else if (settings.getAppLaunchCount() == 0) {
            setSplashScreenPreDrawListener(intent)
        } else {
            ExternalIntentNavigation.handleAppNavigation(
                bundle = intent.extras,
                context = this,
            )
        }
    }

    private fun processEraseAction(intent: SafeIntent) {
        val fromNotificationAction = intent.getBooleanExtra(EXTRA_NOTIFICATION, false)

        components.tabsUseCases.removeAllTabs()

        if (fromNotificationAction) {
            Notifications.eraseOpenButtonTapped.record(Notifications.EraseOpenButtonTappedExtra(tabCount))
        }
    }

    /**
     * Display the widget promo at first data clearing action and if it wasn't added after 5th Focus session or display
     * branded snackbar when widget promo is not shown.
     */
    private fun reactToEraseAction() {
        val onboardingFeature = FocusNimbus.features.onboarding

        val clearBrowsingSessions = components.settings.getClearBrowsingSessions()
        components.settings.addClearBrowsingSessions(INCREMENT_CLEAR_BROWSING_SESSIONS_BY)

        if (shouldShowWidgetPromo(onboardingFeature, clearBrowsingSessions)) {
            showWidgetPromo(onboardingFeature)
        } else {
            showBrandedFeedbackSnackbar()
        }
    }

    /**
     * Determines if the widget promo should be displayed based on feature flags, widget installation status, and the
     * number of data clearing actions.
     *
     * @param onboardingFeature The feature holder for onboarding.
     * @param clearCount The number of times data has been cleared.
     * @return True if the widget promo should be shown, false otherwise.
     */
    private fun shouldShowWidgetPromo(
        onboardingFeature: FeatureHolder<Onboarding>,
        clearCount: Int,
    ): Boolean {
        val isPromoEnabled = onboardingFeature.value().isPromoteSearchWidgetDialogEnabled
        val isWidgetNotInstalled = !settings.searchWidgetInstalled
        val isEligibleSessionCount =
            clearCount == FIRST_DATA_CLEARING_ACTION_COUNT || clearCount == FIFTH_FOCUS_SESSION_THRESHOLD_FOR_PROMO
        return isPromoEnabled && isWidgetNotInstalled && isEligibleSessionCount
    }

    private fun showWidgetPromo(onboardingFeature: FeatureHolder<Onboarding>) {
        onboardingFeature.recordExposure()
        SearchWidgetUtils.showPromoteSearchWidgetDialog(this)
    }

    /**
     * Shows a branded snackbar to provide feedback to the user after an erase action. This is typically shown when the
     * widget promo is not displayed.
     */
    private fun showBrandedFeedbackSnackbar() {
        val rootView = findViewById<View>(android.R.id.content)
        ViewUtils.showBrandedSnackbar(
            rootView,
            R.string.feedback_erase2,
            resources.getInteger(R.integer.erase_snackbar_delay),
        )
    }

    /**
     * Shows the "Start Browsing" CFR if the conditions are met.
     *
     * This function checks if:
     * - The CFR feature is enabled in Nimbus.
     * - It's not the first run of the app.
     * - The app settings indicate that the CFR should be shown.
     *
     * If all conditions are true, it sends an exposure event for the onboarding feature and dispatches an action to
     * show the CFR.
     */
    private fun showStartBrowsingCfr() {
        val onboardingConfig = FocusNimbus.features.onboarding.value()
        if (onboardingConfig.isCfrEnabled && !settings.isFirstRun && settings.shouldShowStartBrowsingCfr) {
            FocusNimbus.features.onboarding.recordExposure()
            components.appStore.dispatch(AppAction.ShowStartBrowsingCfrChange(true))
        }
    }

    private fun shouldAnimateHome(): Boolean {
        val browserFragment =
            supportFragmentManager.findFragmentByTag(BrowserFragment.FRAGMENT_TAG) as? BrowserFragment ?: return false
        return browserFragment.isResumed
    }

    override fun onCreateView(parent: View?, name: String, context: Context, attrs: AttributeSet): View? {
        return if (name == EngineView::class.java.name) {
            components.engine.createView(context, attrs).asView()
        } else {
            super.onCreateView(parent, name, context, attrs)
        }
    }

    private fun handleBackPressed() {
        val fragmentManager = supportFragmentManager

        val urlInputFragment = fragmentManager.findFragmentByTag(UrlInputFragment.FRAGMENT_TAG) as UrlInputFragment?
        if (urlInputFragment != null && urlInputFragment.isVisible && urlInputFragment.onBackPressed()) {
            // The URL input fragment has handled the back press. It does its own animations so
            // we do not try to remove it from outside.
            return
        }

        val browserFragment = fragmentManager.findFragmentByTag(BrowserFragment.FRAGMENT_TAG) as BrowserFragment?
        if (browserFragment != null && browserFragment.isVisible && browserFragment.onBackPressed()) {
            // The Browser fragment handles back presses on its own because it might just go back
            // in the browsing history.
            return
        }

        val appStore = components.appStore
        if (appStore.state.screen is Screen.Settings || appStore.state.screen is Screen.SitePermissionOptionsScreen) {
            // When on a settings screen we want the same behavior as navigating "up" via the toolbar
            // and therefore dispatch the `NavigateUp` action on the app store.
            val selectedTabId = components.store.state.selectedTabId
            appStore.dispatch(AppAction.NavigateUp(selectedTabId))
            return
        }

        // If no fragments are handling the back press, finish the activity.
        finish()
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) {
            // We forward an up action to the app store with the NavigateUp action to let the reducer
            // decide to show a different screen.
            val selectedTabId = components.store.state.selectedTabId
            components.appStore.dispatch(AppAction.NavigateUp(selectedTabId))
            return true
        }

        return super.onOptionsItemSelected(item)
    }

    // Handles the edge case of a user removing all enrolled prints while auth was enabled
    private fun checkBiometricStillValid() {
        if (canUseBiometricFeature()) {
            return
        } else {
            // Disable biometrics if the user is no longer eligible due to un-enrolling fingerprints:
            PreferenceManager.getDefaultSharedPreferences(this).edit {
                putBoolean(
                    getString(R.string.pref_key_biometric),
                    false,
                )
            }
        }
    }

    /**
     * Gets the [ActionBar] for this activity.
     *
     * This function lazily inflates the toolbar from a `ViewStub` the first time it's called, sets it as the
     * `supportActionBar`, and adjusts its padding and height to account for the status bar, ensuring content is not
     * obscured. On subsequent calls, it returns the already inflated and configured `ActionBar`.
     *
     * @return The configured [ActionBar] for the activity.
     */
    fun getToolbar(): ActionBar {
        return if (isToolbarInflated) {
            supportActionBar!!
        } else {
            val toolbar = binding.toolbar.inflate() as Toolbar

            StatusBarUtils.getStatusBarHeight(toolbar) { statusBarHeight ->
                toolbar.updateLayoutParams<ViewGroup.MarginLayoutParams> {
                    height = height + statusBarHeight
                }
                toolbar.updatePadding(top = statusBarHeight)
            }

            setSupportActionBar(toolbar)
            setNavigationIcon(R.drawable.ic_back_button)
            isToolbarInflated = true
            supportActionBar!!
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        _binding = null

        if (this::privateNotificationFeature.isInitialized) {
            privateNotificationFeature.stop()
        }

        components.notificationsDelegate.unBindActivity(this)
    }

    /**
     * Represents the different ways an application can be opened, used for telemetry purposes. This helps distinguish
     * between a fresh start and resuming from the background.
     *
     * @property type The string representation of the open type, used for metrics.
     */
    enum class AppOpenType(val type: String) {
        LAUNCH("Launch"),
        RESUME("Resume"),
    }

    companion object {
        const val ACTION_ERASE = "erase"
        const val ACTION_OPEN = "open"

        const val EXTRA_NOTIFICATION = "notification"

        private const val FIRST_DATA_CLEARING_ACTION_COUNT = 0
        private const val FIFTH_FOCUS_SESSION_THRESHOLD_FOR_PROMO = 4
        private const val INCREMENT_CLEAR_BROWSING_SESSIONS_BY = 1
    }
}

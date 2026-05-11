/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import android.annotation.SuppressLint
import android.appwidget.AppWidgetManager
import android.content.Context
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.os.Build
import android.os.Bundle
import android.os.StrictMode
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.annotation.RequiresApi
import androidx.compose.runtime.Composable
import androidx.core.app.NotificationManagerCompat
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.launch
import mozilla.components.compose.base.LinkTextState
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.service.nimbus.evalJexlSafe
import mozilla.components.service.nimbus.messaging.use
import mozilla.components.support.base.ext.areNotificationsEnabledSafe
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.Browsers
import mozilla.components.support.utils.BuildManufacturerChecker
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.initializeGlean
import org.mozilla.fenix.components.metrics.Event
import org.mozilla.fenix.components.metrics.installSourcePackage
import org.mozilla.fenix.components.startMetricsIfEnabled
import org.mozilla.fenix.ext.application
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.isDefaultBrowserPromptSupported
import org.mozilla.fenix.ext.isLargeScreenSize
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.redesign.view.OnboardingScreenRedesign
import org.mozilla.fenix.onboarding.store.DefaultOnboardingPreferencesRepository
import org.mozilla.fenix.onboarding.store.OnboardingPreferencesMiddleware
import org.mozilla.fenix.onboarding.store.OnboardingState
import org.mozilla.fenix.onboarding.store.OnboardingStore
import org.mozilla.fenix.onboarding.view.Caption
import org.mozilla.fenix.onboarding.view.ManagePrivacyPreferencesDialogFragment
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData
import org.mozilla.fenix.onboarding.view.OnboardingScreen
import org.mozilla.fenix.onboarding.view.sequencePosition
import org.mozilla.fenix.onboarding.view.telemetrySequenceId
import org.mozilla.fenix.onboarding.view.toPageUiData
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.canShowAddSearchWidgetPrompt
import org.mozilla.fenix.utils.maybeShowAddSearchWidgetPrompt

/**
 * Fragment displaying the onboarding flow.
 */
class OnboardingFragment : Fragment() {
    private val logger = Logger("OnboardingFragment")

    private val removeMarketingFeature = ViewBoundFeatureWrapper<MarketingPageRemovalSupport>()

    private val termsOfServiceEventHandler by lazy {
        DefaultOnboardingTermsOfServiceEventHandler(
            telemetryRecorder = telemetryRecorder,
            openLink = this::launchSandboxCustomTab,
            showManagePrivacyPreferencesDialog = this::showPrivacyPreferencesDialog,
            settings = requireContext().settings(),
            startGlean = ::startGlean,
        )
    }

    private val pagesToDisplay by lazy {
        with(requireContext()) {
            if (settings().rtamoAddonDownloadUrl.isNotBlank()) {
                pagesToDisplay(
                    showDefaultBrowserPage = false,
                    showNotificationPage = false,
                    showAddWidgetPage = false,
                ).filter {
                    it.type == OnboardingPageUiData.Type.TERMS_OF_SERVICE
                }.toMutableList()
            } else {
                pagesToDisplay(
                    showDefaultBrowserPage = displayDefaultBrowserPage(this),
                    showNotificationPage = canShowNotificationPage(this),
                    showAddWidgetPage = AppWidgetManager.getInstance(requireContext())
                        ?.let { canShowAddSearchWidgetPrompt(it) }
                        ?: false,
                ).toMutableList()
            }
        }
    }

    private fun displayDefaultBrowserPage(context: Context): Boolean = with(context) {
        isNotDefaultBrowser(this) && (!isDefaultBrowserPromptSupported() || settings().useOnboardingRedesign)
    }

    private val telemetryRecorder by lazy {
        OnboardingTelemetryRecorder(
            onboardingReason = if (requireComponents.settings.enablePersistentOnboarding) {
                OnboardingReason.EXISTING_USER
            } else {
                OnboardingReason.NEW_USER
            },
            installSource = installSourcePackage(
                packageManager = requireContext().application.packageManager,
                packageName = requireContext().application.packageName,
            ),
        )
    }

    private val onboardingStore by fragmentStore(OnboardingState()) {
        OnboardingStore(
            initialState = it,
            middleware = listOf(
                OnboardingPreferencesMiddleware(
                    repository = DefaultOnboardingPreferencesRepository(
                        context = requireContext(),
                        lifecycleOwner = viewLifecycleOwner,
                    ),
                ),
            ),
        )
    }

    private val defaultBrowserPromptStorage by lazy { DefaultDefaultBrowserPromptStorage(requireContext()) }
    private val defaultBrowserPromptManager by lazy {
        DefaultBrowserPromptManager(
            storage = defaultBrowserPromptStorage,
            promptToSetAsDefaultBrowser = {
                requireContext().components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
                    promptToSetAsDefaultBrowser()
                }
            },
        )
    }

    @SuppressLint("SourceLockedOrientationActivity")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val context = requireContext()
        if (pagesToDisplay.isEmpty()) {
            // do not continue if there's no onboarding pages to display
            onFinish(null)
        }

        if (!isLargeScreenSize()) {
            activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        }

        // We want the prompt to be displayed once per onboarding opening.
        // In case the host got recreated, we don't reset the flag.
        if (savedInstanceState == null) {
            defaultBrowserPromptStorage.promptToSetAsDefaultBrowserDisplayedInOnboarding = false
        }

        telemetryRecorder.onOnboardingStarted()
    }

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        FirefoxTheme {
            if (requireContext().settings().useOnboardingRedesign) {
                ScreenContentRedesign()
            } else {
                ScreenContent()
            }
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        removeMarketingFeature.set(
            feature = MarketingPageRemovalSupport(
                prefKey = requireContext().getString(R.string.pref_key_should_show_marketing_onboarding),
                pagesToDisplay = pagesToDisplay,
                settings = requireContext().settings(),
                lifecycleOwner = viewLifecycleOwner,
            ),
            owner = this,
            view = view,
        )
        super.onViewCreated(view, savedInstanceState)
    }

    override fun onResume() {
        super.onResume()
        hideToolbar()
    }

    @SuppressLint("SourceLockedOrientationActivity")
    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (!isLargeScreenSize()) {
            activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        } else {
            activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (!isLargeScreenSize()) {
            activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }
    }

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    @Composable
    @Suppress("LongMethod")
    private fun ScreenContentRedesign() {
        OnboardingScreenRedesign(
            pagesToDisplay = pagesToDisplay,
            onMakeFirefoxDefaultClick = {
                promptToSetAsDefaultBrowser()
            },
            onSkipDefaultClick = {
                telemetryRecorder.onSkipSetToDefaultClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.DEFAULT_BROWSER),
                )
            },
            onSignInButtonClick = {
                findNavController().nav(
                    id = R.id.onboardingFragment,
                    directions = OnboardingFragmentDirections.actionGlobalTurnOnSync(
                        entrypoint = FenixFxAEntryPoint.NewUserOnboarding,
                    ),
                )
                telemetryRecorder.onSyncSignInClick(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    sequencePosition = pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.SYNC_SIGN_IN),
                )
            },
            onSkipSignInClick = {
                telemetryRecorder.onSkipSignInClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.SYNC_SIGN_IN),
                )
            },
            onNotificationPermissionButtonClick = {
                requireComponents.notificationsDelegate.requestNotificationPermission()
                telemetryRecorder.onNotificationPermissionClick(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    sequencePosition =
                        pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.NOTIFICATION_PERMISSION),
                )
            },
            onSkipNotificationClick = {
                telemetryRecorder.onSkipTurnOnNotificationsClick(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    sequencePosition =
                        pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.NOTIFICATION_PERMISSION),
                )
            },
            onAddFirefoxWidgetClick = {
                telemetryRecorder.onAddSearchWidgetClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.ADD_SEARCH_WIDGET),
                )
                maybeShowAddSearchWidgetPrompt(requireActivity())
            },
            onSkipFirefoxWidgetClick = {
                telemetryRecorder.onSkipAddWidgetClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.ADD_SEARCH_WIDGET),
                )
            },
            onFinish = {
                onFinish(it)
            },
            onImpression = {
                telemetryRecorder.onImpression(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    pageType = it.type,
                    sequencePosition = pagesToDisplay.sequencePosition(it.type),
                )

                defaultBrowserPromptManager.maybePromptToSetAsDefaultBrowser(
                    pagesToDisplay = pagesToDisplay,
                    currentCard = it,
                )
            },
            onboardingStore = onboardingStore,
            termsOfServiceEventHandler = termsOfServiceEventHandler,
            onCustomizeToolbarClick = {
                requireContext().settings().hasCompletedSetupStepToolbar = true

                telemetryRecorder.onSelectToolbarPlacementClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.TOOLBAR_PLACEMENT),
                    onboardingStore.state.toolbarOptionSelected.id,
                )
            },
            onMarketingDataLearnMoreClick = {
                telemetryRecorder.onMarketingDataLearnMoreClick()

                val url = SupportUtils.getSumoURLForTopic(
                    requireContext(),
                    SupportUtils.SumoTopic.MARKETING_DATA,
                )
                launchSandboxCustomTab(url)
            },
            onMarketingOptInToggle = { optIn ->
                telemetryRecorder.onMarketingDataOptInToggled(optIn)
            },
            onMarketingDataContinueClick = { allowMarketingDataCollection ->
                with(requireContext().settings()) {
                    isMarketingTelemetryEnabled = allowMarketingDataCollection
                    hasMadeMarketingTelemetrySelection = true
                }
                telemetryRecorder.onMarketingDataContinueClicked(allowMarketingDataCollection)
            },
            onMarketingDataSkipClick = {
                telemetryRecorder.onMarketingDataSkipClicked()
            },
            currentIndex = { index ->
                removeMarketingFeature.withFeature { it.currentPageIndex = index }
            },
            onNavigateToNextPage = {
                telemetryRecorder.onNavigatedToNextPage()
            },
        )
    }

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    @Composable
    @Suppress("LongMethod")
    private fun ScreenContent() {
        OnboardingScreen(
            pagesToDisplay = pagesToDisplay,
            onMakeFirefoxDefaultClick = {
                promptToSetAsDefaultBrowser()
            },
            onSkipDefaultClick = {
                telemetryRecorder.onSkipSetToDefaultClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.DEFAULT_BROWSER),
                )
            },
            onSignInButtonClick = {
                findNavController().nav(
                    id = R.id.onboardingFragment,
                    directions = OnboardingFragmentDirections.actionGlobalTurnOnSync(
                        entrypoint = FenixFxAEntryPoint.NewUserOnboarding,
                    ),
                )
                telemetryRecorder.onSyncSignInClick(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    sequencePosition = pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.SYNC_SIGN_IN),
                )
            },
            onSkipSignInClick = {
                telemetryRecorder.onSkipSignInClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.SYNC_SIGN_IN),
                )
            },
            onNotificationPermissionButtonClick = {
                requireComponents.notificationsDelegate.requestNotificationPermission()
                telemetryRecorder.onNotificationPermissionClick(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    sequencePosition =
                        pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.NOTIFICATION_PERMISSION),
                )
            },
            onSkipNotificationClick = {
                telemetryRecorder.onSkipTurnOnNotificationsClick(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    sequencePosition =
                        pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.NOTIFICATION_PERMISSION),
                )
            },
            onAddFirefoxWidgetClick = {
                telemetryRecorder.onAddSearchWidgetClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.ADD_SEARCH_WIDGET),
                )
                maybeShowAddSearchWidgetPrompt(requireActivity())
            },
            onSkipFirefoxWidgetClick = {
                telemetryRecorder.onSkipAddWidgetClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.ADD_SEARCH_WIDGET),
                )
            },
            onFinish = {
                onFinish(it)
            },
            onImpression = {
                telemetryRecorder.onImpression(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    pageType = it.type,
                    sequencePosition = pagesToDisplay.sequencePosition(it.type),
                )

                defaultBrowserPromptManager.maybePromptToSetAsDefaultBrowser(
                    pagesToDisplay = pagesToDisplay,
                    currentCard = it,
                )
            },
            onboardingStore = onboardingStore,
            termsOfServiceEventHandler = termsOfServiceEventHandler,
            onCustomizeToolbarClick = {
                requireContext().settings().hasCompletedSetupStepToolbar = true

                telemetryRecorder.onSelectToolbarPlacementClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.TOOLBAR_PLACEMENT),
                    onboardingStore.state.toolbarOptionSelected.id,
                )
            },
            onMarketingDataLearnMoreClick = {
                telemetryRecorder.onMarketingDataLearnMoreClick()

                val url = SupportUtils.getSumoURLForTopic(
                    requireContext(),
                    SupportUtils.SumoTopic.MARKETING_DATA,
                )
                launchSandboxCustomTab(url)
            },
            onMarketingOptInToggle = { optIn ->
                telemetryRecorder.onMarketingDataOptInToggled(optIn)
            },
            onMarketingDataContinueClick = { allowMarketingDataCollection ->
                with(requireContext().settings()) {
                    isMarketingTelemetryEnabled = allowMarketingDataCollection
                    hasMadeMarketingTelemetrySelection = true
                }
                telemetryRecorder.onMarketingDataContinueClicked(allowMarketingDataCollection)
            },
            currentIndex = { index ->
                removeMarketingFeature.withFeature { it.currentPageIndex = index }
            },
            onCustomizeThemeClick = {
                requireContext().settings().hasCompletedSetupStepTheme = true

                telemetryRecorder.onSelectThemeClick(
                    onboardingStore.state.themeOptionSelected.id,
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.THEME_SELECTION),
                )
            },
            onNavigateToNextPage = {
                telemetryRecorder.onNavigatedToNextPage()
            },
        )
    }

    private fun startGlean() {
        val settings = requireContext().settings()
        viewLifecycleOwner.lifecycleScope.launch {
            initializeGlean(
                requireContext().applicationContext,
                logger,
                settings.isTelemetryEnabled,
                requireComponents.core.client,
            )
        }

        if (!settings.isTelemetryEnabled) {
            Pings.onboardingOptOut.setEnabled(true)
            Pings.onboardingOptOut.submit()
        }

        // The marketing telemetry may be enabled after finishing onboarding.
        startMetricsIfEnabled(
            logger = logger,
            analytics = requireComponents.analytics,
            isTelemetryEnabled = settings.isTelemetryEnabled,
            isMarketingTelemetryEnabled = false,
            isDailyUsagePingEnabled = settings.isDailyUsagePingEnabled,
        )
    }

    private fun onFinish(onboardingPageUiData: OnboardingPageUiData?) {
        // onboarding page UI data can be null if there was no pages to display
        if (onboardingPageUiData != null) {
            val sequenceId = pagesToDisplay.telemetrySequenceId()
            val sequencePosition = pagesToDisplay.sequencePosition(onboardingPageUiData.type)

            telemetryRecorder.onOnboardingComplete(
                sequenceId = sequenceId,
                sequencePosition = sequencePosition,
            )
        }

        requireComponents.fenixOnboarding.finish()

        val settings = requireContext().settings()
        settings.onboardingCompletedTimestamp = System.currentTimeMillis()

        // Telemetry and daily usage ping get enabled after ToU acceptance.
        startMetricsIfEnabled(
            logger = logger,
            analytics = requireComponents.analytics,
            isTelemetryEnabled = false,
            isMarketingTelemetryEnabled = settings.isMarketingTelemetryEnabled,
            isDailyUsagePingEnabled = false,
        )

        requireComponents.analytics.metrics.track(Event.GrowthData.ConversionEvent6)

        val downloadUrl = settings.rtamoAddonDownloadUrl
        if (downloadUrl.isNotBlank()) {
            settings.rtamoAddonDownloadUrl = ""
            requireComponents.addonManager.installAddon(
                url = downloadUrl,
                installationMethod = InstallationMethod.RTAMO,
            )
        }

        findNavController().nav(
            id = R.id.onboardingFragment,
            directions = OnboardingFragmentDirections.actionHome(),
        )

        maybeAddMenuNotification()
    }

    private fun isNotDefaultBrowser(context: Context) =
        !Browsers.isDefaultBrowser(context)

    private fun canShowNotificationPage(context: Context) =
        !NotificationManagerCompat.from(context.applicationContext)
            .areNotificationsEnabledSafe() && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU

    private fun pagesToDisplay(
        showDefaultBrowserPage: Boolean,
        showNotificationPage: Boolean,
        showAddWidgetPage: Boolean,
    ): List<OnboardingPageUiData> {
        val jexlConditions = FxNimbus.features.junoOnboarding.value().conditions
        val jexlHelper = requireContext().components.nimbus.createJexlHelper()

        val privacyCaption = Caption(
            text = getString(R.string.juno_onboarding_privacy_notice_text),
            linkTextState = LinkTextState(
                text = getString(R.string.juno_onboarding_privacy_notice_text),
                url = SupportUtils.getMozillaPageUrl(SupportUtils.MozillaPage.PRIVACY_NOTICE),
                onClick = {
                    SupportUtils.launchSandboxCustomTab(
                        context = requireContext(),
                        url = it,
                    )
                    telemetryRecorder.onPrivacyPolicyClick(
                        pagesToDisplay.telemetrySequenceId(),
                        pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.DEFAULT_BROWSER),
                    )
                },
            ),
        )
        return jexlHelper.use {
            FxNimbus.features.junoOnboarding.value().cards.values.toPageUiData(
                privacyCaption,
                showDefaultBrowserPage,
                showNotificationPage,
                showAddWidgetPage,
                requireContext().settings().isTabStripEnabled.not(),
                jexlConditions,
                BuildManufacturerChecker(),
            ) { condition -> jexlHelper.evalJexlSafe(condition) }
        }
    }

    private fun promptToSetAsDefaultBrowser() {
        activity?.openSetDefaultBrowserOption(useCustomTab = true)
        requireContext().settings().coldStartsBetweenSetAsDefaultPrompts = 0
        requireContext().settings().lastSetAsDefaultPromptShownTimeInMillis = System.currentTimeMillis()
        telemetryRecorder.onSetToDefaultClick(
            sequenceId = pagesToDisplay.telemetrySequenceId(),
            sequencePosition = pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.DEFAULT_BROWSER),
        )
    }

    private fun launchSandboxCustomTab(url: String) =
        SupportUtils.launchSandboxCustomTab(requireContext(), url)

    private fun showPrivacyPreferencesDialog() {
        ManagePrivacyPreferencesDialogFragment()
            .show(parentFragmentManager, ManagePrivacyPreferencesDialogFragment.TAG)
    }

    private fun maybeAddMenuNotification() {
        with(requireContext()) {
            if (shouldAddMenuNotification()) {
                requireComponents.appStore.dispatch(
                    AppAction.MenuNotification.AddMenuNotification(
                        SupportedMenuNotifications.NotDefaultBrowser,
                    ),
                )
            }
        }
    }

    private fun shouldAddMenuNotification() =
        with(requireContext()) { !settings().isDefaultBrowser && settings().shouldShowMenuBanner }
}

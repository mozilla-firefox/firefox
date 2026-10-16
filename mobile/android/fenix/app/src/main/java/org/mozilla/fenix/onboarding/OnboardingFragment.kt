/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.os.Bundle
import android.os.StrictMode
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.runtime.Composable
import androidx.compose.runtime.toMutableStateList
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.service.nimbus.evalJexlSafe
import mozilla.components.service.nimbus.messaging.use
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.Browsers
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.initializeGlean
import org.mozilla.fenix.components.metrics.InstallReferrerHandlingService
import org.mozilla.fenix.components.metrics.RtamoAttributionHandler
import org.mozilla.fenix.components.metrics.installSourcePackage
import org.mozilla.fenix.components.startMetricsIfEnabled
import org.mozilla.fenix.ext.application
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.isLargeScreenSize
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.view.ManagePrivacyPreferencesDialogFragment
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData
import org.mozilla.fenix.onboarding.view.OnboardingScreen
import org.mozilla.fenix.onboarding.view.sequencePosition
import org.mozilla.fenix.onboarding.view.telemetrySequenceId
import org.mozilla.fenix.onboarding.view.toPageUiData
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.FirefoxTheme

/** Fragment displaying the onboarding flow. */
class OnboardingFragment : Fragment() {
    private val logger = Logger("OnboardingFragment")

    private val addMarketingFeature = ViewBoundFeatureWrapper<OnboardingPageAdditionSupport>()
    private val addPairingSyncSignInFeature = ViewBoundFeatureWrapper<OnboardingPageAdditionSupport>()

    private val rtamoAttributionHandler by lazy {
        RtamoAttributionHandler(
            requireContext(),
            requireComponents.settings,
            requireComponents.addonsProvider,
            requireComponents.applicationScope,
        )
    }

    private val termsOfServiceEventHandler by lazy {
        DefaultOnboardingTermsOfServiceEventHandler(
            telemetryRecorder = telemetryRecorder,
            openLink = this::launchSandboxCustomTab,
            showManagePrivacyPreferencesDialog = this::showPrivacyPreferencesDialog,
            settings = requireComponents.settings,
            startGlean = ::startGlean,
        )
    }

    private val allOnboardingPages by lazy {
        pagesToDisplay(showDefaultBrowserPage = displayDefaultBrowserPage(requireContext()))
    }

    private val marketingPage by lazy {
        allOnboardingPages.find { it.type == OnboardingPageUiData.Type.MARKETING_DATA }
    }

    /**
     * The Sync sign-in page available for pairing campaigns.
     *
     * Null if the page is absent or the prompt is disabled via Nimbus, preventing [addPairingSyncSignInFeature] from
     * adding it when attribution resolves later.
     */
    private val pairingSyncSignInPage by lazy {
        allOnboardingPages
            .find { it.type == OnboardingPageUiData.Type.SYNC_SIGN_IN }
            ?.takeIf { requireComponents.settings.isPairingSignInPromptEnabled }
    }

    private val pagesToDisplay by lazy {
        val settings = requireComponents.settings

        allOnboardingPages
            .filter { page ->
                when (page.type) {
                    OnboardingPageUiData.Type.MARKETING_DATA -> settings.shouldShowMarketingOnboarding
                    OnboardingPageUiData.Type.SYNC_SIGN_IN ->
                        pairingSyncSignInPage != null && settings.isUserPairingCampaignAttributed

                    else -> true
                }
            }
            .distinctBy { it.type }
            .toMutableStateList()
    }

    private fun displayDefaultBrowserPage(context: Context): Boolean = isNotDefaultBrowser(context)

    private val telemetryRecorder by lazy {
        OnboardingTelemetryRecorder(
            onboardingReason =
                if (requireComponents.settings.enablePersistentOnboarding) {
                    OnboardingReason.EXISTING_USER
                } else {
                    OnboardingReason.NEW_USER
                },
            installSource =
                installSourcePackage(
                    packageManager = requireContext().application.packageManager,
                    packageName = requireContext().application.packageName,
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

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        FirefoxTheme {
            ScreenContent()
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        addMarketingFeature.set(
            feature =
                OnboardingPageAdditionSupport(
                    prefKey = requireContext().getString(R.string.pref_key_should_show_marketing_onboarding),
                    pagesToDisplay = pagesToDisplay,
                    settings = requireComponents.settings,
                    page = marketingPage,
                    lifecycleOwner = viewLifecycleOwner,
                ),
            owner = this,
            view = view,
        )
        addPairingSyncSignInFeature.set(
            feature =
                OnboardingPageAdditionSupport(
                    prefKey = requireContext().getString(R.string.pref_key_is_user_pairing_campaign_attributed),
                    pagesToDisplay = pagesToDisplay,
                    settings = requireComponents.settings,
                    page = pairingSyncSignInPage,
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

    @Composable
    @Suppress("LongMethod")
    private fun ScreenContent() {
        OnboardingScreen(
            pagesToDisplay = pagesToDisplay,
            initialPageIndex = requireComponents.settings.onboardingCurrentPageIndex,
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
                findNavController()
                    .nav(
                        id = R.id.onboardingFragment,
                        directions =
                            OnboardingFragmentDirections.actionGlobalTurnOnSync(
                                entrypoint = FenixFxAEntryPoint.NewUserOnboarding
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
            onFinish = {
                onFinish(it)
            },
            onImpression = {
                telemetryRecorder.onImpression(
                    sequenceId = pagesToDisplay.telemetrySequenceId(),
                    pageType = it.type,
                    sequencePosition = pagesToDisplay.sequencePosition(it.type),
                )

                defaultBrowserPromptManager.maybePromptToSetAsDefaultBrowser(it)
            },
            termsOfServiceEventHandler = termsOfServiceEventHandler,
            onMarketingDataLearnMoreClick = {
                telemetryRecorder.onMarketingDataLearnMoreClick()

                val url =
                    SupportUtils.getSumoURLForTopic(
                        requireContext(),
                        SupportUtils.SumoTopic.MARKETING_DATA,
                    )
                launchSandboxCustomTab(url)
            },
            onMarketingOptInToggle = { optIn ->
                telemetryRecorder.onMarketingDataOptInToggled(optIn)
            },
            onMarketingDataContinueClick = { allowMarketingDataCollection ->
                with(requireComponents.settings) {
                    isMarketingTelemetryEnabled = allowMarketingDataCollection
                    hasMadeMarketingTelemetrySelection = true
                }
                telemetryRecorder.onMarketingDataContinueClicked(allowMarketingDataCollection)
            },
            onMarketingDataSkipClick = {
                telemetryRecorder.onMarketingDataSkipClicked()
            },
            currentIndex = { index ->
                requireComponents.settings.onboardingCurrentPageIndex = index
            },
            onNavigateToNextPage = {
                telemetryRecorder.onNavigatedToNextPage()
            },
        )
    }

    private fun startGlean() {
        val settings = requireComponents.settings
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

        rtamoAttributionHandler.handleReferrer(InstallReferrerHandlingService.response)

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

        val settings = requireComponents.settings

        // Telemetry and daily usage ping get enabled after ToU acceptance.
        startMetricsIfEnabled(
            logger = logger,
            analytics = requireComponents.analytics,
            isTelemetryEnabled = false,
            isMarketingTelemetryEnabled = settings.isMarketingTelemetryEnabled,
            isDailyUsagePingEnabled = false,
        )

        findNavController()
            .nav(
                id = R.id.onboardingFragment,
                directions = OnboardingFragmentDirections.actionHome(),
            )

        // finish() synchronously notifies the onboarding completed listener in HomeActivity, which replays the launch
        // intent and may navigate to the browser. Navigating home first keeps home on the back stack beneath it.
        requireComponents.fenixOnboarding.finish()

        settings.recordOnboardingCompleted()
        settings.onboardingCurrentPageIndex = 0

        val downloadUrl = settings.rtamoAddonDownloadUrl
        if (downloadUrl.isNotBlank()) {
            requireComponents.core.store.dispatch(
                WebExtensionAction.UpdatePromptRequestWebExtensionAction(
                    WebExtensionPromptRequest.InstallationRequested(
                        url = downloadUrl,
                        name = settings.rtamoAddonName,
                        iconUrl = settings.rtamoAddonImageUrl,
                        installationMethod = InstallationMethod.RTAMO,
                    )
                )
            )
        }
        settings.rtamoAddonDownloadUrl = ""
        settings.rtamoAddonName = ""
        settings.rtamoAddonImageUrl = ""

        maybeAddMenuNotification()
    }

    private fun isNotDefaultBrowser(context: Context) = !Browsers.isDefaultBrowser(context)

    private fun pagesToDisplay(showDefaultBrowserPage: Boolean): List<OnboardingPageUiData> {
        val jexlConditions = FxNimbus.features.junoOnboarding.value().conditions
        val jexlHelper = requireContext().components.nimbus.createJexlHelper()

        return jexlHelper.use {
            FxNimbus.features.junoOnboarding.value().cards.values.toPageUiData(
                showDefaultBrowserPage,
                jexlConditions,
            ) { condition ->
                jexlHelper.evalJexlSafe(condition)
            }
        }
    }

    private fun promptToSetAsDefaultBrowser() {
        activity?.openSetDefaultBrowserOption(useCustomTab = true)
        requireComponents.settings.coldStartsBetweenSetAsDefaultPrompts = 0
        requireComponents.settings.recordSetAsDefaultPromptShownTime()
        telemetryRecorder.onSetToDefaultClick(
            sequenceId = pagesToDisplay.telemetrySequenceId(),
            sequencePosition = pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.DEFAULT_BROWSER),
        )
    }

    private fun launchSandboxCustomTab(url: String) = SupportUtils.launchSandboxCustomTab(requireContext(), url)

    private fun showPrivacyPreferencesDialog() {
        ManagePrivacyPreferencesDialogFragment().show(parentFragmentManager, ManagePrivacyPreferencesDialogFragment.TAG)
    }

    private fun maybeAddMenuNotification() {
        with(requireContext()) {
            if (shouldAddMenuNotification()) {
                requireComponents.appStore.dispatch(
                    AppAction.MenuNotification.AddMenuNotification(SupportedMenuNotifications.NotDefaultBrowser)
                )
            }
        }
    }

    private fun shouldAddMenuNotification() =
        with(requireComponents) { !settings.isDefaultBrowser && settings.shouldShowMenuBanner }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import android.annotation.SuppressLint
import android.content.Context
import android.content.IntentFilter
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
import androidx.compose.ui.platform.ComposeView
import androidx.fragment.app.Fragment
import androidx.localbroadcastmanager.content.LocalBroadcastManager
import androidx.navigation.fragment.findNavController
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.service.nimbus.evalJexlSafe
import mozilla.components.service.nimbus.messaging.use
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.BrowsersCache
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.initializeGlean
import org.mozilla.fenix.components.lazyStore
import org.mozilla.fenix.components.startMetricsIfEnabled
import org.mozilla.fenix.compose.LinkTextState
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.isDefaultBrowserPromptSupported
import org.mozilla.fenix.ext.isLargeScreenSize
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.store.DefaultOnboardingPreferencesRepository
import org.mozilla.fenix.onboarding.store.OnboardingAction.OnboardingAddOnsAction
import org.mozilla.fenix.onboarding.store.OnboardingAddonStatus
import org.mozilla.fenix.onboarding.store.OnboardingPreferencesMiddleware
import org.mozilla.fenix.onboarding.store.OnboardingStore
import org.mozilla.fenix.onboarding.view.Caption
import org.mozilla.fenix.onboarding.view.ManagePrivacyPreferencesDialogFragment
import org.mozilla.fenix.onboarding.view.OnboardingAddOn
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData
import org.mozilla.fenix.onboarding.view.OnboardingScreen
import org.mozilla.fenix.onboarding.view.sequencePosition
import org.mozilla.fenix.onboarding.view.telemetrySequenceId
import org.mozilla.fenix.onboarding.view.toPageUiData
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.canShowAddSearchWidgetPrompt
import org.mozilla.fenix.utils.showAddSearchWidgetPrompt

/**
 * Fragment displaying the onboarding flow.
 */
class OnboardingFragment : Fragment() {
    private val logger = Logger("OnboardingFragment")

    private val termsOfServiceEventHandler by lazy {
        DefaultOnboardingTermsOfServiceEventHandler(
            telemetryRecorder = telemetryRecorder,
            openLink = this::launchSandboxCustomTab,
            showManagePrivacyPreferencesDialog = this::showPrivacyPreferencesDialog,
            settings = requireContext().settings(),
        )
    }

    private val pagesToDisplay by lazy {
        with(requireContext()) {
            pagesToDisplay(
                showDefaultBrowserPage = isNotDefaultBrowser(this) && !isDefaultBrowserPromptSupported(),
                showNotificationPage = canShowNotificationPage(),
                showAddWidgetPage = canShowAddSearchWidgetPrompt(),
            ).toMutableList()
        }
    }
    private val telemetryRecorder by lazy { OnboardingTelemetryRecorder() }

    private val onboardingStore by lazyStore {
        OnboardingStore(
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

    private val pinAppWidgetReceiver = WidgetPinnedReceiver()
    private val defaultBrowserPromptStorage by lazy { DefaultDefaultBrowserPromptStorage(requireContext()) }
    private val defaultBrowserPromptManager by lazy {
        DefaultBrowserPromptManager(
            storage = defaultBrowserPromptStorage,
            promptToSetAsDefaultBrowser = {
                requireContext().components.strictMode.resetAfter(StrictMode.allowThreadDiskReads()) {
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
            /* do not continue if there's no onboarding pages to display */
            onFinish(null)
        }

        if (!isLargeScreenSize()) {
            activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
        }
        val filter = IntentFilter(WidgetPinnedReceiver.ACTION)
        LocalBroadcastManager.getInstance(context)
            .registerReceiver(pinAppWidgetReceiver, filter)

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
    ): View = ComposeView(requireContext()).apply {
        setContent {
            FirefoxTheme {
                ScreenContent()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        hideToolbar()
        maybeResetBrowserCache()
    }

    /**
     * If the user was shown the default browser prompt, we reset the browsers cache.
     *
     * In a general case, the cache is cleared every [HomeActivity.onPause] to guarantee correct
     * data, but in a case of a default browser prompt during onboarding, a queued
     * [FenixApplication.setStartupMetrics] call breaks that mechanism. The call repopulates
     * the cache while the user is still choosing a browser.
     */
    private fun maybeResetBrowserCache() {
        if (defaultBrowserPromptStorage.promptToSetAsDefaultBrowserDisplayedInOnboarding) {
            BrowsersCache.resetAll()
        }
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
        LocalBroadcastManager.getInstance(requireContext()).unregisterReceiver(pinAppWidgetReceiver)
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
                showAddSearchWidgetPrompt(requireActivity())
            },
            onSkipFirefoxWidgetClick = {
                telemetryRecorder.onSkipAddWidgetClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.ADD_SEARCH_WIDGET),
                )
            },
            onAddOnsButtonClick = {
                telemetryRecorder.onAddOnsButtonClick(
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.ADD_ONS),
                )
            },
            onFinish = {
                onFinish(it)
                disableNavBarCFRForNewUser()
                enableSearchBarCFRForNewUser()
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
            onInstallAddOnButtonClick = { installUrl -> installAddon(installUrl) },
            termsOfServiceEventHandler = termsOfServiceEventHandler,
            onCustomizeToolbarClick = {
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
            onCustomizeThemeClick = {
                telemetryRecorder.onSelectThemeClick(
                    onboardingStore.state.themeOptionSelected.id,
                    pagesToDisplay.telemetrySequenceId(),
                    pagesToDisplay.sequencePosition(OnboardingPageUiData.Type.THEME_SELECTION),
                )
            },
        )
    }

    private fun installAddon(addOn: OnboardingAddOn) {
        onboardingStore.dispatch(
            OnboardingAddOnsAction.UpdateStatus(
                addOnId = addOn.id,
                status = OnboardingAddonStatus.INSTALLING,
            ),
        )
        requireComponents.addonManager.installAddon(
            url = addOn.installUrl,
            installationMethod = InstallationMethod.ONBOARDING,
            onSuccess = { addon ->
                logger.info("Extension installed successfully")
                telemetryRecorder.onAddOnInstalled(addon.id)
                onboardingStore.dispatch(
                    OnboardingAddOnsAction.UpdateStatus(
                        addOnId = addOn.id,
                        status = OnboardingAddonStatus.INSTALLED,
                    ),
                )
            },
            onError = { e ->
                logger.error("Unable to install extension", e)
                onboardingStore.dispatch(
                    OnboardingAddOnsAction.UpdateStatus(
                        addOn.id,
                        status = OnboardingAddonStatus.NOT_INSTALLED,
                    ),
                )
            },
        )
    }

    private fun onFinish(onboardingPageUiData: OnboardingPageUiData?) {
        /* onboarding page UI data can be null if there was no pages to display */
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
        initializeGlean(
            requireContext().applicationContext,
            logger,
            settings.isTelemetryEnabled,
            requireComponents.core.client,
        )
        if (!settings.isTelemetryEnabled) {
            Pings.onboardingOptOut.setEnabled(true)
            Pings.onboardingOptOut.submit()
        }

        startMetricsIfEnabled(
            logger = logger,
            analytics = requireComponents.analytics,
            isTelemetryEnabled = settings.isTelemetryEnabled,
            isMarketingTelemetryEnabled = settings.isMarketingTelemetryEnabled,
            isDailyUsagePingEnabled = settings.isDailyUsagePingEnabled,
        )

        findNavController().nav(
            id = R.id.onboardingFragment,
            directions = OnboardingFragmentDirections.actionHome(),
        )
    }

    private fun disableNavBarCFRForNewUser() {
        requireContext().settings().shouldShowNavigationBarCFR = false
    }

    private fun enableSearchBarCFRForNewUser() {
        requireContext().settings().shouldShowSearchBarCFR = FxNimbus.features.encourageSearchCfr.value().enabled
    }

    private fun isNotDefaultBrowser(context: Context) =
        !BrowsersCache.all(context.applicationContext).isDefaultBrowser

    private fun canShowNotificationPage() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU

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
                url = SupportUtils.getMozillaPageUrl(SupportUtils.MozillaPage.PRIVATE_NOTICE),
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
                jexlConditions,
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
}

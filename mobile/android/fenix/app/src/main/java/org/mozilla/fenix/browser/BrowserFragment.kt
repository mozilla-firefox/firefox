/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import android.content.Context
import android.content.Intent
import android.hardware.SensorManager
import android.os.StrictMode
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.VisibleForTesting
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.Observer
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.thumbnails.BrowserThumbnails
import mozilla.components.concept.engine.HitResult
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.contextmenu.ContextMenuCandidate
import mozilla.components.feature.readerview.ReaderViewFeature
import mozilla.components.feature.tab.collections.TabCollection
import mozilla.components.feature.tabs.WindowFeature
import mozilla.components.lib.accelerometer.sensormanager.LifecycleAwareSensorManagerAccelerometer
import mozilla.components.lib.shake.detectShakes
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.kotlin.isContentUrl
import org.mozilla.fenix.GleanMetrics.Translations
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.store.BrowserScreenAction.ReaderModeStatusUpdated
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.LensFeature
import org.mozilla.fenix.components.QrScanFenixFeature
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.VoiceSearchFeature
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.metrics.installSourcePackage
import org.mozilla.fenix.components.toolbar.gestures.ToolbarHorizontalGesturesHandler
import org.mozilla.fenix.components.toolbar.gestures.ToolbarVerticalGesturesHandler
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.application
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isGoogleSearchEngine
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.navigateSafe
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.OnboardingFragmentDirections
import org.mozilla.fenix.onboarding.OnboardingReason
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingFeatureDefault
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingStageProviderDefault
import org.mozilla.fenix.settings.downloads.DownloadLocationManager
import org.mozilla.fenix.settings.quicksettings.protections.cookiebanners.getCookieBannerUIMode
import org.mozilla.fenix.shortcut.PwaOnboardingObserver
import org.mozilla.fenix.termsofuse.store.Surface
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.ipprotection.store.Surface as IPProtectionSurface

/**
 * Fragment used for browsing the web within the main app.
 */
@Suppress("TooManyFunctions", "LargeClass")
class BrowserFragment : BaseBrowserFragment(), UserInteractionHandler, SystemInsetsPaddedFragment {
    private val windowFeature = ViewBoundFeatureWrapper<WindowFeature>()
    private val openInAppOnboardingObserver = ViewBoundFeatureWrapper<OpenInAppOnboardingObserver>()
    private val translationsBinding = ViewBoundFeatureWrapper<TranslationsBinding>()
    private val translationsBannerIntegration = ViewBoundFeatureWrapper<TranslationsBannerIntegration>()

    private var qrScanFenixFeature: ViewBoundFeatureWrapper<QrScanFenixFeature>? =
        ViewBoundFeatureWrapper<QrScanFenixFeature>()
    private val qrScanLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            qrScanFenixFeature?.get()?.handleToolbarQrScanResults(result.resultCode, result.data)
        }
    private var voiceSearchFeature: ViewBoundFeatureWrapper<VoiceSearchFeature>? =
        ViewBoundFeatureWrapper<VoiceSearchFeature>()
    private val voiceSearchLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            voiceSearchFeature?.get()?.handleVoiceSearchResult(result.resultCode, result.data)
        }
    private var lensFeature: ViewBoundFeatureWrapper<LensFeature>? =
        ViewBoundFeatureWrapper<LensFeature>()
    private val lensLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            lensFeature?.get()?.handleImageResult(result.resultCode, result.data)
        }
    private val lensCameraPermissionLauncher: ActivityResultLauncher<String> =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            lensFeature?.get()?.onCameraPermissionResult(isGranted)
        }

    private val continuousOnboardingDefaultBrowserLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            continuousOnboardingFeature.onDefaultBrowserStepCompleted(
                activity = requireActivity(),
                resultCode = result.resultCode,
            )
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

    private val continuousOnboardingFeature by lazy {
        val settings = requireContext().settings()
        ContinuousOnboardingFeatureDefault(
            settings = settings,
            telemetryRecorder = telemetryRecorder,
            stageProvider = ContinuousOnboardingStageProviderDefault(settings),
            navigateToSyncSignIn = {
                findNavController().nav(
                    id = R.id.browserFragment,
                    directions = OnboardingFragmentDirections.actionGlobalTurnOnSync(
                        entrypoint = FenixFxAEntryPoint.NewUserOnboarding,
                    ),
                )
            },
        )
    }

    private var pwaOnboardingObserver: PwaOnboardingObserver? = null

    override fun initializeUI(view: View, tab: SessionState) {
        super.initializeUI(view, tab)

        val context = requireContext()
        val components = context.components
        val settings = context.settings()

        setupToolbarSwipeBehavior(settings, components)

        initBrowserToolbarComposableUpdates(view)
        initTranslationsUpdates(context = context, rootView = view)

        thumbnailsFeature.set(
            feature = BrowserThumbnails(context, binding.engineView, components.core.store),
            owner = this,
            view = view,
        )

        windowFeature.set(
            feature = WindowFeature(
                store = components.core.store,
                tabsUseCases = components.useCases.tabsUseCases,
            ),
            owner = this,
            view = view,
        )

        if (settings.shouldShowOpenInAppCfr) {
            openInAppOnboardingObserver.set(
                feature = OpenInAppOnboardingObserver(
                    context = context,
                    store = context.components.core.store,
                    lifecycleOwner = this,
                    navController = findNavController(),
                    settings = settings,
                    appLinksUseCases = context.components.useCases.appLinksUseCases,
                    container = binding.browserLayout as ViewGroup,
                    shouldScrollWithTopToolbar = !settings.shouldUseBottomToolbar,
                ),
                owner = this,
                view = view,
            )
        }

        setupShakeDetection()

        continuousOnboardingFeature.maybeRunContinuousOnboarding(
            activity = requireActivity(),
            launcher = continuousOnboardingDefaultBrowserLauncher,
        )
    }

    private fun setupToolbarSwipeBehavior(settings: Settings, components: Components) {
        if (!settings.isTabStripEnabled && settings.isSwipeToolbarToSwitchTabsEnabled) {
            binding.gestureLayout.addGestureListener(
                ToolbarHorizontalGesturesHandler(
                    activity = requireActivity(),
                    contentLayout = binding.browserLayout,
                    tabPreview = binding.tabPreview,
                    toolbarLayout = browserToolbar.layout,
                    navBarLayout = browserNavigationBar?.layout,
                    store = components.core.store,
                    selectTabUseCase = components.useCases.tabsUseCases.selectTab,
                    onSwipeStarted = {
                        thumbnailsFeature.get()?.requestScreenshot()
                    },
                ),
            )
        }

        if (settings.isSwipeToolbarToShowTabsEnabled) {
            binding.gestureLayout.addGestureListener(
                ToolbarVerticalGesturesHandler(
                    appStore = components.appStore,
                    toolbarLayout = browserToolbar.layout,
                    navBarLayout = browserNavigationBar?.layout,
                    toolbarPosition = settings.toolbarPosition,
                    navController = findNavController(),
                ),
            )
        }
    }

    private fun setupShakeDetection() {
        val shouldSetupShake = requireComponents.core.summarizeFeatureSettings.canShowFeature &&
                requireComponents.core.summarizationSettings.isGestureEnabled.value
        if (!shouldSetupShake) {
            return
        }

        val sensorManager = requireActivity().getSystemService(SensorManager::class.java) ?: return
        val accelerometer = LifecycleAwareSensorManagerAccelerometer(sensorManager)
        with(viewLifecycleOwner) {
            lifecycle.addObserver(accelerometer)
            lifecycleScope.launch {
                viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.STARTED) {
                    accelerometer.detectShakes()
                        .collect {
                            summarizeToolbarCfrBinding.get()?.maybeDismissCfr()
                            navigateToSummarizationIfEligible()
                        }
                }
            }
        }
    }

    private suspend fun navigateToSummarizationIfEligible() {
        findNavController().apply {
            // If the shake gesture or the parent feature was disabled in the bottom sheet hosted
            // settings but the fragment has not been recreated yet, we need to check both are still
            // active before proceeding.
            val summarizationSettings = requireComponents.core.summarizationSettings
            val shakeEnabled = summarizationSettings.isFeatureEnabled.value &&
                summarizationSettings.isGestureEnabled.value

            if (!shakeEnabled) {
                return
            }

            // We don't want to navigate to the summarization fragment if the current
            // tab is private.
            val isPrivate = getSafeCurrentTab()?.content?.private == true

            // We don't want to navigate to the summarization fragment if the current
            // tab is loading.
            val isPageLoading = getSafeCurrentTab()?.content?.loading == true

            // Since the summarization fragment is in a dialog, it's possible that we
            // can still detect shakes in the background. Don't try to navigate twice.
            val currentDestinationIsNotTheBrowser = currentDestination?.id != R.id.browserFragment

            // evaluate this lazy, to try and avoid querying the engine unless necessary
            val isEnglishContent: suspend () -> Boolean = {
                getSafeCurrentTab()?.engineState?.engineSession?.let { session ->
                    requireComponents.core.summarizationEligibilityChecker
                        .checkLanguage(session)
                        .getOrNull()
                } ?: false
            }

            // this can be removed when we get rid of language gating
            @Suppress("ComplexCondition")
            if (isPrivate ||
                isPageLoading ||
                currentDestinationIsNotTheBrowser ||
                !isEnglishContent()
            ) {
                return
            }

            navigate(
                BrowserFragmentDirections.actionBrowserFragmentToSummarizationFragment(
                    true,
                ),
            )
        }
    }

    private fun initBrowserToolbarComposableUpdates(rootView: View) {
        initReaderModeUpdates(rootView.context, rootView)
        qrScanFenixFeature = QrScanFenixFeature.register(this, qrScanLauncher)
        voiceSearchFeature = VoiceSearchFeature.register(this, voiceSearchLauncher)
        lensFeature = LensFeature.register(this, lensLauncher, lensCameraPermissionLauncher)
    }

    private fun initReaderModeUpdates(context: Context, view: View) {
        readerViewFeature.set(
            feature = context.components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
                ReaderViewFeature(
                    context = context,
                    engine = context.components.core.engine,
                    store = context.components.core.store,
                    controlsView = binding.readerViewControlsBar,
                ) { available, active ->
                    browserScreenStore.dispatch(
                        ReaderModeStatusUpdated(ReaderModeStatus(available, active)),
                    )
                }
            },
            owner = this,
            view = view,
        )
    }

    private fun initTranslationsUpdates(context: Context, rootView: View) {
        translationsBannerIntegration.set(
            feature = TranslationsBannerIntegration(
                browserStore = context.components.core.store,
                browserScreenStore = browserScreenStore,
                binding = binding,
                onExpand = {
                    val directions =
                        BrowserFragmentDirections.actionBrowserFragmentToTranslationsDialogFragment()
                    findNavController().navigateSafe(R.id.browserFragment, directions)
                },
            ),
            owner = this,
            view = rootView,
        )

        if (FxNimbus.features.translations.value().mainFlowToolbarEnabled) {
            translationsBinding.set(
                feature = TranslationsBinding(
                    browserStore = rootView.context.components.core.store,
                    browserScreenStore = browserScreenStore,
                    appStore = rootView.context.components.appStore,
                    onTranslationStatusUpdate = {},
                    onShowTranslationsDialog = ::openTranslationsDialogFromToolbar,
                    navController = findNavController(),
                ),
                owner = this,
                view = rootView,
            )
        }
    }

    private fun openTranslationsDialogFromToolbar() {
        Translations.action.record(Translations.ActionExtra("main_flow_toolbar"))
        requireComponents.appStore.dispatch(SnackbarAction.SnackbarDismissed)
        findNavController().navigateSafe(
            R.id.browserFragment,
            BrowserFragmentDirections.actionBrowserFragmentToTranslationsDialogFragment(),
        )
    }

    override fun onStart() {
        super.onStart()
        val context = requireContext()
        val settings = context.settings()

        if (!settings.userKnowsAboutPwas) {
            pwaOnboardingObserver = PwaOnboardingObserver(
                store = context.components.core.store,
                lifecycleOwner = this,
                navController = findNavController(),
                settings = settings,
                webAppUseCases = context.components.useCases.webAppUseCases,
            ).also {
                it.start()
            }
        }

        subscribeToTabCollections()
        updateLastBrowseActivity()

        if (requireComponents.termsOfUseManager.shouldShowTermsOfUsePromptOnBrowserFragment()) {
            findNavController().navigate(
                BrowserFragmentDirections.actionGlobalTermsOfUseDialog(Surface.BROWSER),
            )
        } else if (requireComponents.ipProtectionManager.shouldShowIPProtectionPrompt()) {
            findNavController().navigate(
                BrowserFragmentDirections.actionGlobalIpProtectionDialog(IPProtectionSurface.BROWSER),
            )
        }
    }

    override fun onStop() {
        super.onStop()
        updateLastBrowseActivity()
        updateHistoryMetadata()
        pwaOnboardingObserver?.stop()
    }

    private fun updateHistoryMetadata() {
        getCurrentTab()?.let { tab ->
            (tab as? TabSessionState)?.historyMetadata?.let {
                requireComponents.core.historyMetadataService.updateMetadata(it, tab)
            }
        }
    }

    private fun subscribeToTabCollections() {
        Observer<List<TabCollection>> {
            requireComponents.core.tabCollectionStorage.cachedTabCollections = it
        }.also { observer ->
            requireComponents.core.tabCollectionStorage.getCollections()
                .observe(viewLifecycleOwner, observer)
        }
    }

    override fun onResume() {
        super.onResume()
        requireComponents.core.tabCollectionStorage.register(collectionStorageObserver, this)
    }

    override fun onBackPressed(): Boolean {
        return readerViewFeature.onBackPressed() || super.onBackPressed()
    }

    override fun navToQuickSettingsSheet(tab: SessionState, sitePermissions: SitePermissions?) {
        val useCase = requireComponents.useCases.trackingProtectionUseCases
        FxNimbus.features.cookieBanners.recordExposure()
        useCase.containsException(tab.id) { hasTrackingProtectionException ->
            lifecycleScope.launch {
                val cookieBannersStorage = requireComponents.core.cookieBannersStorage
                val cookieBannerUIMode = cookieBannersStorage.getCookieBannerUIMode(
                    tab = tab,
                    isFeatureEnabledInPrivateMode = requireContext().settings().shouldUseCookieBannerPrivateMode,
                    publicSuffixList = requireComponents.publicSuffixList,
                )
                withContext(Dispatchers.Main) {
                    runIfFragmentIsAttached {
                        val isTrackingProtectionEnabled =
                            tab.trackingProtection.enabled && !hasTrackingProtectionException
                        val directions = if (requireContext().settings().enableUnifiedTrustPanel) {
                            BrowserFragmentDirections.actionBrowserFragmentToTrustPanelFragment(
                                sessionId = tab.id,
                                url = tab.content.url,
                                title = tab.content.title,
                                isLocalPdf = tab.content.url.isContentUrl(),
                                isSecured = tab.content.securityInfo.isSecure,
                                sitePermissions = sitePermissions,
                                certificate = tab.content.securityInfo.certificate,
                                permissionHighlights = tab.content.permissionHighlights,
                                isTrackingProtectionEnabled = isTrackingProtectionEnabled,
                                cookieBannerUIMode = cookieBannerUIMode,
                            )
                        } else {
                            BrowserFragmentDirections.actionBrowserFragmentToQuickSettingsSheetDialogFragment(
                                sessionId = tab.id,
                                url = tab.content.url,
                                title = tab.content.title,
                                isLocalPdf = tab.content.url.isContentUrl(),
                                isSecured = tab.content.securityInfo.isSecure,
                                sitePermissions = sitePermissions,
                                gravity = getAppropriateLayoutGravity(),
                                certificateName = tab.content.securityInfo.issuer,
                                permissionHighlights = tab.content.permissionHighlights,
                                isTrackingProtectionEnabled = isTrackingProtectionEnabled,
                                cookieBannerUIMode = cookieBannerUIMode,
                            )
                        }
                        nav(R.id.browserFragment, directions)
                    }
                }
            }
        }
    }

    private val collectionStorageObserver = object : TabCollectionStorage.Observer {
        override fun onCollectionCreated(
            title: String,
            sessions: List<TabSessionState>,
            id: Long?,
        ) {
            showTabSavedToCollectionSnackbar(sessions.size, true)
        }

        override fun onTabsAdded(tabCollection: TabCollection, sessions: List<TabSessionState>) {
            showTabSavedToCollectionSnackbar(sessions.size)
        }

        private fun showTabSavedToCollectionSnackbar(
            tabSize: Int,
            isNewCollection: Boolean = false,
        ) {
            val messageResId = when {
                isNewCollection -> R.string.create_collection_tabs_saved_new_collection_2
                tabSize == 1 -> R.string.create_collection_tab_saved_2
                else -> return // Don't show snackbar for multiple tabs
            }

            view?.let {
                Snackbar.make(
                    snackBarParentView = binding.dynamicSnackbarContainer,
                    snackbarState = SnackbarState(
                        message = getString(messageResId),
                    ),
                ).show()
            }
        }
    }

    override fun getContextMenuCandidates(
        context: Context,
        view: View,
    ): List<ContextMenuCandidate> {
        val contextMenuCandidateAppLinksUseCases = AppLinksUseCases(
            requireContext(),
            { true },
        )

        return ContextMenuCandidate.defaultCandidates(
            context = context,
            tabsUseCases = context.components.useCases.tabsUseCases,
            contextMenuUseCases = context.components.useCases.contextMenuUseCases,
            snackBarParentView = view,
            snackbarDelegate = ContextMenuSnackbarDelegate(),
            downloadsLocation = {
                DownloadLocationManager(requireContext()).defaultLocation
            },
        ) + ContextMenuCandidate.createOpenInExternalAppCandidate(
            requireContext(),
            contextMenuCandidateAppLinksUseCases,
        ) + createOpenWithGoogleLensCandidate(context)
    }

    private fun createOpenWithGoogleLensCandidate(context: Context) = ContextMenuCandidate(
        id = "fenix.contextmenu.open_with_google_lens",
        label = context.getString(R.string.context_menu_open_image_with_google_lens),
        showFor = { _, hitResult ->
            val isImage = hitResult is HitResult.IMAGE || hitResult is HitResult.IMAGE_SRC
            val selectedEngine = context.components.core.store.state.search.selectedOrDefaultSearchEngine
            isImage &&
                hitResult.src.isHttpUrl() &&
                context.settings().googleLensIntegrationEnabled &&
                selectedEngine.isGoogleSearchEngine()
        },
        action = { _, hitResult ->
            context.components.appStore.dispatch(
                AppAction.LensAction.LensRequestedWithImageUrl(hitResult.src),
            )
        },
    )

    private fun String.isHttpUrl(): Boolean =
        startsWith("https://", ignoreCase = true) || startsWith("http://", ignoreCase = true)

    /**
     * Updates the last time the user was active on the [BrowserFragment].
     * This is useful to determine if the user has to start on the [HomeFragment]
     * or it should go directly to the [BrowserFragment].
     */
    @VisibleForTesting
    internal fun updateLastBrowseActivity() {
        requireContext().settings().lastBrowseActivity = System.currentTimeMillis()
    }
}

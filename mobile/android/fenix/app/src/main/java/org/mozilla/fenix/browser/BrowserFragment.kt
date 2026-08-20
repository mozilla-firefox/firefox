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
import androidx.navigation.NavOptions
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.thumbnails.BrowserThumbnails
import mozilla.components.concept.engine.HitResult
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.contextmenu.ContextMenuCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createOpenInExternalAppCandidate
import mozilla.components.feature.contextmenu.R as contextMenuR
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
import org.mozilla.fenix.components.share.isSystemShareSheetSupported
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.components.toolbar.gestures.ToolbarHorizontalGesturesHandler
import org.mozilla.fenix.components.toolbar.gestures.ToolbarVerticalGesturesHandler
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.application
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getRectWithScreenLocation
import org.mozilla.fenix.ext.isGoogleSearchEngine
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.navigateSafe
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.ipprotection.store.Surface as IPProtectionSurface
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.onboarding.OnboardingFragmentDirections
import org.mozilla.fenix.onboarding.OnboardingReason
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingFeature
import org.mozilla.fenix.pdf.PdfToolsIntegration
import org.mozilla.fenix.settings.downloads.DownloadLocationManager
import org.mozilla.fenix.summarization.SummarizationNavigator
import org.mozilla.fenix.termsofuse.store.Surface
import org.mozilla.fenix.utils.Settings

/** Fragment used for browsing the web within the main app. */
@Suppress("TooManyFunctions", "LargeClass")
class BrowserFragment : BaseBrowserFragment(), UserInteractionHandler, SystemInsetsPaddedFragment {
    private val windowFeature = ViewBoundFeatureWrapper<WindowFeature>()
    private val openInAppOnboardingObserver = ViewBoundFeatureWrapper<OpenInAppOnboardingObserver>()
    private val translationsBinding = ViewBoundFeatureWrapper<TranslationsBinding>()
    private val translationsBannerIntegration = ViewBoundFeatureWrapper<TranslationsBannerIntegration>()
    private val pdfToolsIntegration = ViewBoundFeatureWrapper<PdfToolsIntegration>()
    private val continuousOnboardingFeature = ViewBoundFeatureWrapper<ContinuousOnboardingFeature>()
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
    private var lensFeature: ViewBoundFeatureWrapper<LensFeature>? = ViewBoundFeatureWrapper<LensFeature>()
    private val lensLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            lensFeature
                ?.get()
                ?.handleCameraActivityResult(
                    result.resultCode,
                    result.data,
                    qrScanFenixFeature?.get(),
                )
        }
    private val lensCameraPermissionLauncher: ActivityResultLauncher<String> =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            lensFeature?.get()?.onCameraPermissionResult(isGranted)
        }

    private val continuousOnboardingDefaultBrowserLauncher: ActivityResultLauncher<Intent> =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            continuousOnboardingFeature.get()?.onDefaultBrowserStepCompleted(result.resultCode)
        }

    private val summarizationNavigator by lazy {
        SummarizationNavigator(
            summarizationSettings = requireComponents.core.summarizationSettingsBinding,
            eligibilityChecker = requireComponents.core.summarizationEligibilityChecker,
            getCurrentTab = ::getSafeCurrentTab,
        )
    }

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

    override fun initializeUI(view: View, tab: SessionState) {
        super.initializeUI(view, tab)

        val context = requireContext()
        val components = context.components
        val settings = components.settings

        setupToolbarSwipeBehavior(settings, components)

        initBrowserToolbarComposableUpdates(view)
        initTranslationsUpdates(context = context, rootView = view)
        initPdfTools(context = context, rootView = view)
        initContinuousOnboardingFeature()

        thumbnailsFeature.set(
            feature = BrowserThumbnails(context, binding.engineView, components.core.store),
            owner = this,
            view = view,
        )

        windowFeature.set(
            feature =
                WindowFeature(
                    store = components.core.store,
                    tabsUseCases = components.useCases.tabsUseCases,
                ),
            owner = this,
            view = view,
        )

        if (settings.shouldShowOpenInAppCfr) {
            openInAppOnboardingObserver.set(
                feature =
                    OpenInAppOnboardingObserver(
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
    }

    private fun setupToolbarSwipeBehavior(settings: Settings, components: Components) {
        if (!settings.isTabStripEnabled && settings.isSwipeToolbarToSwitchTabsEnabled) {
            binding.gestureLayout.addGestureListener(
                ToolbarHorizontalGesturesHandler(
                    activity = requireActivity(),
                    contentLayout = binding.browserLayout,
                    tabPreview = binding.tabPreview,
                    toolbarLayoutRect = { browserToolbar.layout.getRectWithScreenLocation() },
                    navBarLayoutRect = { browserNavigationBar?.layout?.getRectWithScreenLocation() },
                    store = components.core.store,
                    selectTabUseCase = components.useCases.tabsUseCases.selectTab,
                    onSwipeStarted = {
                        thumbnailsFeature.get()?.requestScreenshot()
                    },
                )
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
                )
            )
        }
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    private fun setupShakeDetection() {
        val shouldSetupShake =
            requireComponents.core.summarizeFeatureSettings.canShowFeature &&
                requireComponents.core.summarizationSettingsBinding.isGestureEnabled.value
        if (!shouldSetupShake) {
            return
        }

        val sensorManager = requireActivity().getSystemService(SensorManager::class.java) ?: return
        val accelerometer = LifecycleAwareSensorManagerAccelerometer(sensorManager)
        with(viewLifecycleOwner) {
            lifecycle.addObserver(accelerometer)
            lifecycleScope.launch {
                viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.STARTED) {
                    requireComponents.core.summarizationSettingsBinding.shakeSensitivity
                        .flatMapLatest { sensitivity ->
                            accelerometer.detectShakes(sensitivity = sensitivity)
                        }
                        .collect {
                            summarizeToolbarCfrBinding.get()?.maybeDismissCfr()
                            summarizationNavigator.navigateToSummarizationIfEligible(
                                navController = findNavController(),
                                fromShakeGesture = true,
                            )
                        }
                }
            }
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
            feature =
                context.components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
                    ReaderViewFeature(
                        context = context,
                        engine = context.components.core.engine,
                        store = context.components.core.store,
                        controlsView = binding.readerViewControlsBar,
                    ) { available, active ->
                        browserScreenStore.dispatch(ReaderModeStatusUpdated(ReaderModeStatus(available, active)))
                    }
                },
            owner = this,
            view = view,
        )
    }

    private fun initTranslationsUpdates(context: Context, rootView: View) {
        translationsBannerIntegration.set(
            feature =
                TranslationsBannerIntegration(
                    settings = context.components.settings,
                    browserStore = context.components.core.store,
                    browserScreenStore = browserScreenStore,
                    binding = binding,
                    onExpand = {
                        val directions = BrowserFragmentDirections.actionBrowserFragmentToTranslationsDialogFragment()
                        findNavController().navigateSafe(R.id.browserFragment, directions)
                    },
                ),
            owner = this,
            view = rootView,
        )

        if (FxNimbus.features.translations.value().mainFlowToolbarEnabled) {
            translationsBinding.set(
                feature =
                    TranslationsBinding(
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

    private fun initPdfTools(context: Context, rootView: View) {
        val settings = context.components.settings
        if (!settings.enablePdfTools) {
            return
        }

        pdfToolsIntegration.set(
            feature =
                PdfToolsIntegration(
                    container = binding.browserLayout,
                    browserStore = context.components.core.store,
                    isAddressBarAtBottom = settings.toolbarPosition == ToolbarPosition.BOTTOM,
                ),
            owner = this,
            view = rootView,
        )
    }

    private fun initContinuousOnboardingFeature() {
        ContinuousOnboardingFeature.register(
            fragment = this,
            binding = continuousOnboardingFeature,
            launcher = continuousOnboardingDefaultBrowserLauncher,
            telemetryRecorder = telemetryRecorder,
            navigateToSyncSignIn = {
                findNavController()
                    .nav(
                        id = R.id.browserFragment,
                        directions =
                            OnboardingFragmentDirections.actionGlobalTurnOnSync(
                                entrypoint = FenixFxAEntryPoint.NewUserOnboarding
                            ),
                    )
            },
            navigateToIpProtection = {
                findNavController()
                    .navigate(BrowserFragmentDirections.actionGlobalIpProtectionDialog(IPProtectionSurface.BROWSER))
            },
        )
    }

    private fun openTranslationsDialogFromToolbar() {
        Translations.action.record(Translations.ActionExtra("main_flow_toolbar"))
        requireComponents.appStore.dispatch(SnackbarAction.SnackbarDismissed)
        findNavController()
            .navigateSafe(
                R.id.browserFragment,
                BrowserFragmentDirections.actionBrowserFragmentToTranslationsDialogFragment(),
            )
    }

    override fun onStart() {
        super.onStart()
        val context = requireContext()

        if (context.components.appStore.state.longfoxEntryPointReady) {
            context.components.appStore.dispatch(AppAction.UpdateShowFoxPeekAnimation(false))
        }

        subscribeToTabCollections()
        updateLastBrowseActivity()

        if (requireComponents.termsOfUseManager.shouldShowTermsOfUsePromptOnBrowserFragment()) {
            findNavController().navigate(BrowserFragmentDirections.actionGlobalTermsOfUseDialog(Surface.BROWSER))
        }
    }

    override fun onStop() {
        super.onStop()
        updateLastBrowseActivity()
        updateHistoryMetadata()
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
            }
            .also { observer ->
                requireComponents.core.tabCollectionStorage.getCollections().observe(viewLifecycleOwner, observer)
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
        useCase.containsException(tab.id) { hasTrackingProtectionException ->
            lifecycleScope.launch {
                withContext(Dispatchers.Main) {
                    runIfFragmentIsAttached {
                        val isTrackingProtectionEnabled =
                            tab.trackingProtection.enabled && !hasTrackingProtectionException
                        val directions =
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
                            )
                        nav(R.id.browserFragment, directions)
                    }
                }
            }
        }
    }

    private val collectionStorageObserver =
        object : TabCollectionStorage.Observer {
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
                val messageResId =
                    when {
                        isNewCollection -> R.string.create_collection_tabs_saved_new_collection_2
                        tabSize == 1 -> R.string.create_collection_tab_saved_2
                        else -> return // Don't show snackbar for multiple tabs
                    }

                view?.let {
                    Snackbar.make(
                            snackBarParentView = binding.dynamicSnackbarContainer,
                            snackbarState = SnackbarState(message = getString(messageResId)),
                        )
                        .show()
                }
            }
        }

    override fun getContextMenuCandidates(
        context: Context,
        view: View,
    ): List<ContextMenuCandidate> {
        val contextMenuCandidateAppLinksUseCases =
            AppLinksUseCases(
                requireContext(),
                { true },
            )

        return if (requireComponents.settings.nativeShareSheetEnabled && isSystemShareSheetSupported) {
            NativeShareSheetContextMenuCandidate.defaultCandidates(
                context = context,
                tabsUseCases = context.components.useCases.tabsUseCases,
                contextMenuUseCases = context.components.useCases.contextMenuUseCases,
                shareUseCases = context.components.useCases.shareUseCases,
                getShareItems = {
                    listOf(
                        ShareData(
                            title = context.getString(contextMenuR.string.mozac_feature_contextmenu_share_link),
                            text = it,
                            url = it,
                        )
                    )
                },
                snackBarParentView = view,
                snackbarDelegate = ContextMenuSnackbarDelegate(),
                downloadsLocation = {
                    DownloadLocationManager(
                            requireComponents.settings,
                            requireContext().contentResolver,
                        )
                        .defaultLocation
                },
                navigateToShareFragment = { currentTab, hitTabUrl ->
                    val shareData = arrayOf(ShareData(title = hitTabUrl, url = hitTabUrl))
                    val popUpToId =
                        if (currentTab is CustomTabSessionState) {
                            R.id.externalAppBrowserFragment
                        } else {
                            R.id.browserFragment
                        }

                    findNavController()
                        .nav(
                            id = R.id.browserFragment,
                            directions =
                                BrowserFragmentDirections.actionGlobalShareFragment(
                                    sessionId = currentTab.id,
                                    data = shareData,
                                    showPage = true,
                                ),
                            navOptions = NavOptions.Builder().setPopUpTo(popUpToId, false).build(),
                        )
                },
            )
        } else {
            ContextMenuCandidate.defaultCandidates(
                context = context,
                tabsUseCases = context.components.useCases.tabsUseCases,
                contextMenuUseCases = context.components.useCases.contextMenuUseCases,
                snackBarParentView = view,
                snackbarDelegate = ContextMenuSnackbarDelegate(),
                downloadsLocation = {
                    DownloadLocationManager(
                            requireComponents.settings,
                            requireContext().contentResolver,
                        )
                        .defaultLocation
                },
            )
        } +
            createOpenInExternalAppCandidate(
                requireContext(),
                contextMenuCandidateAppLinksUseCases,
            ) +
            createOpenWithGoogleLensCandidate(context)
    }

    private fun createOpenWithGoogleLensCandidate(context: Context) =
        ContextMenuCandidate(
            id = "fenix.contextmenu.open_with_google_lens",
            label = context.getString(R.string.context_menu_open_image_with_google_lens),
            showFor = { _, hitResult ->
                val isImage = hitResult is HitResult.IMAGE || hitResult is HitResult.IMAGE_SRC
                val selectedEngine = context.components.core.store.state.search.selectedOrDefaultSearchEngine
                val settings = context.components.settings
                isImage &&
                    hitResult.src.isHttpUrl() &&
                    settings.googleLensIntegrationEnabled &&
                    settings.googleLensIntegrationUserEnabled &&
                    selectedEngine.isGoogleSearchEngine()
            },
            action = { _, hitResult ->
                context.components.appStore.dispatch(AppAction.LensAction.LensRequestedWithImageUrl(hitResult.src))
            },
        )

    private fun String.isHttpUrl(): Boolean =
        startsWith("https://", ignoreCase = true) || startsWith("http://", ignoreCase = true)

    /**
     * Updates the last time the user was active on the [BrowserFragment]. This is useful to determine if the user has
     * to start on the [HomeFragment] or it should go directly to the [BrowserFragment].
     */
    @VisibleForTesting
    internal fun updateLastBrowseActivity() {
        requireComponents.settings.recordLastBrowseActivity()
    }
}

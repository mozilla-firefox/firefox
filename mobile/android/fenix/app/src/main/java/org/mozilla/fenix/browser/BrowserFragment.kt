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
import androidx.appcompat.content.res.AppCompatResources
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.Observer
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.thumbnails.BrowserThumbnails
import mozilla.components.browser.toolbar.BrowserToolbar
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.toolbar.Toolbar
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
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.ReaderMode
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.store.BrowserScreenAction.ReaderModeStatusUpdated
import org.mozilla.fenix.components.QrScanFenixFeature
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.VoiceSearchFeature
import org.mozilla.fenix.components.toolbar.BrowserToolbarComposable
import org.mozilla.fenix.components.toolbar.BrowserToolbarView
import org.mozilla.fenix.components.toolbar.FenixBrowserToolbarView
import org.mozilla.fenix.components.toolbar.ToolbarMenu
import org.mozilla.fenix.components.toolbar.ui.createShareBrowserAction
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isLargeWindow
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.navigateSafe
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.settings.downloads.DownloadLocationManager
import org.mozilla.fenix.settings.quicksettings.protections.cookiebanners.getCookieBannerUIMode
import org.mozilla.fenix.shortcut.PwaOnboardingObserver
import org.mozilla.fenix.telemetry.ACTION_SHARE_CLICKED
import org.mozilla.fenix.telemetry.SOURCE_ADDRESS_BAR
import org.mozilla.fenix.termsofuse.store.Surface
import org.mozilla.fenix.theme.ThemeManager
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.GleanMetrics.Toolbar as GleanMetricsToolbar

/**
 * Fragment used for browsing the web within the main app.
 */
@Suppress("TooManyFunctions", "LargeClass")
class BrowserFragment : BaseBrowserFragment(), UserInteractionHandler {
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

    private var readerModeAvailable = false
    private var translationsAvailable = false

    private var pwaOnboardingObserver: PwaOnboardingObserver? = null

    @VisibleForTesting
    internal var homeAction: BrowserToolbar.Button? = null
    private var forwardAction: BrowserToolbar.TwoStateButton? = null
    private var backAction: BrowserToolbar.TwoStateButton? = null
    private var refreshAction: BrowserToolbar.TwoStateButton? = null
    private var isTablet: Boolean = false

    override fun initializeUI(view: View, tab: SessionState) {
        super.initializeUI(view, tab)

        val context = requireContext()
        val components = context.components

        if (!context.settings().isTabStripEnabled && context.settings().isSwipeToolbarToSwitchTabsEnabled) {
            binding.gestureLayout.addGestureListener(
                ToolbarGestureHandler(
                    activity = requireActivity(),
                    contentLayout = binding.browserLayout,
                    tabPreview = binding.tabPreview,
                    toolbarLayout = browserToolbarView.layout,
                    navBarLayout = browserNavigationBar?.layout,
                    store = components.core.store,
                    selectTabUseCase = components.useCases.tabsUseCases.selectTab,
                    onSwipeStarted = {
                        thumbnailsFeature.get()?.requestScreenshot()
                    },
                ),
            )
        }

        if (browserToolbarView is BrowserToolbarView) {
            updateBrowserToolbarLeadingAndNavigationActions(
                context = context,
                isTablet = isLargeWindow(),
            )
            initBrowserToolbarViewActions(view)
        } else {
            (browserToolbarView as? BrowserToolbarComposable)?.let {
                initBrowserToolbarComposableUpdates(view)
            }
        }
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

        if (context.settings().shouldShowOpenInAppCfr) {
            openInAppOnboardingObserver.set(
                feature = OpenInAppOnboardingObserver(
                    context = context,
                    store = context.components.core.store,
                    lifecycleOwner = this,
                    navController = findNavController(),
                    settings = context.settings(),
                    appLinksUseCases = context.components.useCases.appLinksUseCases,
                    container = binding.browserLayout as ViewGroup,
                    shouldScrollWithTopToolbar = !context.settings().shouldUseBottomToolbar,
                ),
                owner = this,
                view = view,
            )
        }

        setupShakeDetection()
    }

    private fun setupShakeDetection() {
        if (!requireComponents.core.summarizeFeatureDiscoverySettings.canShowFeature) {
            return
        }

        val sensorManager = requireActivity().getSystemService(SensorManager::class.java) ?: return
        val accelerometer = LifecycleAwareSensorManagerAccelerometer(sensorManager)
        with(viewLifecycleOwner) {
            lifecycle.addObserver(accelerometer)
            lifecycleScope.launch {
                viewLifecycleOwner.repeatOnLifecycle(Lifecycle.State.STARTED) {
                    accelerometer.detectShakes().collect {
                        findNavController().apply {
                            if (currentDestination?.id != R.id.browserFragment) {
                                return@collect
                            }

                            navigate(
                                BrowserFragmentDirections.actionBrowserFragmentToSummarizationFragment(
                                    true,
                                ),
                            )
                        }
                    }
                }
            }
        }
    }

    private fun initBrowserToolbarViewActions(rootView: View) {
        initReaderMode(rootView.context, rootView)
        initTranslationsAction(rootView.context, rootView)
        initSharePageAction(rootView.context)
    }

    private fun initBrowserToolbarComposableUpdates(rootView: View) {
        initReaderModeUpdates(rootView.context, rootView)
        qrScanFenixFeature = QrScanFenixFeature.register(this, qrScanLauncher)
        voiceSearchFeature = VoiceSearchFeature.register(this, voiceSearchLauncher)
    }

    private fun initSharePageAction(context: Context) {
        // Only adding share page action if tab strip is disabled.
        if (!context.settings().isTabStripEnabled && isLargeWindow()) {
            val sharePageAction = BrowserToolbar.createShareBrowserAction(
                context = context,
            ) {
                GleanMetricsToolbar.buttonTapped.record(
                    GleanMetricsToolbar.ButtonTappedExtra(
                        source = SOURCE_ADDRESS_BAR,
                        item = ACTION_SHARE_CLICKED,
                    ),
                )
                browserToolbarInteractor.onShareActionClicked()
            }

            (browserToolbarView as BrowserToolbarView).toolbar.addPageAction(sharePageAction)
        }
    }

    private fun initTranslationsAction(context: Context, view: View) {
        if (
            !FxNimbus.features.translations.value().mainFlowToolbarEnabled
        ) {
            return
        }

        // Do not add translation page action if device doesn't have large window
        val translationsAction = if (isLargeWindow()) {
            Toolbar.ActionButton(
                AppCompatResources.getDrawable(
                    context,
                    iconsR.drawable.mozac_ic_translate_24,
                ),
                contentDescription = context.getString(R.string.browser_toolbar_translate),
                iconTintColorResource = ThemeManager.resolveAttribute(R.attr.textPrimary, context),
                visible = { translationsAvailable },
                weight = { TRANSLATIONS_WEIGHT },
                listener = {
                    browserToolbarInteractor.onTranslationsButtonClicked()
                },
            )
        } else {
            null
        }

        translationsAction?.let {
            (browserToolbarView as BrowserToolbarView).toolbar.addPageAction(it)
        }

        translationsBinding.set(
            feature = TranslationsBinding(
                browserStore = context.components.core.store,
                appStore = context.components.appStore,
                onTranslationStatusUpdate = {
                    translationsAvailable = it.isTranslationPossible

                    translationsAction?.let { action ->
                        action.updateView(
                            tintColorResource = if (it.isTranslated) {
                                R.color.fx_mobile_icon_color_accent_violet
                            } else {
                                ThemeManager.resolveAttribute(R.attr.textPrimary, context)
                            },
                            contentDescription = if (it.isTranslated) {
                                context.getString(
                                    R.string.browser_toolbar_translated_successfully,
                                    it.fromSelectedLanguage?.localizedDisplayName,
                                    it.toSelectedLanguage?.localizedDisplayName,
                                )
                            } else {
                                context.getString(R.string.browser_toolbar_translate)
                            },
                        )

                        safeInvalidateBrowserToolbarView()
                    }
                },
                onShowTranslationsDialog = browserToolbarInteractor::onTranslationsButtonClicked,
            ),
            owner = this,
            view = view,
        )
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
                    onTranslationStatusUpdate = {
                        translationsAvailable = it.isTranslationPossible
                    },
                    onShowTranslationsDialog = browserToolbarInteractor::onTranslationsButtonClicked,
                    navController = findNavController(),
                ),
                owner = this,
                view = rootView,
            )
        }
    }

    private fun initReaderMode(context: Context, view: View) {
        val readerModeAction = BrowserToolbar.ToggleButton(
            image = AppCompatResources.getDrawable(
                context,
                iconsR.drawable.mozac_ic_reader_view_24,
            )!!,
            imageSelected = AppCompatResources.getDrawable(
                context,
                iconsR.drawable.mozac_ic_reader_view_fill_24,
            )!!,
            contentDescription = context.getString(R.string.browser_menu_read),
            contentDescriptionSelected = context.getString(R.string.browser_menu_read_close),
            visible = {
                readerModeAvailable
            },
            weight = { READER_MODE_WEIGHT },
            selected = getSafeCurrentTab()?.let {
                activity?.components?.core?.store?.state?.findTab(it.id)?.readerState?.active
            } ?: false,
            listener = browserToolbarInteractor::onReaderModePressed,
        )

        (browserToolbarView as BrowserToolbarView).toolbar.addPageAction(readerModeAction)

        readerViewFeature.set(
            feature = context.components.strictMode.allowViolation(StrictMode::allowThreadDiskReads) {
                ReaderViewFeature(
                    context = context,
                    engine = context.components.core.engine,
                    store = context.components.core.store,
                    controlsView = binding.readerViewControlsBar,
                ) { available, active ->
                    if (available) {
                        ReaderMode.available.record(NoExtras())
                    }

                    (browserToolbarView as BrowserToolbarView).let {
                        if (active) {
                            it.toolbar.display.removeSecurityIndicator()
                        } else {
                            it.toolbar.display.addSecurityIndicator()
                        }
                    }

                    readerModeAvailable = available
                    readerModeAction.setSelected(active)
                    safeInvalidateBrowserToolbarView()
                }
            },
            owner = this,
            view = view,
        )
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun addHomeAction(context: Context) {
        if (homeAction != null) return

        homeAction = BrowserToolbar.Button(
            imageDrawable = AppCompatResources.getDrawable(
                context,
                iconsR.drawable.mozac_ic_home_24,
            )!!,
            contentDescription = context.getString(R.string.browser_toolbar_home),
            iconTintColorResource = ThemeManager.resolveAttribute(R.attr.textPrimary, context),
            listener = browserToolbarInteractor::onHomeButtonClicked,
        ).also {
            (_browserToolbarView as? BrowserToolbarView)?.toolbar?.addNavigationAction(it)
        }
    }

    @VisibleForTesting
    internal fun updateBrowserToolbarLeadingAndNavigationActions(
        context: Context,
        isTablet: Boolean,
    ) {
        addHomeAction(context = context)
        updateTabletToolbarActions(isTablet = isTablet)
        (browserToolbarView as? BrowserToolbarView)?.toolbar?.invalidateActions()
    }

    override fun onUpdateToolbarForConfigurationChange(toolbar: FenixBrowserToolbarView) {
        super.onUpdateToolbarForConfigurationChange(toolbar)

        if (browserToolbarView is BrowserToolbarView) {
            updateBrowserToolbarLeadingAndNavigationActions(
                context = requireContext(),
                isTablet = isLargeWindow(),
            )
        }
    }

    @VisibleForTesting
    internal fun updateTabletToolbarActions(isTablet: Boolean) {
        if (isTablet == this.isTablet) return

        if (isTablet) {
            addTabletActions(requireContext())
        } else {
            removeTabletActions()
        }

        this.isTablet = isTablet
    }

    @VisibleForTesting
    internal fun addNavigationActions(context: Context) {
        val enableTint = ThemeManager.resolveAttribute(R.attr.textPrimary, context)
        val disableTint = ThemeManager.resolveAttribute(R.attr.textDisabled, context)

        if (backAction == null) {
            backAction = BrowserToolbar.TwoStateButton(
                primaryImage = AppCompatResources.getDrawable(
                    context,
                    iconsR.drawable.mozac_ic_back_24,
                )!!,
                primaryContentDescription = context.getString(R.string.browser_menu_back),
                primaryImageTintResource = enableTint,
                isInPrimaryState = { getSafeCurrentTab()?.content?.canGoBack ?: false },
                secondaryImageTintResource = disableTint,
                disableInSecondaryState = true,
                longClickListener = {
                    browserToolbarInteractor.onBrowserToolbarMenuItemTapped(
                        ToolbarMenu.Item.Back(viewHistory = true, isOnToolbar = true),
                    )
                },
                listener = {
                    browserToolbarInteractor.onBrowserToolbarMenuItemTapped(
                        ToolbarMenu.Item.Back(viewHistory = false, isOnToolbar = true),
                    )
                },
            ).also {
                (_browserToolbarView as? BrowserToolbarView)?.toolbar?.addNavigationAction(it)
            }
        }

        if (forwardAction == null) {
            forwardAction = BrowserToolbar.TwoStateButton(
                primaryImage = AppCompatResources.getDrawable(
                    context,
                    iconsR.drawable.mozac_ic_forward_24,
                )!!,
                primaryContentDescription = context.getString(R.string.browser_menu_forward),
                primaryImageTintResource = enableTint,
                isInPrimaryState = { getSafeCurrentTab()?.content?.canGoForward ?: false },
                secondaryImageTintResource = disableTint,
                disableInSecondaryState = true,
                longClickListener = {
                    browserToolbarInteractor.onBrowserToolbarMenuItemTapped(
                        ToolbarMenu.Item.Forward(viewHistory = true, isOnToolbar = true),
                    )
                },
                listener = {
                    browserToolbarInteractor.onBrowserToolbarMenuItemTapped(
                        ToolbarMenu.Item.Forward(viewHistory = false, isOnToolbar = true),
                    )
                },
            ).also {
                (_browserToolbarView as? BrowserToolbarView)?.toolbar?.addNavigationAction(it)
            }
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun addTabletActions(context: Context) {
        addNavigationActions(context)

        val enableTint = ThemeManager.resolveAttribute(R.attr.textPrimary, context)
        if (refreshAction == null) {
            refreshAction = BrowserToolbar.TwoStateButton(
                primaryImage = AppCompatResources.getDrawable(
                    context,
                    iconsR.drawable.mozac_ic_arrow_clockwise_24,
                )!!,
                primaryContentDescription = context.getString(R.string.browser_menu_refresh),
                primaryImageTintResource = enableTint,
                isInPrimaryState = {
                    getSafeCurrentTab()?.content?.loading == false
                },
                secondaryImage = AppCompatResources.getDrawable(
                    context,
                    iconsR.drawable.mozac_ic_stop,
                )!!,
                secondaryContentDescription = context.getString(R.string.browser_menu_stop),
                disableInSecondaryState = false,
                longClickListener = {
                    browserToolbarInteractor.onBrowserToolbarMenuItemTapped(
                        ToolbarMenu.Item.Reload(bypassCache = true),
                    )
                },
                listener = {
                    if (getCurrentTab()?.content?.loading == true) {
                        browserToolbarInteractor.onBrowserToolbarMenuItemTapped(ToolbarMenu.Item.Stop)
                    } else {
                        browserToolbarInteractor.onBrowserToolbarMenuItemTapped(
                            ToolbarMenu.Item.Reload(bypassCache = false),
                        )
                    }
                },
            ).also {
                (_browserToolbarView as? BrowserToolbarView)?.toolbar?.addNavigationAction(it)
            }
        }
    }

    @VisibleForTesting
    internal fun removeNavigationActions() {
        forwardAction?.let {
            (_browserToolbarView as? BrowserToolbarView)?.toolbar?.removeNavigationAction(it)
        }
        forwardAction = null
        backAction?.let {
            (_browserToolbarView as? BrowserToolbarView)?.toolbar?.removeNavigationAction(it)
        }
        backAction = null
    }

    @VisibleForTesting
    internal fun removeTabletActions() {
        removeNavigationActions()

        refreshAction?.let {
            (_browserToolbarView as? BrowserToolbarView)?.toolbar?.removeNavigationAction(it)
        }
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
        }
    }

    override fun onStop() {
        super.onStop()
        updateLastBrowseActivity()
        updateHistoryMetadata()
        pwaOnboardingObserver?.stop()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        isTablet = false
        homeAction = null
        forwardAction = null
        backAction = null
        refreshAction = null
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
        )
    }

    /**
     * Updates the last time the user was active on the [BrowserFragment].
     * This is useful to determine if the user has to start on the [HomeFragment]
     * or it should go directly to the [BrowserFragment].
     */
    @VisibleForTesting
    internal fun updateLastBrowseActivity() {
        requireContext().settings().lastBrowseActivity = System.currentTimeMillis()
    }

    companion object {
        /**
         * Indicates weight of a page action. The lesser the weight, the closer it is to the URL.
         *
         * A weight of -1 indicates the position is not cared for and the action will be appended at the end.
         */
        const val READER_MODE_WEIGHT = 1
        const val TRANSLATIONS_WEIGHT = 2
        const val SHARE_WEIGHT = 4
        const val OPEN_IN_ACTION_WEIGHT = 6
    }
}

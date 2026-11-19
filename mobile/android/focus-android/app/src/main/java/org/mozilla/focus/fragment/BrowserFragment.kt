/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.fragment

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityManager
import android.webkit.MimeTypeMap
import android.widget.Toast
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import androidx.core.view.isVisible
import androidx.fragment.app.Fragment
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import com.google.android.material.snackbar.Snackbar
import java.net.URLEncoder
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.selector.privateTabs
import mozilla.components.browser.state.state.CustomTabConfig
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.ExternalAppType
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.createTab
import mozilla.components.concept.engine.HitResult
import mozilla.components.feature.app.links.AppLinksFeature
import mozilla.components.feature.contextmenu.ContextMenuFeature
import mozilla.components.feature.customtabs.CustomTabWindowFeature
import mozilla.components.feature.downloads.DownloadsFeature
import mozilla.components.feature.downloads.manager.FetchDownloadManager
import mozilla.components.feature.downloads.temporary.ShareResourceFeature
import mozilla.components.feature.media.fullscreen.MediaSessionFullscreenFeature
import mozilla.components.feature.prompts.PromptFeature
import mozilla.components.feature.prompts.file.AndroidPhotoPicker
import mozilla.components.feature.session.PictureInPictureFeature
import mozilla.components.feature.session.SessionFeature
import mozilla.components.feature.sitepermissions.OnDiskSitePermissionsStorage
import mozilla.components.feature.sitepermissions.SitePermissionsFeature
import mozilla.components.feature.tabs.WindowFeature
import mozilla.components.feature.toolbar.ToolbarFeature
import mozilla.components.feature.top.sites.TopSitesConfig
import mozilla.components.feature.top.sites.TopSitesFeature
import mozilla.components.lib.crash.Crash
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.android.content.createChooserExcludingCurrentApp
import mozilla.components.support.ktx.android.view.ImeInsetsSynchronizer
import mozilla.components.support.ktx.android.view.exitImmersiveMode
import mozilla.components.support.locale.ActivityContextWrapper
import mozilla.components.support.utils.Browsers
import mozilla.components.support.utils.DefaultDownloadFileUtils
import mozilla.components.support.utils.DownloadFileUtils
import mozilla.components.support.utils.ext.requestInPlacePermissions
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.focus.Components
import org.mozilla.focus.GleanMetrics.Browser
import org.mozilla.focus.GleanMetrics.Downloads
import org.mozilla.focus.GleanMetrics.OpenWith
import org.mozilla.focus.GleanMetrics.TabCount
import org.mozilla.focus.GleanMetrics.TrackingProtection
import org.mozilla.focus.R
import org.mozilla.focus.activity.FirefoxInstallationHelper
import org.mozilla.focus.activity.MainActivity
import org.mozilla.focus.browser.integration.BrowserToolbarIntegration
import org.mozilla.focus.browser.integration.FindInPageIntegration
import org.mozilla.focus.browser.integration.FullScreenIntegration
import org.mozilla.focus.contextmenu.ContextMenuCandidates
import org.mozilla.focus.databinding.FragmentBrowserBinding
import org.mozilla.focus.downloads.DownloadService
import org.mozilla.focus.engine.EngineSharedPreferencesListener
import org.mozilla.focus.ext.accessibilityManager
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.disableDynamicBehavior
import org.mozilla.focus.ext.enableDynamicBehavior
import org.mozilla.focus.ext.ifCustomTab
import org.mozilla.focus.ext.isAccessibilityEnabled
import org.mozilla.focus.ext.isCustomTab
import org.mozilla.focus.ext.requireComponents
import org.mozilla.focus.ext.settings
import org.mozilla.focus.ext.showAsFixed
import org.mozilla.focus.ext.titleOrDomain
import org.mozilla.focus.menu.BrowserMenuCallbacks
import org.mozilla.focus.open.OpenWithFragment
import org.mozilla.focus.session.ui.TabsPopup
import org.mozilla.focus.settings.permissions.permissionoptions.SitePermissionOptionsStorage
import org.mozilla.focus.settings.privacy.ConnectionDetailsPanel
import org.mozilla.focus.settings.privacy.SiteSecurityInfo
import org.mozilla.focus.settings.privacy.TrackingProtectionPanel
import org.mozilla.focus.settings.privacy.TrackingProtectionPanelInteractor
import org.mozilla.focus.shortcut.HomeScreen
import org.mozilla.focus.state.AppAction
import org.mozilla.focus.topsites.DefaultTopSitesStorage.Companion.TOP_SITES_MAX_LIMIT
import org.mozilla.focus.topsites.DefaultTopSitesView
import org.mozilla.focus.utils.FocusSnackbar
import org.mozilla.focus.utils.FocusSnackbarDelegate

/** Fragment for displaying the browser UI. */
@Suppress("LargeClass", "TooManyFunctions")
class BrowserFragment : BaseFragment(), UserInteractionHandler, AccessibilityManager.AccessibilityStateChangeListener {

    private var _binding: FragmentBrowserBinding? = null
    private val binding
        get() = _binding!!

    private val findInPageIntegration = ViewBoundFeatureWrapper<FindInPageIntegration>()
    private val fullScreenIntegration = ViewBoundFeatureWrapper<FullScreenIntegration>()
    private var pictureInPictureFeature: PictureInPictureFeature? = null

    internal val sessionFeature = ViewBoundFeatureWrapper<SessionFeature>()
    private val promptFeature = ViewBoundFeatureWrapper<PromptFeature>()
    private val contextMenuFeature = ViewBoundFeatureWrapper<ContextMenuFeature>()
    private val downloadsFeature = ViewBoundFeatureWrapper<DownloadsFeature>()
    private val shareResourceFeature = ViewBoundFeatureWrapper<ShareResourceFeature>()
    private val windowFeature = ViewBoundFeatureWrapper<WindowFeature>()
    private val customTabWindowFeature = ViewBoundFeatureWrapper<CustomTabWindowFeature>()
    private val appLinksFeature = ViewBoundFeatureWrapper<AppLinksFeature>()
    private val topSitesFeature = ViewBoundFeatureWrapper<TopSitesFeature>()
    private var sitePermissionsFeature = ViewBoundFeatureWrapper<SitePermissionsFeature>()
    private var fullScreenMediaSessionFeature = ViewBoundFeatureWrapper<MediaSessionFullscreenFeature>()

    private val toolbarIntegration = ViewBoundFeatureWrapper<BrowserToolbarIntegration>()

    private var trackingProtectionPanel: TrackingProtectionPanel? = null
    private lateinit var requestPermissionLauncher: ActivityResultLauncher<Array<String>>
    private lateinit var downloadFileUtils: DownloadFileUtils
    private var tabsPopup: TabsPopup? = null

    /** The ID of the tab associated with this fragment. */
    private val tabId: String
        get() =
            requireArguments().getString(ARGUMENT_SESSION_UUID)
                ?: throw IllegalAccessError("No session ID set on fragment")

    /** The tab associated with this fragment. */
    val tab: SessionState
        get() =
            requireComponents.store.state.findTabOrCustomTab(tabId)
                // Workaround for tab not existing temporarily.
                ?: createTab("about:blank")

    // Registers a photo picker activity launcher in single-select mode.
    private val singleMediaPicker =
        AndroidPhotoPicker.singleMediaPicker(
            { getFragment() },
            { getPromptFeature() },
        )

    // Registers a photo picker activity launcher in multi-select mode.
    private val multipleMediaPicker =
        AndroidPhotoPicker.multipleMediaPicker(
            { getFragment() },
            { getPromptFeature() },
        )

    private fun getFragment(): Fragment {
        return this
    }

    private fun getPromptFeature(): PromptFeature? {
        return promptFeature.get()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        initDownloadFileUtils()
        requestPermissionLauncher =
            registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissionsResult ->
                val grandResults = ArrayList<Int>()
                permissionsResult.entries.forEach {
                    val isGranted = it.value
                    if (isGranted) {
                        grandResults.add(PackageManager.PERMISSION_GRANTED)
                    } else {
                        grandResults.add(PackageManager.PERMISSION_DENIED)
                    }
                }
                val feature = sitePermissionsFeature.get()
                feature?.onPermissionsResult(
                    permissionsResult.keys.toTypedArray(),
                    grandResults.toIntArray(),
                )
            }

        HomeScreen.checkIfPinningSupported(requireContext(), lifecycleScope)
    }

    private fun initDownloadFileUtils() {
        downloadFileUtils = DefaultDownloadFileUtils(context = requireContext())
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        _binding = FragmentBrowserBinding.inflate(inflater, container, false)

        requireContext().accessibilityManager.addAccessibilityStateChangeListener(this)

        val originalContext = ActivityContextWrapper.getOriginalContext(requireActivity())
        binding.engineView.setActivityContext(originalContext)

        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        val components = requireComponents

        initializeFindInPageFeature(view, components)
        initializeFullScreenIntegrationFeature(view, components)
        initializePictureInPictureFeature(components)
        initializeContextMenuFeature(view, components)
        initializeSessionFeature(view, components)
        initializePromptFeature(view, components)
        initializeDownloadsFeature(view, components)
        initializeShareResourceFeature(view, components)
        initializeAppLinksFeature(view, components)
        initializeTopSitesFeature(view, components)
        customizeToolbar()
        initializeUiBasedOnTabType(view, components)
        initializeMediaSessionFullscreenFeature(view, components)
        initializeSitePermissionsFeature(view)
        setupImeInsets(view)
    }

    private fun initializeFindInPageFeature(view: View, components: Components) {
        findInPageIntegration.set(
            FindInPageIntegration(
                components.store,
                binding.findInPage,
                binding.browserToolbar,
                binding.engineView,
            ),
            this,
            view,
        )
    }

    private fun initializeFullScreenIntegrationFeature(view: View, components: Components) {
        fullScreenIntegration.set(
            FullScreenIntegration(
                requireActivity(),
                components.store,
                tab.id,
                components.sessionUseCases,
                binding.browserToolbar,
                binding.engineView,
                isAccessibilityEnabled = { requireContext().isAccessibilityEnabled() },
            ),
            this,
            view,
        )
    }

    private fun initializePictureInPictureFeature(components: Components) {
        pictureInPictureFeature =
            PictureInPictureFeature(
                store = components.store,
                activity = requireActivity(),
                crashReporting = components.crashReporter,
                tabId = tabId,
            )
    }

    private fun initializeContextMenuFeature(view: View, components: Components) {
        contextMenuFeature.set(
            ContextMenuFeature(
                parentFragmentManager,
                components.store,
                ContextMenuCandidates.get(
                    requireContext(),
                    components.tabsUseCases,
                    components.contextMenuUseCases,
                    components.appLinksUseCases,
                    view,
                    FocusSnackbarDelegate(view),
                    tab.isCustomTab(),
                ),
                binding.engineView,
                requireComponents.contextMenuUseCases,
                tabId,
                additionalNote = { hitResult -> getAdditionalNote(hitResult) },
            ),
            this,
            view,
        )
    }

    private fun initializeSessionFeature(view: View, components: Components) {
        sessionFeature.set(
            SessionFeature(
                components.store,
                components.sessionUseCases.goBack,
                components.sessionUseCases.goForward,
                binding.engineView,
                tab.id,
            ),
            this,
            view,
        )
    }

    private fun initializePromptFeature(view: View, components: Components) {
        promptFeature.set(
            PromptFeature(
                fragment = this,
                store = components.store,
                tabsUseCases = components.tabsUseCases,
                customTabId = tryGetCustomTabId(),
                fragmentManager = parentFragmentManager,
                fileUploadsDirCleaner = requireComponents.fileUploadsDirCleaner,
                onNeedToRequestPermissions = { permissions ->
                    requestInPlacePermissions(REQUEST_KEY_PROMPT_PERMISSIONS, permissions) { result ->
                        promptFeature
                            .get()
                            ?.onPermissionsResult(
                                result.keys.toTypedArray(),
                                result.values
                                    .map {
                                        when (it) {
                                            true -> PackageManager.PERMISSION_GRANTED
                                            false -> PackageManager.PERMISSION_DENIED
                                        }
                                    }
                                    .toIntArray(),
                            )
                    }
                },
                androidPhotoPicker =
                    AndroidPhotoPicker(
                        requireContext(),
                        singleMediaPicker,
                        multipleMediaPicker,
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            AndroidPhotoPicker.allHdrCapabilities()
                        } else {
                            null
                        },
                    ),
            ),
            this,
            view,
        )
    }

    private fun initializeDownloadsFeature(view: View, components: Components) {
        downloadsFeature.set(
            DownloadsFeature(
                requireContext().applicationContext,
                components.store,
                components.downloadsUseCases,
                fragmentManager = childFragmentManager,
                promptsStyling =
                    DownloadsFeature.PromptsStyling(
                        gravity = Gravity.CENTER,
                        positiveButtonBackgroundColor = R.color.contrastColor,
                        positiveButtonTextColor = R.color.colorPrimary,
                    ),
                tabId = tabId,
                downloadManager =
                    FetchDownloadManager(
                        requireContext().applicationContext,
                        components.store,
                        DownloadService::class,
                        notificationsDelegate = components.notificationsDelegate,
                    ),
                onNeedToRequestPermissions = { permissions ->
                    requestInPlacePermissions(REQUEST_KEY_DOWNLOAD_PERMISSIONS, permissions) { result ->
                        downloadsFeature
                            .get()
                            ?.onPermissionsResult(
                                result.keys.toTypedArray(),
                                result.values
                                    .map {
                                        when (it) {
                                            true -> PackageManager.PERMISSION_GRANTED
                                            false -> PackageManager.PERMISSION_DENIED
                                        }
                                    }
                                    .toIntArray(),
                            )
                    }
                },
                onDownloadStopped = { state, _, status ->
                    handleDownloadStopped(state, status)
                },
                downloadFileUtils = downloadFileUtils,
            ),
            this,
            view,
        )
    }

    private fun initializeShareResourceFeature(view: View, components: Components) {
        shareResourceFeature.set(
            ShareResourceFeature(
                context = requireContext().applicationContext,
                httpClient = components.client,
                store = components.store,
                tabId = tab.id,
            ),
            this,
            view,
        )
    }

    private fun initializeAppLinksFeature(view: View, components: Components) {
        appLinksFeature.set(
            feature =
                AppLinksFeature(
                    requireContext(),
                    store = components.store,
                    sessionId = tabId,
                    fragmentManager = parentFragmentManager,
                    launchInApp = { _ -> requireContext().settings.openLinksInExternalApp },
                    loadUrlUseCase = requireContext().components.sessionUseCases.loadUrl,
                    failedToLaunchAction = { fallbackUrl ->
                        fallbackUrl?.let {
                            val appLinksUseCases = components.appLinksUseCases
                            val getRedirect = appLinksUseCases.appLinkRedirect
                            val redirect = getRedirect.invoke(fallbackUrl)
                            redirect.appIntent?.flags = Intent.FLAG_ACTIVITY_NEW_TASK
                            appLinksUseCases.openAppLink.invoke(redirect.appIntent)
                        }
                    },
                ),
            owner = this,
            view = view,
        )
    }

    private fun initializeTopSitesFeature(view: View, components: Components) {
        topSitesFeature.set(
            feature =
                TopSitesFeature(
                    view = DefaultTopSitesView(requireComponents.appStore),
                    storage = components.topSitesStorage,
                    config = {
                        TopSitesConfig(
                            targetSitesCount = TOP_SITES_MAX_LIMIT,
                            frecencyConfig = null,
                            providerConfig = null,
                        )
                    },
                ),
            owner = this,
            view = view,
        )
    }

    private fun initializeUiBasedOnTabType(view: View, components: Components) {
        val customTabConfig = tab.ifCustomTab()?.config
        if (customTabConfig != null) {
            initialiseCustomTabUi(
                customTabConfig,
                requireContext().isAccessibilityEnabled(),
            )

            customTabWindowFeature.set(
                feature = CustomTabWindowFeature(requireActivity(), components.store, tab.id),
                owner = this,
                view = view,
            )
        } else {
            initialiseNormalBrowserUi(requireContext().isAccessibilityEnabled())

            windowFeature.set(
                feature =
                    WindowFeature(
                        store = components.store,
                        tabsUseCases = components.tabsUseCases,
                    ),
                owner = this,
                view = view,
            )
        }
    }

    private fun initializeMediaSessionFullscreenFeature(view: View, components: Components) {
        fullScreenMediaSessionFeature.set(
            feature = MediaSessionFullscreenFeature(requireActivity(), components.store, tryGetCustomTabId()),
            owner = this,
            view = view,
        )
    }

    private fun initializeSitePermissionsFeature(rootView: View) {
        sitePermissionsFeature.set(
            feature =
                SitePermissionsFeature(
                    context = requireContext(),
                    storage =
                        OnDiskSitePermissionsStorage(
                            requireContext(),
                            coroutineScope = requireComponents.applicationScope,
                        ),
                    fragmentManager = parentFragmentManager,
                    promptsStyling =
                        SitePermissionsFeature.PromptsStyling(
                            gravity = Gravity.CENTER,
                            positiveButtonBackgroundColor = R.color.contrastColor,
                            positiveButtonTextColor = R.color.colorPrimary,
                        ),
                    onNeedToRequestPermissions = { permissions ->
                        requestPermissionLauncher.launch(permissions)
                    },
                    onShouldShowRequestPermissionRationale = {
                        shouldShowRequestPermissionRationale(it)
                    },
                    sitePermissionsRules =
                        SitePermissionOptionsStorage(requireContext()).getSitePermissionsSettingsRules(),
                    sessionId = tabId,
                    store = requireComponents.store,
                    coroutineScope = requireComponents.applicationScope,
                    shouldShowDoNotAskAgainCheckBox = false,
                ),
            owner = this,
            view = rootView,
        )
        if (requireComponents.appStore.state.sitePermissionOptionChange) {
            requireComponents.sessionUseCases.reload(tabId)
            requireComponents.appStore.dispatch(AppAction.SitePermissionOptionChange(false))
        }
    }

    private fun setupImeInsets(view: View) {
        ImeInsetsSynchronizer.setup(
            targetView = view,
            synchronizeViewWithIME = false,
            onIMEAnimationStarted = { isKeyboardShowingUp, _ ->
                if (!isKeyboardShowingUp) {
                    (view.layoutParams as? ViewGroup.MarginLayoutParams)?.bottomMargin = 0
                    view.requestLayout()
                }
            },
            onIMEAnimationFinished = { isKeyboardShowingUp, keyboardHeight ->
                if (isKeyboardShowingUp || keyboardHeight == 0) {
                    (view.layoutParams as? ViewGroup.MarginLayoutParams)?.bottomMargin = keyboardHeight
                    view.requestLayout()
                }
            },
        )
    }

    override fun onAccessibilityStateChanged(enabled: Boolean) {
        when (enabled) {
            // using _binding, because this might be called before onCreateView.
            false ->
                _binding
                    ?.browserToolbar
                    ?.enableDynamicBehavior(
                        requireContext(),
                        binding.engineView,
                    )
            true -> {
                _binding?.browserToolbar?.let {
                    it.disableDynamicBehavior(binding.engineView)
                    it.showAsFixed(requireContext(), binding.engineView)
                }
            }
        }
    }

    override fun onPictureInPictureModeChanged(isInPipMode: Boolean) {
        pictureInPictureFeature?.onPictureInPictureModeChanged(isInPipMode)
        if (lifecycle.currentState == Lifecycle.State.CREATED) {
            onBackPressed()
        }
    }

    private fun getAdditionalNote(hitResult: HitResult): String? {
        return if ((hitResult is HitResult.IMAGE_SRC || hitResult is HitResult.IMAGE) && hitResult.src.isNotEmpty()) {
            getString(R.string.contextmenu_erased_images_note2, getString(R.string.app_name))
        } else {
            null
        }
    }

    private fun customizeToolbar() {
        val customTabSessionState = tab.ifCustomTab()

        val renderStyle =
            if (tab.isCustomTab()) {
                ToolbarFeature.RenderStyle.ColoredDomain
            } else {
                ToolbarFeature.RenderStyle.ColoredUrl
            }

        toolbarIntegration.set(
            BrowserToolbarIntegration(
                requireComponents.store,
                toolbar = binding.browserToolbar,
                fragment = this,
                currentTabId = tabId,
                menuCallbacks =
                    BrowserMenuCallbacks(
                        shareCallback = ::shareCurrentUrl,
                        requestDesktopCallback = ::toggleDesktopSite,
                        addToHomeScreenCallback = ::showAddToHomescreenDialog,
                        showFindInPageCallback = ::showFindInPageBar,
                        openInCallback = ::openSelectBrowser,
                        openInBrowser = ::openInBrowser,
                        showShortcutAddedSnackBar = ::showShortcutAddedSnackBar,
                    ),
                customTabId = tryGetCustomTabId(),
                customTabsUseCases = requireComponents.customTabsUseCases,
                sessionUseCases = requireComponents.sessionUseCases,
                onUrlLongClicked = ::onUrlLongClicked,
                eraseActionListener = { erase(shouldEraseAllTabs = true) },
                isOnboardingTab = isOnboardingTab(customTabSessionState),
                tabCounterListener = ::tabCounterListener,
                renderStyle = renderStyle,
            ),
            owner = this,
            view = binding.browserToolbar,
        )
    }

    @VisibleForTesting
    internal fun isOnboardingTab(sessionState: CustomTabSessionState?) =
        sessionState?.config?.externalAppType == ExternalAppType.ONBOARDING_CUSTOM_TAB

    private fun showShortcutAddedSnackBar() {
        FocusSnackbar.make(requireView())
            .setText(requireContext().getString(R.string.snackbar_added_to_shortcuts))
            .show()
    }

    private fun initialiseNormalBrowserUi(accessibilityEnabled: Boolean) {
        if (accessibilityEnabled) {
            binding.browserToolbar.showAsFixed(requireContext(), binding.engineView)
        } else {
            binding.browserToolbar.enableDynamicBehavior(requireContext(), binding.engineView)
        }
    }

    private fun initialiseCustomTabUi(
        customTabConfig: CustomTabConfig,
        accessibilityEnabled: Boolean,
    ) {
        if (customTabConfig.enableUrlbarHiding && !accessibilityEnabled) {
            binding.browserToolbar.enableDynamicBehavior(requireContext(), binding.engineView)
        } else {
            binding.browserToolbar.showAsFixed(requireContext(), binding.engineView)
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()

        tabsPopup?.dismiss()
        tabsPopup = null

        requireContext().accessibilityManager.removeAccessibilityStateChangeListener(this)
        binding.engineView.setActivityContext(null)

        _binding = null
    }

    override fun onDestroy() {
        super.onDestroy()

        // This fragment might get destroyed before the user left immersive mode (e.g. by opening another URL from an
        // app). In this case let's leave immersive mode now when the fragment gets destroyed.
        requireActivity().exitImmersiveMode()
    }

    @Suppress("OVERRIDE_DEPRECATION")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        promptFeature.withFeature { it.onActivityResult(requestCode, data, resultCode) }
    }

    private fun showCrashReporter(crash: Crash) {
        val fragmentManager = requireActivity().supportFragmentManager

        if (crashReporterIsVisible()) {
            // We are already displaying the crash reporter
            // No need to show another one.
            return
        }

        val crashReporterFragment = CrashReporterFragment.create()

        crashReporterFragment.onCloseTabPressed = { sendCrashReport ->
            if (sendCrashReport) {
                requireComponents.crashReporter.submitReport(crash)
            }

            requireComponents.sessionUseCases.crashRecovery.invoke()
            erase()
            hideCrashReporter()
        }

        fragmentManager
            .beginTransaction()
            .addToBackStack(null)
            .add(R.id.crash_container, crashReporterFragment, CrashReporterFragment.FRAGMENT_TAG)
            .commit()

        binding.crashContainer.isVisible = true
    }

    private fun hideCrashReporter() {
        val fragmentManager = requireActivity().supportFragmentManager
        val fragment = fragmentManager.findFragmentByTag(CrashReporterFragment.FRAGMENT_TAG) ?: return

        fragmentManager.beginTransaction().remove(fragment).commit()

        binding.crashContainer.isVisible = false
    }

    /** Returns true if the crash reporter fragment is visible. */
    fun crashReporterIsVisible(): Boolean =
        requireActivity().supportFragmentManager.let {
            it.findFragmentByTag(CrashReporterFragment.FRAGMENT_TAG)?.isVisible == true
        }

    private fun handleDownloadStopped(
        state: DownloadState,
        status: DownloadState.Status,
    ) {
        val extension = MimeTypeMap.getFileExtensionFromUrl(URLEncoder.encode(state.filePath, "utf-8"))

        when (status) {
            DownloadState.Status.FAILED -> {
                Downloads.downloadFailed.record(Downloads.DownloadFailedExtra(extension))
            }

            DownloadState.Status.PAUSED -> {
                Downloads.downloadPaused.record(NoExtras())
            }

            DownloadState.Status.COMPLETED -> {
                Downloads.downloadCompleted.record(NoExtras())

                showDownloadCompletedSnackbar(state, extension)
            }

            else -> {}
        }
    }

    override fun onPause() {
        super.onPause()
        // Custom tab content should always be visible, even if the app is locked.
        if (tab.isCustomTab()) {
            view?.isVisible = true
        }
    }

    private fun showDownloadCompletedSnackbar(
        state: DownloadState,
        extension: String?,
    ) {
        val snackbar =
            FocusSnackbar.make(
                requireView(),
                Snackbar.LENGTH_LONG,
            )

        snackbar.setText(
            String.format(
                requireContext().getString(R.string.download_snackbar_finished),
                state.fileName,
            )
        )

        snackbar.setAction(getString(R.string.download_snackbar_open)) { context ->
            val opened =
                downloadFileUtils.openFile(
                    fileName = state.fileName,
                    directoryPath = state.directoryPath,
                    contentType = state.contentType,
                )

            if (!opened) {
                Toast.makeText(
                        context,
                        getString(
                            mozilla.components.feature.downloads.R.string.mozac_feature_downloads_open_not_supported1,
                            extension,
                        ),
                        Toast.LENGTH_LONG,
                    )
                    .show()
            }

            Downloads.openButtonTapped.record(
                Downloads.OpenButtonTappedExtra(fileExtension = extension, openSuccessful = opened)
            )
        }

        snackbar.show()
    }

    private fun showAddToHomescreenDialog() {
        val fragmentManager = childFragmentManager

        if (fragmentManager.findFragmentByTag(AddToHomescreenDialogFragment.FRAGMENT_TAG) != null) {
            // We are already displaying a homescreen dialog fragment (Probably a restored fragment).
            // No need to show another one.
            return
        }

        val requestDesktop = tab.content.desktopMode

        val addToHomescreenDialogFragment =
            AddToHomescreenDialogFragment.newInstance(
                url = tab.content.url,
                title = tab.content.titleOrDomain,
                blockingEnabled = !tab.trackingProtection.ignoredOnTrackingProtection,
                requestDesktop = requestDesktop,
            )

        try {
            addToHomescreenDialogFragment.show(
                fragmentManager,
                AddToHomescreenDialogFragment.FRAGMENT_TAG,
            )
        } catch (e: IllegalStateException) {
            // It can happen that at this point in time the activity is already in the background
            // and onSaveInstanceState() has already been called. Fragment transactions are not
            // allowed after that anymore. It's probably safe to guess that the user might not
            // be interested in adding to homescreen now.
        }
    }

    override fun onResume() {
        super.onResume()

        updateEngineColorScheme()

        // Custom tab content should always be visible, even if the app is locked.
        if (tab.isCustomTab()) {
            view?.isVisible = true
        }
    }

    private fun updateEngineColorScheme() {
        val preferredColorScheme = requireComponents.settings.getPreferredColorScheme()
        if (requireComponents.engine.settings.preferredColorScheme != preferredColorScheme) {
            requireComponents.engine.settings.preferredColorScheme = preferredColorScheme
            requireComponents.sessionUseCases.reload()
        }
    }

    override fun onStop() {
        super.onStop()
        tabsPopup?.dismiss()
        trackingProtectionPanel?.hide()
        requireComponents.sessionUseCases.exitFullscreen(tabId)
    }

    override fun onHomePressed() = pictureInPictureFeature?.onHomePressed() ?: false

    @Suppress("ReturnCount")
    override fun onBackPressed(): Boolean {
        if (findInPageIntegration.onBackPressed()) {
            return true
        } else if (fullScreenIntegration.onBackPressed()) {
            return true
        } else if (sessionFeature.get()?.onBackPressed() == true) {
            return true
        } else if (tab.source is SessionState.Source.Internal.TextSelection) {
            erase()
            return true
        } else {
            if (tab.source is SessionState.Source.External || tab.isCustomTab()) {
                Browser.backButtonPressed.record(Browser.BackButtonPressedExtra("erase_to_external_app"))

                // This session has been started from a VIEW intent. Go back to the previous app
                // immediately and erase the current browsing session.
                erase()

                // If there are no other sessions then we remove the whole task because otherwise
                // the old session might still be partially visible in the app switcher.
                if (requireComponents.store.state.privateTabs.isEmpty()) {
                    requireActivity().finishAndRemoveTask()
                } else {
                    requireActivity().finish()
                }
                // We can't show a snackbar outside of the app. So let's show a toast instead.
                Toast.makeText(context, R.string.feedback_erase_custom_tab, Toast.LENGTH_SHORT).show()
            } else {
                // Just go back to the home screen.
                Browser.backButtonPressed.record(Browser.BackButtonPressedExtra("erase_to_home"))

                erase()
            }
        }

        return true
    }

    /**
     * Erases the current tab or all tabs.
     *
     * If [shouldEraseAllTabs] is true, all tabs will be removed and the user will be navigated to the home screen.
     * Otherwise, only the current tab will be removed and the user will be navigated to the previously selected tab.
     *
     * @param shouldEraseAllTabs Whether to erase all tabs or just the current tab.
     */
    fun erase(shouldEraseAllTabs: Boolean = false) {
        if (shouldEraseAllTabs) {
            requireComponents.appStore.dispatch(AppAction.NavigateUp(null))
            requireComponents.tabsUseCases.removeAllTabs()
        } else {
            requireComponents.tabsUseCases.removeTab(tab.id)
            requireComponents.appStore.dispatch(AppAction.NavigateUp(requireComponents.store.state.selectedTabId))
        }
    }

    private fun shareCurrentUrl() {
        val shareIntent = Intent(Intent.ACTION_SEND)
        shareIntent.type = "text/plain"
        shareIntent.putExtra(Intent.EXTRA_TEXT, tab.content.url)

        val title = tab.content.title
        if (title.isNotEmpty()) {
            shareIntent.putExtra(Intent.EXTRA_SUBJECT, title)
        }
        startActivity(
            shareIntent.createChooserExcludingCurrentApp(
                context = requireContext(),
                title = getString(R.string.share_dialog_title),
            )
        )
    }

    private fun openInBrowser() {
        // Release the session from this view so that it can immediately be rendered by a different view
        sessionFeature.get()?.release()

        requireComponents.customTabsUseCases.migrate(tab.id)

        val intent = Intent(context, MainActivity::class.java)
        intent.action = Intent.ACTION_MAIN
        intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
        startActivity(intent)

        // Close this activity (and the task) since it is no longer displaying any session
        val activity = activity
        activity?.finishAndRemoveTask()
    }

    internal fun edit(tabId: String = tab.id) {
        requireComponents.appStore.dispatch(AppAction.EditAction(tabId))
    }

    private fun tabCounterListener() {
        if (tabsPopup?.isShowing == true) {
            return
        }

        val openedTabs = requireComponents.store.state.tabs.size
        TabCount.sessionButtonTapped.record(TabCount.SessionButtonTappedExtra(openedTabs))

        // We use post to avoid an ANR in HardwareRenderer.nSetName which can happen
        // if we show the popup while the window is in transition (e.g. keyboard hiding).
        binding.browserToolbar.post {
            val toolbar = _binding?.browserToolbar ?: return@post
            if (!isAdded || isRemoving || tabsPopup?.isShowing == true) {
                return@post
            }

            tabsPopup =
                TabsPopup(toolbar, requireComponents).also {
                    it.showAsDropDown(toolbar, 0, 0, Gravity.END)
                }
        }
    }

    private fun showFindInPageBar() {
        findInPageIntegration.get()?.show(tab)
    }

    private fun openSelectBrowser() {
        val browsers = Browsers.forUrl(requireContext(), tab.content.url)

        val apps = browsers.installedBrowsers.filterNot { it.packageName == requireContext().packageName }
        val store =
            if (browsers.hasFirefoxBrandedBrowserInstalled) {
                null
            } else {
                FirefoxInstallationHelper.resolveAppStore(requireContext())
            }

        val fragment =
            OpenWithFragment.newInstance(
                apps.toTypedArray(),
                tab.content.url,
                store,
            )
        fragment.show(getParentFragmentManager(), OpenWithFragment.FRAGMENT_TAG)

        OpenWith.listDisplayed.record(OpenWith.ListDisplayedExtra(apps.size))
    }

    internal fun closeCustomTab() {
        requireComponents.customTabsUseCases.remove(tab.id)
        requireActivity().finish()
    }

    /**
     * Toggles the "Request desktop site" setting for the current tab.
     *
     * @param enabled True to request the desktop site, false to request the mobile site.
     */
    private fun toggleDesktopSite(enabled: Boolean) {
        requireComponents.sessionUseCases.requestDesktopSite(enabled, tab.id)
    }

    /** Show tracking protection panel. */
    fun showTrackingProtectionPanel() {
        trackingProtectionPanel =
            TrackingProtectionPanel(
                    context = requireContext(),
                    lifecycleOwner = this,
                    siteInfo =
                        SiteSecurityInfo(
                            tabUrl = tab.content.url,
                            isTrackingProtectionOn = tab.trackingProtection.ignoredOnTrackingProtection.not(),
                            isConnectionSecure = tab.content.securityInfo.isSecure,
                            blockedTrackersCount = requireContext().settings.getTotalBlockedTrackersCount(),
                        ),
                    interactor =
                        object : TrackingProtectionPanelInteractor {
                            override fun toggleTrackingProtection(enabled: Boolean) =
                                this@BrowserFragment.toggleTrackingProtection(enabled)

                            override fun updateTrackingProtectionPolicy(tracker: String?, enabled: Boolean) {
                                EngineSharedPreferencesListener(requireContext())
                                    .updateTrackingProtectionPolicy(
                                        source = EngineSharedPreferencesListener.ChangeSource.PANEL.source,
                                        tracker = tracker,
                                        isEnabled = enabled,
                                    )
                                reloadCurrentTab()
                            }

                            override fun showConnectionInfo() = this@BrowserFragment.showConnectionInfo()
                        },
                )
                .also { currentEtp -> context?.let { currentEtp.show() } }
    }

    private fun reloadCurrentTab() {
        requireComponents.sessionUseCases.reload(tab.id)
    }

    private fun showConnectionInfo() {
        val connectionInfoPanel =
            ConnectionDetailsPanel(
                context = requireContext(),
                engineSession = tab.engineState.engineSession,
                tabTitle = tab.content.title,
                tabUrl = tab.content.url,
                isConnectionSecure = tab.content.securityInfo.isSecure,
                goBack = { trackingProtectionPanel?.show() },
            )
        trackingProtectionPanel?.hide()
        connectionInfoPanel.show()
    }

    private fun toggleTrackingProtection(enable: Boolean) {
        with(requireComponents) {
            if (enable) {
                trackingProtectionUseCases.removeException(tab.id)
            } else {
                trackingProtectionUseCases.addException(tab.id, persistInPrivateMode = true)
            }
        }

        reloadCurrentTab()

        TrackingProtection.hasEverChangedEtp.set(true)
        TrackingProtection.trackingProtectionChanged.record(
            TrackingProtection.TrackingProtectionChangedExtra(isEnabled = enable)
        )
    }

    private fun onUrlLongClicked(): Boolean {
        val context = activity ?: return false

        return if (tab.isCustomTab()) {
            val clipBoard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            val uri = tab.content.url.toUri()
            clipBoard.setPrimaryClip(ClipData.newRawUri("Uri", uri))
            Toast.makeText(context, getString(R.string.custom_tab_copy_url_action), Toast.LENGTH_SHORT).show()
            true
        } else {
            false
        }
    }

    private fun tryGetCustomTabId() =
        if (tab.isCustomTab()) {
            tab.id
        } else {
            null
        }

    /**
     * Handles a tab crash by showing the crash reporter.
     *
     * @param crash The crash that occurred.
     */
    fun handleTabCrash(crash: Crash) {
        showCrashReporter(crash)
    }

    companion object {
        const val FRAGMENT_TAG = "browser"

        private const val ARGUMENT_SESSION_UUID = "sessionUUID"

        private const val REQUEST_KEY_DOWNLOAD_PERMISSIONS = "downloadFeature"
        private const val REQUEST_KEY_PROMPT_PERMISSIONS = "promptFeature"

        /**
         * Creates a new [BrowserFragment] instance for the given tab ID.
         *
         * @param tabId The ID of the tab to create the fragment for.
         * @return A new [BrowserFragment] instance.
         */
        fun createForTab(tabId: String): BrowserFragment {
            val fragment = BrowserFragment()
            fragment.arguments =
                Bundle().apply {
                    putString(ARGUMENT_SESSION_UUID, tabId)
                }
            return fragment
        }

        /**
         * Creates a [Bundle] containing the arguments needed for this fragment.
         *
         * @param tabId The ID of the tab to create the fragment for.
         * @return A [Bundle] with the tab ID set.
         */
        fun bundleForTab(tabId: String): Bundle {
            return Bundle().apply {
                putString(ARGUMENT_SESSION_UUID, tabId)
            }
        }
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import android.app.Dialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.activity.compose.BackHandler
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.ContentTransform
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat.Type.systemBars
import androidx.fragment.compose.content
import androidx.lifecycle.coroutineScope
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus
import mozilla.components.feature.ipprotection.store.state.isEligible
import mozilla.components.lib.state.ext.consumeFlow
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlinx.coroutines.flow.ifAnyChanged
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.menu.IPProtectionMenuBinding
import org.mozilla.fenix.components.menu.compose.MenuDialogBottomSheet
import org.mozilla.fenix.components.menu.compose.MenuHandleState
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.ipprotection.ui.IPProtectionSnackbarBinding
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelMiddleware
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelNavigationMiddleware
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelTelemetryMiddleware
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermission
import org.mozilla.fenix.settings.trustpanel.ui.ClearSiteDataDialog
import org.mozilla.fenix.settings.trustpanel.ui.ProtectionPanel
import org.mozilla.fenix.settings.trustpanel.ui.TrackerCategoryDetailsPanel
import org.mozilla.fenix.settings.trustpanel.ui.TrackersBlockedPanel
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.trackingprotection.ProtectionsDashboardContent
import org.mozilla.fenix.trackingprotection.TrackersBlockedFeature
import org.mozilla.fenix.utils.DELAY_MS_MAIN_MENU
import org.mozilla.fenix.utils.DELAY_MS_SUB_MENU
import org.mozilla.fenix.utils.DURATION_MS_MAIN_MENU
import org.mozilla.fenix.utils.DURATION_MS_SUB_MENU
import org.mozilla.fenix.utils.contentGrowth
import org.mozilla.fenix.utils.enterMenu
import org.mozilla.fenix.utils.enterSubmenu
import org.mozilla.fenix.utils.exitMenu
import org.mozilla.fenix.utils.exitSubmenu

/** A bottom sheet dialog fragment displaying the unified trust panel. */
class TrustPanelFragment : BottomSheetDialogFragment() {

    private val args by navArgs<TrustPanelFragmentArgs>()
    private val trackersBlockedFeature = ViewBoundFeatureWrapper<TrackersBlockedFeature>()
    private val ipProtectionMenuBinding = ViewBoundFeatureWrapper<IPProtectionMenuBinding>()
    private val ipProtectionSnackbarBinding = ViewBoundFeatureWrapper<IPProtectionSnackbarBinding>()
    private val securityInfoBinding = ViewBoundFeatureWrapper<SecurityInfoBinding>()
    private val snackbarHostState = SnackbarHostState()
    private lateinit var permissionsCallback: ((Map<String, Boolean>) -> Unit)
    private val requestPermissionsLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
            isGranted: Map<String, Boolean> ->
            permissionsCallback.invoke(isGranted)
        }

    private val store by
        fragmentStore(TrustPanelState()) {
            val lifecycleScope = viewLifecycleOwner.lifecycle.coroutineScope
            TrustPanelStore(
                isTrackingProtectionEnabled = args.isTrackingProtectionEnabled,
                websiteInfoState =
                    WebsiteInfoState(
                        isSecured = args.isSecured,
                        websiteUrl = args.url,
                        websiteTitle = args.title,
                        certificate = args.certificate,
                    ),
                sessionState = requireComponents.core.store.state.findTabOrCustomTab(args.sessionId),
                settings = requireComponents.settings,
                sitePermissions = args.sitePermissions,
                permissionHighlights = args.permissionHighlights,
                isPermissionBlockedByAndroid = { phoneFeature ->
                    !phoneFeature.isAndroidPermissionGranted(requireContext())
                },
                middleware =
                    listOf(
                        TrustPanelMiddleware(
                            engine = requireComponents.core.engine,
                            publicSuffixList = requireComponents.publicSuffixList,
                            sessionUseCases = requireComponents.useCases.sessionUseCases,
                            trackingProtectionUseCases = requireComponents.useCases.trackingProtectionUseCases,
                            settings = requireComponents.settings,
                            permissionStorage = requireComponents.core.permissionStorage,
                            requestPermissionsLauncher = requestPermissionsLauncher,
                            onDismiss = {
                                withContext(Dispatchers.Main) {
                                    this@TrustPanelFragment.dismiss()
                                }
                            },
                            scope = lifecycleScope,
                        ),
                        TrustPanelNavigationMiddleware(
                            navController = findNavController(),
                            privacySecurityPrefKey =
                                requireContext().getString(R.string.pref_key_privacy_security_category),
                            appStore = requireComponents.appStore,
                            tabsUseCases = requireComponents.useCases.tabsUseCases,
                            scope = lifecycleScope,
                        ),
                        TrustPanelTelemetryMiddleware(),
                    ),
            )
        }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        (super.onCreateDialog(savedInstanceState) as BottomSheetDialog).apply {
            setOnShowListener {
                runIfFragmentIsAttached {
                    val bottomSheet = findViewById<FrameLayout>(materialR.id.design_bottom_sheet)
                    bottomSheet?.let {
                        ViewCompat.setOnApplyWindowInsetsListener(it) { view, insets ->
                            val systemBarInsets = insets.getInsets(systemBars())
                            view.setPadding(0, systemBarInsets.top, 0, systemBarInsets.bottom)
                            insets
                        }
                    }
                    bottomSheet?.setBackgroundResource(R.drawable.bottom_sheet_with_top_rounded_corners)

                    behavior.peekHeight = context.resources.displayMetrics.heightPixels
                    behavior.state = BottomSheetBehavior.STATE_EXPANDED
                }
            }
        }

    @Suppress("LongMethod", "MagicNumber", "CognitiveComplexMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        FirefoxTheme {
            val components = components
            val settings = components.settings
            val appStore = components.appStore

            val initRoute = Route.ProtectionPanel
            var contentState: Route by remember { mutableStateOf(initRoute) }
            val isShowingProtectionsDashboard =
                remember(contentState) {
                    contentState == Route.TrackersProtectionDashboard
                }

            MenuDialogBottomSheet(
                modifier = Modifier.padding(top = 8.dp, bottom = 5.dp).fillMaxWidth(0.1f),
                onRequestDismiss = ::dismiss,
                menuHandleState =
                    MenuHandleState(
                        contentDescription = "",
                        visible = !isShowingProtectionsDashboard,
                    ),
                snackbarHostState = snackbarHostState,
                cornerShape =
                    if (isShowingProtectionsDashboard) {
                            MaterialTheme.shapes.extraLarge
                        } else {
                            MaterialTheme.shapes.large
                        }
                        .copy(
                            bottomStart = CornerSize(0.dp),
                            bottomEnd = CornerSize(0.dp),
                        ),
            ) {
                val websiteInfoState by remember {
                    store.stateFlow.map { state -> state.websiteInfoState }
                }
                    .collectAsState(initial = store.state.websiteInfoState)
                val baseDomain by remember {
                    store.stateFlow.map { state -> state.baseDomain }
                }
                    .collectAsState(initial = null)
                val isTrackingProtectionEnabled by remember {
                    store.stateFlow.map { state -> state.isTrackingProtectionEnabled }
                }
                    .collectAsState(initial = store.state.isTrackingProtectionEnabled)
                val numberOfTrackersBlocked by remember {
                    store.stateFlow.map { state -> state.numberOfTrackersBlocked }
                }
                    .collectAsState(initial = store.state.numberOfTrackersBlocked)
                val numberOfTrackersBlockedThisWeek by remember {
                    appStore.stateFlow.map { state ->
                        state.blockedTrackersState.trackersBlockedThisWeek.sumOf { it.count }
                    }
                }
                    .collectAsState(
                        initial = appStore.state.blockedTrackersState.trackersBlockedThisWeek.sumOf { it.count }
                    )
                val bucketedTrackers by remember {
                    store.stateFlow.map { state -> state.bucketedTrackers }
                }
                    .collectAsState(initial = store.state.bucketedTrackers)
                val detailedTrackerCategory by remember {
                    store.stateFlow.map { state -> state.detailedTrackerCategory }
                }
                    .collectAsState(initial = null)
                val sessionState by remember {
                    store.stateFlow.map { state -> state.sessionState }
                }
                    .collectAsState(initial = null)
                val websitePermissions by remember {
                    store.stateFlow.map { state -> state.websitePermissionsState.values }
                }
                    .collectAsState(initial = listOf())
                val isGlobalTrackingProtectionEnabled = settings.shouldUseTrackingProtection
                val showIpProtection = components.ipProtection.store.state.isEligible
                val ipProtectionMenuState by remember {
                    store.stateFlow.map { state -> state.ipProtectionMenuState }
                }
                    .collectAsState(initial = store.state.ipProtectionMenuState)

                permissionsCallback = { isGranted: Map<String, Boolean> ->
                    if (isGranted.values.all { it }) {
                        val phoneFeature = PhoneFeature.findFeatureBy(isGranted.keys.toTypedArray())

                        phoneFeature?.let {
                            store.dispatch(
                                TrustPanelAction.WebsitePermissionAction.GrantPermissionBlockedByAndroid(phoneFeature)
                            )
                        }
                    } else {
                        if (isGranted.keys.any { !shouldShowRequestPermissionRationale(it) }) {
                            // The user has permanently blocked these permissions and is trying to enable them.
                            // At this point, we are not able to request these permissions; the only way to allow
                            // them is to take the user to the system app setting page, and there the user can
                            // choose to allow the permissions.
                            startActivity(
                                Intent().apply {
                                    action = android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS
                                    data = Uri.fromParts("package", BuildConfig.APPLICATION_ID, null)
                                }
                            )
                        }
                    }
                }

                BackHandler {
                    when (contentState) {
                        Route.TrackersPanel -> contentState = Route.ProtectionPanel

                        Route.TrackerCategoryDetailsPanel -> contentState = Route.TrackersPanel

                        Route.TrackersProtectionDashboard -> contentState = Route.TrackersPanel

                        else -> this@TrustPanelFragment.dismissAllowingStateLoss()
                    }
                }

                LaunchedEffect(Unit) {
                    observeTrackersChange(components.core.store) {
                        components.useCases.trackingProtectionUseCases.fetchTrackingLogs(
                            tabId = args.sessionId,
                            onSuccess = { trackerLogs ->
                                store.dispatch(TrustPanelAction.UpdateTrackersBlocked(trackerLogs))
                            },
                            onError = {
                                Logger.error("TrackingProtectionUseCases - fetchTrackingLogs onError", it)
                            },
                        )
                    }
                }

                LaunchedEffect(Unit) {
                    store.dispatch(TrustPanelAction.RequestQWAC)
                }

                AnimatedContent(
                    targetState = contentState,
                    transitionSpec = trustPanelTransitionSpec(contentState),
                    label = "MenuDialogAnimation",
                ) { route ->
                    when (route) {
                        Route.ProtectionPanel -> {
                            ProtectionPanel(
                                websiteInfoState = websiteInfoState,
                                ipProtectionMenuState = ipProtectionMenuState,
                                icon = sessionState?.content?.icon,
                                isTrackingProtectionEnabled = isTrackingProtectionEnabled,
                                isGlobalTrackingProtectionEnabled = isGlobalTrackingProtectionEnabled,
                                isLocalPdf = args.isLocalPdf,
                                isPrivate = sessionState?.content?.private ?: false,
                                showIPProtection = showIpProtection,
                                numberOfTrackersBlocked = numberOfTrackersBlocked,
                                websitePermissions = websitePermissions.filter { it.isVisible },
                                onTrackerBlockedMenuClick = {
                                    contentState = Route.TrackersPanel
                                },
                                onTrackingProtectionToggleClick = {
                                    store.dispatch(TrustPanelAction.ToggleTrackingProtection)
                                },
                                onClearSiteDataMenuClick = {
                                    store.dispatch(TrustPanelAction.RequestClearSiteDataDialog)
                                    contentState = Route.ClearSiteDataDialog
                                },
                                onPrivacySecuritySettingsClick = {
                                    store.dispatch(TrustPanelAction.Navigate.PrivacySecuritySettings)
                                },
                                onAutoplayValueClick = { autoplayValue ->
                                    store.dispatch(TrustPanelAction.UpdateAutoplayValue(autoplayValue))
                                },
                                onToggleablePermissionClick = { websitePermission: WebsitePermission.Toggleable ->
                                    store.dispatch(TrustPanelAction.TogglePermission(websitePermission))
                                },
                                onViewCertificateClick = {
                                    store.dispatch(TrustPanelAction.Navigate.SecurityCertificate)
                                },
                                onViewQWACClick = {
                                    store.dispatch(TrustPanelAction.Navigate.QWAC)
                                },
                                onIPProtectionToggle = {
                                    handleIPProtectionToggleAndRecordTelemetry(ipProtectionMenuState, components)
                                },
                                onIPProtectionNavigate = {
                                    Vpn.settingsPageTapped.record(
                                        Vpn.SettingsPageTappedExtra(entrypoint = "Trust Panel")
                                    )
                                    store.dispatch(TrustPanelAction.Navigate.IPProtectionSettings)
                                },
                            )
                        }

                        Route.TrackersPanel -> {
                            TrackersBlockedPanel(
                                title = args.title,
                                url = args.url,
                                numberOfTrackersBlocked = numberOfTrackersBlocked,
                                numberOfTrackersBlockedThisWeek = numberOfTrackersBlockedThisWeek,
                                bucketedTrackers = bucketedTrackers,
                                onTrackerCategoryClick = { detailedTrackerCategory ->
                                    store.dispatch(
                                        TrustPanelAction.UpdateDetailedTrackerCategory(detailedTrackerCategory)
                                    )
                                    contentState = Route.TrackerCategoryDetailsPanel
                                },
                                onTrackersBlockedThisWeekClicked = {
                                    store.dispatch(TrustPanelAction.Navigate.TrackersProtectionDashboard)
                                    contentState = Route.TrackersProtectionDashboard
                                },
                                onBackButtonClick = {
                                    contentState = Route.ProtectionPanel
                                },
                            )
                        }

                        Route.TrackerCategoryDetailsPanel -> {
                            TrackerCategoryDetailsPanel(
                                title = args.title,
                                url = args.url,
                                detailedTrackerCategory = detailedTrackerCategory,
                                bucketedTrackers = bucketedTrackers,
                                onBackButtonClick = {
                                    contentState = Route.TrackersPanel
                                },
                            )
                        }

                        Route.TrackersProtectionDashboard -> {
                            val appStore = requireComponents.appStore
                            val blockedTrackersState by appStore.observeAsComposableState { state ->
                                state.blockedTrackersState
                            }

                            ProtectionsDashboardContent(
                                totalTrackersBlocked = blockedTrackersState.trackersBlockedCount,
                                trackersBlockedThisWeek = blockedTrackersState.trackersBlockedThisWeek,
                                earliestTrackingDate = blockedTrackersState.earliestTrackingDate,
                                onDismiss = {
                                    contentState = Route.TrackersPanel
                                },
                            )
                        }

                        Route.ClearSiteDataDialog -> {
                            ClearSiteDataDialog(
                                baseDomain = baseDomain ?: "",
                                onClearSiteDataClick = {
                                    store.dispatch(TrustPanelAction.ClearSiteData)
                                },
                                onCancelClick = { ::dismiss.invoke() },
                            )
                        }
                    }
                }
            }
        }
    }

    private fun handleIPProtectionToggleAndRecordTelemetry(
        ipProtectionMenuState: IPProtectionMenuState,
        components: Components,
    ) {
        recordIPProtectionTelemetry(ipProtectionMenuState.status)

        when (ipProtectionMenuState.status) {
            IPProtectionStatus.AuthRequired,
            IPProtectionStatus.AwaitingService -> store.dispatch(TrustPanelAction.Navigate.IPProtectionSettings)

            IPProtectionStatus.Disabled,
            IPProtectionStatus.Activating,
            IPProtectionStatus.Enabled,
            IPProtectionStatus.ConnectionError ->
                components.ipProtection.store.dispatch(IPProtectionAction.ActivationAction.Toggle)

            IPProtectionStatus.DataLimitReached,
            IPProtectionStatus.Unavailable,
            IPProtectionStatus.Uninitialized -> Unit
        }
    }

    private fun recordIPProtectionTelemetry(status: IPProtectionStatus) {
        when (status) {
            IPProtectionStatus.Disabled -> Vpn.trustPanelTurnedOn.record()
            IPProtectionStatus.Enabled -> Vpn.trustPanelTurnedOff.record()
            IPProtectionStatus.AuthRequired -> Vpn.trustPanelTryItTapped.record()

            IPProtectionStatus.Unavailable,
            IPProtectionStatus.Uninitialized,
            IPProtectionStatus.AwaitingService,
            IPProtectionStatus.DataLimitReached,
            IPProtectionStatus.ConnectionError,
            IPProtectionStatus.Activating -> Unit
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        if (requireComponents.settings.shouldUseTrackingProtection) {
            trackersBlockedFeature.set(
                feature =
                    TrackersBlockedFeature(
                        browserStore = requireComponents.core.store,
                        appStore = requireComponents.appStore,
                        currentSessionId = args.sessionId,
                        trackingProtectionUseCases = requireComponents.useCases.trackingProtectionUseCases,
                    ),
                owner = viewLifecycleOwner,
                view = view,
            )
        }

        ipProtectionMenuBinding.set(
            feature =
                IPProtectionMenuBinding(
                    ipProtectionStore = requireComponents.ipProtection.store,
                    onIPProtectionStatusUpdate = {
                        store.dispatch(TrustPanelAction.UpdateIPProtectionMenuState(it))
                    },
                ),
            owner = this@TrustPanelFragment,
            view = view,
        )

        ipProtectionSnackbarBinding.set(
            feature =
                IPProtectionSnackbarBinding(
                    appStore = requireComponents.appStore,
                    context = requireContext(),
                    navController = findNavController(),
                    snackbarDelegate =
                        FenixSnackbarDelegate(
                            snackbarHostState = snackbarHostState,
                            scope = viewLifecycleOwner.lifecycleScope,
                            context = requireContext(),
                        ),
                ),
            owner = this,
            view = view,
        )

        securityInfoBinding.set(
            feature =
                SecurityInfoBinding(
                    browserStore = requireComponents.core.store,
                    sessionId = args.sessionId,
                    urlHost = args.url.toUri().host,
                    onSecurityInfoUpdate = { store.dispatch(it) },
                    onHostChange = { dismissAllowingStateLoss() },
                ),
            owner = viewLifecycleOwner,
            view = view,
        )
    }

    @Composable
    private fun trustPanelTransitionSpec(
        contentState: Route
    ): AnimatedContentTransitionScope<Route>.() -> ContentTransform = {
        if (contentState == Route.ProtectionPanel || contentState == Route.ClearSiteDataDialog) {
            enterMenu(
                    duration = DURATION_MS_MAIN_MENU,
                    delay = DELAY_MS_MAIN_MENU,
                    easing = LinearOutSlowInEasing,
                )
                .togetherWith(
                    exitSubmenu(
                        duration = DURATION_MS_MAIN_MENU,
                        easing = FastOutLinearInEasing,
                    )
                ) using
                SizeTransform { initialSize, targetSize ->
                    contentGrowth(initialSize, targetSize, DURATION_MS_MAIN_MENU)
                }
        } else {
            enterSubmenu(
                    duration = DURATION_MS_SUB_MENU,
                    delay = DELAY_MS_SUB_MENU,
                    easing = LinearOutSlowInEasing,
                )
                .togetherWith(
                    exitMenu(
                        duration = DURATION_MS_SUB_MENU,
                        easing = FastOutLinearInEasing,
                    )
                ) using
                SizeTransform { initialSize, targetSize ->
                    contentGrowth(initialSize, targetSize, DURATION_MS_SUB_MENU)
                }
        }
    }

    @OptIn(FlowPreview::class)
    private fun observeTrackersChange(store: BrowserStore, onChange: (SessionState) -> Unit) {
        val currentSession = store.state.findTabOrCustomTab(args.sessionId) ?: return

        // Dispatch an immediate change signal to ensure an initial blocked trackers information fetch.
        onChange(currentSession)

        consumeFlow(store) { flow ->
            flow
                .mapNotNull { state -> state.findTabOrCustomTab(args.sessionId) }
                .ifAnyChanged { tab -> arrayOf(tab.trackingProtection.blockedTrackers) }
                .debounce(1.seconds)
                .collect(onChange)
        }
    }
}

/** Trust panel navigation destination. */
enum class Route {
    ProtectionPanel,
    TrackersPanel,
    TrackerCategoryDetailsPanel,
    TrackersProtectionDashboard,
    ClearSiteDataDialog,
}

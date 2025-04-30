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
import androidx.activity.compose.BackHandler
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.ContentTransform
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.togetherWith
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.core.content.ContextCompat
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.ext.consumeFlow
import mozilla.components.lib.state.ext.observeAsState
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.view.setNavigationBarColorCompat
import mozilla.components.support.ktx.kotlinx.coroutines.flow.ifAnyChanged
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.menu.compose.MenuDialogBottomSheet
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelMiddleware
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelNavigationMiddleware
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelTelemetryMiddleware
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermission
import org.mozilla.fenix.settings.trustpanel.ui.ClearSiteDataDialog
import org.mozilla.fenix.settings.trustpanel.ui.ConnectionSecurityPanel
import org.mozilla.fenix.settings.trustpanel.ui.ProtectionPanel
import org.mozilla.fenix.settings.trustpanel.ui.TrackerCategoryDetailsPanel
import org.mozilla.fenix.settings.trustpanel.ui.TrackersBlockedPanel
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.trackingprotection.TrackerBuckets
import org.mozilla.fenix.utils.DELAY_MS_MAIN_MENU
import org.mozilla.fenix.utils.DELAY_MS_SUB_MENU
import org.mozilla.fenix.utils.DURATION_MS_MAIN_MENU
import org.mozilla.fenix.utils.DURATION_MS_SUB_MENU
import org.mozilla.fenix.utils.contentGrowth
import org.mozilla.fenix.utils.enterMenu
import org.mozilla.fenix.utils.enterSubmenu
import org.mozilla.fenix.utils.exitMenu
import org.mozilla.fenix.utils.exitSubmenu

/**
 * A bottom sheet dialog fragment displaying the unified trust panel.
 */
class TrustPanelFragment : BottomSheetDialogFragment() {

    private val args by navArgs<TrustPanelFragmentArgs>()

    private lateinit var permissionsCallback: ((Map<String, Boolean>) -> Unit)
    private val requestPermissionsLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { isGranted: Map<String, Boolean> -> permissionsCallback.invoke(isGranted) }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val safeActivity = activity ?: return@setOnShowListener
                val browsingModeManager = (safeActivity as HomeActivity).browsingModeManager

                val navigationBarColor = if (browsingModeManager.mode.isPrivate) {
                    ContextCompat.getColor(context, R.color.fx_mobile_private_layer_color_3)
                } else {
                    ContextCompat.getColor(context, R.color.fx_mobile_layer_color_3)
                }

                window?.setNavigationBarColorCompat(navigationBarColor)

                val bottomSheet = findViewById<View?>(R.id.design_bottom_sheet)
                bottomSheet?.setBackgroundResource(android.R.color.transparent)

                val behavior = BottomSheetBehavior.from(bottomSheet)
                behavior.peekHeight = resources.displayMetrics.heightPixels
                behavior.state = BottomSheetBehavior.STATE_EXPANDED
            }
        }

    @Suppress("LongMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = ComposeView(requireContext()).apply {
        setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)

        setContent {
            FirefoxTheme {
                val components = components
                val trackingProtectionUseCases = components.useCases.trackingProtectionUseCases
                val settings = components.settings

                val coroutineScope = rememberCoroutineScope()
                val store = remember {
                    TrustPanelStore(
                        isTrackingProtectionEnabled = args.isTrackingProtectionEnabled,
                        websiteInfoState = WebsiteInfoState(
                            isSecured = args.isSecured,
                            websiteUrl = args.url,
                            websiteTitle = args.title,
                            certificateName = args.certificateName,
                        ),
                        sessionState = components.core.store.state.findTabOrCustomTab(args.sessionId),
                        settings = settings,
                        sitePermissions = args.sitePermissions,
                        permissionHighlights = args.permissionHighlights,
                        isPermissionBlockedByAndroid = { phoneFeature ->
                            !phoneFeature.isAndroidPermissionGranted(requireContext())
                        },
                        middleware = listOf(
                            TrustPanelMiddleware(
                                appStore = components.appStore,
                                engine = components.core.engine,
                                publicSuffixList = components.publicSuffixList,
                                sessionUseCases = components.useCases.sessionUseCases,
                                trackingProtectionUseCases = trackingProtectionUseCases,
                                settings = settings,
                                permissionStorage = components.core.permissionStorage,
                                requestPermissionsLauncher = requestPermissionsLauncher,
                                onDismiss = {
                                    withContext(Dispatchers.Main) {
                                        this@TrustPanelFragment.dismiss()
                                    }
                                },
                                scope = coroutineScope,
                            ),
                            TrustPanelNavigationMiddleware(
                                navController = findNavController(),
                                privacySecurityPrefKey = requireContext().getString(
                                    R.string.pref_key_privacy_security_category,
                                ),
                                scope = coroutineScope,
                            ),
                            TrustPanelTelemetryMiddleware(),
                        ),
                    )
                }

                MenuDialogBottomSheet(
                    onRequestDismiss = ::dismiss,
                    handlebarContentDescription = "",
                ) {
                    val baseDomain by store.observeAsState(initialValue = null) { state ->
                        state.baseDomain
                    }
                    val isTrackingProtectionEnabled by store.observeAsState(initialValue = false) { state ->
                        state.isTrackingProtectionEnabled
                    }
                    val numberOfTrackersBlocked by store.observeAsState(initialValue = 0) { state ->
                        state.numberOfTrackersBlocked
                    }
                    val bucketedTrackers by store.observeAsState(initialValue = TrackerBuckets()) { state ->
                        state.bucketedTrackers
                    }
                    val detailedTrackerCategory by store.observeAsState(initialValue = null) { state ->
                        state.detailedTrackerCategory
                    }
                    val sessionState by store.observeAsState(initialValue = null) { state ->
                        state.sessionState
                    }
                    val websitePermissions by store.observeAsState(initialValue = listOf()) { state ->
                        state.websitePermissionsState.values
                    }

                    permissionsCallback = { isGranted: Map<String, Boolean> ->
                        if (isGranted.values.all { it }) {
                            val phoneFeature = PhoneFeature.findFeatureBy(isGranted.keys.toTypedArray())

                            phoneFeature?.let {
                                store.dispatch(
                                    TrustPanelAction.WebsitePermissionAction
                                        .GrantPermissionBlockedByAndroid(phoneFeature),
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
                                    },
                                )
                            }
                        }
                    }

                    val initRoute = Route.ProtectionPanel

                    var contentState: Route by remember { mutableStateOf(initRoute) }

                    BackHandler {
                        when (contentState) {
                            Route.TrackersPanel,
                            Route.ConnectionSecurityPanel,
                            -> contentState = Route.ProtectionPanel

                            Route.TrackerCategoryDetailsPanel,
                            -> contentState = Route.TrackersPanel

                            else -> this@TrustPanelFragment.dismissAllowingStateLoss()
                        }
                    }

                    observeTrackersChange(components.core.store) {
                        trackingProtectionUseCases.fetchTrackingLogs(
                            tabId = args.sessionId,
                            onSuccess = { trackerLogs ->
                                store.dispatch(TrustPanelAction.UpdateTrackersBlocked(trackerLogs))
                            },
                            onError = {
                                Logger.error("TrackingProtectionUseCases - fetchTrackingLogs onError", it)
                            },
                        )
                    }

                    AnimatedContent(
                        targetState = contentState,
                        transitionSpec = trustPanelTransitionSpec(contentState),
                        label = "MenuDialogAnimation",
                    ) { route ->
                        when (route) {
                            Route.ProtectionPanel -> {
                                ProtectionPanel(
                                    icon = sessionState?.content?.icon,
                                    isTrackingProtectionEnabled = isTrackingProtectionEnabled,
                                    numberOfTrackersBlocked = numberOfTrackersBlocked,
                                    websiteInfoState = store.state.websiteInfoState,
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
                                    onConnectionSecurityClick = {
                                        contentState = Route.ConnectionSecurityPanel
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
                                )
                            }

                            Route.TrackersPanel -> {
                                TrackersBlockedPanel(
                                    title = args.title,
                                    numberOfTrackersBlocked = numberOfTrackersBlocked,
                                    bucketedTrackers = bucketedTrackers,
                                    onTrackerCategoryClick = { detailedTrackerCategory ->
                                        store.dispatch(
                                            TrustPanelAction.UpdateDetailedTrackerCategory(detailedTrackerCategory),
                                        )
                                        contentState = Route.TrackerCategoryDetailsPanel
                                    },
                                    onBackButtonClick = {
                                        contentState = Route.ProtectionPanel
                                    },
                                )
                            }

                            Route.TrackerCategoryDetailsPanel -> {
                                TrackerCategoryDetailsPanel(
                                    title = args.title,
                                    isTotalCookieProtectionEnabled = components.settings.enabledTotalCookieProtection,
                                    detailedTrackerCategory = detailedTrackerCategory,
                                    bucketedTrackers = bucketedTrackers,
                                    onBackButtonClick = {
                                        contentState = Route.TrackersPanel
                                    },
                                )
                            }

                            Route.ConnectionSecurityPanel -> {
                                ConnectionSecurityPanel(
                                    websiteInfoState = store.state.websiteInfoState,
                                    onBackButtonClick = {
                                        contentState = Route.ProtectionPanel
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
    }

    @Composable
    private fun trustPanelTransitionSpec(
        contentState: Route,
    ): AnimatedContentTransitionScope<Route>.() -> ContentTransform = {
        if (contentState == Route.ProtectionPanel || contentState == Route.ClearSiteDataDialog) {
            enterMenu(
                duration = DURATION_MS_MAIN_MENU,
                delay = DELAY_MS_MAIN_MENU,
                easing = LinearOutSlowInEasing,
            ).togetherWith(
                exitSubmenu(
                    duration = DURATION_MS_MAIN_MENU,
                    easing = FastOutLinearInEasing,
                ),
            ) using SizeTransform { initialSize, targetSize ->
                contentGrowth(initialSize, targetSize, DURATION_MS_MAIN_MENU)
            }
        } else {
            enterSubmenu(
                duration = DURATION_MS_SUB_MENU,
                delay = DELAY_MS_SUB_MENU,
                easing = LinearOutSlowInEasing,
            ).togetherWith(
                exitMenu(
                    duration = DURATION_MS_SUB_MENU,
                    easing = FastOutLinearInEasing,
                ),
            ) using SizeTransform { initialSize, targetSize ->
                contentGrowth(initialSize, targetSize, DURATION_MS_SUB_MENU)
            }
        }
    }

    private fun observeTrackersChange(store: BrowserStore, onChange: (SessionState) -> Unit) {
        consumeFlow(store) { flow ->
            flow.mapNotNull { state -> state.findTabOrCustomTab(args.sessionId) }
                .ifAnyChanged { tab -> arrayOf(tab.trackingProtection.blockedTrackers) }
                .collect(onChange)
        }
    }
}

/**
 * Trust panel navigation destination.
 */
enum class Route {
    ProtectionPanel,
    TrackersPanel,
    TrackerCategoryDetailsPanel,
    ConnectionSecurityPanel,
    ClearSiteDataDialog,
}

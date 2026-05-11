/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.annotation.VisibleForTesting
import androidx.appcompat.content.res.AppCompatResources
import androidx.core.graphics.drawable.toDrawable
import androidx.core.net.toUri
import androidx.lifecycle.ViewModel
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.CopyToClipboardClicked
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsEndUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.BrowserActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.PageActionsStartUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserDisplayToolbarAction.UpdateProgressBarConfig
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.Init
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.concept.engine.cookiehandling.CookieBannersStorage
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.feature.ipprotection.Authorized
import mozilla.components.feature.ipprotection.IPProtectionStore
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.feature.tabs.CustomTabsUseCases
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.ktx.kotlin.applyRegistrableDomainSpan
import mozilla.components.support.ktx.kotlin.getOrigin
import mozilla.components.support.ktx.kotlin.isContentUrl
import mozilla.components.support.ktx.kotlin.isIpv4OrIpv6
import mozilla.components.support.ktx.kotlin.trimmed
import mozilla.components.support.ktx.kotlinx.coroutines.flow.ifAnyChanged
import mozilla.components.support.utils.ClipboardHandler
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.Toolbar
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.URLCopiedToClipboard
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.DisplayActions.MenuClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.DisplayActions.ShareClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.EndPageActions.CustomButtonClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.StartBrowserActions.CloseClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.StartPageActions.SiteInfoClicked
import org.mozilla.fenix.customtabs.ExternalAppBrowserFragmentDirections
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.settings.quicksettings.protections.cookiebanners.getCookieBannerUIMode
import org.mozilla.fenix.telemetry.ACTION_CLOSE_CLICKED
import org.mozilla.fenix.telemetry.ACTION_MENU_CLICKED
import org.mozilla.fenix.telemetry.ACTION_SECURITY_INDICATOR_CLICKED
import org.mozilla.fenix.telemetry.ACTION_SHARE_CLICKED
import org.mozilla.fenix.telemetry.ACTION_SITE_CUSTOM_CLICKED
import org.mozilla.fenix.telemetry.SOURCE_CUSTOM_BAR
import org.mozilla.fenix.utils.Settings
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.feature.customtabs.R as customtabsR
import mozilla.components.lib.state.Action as MVIAction
import mozilla.components.ui.icons.R as iconsR

private const val CUSTOM_BUTTON_CLICK_RETURN_CODE = 0

/**
 * [Middleware] responsible for configuring and handling interactions with the composable toolbar
 * when shown in a custom tab.
 *
 * This is also a [ViewModel] allowing to be easily persisted between activity restarts.
 *
 * @param uiContext [Context] used for various system interactions.
 * @param customTabId [String] of the custom tab in which the toolbar is shown.
 * @param browserStore [BrowserStore] to sync from.
 * @param appStore [AppStore] allowing to integrate with other features of the applications.
 * @param ipProtectionStore [IPProtectionStore] to observe IP protection proxy status.
 * @param permissionsStorage [SitePermissionsStorage] to sync from.
 * @param cookieBannersStorage [CookieBannersStorage] to sync from.
 * @param useCases [CustomTabsUseCases] used for cleanup when closing the custom tab.
 * @param trackingProtectionUseCases [TrackingProtectionUseCases] allowing to query
 * tracking protection data of the current tab.
 * @param publicSuffixList [PublicSuffixList] used to obtain the base domain of the current site.
 * @param clipboard [ClipboardHandler] to use for reading from device's clipboard.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param closeTabDelegate Callback for when the current custom tab needs to be closed.
 * @param settings [Settings] for accessing user preferences.
 * @param scope [CoroutineScope] used for running long running operations in background.
 * @param isSandboxCustomTab Whether the custom tab is sandboxed.
 */
@Suppress("LongParameterList")
class CustomTabBrowserToolbarMiddleware(
    private val uiContext: Context,
    private val customTabId: String,
    private val browserStore: BrowserStore,
    private val appStore: AppStore,
    private val ipProtectionStore: IPProtectionStore,
    private val permissionsStorage: SitePermissionsStorage,
    private val cookieBannersStorage: CookieBannersStorage,
    private val useCases: CustomTabsUseCases,
    private val trackingProtectionUseCases: TrackingProtectionUseCases,
    private val publicSuffixList: PublicSuffixList,
    private val clipboard: ClipboardHandler,
    private val navController: NavController,
    private val closeTabDelegate: () -> Unit,
    private val settings: Settings,
    private val scope: CoroutineScope,
    private val isSandboxCustomTab: Boolean = false,
) : Middleware<BrowserToolbarState, BrowserToolbarAction> {
    private val customTab
        get() = browserStore.state.findCustomTab(customTabId)
    private var wasTitleShown = false

    @Suppress("LongMethod")
    override fun invoke(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        next: (BrowserToolbarAction) -> Unit,
        action: BrowserToolbarAction,
    ) {
        when (action) {
            is Init -> {
                next(action)

                val customTab = customTab
                updateStartPageActions(store, customTab)
                updateStartBrowserActions(store, customTab)
                updateCurrentPageOrigin(store, customTab)
                updateEndPageActions(store, customTab)
                updateEndBrowserActions(store)

                observePageLoadUpdates(store)
                observePageOriginUpdates(store)
                observePageSecurityUpdates(store)
                observePageTrackingProtectionUpdates(store)
                observeIPProtectionUpdates(store)
            }

            is CloseClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(source = SOURCE_CUSTOM_BAR, item = ACTION_CLOSE_CLICKED),
                )

                useCases.remove(customTabId)
                closeTabDelegate()
            }

            is SiteInfoClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(source = SOURCE_CUSTOM_BAR, item = ACTION_SECURITY_INDICATOR_CLICKED),
                )

                val safeCustomTab = customTab ?: return
                scope.launch(Dispatchers.IO) {
                    val sitePermissions: SitePermissions? = safeCustomTab.content.url.getOrigin()?.let { origin ->
                        permissionsStorage.findSitePermissionsBy(origin, private = safeCustomTab.content.private)
                    }

                    scope.launch(Dispatchers.Main) {
                        trackingProtectionUseCases.containsException(customTabId) { isExcepted ->
                            scope.launch {
                                val cookieBannerUIMode = cookieBannersStorage.getCookieBannerUIMode(
                                    tab = safeCustomTab,
                                    isFeatureEnabledInPrivateMode = settings.shouldUseCookieBannerPrivateMode,
                                    publicSuffixList = publicSuffixList,
                                )

                                val directions = if (settings.enableUnifiedTrustPanel) {
                                    ExternalAppBrowserFragmentDirections.actionGlobalTrustPanelFragment(
                                        sessionId = safeCustomTab.id,
                                        url = safeCustomTab.content.url,
                                        title = safeCustomTab.content.title,
                                        isLocalPdf = safeCustomTab.content.url.isContentUrl(),
                                        isSecured = safeCustomTab.content.securityInfo.isSecure,
                                        sitePermissions = sitePermissions,
                                        certificate = safeCustomTab.content.securityInfo.certificate,
                                        permissionHighlights = safeCustomTab.content.permissionHighlights,
                                        isTrackingProtectionEnabled =
                                            safeCustomTab.trackingProtection.enabled && !isExcepted,
                                        cookieBannerUIMode = cookieBannerUIMode,
                                    )
                                } else {
                                    ExternalAppBrowserFragmentDirections
                                        .actionGlobalQuickSettingsSheetDialogFragment(
                                            sessionId = customTabId,
                                            url = safeCustomTab.content.url,
                                            title = safeCustomTab.content.title,
                                            isLocalPdf = safeCustomTab.content.url.isContentUrl(),
                                            isSecured = safeCustomTab.content.securityInfo.isSecure,
                                            sitePermissions = sitePermissions,
                                            gravity = settings.toolbarPosition.androidGravity,
                                            certificateName = safeCustomTab.content.securityInfo.issuer,
                                            permissionHighlights = safeCustomTab.content.permissionHighlights,
                                            isTrackingProtectionEnabled =
                                                safeCustomTab.trackingProtection.enabled && !isExcepted,
                                            cookieBannerUIMode = cookieBannerUIMode,
                                        )
                                }
                                navController.nav(
                                    R.id.externalAppBrowserFragment,
                                    directions,
                                )
                            }
                        }
                    }
                }
            }

            is CustomButtonClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(source = SOURCE_CUSTOM_BAR, item = ACTION_SITE_CUSTOM_CLICKED),
                )
                val customTab = customTab
                customTab?.config?.actionButtonConfig?.pendingIntent?.send(
                    uiContext,
                    CUSTOM_BUTTON_CLICK_RETURN_CODE,
                    Intent(null, customTab.content.url.toUri()),
                )
            }

            is ShareClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(source = SOURCE_CUSTOM_BAR, item = ACTION_SHARE_CLICKED),
                )
                val customTab = customTab
                navController.navigate(
                    NavGraphDirections.actionGlobalShareFragment(
                        sessionId = customTabId,
                        data = arrayOf(
                            ShareData(
                                url = customTab?.content?.url,
                                title = customTab?.content?.title,
                            ),
                        ),
                        showPage = true,
                    ),
                )
            }

            is MenuClicked -> {
                Toolbar.buttonTapped.record(
                    Toolbar.ButtonTappedExtra(source = SOURCE_CUSTOM_BAR, item = ACTION_MENU_CLICKED),
                )
                navController.nav(
                    R.id.externalAppBrowserFragment,
                    BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                        accesspoint = MenuAccessPoint.External,
                        customTabSessionId = customTabId,
                        isSandboxCustomTab = isSandboxCustomTab,
                    ),
                )
            }

            is CopyToClipboardClicked -> {
                Events.copyUrlTapped.record(NoExtras())

                clipboard.text = customTab?.content?.url?.also {
                    // Android 13+ shows by default a popup for copied text.
                    // Avoid overlapping popups informing the user when the URL is copied to the clipboard.
                    // and only show our snackbar when Android will not show an indication by default.
                    // See https://developer.android.com/develop/ui/views/touch-and-input/copy-paste#duplicate-notifications).
                    if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
                        appStore.dispatch(URLCopiedToClipboard)
                    }
                }
            }

            else -> next(action)
        }
    }

    private fun observePageOriginUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            mapNotNull { state -> state.findCustomTab(customTabId) }
                .ifAnyChanged { tab -> arrayOf(tab.content.title, tab.content.url) }
                .collect {
                    updateCurrentPageOrigin(store, it)
                }
        }
    }

    private fun observePageLoadUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            mapNotNull { state -> state.findCustomTab(customTabId) }
                .distinctUntilChangedBy { it.content.progress }
                .collect {
                    store.dispatch(
                        UpdateProgressBarConfig(
                            buildProgressBar(it.content.progress),
                        ),
                    )
                }
        }
    }

    private fun observePageSecurityUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        browserStore.observeWhileActive {
            mapNotNull { state -> state.findCustomTab(customTabId) }
                .distinctUntilChangedBy { tab -> tab.content.securityInfo }
                .collect {
                    updateStartPageActions(store, it)
                }
        }
    }

    private fun observePageTrackingProtectionUpdates(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
    ) {
        browserStore.observeWhileActive {
            mapNotNull { state -> state.findCustomTab(customTabId) }
                .distinctUntilChangedBy { tab -> tab.trackingProtection }
                .collect { updateStartPageActions(store, it) }
        }
    }

    private fun observeIPProtectionUpdates(store: Store<BrowserToolbarState, BrowserToolbarAction>) {
        ipProtectionStore.observeWhileActive {
            distinctUntilChangedBy { it.proxyStatus }
                .collect {
                    updateStartPageActions(store, customTab)
                }
        }
    }

    private fun updateStartBrowserActions(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        customTab: CustomTabSessionState?,
    ) = store.dispatch(
        BrowserActionsStartUpdated(
            buildStartBrowserActions(customTab),
        ),
    )

    private fun updateStartPageActions(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        customTab: CustomTabSessionState?,
    ) = store.dispatch(
        PageActionsStartUpdated(
            buildStartPageActions(customTab),
        ),
    )

    private fun updateCurrentPageOrigin(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        customTab: CustomTabSessionState?,
    ) {
        scope.launch {
            store.dispatch(
                BrowserDisplayToolbarAction.PageOriginUpdated(
                    PageOrigin(
                        hint = R.string.search_hint,
                        title = getTitleToShown(customTab),
                        url = getHostFromUrl()?.trimmed(),
                        contextualMenuOptions = listOf(ContextualMenuOption.CopyURLToClipboard),
                        onClick = null,
                    ),
                ),
            )
        }
    }

    private fun updateEndPageActions(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
        customTab: CustomTabSessionState?,
    ) = store.dispatch(
        BrowserDisplayToolbarAction.PageActionsEndUpdated(
            buildEndPageActions(customTab),
        ),
    )

    private fun updateEndBrowserActions(
        store: Store<BrowserToolbarState, BrowserToolbarAction>,
    ) = store.dispatch(
        BrowserActionsEndUpdated(
            buildEndBrowserActions(),
        ),
    )

    private fun buildStartBrowserActions(customTab: CustomTabSessionState?): List<Action> {
        val customTabConfig = customTab?.config
        val customIconBitmap = customTabConfig?.closeButtonIcon

        return when (customTabConfig?.showCloseButton) {
            true -> listOf(
                ActionButton(
                    drawable = when (customIconBitmap) {
                        null -> AppCompatResources.getDrawable(
                            uiContext,
                            iconsR.drawable.mozac_ic_cross_24,
                        )

                        else -> customIconBitmap.toDrawable(uiContext.resources)
                    },
                    contentDescription = uiContext.getString(
                        customtabsR.string.mozac_feature_customtabs_exit_button,
                    ),
                    onClick = CloseClicked,
                ),
            )

            else -> emptyList()
        }
    }

    private fun buildStartPageActions(customTab: CustomTabSessionState?) = buildList {
        if (customTab?.content?.url?.isContentUrl() == true) {
            add(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_page_portrait_24,
                    contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                    onClick = SiteInfoClicked,
                ),
            )
        } else if (customTab?.content?.securityInfo == null ||
            customTab.content.securityInfo == SecurityInfo.Unknown
        ) {
            add(
                buildSiteInfoAction(
                    drawableResId = iconsR.drawable.mozac_ic_globe_24,
                    contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                    onClick = object : BrowserToolbarEvent {},
                ),
            )
        } else if (
                customTab.content.securityInfo.isSecure &&
                customTab.trackingProtection.enabled &&
                !customTab.trackingProtection.ignoredOnTrackingProtection
            ) {
            add(
                buildSiteInfoAction(
                    drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
                    contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                    onClick = SiteInfoClicked,
                ),
            )
        } else {
            add(
                buildSiteInfoAction(
                    drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
                    contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
                    onClick = SiteInfoClicked,
                ),
            )
        }
    }

    private fun buildSiteInfoAction(
        drawableResId: Int,
        contentDescription: Int,
        onClick: BrowserToolbarInteraction,
    ): Action {
        return if (ipProtectionStore.state.proxyStatus == Authorized.Active) {
            Action.AnimatedPillActionRes(
                iconResId = drawableResId,
                overlayResId = iconsR.drawable.mozac_ic_globe_24,
                textResId = R.string.ip_protection_toolbar_pill_label,
                contentDescriptionResId = R.string.ip_protection_toolbar_pill_description,
                onClick = onClick,
            )
        } else {
            ActionButtonRes(
                drawableResId = drawableResId,
                contentDescription = contentDescription,
                onClick = onClick,
            )
        }
    }

    private fun buildEndPageActions(customTab: CustomTabSessionState?): List<ActionButton> {
        val customButtonConfig = customTab?.config?.actionButtonConfig
        val customButtonIcon = customButtonConfig?.icon

        return when (customButtonIcon) {
            null -> emptyList()
            else -> listOf(
                ActionButton(
                    drawable = customButtonIcon.toDrawable(uiContext.resources),
                    shouldTint = customTab.content.private || customButtonConfig.tint,
                    contentDescription = customButtonConfig.description,
                    onClick = CustomButtonClicked,
                ),
            )
        }
    }

    private fun buildEndBrowserActions() = buildList {
        add(
            ActionButtonRes(
                drawableResId = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
                contentDescription = R.string.content_description_menu,
                onClick = MenuClicked,
            ),
        )
    }

    private fun buildProgressBar(progress: Int = 0) = ProgressBarConfig(progress)

    /**
     * Get the host of the current URL with the registrable domain span applied.
     * If this cannot be done, the original URL is returned.
     */
    private suspend fun getHostFromUrl(): CharSequence? {
        val url = customTab?.content?.url
        val host = url?.toUri()?.host
        return when {
            host.isNullOrEmpty() -> url
            host.isIpv4OrIpv6() -> host
            else -> {
                val hostStart = url.indexOf(host)
                try {
                    url.applyRegistrableDomainSpan(publicSuffixList)
                        .subSequence(
                            startIndex = hostStart,
                            endIndex = hostStart + host.length,
                        )
                } catch (_: IndexOutOfBoundsException) {
                    host
                }
            }
        }
    }

    private fun getTitleToShown(customTab: CustomTabSessionState?): String? {
        val title = customTab?.content?.title
        // If we showed a title once in a custom tab then we are going to continue displaying
        // a title (to avoid the layout bouncing around).
        // However if no title is available then we just use the URL.
        return when {
            wasTitleShown && title.isNullOrBlank() -> customTab?.content?.url
            !title.isNullOrBlank() -> {
                wasTitleShown = true
                title
            }
            else -> null // title was not shown previously and is currently blank
        }
    }

    private inline fun <S : State, A : MVIAction> Store<S, A>.observeWhileActive(
        crossinline observe: suspend (Flow<S>.() -> Unit),
    ): Job = scope.launch { flow().observe() }

    /**
     * Static functionalities of the [BrowserToolbarMiddleware].
     */
    companion object {
        @VisibleForTesting
        internal sealed class StartBrowserActions : BrowserToolbarEvent {
            data object CloseClicked : StartBrowserActions()
        }

        @VisibleForTesting
        internal sealed class StartPageActions : BrowserToolbarEvent {
            data object SiteInfoClicked : StartPageActions()
        }

        @VisibleForTesting
        internal sealed class EndPageActions : BrowserToolbarEvent {
            data object CustomButtonClicked : EndPageActions()
        }

        @VisibleForTesting
        internal sealed class DisplayActions : BrowserToolbarEvent {
            data object ShareClicked : DisplayActions()
            data object MenuClicked : DisplayActions()
        }
    }
}

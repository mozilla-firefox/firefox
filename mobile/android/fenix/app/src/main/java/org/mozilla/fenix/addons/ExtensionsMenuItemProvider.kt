/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.content.Context
import androidx.appcompat.content.res.AppCompatResources
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.selector.pendingExtensionUpdatesNeedingAttention
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemActionButton
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIcon
import mozilla.components.compose.menu.ui.MenuItemIconBitmap
import mozilla.components.compose.menu.ui.MenuItemIconDrawable
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.AddonManagerException
import mozilla.components.feature.addons.ui.displayName
import mozilla.components.feature.addons.ui.summary
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.util.dpToPx
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.settings.SupportUtils.AMO_HOMEPAGE_FOR_ANDROID

private const val NUMBER_OF_RECOMMENDED_ADDONS_TO_SHOW = 3
private const val EXTENSION_ACTION_ICON_SIZE_DP = 24

/**
 * [MenuItemProvider] for the menu item expanding to the user's extensions, or to a list of recommended ones. On the
 * home screen there is no page for extensions to act on, so the item only opens to the extensions manager.
 *
 * @param context Application scoped [Context] needed for various system interactions. Must not be tied to a screen, as
 *   this provider can outlive the menu it was built for, see [applicationScope].
 * @param browserStore [BrowserStore] used to know what the extensions show for the current page.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param addonManager [AddonManager] used to query the extensions.
 * @param viewLifecycleScope [CoroutineScope] tied to the lifetime of the menu, on which to observe the extensions
 *   details.
 * @param applicationScope [CoroutineScope] tied to the lifetime of the application, on which to query the extensions
 *   since that cannot be interrupted.
 * @param appStore [AppStore] used to know whether to open pages in a private tab.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] for opening the extensions website.
 */
class ExtensionsMenuItemProvider(
    private val context: Context,
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val addonManager: AddonManager,
    viewLifecycleScope: CoroutineScope,
    applicationScope: CoroutineScope,
    private val appStore: AppStore,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
) : MenuItemProvider {
    private val logger = Logger("ExtensionsMenuItemProvider")
    private val actionIconSize = EXTENSION_ACTION_ICON_SIZE_DP.dpToPx(context.resources.displayMetrics)

    // Whether the extensions process is disabled, which extensions are installed and what they offer for the current
    // page is known right away, so that the item can be offered as it will be shown. Only the icons of what extensions
    // offer and the extensions to recommend take a while to know, and they show only once the item is expanded.
    private val extensions =
        MutableStateFlow(
            ExtensionsStatus(
                isProcessDisabled = browserStore.state.extensionsProcessDisabled,
                hasInstalledExtensions = browserStore.state.extensions.values.any { !it.isBuiltIn },
                actions = browserStore.state.webExtensionMenuActionsWithoutIcons { target.browserSessionFrom(this) },
                pendingUpdates =
                    if (target is MenuTarget.CustomTab) {
                        // A custom tab offers no way to act on an update.
                        emptyMap()
                    } else {
                        browserStore.state.pendingExtensionUpdateNames()
                    },
            )
        )

    private val isCustomTab = target is MenuTarget.CustomTab

    override val itemsFlow: StateFlow<List<MenuItem>> =
        when (target) {
            MenuTarget.Home ->
                MutableStateFlow<List<MenuItem>>(listOf(extensions.value.toExtensionsManagerMenuItem(context)))
            MenuTarget.CustomizeMenu -> MutableStateFlow<List<MenuItem>>(listOf(DEFAULT_MENU_ITEM))
            MenuTarget.BrowserTab -> extensions.mapToMenuItem(viewLifecycleScope) { it.toMenuItem(target, context) }
            is MenuTarget.CustomTab ->
                extensions.mapToMenuItem(viewLifecycleScope) { it.toCustomTabMenuItem(target, context) }
        }

    init {
        if (target != MenuTarget.Home && target != MenuTarget.CustomizeMenu) {
            observeExtensions(viewLifecycleScope, applicationScope)
        }
    }

    private fun observeExtensions(viewLifecycleScope: CoroutineScope, applicationScope: CoroutineScope) {
        // AddonManager#getAddons will run until completion even if the coroutine is canceled, so it is deliberately
        // run on a scope outliving this menu. This provider can be kept alive by it for that long since everything it
        // holds is itself tied to the lifetime of the application.
        applicationScope.launch { resolveExtensions() }

        // What the extensions offer can change while the menu is shown so we need to continuously observe this.
        viewLifecycleScope.launch { observeExtensionActions() }
    }

    /** What each extension shows for the current page is what the user gets to interact with from this menu. */
    private suspend fun observeExtensionActions() {
        browserStore.stateFlow
            .webExtensionMenuActions(iconSize = actionIconSize) { target.browserSessionFrom(this) }
            .collect { actions -> extensions.update { it.copy(actions = actions) } }
    }

    private suspend fun resolveExtensions() {
        val addons =
            try {
                addonManager.getAddons()
            } catch (e: AddonManagerException) {
                logger.error("Failed to query extensions", e)
                // When there is nothing to recommend, there is nothing more to wait for.
                extensions.update { it.copy(hasCheckedExtensions = true) }
                return
            }

        val installed = addons.filter { it.isInstalled() }

        extensions.update {
            it.copy(
                hasCheckedExtensions = true,
                hasInstalledExtensions = installed.isNotEmpty(),
                installed = installed.filter { addon -> addon.isEnabled() },
                recommended =
                    when (installed.isEmpty()) {
                        true ->
                            addons.shuffled().take(NUMBER_OF_RECOMMENDED_ADDONS_TO_SHOW).map { addon ->
                                addon.toMenuAddon()
                            }
                        else -> emptyList()
                    },
            )
        }
    }

    private fun Addon.toMenuAddon() = MenuAddon(name = displayName(context), summary = summary(context), addon = this)

    override fun handles(event: MenuEvent) =
        when (event) {
            MenuAction.OnExtensionsMenuClicked,
            is MenuAction.InstallAddon,
            is MenuAction.Navigate.AddonDetails,
            is MenuAction.Navigate.InstalledAddonDetails,
            MenuAction.Navigate.ManageExtensions,
            is MenuAction.WebExtensionActionClicked,
            MenuAction.Navigate.DiscoverMoreExtensions -> true
            else -> false
        }

    /** Expanding the item is left to the menu. */
    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        when (event) {
            is MenuAction.InstallAddon -> installAddon(event.addon, event.addonName, menu)

            is MenuAction.Navigate.AddonDetails ->
                menu.navigate(NavGraphDirections.actionGlobalAddonDetailsFragment(addon = event.addon))

            is MenuAction.Navigate.InstalledAddonDetails ->
                menu.navigate(NavGraphDirections.actionGlobalToInstalledAddonDetailsFragment(addon = event.addon))

            MenuAction.Navigate.ManageExtensions ->
                menu.navigate(NavGraphDirections.actionGlobalAddonsManagementFragment())

            is MenuAction.WebExtensionActionClicked -> clickExtensionAction(event, menu)

            MenuAction.Navigate.DiscoverMoreExtensions -> {
                menu.dismiss()
                fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = AMO_HOMEPAGE_FOR_ANDROID,
                    newTab = true,
                    private = appStore.state.mode.isPrivate,
                )
            }

            else -> Unit
        }
    }

    private fun installAddon(addon: Addon, addonName: String?, menu: MenuHost) {
        if (addon.isInstalled()) return

        menu.dismiss()

        browserStore.dispatch(
            WebExtensionAction.UpdatePromptRequestWebExtensionAction(
                WebExtensionPromptRequest.InstallationRequested(
                    url = addon.downloadUrl,
                    name = addonName,
                    iconUrl = addon.iconUrl,
                    installationMethod = InstallationMethod.MANAGER,
                )
            )
        )
    }

    /**
     * What an extension does when the user clicks what it offers is up to it, so the menu gets out of the way first.
     * The action is resolved as late as possible - what the extension shows may have changed since the menu was built.
     */
    private fun clickExtensionAction(event: MenuAction.WebExtensionActionClicked, menu: MenuHost) {
        if (isCustomTab) return

        val extensionAction =
            browserStore.state.findWebExtensionMenuAction(
                extensionId = event.extensionId,
                isPageAction = event.isPageAction,
            ) {
                target.browserSessionFrom(this)
            } ?: return

        menu.dismiss()
        extensionAction.onClick()
    }

    private fun ExtensionsStatus.toExtensionsManagerMenuItem(context: Context) =
        DEFAULT_MENU_ITEM.copy(
            icon = icon(context),
            summary = if (isProcessDisabled) null else pendingUpdateSummary,
            onClickEvent = MenuAction.Navigate.ManageExtensions,
        )

    private fun ExtensionsStatus.toCustomTabMenuItem(menuTarget: MenuTarget, context: Context): MenuItem =
        when {
            isProcessDisabled || actions.isEmpty() ->
                DEFAULT_MENU_ITEM.copy(
                    icon = icon(context),
                    summary =
                        MenuItemSummary(
                            text =
                                Text.Resource(
                                    when (isProcessDisabled) {
                                        true -> R.string.browser_menu_extensions_disabled_description
                                        false -> R.string.browser_menu_no_extensions_installed_description
                                    }
                                ),
                            state = MenuItemState.DISABLED,
                        ),
                    onClickEvent = MenuAction.OnExtensionsMenuClicked,
                    state = MenuItemState.DISABLED,
                )

            else ->
                ExpandableMenuItem(
                    title = MenuItemTitle(Text.Resource(R.string.browser_menu_extensions)),
                    onClickEvent = MenuAction.OnExtensionsMenuClicked,
                    subMenuItems = actions.map { it.toMenuItem(menuTarget = menuTarget, ownedBy = null) },
                    icon = icon(context),
                    summary = summary,
                    actionButtonText = Text.String(actions.size.toString()),
                )
        }

    /**
     * What to show for the user's extensions, be it the extensions themselves or a way to get some.
     *
     * @param menuTarget [MenuTarget] for which this menu item would be shown for.
     * @param context Application scoped [Context] needed for resources and system interactions.
     */
    private fun ExtensionsStatus.toMenuItem(menuTarget: MenuTarget, context: Context): MenuItem {
        // With the extensions process disabled, or with the user's extensions offering nothing and nothing to recommend
        // them either, there is nothing to expand to, so a plain item leading to the extensions manager is offered
        // instead.
        if (isProcessDisabled || hasNothingToShow) {
            return DEFAULT_MENU_ITEM.copy(
                icon = icon(context),
                summary = summary,
                onClickEvent = MenuAction.Navigate.ManageExtensions,
                actionButton =
                    MenuItemActionButton(
                        icon = iconsR.drawable.mozac_ic_settings_24,
                        contentDescription = Text.Resource(R.string.browser_menu_manage_extensions),
                        onClickEvent = MenuAction.Navigate.ManageExtensions,
                        showDivider = true,
                    ),
            )
        }

        return ExpandableMenuItem(
            title = MenuItemTitle(Text.Resource(R.string.browser_menu_extensions)),
            onClickEvent = MenuAction.OnExtensionsMenuClicked,
            subMenuItems = extensionItems(menuTarget) + listOfNotNull(moreExtensionsItem()),
            icon = icon(context),
            summary = summary,
            // Only what the user's own extensions offer is counted, recommendations are not something they have.
            actionButtonText = actions.size.takeIf { it > 0 }?.let { Text.String(it.toString()) },
        )
    }

    /**
     * What the user's own extensions offer when there is anything, and otherwise the ones we recommend they install.
     *
     * @param menuTarget [MenuTarget] for which this menu item would be shown for.
     */
    private fun ExtensionsStatus.extensionItems(menuTarget: MenuTarget): List<StandardMenuItem> =
        when {
            actions.isNotEmpty() ->
                actions.map { action ->
                    action.toMenuItem(
                        menuTarget = menuTarget,
                        ownedBy = installed.firstOrNull { it.id == action.extensionId },
                        hasPendingUpdate = action.extensionId in pendingUpdates,
                    )
                }
            else -> recommended.map { it.toRecommendedItem() }
        }

    private fun ExtensionsStatus.moreExtensionsItem(): StandardMenuItem =
        when (installed.isEmpty()) {
            true ->
                StandardMenuItem(
                    title = MenuItemTitle(Text.Resource(R.string.browser_menu_discover_more_extensions)),
                    onClickEvent = MenuAction.Navigate.DiscoverMoreExtensions,
                    // Hint that this opens a website rather than staying in the menu.
                    trailingIcon = iconsR.drawable.mozac_ic_external_link_24,
                )
            else ->
                StandardMenuItem(
                    title = MenuItemTitle(Text.Resource(R.string.browser_menu_manage_extensions)),
                    onClickEvent = MenuAction.Navigate.ManageExtensions,
                )
        }

    /**
     * Maps the details of an addon's action to a menu item to show to the user.
     *
     * @param menuTarget [MenuTarget] for which this menu item would be shown for.
     * @param ownedBy The [Addon] this action belongs to, if it is one the user can configure. Extensions installed by
     *   the application itself are not listed as the user's own and so cannot be configured by them.
     * @param hasPendingUpdate Whether an update of the extension waits for review.
     */
    private fun WebExtensionMenuAction.toMenuItem(
        menuTarget: MenuTarget,
        ownedBy: Addon?,
        hasPendingUpdate: Boolean = false,
    ) =
        StandardMenuItem(
            title = MenuItemTitle(Text.String(label)),
            // Extensions the user already has installed are listed by what they offer for the current page, with a
            // summary for what the extension does being shown only for recommended extensions.
            summary =
                if (hasPendingUpdate) {
                    MenuItemSummary(
                        text = Text.Resource(R.string.browser_menu_extension_review_update_description),
                        state = MenuItemState.WARNING,
                    )
                } else {
                    null
                },
            icon = menuIcon,
            badge = badgeText?.takeUnless { it.isBlank() }?.let { MenuItemBadge(text = Text.String(it)) },
            onClickEvent =
                when (menuTarget) {
                    is MenuTarget.CustomTab -> null
                    else -> MenuAction.WebExtensionActionClicked(extensionId = extensionId, isPageAction = isPageAction)
                },
            state =
                when (enabled) {
                    true -> MenuItemState.DEFAULT
                    else -> MenuItemState.DISABLED
                },
            actionButton =
                ownedBy?.let {
                    MenuItemActionButton(
                        icon = iconsR.drawable.mozac_ic_settings_24,
                        // TODO Bug 2076265 - Need a more specific content description.
                        contentDescription = Text.Resource(R.string.browser_menu_manage_extensions),
                        onClickEvent = MenuAction.Navigate.InstalledAddonDetails(it),
                        showDivider = true,
                    )
                },
        )

    private fun MenuAddon.toRecommendedItem() =
        StandardMenuItem(
            title = MenuItemTitle(Text.String(name), maxLines = 1),
            summary = menuSummary,
            icon = addon.menuIcon,
            onClickEvent = MenuAction.Navigate.AddonDetails(addon),
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_plus_24,
                    contentDescription =
                        Text.Resource(R.string.browser_menu_extension_plus_icon_content_description_2, listOf(name)),
                    onClickEvent = MenuAction.InstallAddon(addon = addon, addonName = name),
                    showDivider = true,
                ),
        )

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_extensions)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_extension_24),
            )
    }
}

/**
 * Everything the extensions menu item needs to know about the user's extensions.
 *
 * @property isProcessDisabled Whether the extensions process was disabled after repeated crashes.
 * @property hasCheckedExtensions Whether querying the extensions finished (which tells which ones to recommend), even
 *   if it failed.
 * @property hasInstalledExtensions Whether the user installed any extensions, whether enabled or not.
 * @property actions What the user's extensions offer for the current page, to list when the item is expanded.
 * @property installed The installed and enabled extensions, each the owner of one or more of [actions].
 * @property recommended Extensions to suggest when the user has none installed.
 * @property pendingUpdates Names of the extensions with an update waiting for review, by extension id.
 */
private data class ExtensionsStatus(
    val isProcessDisabled: Boolean = false,
    val hasCheckedExtensions: Boolean = false,
    val hasInstalledExtensions: Boolean = false,
    val actions: List<WebExtensionMenuAction> = emptyList(),
    val installed: List<Addon> = emptyList(),
    val recommended: List<MenuAddon> = emptyList(),
    val pendingUpdates: Map<String, String> = emptyMap(),
)

/** Names of the extensions with an update needing attention, by extension ID. Both app menus use it. */
internal fun BrowserState.pendingExtensionUpdateNames(): Map<String, String> =
    pendingExtensionUpdatesNeedingAttention.keys.associateWith { extensionId ->
        extensions[extensionId]?.name ?: extensionId
    }

/**
 * An [Addon] with its user facing details already resolved, so that building the menu needs no `Context`.
 *
 * @property name The localized name of the extension.
 * @property summary The localized summary of the extension, when it has one.
 * @property addon The [Addon] this was built from, needed by the actions targeting it.
 */
private data class MenuAddon(
    val name: String,
    val summary: String?,
    val addon: Addon,
)

private fun StateFlow<ExtensionsStatus>.mapToMenuItem(
    scope: CoroutineScope,
    toMenuItem: (ExtensionsStatus) -> MenuItem,
): StateFlow<List<MenuItem>> = map {
    listOf(toMenuItem(it))
}
    .stateIn(
        scope = scope,
        started = SharingStarted.Eagerly,
        initialValue = listOf(toMenuItem(value)),
    )

/**
 * Whether the user's extensions offer nothing for this page and there is nothing to recommend them either.
 *
 * Extensions are only recommended to users without any, so for the others this is known before checking.
 */
private val ExtensionsStatus.hasNothingToShow: Boolean
    get() = (hasCheckedExtensions || hasInstalledExtensions) && actions.isEmpty() && recommended.isEmpty()

private fun ExtensionsStatus.icon(context: Context): MenuItemIcon =
    when (isProcessDisabled) {
        true -> {
            // Warning about the extensions process being disabled is done with a multicolored icon,
            // which only the MenuItemIconDrawable support showing without an icon tint being applied.
            AppCompatResources.getDrawable(context, iconsR.drawable.mozac_ic_extension_warning_multicolor_24)
                ?.let(::MenuItemIconDrawable)
                ?: MenuItemIconRes(iconsR.drawable.mozac_ic_extension_warning_multicolor_24)
        }
        else -> MenuItemIconRes(iconsR.drawable.mozac_ic_extension_24)
    }

/** The summary for pending updates, or null if there are none. */
private val ExtensionsStatus.pendingUpdateSummary: MenuItemSummary?
    get() =
        when {
            pendingUpdates.isEmpty() -> null
            else ->
                MenuItemSummary(
                    text =
                        Text.Resource(
                            R.string.browser_menu_extensions_review_update_description,
                            listOf(pendingUpdates.values.joinToString()),
                        ),
                    state = MenuItemState.WARNING,
                )
        }

private val ExtensionsStatus.summary: MenuItemSummary?
    get() =
        when {
            isProcessDisabled ->
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_extensions_disabled_description),
                    state = MenuItemState.DISABLED,
                )
            pendingUpdates.isNotEmpty() -> pendingUpdateSummary
            hasNothingToShow ->
                MenuItemSummary(text = Text.Resource(R.string.browser_menu_no_extensions_installed_description))
            actions.isNotEmpty() -> MenuItemSummary(text = Text.String(actions.joinToString { it.label }))
            // Only worth suggesting to users without any extensions of their own to show instead.
            !hasInstalledExtensions ->
                MenuItemSummary(text = Text.Resource(R.string.browser_menu_try_a_recommended_extension_description))
            else -> null
        }

private val MenuAddon.menuSummary: MenuItemSummary?
    get() = summary?.let { MenuItemSummary(text = Text.String(it)) }

private val WebExtensionMenuAction.menuIcon: MenuItemIcon
    get() =
        when (icon) {
            null -> MenuItemIconRes(iconsR.drawable.mozac_ic_extension_fill_24)
            else -> MenuItemIconBitmap(icon)
        }

private val Addon.menuIcon: MenuItemIcon
    get() =
        when (val icon = provideIcon()) {
            null -> MenuItemIconRes(iconsR.drawable.mozac_ic_extension_24)
            else -> MenuItemIconBitmap(icon)
        }

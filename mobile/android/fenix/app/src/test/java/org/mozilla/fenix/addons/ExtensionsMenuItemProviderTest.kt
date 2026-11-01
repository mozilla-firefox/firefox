/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
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
import mozilla.components.compose.menu.ui.MenuItemIconDrawable
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.AddonManagerException
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.settings.SupportUtils.AMO_HOMEPAGE_FOR_ANDROID

@RunWith(AndroidJUnit4::class)
class ExtensionsMenuItemProviderTest {
    private val addonManager: AddonManager = mockk { coEvery { getAddons() } returns emptyList() }
    private val appStore: AppStore = mockk { every { state } returns AppState() }
    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)
    private val menu = FakeMenuHost()

    @Test
    fun `GIVEN extensions offer something for the current page WHEN building the menu item THEN list what they offer`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true, enabled = true))

            val item = resolvedItem(browserStoreWith(extension(INSTALLED_ID))) as ExpandableMenuItem

            assertEquals(Text.Resource(R.string.browser_menu_extensions), item.title.text)
            assertEquals(MenuItemSummary(text = Text.String(ACTION_TITLE)), item.summary)
            assertEquals(Text.String("1"), item.actionButtonText)
            assertEquals(2, item.subMenuItems.size)
            assertEquals(MenuAction.Navigate.ManageExtensions, item.subMenuItems.last().onClickEvent)

            val extensionItem = item.subMenuItems.first()
            assertEquals(Text.String(ACTION_TITLE), extensionItem.title.text)
            assertEquals(MenuItemBadge(text = Text.String(BADGE_TEXT)), extensionItem.badge)
            assertEquals(
                MenuAction.WebExtensionActionClicked(extensionId = INSTALLED_ID, isPageAction = false),
                extensionItem.onClickEvent,
            )
            assertEquals(
                MenuAction.Navigate.InstalledAddonDetails(addon(INSTALLED_ID, installed = true, enabled = true)),
                extensionItem.actionButton?.onClickEvent,
            )
        }

    @Test
    fun `GIVEN no extensions are installed WHEN building the menu item THEN include recommended extensions and a way to discover more on the website`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(RECOMMENDED_ID))

            val item = resolvedItem() as ExpandableMenuItem

            // Recommendations are not extensions the user has, so they are not counted.
            assertNull(item.actionButtonText)
            assertEquals(2, item.subMenuItems.size)
            // Long names are cut off rather than making the list longer.
            assertEquals(MenuItemTitle(Text.String(RECOMMENDED_ID), maxLines = 1), item.subMenuItems.first().title)
            assertEquals(MenuAction.Navigate.DiscoverMoreExtensions, item.subMenuItems.last().onClickEvent)
            assertEquals(iconsR.drawable.mozac_ic_external_link_24, item.subMenuItems.last().trailingIcon)
        }

    @Test
    fun `GIVEN the installed extensions offer nothing for the current page WHEN building the menu item THEN still allow managing them`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true, enabled = false))

            assertEquals(nothingToShowItem, resolvedItem())
        }

    @Test
    fun `GIVEN no extensions are installed and there are none to recommend WHEN building the menu item THEN say no extensions are enabled and allow managing them`() =
        runTest {
            coEvery { addonManager.getAddons() } returns emptyList()

            assertEquals(nothingToShowItem, resolvedItem())
        }

    @Test
    fun `GIVEN the extensions process is disabled WHEN building the menu item THEN warn about it`() = runTest {
        val browserStore = browserStoreWith(extension(INSTALLED_ID), isExtensionsProcessDisabled = true)

        val item = resolvedItem(browserStore) as StandardMenuItem

        assertEquals(Text.Resource(R.string.browser_menu_extensions), item.title.text)
        assertEquals(
            MenuItemSummary(
                text = Text.Resource(R.string.browser_menu_extensions_disabled_description),
                state = MenuItemState.DISABLED,
            ),
            item.summary,
        )
        assertEquals(MenuAction.Navigate.ManageExtensions, item.onClickEvent)
        // The icon warning about this is multicolored, which only a drawable is shown as it is, without a tint.
        assertIs<MenuItemIconDrawable>(item.icon)
        // Only that icon warns about it, the item itself is not colored as a warning.
        assertEquals(MenuItemState.DEFAULT, item.state)
    }

    @Test
    fun `GIVEN no extensions are installed WHEN querying the extensions fails THEN say no extensions are enabled and allow managing them`() =
        runTest {
            coEvery { addonManager.getAddons() } throws AddonManagerException(Exception("boom"))

            assertEquals(nothingToShowItem, resolvedItem())
        }

    @Test
    fun `GIVEN extensions offer something for the page WHEN opening the menu THEN list what they offer right away`() =
        runTest {
            val item =
                provider(browserStoreWith(extension(INSTALLED_ID))).itemsFlow.value.single() as ExpandableMenuItem

            assertEquals(MenuItemSummary(text = Text.String(ACTION_TITLE)), item.summary)
            assertEquals(Text.String("1"), item.actionButtonText)
        }

    @Test
    fun `GIVEN no extensions are installed WHEN opening the menu THEN suggest trying one right away`() = runTest {
        val item = provider(browserStoreWith()).itemsFlow.value.singleOrNull()

        assertEquals(
            MenuItemSummary(text = Text.Resource(R.string.browser_menu_try_a_recommended_extension_description)),
            item?.summary,
        )
    }

    @Test
    fun `GIVEN only built-in extensions are installed WHEN opening the menu THEN suggest trying one right away`() =
        runTest {
            val builtInExtension = extension(INSTALLED_ID).copy(isBuiltIn = true, browserAction = null)

            val item = provider(browserStoreWith(builtInExtension)).itemsFlow.value.singleOrNull()

            assertEquals(
                MenuItemSummary(text = Text.Resource(R.string.browser_menu_try_a_recommended_extension_description)),
                item?.summary,
            )
        }

    @Test
    fun `GIVEN the installed extensions offer nothing WHEN opening the menu THEN say so right away`() = runTest {
        val disabledExtension = extension(INSTALLED_ID).copy(enabled = false)

        val item = provider(browserStoreWith(disabledExtension)).itemsFlow.value.singleOrNull()

        assertIs<StandardMenuItem>(item)
        assertEquals(
            MenuItemSummary(text = Text.Resource(R.string.browser_menu_no_extensions_installed_description)),
            item.summary,
        )
    }

    @Test
    fun `GIVEN extensions offer something WHEN their icons are loaded THEN keep listing the same`() = runTest {
        coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true, enabled = true))
        val browserStore = browserStoreWith(extension(INSTALLED_ID))
        val beforeLoading = provider(browserStore).itemsFlow.value.singleOrNull()

        val afterLoading = resolvedItem(browserStore)

        assertEquals(beforeLoading?.summary, afterLoading?.summary)
        assertEquals(
            (beforeLoading as ExpandableMenuItem).actionButtonText,
            (afterLoading as ExpandableMenuItem).actionButtonText,
        )
    }

    @Test
    fun `WHEN expanding the item THEN keep the menu open`() = runTest {
        provider().onEvent(MenuAction.OnExtensionsMenuClicked, menu)

        assertFalse(menu.isUsed)
    }

    @Test
    fun `WHEN asking to manage the extensions THEN show the extensions manager in place of the menu`() = runTest {
        provider().onEvent(MenuAction.Navigate.ManageExtensions, menu)

        assertEquals(NavGraphDirections.actionGlobalAddonsManagementFragment(), menu.directions)
    }

    @Test
    fun `WHEN asking for the details of an extension not installed THEN show them in place of the menu`() = runTest {
        val addon = addon(RECOMMENDED_ID)

        provider().onEvent(MenuAction.Navigate.AddonDetails(addon), menu)

        assertEquals(NavGraphDirections.actionGlobalAddonDetailsFragment(addon), menu.directions)
    }

    @Test
    fun `WHEN asking for the details of an installed extension THEN show them in place of the menu`() = runTest {
        val addon = addon(INSTALLED_ID, installed = true)

        provider().onEvent(MenuAction.Navigate.InstalledAddonDetails(addon), menu)

        assertEquals(NavGraphDirections.actionGlobalToInstalledAddonDetailsFragment(addon), menu.directions)
    }

    @Test
    fun `WHEN asking to discover more extensions THEN close the menu and open the extensions website`() = runTest {
        provider().onEvent(MenuAction.Navigate.DiscoverMoreExtensions, menu)

        assertTrue(menu.isDismissed)
        verify {
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = AMO_HOMEPAGE_FOR_ANDROID,
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `WHEN installing an extension THEN close the menu and ask for it to be installed`() = runTest {
        val addon =
            Addon(
                id = RECOMMENDED_ID,
                downloadUrl = "https://mozilla.org/addon.xpi",
                iconUrl = "https://mozilla.org/addon.png",
            )
        val browserStore = browserStoreWith()

        provider(browserStore).onEvent(MenuAction.InstallAddon(addon = addon, addonName = "test"), menu)

        assertTrue(menu.isDismissed)
        // Installing it this way is what shows the progress dialog keeping the user from asking for it again.
        assertEquals(
            WebExtensionPromptRequest.InstallationRequested(
                url = addon.downloadUrl,
                name = "test",
                iconUrl = addon.iconUrl,
                installationMethod = InstallationMethod.MANAGER,
            ),
            browserStore.state.webExtensionPromptRequest,
        )
    }

    @Test
    fun `GIVEN an extension is already installed WHEN installing it again THEN keep the menu open`() = runTest {
        val browserStore = browserStoreWith()

        provider(browserStore).onEvent(MenuAction.InstallAddon(addon = addon(INSTALLED_ID, installed = true)), menu)

        assertFalse(menu.isUsed)
        assertNull(browserStore.state.webExtensionPromptRequest)
    }

    @Test
    fun `WHEN clicking what an extension offers THEN close the menu and let the extension react to it`() = runTest {
        var wasClicked = false
        val extension = extension(INSTALLED_ID).copy(browserAction = extensionAction { wasClicked = true })

        provider(browserStoreWith(extension))
            .onEvent(
                MenuAction.WebExtensionActionClicked(extensionId = INSTALLED_ID, isPageAction = false),
                menu,
            )

        assertTrue(menu.isDismissed)
        assertTrue(wasClicked)
    }

    @Test
    fun `GIVEN an extension no longer offers anything WHEN clicking what it offered THEN keep the menu open`() =
        runTest {
            provider()
                .onEvent(
                    MenuAction.WebExtensionActionClicked(extensionId = INSTALLED_ID, isPageAction = false),
                    menu,
                )

            assertFalse(menu.isUsed)
        }

    @Test
    fun `WHEN extensions are installed or recommended THEN handle all events the item can dispatch and no others`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true))
            val withInstalled = resolvedItem(browserStoreWith(extension(INSTALLED_ID)))
            coEvery { addonManager.getAddons() } returns listOf(addon(RECOMMENDED_ID))
            val withRecommended = resolvedItem()
            val provider = provider()

            listOf(withInstalled, withRecommended).forEach { item ->
                val events = requireNotNull(item).reachableEvents()

                assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
            }
            assertFalse(provider.handles(MenuAction.Navigate.Settings))
        }

    /** The menu item once everything known about the extensions was read and applied to it. */
    @OptIn(ExperimentalCoroutinesApi::class)
    private val nothingToShowItem =
        StandardMenuItem(
            title = MenuItemTitle(Text.Resource(R.string.browser_menu_extensions)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_extension_24),
            summary = MenuItemSummary(text = Text.Resource(R.string.browser_menu_no_extensions_installed_description)),
            onClickEvent = MenuAction.Navigate.ManageExtensions,
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_settings_24,
                    contentDescription = Text.Resource(R.string.browser_menu_manage_extensions),
                    onClickEvent = MenuAction.Navigate.ManageExtensions,
                    showDivider = true,
                ),
        )

    @OptIn(ExperimentalCoroutinesApi::class)
    private fun TestScope.resolvedItem(
        browserStore: BrowserStore = browserStoreWith(),
        target: MenuTarget = MenuTarget.BrowserTab,
    ): MenuItem? {
        val provider = provider(browserStore, target)

        runCurrent()

        return provider.itemsFlow.value.singleOrNull()
    }

    @Test
    fun `GIVEN the menu was opened from custom tab WHEN building the item THEN offer extensions without manage extensions or settings button`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true, enabled = true))
            val customTabState = createCustomTab(url = "https://mozilla.org", id = TAB_ID)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        customTabs = listOf(customTabState),
                        extensions = mapOf(INSTALLED_ID to extension(INSTALLED_ID)),
                    )
                )

            val item = resolvedItem(browserStore, MenuTarget.CustomTab(TAB_ID)) as ExpandableMenuItem

            assertEquals(Text.Resource(R.string.browser_menu_extensions), item.title.text)
            assertEquals(MenuItemSummary(text = Text.String(ACTION_TITLE)), item.summary)
            assertEquals(MenuItemState.DEFAULT, item.state)
            assertEquals(1, item.subMenuItems.size)
            assertNull(item.subMenuItems.first().actionButton)
            assertEquals(MenuItemState.DEFAULT, item.subMenuItems.first().state)
        }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `GIVEN the menu was opened from custom tab WHEN clicking what an extension offers THEN keep the menu open and do not run action`() =
        runTest {
            var wasClicked = false
            val extension = extension(INSTALLED_ID).copy(browserAction = extensionAction { wasClicked = true })
            val customTabState = createCustomTab(url = "https://mozilla.org", id = TAB_ID)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        customTabs = listOf(customTabState),
                        extensions = mapOf(INSTALLED_ID to extension),
                    )
                )

            val provider = provider(browserStore, MenuTarget.CustomTab(TAB_ID))
            provider.onEvent(
                MenuAction.WebExtensionActionClicked(extensionId = INSTALLED_ID, isPageAction = false),
                menu,
            )

            assertFalse(menu.isUsed)
            assertFalse(wasClicked)
        }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `GIVEN extensions process is disabled in custom tab WHEN building the item THEN show greyed out item with disabled summary and no action button`() =
        runTest {
            val customTabState = createCustomTab(url = "https://mozilla.org", id = TAB_ID)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        customTabs = listOf(customTabState),
                        extensionsProcessDisabled = true,
                    )
                )

            val item = resolvedItem(browserStore, MenuTarget.CustomTab(TAB_ID)) as StandardMenuItem

            assertEquals(Text.Resource(R.string.browser_menu_extensions), item.title.text)
            assertEquals(
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_extensions_disabled_description),
                    state = MenuItemState.DISABLED,
                ),
                item.summary,
            )
            assertEquals(MenuItemState.DISABLED, item.state)
            assertNull(item.actionButton)
        }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `GIVEN the menu was opened from custom tab WHEN building expandable item THEN addon actions are shown without manage or discover items`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true, enabled = true))
            val customTabState = createCustomTab(url = "https://mozilla.org", id = TAB_ID)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        customTabs = listOf(customTabState),
                        extensions = mapOf(INSTALLED_ID to extension(INSTALLED_ID)),
                    )
                )

            val item = resolvedItem(browserStore, MenuTarget.CustomTab(TAB_ID)) as ExpandableMenuItem

            assertEquals(1, item.subMenuItems.size)
            assertEquals(Text.String(ACTION_TITLE), item.subMenuItems.first().title.text)
            assertTrue(
                item.subMenuItems.none {
                    it.onClickEvent == MenuAction.Navigate.ManageExtensions ||
                        it.onClickEvent == MenuAction.Navigate.DiscoverMoreExtensions
                }
            )
        }

    @Test
    fun `GIVEN the menu was opened from custom tab WHEN building the item THEN extensions' actions cannot be clicked`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true, enabled = true))
            val customTabState = createCustomTab(url = "https://mozilla.org", id = TAB_ID)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        customTabs = listOf(customTabState),
                        extensions = mapOf(INSTALLED_ID to extension(INSTALLED_ID)),
                    )
                )

            val item = resolvedItem(browserStore, MenuTarget.CustomTab(TAB_ID)) as ExpandableMenuItem

            assertNull(item.subMenuItems.single().onClickEvent)
            // The item itself can still be expanded to see them.
            assertEquals(MenuAction.OnExtensionsMenuClicked, item.onClickEvent)
        }

    @Test
    fun `GIVEN the menu was opened from custom tab and no extensions apply WHEN building the item THEN show no extensions enabled and disable item`() =
        runTest {
            val customTabState = createCustomTab(url = "https://mozilla.org", id = TAB_ID)
            val browserStore =
                BrowserStore(
                    BrowserState(
                        customTabs = listOf(customTabState),
                        extensions = emptyMap(),
                    )
                )

            val item = resolvedItem(browserStore, MenuTarget.CustomTab(TAB_ID)) as StandardMenuItem

            assertEquals(Text.Resource(R.string.browser_menu_extensions), item.title.text)
            assertEquals(
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_no_extensions_installed_description),
                    state = MenuItemState.DISABLED,
                ),
                item.summary,
            )
            assertEquals(MenuItemState.DISABLED, item.state)
        }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `GIVEN the menu was opened from home WHEN building the item THEN only offer managing extensions`() = runTest {
        val provider = provider(browserStoreWith(extension(INSTALLED_ID)), MenuTarget.Home)

        runCurrent()

        assertEquals(
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_extensions)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_extension_24),
                onClickEvent = MenuAction.Navigate.ManageExtensions,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `GIVEN the menu was opened from home WHEN building the item THEN don't query the extensions`() = runTest {
        provider(browserStoreWith(extension(INSTALLED_ID)), MenuTarget.Home)

        runCurrent()

        coVerify(exactly = 0) { addonManager.getAddons() }
    }

    @Test
    fun `GIVEN the extensions process is disabled WHEN building the item for home THEN warn about it`() = runTest {
        val browserStore = browserStoreWith(extension(INSTALLED_ID), isExtensionsProcessDisabled = true)

        val item = provider(browserStore, MenuTarget.Home).itemsFlow.value.single() as StandardMenuItem

        assertIs<MenuItemIconDrawable>(item.icon)
        assertNull(item.summary)
        assertEquals(MenuAction.Navigate.ManageExtensions, item.onClickEvent)
    }

    @Test
    fun `GIVEN the menu was opened from home WHEN clicking the item THEN show the extensions manager`() = runTest {
        val provider = provider(target = MenuTarget.Home)

        provider.onEvent(MenuAction.Navigate.ManageExtensions, menu)

        assertEquals(NavGraphDirections.actionGlobalAddonsManagementFragment(), menu.directions)
    }

    @Test
    fun `GIVEN an extension update waits for the user WHEN building the menu item THEN point it out in warning colors`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true, enabled = true))
            val browserStore =
                browserStoreWith(extension(INSTALLED_ID, INSTALLED_NAME), pendingUpdateIds = listOf(INSTALLED_ID))

            val item = resolvedItem(browserStore) as ExpandableMenuItem

            assertEquals(pendingUpdateSummary, item.summary)
            assertEquals(MenuAction.OnExtensionsMenuClicked, item.onClickEvent)
            assertEquals(
                MenuItemSummary(
                    text = Text.Resource(R.string.browser_menu_extension_review_update_description),
                    state = MenuItemState.WARNING,
                ),
                item.subMenuItems.first().summary,
            )
        }

    @Test
    fun `GIVEN extension updates wait for the user WHEN building the item for home THEN name them and still open the extensions manager`() =
        runTest {
            val browserStore =
                browserStoreWith(
                    extension(INSTALLED_ID, INSTALLED_NAME),
                    extension(RECOMMENDED_ID, "Dark Reader"),
                    pendingUpdateIds = listOf(INSTALLED_ID, RECOMMENDED_ID),
                )

            val item = provider(browserStore, MenuTarget.Home).itemsFlow.value.single() as StandardMenuItem

            assertEquals(
                MenuItemSummary(
                    text =
                        Text.Resource(
                            R.string.browser_menu_extensions_review_update_description,
                            listOf("$INSTALLED_NAME, Dark Reader"),
                        ),
                    state = MenuItemState.WARNING,
                ),
                item.summary,
            )
            assertEquals(MenuAction.Navigate.ManageExtensions, item.onClickEvent)
        }

    @OptIn(ExperimentalCoroutinesApi::class)
    @Test
    fun `GIVEN an extension update waits for the user WHEN building the item for a custom tab THEN do not point it out`() =
        runTest {
            coEvery { addonManager.getAddons() } returns listOf(addon(INSTALLED_ID, installed = true, enabled = true))
            val browserStore =
                BrowserStore(
                    BrowserState(
                        customTabs = listOf(createCustomTab(url = "https://mozilla.org", id = TAB_ID)),
                        extensions = mapOf(INSTALLED_ID to extension(INSTALLED_ID)),
                        pendingExtensionUpdates = mapOf(INSTALLED_ID to pendingExtensionUpdate(INSTALLED_ID)),
                    )
                )

            val item = resolvedItem(browserStore, MenuTarget.CustomTab(TAB_ID)) as ExpandableMenuItem

            assertEquals(MenuItemSummary(text = Text.String(ACTION_TITLE)), item.summary)
            assertNull(item.subMenuItems.first().summary)
        }

    private val pendingUpdateSummary =
        MenuItemSummary(
            text = Text.Resource(R.string.browser_menu_extensions_review_update_description, listOf(INSTALLED_NAME)),
            state = MenuItemState.WARNING,
        )

    private fun pendingExtensionUpdate(extensionId: String) =
        PendingExtensionUpdate(
            extensionId = extensionId,
            permissions = listOf("privacy"),
            origins = emptyList(),
            dataCollectionPermissions = emptyList(),
            onPermissionsGranted = {},
        )

    private fun TestScope.provider(
        browserStore: BrowserStore = browserStoreWith(),
        target: MenuTarget = MenuTarget.BrowserTab,
    ) =
        ExtensionsMenuItemProvider(
            context = testContext,
            browserStore = browserStore,
            target = target,
            addonManager = addonManager,
            // Both are scopes that runTest runs and then cancels at the end of each test.
            viewLifecycleScope = backgroundScope,
            applicationScope = backgroundScope,
            appStore = appStore,
            fenixBrowserUseCases = fenixBrowserUseCases,
        )

    private fun browserStoreWith(
        vararg extensions: WebExtensionState,
        isExtensionsProcessDisabled: Boolean = false,
        pendingUpdateIds: List<String> = emptyList(),
    ) =
        BrowserStore(
            BrowserState(
                tabs = listOf(createTab(url = "https://mozilla.org", id = TAB_ID)),
                selectedTabId = TAB_ID,
                extensions = extensions.associateBy { it.id },
                extensionsProcessDisabled = isExtensionsProcessDisabled,
                pendingExtensionUpdates = pendingUpdateIds.associateWith { pendingExtensionUpdate(it) },
            )
        )

    private fun extension(id: String, name: String = id) =
        WebExtensionState(
            id = id,
            url = "url",
            name = name,
            enabled = true,
            browserAction =
                Action(
                    title = ACTION_TITLE,
                    enabled = true,
                    loadIcon = null,
                    badgeText = BADGE_TEXT,
                    badgeTextColor = null,
                    badgeBackgroundColor = null,
                    onClick = {},
                ),
        )

    private fun extensionAction(onClick: () -> Unit) =
        Action(
            title = ACTION_TITLE,
            enabled = true,
            loadIcon = null,
            badgeText = null,
            badgeTextColor = null,
            badgeBackgroundColor = null,
            onClick = onClick,
        )

    private fun addon(
        id: String,
        installed: Boolean = false,
        enabled: Boolean = true,
    ) =
        Addon(
            id = id,
            installedState =
                when (installed) {
                    true -> Addon.InstalledState(id = id, version = "1.0", optionsPageUrl = null, enabled = enabled)
                    else -> null
                },
        )

    private companion object {
        const val INSTALLED_ID = "installed"
        const val INSTALLED_NAME = "uBlock Origin"
        const val RECOMMENDED_ID = "recommended"
        const val TAB_ID = "tab1"
        const val ACTION_TITLE = "uBlock Origin (0)"
        const val BADGE_TEXT = "3"
    }
}

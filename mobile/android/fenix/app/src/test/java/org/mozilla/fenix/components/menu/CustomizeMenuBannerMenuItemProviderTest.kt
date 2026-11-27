/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.ui.MenuItemIconRes
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.utils.Settings

class CustomizeMenuBannerMenuItemProviderTest {
    private val settings: Settings = mockk(relaxed = true)
    private val menu = FakeMenuHost()
    private var viewCount = 0
    private var isBannerEnabled = true
    private var shownEvents = 0
    private val decisions = mutableListOf<Boolean>()
    private val defaultMenuInfo =
        BrowserMenuConfiguration.defaultMenuInfo(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)
    private val customizedMenuInfo =
        CustomizedMenuInfo(
            visibleSectionIds = listOf(TOOLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
            hiddenSectionIds = emptyList(),
        )
    private val customizedToolsItems =
        BrowserMenuConfiguration.defaultSectionsInfo().mapValues { (_, info) ->
            info.copy(items = info.items.reversed())
        }

    @Before
    fun setUp() {
        every { settings.isMenuCustomizationScreenEnabled } returns true
        every { settings.shouldShowMenuBanner } returns false
        every { settings.isDefaultBrowser } returns false
        every { settings.shouldShowCustomizeMenuBanner } answers { isBannerEnabled }
        every { settings.shouldShowCustomizeMenuBanner = any() } answers { isBannerEnabled = firstArg() }
        every { settings.shouldUseExpandedToolbar } returns false
        every { settings.customizeMenuBannerViewCount } answers { viewCount }
        every { settings.customizeMenuBannerViewCount = any() } answers { viewCount = firstArg() }
    }

    @Test
    fun `WHEN providing the item THEN offer the promo banner`() {
        val provider = createProvider()

        assertEquals(
            BannerMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_customize_banner_title)),
                summary =
                    MenuItemSummary(
                        text = Text.Resource(R.string.browser_menu_customize_banner_subtitle),
                        maxLines = 3,
                    ),
                icon = MenuItemIconRes(R.drawable.customize_menu_banner_illustration),
                onClickEvent = MenuAction.CustomizeMenuBannerClicked,
                actionButtonText = Text.Resource(R.string.browser_menu_customize_banner_button),
                onDismissEvent = MenuAction.CustomizeMenuBannerDismissed,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `WHEN clicking the item THEN navigate to customize menu`() {
        val provider = createProvider()

        provider.onEvent(MenuAction.CustomizeMenuBannerClicked, menu)

        assertEquals(MenuFragmentDirections.actionMenuFragmentToCustomizeMenuFragment(), menu.directions)
        assertIs<BannerMenuItem>(provider.itemsFlow.value.single())
    }

    @Test
    fun `WHEN dismissing the item THEN hide it for good and keep the menu open`() {
        val provider = createProvider()

        provider.onEvent(MenuAction.CustomizeMenuBannerDismissed, menu)

        assertFalse(isBannerEnabled)
        assertTrue(provider.itemsFlow.value.isEmpty())
        assertFalse(menu.isUsed)
    }

    @Test
    fun `WHEN building the item THEN handle all events it can dispatch and no others`() {
        val provider = createProvider()

        val events = provider.itemsFlow.value.single().reachableEvents()

        assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
        assertFalse(provider.handles(MenuAction.Navigate.Settings))
    }

    @Test
    fun `GIVEN a menu other than the one of a browser tab WHEN providing the item THEN offer nothing nor count a view`() {
        listOf(MenuTarget.Preview, MenuTarget.Home, MenuTarget.CustomTab("id")).forEach { target ->
            assertFalse(createProvider(target = target).isOffered)
        }
        assertEquals(0, viewCount)
        assertEquals(0, shownEvents)
        assertTrue(decisions.isEmpty())
    }

    @Test
    fun `GIVEN no browser menu is being built WHEN providing the item THEN offer nothing nor count a view`() {
        val provider =
            CustomizeMenuBannerMenuItemProvider(
                settings = settings,
                target = MenuTarget.BrowserTab,
                isToolbarAtBottom = false,
                session = null,
                isBannerFeatureEnabled = true,
            )

        assertFalse(provider.isOffered)
        assertEquals(0, viewCount)
    }

    @Test
    fun `GIVEN an eligible banner WHEN opening a menu THEN offer it, count a view and report it shown`() {
        assertTrue(open(defaultMenuInfo))

        assertEquals(1, viewCount)
        assertEquals(1, shownEvents)
    }

    @Test
    fun `WHEN deciding whether menus show the banner THEN report each decision to be saved for a restore`() {
        val wasShown = open(defaultMenuInfo)
        restore(wasShown)
        isBannerEnabled = false
        restore(wasShown)

        assertEquals(listOf(true, true, false), decisions)
    }

    @Test
    fun `GIVEN an eligible banner WHEN opening menus THEN count the first 3 views and hide it on the 4th`() {
        repeat(MAX_CUSTOMIZE_MENU_BANNER_VIEWS) { assertTrue(open(defaultMenuInfo)) }

        assertFalse(open(defaultMenuInfo))
        assertEquals(MAX_CUSTOMIZE_MENU_BANNER_VIEWS, viewCount)
        assertEquals(MAX_CUSTOMIZE_MENU_BANNER_VIEWS, shownEvents)
    }

    @Test
    fun `GIVEN a menu never customized WHEN opening it THEN offer the banner`() {
        assertTrue(open(customizationInfo = null))
    }

    @Test
    fun `GIVEN a customized menu WHEN opening it THEN don't offer the banner and turn it off for good`() {
        assertFalse(open(customizedMenuInfo))

        assertFalse(isBannerEnabled)
        assertEquals(0, viewCount)
        assertEquals(0, shownEvents)
    }

    @Test
    fun `GIVEN only the tools items were customized WHEN opening the menu THEN don't offer the banner and turn it off for good`() {
        listOf(null, defaultMenuInfo).forEach { customizationInfo ->
            isBannerEnabled = true

            assertFalse(open(customizationInfo, customizedToolsItems))

            assertFalse(isBannerEnabled)
        }
        assertEquals(0, viewCount)
    }

    @Test
    fun `GIVEN the banner was dismissed WHEN opening a customized menu THEN don't write the setting again`() {
        isBannerEnabled = false

        assertFalse(open(customizedMenuInfo, customizedToolsItems))

        verify(exactly = 0) { settings.shouldShowCustomizeMenuBanner = any() }
    }

    @Test
    fun `GIVEN the banner used all its views WHEN opening a customized menu THEN don't turn it off`() {
        viewCount = MAX_CUSTOMIZE_MENU_BANNER_VIEWS

        assertFalse(open(customizedMenuInfo, customizedToolsItems))

        assertTrue(isBannerEnabled)
        verify(exactly = 0) { settings.shouldShowCustomizeMenuBanner = any() }
    }

    @Test
    fun `GIVEN the tools items are the default ones WHEN opening the menu THEN offer the banner`() {
        assertTrue(open(defaultMenuInfo, BrowserMenuConfiguration.defaultSectionsInfo()))
    }

    @Test
    fun `GIVEN the banner was dismissed WHEN opening a menu THEN don't offer it`() {
        isBannerEnabled = false

        assertFalse(open(defaultMenuInfo))
        assertEquals(0, viewCount)
    }

    @Test
    fun `GIVEN the default browser banner was not dismissed WHEN opening a menu THEN don't offer the banner nor count a view`() {
        every { settings.shouldShowMenuBanner } returns true

        assertFalse(open(defaultMenuInfo))
        assertEquals(0, viewCount)
        assertEquals(0, shownEvents)
    }

    @Test
    fun `GIVEN the default browser banner was not dismissed AND this is the default browser WHEN opening a menu THEN offer the banner`() {
        every { settings.shouldShowMenuBanner } returns true
        every { settings.isDefaultBrowser } returns true

        assertTrue(open(defaultMenuInfo))
        assertEquals(1, viewCount)
    }

    @Test
    fun `GIVEN the default browser banner was dismissed after a menu without the banner WHEN opening a menu THEN offer it`() {
        every { settings.shouldShowMenuBanner } returns true
        assertFalse(open(defaultMenuInfo))

        every { settings.shouldShowMenuBanner } returns false

        assertTrue(open(defaultMenuInfo))
        assertEquals(1, viewCount)
    }

    @Test
    fun `GIVEN the banner feature flag is off WHEN opening a menu THEN don't offer the banner nor count a view`() {
        assertFalse(open(defaultMenuInfo, isBannerFeatureEnabled = false))
        assertEquals(0, viewCount)
        assertEquals(0, shownEvents)
    }

    @Test
    fun `GIVEN the banner feature flag is off WHEN opening a customized menu THEN keep the banner for when the flag is on`() {
        assertFalse(open(customizedMenuInfo, customizedToolsItems, isBannerFeatureEnabled = false))
        assertTrue(isBannerEnabled)
    }

    @Test
    fun `GIVEN menu customization is disabled WHEN opening a menu THEN don't offer the banner`() {
        every { settings.isMenuCustomizationScreenEnabled } returns false

        assertFalse(open(defaultMenuInfo))
        assertEquals(0, viewCount)
    }

    @Test
    fun `GIVEN a menu showing the banner WHEN it is restored several times THEN keep offering it without counting a view nor reporting it shown again`() {
        var wasShown = open(defaultMenuInfo)

        repeat(2) {
            wasShown = restore(wasShown)
            assertTrue(wasShown)
        }
        assertEquals(1, viewCount)
        assertEquals(1, shownEvents)
    }

    @Test
    fun `GIVEN the banner was shown on the last view WHEN the menu is restored THEN keep offering it`() {
        viewCount = MAX_CUSTOMIZE_MENU_BANNER_VIEWS - 1
        val wasShown = open(defaultMenuInfo)

        assertTrue(restore(wasShown))
        assertEquals(MAX_CUSTOMIZE_MENU_BANNER_VIEWS, viewCount)
    }

    @Test
    fun `GIVEN a menu not showing the banner WHEN it is restored while the banner became eligible THEN still don't offer it`() {
        every { settings.shouldShowMenuBanner } returns true
        val wasShown = open(defaultMenuInfo)
        every { settings.shouldShowMenuBanner } returns false

        assertFalse(restore(wasShown))
        assertEquals(0, viewCount)
        assertEquals(0, shownEvents)
    }

    @Test
    fun `GIVEN a menu showing the banner WHEN it is restored after this stopped being the default browser THEN don't offer it nor count a view`() {
        every { settings.shouldShowMenuBanner } returns true
        every { settings.isDefaultBrowser } returns true
        val wasShown = open(defaultMenuInfo)
        every { settings.isDefaultBrowser } returns false

        assertFalse(restore(wasShown))
        assertEquals(1, viewCount)
        assertEquals(1, shownEvents)
        assertEquals(listOf(true, false), decisions)
    }

    @Test
    fun `GIVEN the banner was dismissed WHEN the menu is restored THEN don't offer it again`() {
        val provider = createProvider()
        provider.onEvent(MenuAction.CustomizeMenuBannerDismissed, menu)

        assertFalse(restore(wasShown = decisions.single()))
        assertEquals(1, viewCount)
        assertEquals(1, shownEvents)
    }

    /** Opens a new browser tab menu, returning whether it offers the banner. */
    private fun open(
        customizationInfo: CustomizedMenuInfo?,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo> = emptyMap(),
        isBannerFeatureEnabled: Boolean = true,
    ) =
        createProvider(
                customizationInfo = customizationInfo,
                customizedSectionsInfo = customizedSectionsInfo,
                isBannerFeatureEnabled = isBannerFeatureEnabled,
            )
            .isOffered

    /** Restores a browser tab menu which offered the banner if [wasShown], returning whether it offers the banner. */
    private fun restore(wasShown: Boolean) = createProvider(isRestored = true, wasShown = wasShown).isOffered

    private fun createProvider(
        target: MenuTarget = MenuTarget.BrowserTab,
        customizationInfo: CustomizedMenuInfo? = defaultMenuInfo,
        customizedSectionsInfo: Map<String, CustomizedSectionInfo> = emptyMap(),
        isRestored: Boolean = false,
        wasShown: Boolean = false,
        isBannerFeatureEnabled: Boolean = true,
    ) =
        CustomizeMenuBannerMenuItemProvider(
            settings = settings,
            target = target,
            isToolbarAtBottom = false,
            session =
                CustomizeMenuBannerSession(
                    customizationInfo = customizationInfo,
                    customizedSectionsInfo = customizedSectionsInfo,
                    isRestored = isRestored,
                    wasShown = wasShown,
                    onViewCounted = { shownEvents++ },
                    onShownDecided = { decisions += it },
                ),
            isBannerFeatureEnabled = isBannerFeatureEnabled,
        )

    private val CustomizeMenuBannerMenuItemProvider.isOffered
        get() = itemsFlow.value.isNotEmpty()
}

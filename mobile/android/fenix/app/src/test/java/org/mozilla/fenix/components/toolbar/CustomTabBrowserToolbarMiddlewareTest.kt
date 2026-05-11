/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import android.graphics.Bitmap
import android.net.InetAddresses
import android.util.Patterns
import androidx.appcompat.content.res.AppCompatResources
import androidx.core.graphics.drawable.toBitmap
import androidx.core.graphics.drawable.toDrawable
import androidx.lifecycle.Lifecycle
import androidx.navigation.NavController
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.browser.state.action.ContentAction.UpdateProgressAction
import mozilla.components.browser.state.action.ContentAction.UpdateSecurityInfoAction
import mozilla.components.browser.state.action.ContentAction.UpdateTitleAction
import mozilla.components.browser.state.action.ContentAction.UpdateUrlAction
import mozilla.components.browser.state.action.TrackingProtectionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.state.TrackingProtectionState
import mozilla.components.browser.state.state.createCustomTab
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.AnimatedPillActionRes
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.ContextualMenuOption
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.PageOriginContextualMenuInteractions.CopyToClipboardClicked
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.concept.engine.cookiehandling.CookieBannersStorage
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.feature.ipprotection.Authorized
import mozilla.components.feature.ipprotection.IPProtectionAction
import mozilla.components.feature.ipprotection.IPProtectionState
import mozilla.components.feature.ipprotection.IPProtectionStore
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.feature.tabs.CustomTabsUseCases
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.support.ktx.kotlin.getRegistrableDomainIndexRange
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.ClipboardHandler
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.Toolbar
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.URLCopiedToClipboard
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.DisplayActions.MenuClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.EndPageActions.CustomButtonClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.StartBrowserActions.CloseClicked
import org.mozilla.fenix.components.toolbar.CustomTabBrowserToolbarMiddleware.Companion.StartPageActions.SiteInfoClicked
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.mozilla.fenix.telemetry.ACTION_SECURITY_INDICATOR_CLICKED
import org.mozilla.fenix.telemetry.SOURCE_CUSTOM_BAR
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.Implementation
import org.robolectric.annotation.Implements
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.feature.customtabs.R as customtabsR
import mozilla.components.ui.icons.R as iconsR

@RunWith(RobolectricTestRunner::class)
@Config(shadows = [ShadowInetAddresses::class])
class CustomTabBrowserToolbarMiddlewareTest {
    private val testDispatcher = StandardTestDispatcher()
    private val testScope = TestScope(testDispatcher)

    @get:Rule
    val gleanRule = FenixGleanTestRule(testContext)

    private val customTabId = "test"
    private val customTab: CustomTabSessionState = mockk(relaxed = true) {
        every { id } returns customTabId
    }
    private val selectedTab = createTab("test.com")
    private val browserStore = BrowserStore(
        BrowserState(
            tabs = listOf(selectedTab),
            customTabs = listOf(customTab),
            selectedTabId = selectedTab.id,
        ),
    )
    private val appStore: AppStore = mockk()
    private val ipProtectionStore = IPProtectionStore()
    private val permissionsStorage: SitePermissionsStorage = mockk()
    private val cookieBannersStorage: CookieBannersStorage = mockk()
    private val useCases: CustomTabsUseCases = mockk()
    private val trackingProtectionUseCases: TrackingProtectionUseCases = mockk()
    private val publicSuffixList: PublicSuffixList = mockk {
        every { getPublicSuffixPlusOne(any()) } returns CompletableDeferred(null)
    }
    private val clipboard: ClipboardHandler = mockk()
    private val lifecycleOwner = TestLifecycleOwner(Lifecycle.State.RESUMED)
    private val navController: NavController = mockk()
    private val closeTabDelegate: () -> Unit = {}
    private val settings: Settings = mockk {
        every { shouldUseBottomToolbar } returns true
    }

    @Test
    fun `GIVEN the custom tab is configured to show a close button WHEN initializing the toolbar THEN add a close button`() {
        every { customTab.config.showCloseButton } returns true
        every { customTab.config.closeButtonIcon } returns null
        val expectedCloseButton = ActionButton(
            drawable = AppCompatResources.getDrawable(testContext, iconsR.drawable.mozac_ic_cross_24),
            contentDescription = testContext.getString(customtabsR.string.mozac_feature_customtabs_exit_button),
            onClick = CloseClicked,
        )

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsStart
        assertEquals(1, toolbarBrowserActions.size)
        val closeButton = toolbarBrowserActions[0] as ActionButton
        assertNotNull(closeButton.drawable)
        assertEquals(expectedCloseButton.drawable?.state, closeButton.drawable?.state)
    }

    @Test
    fun `GIVEN the custom tab is configured to show a custom close button WHEN initializing the toolbar THEN add a close button with a custom icon`() {
        every { customTab.config.showCloseButton } returns true
        val closeButtonIcon: Bitmap = testContext.getDrawable(iconsR.drawable.mozac_ic_back_24)!!.toBitmap(10, 10)
        every { customTab.config.closeButtonIcon } returns closeButtonIcon
        val expectedCloseButton = ActionButton(
            drawable = closeButtonIcon.toDrawable(testContext.resources),
            contentDescription = testContext.getString(customtabsR.string.mozac_feature_customtabs_exit_button),
            onClick = CloseClicked,
        )

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsStart
        assertEquals(1, toolbarBrowserActions.size)
        val closeButton = toolbarBrowserActions[0] as ActionButton
        assertEquals(expectedCloseButton.contentDescription, closeButton.contentDescription)
        assertEquals(expectedCloseButton.shouldTint, closeButton.shouldTint)
        assertNotNull(closeButton.drawable)
        assertEquals(expectedCloseButton.drawable?.state, closeButton.drawable?.state)
        assertEquals(expectedCloseButton.onClick, closeButton.onClick)
    }

    @Test
    fun `GIVEN the custom tab is not configured to show a close button WHEN initializing the toolbar THEN don't add a close button`() {
        every { customTab.config.showCloseButton } returns false

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsStart
        assertTrue(toolbarBrowserActions.isEmpty())
    }

    @Test
    fun `GIVEN the custom tab is configured to show a custom button WHEN initializing the toolbar THEN add a custom button with a custom icon`() {
        val customButtonIcon: Bitmap = testContext.getDrawable(iconsR.drawable.mozac_ic_logo_firefox_24)!!.toBitmap(10, 10)
        every { customTab.config.actionButtonConfig?.icon } returns customButtonIcon
        every { customTab.config.actionButtonConfig?.description } returns "test"
        val expectedCustomButton = ActionButton(
            drawable = customButtonIcon.toDrawable(testContext.resources),
            shouldTint = false,
            contentDescription = "test",
            onClick = CustomButtonClicked,
        )

        val toolbarStore = buildStore()

        val pageEndActions = toolbarStore.state.displayState.pageActionsEnd
        assertEquals(1, pageEndActions.size)
        val customButton = pageEndActions[0] as ActionButton
        assertEquals(expectedCustomButton.contentDescription, customButton.contentDescription)
        assertFalse(customButton.shouldTint)
        assertNotNull(customButton.drawable)
        assertEquals(expectedCustomButton.drawable?.state, customButton.drawable?.state)
        assertEquals(expectedCustomButton.onClick, customButton.onClick)
    }

    @Test
    fun `GIVEN a private custom tab is configured to show a custom button WHEN initializing the toolbar THEN add a custom button with a custom icon`() {
        val customButtonIcon: Bitmap = testContext.getDrawable(iconsR.drawable.mozac_ic_logo_firefox_24)!!.toBitmap(10, 10)
        every { customTab.config.actionButtonConfig?.icon } returns customButtonIcon
        every { customTab.config.actionButtonConfig?.description } returns "test"
        every { customTab.content.private } returns true
        val expectedCustomButton = ActionButton(
            drawable = customButtonIcon.toDrawable(testContext.resources),
            shouldTint = false,
            contentDescription = "test",
            onClick = CustomButtonClicked,
        )

        val toolbarStore = buildStore()

        val pageEndActions = toolbarStore.state.displayState.pageActionsEnd
        assertEquals(1, pageEndActions.size)
        val customButton = pageEndActions[0] as ActionButton
        assertEquals(expectedCustomButton.contentDescription, customButton.contentDescription)
        assertTrue(customButton.shouldTint)
        assertNotNull(customButton.drawable)
        assertEquals(expectedCustomButton.drawable?.state, customButton.drawable?.state)
        assertEquals(expectedCustomButton.onClick, customButton.onClick)
    }

    @Test
    fun `GIVEN a normal custom tab is configured to show a tinted custom button WHEN initializing the toolbar THEN add a custom button with a custom icon`() {
        val customButtonIcon: Bitmap = testContext.getDrawable(iconsR.drawable.mozac_ic_logo_firefox_24)!!.toBitmap(10, 10)
        every { customTab.config.actionButtonConfig?.icon } returns customButtonIcon
        every { customTab.config.actionButtonConfig?.description } returns "test"
        every { customTab.config.actionButtonConfig?.tint } returns true
        every { customTab.content.private } returns false
        val expectedCustomButton = ActionButton(
            drawable = customButtonIcon.toDrawable(testContext.resources),
            shouldTint = false,
            contentDescription = "test",
            onClick = CustomButtonClicked,
        )

        val toolbarStore = buildStore()

        val pageEndActions = toolbarStore.state.displayState.pageActionsEnd
        assertEquals(1, pageEndActions.size)
        val customButton = pageEndActions[0] as ActionButton
        assertEquals(expectedCustomButton.contentDescription, customButton.contentDescription)
        assertTrue(customButton.shouldTint)
        assertNotNull(customButton.drawable)
        assertEquals(expectedCustomButton.drawable?.state, customButton.drawable?.state)
        assertEquals(expectedCustomButton.onClick, customButton.onClick)
    }

    @Test
    fun `GIVEN the url if of a local file WHEN initializing the toolbar THEN add an appropriate security indicator`() {
        every { customTab.content.url } returns "content://test"
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_page_portrait_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )

        val toolbarStore = buildStore()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecurityIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the website's security is unknown WHEN initializing the toolbar THEN add an appropriate security indicator`() {
        every { customTab.content.securityInfo } returns SecurityInfo.Unknown
        every { customTab.trackingProtection.enabled } returns true
        every { customTab.trackingProtection.ignoredOnTrackingProtection } returns false
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_globe_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            state = ActionButton.State.DEFAULT,
            highlighted = false,
            onClick = object : BrowserToolbarEvent {},
        )

        val toolbarStore = buildStore()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0] as ActionButtonRes
        assertEquals(expectedSecurityIndicator.drawableResId, securityIndicator.drawableResId)
        assertEquals(expectedSecurityIndicator.contentDescription, securityIndicator.contentDescription)
        assertEquals(expectedSecurityIndicator.state, securityIndicator.state)
        assertEquals(expectedSecurityIndicator.highlighted, securityIndicator.highlighted)
        assertFalse(securityIndicator.onClick is StartPageActions.SiteInfoClicked)
        assertNull(securityIndicator.onLongClick)
    }

    @Test
    fun `GIVEN the website is secure WHEN initializing the toolbar THEN add an appropriate security indicator`() {
        every { customTab.content.securityInfo } returns SecurityInfo.Secure()
        every { customTab.trackingProtection.enabled } returns true
        every { customTab.trackingProtection.ignoredOnTrackingProtection } returns false
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )

        val toolbarStore = buildStore()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecurityIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the website is insecure WHEN initializing the toolbar THEN add an appropriate security indicator`() {
        every { customTab.content.securityInfo } returns SecurityInfo.Insecure()
        val expectedSecurityIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )

        val toolbarStore = buildStore()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecurityIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the website is insecure WHEN the conection becomes secure THEN update appropriate security indicator`() = runTest {
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = false,
            ),
            securityInfo = SecurityInfo.Insecure(),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedSecureIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val expectedInsecureIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()
        var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        var securityIndicator = toolbarPageActions[0]
        assertEquals(expectedInsecureIndicator, securityIndicator)

        browserStore.dispatch(UpdateSecurityInfoAction(customTabId, SecurityInfo.Secure()))
        testDispatcher.scheduler.advanceUntilIdle()
        toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecureIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the custom tab has tracking protection disabled THEN show appropriate security indicator`() = runTest {
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            trackingProtection = TrackingProtectionState(
                enabled = false,
                ignoredOnTrackingProtection = false,
            ),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedInsecureIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val toolbarStore = buildStore(middleware)
        browserStore.dispatch(UpdateSecurityInfoAction(customTabId, SecurityInfo.Secure()))
        testDispatcher.scheduler.advanceUntilIdle()
        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val securityIndicator = toolbarPageActions[0]
        assertEquals(expectedInsecureIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the custom tab has tracking protection disabled WHEN tracking protection is enabled THEN show appropriate security indicator`() = runTest {
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            trackingProtection = TrackingProtectionState(
                enabled = false,
                ignoredOnTrackingProtection = false,
            ),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedInsecureIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val expectedSecureIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val toolbarStore = buildStore(middleware)
        browserStore.dispatch(UpdateSecurityInfoAction(customTabId, SecurityInfo.Secure()))
        testDispatcher.scheduler.advanceUntilIdle()
        var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        var securityIndicator = toolbarPageActions[0]
        assertEquals(expectedInsecureIndicator, securityIndicator)
        browserStore.dispatch(TrackingProtectionAction.ToggleAction(tabId = customTabId, enabled = true))

        testDispatcher.scheduler.advanceUntilIdle()
        toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecureIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN the custom tab has tracking protection enabled WHEN tracking protection is disabled THEN show appropriate security indicator`() = runTest {
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = false,
            ),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedInsecureIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_slash_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val expectedSecureIndicator = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
            contentDescription = toolbarR.string.mozac_browser_toolbar_content_description_site_info,
            onClick = SiteInfoClicked,
        )
        val toolbarStore = buildStore(middleware)
        browserStore.dispatch(UpdateSecurityInfoAction(customTabId, SecurityInfo.Secure()))
        testDispatcher.scheduler.advanceUntilIdle()
        var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        var securityIndicator = toolbarPageActions[0]
        assertEquals(expectedSecureIndicator, securityIndicator)
        browserStore.dispatch(TrackingProtectionAction.ToggleAction(tabId = customTabId, enabled = false))
        testDispatcher.scheduler.advanceUntilIdle()
        toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        securityIndicator = toolbarPageActions[0]
        assertEquals(expectedInsecureIndicator, securityIndicator)
    }

    @Test
    fun `GIVEN ip protection is active WHEN initializing the toolbar THEN show animated pill action`() = runTest {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = false,
            ),
            securityInfo = SecurityInfo.Secure(),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_shield_checkmark_24, siteInfo.iconResId)
        assertEquals(iconsR.drawable.mozac_ic_globe_24, siteInfo.overlayResId)
        assertEquals(R.string.ip_protection_toolbar_pill_label, siteInfo.textResId)
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `GIVEN ip protection is not active WHEN it becomes active THEN update site info to animated pill`() = runTest {
        val ipProtectionStore = IPProtectionStore()
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = false,
            ),
            securityInfo = SecurityInfo.Secure(),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        assertTrue(toolbarPageActions[0] is ActionButtonRes)

        ipProtectionStore.dispatch(
            IPProtectionAction.EngineStateChanged(
                StateInfo(
                    serviceState = StateInfo.SERVICE_STATE_READY,
                    proxyState = StateInfo.PROXY_STATE_ACTIVE,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        assertTrue(toolbarPageActions[0] is AnimatedPillActionRes)
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `GIVEN ip protection is active WHEN it becomes inactive THEN update site info to regular button`() = runTest {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = false,
            ),
            securityInfo = SecurityInfo.Secure(),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        assertTrue(toolbarPageActions[0] is AnimatedPillActionRes)

        ipProtectionStore.dispatch(
            IPProtectionAction.EngineStateChanged(
                StateInfo(
                    serviceState = StateInfo.SERVICE_STATE_READY,
                    proxyState = StateInfo.PROXY_STATE_READY,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        assertTrue(toolbarPageActions[0] is ActionButtonRes)
    }

    @Test
    fun `GIVEN ip protection is active and security is unknown WHEN initializing THEN show animated pill with globe icon`() = runTest {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            securityInfo = SecurityInfo.Unknown,
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_globe_24, siteInfo.iconResId)
    }

    @Test
    fun `GIVEN ip protection is active and site is insecure WHEN initializing THEN show animated pill with shield slash icon`() = runTest {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            securityInfo = SecurityInfo.Insecure(),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        val siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_shield_slash_24, siteInfo.iconResId)
    }

    @Test
    fun `GIVEN ip protection is active and url is content WHEN initializing THEN show regular button without pill`() = runTest {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val customTab = createCustomTab(
            url = "content://test",
            id = customTabId,
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        assertTrue(toolbarPageActions[0] is ActionButtonRes)
    }

    @Test
    fun `GIVEN ip protection is active WHEN security changes from insecure to secure THEN pill remains with updated icon`() = runTest {
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )
        val customTab = createCustomTab(
            url = "URL",
            id = customTabId,
            trackingProtection = TrackingProtectionState(
                enabled = true,
                ignoredOnTrackingProtection = false,
            ),
            securityInfo = SecurityInfo.Insecure(),
        )
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(
            browserStore = browserStore,
            ipProtectionStore = ipProtectionStore,
        )
        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        var toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        var siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_shield_slash_24, siteInfo.iconResId)

        browserStore.dispatch(UpdateSecurityInfoAction(customTabId, SecurityInfo.Secure()))
        testDispatcher.scheduler.advanceUntilIdle()

        toolbarPageActions = toolbarStore.state.displayState.pageActionsStart
        assertEquals(1, toolbarPageActions.size)
        siteInfo = toolbarPageActions[0] as AnimatedPillActionRes
        assertEquals(iconsR.drawable.mozac_ic_shield_checkmark_24, siteInfo.iconResId)
    }

    @Test
    fun `GIVEN current custom tab WHEN the security indicator button is clicked THEN record telemetry`() {
        val trackingProtectionUseCases: TrackingProtectionUseCases = mockk(relaxed = true)
        val navController: NavController = mockk(relaxed = true)
        val middleware = buildMiddleware(
            trackingProtectionUseCases = trackingProtectionUseCases,
            navController = navController,
        )
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(SiteInfoClicked)
        testDispatcher.scheduler.advanceUntilIdle()

        val telemetry = Toolbar.buttonTapped.testGetValue()?.get(0)
        assertNotNull(telemetry)
        assertEquals("toolbar", telemetry?.category)
        assertEquals("button_tapped", telemetry?.name)
        assertEquals(SOURCE_CUSTOM_BAR, telemetry?.extra?.get("source"))
        assertEquals(ACTION_SECURITY_INDICATOR_CLICKED, telemetry?.extra?.get("item"))
    }

    @Test
    fun `GIVEN unknown custom tab WHEN the security indicator button is clicked THEN record telemetry and fail silently`() {
        every { customTab.id } returns "unknown"
        val toolbarStore = buildStore()

        toolbarStore.dispatch(SiteInfoClicked)
        testDispatcher.scheduler.advanceUntilIdle()

        val telemetry = Toolbar.buttonTapped.testGetValue()?.get(0)
        assertNotNull(telemetry)
        assertEquals("toolbar", telemetry?.category)
        assertEquals("button_tapped", telemetry?.name)
        assertEquals(SOURCE_CUSTOM_BAR, telemetry?.extra?.get("source"))
        assertEquals(ACTION_SECURITY_INDICATOR_CLICKED, telemetry?.extra?.get("item"))
    }

    @Test
    @Config(sdk = [31])
    fun `GIVEN on Android 12 WHEN choosing to copy the current URL to clipboard THEN copy to clipboard and show a snackbar`() {
        val appStore: AppStore = mockk(relaxed = true)
        val navController: NavController = mockk(relaxed = true)
        every { customTab.content.url } returns "https://mozilla.test"
        val clipboard = ClipboardHandler(testContext)
        val middleware = buildMiddleware(appStore = appStore, clipboard = clipboard, navController = navController)
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(CopyToClipboardClicked)

        assertEquals(customTab.content.url, clipboard.text)
        verify { appStore.dispatch(URLCopiedToClipboard) }
        assertNotNull(Events.copyUrlTapped.testGetValue())
    }

    @Test
    @Config(sdk = [33])
    fun `GIVEN on Android 13 WHEN choosing to copy the current URL to clipboard THEN copy to clipboard and don't show a snackbar`() {
        val appStore: AppStore = mockk(relaxed = true)
        every { customTab.content.url } returns "https://mozilla.test"
        val clipboard = ClipboardHandler(testContext)
        val middleware = buildMiddleware(appStore = appStore, clipboard = clipboard)
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(CopyToClipboardClicked)

        assertEquals(customTab.content.url, clipboard.text)
        verify(exactly = 0) { appStore.dispatch(URLCopiedToClipboard) }
        assertNotNull(Events.copyUrlTapped.testGetValue())
    }

    @Test
    fun `WHEN the website title changes THEN update the shown page origin`() = runTest {
        val customTab = createCustomTab(title = "Title", url = "URL", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedDetails = PageOrigin(
            hint = R.string.search_hint,
            title = "Title",
            url = "URL",
            contextualMenuOptions = listOf(ContextualMenuOption.CopyURLToClipboard),
            onClick = null,
        )

        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()
        var pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertPageOriginEquals(expectedDetails, pageOrigin)

        browserStore.dispatch(UpdateTitleAction(customTabId, "UpdatedTitle"))
        testDispatcher.scheduler.advanceUntilIdle()
        pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertPageOriginEquals(expectedDetails.copy(title = "UpdatedTitle"), pageOrigin)
    }

    @Test
    fun `GIVEN no title available WHEN the website url changes THEN update the shown page origin`() = runTest {
        val customTab = createCustomTab(url = "URL", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        var expectedDetails = PageOrigin(
            hint = R.string.search_hint,
            title = null,
            url = "URL",
            contextualMenuOptions = listOf(ContextualMenuOption.CopyURLToClipboard),
            onClick = null,
        )

        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()
        var pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertPageOriginEquals(expectedDetails, pageOrigin)

        browserStore.dispatch(UpdateUrlAction(customTabId, "UpdatedURL"))
        testDispatcher.scheduler.advanceUntilIdle()
        pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertPageOriginEquals(expectedDetails.copy(url = "UpdatedURL"), pageOrigin)
    }

    @Test
    fun `GIVEN a title previously available WHEN the website url changes THEN update the shown page origin`() = runTest {
        val customTab = createCustomTab(title = "Title", url = "URL", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        var expectedDetails = PageOrigin(
            hint = R.string.search_hint,
            title = "Title",
            url = "URL",
            contextualMenuOptions = listOf(ContextualMenuOption.CopyURLToClipboard),
            onClick = null,
        )

        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()
        var pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertPageOriginEquals(expectedDetails, pageOrigin)

        browserStore.dispatch(UpdateUrlAction(customTabId, "UpdatedURL"))
        testDispatcher.scheduler.advanceUntilIdle()
        pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertPageOriginEquals(
            expectedDetails.copy(
                // If a title was used previously and not available after then the URL is shown as title also.
                title = "UpdatedURL",
                url = "UpdatedURL",
            ),
            pageOrigin,
        )
    }

    @Test
    fun `GIVEN an url with an ip address for the domain WHEN displaying the page origin THEN correctly infer the ip address as the domain`() = runTest {
        val customTab = createCustomTab(title = "Title", url = "http://127.0.0.1/test", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val middleware = buildMiddleware(browserStore)
        val expectedPageOrigin = PageOrigin(
            hint = R.string.search_hint,
            title = "Title",
            url = "127.0.0.1",
            contextualMenuOptions = listOf(ContextualMenuOption.CopyURLToClipboard),
            onClick = null,
        )

        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()
        val pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertPageOriginEquals(expectedPageOrigin, pageOrigin)
    }

    @Test
    fun `GIVEN a url with subdomain and path WHEN displaying the page origin THEN show the subdomain and domain`() = runTest {
        val registrableDomain = "mozilla.com"
        val subDomain = "www."
        val domain = "$subDomain$registrableDomain"
        val customTab = createCustomTab(title = "Title", url = "https://$domain/firefox", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(customTabs = listOf(customTab)),
        )
        val expectedPageOrigin = PageOrigin(
            hint = R.string.search_hint,
            title = "Title",
            url = domain,
            onClick = null,
        )
        every { publicSuffixList.getPublicSuffixPlusOne(any()) } returns CompletableDeferred(registrableDomain)
        val middleware = buildMiddleware(browserStore)

        val toolbarStore = buildStore(middleware)
        testDispatcher.scheduler.advanceUntilIdle()

        val pageOrigin = toolbarStore.state.displayState.pageOrigin
        assertPageOriginEquals(expectedPageOrigin, pageOrigin)
        assertEquals(
            subDomain.length to domain.length,
            pageOrigin.url?.getRegistrableDomainIndexRange(),
        )
    }

    @Test
    fun `GIVEN the custom tab is not configured to show a share button WHEN initializing the toolbar THEN show just a menu button`() {
        every { customTab.config.showShareMenuItem } returns false
        val expectedMenuButton = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
            contentDescription = R.string.content_description_menu,
            onClick = MenuClicked,
        )

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(1, toolbarBrowserActions.size)
        val menuButton = toolbarBrowserActions[0]
        assertEquals(expectedMenuButton, menuButton)
    }

    @Test
    fun `GIVEN the custom tab is configured to show a share button WHEN initializing the toolbar THEN show just a menu button`() {
        every { customTab.config.showShareMenuItem } returns true
        val expectedMenuButton = ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
            contentDescription = R.string.content_description_menu,
            onClick = MenuClicked,
        )

        val toolbarStore = buildStore()

        val toolbarBrowserActions = toolbarStore.state.displayState.browserActionsEnd
        assertEquals(1, toolbarBrowserActions.size)
        val menuButton = toolbarBrowserActions[0]
        assertEquals(expectedMenuButton, menuButton)
    }

    @Test
    fun `GIVEN a non-sandbox custom tab WHEN the menu button is clicked THEN navigate to menu with isSandboxCustomTab false`() {
        val navController: NavController = mockk(relaxed = true)
        val middleware = buildMiddleware(navController = navController, isSandboxCustomTab = false)
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(MenuClicked)
        testDispatcher.scheduler.advanceUntilIdle()

        verify {
            navController.nav(
                R.id.externalAppBrowserFragment,
                BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                    accesspoint = MenuAccessPoint.External,
                    customTabSessionId = customTabId,
                    isSandboxCustomTab = false,
                ),
            )
        }
    }

    @Test
    fun `GIVEN a sandbox custom tab WHEN the menu button is clicked THEN navigate to menu with isSandboxCustomTab true`() {
        val navController: NavController = mockk(relaxed = true)
        val middleware = buildMiddleware(navController = navController, isSandboxCustomTab = true)
        val toolbarStore = buildStore(middleware)

        toolbarStore.dispatch(MenuClicked)
        testDispatcher.scheduler.advanceUntilIdle()

        verify {
            navController.nav(
                R.id.externalAppBrowserFragment,
                BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                    accesspoint = MenuAccessPoint.External,
                    customTabSessionId = customTabId,
                    isSandboxCustomTab = true,
                ),
            )
        }
    }

    @Test
    fun `GIVEN a bottom toolbar WHEN the loading progress changes THEN update the progress bar`() = runTest {
        every { settings.shouldUseBottomToolbar } returns true
        val customTab = createCustomTab(url = "test", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(
                customTabs = listOf(customTab),
            ),
        )
        val middleware = buildMiddleware(browserStore)
        val toolbarStore = buildStore(middleware)

        browserStore.dispatch(UpdateProgressAction(customTabId, 50))
        testDispatcher.scheduler.advanceUntilIdle()
        assertEquals(
            ProgressBarConfig(
                progress = 50,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )

        browserStore.dispatch(UpdateProgressAction(customTabId, 80))
        testDispatcher.scheduler.advanceUntilIdle()
        assertEquals(
            ProgressBarConfig(
                progress = 80,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )
    }

    @Test
    fun `GIVEN a top toolbar WHEN the loading progress changes THEN update the progress bar`() = runTest {
        every { settings.shouldUseBottomToolbar } returns false
        val customTab = createCustomTab(url = "test", id = customTabId)
        val browserStore = BrowserStore(
            BrowserState(
                customTabs = listOf(customTab),
            ),
        )
        val middleware = buildMiddleware(browserStore)
        val toolbarStore = buildStore(middleware)

        browserStore.dispatch(UpdateProgressAction(customTabId, 22))
        testDispatcher.scheduler.advanceUntilIdle()
        assertEquals(
            ProgressBarConfig(
                progress = 22,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )

        browserStore.dispatch(UpdateProgressAction(customTabId, 67))
        testDispatcher.scheduler.advanceUntilIdle()
        assertEquals(
            ProgressBarConfig(
                progress = 67,
                color = null,
            ),
            toolbarStore.state.displayState.progressBarConfig,
        )
    }

    private fun buildMiddleware(
        browserStore: BrowserStore = this.browserStore,
        appStore: AppStore = this.appStore,
        ipProtectionStore: IPProtectionStore = this.ipProtectionStore,
        permissionsStorage: SitePermissionsStorage = this.permissionsStorage,
        cookieBannersStorage: CookieBannersStorage = this.cookieBannersStorage,
        useCases: CustomTabsUseCases = this.useCases,
        trackingProtectionUseCases: TrackingProtectionUseCases = this.trackingProtectionUseCases,
        publicSuffixList: PublicSuffixList = this.publicSuffixList,
        clipboard: ClipboardHandler = this.clipboard,
        navController: NavController = this.navController,
        closeTabDelegate: () -> Unit = this.closeTabDelegate,
        settings: Settings = this.settings,
        isSandboxCustomTab: Boolean = false,
    ) = CustomTabBrowserToolbarMiddleware(
        uiContext = testContext,
        customTabId = this.customTabId,
        browserStore = browserStore,
        appStore = appStore,
        ipProtectionStore = ipProtectionStore,
        permissionsStorage = permissionsStorage,
        cookieBannersStorage = cookieBannersStorage,
        useCases = useCases,
        trackingProtectionUseCases = trackingProtectionUseCases,
        publicSuffixList = publicSuffixList,
        clipboard = clipboard,
        navController = navController,
        closeTabDelegate = closeTabDelegate,
        settings = settings,
        scope = testScope,
        isSandboxCustomTab = isSandboxCustomTab,
    )

    private fun buildStore(
        middleware: CustomTabBrowserToolbarMiddleware = buildMiddleware(),
    ) = BrowserToolbarStore(
        middleware = listOf(middleware),
    )

    private fun assertPageOriginEquals(expected: PageOrigin, actual: PageOrigin) {
        assertEquals(expected.hint, actual.hint)
        assertEquals(expected.title, actual.title)
        assertEquals(expected.url.toString(), actual.url.toString())
        // Cannot check the onClick and onLongClick anonymous object
    }
}

/**
 * Robolectric default implementation of [InetAddresses] returns false for any address.
 * This shadow is used to override that behavior and return true for any IP address.
 */
@Implements(InetAddresses::class)
private class ShadowInetAddresses {
    companion object {
        @Implementation
        @JvmStatic
        @Suppress("DEPRECATION")
        fun isNumericAddress(address: String): Boolean {
            return Patterns.IP_ADDRESS.matcher(address).matches() || address.contains(":")
        }
    }
}

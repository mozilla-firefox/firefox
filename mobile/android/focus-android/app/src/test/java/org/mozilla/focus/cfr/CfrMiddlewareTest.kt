/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.focus.cfr

import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.action.TrackingProtectionAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.content.blocking.Tracker
import mozilla.components.support.test.any
import mozilla.components.support.test.whenever
import org.junit.Before
import org.junit.Test
import org.mockito.Mock
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify
import org.mockito.MockitoAnnotations
import org.mozilla.experiments.nimbus.internal.FeatureHolder
import org.mozilla.focus.nimbus.Onboarding
import org.mozilla.focus.state.AppAction
import org.mozilla.focus.state.AppState
import org.mozilla.focus.state.AppStore
import org.mozilla.focus.utils.Settings

class CfrMiddlewareTest {

    @Mock private lateinit var onboardingExperiment: FeatureHolder<Onboarding>

    @Mock private lateinit var onboardingConfig: Onboarding

    @Mock private lateinit var appStore: AppStore

    private lateinit var appState: AppState

    @Mock private lateinit var settings: Settings
    private lateinit var browserStore: BrowserStore
    private lateinit var cfrMiddleware: CfrMiddleware

    @Before
    fun setUp() {
        MockitoAnnotations.openMocks(this)
        whenever(onboardingExperiment.value()).thenReturn(onboardingConfig)
        whenever(onboardingConfig.isCfrEnabled).thenReturn(true)

        whenever(settings.shouldShowCfrForTrackingProtection).thenReturn(true)

        val defaultScreen = org.mozilla.focus.state.Screen.Home()
        appState = AppState(screen = defaultScreen, showEraseTabsCfr = false)
        whenever(appStore.state).thenAnswer { appState }

        cfrMiddleware = spy(CfrMiddleware(appStore, settings) { onboardingExperiment })

        browserStore =
            BrowserStore(
                initialState = BrowserState(tabs = listOf()),
                middleware = listOf(cfrMiddleware),
            )
    }

    @Test
    fun `GIVEN shouldShowCfrForTrackingProtection is true WHEN UpdateSecurityInfoAction is intercepted THEN showTrackingProtectionCfr is changed to true`() {
        doReturn(false).`when`(cfrMiddleware).isMozillaUrl(any())

        val tab = createTab(tabId = 1, isSecure = true)
        browserStore.dispatch(TabListAction.AddTabAction(tab))
        browserStore.dispatch(TabListAction.SelectTabAction(tab.id))

        val trackerBlockedAction =
            TrackingProtectionAction.TrackerBlockedAction(
                tabId = "1",
                tracker =
                    Tracker(
                        url = "test.org",
                        trackingCategories =
                            listOf(EngineSession.TrackingProtectionPolicy.TrackingCategory.CRYPTOMINING),
                        cookiePolicies = listOf(EngineSession.TrackingProtectionPolicy.CookiePolicy.ACCEPT_NONE),
                    ),
            )

        browserStore.dispatch(trackerBlockedAction)

        verify(appStore).dispatch(AppAction.ShowTrackingProtectionCfrChange(mapOf("1" to true)))
    }

    @Test
    fun `GIVEN insecure tab WHEN UpdateSecurityInfoAction is intercepted THEN showTrackingProtectionCfr is not changed to true`() {
        doReturn(false).`when`(cfrMiddleware).isMozillaUrl(any())

        val insecureTab = createTab(isSecure = false)

        browserStore.dispatch(TabListAction.AddTabAction(insecureTab))
        browserStore.dispatch(TabListAction.SelectTabAction(insecureTab.id))

        val updateSecurityInfoAction =
            ContentAction.UpdateSecurityInfoAction(
                "1",
                SecurityInfo.Insecure(
                    host = "test.org",
                    issuer = "Test",
                ),
            )
        browserStore.dispatch(updateSecurityInfoAction)

        val trackerBlockedAction =
            TrackingProtectionAction.TrackerBlockedAction(
                tabId = "1",
                tracker =
                    Tracker(
                        url = "test.org",
                        trackingCategories =
                            listOf(EngineSession.TrackingProtectionPolicy.TrackingCategory.CRYPTOMINING),
                        cookiePolicies = listOf(EngineSession.TrackingProtectionPolicy.CookiePolicy.ACCEPT_NONE),
                    ),
            )
        browserStore.dispatch(trackerBlockedAction)

        verify(appStore, never()).dispatch(AppAction.ShowTrackingProtectionCfrChange(mapOf("1" to true)))
    }

    @Test
    fun `GIVEN mozilla tab WHEN UpdateSecurityInfoAction is intercepted THEN showTrackingProtectionCfr is not changed to true`() {
        doReturn(true).`when`(cfrMiddleware).isMozillaUrl(any())

        val mozillaTab = createTab(tabId = 1, tabUrl = "https://www.mozilla.org", isSecure = true)

        browserStore.dispatch(TabListAction.AddTabAction(mozillaTab))
        browserStore.dispatch(TabListAction.SelectTabAction(mozillaTab.id))

        val updateSecurityInfoAction =
            ContentAction.UpdateSecurityInfoAction(
                "1",
                SecurityInfo.Secure(
                    host = "test.org",
                    issuer = "Test",
                ),
            )
        browserStore.dispatch(updateSecurityInfoAction)

        val trackerBlockedAction =
            TrackingProtectionAction.TrackerBlockedAction(
                tabId = "1",
                tracker =
                    Tracker(
                        url = "test.org",
                        trackingCategories =
                            listOf(EngineSession.TrackingProtectionPolicy.TrackingCategory.CRYPTOMINING),
                        cookiePolicies = listOf(EngineSession.TrackingProtectionPolicy.CookiePolicy.ACCEPT_NONE),
                    ),
            )
        browserStore.dispatch(trackerBlockedAction)

        verify(appStore, never()).dispatch(AppAction.ShowTrackingProtectionCfrChange(mapOf("1" to true)))
    }

    @Test
    fun `GIVEN settings prevent CFR WHEN TrackerBlockedAction is intercepted THEN showTrackingProtectionCfr is not dispatched`() {
        whenever(settings.shouldShowCfrForTrackingProtection).thenReturn(false)
        doReturn(false).`when`(cfrMiddleware).isMozillaUrl(any())

        val tab = createTab(tabId = 1, isSecure = true)
        browserStore.dispatch(TabListAction.AddTabAction(tab))
        browserStore.dispatch(TabListAction.SelectTabAction(tab.id))

        val trackerBlockedAction =
            TrackingProtectionAction.TrackerBlockedAction(
                tabId = "1",
                tracker =
                    Tracker(
                        url = "test.org",
                        trackingCategories =
                            listOf(EngineSession.TrackingProtectionPolicy.TrackingCategory.CRYPTOMINING),
                        cookiePolicies = listOf(EngineSession.TrackingProtectionPolicy.CookiePolicy.ACCEPT_NONE),
                    ),
            )
        browserStore.dispatch(trackerBlockedAction)

        verify(appStore, never()).dispatch(AppAction.ShowTrackingProtectionCfrChange(mapOf("1" to true)))
    }

    @Test
    fun `GIVEN erase tabs CFR is shown WHEN TrackerBlockedAction is intercepted THEN showTrackingProtectionCfr is not dispatched`() {
        appState = appState.copy(showEraseTabsCfr = true)
        doReturn(false).`when`(cfrMiddleware).isMozillaUrl(any())

        val tab = createTab(tabId = 1, isSecure = true)
        browserStore.dispatch(TabListAction.AddTabAction(tab))
        browserStore.dispatch(TabListAction.SelectTabAction(tab.id))

        val trackerBlockedAction =
            TrackingProtectionAction.TrackerBlockedAction(
                tabId = "1",
                tracker =
                    Tracker(
                        url = "test.org",
                        trackingCategories =
                            listOf(EngineSession.TrackingProtectionPolicy.TrackingCategory.CRYPTOMINING),
                        cookiePolicies = listOf(EngineSession.TrackingProtectionPolicy.CookiePolicy.ACCEPT_NONE),
                    ),
            )
        browserStore.dispatch(trackerBlockedAction)

        verify(appStore, never()).dispatch(AppAction.ShowTrackingProtectionCfrChange(mapOf("1" to true)))
    }

    private fun createTab(
        tabUrl: String = "https://www.test.org",
        tabId: Int = 1,
        isSecure: Boolean = true,
    ): TabSessionState {
        val tab = createTab(tabUrl, id = tabId.toString())
        return tab.copy(
            content =
                tab.content.copy(
                    private = true,
                    securityInfo = SecurityInfo.from(isSecure),
                )
        )
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import androidx.activity.result.ActivityResultLauncher
import androidx.core.net.toUri
import kotlinx.coroutines.Deferred
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.TrackingCategory
import mozilla.components.concept.engine.content.blocking.TrackerLog
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.support.ktx.kotlin.getOrigin
import mozilla.components.support.test.any
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.mock
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyLong
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.PermissionStorage
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.toggle
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelMiddleware
import org.mozilla.fenix.settings.trustpanel.store.AutoplayValue
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermission
import org.mozilla.fenix.trackingprotection.TrackerBuckets
import org.mozilla.fenix.utils.Settings

@RunWith(FenixRobolectricTestRunner::class)
class TrustPanelMiddlewareTest {

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()
    private val scope = coroutinesTestRule.scope

    private lateinit var appStore: AppStore
    private lateinit var engine: Engine
    private lateinit var publicSuffixList: PublicSuffixList
    private lateinit var sessionUseCases: SessionUseCases
    private lateinit var trackingProtectionUseCases: TrackingProtectionUseCases
    private lateinit var settings: Settings
    private lateinit var permissionStorage: PermissionStorage
    private lateinit var requestPermissionsLauncher: ActivityResultLauncher<Array<String>>

    @Before
    fun setup() {
        appStore = mock()
        engine = mock()
        publicSuffixList = mock()
        sessionUseCases = mock()
        trackingProtectionUseCases = mock()
        settings = mock()
        permissionStorage = mock()
        requestPermissionsLauncher = mock()
    }

    @Test
    fun `GIVEN tracking protection is enabled WHEN toggle tracking protection action is dispatched THEN tracking protection exception is added`() = runTestOnMain {
        val sessionId = "0"
        val sessionState: SessionState = mock()
        val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mock()
        val addExceptionUseCase: TrackingProtectionUseCases.AddExceptionUseCase = mock()

        whenever(sessionState.id).thenReturn(sessionId)
        whenever(sessionUseCases.reload).thenReturn(reloadUrlUseCase)
        whenever(trackingProtectionUseCases.addException).thenReturn(addExceptionUseCase)

        val store = createStore(
            trustPanelState = TrustPanelState(
                isTrackingProtectionEnabled = true,
                sessionState = sessionState,
            ),
        )

        store.dispatch(TrustPanelAction.ToggleTrackingProtection)
        store.waitUntilIdle()

        verify(addExceptionUseCase).invoke(sessionId)
        verify(reloadUrlUseCase).invoke(sessionId)
    }

    @Test
    fun `GIVEN tracking protection is disabled WHEN toggle tracking protection action is dispatched THEN tracking protection exception is removed`() = runTestOnMain {
        val sessionId = "0"
        val sessionState: SessionState = mock()
        val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mock()
        val removeExceptionUseCase: TrackingProtectionUseCases.RemoveExceptionUseCase = mock()

        whenever(sessionState.id).thenReturn(sessionId)
        whenever(sessionUseCases.reload).thenReturn(reloadUrlUseCase)
        whenever(trackingProtectionUseCases.removeException).thenReturn(removeExceptionUseCase)

        val store = createStore(
            trustPanelState = TrustPanelState(
                isTrackingProtectionEnabled = false,
                sessionState = sessionState,
            ),
        )

        store.dispatch(TrustPanelAction.ToggleTrackingProtection)
        store.waitUntilIdle()

        verify(removeExceptionUseCase).invoke(sessionId)
        verify(reloadUrlUseCase).invoke(sessionId)
    }

    @Test
    fun `WHEN update trackers blocked action is dispatched THEN bucketed trackers state is updated`() = runTestOnMain {
        val url = "https://www.mozilla.org"
        val trackerLogList = listOf(
            TrackerLog(url = url, blockedCategories = listOf(TrackingCategory.FINGERPRINTING)),
        )
        val bucketedTrackers = spy(TrackerBuckets())

        val store = createStore(
            trustPanelState = TrustPanelState(bucketedTrackers = bucketedTrackers),
        )

        store.dispatch(TrustPanelAction.UpdateTrackersBlocked(trackerLogList))
        store.waitUntilIdle()

        verify(bucketedTrackers).updateIfNeeded(trackerLogList)
        assertEquals(store.state.numberOfTrackersBlocked, 1)
    }

    @Test
    fun `GIVEN the base domain is null WHEN request clear site data dialog action is dispatched THEN clear site data dialog is not launched`() = runTestOnMain {
        val url = "www.mozilla.org"
        val baseDomain = "mozilla.org"

        val sessionState: SessionState = mock()
        val contentState: ContentState = mock()

        whenever(sessionState.content).thenReturn(contentState)
        whenever(contentState.url).thenReturn(url)
        whenever(publicSuffixList.getPublicSuffixPlusOne(url)).thenReturn(null)

        val store = spy(
            createStore(
                trustPanelState = TrustPanelState(sessionState = sessionState),
            ),
        )

        store.dispatch(TrustPanelAction.RequestClearSiteDataDialog)
        store.waitUntilIdle()
        store.waitUntilIdle() // Wait to ensure no calls to store.dispatch(TrustPanelAction.UpdateBaseDomain(...))

        verify(store, never()).dispatch(TrustPanelAction.UpdateBaseDomain(baseDomain))
    }

    @Test
    fun `GIVEN the base domain is not null WHEN request clear site data dialog action is dispatched THEN clear site data dialog is launched`() = runTestOnMain {
        val baseDomain = "mozilla.org"
        val url = "https://www.mozilla.org"
        val urlHost = url.toUri().host.orEmpty()

        val publicSuffixDeferredString: Deferred<String?> = mock()
        val sessionState: SessionState = mock()
        val contentState: ContentState = mock()

        whenever(sessionState.content).thenReturn(contentState)
        whenever(contentState.url).thenReturn(url)
        whenever(publicSuffixList.getPublicSuffixPlusOne(urlHost)).thenReturn(publicSuffixDeferredString)
        whenever(publicSuffixDeferredString.await()).thenReturn(baseDomain)

        val store = createStore(
            trustPanelState = TrustPanelState(sessionState = sessionState),
        )

        store.dispatch(TrustPanelAction.RequestClearSiteDataDialog)
        store.waitUntilIdle()
        store.waitUntilIdle() // Wait for call to store.dispatch(TrustPanelAction.UpdateBaseDomain(...))

        assertEquals(store.state.baseDomain, baseDomain)
    }

    @Test
    fun `WHEN clear site data action is dispatched THEN site data is cleared`() = runTestOnMain {
        val baseDomain = "mozilla.org"

        val store = createStore(
            trustPanelState = TrustPanelState(baseDomain = baseDomain),
        )

        store.dispatch(TrustPanelAction.ClearSiteData)
        store.waitUntilIdle()

        verify(engine).clearData(
            host = baseDomain,
            data = Engine.BrowsingData.select(
                Engine.BrowsingData.AUTH_SESSIONS,
                Engine.BrowsingData.ALL_SITE_DATA,
            ),
        )
        verify(appStore).dispatch(AppAction.SiteDataCleared)
    }

    @Test
    fun `GIVEN toggleable permission is blocked by Android WHEN toggle toggleable permission action is dispatched THEN permission is requested`() = runTestOnMain {
        val toggleablePermission = WebsitePermission.Toggleable(
            isEnabled = true,
            isBlockedByAndroid = true,
            isVisible = true,
            deviceFeature = PhoneFeature.CAMERA,
        )
        val store = createStore(
            trustPanelState = TrustPanelState(
                websitePermissionsState = mapOf(PhoneFeature.CAMERA to toggleablePermission),
            ),
        )

        store.dispatch(TrustPanelAction.TogglePermission(toggleablePermission))
        store.waitUntilIdle()

        verify(requestPermissionsLauncher).launch(PhoneFeature.CAMERA.androidPermissionsList)
    }

    @Test
    fun `GIVEN site permissions are null WHEN toggle toggleable permission action is dispatched THEN permissions are not updated`() = runTestOnMain {
        val toggleablePermission = WebsitePermission.Toggleable(
            isEnabled = true,
            isBlockedByAndroid = false,
            isVisible = true,
            deviceFeature = PhoneFeature.CAMERA,
        )

        val trustPanelState = spy(TrustPanelState(sitePermissions = null))
        val store = createStore(
            trustPanelState = trustPanelState,
        )

        store.dispatch(TrustPanelAction.TogglePermission(toggleablePermission))
        store.waitUntilIdle()

        // Ensure request permissions launcher is not accessed to request permission
        verify(requestPermissionsLauncher, never()).launch(any())
        // Ensure session state is not accessed to update permissions
        verify(trustPanelState, never()).sessionState
    }

    @Test
    fun `GIVEN toggleable permission is not blocked by Android and site permissions are not null WHEN toggle toggleable permission action is dispatched THEN site permissions are updated`() = runTestOnMain {
        val sessionId = "0"
        val sessionUrl = "https://mozilla.org"
        val sessionState: SessionState = mock()
        val urlOrigin = sessionUrl.getOrigin()
        val originalSitePermissions = SitePermissions(
            origin = urlOrigin!!,
            savedAt = 0,
        )
        val toggleablePermission = WebsitePermission.Toggleable(
            isEnabled = true,
            isBlockedByAndroid = false,
            isVisible = true,
            deviceFeature = PhoneFeature.CAMERA,
        )

        val sessionContentState: ContentState = mock()
        val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mock()
        val updatedSitePermissions = originalSitePermissions.toggle(PhoneFeature.CAMERA)

        whenever(sessionState.id).thenReturn(sessionId)
        whenever(sessionState.content).thenReturn(sessionContentState)
        whenever(sessionUseCases.reload).thenReturn(reloadUrlUseCase)
        whenever(sessionContentState.url).thenReturn(sessionUrl)
        whenever(sessionContentState.private).thenReturn(false)

        val store = createStore(
            trustPanelState = TrustPanelState(
                sitePermissions = originalSitePermissions,
                sessionState = sessionState,
                websitePermissionsState = mapOf(PhoneFeature.CAMERA to toggleablePermission),
            ),
        )

        store.dispatch(TrustPanelAction.TogglePermission(toggleablePermission))
        store.waitUntilIdle()

        verify(permissionStorage).updateSitePermissions(updatedSitePermissions, false)
        verify(reloadUrlUseCase).invoke(sessionId)
    }

    @Test
    fun `GIVEN site permissions is null WHEN update autoplay value action is dispatched THEN site permissions are updated`() = runTestOnMain {
        val sessionId = "0"
        val sessionUrl = "https://mozilla.org"
        val autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL
        val sessionState: SessionState = mock()

        val sessionContentState: ContentState = mock()
        val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mock()

        val updatedSitePermissions: SitePermissions = mock()
        val newSitePermissions: SitePermissions = mock()
        val sitePermissionsRules: SitePermissionsRules = mock()

        whenever(sessionState.id).thenReturn(sessionId)
        whenever(sessionState.content).thenReturn(sessionContentState)
        whenever(sessionUseCases.reload).thenReturn(reloadUrlUseCase)
        whenever(sessionContentState.url).thenReturn(sessionUrl)
        whenever(sessionContentState.private).thenReturn(false)

        whenever(settings.getSitePermissionsCustomSettingsRules()).thenReturn(sitePermissionsRules)
        whenever(sitePermissionsRules.toSitePermissions(anyString(), anyLong())).thenReturn(newSitePermissions)
        whenever(
            newSitePermissions.copy(
                autoplayAudible = autoplayValue.autoplayAudibleStatus,
                autoplayInaudible = autoplayValue.autoplayInaudibleStatus,
            ),
        ).thenReturn(updatedSitePermissions)

        val store = createStore(
            trustPanelState = TrustPanelState(
                sitePermissions = null,
                sessionState = sessionState,
            ),
        )

        store.dispatch(TrustPanelAction.UpdateAutoplayValue(autoplayValue))
        store.waitUntilIdle()

        verify(permissionStorage).add(updatedSitePermissions, false)
        verify(reloadUrlUseCase).invoke(sessionId)
    }

    @Test
    fun `GIVEN site permissions is not null WHEN update autoplay value action is dispatched THEN site permissions are updated`() = runTestOnMain {
        val sessionId = "0"
        val autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL
        val sessionState: SessionState = mock()

        val sessionContentState: ContentState = mock()
        val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mock()

        val originalSitePermissions: SitePermissions = mock()
        val updatedSitePermissions: SitePermissions = mock()

        whenever(sessionState.id).thenReturn(sessionId)
        whenever(sessionState.content).thenReturn(sessionContentState)
        whenever(sessionUseCases.reload).thenReturn(reloadUrlUseCase)
        whenever(sessionContentState.private).thenReturn(false)
        whenever(
            originalSitePermissions.copy(
                autoplayAudible = autoplayValue.autoplayAudibleStatus,
                autoplayInaudible = autoplayValue.autoplayInaudibleStatus,
            ),
        ).thenReturn(updatedSitePermissions)

        val store = createStore(
            trustPanelState = TrustPanelState(
                sitePermissions = originalSitePermissions,
                sessionState = sessionState,
            ),
        )

        store.dispatch(TrustPanelAction.UpdateAutoplayValue(autoplayValue))
        store.waitUntilIdle()

        verify(permissionStorage).updateSitePermissions(updatedSitePermissions, false)
        verify(reloadUrlUseCase).invoke(sessionId)
    }

    @Test
    fun `GIVEN autoplay value matches the current autoplay status WHEN update autoplay value action is dispatched THEN site permissions are not updated`() = runTestOnMain {
        val sessionId = "0"
        val autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL

        val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mock()
        val updatedSitePermissions: SitePermissions = mock()

        whenever(sessionUseCases.reload).thenReturn(reloadUrlUseCase)

        val store = createStore(
            trustPanelState = TrustPanelState(
                websitePermissionsState = mapOf(
                    PhoneFeature.AUTOPLAY to WebsitePermission.Autoplay(
                        autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL,
                        isVisible = true,
                        deviceFeature = PhoneFeature.AUTOPLAY,
                    ),
                ),
            ),
        )

        store.dispatch(TrustPanelAction.UpdateAutoplayValue(autoplayValue))
        store.waitUntilIdle()

        verify(permissionStorage, never()).updateSitePermissions(updatedSitePermissions, false)
        verify(reloadUrlUseCase, never()).invoke(sessionId)
    }

    private fun createStore(
        trustPanelState: TrustPanelState = TrustPanelState(),
        onDismiss: suspend () -> Unit = {},
    ) = TrustPanelStore(
        initialState = trustPanelState,
        middleware = listOf(
            TrustPanelMiddleware(
                appStore = appStore,
                engine = engine,
                publicSuffixList = publicSuffixList,
                sessionUseCases = sessionUseCases,
                trackingProtectionUseCases = trackingProtectionUseCases,
                settings = settings,
                permissionStorage = permissionStorage,
                requestPermissionsLauncher = requestPermissionsLauncher,
                onDismiss = onDismiss,
                scope = scope,
            ),
        ),
    )
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import androidx.activity.result.ActivityResultLauncher
import io.mockk.Called
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.spyk
import io.mockk.verify
import java.security.cert.X509Certificate
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.TrackingCategory
import mozilla.components.concept.engine.content.blocking.TrackerLog
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.session.TrackingProtectionUseCases
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.support.ktx.kotlin.getOrigin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.PermissionStorage
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.toggle
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelMiddleware
import org.mozilla.fenix.settings.trustpanel.store.AutoplayValue
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermission
import org.mozilla.fenix.trackingprotection.TrackerBuckets
import org.mozilla.fenix.utils.Settings

class TrustPanelMiddlewareTest {
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
        appStore = mockk()
        engine = mockk()
        publicSuffixList = mockk()
        sessionUseCases = mockk()
        trackingProtectionUseCases = mockk()
        settings = mockk()
        permissionStorage = mockk(relaxUnitFun = true)
        requestPermissionsLauncher = mockk(relaxUnitFun = true)
    }

    @Test
    fun `GIVEN tracking protection is enabled WHEN toggle tracking protection action is dispatched THEN tracking protection exception is added`() =
        runTest {
            val sessionId = "0"
            val sessionState: SessionState = mockk()
            val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mockk(relaxUnitFun = true)
            val addExceptionUseCase: TrackingProtectionUseCases.AddExceptionUseCase = mockk(relaxUnitFun = true)

            every { sessionState.id } returns sessionId
            every { sessionUseCases.reload } returns reloadUrlUseCase
            every { trackingProtectionUseCases.addException } returns addExceptionUseCase

            val store =
                createStore(
                    scope = this,
                    trustPanelState =
                        TrustPanelState(
                            isTrackingProtectionEnabled = true,
                            sessionState = sessionState,
                        ),
                )

            store.dispatch(TrustPanelAction.ToggleTrackingProtection)
            testScheduler.advanceUntilIdle()

            verify { addExceptionUseCase.invoke(sessionId) }
            verify { reloadUrlUseCase.invoke(sessionId) }
        }

    @Test
    fun `GIVEN tracking protection is disabled WHEN toggle tracking protection action is dispatched THEN tracking protection exception is removed`() =
        runTest {
            val sessionId = "0"
            val sessionState: SessionState = mockk()
            val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mockk(relaxUnitFun = true)
            val removeExceptionUseCase: TrackingProtectionUseCases.RemoveExceptionUseCase = mockk(relaxUnitFun = true)

            every { sessionState.id } returns sessionId
            every { sessionUseCases.reload } returns reloadUrlUseCase
            every { trackingProtectionUseCases.removeException } returns removeExceptionUseCase

            val store =
                createStore(
                    scope = this,
                    trustPanelState =
                        TrustPanelState(
                            isTrackingProtectionEnabled = false,
                            sessionState = sessionState,
                        ),
                )

            store.dispatch(TrustPanelAction.ToggleTrackingProtection)
            testScheduler.advanceUntilIdle()

            verify { removeExceptionUseCase.invoke(sessionId) }
            verify { reloadUrlUseCase.invoke(sessionId) }
        }

    @Test
    fun `WHEN update trackers blocked action is dispatched THEN bucketed trackers state is updated`() = runTest {
        val url = "https://www.mozilla.org"
        val trackerLogList = listOf(TrackerLog(url = url, blockedCategories = listOf(TrackingCategory.FINGERPRINTING)))
        val bucketedTrackers = spyk(TrackerBuckets())

        val store =
            createStore(
                scope = this,
                trustPanelState = TrustPanelState(bucketedTrackers = bucketedTrackers),
            )

        store.dispatch(TrustPanelAction.UpdateTrackersBlocked(trackerLogList))
        testScheduler.advanceUntilIdle()

        verify { bucketedTrackers.updateIfNeeded(trackerLogList) }
        assertEquals(store.state.numberOfTrackersBlocked, 1)
    }

    @Test
    fun `GIVEN the base domain is null WHEN request clear site data dialog action is dispatched THEN clear site data dialog is not launched`() =
        runTest {
            val url = "www.mozilla.org"
            val baseDomain = "mozilla.org"

            val sessionState: SessionState = mockk()
            val contentState: ContentState = mockk()

            every { sessionState.content } returns contentState
            every { contentState.url } returns url
            every { publicSuffixList.getPublicSuffixPlusOne(url) } returns CompletableDeferred(null)

            val store =
                spyk(
                    createStore(
                        scope = backgroundScope,
                        trustPanelState = TrustPanelState(sessionState = sessionState),
                    )
                )

            store.dispatch(TrustPanelAction.RequestClearSiteDataDialog)
            testScheduler.advanceUntilIdle()

            verify(exactly = 0) { store.dispatch(TrustPanelAction.UpdateBaseDomain(baseDomain)) }
        }

    @Test
    fun `GIVEN the base domain is not null WHEN request clear site data dialog action is dispatched THEN clear site data dialog is launched`() =
        runTest {
            val baseDomain = "mozilla.org"
            val url = "https://www.mozilla.org"

            val publicSuffixDeferredString = CompletableDeferred(baseDomain)
            val sessionState: SessionState = mockk()
            val contentState: ContentState = mockk()

            every { sessionState.content } returns contentState
            every { contentState.url } returns url
            every { publicSuffixList.getPublicSuffixPlusOne(any()) } returns publicSuffixDeferredString

            val store =
                createStore(
                    scope = this,
                    trustPanelState = TrustPanelState(sessionState = sessionState),
                )

            store.dispatch(TrustPanelAction.RequestClearSiteDataDialog)
            testScheduler.advanceUntilIdle()

            assertEquals(store.state.baseDomain, baseDomain)
        }

    @Test
    fun `GIVEN toggleable permission is blocked by Android WHEN toggle toggleable permission action is dispatched THEN permission is requested`() =
        runTest {
            val toggleablePermission =
                WebsitePermission.Toggleable(
                    isEnabled = true,
                    isBlockedByAndroid = true,
                    isVisible = true,
                    deviceFeature = PhoneFeature.CAMERA,
                )
            val store =
                createStore(
                    scope = this,
                    trustPanelState =
                        TrustPanelState(websitePermissionsState = mapOf(PhoneFeature.CAMERA to toggleablePermission)),
                )

            store.dispatch(TrustPanelAction.TogglePermission(toggleablePermission))
            testScheduler.advanceUntilIdle()

            verify { requestPermissionsLauncher.launch(PhoneFeature.CAMERA.androidPermissionsList) }
        }

    @Test
    fun `GIVEN site permissions are null WHEN toggle toggleable permission action is dispatched THEN permissions are not updated`() =
        runTest {
            val toggleablePermission =
                WebsitePermission.Toggleable(
                    isEnabled = true,
                    isBlockedByAndroid = false,
                    isVisible = true,
                    deviceFeature = PhoneFeature.CAMERA,
                )

            val mockSessionState: SessionState = mockk()
            val trustPanelState =
                TrustPanelState(
                    sitePermissions = null,
                    sessionState = mockSessionState,
                )
            val store =
                createStore(
                    scope = this,
                    trustPanelState = trustPanelState,
                )

            store.dispatch(TrustPanelAction.TogglePermission(toggleablePermission))
            testScheduler.advanceUntilIdle()

            // Ensure request permissions launcher is not accessed to request permission
            verify(exactly = 0) { requestPermissionsLauncher.launch(any()) }
            // Ensure session state is not accessed to update permissions
            verify { mockSessionState wasNot Called }
        }

    @Test
    fun `GIVEN toggleable permission is not blocked by Android and site permissions are not null WHEN toggle toggleable permission action is dispatched THEN site permissions are updated`() =
        runTest {
            val sessionId = "0"
            val sessionUrl = "https://mozilla.org"
            val sessionState: SessionState = mockk()
            val urlOrigin = sessionUrl.getOrigin()
            val originalSitePermissions =
                SitePermissions(
                    origin = urlOrigin!!,
                    savedAt = 0,
                )
            val toggleablePermission =
                WebsitePermission.Toggleable(
                    isEnabled = true,
                    isBlockedByAndroid = false,
                    isVisible = true,
                    deviceFeature = PhoneFeature.CAMERA,
                )

            val sessionContentState: ContentState = mockk()
            val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mockk(relaxUnitFun = true)
            val updatedSitePermissions = originalSitePermissions.toggle(PhoneFeature.CAMERA)

            every { sessionState.id } returns sessionId
            every { sessionState.content } returns sessionContentState
            every { sessionUseCases.reload } returns reloadUrlUseCase
            every { sessionContentState.url } returns sessionUrl
            every { sessionContentState.private } returns false

            val store =
                createStore(
                    scope = this,
                    trustPanelState =
                        TrustPanelState(
                            sitePermissions = originalSitePermissions,
                            sessionState = sessionState,
                            websitePermissionsState = mapOf(PhoneFeature.CAMERA to toggleablePermission),
                        ),
                )

            store.dispatch(TrustPanelAction.TogglePermission(toggleablePermission))
            testScheduler.advanceUntilIdle()

            coVerify { permissionStorage.updateSitePermissions(updatedSitePermissions, false) }
            verify { reloadUrlUseCase.invoke(sessionId) }
        }

    @Test
    fun `GIVEN site permissions is null WHEN update autoplay value action is dispatched THEN site permissions are updated`() =
        runTest {
            val sessionId = "0"
            val sessionUrl = "https://mozilla.org"
            val autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL
            val sessionState: SessionState = mockk()

            val sessionContentState: ContentState = mockk()
            val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mockk(relaxUnitFun = true)

            val updatedSitePermissions: SitePermissions = mockk()
            val newSitePermissions: SitePermissions = mockk()
            val sitePermissionsRules: SitePermissionsRules = mockk()

            every { sessionState.id } returns sessionId
            every { sessionState.content } returns sessionContentState
            every { sessionUseCases.reload } returns reloadUrlUseCase
            every { sessionContentState.url } returns sessionUrl
            every { sessionContentState.private } returns false

            every { settings.getSitePermissionsCustomSettingsRules() } returns sitePermissionsRules
            every { sitePermissionsRules.toSitePermissions(any()) } returns newSitePermissions
            every {
                newSitePermissions.copy(
                    autoplayAudible = autoplayValue.autoplayAudibleStatus,
                    autoplayInaudible = autoplayValue.autoplayInaudibleStatus,
                )
            } returns updatedSitePermissions

            val store =
                createStore(
                    scope = this,
                    trustPanelState =
                        TrustPanelState(
                            sitePermissions = null,
                            sessionState = sessionState,
                            websitePermissionsState =
                                mapOf(
                                    PhoneFeature.AUTOPLAY to
                                        WebsitePermission.Autoplay(
                                            autoplayValue = AutoplayValue.AUTOPLAY_BLOCK_AUDIBLE,
                                            isVisible = true,
                                            deviceFeature = PhoneFeature.CAMERA,
                                        )
                                ),
                        ),
                )

            store.dispatch(TrustPanelAction.UpdateAutoplayValue(autoplayValue))
            testScheduler.advanceUntilIdle()

            coVerify { permissionStorage.add(updatedSitePermissions, false) }
            verify { reloadUrlUseCase.invoke(sessionId) }
        }

    @Test
    fun `GIVEN site permissions is not null WHEN update autoplay value action is dispatched THEN site permissions are updated`() =
        runTest {
            val sessionId = "0"
            val autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL
            val sessionState: SessionState = mockk()

            val sessionContentState: ContentState = mockk()
            val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mockk(relaxUnitFun = true)

            val originalSitePermissions: SitePermissions = mockk()
            val updatedSitePermissions: SitePermissions = mockk()

            every { sessionState.id } returns sessionId
            every { sessionState.content } returns sessionContentState
            every { sessionUseCases.reload } returns reloadUrlUseCase
            every { sessionContentState.private } returns false
            every {
                originalSitePermissions.copy(
                    autoplayAudible = autoplayValue.autoplayAudibleStatus,
                    autoplayInaudible = autoplayValue.autoplayInaudibleStatus,
                )
            } returns updatedSitePermissions

            val store =
                createStore(
                    scope = this,
                    trustPanelState =
                        TrustPanelState(
                            sitePermissions = originalSitePermissions,
                            sessionState = sessionState,
                            websitePermissionsState =
                                mapOf(
                                    PhoneFeature.AUTOPLAY to
                                        WebsitePermission.Autoplay(
                                            autoplayValue = AutoplayValue.AUTOPLAY_BLOCK_AUDIBLE,
                                            isVisible = true,
                                            deviceFeature = PhoneFeature.CAMERA,
                                        )
                                ),
                        ),
                )

            store.dispatch(TrustPanelAction.UpdateAutoplayValue(autoplayValue))
            testScheduler.advanceUntilIdle()

            coVerify { permissionStorage.updateSitePermissions(updatedSitePermissions, false) }
            verify { reloadUrlUseCase.invoke(sessionId) }
        }

    @Test
    fun `GIVEN autoplay value matches the current autoplay status WHEN update autoplay value action is dispatched THEN site permissions are not updated`() =
        runTest {
            val sessionId = "0"
            val autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL

            val reloadUrlUseCase: SessionUseCases.ReloadUrlUseCase = mockk()
            val updatedSitePermissions: SitePermissions = mockk()

            every { sessionUseCases.reload } returns reloadUrlUseCase

            val store =
                createStore(
                    scope = this,
                    trustPanelState =
                        TrustPanelState(
                            websitePermissionsState =
                                mapOf(
                                    PhoneFeature.AUTOPLAY to
                                        WebsitePermission.Autoplay(
                                            autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL,
                                            isVisible = true,
                                            deviceFeature = PhoneFeature.AUTOPLAY,
                                        )
                                )
                        ),
                )

            store.dispatch(TrustPanelAction.UpdateAutoplayValue(autoplayValue))
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) { permissionStorage.updateSitePermissions(updatedSitePermissions, false) }
            verify(exactly = 0) { reloadUrlUseCase.invoke(sessionId) }
        }

    @Test
    fun `GIVEN a QWAC WHEN the certificate changes THEN the QWAC status of the new certificate is requested`() =
        runTest {
            val newQwac: X509Certificate = mockk()
            val engineSession = mockQwacStatus(newQwac)
            val store =
                createStore(
                    scope = this,
                    trustPanelState = qwacTrustPanelState(engineSession, certificate = mockk(), qwac = mockk()),
                    mainDispatcher = StandardTestDispatcher(testScheduler),
                )

            store.dispatch(TrustPanelAction.UpdateSecurityInfo(isSecured = true, certificate = mockk()))
            testScheduler.advanceUntilIdle()

            verify(exactly = 1) { engineSession.qwacStatus(any()) }
            assertEquals(newQwac, store.state.websiteInfoState.qwac)
        }

    @Test
    fun `GIVEN a QWAC WHEN the security info is updated with the same certificate THEN the QWAC is not requested again`() =
        runTest {
            val certificate: X509Certificate = mockk()
            val qwac: X509Certificate = mockk()
            val engineSession = mockQwacStatus(mockk())
            val store =
                createStore(
                    scope = this,
                    trustPanelState = qwacTrustPanelState(engineSession, certificate = certificate, qwac = qwac),
                    mainDispatcher = StandardTestDispatcher(testScheduler),
                )

            store.dispatch(TrustPanelAction.UpdateSecurityInfo(isSecured = true, certificate = certificate))
            testScheduler.advanceUntilIdle()

            verify(exactly = 0) { engineSession.qwacStatus(any()) }
            assertEquals(qwac, store.state.websiteInfoState.qwac)
        }

    @Test
    fun `GIVEN a QWAC WHEN the certificate is removed THEN the QWAC is not requested again`() = runTest {
        val engineSession = mockQwacStatus(mockk())
        val store =
            createStore(
                scope = this,
                trustPanelState = qwacTrustPanelState(engineSession, certificate = mockk(), qwac = mockk()),
                mainDispatcher = StandardTestDispatcher(testScheduler),
            )

        store.dispatch(TrustPanelAction.UpdateSecurityInfo(isSecured = false, certificate = null))
        testScheduler.advanceUntilIdle()

        verify(exactly = 0) { engineSession.qwacStatus(any()) }
        assertNull(store.state.websiteInfoState.qwac)
    }

    @Test
    fun `GIVEN a pending QWAC request WHEN the page becomes insecure before the result arrives THEN the result is ignored`() =
        runTest {
            val onQwacStatus = slot<(X509Certificate?) -> Unit>()
            val engineSession: EngineSession = mockk()
            every { engineSession.qwacStatus(capture(onQwacStatus)) } returns Unit
            val sessionState: SessionState = mockk()
            every { sessionState.engineState.engineSession } returns engineSession
            val store =
                createStore(
                    scope = this,
                    trustPanelState =
                        TrustPanelState(
                            sessionState = sessionState,
                            websiteInfoState = WebsiteInfoState(certificate = mockk()),
                        ),
                    mainDispatcher = StandardTestDispatcher(testScheduler),
                )

            store.dispatch(TrustPanelAction.RequestQWAC)
            testScheduler.advanceUntilIdle()
            store.dispatch(TrustPanelAction.UpdateSecurityInfo(isSecured = false, certificate = null))
            onQwacStatus.captured.invoke(mockk())

            assertNull(store.state.websiteInfoState.certificate)
            assertNull(store.state.websiteInfoState.qwac)
        }

    private fun mockQwacStatus(qwac: X509Certificate?): EngineSession {
        val engineSession: EngineSession = mockk()
        every { engineSession.qwacStatus(any()) } answers { firstArg<(X509Certificate?) -> Unit>().invoke(qwac) }
        return engineSession
    }

    private fun qwacTrustPanelState(
        engineSession: EngineSession,
        certificate: X509Certificate,
        qwac: X509Certificate,
    ): TrustPanelState {
        val sessionState: SessionState = mockk()
        every { sessionState.engineState.engineSession } returns engineSession
        return TrustPanelState(
            sessionState = sessionState,
            websiteInfoState = WebsiteInfoState(certificate = certificate, qwac = qwac),
        )
    }

    private fun createStore(
        scope: CoroutineScope,
        trustPanelState: TrustPanelState = TrustPanelState(),
        onDismiss: suspend () -> Unit = {},
        mainDispatcher: CoroutineDispatcher = StandardTestDispatcher(),
    ) =
        TrustPanelStore(
            initialState = trustPanelState,
            middleware =
                listOf(
                    TrustPanelMiddleware(
                        engine = engine,
                        publicSuffixList = publicSuffixList,
                        sessionUseCases = sessionUseCases,
                        trackingProtectionUseCases = trackingProtectionUseCases,
                        settings = settings,
                        permissionStorage = permissionStorage,
                        requestPermissionsLauncher = requestPermissionsLauncher,
                        onDismiss = onDismiss,
                        scope = scope,
                        mainDispatcher = mainDispatcher,
                    )
                ),
        )
}

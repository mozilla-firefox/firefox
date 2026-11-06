/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import io.mockk.every
import io.mockk.mockk
import java.security.cert.X509Certificate
import javax.security.auth.x500.X500Principal
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.content.PermissionHighlightsState
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.engine.permission.SitePermissions.Status.ALLOWED
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.IPProtectionStatus
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action.ASK_TO_ALLOW
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.menu.store.IPProtectionMenuState
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_ALLOW_ALL
import org.mozilla.fenix.settings.trustpanel.store.AutoplayValue
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermission
import org.mozilla.fenix.trackingprotection.TrackingProtectionCategory
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TrustPanelStoreTest {

    @Test
    fun `WHEN toggle tracking protection action is dispatched THEN tracking protection enabled state is updated`() =
        runTest {
            val store = TrustPanelStore(initialState = TrustPanelState())

            store.dispatch(TrustPanelAction.ToggleTrackingProtection)

            assertFalse(store.state.isTrackingProtectionEnabled)
        }

    @Test
    fun `WHEN update number of trackers blocked action is dispatched THEN number of trackers blocked state is updated`() =
        runTest {
            val store = TrustPanelStore(initialState = TrustPanelState())

            store.dispatch(TrustPanelAction.UpdateNumberOfTrackersBlocked(1))

            assertEquals(store.state.numberOfTrackersBlocked, 1)
        }

    @Test
    fun `WHEN update base domain action is dispatched THEN base domain state is updated`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState())
        val baseDomain = "mozilla.org"

        store.dispatch(TrustPanelAction.UpdateBaseDomain(baseDomain))

        assertEquals(store.state.baseDomain, baseDomain)
    }

    @Test
    fun `WHEN update detailed tracker category action is dispatched THEN detailed tracker category state is updated`() =
        runTest {
            val store = TrustPanelStore(initialState = TrustPanelState())
            val trackerCategory = TrackingProtectionCategory.CRYPTOMINERS

            store.dispatch(TrustPanelAction.UpdateDetailedTrackerCategory(trackerCategory))

            assertEquals(store.state.detailedTrackerCategory, trackerCategory)
        }

    @Test
    fun `WHEN create website permission state method is called THEN website permission state is created`() {
        val settings: Settings = mockk(relaxed = true)
        val sitePermissions: SitePermissions = mockk(relaxed = true)
        val permissionHighlights: PermissionHighlightsState = mockk()

        initializeSitePermissions(sitePermissions)
        every { permissionHighlights.isAutoPlayBlocking } returns true

        val state =
            TrustPanelStore.createWebsitePermissionState(
                settings = settings,
                sitePermissions = sitePermissions,
                permissionHighlights = permissionHighlights,
                isPermissionBlockedByAndroid = { phoneFeature: PhoneFeature ->
                    phoneFeature == PhoneFeature.CAMERA // Only the camera permission is blocked
                },
            )

        state.entries.forEach { (phoneFeature, websitePermission) ->
            if (websitePermission is WebsitePermission.Autoplay) {
                assertEquals(
                    websitePermission,
                    WebsitePermission.Autoplay(
                        autoplayValue = AutoplayValue.AUTOPLAY_BLOCK_ALL,
                        isVisible = true,
                        deviceFeature = phoneFeature,
                    ),
                )
            } else {
                assertEquals(
                    websitePermission,
                    WebsitePermission.Toggleable(
                        isEnabled = phoneFeature == PhoneFeature.LOCATION,
                        isVisible = phoneFeature == PhoneFeature.LOCATION,
                        isBlockedByAndroid = phoneFeature == PhoneFeature.CAMERA,
                        deviceFeature = phoneFeature,
                    ),
                )
            }
        }
    }

    @Test
    fun `WHEN LNA blocking disabled THEN LNA permissions are not visible in website permission state`() {
        val settings: Settings = mockk(relaxed = true)
        val sitePermissions: SitePermissions = mockk(relaxed = true)
        val permissionHighlights: PermissionHighlightsState = mockk()

        initializeSitePermissions(sitePermissions)

        every { sitePermissions.localDeviceAccess } returns ALLOWED
        every { sitePermissions.localNetworkAccess } returns ALLOWED

        every { permissionHighlights.isAutoPlayBlocking } returns true
        every { settings.isLnaFeatureEnabled } returns false

        val state =
            TrustPanelStore.createWebsitePermissionState(
                settings = settings,
                sitePermissions = sitePermissions,
                permissionHighlights = permissionHighlights,
                isPermissionBlockedByAndroid = { phoneFeature: PhoneFeature ->
                    phoneFeature == PhoneFeature.CAMERA // Only the camera permission is blocked
                },
            )

        val localDeviceAccessPermission = state[PhoneFeature.LOCAL_DEVICE_ACCESS]!!
        val localNetworkAccessPermission = state[PhoneFeature.LOCAL_NETWORK_ACCESS]!!

        assertFalse(
            "Expected local device access permission to be invisible",
            localDeviceAccessPermission.isVisible,
        )
        assertFalse(
            "Expected local network access permission to be invisible",
            localNetworkAccessPermission.isVisible,
        )
    }

    @Test
    fun `WHEN LNA blocking is enabled THEN LNA permissions are visible in website permission state`() {
        val settings: Settings = mockk(relaxed = true)
        val sitePermissions: SitePermissions = mockk(relaxed = true)
        val permissionHighlights: PermissionHighlightsState = mockk()

        initializeSitePermissions(sitePermissions)
        every { sitePermissions.localDeviceAccess } returns ALLOWED
        every { sitePermissions.localNetworkAccess } returns ALLOWED
        every { permissionHighlights.isAutoPlayBlocking } returns true
        every { settings.isLnaFeatureEnabled } returns true

        val state =
            TrustPanelStore.createWebsitePermissionState(
                settings = settings,
                sitePermissions = sitePermissions,
                permissionHighlights = permissionHighlights,
                isPermissionBlockedByAndroid = { phoneFeature: PhoneFeature ->
                    phoneFeature == PhoneFeature.CAMERA // Only the camera permission is blocked
                },
            )

        val localDeviceAccessPermission = state[PhoneFeature.LOCAL_DEVICE_ACCESS]!!
        val localNetworkAccessPermission = state[PhoneFeature.LOCAL_NETWORK_ACCESS]!!

        assertTrue(
            "Expected local device access permission to be visible",
            localDeviceAccessPermission.isVisible,
        )
        assertTrue(
            "Expected local network access permission to be visible",
            localNetworkAccessPermission.isVisible,
        )
    }

    @Test
    fun `WHEN create website permission state method is called THEN te AUTOPLAY_AUDIBLE and AUTOPLAY_INAUDIBLE permissions aren't included`() {
        val settings: Settings = mockk(relaxed = true)
        val sitePermissions: SitePermissions = mockk(relaxed = true)
        val permissionHighlights: PermissionHighlightsState = mockk()

        initializeSitePermissions(sitePermissions)
        every { permissionHighlights.isAutoPlayBlocking } returns true

        val state =
            TrustPanelStore.createWebsitePermissionState(
                settings = settings,
                sitePermissions = sitePermissions,
                permissionHighlights = permissionHighlights,
                isPermissionBlockedByAndroid = { phoneFeature: PhoneFeature ->
                    phoneFeature == PhoneFeature.CAMERA // Only the camera permission is blocked
                },
            )

        assertFalse(PhoneFeature.AUTOPLAY_AUDIBLE in state.keys)
        assertFalse(PhoneFeature.AUTOPLAY_INAUDIBLE in state.keys)
    }

    @Test
    fun `GIVEN site permissions are null WHEN create website permission state method is called THEN autoplay defaults to settings autoplay state`() {
        val settings: Settings = mockk(relaxed = true)
        val permissionHighlights: PermissionHighlightsState = mockk()

        every { permissionHighlights.isAutoPlayBlocking } returns true
        every { settings.getSitePermissionsPhoneFeatureAction(any(), any()) } returns ASK_TO_ALLOW
        every { settings.getAutoplayUserSetting() } returns AUTOPLAY_ALLOW_ALL

        val state =
            TrustPanelStore.createWebsitePermissionState(
                settings = settings,
                sitePermissions = null,
                permissionHighlights = permissionHighlights,
                isPermissionBlockedByAndroid = { phoneFeature: PhoneFeature ->
                    phoneFeature == PhoneFeature.CAMERA // Only the camera permission is blocked
                },
            )

        assertEquals(
            state[PhoneFeature.AUTOPLAY],
            WebsitePermission.Autoplay(
                autoplayValue = AutoplayValue.AUTOPLAY_ALLOW_ALL,
                isVisible = true,
                deviceFeature = PhoneFeature.AUTOPLAY,
            ),
        )
    }

    @Test
    fun `GIVEN site permissions are null and autoplay is not blocking WHEN create website permission state method is called THEN autoplay isn't visible`() {
        val settings: Settings = mockk(relaxed = true)
        val permissionHighlights: PermissionHighlightsState = mockk()

        every { permissionHighlights.isAutoPlayBlocking } returns false
        every { settings.getSitePermissionsPhoneFeatureAction(any(), any()) } returns ASK_TO_ALLOW

        val state =
            TrustPanelStore.createWebsitePermissionState(
                settings = settings,
                sitePermissions = null,
                permissionHighlights = permissionHighlights,
                isPermissionBlockedByAndroid = { phoneFeature: PhoneFeature ->
                    phoneFeature == PhoneFeature.CAMERA // Only the camera permission is blocked
                },
            )

        assertEquals(
            state[PhoneFeature.AUTOPLAY],
            WebsitePermission.Autoplay(
                autoplayValue = AutoplayValue.AUTOPLAY_BLOCK_ALL,
                isVisible = false,
                deviceFeature = PhoneFeature.AUTOPLAY,
            ),
        )
    }

    @Test
    fun `WHEN update IP protection menu state action is dispatched THEN IP protection menu state is updated`() =
        runTest {
            val store = TrustPanelStore(initialState = TrustPanelState())
            val newState =
                IPProtectionMenuState(
                    status = IPProtectionStatus.Enabled,
                    dataLimit = DataVolume.roundedGigabytes(5),
                )

            store.dispatch(TrustPanelAction.UpdateIPProtectionMenuState(newState))

            assertEquals(newState, store.state.ipProtectionMenuState)
        }

    @Test
    fun `WHEN update site permissions action is dispatched THEN site permissions state is updated`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState())
        val newSitePermissions: SitePermissions = mockk()

        store.dispatch(TrustPanelAction.UpdateSitePermissions(newSitePermissions))

        assertEquals(store.state.sitePermissions, newSitePermissions)
    }

    @Test
    fun `WHEN grant permission blocked by android action is dispatched THEN permissions blocked by android state is updated`() =
        runTest {
            val toggleablePermission =
                WebsitePermission.Toggleable(
                    isEnabled = true,
                    isBlockedByAndroid = true,
                    isVisible = true,
                    deviceFeature = PhoneFeature.CAMERA,
                )

            val store =
                TrustPanelStore(
                    initialState =
                        TrustPanelState(websitePermissionsState = mapOf(PhoneFeature.CAMERA to toggleablePermission))
                )

            store.dispatch(
                TrustPanelAction.WebsitePermissionAction.GrantPermissionBlockedByAndroid(PhoneFeature.CAMERA)
            )

            assertEquals(
                (store.state.websitePermissionsState[PhoneFeature.CAMERA] as? WebsitePermission.Toggleable)
                    ?.isBlockedByAndroid,
                false,
            )
        }

    @Test
    fun `WHEN toggle permission action is dispatched THEN permission enabled state is updated`() = runTest {
        val toggleablePermission =
            WebsitePermission.Toggleable(
                isEnabled = true,
                isBlockedByAndroid = true,
                isVisible = true,
                deviceFeature = PhoneFeature.CAMERA,
            )

        val store =
            TrustPanelStore(
                initialState =
                    TrustPanelState(websitePermissionsState = mapOf(PhoneFeature.CAMERA to toggleablePermission))
            )

        store.dispatch(TrustPanelAction.WebsitePermissionAction.TogglePermission(PhoneFeature.CAMERA))

        assertEquals(
            (store.state.websitePermissionsState[PhoneFeature.CAMERA] as? WebsitePermission.Toggleable)?.isEnabled,
            false,
        )
    }

    @Test
    fun `WHEN change autoplay action is dispatched THEN autoplay value state is updated`() = runTest {
        val toggleablePermission =
            WebsitePermission.Autoplay(
                autoplayValue = AutoplayValue.AUTOPLAY_BLOCK_AUDIBLE,
                isVisible = true,
                deviceFeature = PhoneFeature.CAMERA,
            )

        val store =
            TrustPanelStore(
                initialState =
                    TrustPanelState(websitePermissionsState = mapOf(PhoneFeature.AUTOPLAY to toggleablePermission))
            )

        store.dispatch(TrustPanelAction.WebsitePermissionAction.ChangeAutoplay(AutoplayValue.AUTOPLAY_ALLOW_ALL))

        assertEquals(
            (store.state.websitePermissionsState[PhoneFeature.AUTOPLAY] as? WebsitePermission.Autoplay)?.autoplayValue,
            AutoplayValue.AUTOPLAY_ALLOW_ALL,
        )
    }

    @Test
    fun `WHEN update security info action is dispatched THEN only the security info is updated`() = runTest {
        val certificate: X509Certificate = mockk()
        val store =
            TrustPanelStore(
                initialState =
                    TrustPanelState(
                        websiteInfoState =
                            WebsiteInfoState(
                                isSecured = false,
                                websiteUrl = "https://www.mozilla.org",
                                websiteTitle = "Mozilla",
                            )
                    )
            )

        store.dispatch(TrustPanelAction.UpdateSecurityInfo(isSecured = true, certificate = certificate))

        assertEquals(
            WebsiteInfoState(
                isSecured = true,
                websiteUrl = "https://www.mozilla.org",
                websiteTitle = "Mozilla",
                certificate = certificate,
            ),
            store.state.websiteInfoState,
        )
    }

    @Test
    fun `GIVEN an HTTPS page WHEN navigating to an HTTP page THEN the certificate and its details are cleared`() =
        runTest {
            val certificate: X509Certificate = mockk()
            every { certificate.subjectX500Principal } returns X500Principal("CN=www.mozilla.org, O=Mozilla")
            every { certificate.issuerX500Principal } returns X500Principal("CN=Issuer")
            val store =
                TrustPanelStore(
                    initialState =
                        TrustPanelState(
                            websiteInfoState =
                                WebsiteInfoState(websiteUrl = "https://www.mozilla.org", certificate = certificate)
                        )
                )
            assertEquals("Mozilla", store.state.websiteInfoState.certificateDetails?.subjectOrganization)

            store.dispatch(
                TrustPanelAction.UpdateSecurityInfo(
                    isSecured = false,
                    certificate = null,
                )
            )

            assertFalse(store.state.websiteInfoState.isSecured)
            assertNull(store.state.websiteInfoState.certificate)
            assertNull(store.state.websiteInfoState.certificateDetails)
        }

    @Test
    fun `GIVEN a QWAC WHEN security info is updated with the same certificate THEN the QWAC is kept`() = runTest {
        val certificate: X509Certificate = mockk()
        val qwac: X509Certificate = mockk()
        val store =
            TrustPanelStore(
                initialState =
                    TrustPanelState(websiteInfoState = WebsiteInfoState(certificate = certificate, qwac = qwac))
            )

        store.dispatch(
            TrustPanelAction.UpdateSecurityInfo(
                isSecured = true,
                certificate = certificate,
            )
        )

        assertEquals(qwac, store.state.websiteInfoState.qwac)
    }

    @Test
    fun `GIVEN a QWAC WHEN security info is updated with a different certificate THEN the QWAC is cleared`() = runTest {
        val store =
            TrustPanelStore(
                initialState =
                    TrustPanelState(websiteInfoState = WebsiteInfoState(certificate = mockk(), qwac = mockk()))
            )

        store.dispatch(
            TrustPanelAction.UpdateSecurityInfo(
                isSecured = true,
                certificate = mockk(),
            )
        )

        assertNull(store.state.websiteInfoState.qwac)
    }

    @Test
    fun `WHEN a QWAC is determined for the current certificate THEN the QWAC is stored`() = runTest {
        val certificate: X509Certificate = mockk()
        val qwac: X509Certificate = mockk()
        val store =
            TrustPanelStore(
                initialState = TrustPanelState(websiteInfoState = WebsiteInfoState(certificate = certificate))
            )

        store.dispatch(TrustPanelAction.UpdateQWAC(qwac = qwac, forCertificate = certificate))

        assertEquals(qwac, store.state.websiteInfoState.qwac)
    }

    @Test
    fun `WHEN a QWAC is determined for a previous certificate THEN the QWAC is ignored`() = runTest {
        val store =
            TrustPanelStore(initialState = TrustPanelState(websiteInfoState = WebsiteInfoState(certificate = mockk())))

        store.dispatch(TrustPanelAction.UpdateQWAC(qwac = mockk(), forCertificate = mockk()))

        assertNull(store.state.websiteInfoState.qwac)
    }

    @Test
    fun `GIVEN no QWAC certificate data WHEN the QWAC status is updated without a QWAC THEN isQwac is false`() =
        runTest {
            val store = TrustPanelStore(initialState = TrustPanelState(websiteInfoState = WebsiteInfoState()))

            store.dispatch(
                TrustPanelAction.UpdateQWAC(qwac = null, forCertificate = store.state.websiteInfoState.certificate)
            )

            assertFalse(store.state.websiteInfoState.isQwac)
        }

    @Test
    fun `GIVEN QWAC certificate data WHEN the QWAC status is updated without a QWAC THEN isQwac is false`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState(websiteInfoState = WebsiteInfoState(qwac = mockk())))

        store.dispatch(
            TrustPanelAction.UpdateQWAC(qwac = null, forCertificate = store.state.websiteInfoState.certificate)
        )

        assertFalse(store.state.websiteInfoState.isQwac)
    }

    @Test
    fun `GIVEN no QWAC certificate data yet WHEN QWAC certificate data arrives later THEN isQwac is true`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState(websiteInfoState = WebsiteInfoState()))
        assertFalse(store.state.websiteInfoState.isQwac)

        store.dispatch(
            TrustPanelAction.UpdateQWAC(qwac = mockk(), forCertificate = store.state.websiteInfoState.certificate)
        )

        assertTrue(store.state.websiteInfoState.isQwac)
    }

    private fun initializeSitePermissions(sitePermissions: SitePermissions) {
        every { sitePermissions.camera } returns SitePermissions.Status.NO_DECISION
        every { sitePermissions.microphone } returns SitePermissions.Status.NO_DECISION
        every { sitePermissions.notification } returns SitePermissions.Status.NO_DECISION
        every { sitePermissions.location } returns ALLOWED // Only location allowed
        every { sitePermissions.localStorage } returns SitePermissions.Status.NO_DECISION
        every { sitePermissions.crossOriginStorageAccess } returns SitePermissions.Status.NO_DECISION
        every { sitePermissions.mediaKeySystemAccess } returns SitePermissions.Status.NO_DECISION
        every { sitePermissions.localDeviceAccess } returns SitePermissions.Status.NO_DECISION
        every { sitePermissions.localNetworkAccess } returns SitePermissions.Status.NO_DECISION
    }
}

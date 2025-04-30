/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.content.PermissionHighlightsState
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action.ASK_TO_ALLOW
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.trustpanel.store.AutoplayValue
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore
import org.mozilla.fenix.settings.trustpanel.store.WebsitePermission
import org.mozilla.fenix.trackingprotection.TrackingProtectionCategory
import org.mozilla.fenix.utils.Settings

@RunWith(FenixRobolectricTestRunner::class)
class TrustPanelStoreTest {

    @Test
    fun `WHEN toggle tracking protection action is dispatched THEN tracking protection enabled state is updated`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState())

        store.dispatch(TrustPanelAction.ToggleTrackingProtection).join()

        assertFalse(store.state.isTrackingProtectionEnabled)
    }

    @Test
    fun `WHEN update number of trackers blocked action is dispatched THEN number of trackers blocked state is updated`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState())

        store.dispatch(TrustPanelAction.UpdateNumberOfTrackersBlocked(1)).join()

        assertEquals(store.state.numberOfTrackersBlocked, 1)
    }

    @Test
    fun `WHEN update base domain action is dispatched THEN base domain state is updated`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState())
        val baseDomain = "mozilla.org"

        store.dispatch(TrustPanelAction.UpdateBaseDomain(baseDomain)).join()

        assertEquals(store.state.baseDomain, baseDomain)
    }

    @Test
    fun `WHEN update detailed tracker category action is dispatched THEN detailed tracker category state is updated`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState())
        val trackerCategory = TrackingProtectionCategory.CRYPTOMINERS

        store.dispatch(TrustPanelAction.UpdateDetailedTrackerCategory(trackerCategory)).join()

        assertEquals(store.state.detailedTrackerCategory, trackerCategory)
    }

    @Test
    fun `WHEN create website permission state method is called THEN website permission state is created`() {
        val settings: Settings = mock()
        val sitePermissions: SitePermissions = mock()
        val permissionHighlights: PermissionHighlightsState = mock()

        initializeSitePermissions(sitePermissions)
        whenever(permissionHighlights.isAutoPlayBlocking).thenReturn(true)

        val state = TrustPanelStore.createWebsitePermissionState(
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
                        autoplayValue = AutoplayValue.AUTOPLAY_BLOCK_AUDIBLE,
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
    fun `WHEN create website permission state method is called THEN te AUTOPLAY_AUDIBLE and AUTOPLAY_INAUDIBLE permissions aren't included`() {
        val settings: Settings = mock()
        val sitePermissions: SitePermissions = mock()
        val permissionHighlights: PermissionHighlightsState = mock()

        initializeSitePermissions(sitePermissions)
        whenever(permissionHighlights.isAutoPlayBlocking).thenReturn(true)

        val state = TrustPanelStore.createWebsitePermissionState(
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
    fun `GIVEN site permissions are null and autoplay is not blocking WHEN create website permission state method is called THEN autoplay isn't visible`() {
        val settings: Settings = mock()
        val permissionHighlights: PermissionHighlightsState = mock()

        whenever(permissionHighlights.isAutoPlayBlocking).thenReturn(false)
        whenever(settings.getSitePermissionsPhoneFeatureAction(any(), any())).thenReturn(ASK_TO_ALLOW)

        val state = TrustPanelStore.createWebsitePermissionState(
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
                autoplayValue = AutoplayValue.AUTOPLAY_BLOCK_AUDIBLE,
                isVisible = false,
                deviceFeature = PhoneFeature.AUTOPLAY,
            ),
        )
    }

    @Test
    fun `WHEN update site permissions action is dispatched THEN site permissions state is updated`() = runTest {
        val store = TrustPanelStore(initialState = TrustPanelState())
        val newSitePermissions: SitePermissions = mock()

        store.dispatch(TrustPanelAction.UpdateSitePermissions(newSitePermissions)).join()

        assertEquals(store.state.sitePermissions, newSitePermissions)
    }

    @Test
    fun `WHEN grant permission blocked by android action is dispatched THEN permissions blocked by android state is updated`() = runTest {
        val toggleablePermission = WebsitePermission.Toggleable(
            isEnabled = true,
            isBlockedByAndroid = true,
            isVisible = true,
            deviceFeature = PhoneFeature.CAMERA,
        )

        val store = TrustPanelStore(
            initialState = TrustPanelState(
                websitePermissionsState = mapOf(PhoneFeature.CAMERA to toggleablePermission),
            ),
        )

        store.dispatch(TrustPanelAction.WebsitePermissionAction.GrantPermissionBlockedByAndroid(PhoneFeature.CAMERA)).join()

        assertEquals(
            (store.state.websitePermissionsState[PhoneFeature.CAMERA]as? WebsitePermission.Toggleable)
                ?.isBlockedByAndroid,
            false,
        )
    }

    @Test
    fun `WHEN toggle permission action is dispatched THEN permission enabled state is updated`() = runTest {
        val toggleablePermission = WebsitePermission.Toggleable(
            isEnabled = true,
            isBlockedByAndroid = true,
            isVisible = true,
            deviceFeature = PhoneFeature.CAMERA,
        )

        val store = TrustPanelStore(
            initialState = TrustPanelState(
                websitePermissionsState = mapOf(PhoneFeature.CAMERA to toggleablePermission),
            ),
        )

        store.dispatch(TrustPanelAction.WebsitePermissionAction.TogglePermission(PhoneFeature.CAMERA)).join()

        assertEquals(
            (store.state.websitePermissionsState[PhoneFeature.CAMERA]as? WebsitePermission.Toggleable)
                ?.isEnabled,
            false,
        )
    }

    @Test
    fun `WHEN change autoplay action is dispatched THEN autoplay value state is updated`() = runTest {
        val toggleablePermission = WebsitePermission.Autoplay(
            autoplayValue = AutoplayValue.AUTOPLAY_BLOCK_AUDIBLE,
            isVisible = true,
            deviceFeature = PhoneFeature.CAMERA,
        )

        val store = TrustPanelStore(
            initialState = TrustPanelState(
                websitePermissionsState = mapOf(PhoneFeature.AUTOPLAY to toggleablePermission),
            ),
        )

        store.dispatch(
            TrustPanelAction.WebsitePermissionAction.ChangeAutoplay(AutoplayValue.AUTOPLAY_ALLOW_ALL),
        ).join()

        assertEquals(
            (store.state.websitePermissionsState[PhoneFeature.AUTOPLAY]as? WebsitePermission.Autoplay)
                ?.autoplayValue,
            AutoplayValue.AUTOPLAY_ALLOW_ALL,
        )
    }

    private fun initializeSitePermissions(
        sitePermissions: SitePermissions,
    ) {
        whenever(sitePermissions.camera).thenReturn(SitePermissions.Status.NO_DECISION)
        whenever(sitePermissions.microphone).thenReturn(SitePermissions.Status.NO_DECISION)
        whenever(sitePermissions.notification).thenReturn(SitePermissions.Status.NO_DECISION)
        whenever(sitePermissions.location).thenReturn(SitePermissions.Status.ALLOWED) // Only location allowed
        whenever(sitePermissions.localStorage).thenReturn(SitePermissions.Status.NO_DECISION)
        whenever(sitePermissions.crossOriginStorageAccess).thenReturn(SitePermissions.Status.NO_DECISION)
        whenever(sitePermissions.mediaKeySystemAccess).thenReturn(SitePermissions.Status.NO_DECISION)
    }
}

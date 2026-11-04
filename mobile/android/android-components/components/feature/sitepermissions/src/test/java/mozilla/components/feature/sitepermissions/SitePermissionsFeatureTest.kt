/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.sitepermissions

import android.Manifest.permission.ACCESS_COARSE_LOCATION
import android.Manifest.permission.ACCESS_FINE_LOCATION
import android.content.pm.PackageManager.PERMISSION_DENIED
import android.content.pm.PackageManager.PERMISSION_GRANTED
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.FragmentTransaction
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.net.URL
import java.security.InvalidParameterException
import java.util.UUID
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.AutoPlayAudibleBlockingAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.AutoPlayInAudibleBlockingAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.AutoPlayInAudibleChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.CameraChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.LocalDeviceAccessChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.LocalNetworkAccessChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.LocationChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.MediaKeySystemAccesChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.MicrophoneChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.NotificationChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.PersistentStorageChangedAction
import mozilla.components.browser.state.action.SystemPermissionRequestAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.permission.Permission
import mozilla.components.concept.engine.permission.Permission.AppAudio
import mozilla.components.concept.engine.permission.Permission.ContentAudioCapture
import mozilla.components.concept.engine.permission.Permission.ContentAudioMicrophone
import mozilla.components.concept.engine.permission.Permission.ContentAutoPlayAudible
import mozilla.components.concept.engine.permission.Permission.ContentAutoPlayInaudible
import mozilla.components.concept.engine.permission.Permission.ContentCrossOriginStorageAccess
import mozilla.components.concept.engine.permission.Permission.ContentGeoLocation
import mozilla.components.concept.engine.permission.Permission.ContentLocalDeviceAccess
import mozilla.components.concept.engine.permission.Permission.ContentLocalNetworkAccess
import mozilla.components.concept.engine.permission.Permission.ContentMediaKeySystemAccess
import mozilla.components.concept.engine.permission.Permission.ContentNotification
import mozilla.components.concept.engine.permission.Permission.ContentPersistentStorage
import mozilla.components.concept.engine.permission.Permission.ContentVideoCamera
import mozilla.components.concept.engine.permission.Permission.ContentVideoCapture
import mozilla.components.concept.engine.permission.Permission.Generic
import mozilla.components.concept.engine.permission.PermissionRequest
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.engine.permission.SitePermissions.AutoplayStatus
import mozilla.components.concept.engine.permission.SitePermissions.Status.ALLOWED
import mozilla.components.concept.engine.permission.SitePermissions.Status.BLOCKED
import mozilla.components.concept.engine.permission.SitePermissions.Status.NO_DECISION
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.feature.tabs.TabsUseCases.SelectOrAddUseCase
import mozilla.components.support.base.Component.FEATURE_SITEPERMISSIONS
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.base.feature.OnNeedToRequestPermissions
import mozilla.components.support.test.any
import mozilla.components.support.test.eq
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers
import org.mockito.Mockito.anyBoolean
import org.mockito.Mockito.anyString
import org.mockito.Mockito.atLeastOnce
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class SitePermissionsFeatureTest {
    private lateinit var sitePermissionFeature: SitePermissionsFeature
    private lateinit var mockOnNeedToRequestPermissions: OnNeedToRequestPermissions
    private lateinit var mockStorage: SitePermissionsStorage
    private lateinit var mockFragmentManager: FragmentManager
    private lateinit var browserStore: BrowserStore
    private lateinit var mockContentState: ContentState
    private lateinit var mockPermissionRequest: PermissionRequest
    private lateinit var mockAppPermissionRequest: PermissionRequest
    private lateinit var mockSitePermissionRules: SitePermissionsRules
    private lateinit var selectedTab: TabSessionState
    private val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()

    private val testDispatcher = StandardTestDispatcher()
    private val testScope = TestScope(testDispatcher)

    companion object {
        const val SESSION_ID = "testSessionId"
        const val URL = "http://www.example.com"
        const val PERMISSION_ID = "PERMISSION_ID"
    }

    @Before
    fun setup() {
        mockOnNeedToRequestPermissions = mock()
        mockStorage = mock()
        mockFragmentManager = mockFragmentManager()
        mockContentState = mock()
        mockPermissionRequest = mock()
        doReturn(true).`when`(mockPermissionRequest).containsVideoAndAudioSources()
        mockAppPermissionRequest = mock()
        mockSitePermissionRules = mock()

        selectedTab =
            mozilla.components.browser.state.state.createTab(
                url = "https://www.mozilla.org",
                id = SESSION_ID,
            )
        val mockEngine = mock<Engine>()
        whenever(mockEngine.createSession()).thenReturn(mock())
        browserStore =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(selectedTab),
                        selectedTabId = selectedTab.id,
                    ),
                middleware = listOf(captureActionsMiddleware) + EngineMiddleware.create(engine = mockEngine),
            )

        sitePermissionFeature =
            spy(
                SitePermissionsFeature(
                    context = testContext,
                    sitePermissionsRules = mockSitePermissionRules,
                    onNeedToRequestPermissions = mockOnNeedToRequestPermissions,
                    storage = mockStorage,
                    fragmentManager = mockFragmentManager,
                    onShouldShowRequestPermissionRationale = { false },
                    store = browserStore,
                    sessionId = SESSION_ID,
                    coroutineScope = testScope,
                    mainDispatcher = testDispatcher,
                )
            )
    }

    @Test
    fun `GIVEN an url WHEN calling trimOriginHttpsSchemeAndPort THEN default https scheme and default port are trimmed`() {
        assertEquals(
            sitePermissionFeature.trimOriginHttpsSchemeAndPort("https://https-scheme-https-port.test:443"),
            "https-scheme-https-port.test",
        )

        assertEquals(
            sitePermissionFeature.trimOriginHttpsSchemeAndPort("https://https-scheme-random-port.test:8080"),
            "https-scheme-random-port.test:8080",
        )

        assertEquals(
            sitePermissionFeature.trimOriginHttpsSchemeAndPort("https://https-scheme-nonhttps-port.test:80"),
            "https-scheme-nonhttps-port.test:80",
        )

        assertEquals(
            sitePermissionFeature.trimOriginHttpsSchemeAndPort("http://http-scheme-http-port.test:80"),
            "http://http-scheme-http-port.test",
        )
    }

    @Test
    fun `GIVEN a tab load THEN stale permission indicators should be clear up and temporary permissions`() =
        runTest(testDispatcher) {
            sitePermissionFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(sitePermissionFeature).setupLoadingCollector()

            // when
            browserStore.dispatch(ContentAction.UpdateLoadingStateAction(SESSION_ID, true))
            testDispatcher.scheduler.advanceUntilIdle()

            // then
            captureActionsMiddleware.assertFirstAction(UpdatePermissionHighlightsStateAction.Reset::class) { action ->
                assertEquals(SESSION_ID, action.tabId)
            }

            verify(mockStorage).clearTemporaryPermissions()
        }

    @Test
    fun `GIVEN a tab load after stop is called THEN none stale permission indicators should be clear up`() {
        sitePermissionFeature.start()

        verify(sitePermissionFeature).setupLoadingCollector()

        sitePermissionFeature.stop()

        // when
        browserStore.dispatch(ContentAction.UpdateLoadingStateAction(SESSION_ID, true))

        verify(mockStorage).clearTemporaryPermissions()

        // then
        captureActionsMiddleware.assertNotDispatched(UpdatePermissionHighlightsStateAction.Reset::class)
    }

    @Test
    fun `GIVEN a sessionId WHEN calling consumePermissionRequest THEN dispatch ConsumePermissionsRequest action with given id`() {
        // when
        sitePermissionFeature.consumePermissionRequest(mockPermissionRequest, "sessionIdTest")

        // then
        captureActionsMiddleware.assertFirstAction(ContentAction.ConsumePermissionsRequest::class) { action ->
            assertEquals("sessionIdTest", action.sessionId)
            assertEquals(mockPermissionRequest, action.permissionRequest)
        }
    }

    @Test
    fun `GIVEN no sessionId WHEN calling consumePermissionRequest THEN dispatch ConsumePermissionsRequest action with selected tab`() {
        // when
        sitePermissionFeature.consumePermissionRequest(mockPermissionRequest)

        // then
        captureActionsMiddleware.assertFirstAction(ContentAction.ConsumePermissionsRequest::class) { action ->
            assertEquals(selectedTab.id, action.sessionId)
            assertEquals(mockPermissionRequest, action.permissionRequest)
        }
    }

    @Test
    fun `GIVEN a sessionId WHEN calling consumeAppPermissionRequest THEN dispatch ConsumeAppPermissionsRequest action with given id`() {
        // when
        sitePermissionFeature.consumeAppPermissionRequest(mockAppPermissionRequest, "sessionIdTest")

        // then
        captureActionsMiddleware.assertFirstAction(ContentAction.ConsumeAppPermissionsRequest::class) { action ->
            assertEquals("sessionIdTest", action.sessionId)
            assertEquals(mockAppPermissionRequest, action.appPermissionRequest)
        }
    }

    @Test
    fun `GIVEN no sessionId WHEN calling consumeAppPermissionRequest THEN dispatch ConsumeAppPermissionsRequest action with selected tab`() {
        // when
        sitePermissionFeature.consumeAppPermissionRequest(mockAppPermissionRequest)

        // then
        captureActionsMiddleware.assertFirstAction(ContentAction.ConsumeAppPermissionsRequest::class) { action ->
            assertEquals(selectedTab.id, action.sessionId)
            assertEquals(mockAppPermissionRequest, action.appPermissionRequest)
        }
    }

    @Test
    fun `GIVEN an appPermissionRequest with all granted permissions WHEN onPermissionsResult() THEN grant() and consumeAppPermissionRequest() are called`() {
        // given
        doReturn(mockAppPermissionRequest).`when`(sitePermissionFeature).findRequestedAppPermission(any())

        // when
        sitePermissionFeature.onPermissionsResult(arrayOf("permission"), arrayOf(PERMISSION_GRANTED).toIntArray())

        // then
        verify(mockAppPermissionRequest).grant()
        verify(sitePermissionFeature).consumeAppPermissionRequest(mockAppPermissionRequest)
    }

    @Test
    fun `GIVEN an appPermissionRequest for both fine and coarse location WHEN either permission is granted THEN permission request is granted`() {
        // given
        doReturn(mockAppPermissionRequest).`when`(sitePermissionFeature).findRequestedAppPermission(any())

        // when
        sitePermissionFeature.onPermissionsResult(
            arrayOf(ACCESS_FINE_LOCATION, ACCESS_COARSE_LOCATION),
            arrayOf(PERMISSION_DENIED, PERMISSION_GRANTED).toIntArray(),
        )

        // then
        verify(mockAppPermissionRequest).grant()
        verify(sitePermissionFeature).consumeAppPermissionRequest(mockAppPermissionRequest)
    }

    @Test
    fun `GIVEN an appPermissionRequest for fine and coarse location and another permission WHEN either location permission is granted and the third is denied THEN permission request is not granted`() {
        // given
        doReturn(mockAppPermissionRequest).`when`(sitePermissionFeature).findRequestedAppPermission(any())

        // when
        sitePermissionFeature.onPermissionsResult(
            arrayOf(ACCESS_FINE_LOCATION, ACCESS_COARSE_LOCATION, "random permission"),
            arrayOf(PERMISSION_DENIED, PERMISSION_GRANTED, PERMISSION_DENIED).toIntArray(),
        )

        // then
        verify(mockAppPermissionRequest).reject()
        verify(sitePermissionFeature).consumeAppPermissionRequest(mockAppPermissionRequest)
    }

    @Test
    fun `GIVEN an appPermissionRequest for fine and coarse location and another permission WHEN either location permission is granted and the third is granted THEN permission request is granted`() {
        // given
        doReturn(mockAppPermissionRequest).`when`(sitePermissionFeature).findRequestedAppPermission(any())

        // when
        sitePermissionFeature.onPermissionsResult(
            arrayOf(ACCESS_FINE_LOCATION, ACCESS_COARSE_LOCATION, "random permission"),
            arrayOf(PERMISSION_DENIED, PERMISSION_GRANTED, PERMISSION_GRANTED).toIntArray(),
        )

        // then
        verify(mockAppPermissionRequest).grant()
        verify(sitePermissionFeature).consumeAppPermissionRequest(mockAppPermissionRequest)
    }

    @Test
    fun `GIVEN an appPermissionRequest for two other permissions WHEN either permission is granted THEN permission request is not granted`() {
        // given
        doReturn(mockAppPermissionRequest).`when`(sitePermissionFeature).findRequestedAppPermission(any())

        // when
        sitePermissionFeature.onPermissionsResult(
            arrayOf("random permission 1", "random permission 2"),
            arrayOf(PERMISSION_DENIED, PERMISSION_GRANTED).toIntArray(),
        )

        // then
        verify(mockAppPermissionRequest).reject()
        verify(sitePermissionFeature).consumeAppPermissionRequest(mockAppPermissionRequest)
    }

    @Test
    fun `GIVEN an appPermissionRequest with blocked permissions and !onShouldShowRequestPermissionRationale WHEN onPermissionsResult() THEN reject(), storeSitePermissions, consume are called`() {
        // given
        doReturn(mockAppPermissionRequest).`when`(sitePermissionFeature).findRequestedAppPermission(any())
        doNothing().`when`(sitePermissionFeature).storeSitePermissions(any(), any(), any(), any())

        // when
        sitePermissionFeature.onPermissionsResult(
            arrayOf("permission1", "permission2"),
            arrayOf(PERMISSION_DENIED, PERMISSION_DENIED).toIntArray(),
        )

        // then
        verify(mockAppPermissionRequest).reject()
        verify(sitePermissionFeature, times(2))
            .storeSitePermissions(eq(selectedTab.content), eq(mockAppPermissionRequest), eq(BLOCKED), any())
        verify(sitePermissionFeature).consumeAppPermissionRequest(mockAppPermissionRequest)
    }

    @Test
    fun `GIVEN shouldStore true WHEN onContentPermissionGranted() THEN storeSitePermissions() called`() {
        // given
        doReturn(listOf(ContentNotification())).`when`(mockPermissionRequest).permissions
        doNothing().`when`(sitePermissionFeature).storeSitePermissions(any(), any(), any(), any())

        // when
        sitePermissionFeature.onContentPermissionGranted(mockPermissionRequest, true)

        // then
        verify(sitePermissionFeature)
            .storeSitePermissions(eq(selectedTab.content), eq(mockPermissionRequest), eq(ALLOWED), any())
    }

    @Test
    fun `GIVEN shouldStore false WHEN onContentPermissionGranted() THEN storeSitePermissions() MUST NOT BE called`() {
        // given
        doNothing().`when`(sitePermissionFeature).storeSitePermissions(any(), any(), any(), any())

        // when
        sitePermissionFeature.onContentPermissionGranted(mockPermissionRequest, false)

        // then
        verify(sitePermissionFeature, never())
            .storeSitePermissions(eq(selectedTab.content), eq(mockPermissionRequest), eq(ALLOWED), any())
    }

    @Test
    fun `GIVEN permissionRequest WHEN onPositiveButtonPress() THEN consumePermissionRequest, onContentPermissionGranted are called`() {
        // given
        doReturn(listOf(ContentNotification())).`when`(mockPermissionRequest).permissions
        doNothing().`when`(sitePermissionFeature).consumePermissionRequest(any(), any())
        doNothing().`when`(sitePermissionFeature).onContentPermissionGranted(mockPermissionRequest, true)
        doReturn(mockPermissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())

        // when
        sitePermissionFeature.onPositiveButtonPress(PERMISSION_ID, SESSION_ID, true)

        // then
        verify(sitePermissionFeature).consumePermissionRequest(mockPermissionRequest, SESSION_ID)
        verify(sitePermissionFeature)
            .onContentPermissionGranted(
                eq(mockPermissionRequest),
                eq(true),
                any(),
            )
    }

    @Test
    fun `GIVEN a permission prompt WHEN one permission is allowed THEN emit a fact`() {
        CollectionProcessor.withFactCollection { facts ->
            doNothing().`when`(sitePermissionFeature).consumePermissionRequest(any(), any())
            doNothing()
                .`when`(sitePermissionFeature)
                .onContentPermissionGranted(eq(mockPermissionRequest), eq(true), any())
            doReturn(listOf(ContentCrossOriginStorageAccess())).`when`(mockPermissionRequest).permissions
            doReturn(mockPermissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())

            sitePermissionFeature.onPositiveButtonPress(PERMISSION_ID, SESSION_ID, true)

            assertEquals(1, facts.size)
            assertEquals(FEATURE_SITEPERMISSIONS, facts[0].component)
            assertEquals(Action.CONFIRM, facts[0].action)
            assertEquals(SitePermissionsFacts.Items.PERMISSIONS, facts[0].item)
            assertEquals(ContentCrossOriginStorageAccess().id, facts[0].value)
        }
    }

    @Test
    fun `GIVEN a permission prompt WHEN multiple permission are allowed THEN emit a fact`() {
        CollectionProcessor.withFactCollection { facts ->
            doReturn(listOf(ContentVideoCapture(), ContentVideoCamera(), ContentAudioMicrophone()))
                .`when`(mockPermissionRequest)
                .permissions
            doReturn(true).`when`(mockPermissionRequest).containsVideoAndAudioSources()
            doReturn(mockPermissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())

            sitePermissionFeature.onPositiveButtonPress(PERMISSION_ID, SESSION_ID, true)

            assertEquals(1, facts.size)
            assertEquals(FEATURE_SITEPERMISSIONS, facts[0].component)
            assertEquals(Action.CONFIRM, facts[0].action)
            assertEquals(SitePermissionsFacts.Items.PERMISSIONS, facts[0].item)
            assertEquals(
                listOf(ContentVideoCapture(), ContentVideoCamera(), ContentAudioMicrophone()).joinToString { it.id!! },
                facts[0].value,
            )
        }
    }

    @Test
    fun `GIVEN permissionRequest WHEN onNegativeButtonPress() THEN consumePermissionRequest, onContentPermissionDeny are called`() {
        // given
        doNothing().`when`(sitePermissionFeature).consumePermissionRequest(any(), any())
        doNothing().`when`(sitePermissionFeature).onContentPermissionDeny(mockPermissionRequest, true)
        doReturn(mockPermissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())

        // when
        sitePermissionFeature.onNegativeButtonPress(PERMISSION_ID, SESSION_ID, true)

        // then
        verify(sitePermissionFeature).consumePermissionRequest(mockPermissionRequest, SESSION_ID)
        verify(sitePermissionFeature).onContentPermissionDeny(mockPermissionRequest, true)
    }

    @Test
    fun `GIVEN a permission prompt WHEN the permission is denied THEN emit a fact`() {
        CollectionProcessor.withFactCollection { facts ->
            doNothing().`when`(sitePermissionFeature).consumePermissionRequest(any(), any())
            doNothing().`when`(sitePermissionFeature).onContentPermissionDeny(mockPermissionRequest, true)
            doReturn(listOf(ContentGeoLocation())).`when`(mockPermissionRequest).permissions
            doReturn(mockPermissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())

            sitePermissionFeature.onNegativeButtonPress(PERMISSION_ID, SESSION_ID, true)

            assertEquals(1, facts.size)
            assertEquals(FEATURE_SITEPERMISSIONS, facts[0].component)
            assertEquals(Action.CANCEL, facts[0].action)
            assertEquals(SitePermissionsFacts.Items.PERMISSIONS, facts[0].item)
            assertEquals(ContentGeoLocation().id, facts[0].value)
        }
    }

    @Test
    fun `GIVEN a permission prompt WHEN multiple permissions are denied THEN emit a fact`() {
        CollectionProcessor.withFactCollection { facts ->
            doReturn(listOf(ContentVideoCapture(), ContentVideoCamera(), ContentAudioMicrophone()))
                .`when`(mockPermissionRequest)
                .permissions
            doReturn(true).`when`(mockPermissionRequest).containsVideoAndAudioSources()
            doReturn(mockPermissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())

            sitePermissionFeature.onNegativeButtonPress(PERMISSION_ID, SESSION_ID, true)

            assertEquals(1, facts.size)
            assertEquals(FEATURE_SITEPERMISSIONS, facts[0].component)
            assertEquals(Action.CANCEL, facts[0].action)
            assertEquals(SitePermissionsFacts.Items.PERMISSIONS, facts[0].item)
            assertEquals(
                listOf(ContentVideoCapture(), ContentVideoCamera(), ContentAudioMicrophone()).joinToString { it.id!! },
                facts[0].value,
            )
        }
    }

    @Test
    fun `GIVEN permissionRequest WHEN onDismiss() THEN consumePermissionRequest, onContentPermissionDeny are called`() {
        // given
        doNothing().`when`(sitePermissionFeature).consumePermissionRequest(any(), any())
        doNothing().`when`(sitePermissionFeature).onContentPermissionDeny(mockPermissionRequest, false)
        doReturn(mockPermissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())

        // when
        sitePermissionFeature.onDismiss(PERMISSION_ID, SESSION_ID)

        // then
        verify(sitePermissionFeature).consumePermissionRequest(mockPermissionRequest, SESSION_ID)
        verify(sitePermissionFeature).onContentPermissionDeny(mockPermissionRequest, false)
    }

    @Test
    fun `GIVEN permissionRequest WHEN onLearnMorePress() THEN consumePermissionRequest, onContentPermissionDeny are called and SelectOrAddUseCase is used`() {
        // given
        val permission: ContentCrossOriginStorageAccess = mock()
        val permissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(permission))
        }
        doNothing().`when`(sitePermissionFeature).consumePermissionRequest(any(), any())
        doNothing().`when`(sitePermissionFeature).onContentPermissionDeny(mockPermissionRequest, true)
        doReturn(permissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())
        doReturn(mock<SelectOrAddUseCase>()).`when`(sitePermissionFeature).selectOrAddUseCase

        // when
        sitePermissionFeature.onLearnMorePress(
            permissionId = PERMISSION_ID,
            sessionId = SESSION_ID,
            learnMoreLink = "https://mozilla.org",
        )

        // then
        verify(sitePermissionFeature).consumePermissionRequest(permissionRequest, SESSION_ID)
        verify(sitePermissionFeature).onContentPermissionDeny(permissionRequest, false)
        verify(sitePermissionFeature.selectOrAddUseCase)
            .invoke(
                url = "https://mozilla.org",
                private = false,
                source = SessionState.Source.Internal.TextSelection,
            )
    }

    @Test
    fun `GIVEN permissionRequest WHEN onLearnMorePress() THEN SelectOrAddUseCase is not called if learn more link is empty`() {
        // given
        val permission: ContentCrossOriginStorageAccess = mock()
        val permissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(permission))
        }
        doNothing().`when`(sitePermissionFeature).consumePermissionRequest(any(), any())
        doNothing().`when`(sitePermissionFeature).onContentPermissionDeny(mockPermissionRequest, true)
        doReturn(permissionRequest).`when`(sitePermissionFeature).findRequestedPermission(anyString())
        doReturn(mock<SelectOrAddUseCase>()).`when`(sitePermissionFeature).selectOrAddUseCase

        // given the learn more link provider returns an empty string
        sitePermissionFeature.learnMoreUrlProvider = FakeSitePermissionsLearnMoreUrlProvider(expectedLink = "")

        // when
        sitePermissionFeature.onLearnMorePress(
            permissionId = PERMISSION_ID,
            sessionId = SESSION_ID,
            learnMoreLink = "",
        )

        // then verify that SelectOrAddUseCase is never called
        verify(sitePermissionFeature.selectOrAddUseCase, never())
            .invoke(
                url = anyString(),
                private = anyBoolean(),
                source = any(),
                flags = any(),
                ignoreFragment = anyBoolean(),
            )
    }

    @Test
    fun `GIVEN a new permissionRequest WHEN storeSitePermissions() THEN save(permissionRequest) is called`() = runTest {
        // given
        val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
        doReturn(null)
            .`when`(mockStorage)
            .findSitePermissionsBy(ArgumentMatchers.anyString(), anyBoolean(), anyBoolean())
        doNothing()
            .`when`(sitePermissionFeature)
            .updatePermissionToolbarIndicator(
                any(),
                any(),
                anyBoolean(),
            )
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(Permission.AppAudio(id = "permission")))
            whenever(uri).thenReturn(URL)
        }
        doReturn(sitePermissions).`when`(sitePermissionFeature).updateSitePermissionsStatus(any(), any(), any())

        // when
        sitePermissionFeature.storeSitePermissions(
            mockContentState,
            mockPermissionRequest,
            ALLOWED,
            this,
        )
        testScheduler.advanceUntilIdle()

        // then
        verify(mockStorage).save(sitePermissions, mockPermissionRequest, private = selectedTab.content.private)
        verify(sitePermissionFeature)
            .updatePermissionToolbarIndicator(
                mockPermissionRequest,
                ALLOWED,
                true,
            )
    }

    @Test
    fun `GIVEN an already saved permissionRequest WHEN storeSitePermissions() THEN update(permissionRequest) is called`() =
        runTest {
            // given
            val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
            doReturn(sitePermissions)
                .`when`(mockStorage)
                .findSitePermissionsBy(ArgumentMatchers.anyString(), anyBoolean(), anyBoolean())
            val mockPermissionRequest: PermissionRequest = mock {
                whenever(uri).thenReturn(URL)
                whenever(permissions).thenReturn(listOf(AppAudio(id = "permission")))
            }
            doReturn(sitePermissions).`when`(sitePermissionFeature).updateSitePermissionsStatus(any(), any(), any())

            // when
            sitePermissionFeature.storeSitePermissions(
                mockContentState,
                mockPermissionRequest,
                ALLOWED,
                this,
            )
            testScheduler.advanceUntilIdle()

            // then
            verify(mockStorage).update(eq(sitePermissions), anyBoolean())
        }

    @Test
    fun `GIVEN a permissionRequest WITH a private tab WHEN storeSitePermissions() THEN save or update MUST NOT BE called`() =
        runTest {
            // then
            sitePermissionFeature.storeSitePermissions(
                selectedTab.content.copy(private = true),
                mockPermissionRequest,
                ALLOWED,
                this,
            )

            // when
            verify(mockStorage, never()).save(any(), any(), anyBoolean())
            verify(mockStorage, never()).update(any(), anyBoolean())
        }

    @Test
    fun `GIVEN a permanent permissionRequest WHEN onContentPermissionDeny THEN reject(), storeSitePermissions are called`() {
        // given
        doNothing().`when`(mockPermissionRequest).reject()
        doNothing().`when`(sitePermissionFeature).storeSitePermissions(any(), any(), any(), any())

        // then
        sitePermissionFeature.onContentPermissionDeny(mockPermissionRequest, true)

        // when
        verify(mockPermissionRequest).reject()
        verify(sitePermissionFeature)
            .storeSitePermissions(
                eq(selectedTab.content),
                eq(mockPermissionRequest),
                eq(BLOCKED),
                any(),
            )
    }

    @Test
    fun `GIVEN a temporary permissionRequest WHEN denying THEN store it on memory`() {
        doNothing().`when`(mockPermissionRequest).reject()
        doNothing().`when`(sitePermissionFeature).storeSitePermissions(any(), any(), any(), any())

        sitePermissionFeature.onContentPermissionDeny(mockPermissionRequest, shouldStore = false)

        verify(mockPermissionRequest).reject()
        verify(mockStorage).saveTemporary(mockPermissionRequest)

        verify(sitePermissionFeature, never())
            .storeSitePermissions(
                eq(selectedTab.content),
                eq(mockPermissionRequest),
                eq(BLOCKED),
                any(),
            )
    }

    @Test
    fun `GIVEN a temporary permissionRequest WHEN granting THEN store it on memory`() {
        doNothing().`when`(mockPermissionRequest).reject()
        doNothing().`when`(sitePermissionFeature).storeSitePermissions(any(), any(), any(), any())

        sitePermissionFeature.onContentPermissionGranted(mockPermissionRequest, shouldStore = false)

        verify(mockPermissionRequest).grant()
        verify(mockStorage).saveTemporary(mockPermissionRequest)

        verify(sitePermissionFeature, never())
            .storeSitePermissions(
                eq(selectedTab.content),
                eq(mockPermissionRequest),
                eq(ALLOWED),
                any(),
            )
    }

    @Test
    fun `GIVEN a media permissionRequest without all media permissions granted WHEN onContentPermissionRequested() THEN reject, consumePermissionRequest are called `() =
        runTest {
            // given
            val mockPermissionRequest: PermissionRequest = mock {
                whenever(permissions).thenReturn(listOf(ContentVideoCamera(id = "permission")))
            }
            doNothing().`when`(mockPermissionRequest).reject()

            // when
            sitePermissionFeature.onContentPermissionRequested(mockPermissionRequest, URL)

            // then
            verify(mockPermissionRequest).reject()
            verify(sitePermissionFeature).consumePermissionRequest(mockPermissionRequest)
        }

    @Test
    fun `GIVEN sessionId which does not match a selected or custom tab WHEN onContentPermissionRequested() THEN reject, consumePermissionRequest are called `() =
        runTest {
            val mockPermissionRequest: PermissionRequest = mock {
                whenever(permissions).thenReturn(listOf(ContentVideoCamera(id = "permission")))
            }

            doNothing().`when`(mockPermissionRequest).reject()

            sitePermissionFeature.sessionId = null

            sitePermissionFeature.onContentPermissionRequested(mockPermissionRequest, URL)

            verify(mockPermissionRequest).reject()
            verify(sitePermissionFeature).consumePermissionRequest(mockPermissionRequest)
        }

    @Test
    fun `GIVEN location permissionRequest and shouldApplyRules is true WHEN onContentPermissionRequested() THEN handleRuledFlow is called`() =
        runTest {
            // given
            val mockPermissionRequest: PermissionRequest = mock {
                whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
            }
            val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
            val mockedSitePermissionsDialogFragment = SitePermissionsDialogFragment()
            doReturn(sitePermissions)
                .`when`(mockStorage)
                .findSitePermissionsBy(URL, private = selectedTab.content.private)
            doReturn(true).`when`(sitePermissionFeature).shouldApplyRules(any())
            doReturn(mockedSitePermissionsDialogFragment)
                .`when`(sitePermissionFeature)
                .handleRuledFlow(mockPermissionRequest, URL)

            // when
            sitePermissionFeature.onContentPermissionRequested(
                mockPermissionRequest,
                URL,
            )

            // then
            verify(mockStorage).findSitePermissionsBy(URL, private = selectedTab.content.private)
            verify(sitePermissionFeature).handleRuledFlow(mockPermissionRequest, URL)
        }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `GIVEN a prompt is shown WHEN onContentPermissionRequested() THEN notifyShown is called on the permission request`() =
        runTest {
            // given
            val mockPermissionRequest: PermissionRequest = mock {
                whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
            }
            val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
            val mockedSitePermissionsDialogFragment = SitePermissionsDialogFragment()
            doReturn(sitePermissions)
                .`when`(mockStorage)
                .findSitePermissionsBy(URL, private = selectedTab.content.private)
            doReturn(true).`when`(sitePermissionFeature).shouldApplyRules(any())
            doReturn(mockedSitePermissionsDialogFragment)
                .`when`(sitePermissionFeature)
                .handleRuledFlow(mockPermissionRequest, URL)

            // when
            sitePermissionFeature.onContentPermissionRequested(
                mockPermissionRequest,
                URL,
            )

            // then
            verify(mockPermissionRequest).notifyShown()
        }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `GIVEN no prompt is shown WHEN onContentPermissionRequested() THEN notifyShown is not called`() = runTest {
        // given
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
        }
        val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
        doReturn(sitePermissions).`when`(mockStorage).findSitePermissionsBy(URL, private = selectedTab.content.private)
        doReturn(true).`when`(sitePermissionFeature).shouldApplyRules(any())
        doReturn(null).`when`(sitePermissionFeature).handleRuledFlow(mockPermissionRequest, URL)

        // when
        sitePermissionFeature.onContentPermissionRequested(
            mockPermissionRequest,
            URL,
        )

        // then
        verify(mockPermissionRequest, never()).notifyShown()
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `GIVEN fragmentManager isDestroyed WHEN onContentPermissionRequested() THEN prompt is not shown`() = runTest {
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
        }
        val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
        val mockedSitePermissionsDialogFragment = SitePermissionsDialogFragment()
        doReturn(sitePermissions).`when`(mockStorage).findSitePermissionsBy(URL, private = selectedTab.content.private)
        doReturn(true).`when`(sitePermissionFeature).shouldApplyRules(any())
        doReturn(mockedSitePermissionsDialogFragment)
            .`when`(sitePermissionFeature)
            .handleRuledFlow(mockPermissionRequest, URL)
        doReturn(true).`when`(mockFragmentManager).isDestroyed

        val result =
            sitePermissionFeature.onContentPermissionRequested(
                mockPermissionRequest,
                URL,
            )

        assertNull(result)
        verify(mockPermissionRequest, never()).notifyShown()
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `GIVEN fragmentManager isStateSaved WHEN onContentPermissionRequested() THEN prompt is not shown`() = runTest {
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
        }
        val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
        val mockedSitePermissionsDialogFragment = SitePermissionsDialogFragment()
        doReturn(sitePermissions).`when`(mockStorage).findSitePermissionsBy(URL, private = selectedTab.content.private)
        doReturn(true).`when`(sitePermissionFeature).shouldApplyRules(any())
        doReturn(mockedSitePermissionsDialogFragment)
            .`when`(sitePermissionFeature)
            .handleRuledFlow(mockPermissionRequest, URL)
        doReturn(true).`when`(mockFragmentManager).isStateSaved

        val result =
            sitePermissionFeature.onContentPermissionRequested(
                mockPermissionRequest,
                URL,
            )

        assertNull(result)
        verify(mockPermissionRequest, never()).notifyShown()
    }

    @Test
    fun `GIVEN location permissionRequest and shouldApplyRules is false WHEN onContentPermissionRequested() THEN handleNoRuledFlow is called`() =
        runTest {
            // given
            val mockPermissionRequest: PermissionRequest = mock {
                whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
            }
            val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
            val sitePermissionsDialogFragment = SitePermissionsDialogFragment()
            doReturn(sitePermissions)
                .`when`(mockStorage)
                .findSitePermissionsBy(URL, private = selectedTab.content.private)
            doReturn(false).`when`(sitePermissionFeature).shouldApplyRules(any())
            doReturn(sitePermissionsDialogFragment)
                .`when`(sitePermissionFeature)
                .handleNoRuledFlow(sitePermissions, mockPermissionRequest, URL)

            // when
            sitePermissionFeature.onContentPermissionRequested(
                mockPermissionRequest,
                URL,
            )

            // then
            verify(mockStorage).findSitePermissionsBy(URL, private = selectedTab.content.private)
            verify(sitePermissionFeature).handleNoRuledFlow(sitePermissions, mockPermissionRequest, URL)
        }

    @Test
    fun `GIVEN autoplay permissionRequest and shouldApplyRules is false WHEN onContentPermissionRequested() THEN handleNoRuledFlow is called`() =
        runTest {
            // given
            val mockPermissionRequest: PermissionRequest = mock {
                whenever(permissions).thenReturn(listOf(ContentAutoPlayInaudible(id = "permission")))
            }
            val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
            val sitePermissionsDialogFragment = SitePermissionsDialogFragment()

            doReturn(sitePermissions)
                .`when`(mockStorage)
                .findSitePermissionsBy(URL, private = selectedTab.content.private)
            doReturn(false).`when`(sitePermissionFeature).shouldApplyRules(any())
            doReturn(sitePermissionsDialogFragment)
                .`when`(sitePermissionFeature)
                .handleNoRuledFlow(sitePermissions, mockPermissionRequest, URL)

            // when
            sitePermissionFeature.onContentPermissionRequested(
                mockPermissionRequest,
                URL,
            )

            // then
            verify(mockStorage).findSitePermissionsBy(URL, private = selectedTab.content.private)
            verify(sitePermissionFeature).handleNoRuledFlow(sitePermissions, mockPermissionRequest, URL)
        }

    @Test
    fun `GIVEN shouldShowPrompt with isForAutoplay false AND null permissionFromStorage THEN return true`() = runTest {
        // given
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(Permission.ContentGeoLocation(id = "permission")))
        }

        // when
        val shouldShowPrompt = sitePermissionFeature.shouldShowPrompt(mockPermissionRequest, null)

        // then
        assertTrue(shouldShowPrompt)
    }

    @Test
    fun `GIVEN shouldShowPrompt with isForAutoplay true THEN return false`() {
        // given
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(Permission.ContentAutoPlayInaudible(id = "permission")))
        }

        // when
        val shouldShowPrompt = sitePermissionFeature.shouldShowPrompt(mockPermissionRequest, mock())

        // then
        assertFalse(shouldShowPrompt)
    }

    @Test
    fun `GIVEN shouldShowPrompt true WHEN handleNoRuledFlow THEN createPrompt is called`() {
        // given
        val sitePermissionsDialogFragment = SitePermissionsDialogFragment()
        val sitePermissions = SitePermissions(origin = "origin", savedAt = 0)
        doReturn(true).`when`(sitePermissionFeature).shouldShowPrompt(mockPermissionRequest, sitePermissions)
        doReturn(sitePermissionsDialogFragment).`when`(sitePermissionFeature).createPrompt(any(), any())

        // when
        sitePermissionFeature.handleNoRuledFlow(sitePermissions, mockPermissionRequest, URL)

        // then
        verify(sitePermissionFeature).createPrompt(mockPermissionRequest, URL)
    }

    @Test
    fun `GIVEN shouldShowPrompt false and permissionFromStorage not granted WHEN handleNoRuledFlow THEN reject, consumePermissionRequest are called `() {
        // given
        doReturn(false).`when`(sitePermissionFeature).shouldShowPrompt(mockPermissionRequest, null)
        doNothing().`when`(sitePermissionFeature).updatePermissionToolbarIndicator(mockPermissionRequest, BLOCKED)

        // when
        sitePermissionFeature.handleNoRuledFlow(null, mockPermissionRequest, URL)

        // then
        verify(mockPermissionRequest).reject()
        verify(sitePermissionFeature).consumePermissionRequest(mockPermissionRequest)
        verify(sitePermissionFeature)
            .updatePermissionToolbarIndicator(
                mockPermissionRequest,
                BLOCKED,
            )
    }

    @Test
    fun `GIVEN AutoplayAudible request WHEN calling updatePermissionToolbarIndicator THEN dispatch AutoPlayAudibleBlockingAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentAutoPlayAudible(id = "permission")))
        }
        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED)

        captureActionsMiddleware.assertFirstAction(AutoPlayAudibleBlockingAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED)

        captureActionsMiddleware.assertLastAction(AutoPlayAudibleBlockingAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }
    }

    @Test
    fun `GIVEN AutoplayInaudible request WHEN calling updatePermissionToolbarIndicator THEN dispatch AutoPlayInAudibleBlockingAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentAutoPlayInaudible(id = "permission")))
        }
        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED)

        captureActionsMiddleware.assertFirstAction(AutoPlayInAudibleBlockingAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED)

        captureActionsMiddleware.assertLastAction(AutoPlayInAudibleBlockingAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }
    }

    @Test
    fun `GIVEN notification request WHEN calling updatePermissionToolbarIndicator THEN dispatch NotificationChangedAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentNotification(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.Action.BLOCKED).`when`(mockSitePermissionRules).notification

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, false)

        captureActionsMiddleware.assertNotDispatched(NotificationChangedAction::class)

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)

        captureActionsMiddleware.assertFirstAction(NotificationChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)

        captureActionsMiddleware.assertLastAction(NotificationChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN local device access request WHEN calling updatePermissionToolbarIndicator THEN dispatch `() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentLocalDeviceAccess(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.Action.BLOCKED).`when`(mockSitePermissionRules).localDeviceAccess

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, false)
        captureActionsMiddleware.assertNotDispatched(LocalDeviceAccessChangedAction::class)

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)
        captureActionsMiddleware.assertFirstAction(LocalDeviceAccessChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)
        captureActionsMiddleware.assertLastAction(LocalDeviceAccessChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN local network access request WHEN calling updatePermissionToolbarIndicator THEN dispatch `() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentLocalNetworkAccess(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.Action.BLOCKED).`when`(mockSitePermissionRules).localNetworkAccess

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, false)

        captureActionsMiddleware.assertNotDispatched(LocalNetworkAccessChangedAction::class)

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)
        captureActionsMiddleware.assertFirstAction(LocalNetworkAccessChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)
        captureActionsMiddleware.assertLastAction(LocalNetworkAccessChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN camera request WHEN calling updatePermissionToolbarIndicator THEN dispatch CameraChangedAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentVideoCamera(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.Action.BLOCKED).`when`(mockSitePermissionRules).camera

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, false)

        captureActionsMiddleware.assertNotDispatched(CameraChangedAction::class)

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)

        captureActionsMiddleware.assertFirstAction(CameraChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)

        captureActionsMiddleware.assertLastAction(CameraChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN location request WHEN calling updatePermissionToolbarIndicator THEN dispatch CameraChangedAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.Action.BLOCKED).`when`(mockSitePermissionRules).location

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, false)

        captureActionsMiddleware.assertNotDispatched(LocationChangedAction::class)

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)

        captureActionsMiddleware.assertFirstAction(LocationChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)

        captureActionsMiddleware.assertLastAction(LocationChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN microphone request WHEN calling updatePermissionToolbarIndicator THEN dispatch MicrophoneChangedAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentAudioCapture(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.Action.BLOCKED).`when`(mockSitePermissionRules).microphone

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, false)

        captureActionsMiddleware.assertNotDispatched(MicrophoneChangedAction::class)

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)

        captureActionsMiddleware.assertFirstAction(MicrophoneChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)

        captureActionsMiddleware.assertLastAction(MicrophoneChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN persistentStorage request WHEN calling updatePermissionToolbarIndicator THEN dispatch PersistentStorageChangedAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentPersistentStorage(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.Action.BLOCKED).`when`(mockSitePermissionRules).persistentStorage

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, false)

        captureActionsMiddleware.assertNotDispatched(PersistentStorageChangedAction::class)

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)

        captureActionsMiddleware.assertFirstAction(PersistentStorageChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)

        captureActionsMiddleware.assertLastAction(PersistentStorageChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN mediaKeySystemAccess request WHEN calling updatePermissionToolbarIndicator THEN dispatch MediaKeySystemAccesChangedAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentMediaKeySystemAccess(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.Action.BLOCKED).`when`(mockSitePermissionRules).mediaKeySystemAccess

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, false)

        captureActionsMiddleware.assertNotDispatched(MediaKeySystemAccesChangedAction::class)

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)

        captureActionsMiddleware.assertFirstAction(MediaKeySystemAccesChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)

        captureActionsMiddleware.assertLastAction(MediaKeySystemAccesChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN autoplayAudible request WHEN calling updatePermissionToolbarIndicator THEN dispatch AutoPlayAudibleChangedAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentAutoPlayAudible(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.AutoplayAction.BLOCKED).`when`(mockSitePermissionRules).autoplayAudible

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)

        captureActionsMiddleware.assertFirstAction(
            UpdatePermissionHighlightsStateAction.AutoPlayAudibleChangedAction::class
        ) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)
        captureActionsMiddleware.assertLastAction(
            UpdatePermissionHighlightsStateAction.AutoPlayAudibleChangedAction::class
        ) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN autoplayInaudible request WHEN calling updatePermissionToolbarIndicator THEN dispatch AutoPlayInAudibleChangedAction`() {
        val tab1 = createTab("https://www.mozilla.org", id = "1")
        val request: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentAutoPlayInaudible(id = "permission")))
        }

        doReturn(tab1).`when`(sitePermissionFeature).getCurrentTabState()
        doReturn(SitePermissionsRules.AutoplayAction.BLOCKED).`when`(mockSitePermissionRules).autoplayInaudible

        sitePermissionFeature.updatePermissionToolbarIndicator(request, BLOCKED, true)

        captureActionsMiddleware.assertFirstAction(AutoPlayInAudibleChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertFalse(action.value)
        }

        sitePermissionFeature.updatePermissionToolbarIndicator(request, ALLOWED, true)

        captureActionsMiddleware.assertLastAction(AutoPlayInAudibleChangedAction::class) { action ->
            assertEquals(tab1.id, action.tabId)
            assertTrue(action.value)
        }
    }

    @Test
    fun `GIVEN shouldShowPrompt false and permissionFromStorage granted WHEN handleNoRuledFlow THEN grant, consumePermissionRequest are called `() {
        // given
        val sitePermissions = SitePermissions(origin = "origin", savedAt = 0, location = ALLOWED)
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
        }
        doNothing().`when`(sitePermissionFeature).updatePermissionToolbarIndicator(mockPermissionRequest, ALLOWED, true)

        doReturn(false).`when`(sitePermissionFeature).shouldShowPrompt(mockPermissionRequest, sitePermissions)

        // when
        sitePermissionFeature.handleNoRuledFlow(sitePermissions, mockPermissionRequest, URL)

        // then
        verify(mockPermissionRequest, atLeastOnce()).grant()
        verify(sitePermissionFeature).consumePermissionRequest(mockPermissionRequest)
        verify(sitePermissionFeature).updatePermissionToolbarIndicator(mockPermissionRequest, ALLOWED, true)
    }

    @Test
    fun `GIVEN permissionRequest with isForAutoplay true and BLOCKED WHEN handleRuledFlow THEN reject, consumePermissionRequest are called`() {
        // given
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentAutoPlayAudible(id = "permission")))
        }
        doNothing().`when`(sitePermissionFeature).updatePermissionToolbarIndicator(mockPermissionRequest, BLOCKED)
        doReturn(mockSitePermissionRules).`when`(sitePermissionFeature).sitePermissionsRules
        doReturn(SitePermissionsRules.Action.BLOCKED)
            .`when`(mockSitePermissionRules)
            .getActionFrom(mockPermissionRequest)

        // when
        sitePermissionFeature.handleRuledFlow(mockPermissionRequest, URL)

        // then
        verify(mockPermissionRequest).reject()
        verify(sitePermissionFeature).consumePermissionRequest(mockPermissionRequest)
        verify(sitePermissionFeature)
            .updatePermissionToolbarIndicator(
                mockPermissionRequest,
                BLOCKED,
            )
    }

    @Test
    fun `GIVEN permissionRequest with isForAutoplay false and ASK_TO_ALLOW WHEN handleRuledFlow THEN createPrompt is called`() {
        // given
        val sitePermissionsDialogFragment = SitePermissionsDialogFragment()
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
        }
        doReturn(mockSitePermissionRules).`when`(sitePermissionFeature).sitePermissionsRules
        doReturn(SitePermissionsRules.Action.ASK_TO_ALLOW)
            .`when`(mockSitePermissionRules)
            .getActionFrom(mockPermissionRequest)
        doReturn(sitePermissionsDialogFragment).`when`(sitePermissionFeature).createPrompt(mockPermissionRequest, URL)

        // when
        sitePermissionFeature.handleRuledFlow(mockPermissionRequest, URL)

        // then
        verify(sitePermissionFeature).createPrompt(mockPermissionRequest, URL)
    }

    @Test
    fun `GIVEN permissionRequest and containsVideoAndAudioSources false WHEN createPrompt THEN handlingSingleContentPermissions is called`() {
        // given
        val sitePermissionsDialogFragment = SitePermissionsDialogFragment()
        val mockPermissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
        }
        doReturn(sitePermissionsDialogFragment)
            .`when`(sitePermissionFeature)
            .handlingSingleContentPermissions(any(), any(), any())

        // when
        sitePermissionFeature.createPrompt(mockPermissionRequest, URL)

        // then
        verify(sitePermissionFeature)
            .handlingSingleContentPermissions(
                mockPermissionRequest,
                ContentGeoLocation(id = "permission"),
                URL,
            )
    }

    @Test
    fun `GIVEN a ContentStorageAccess request WHEN handlingSingleContentPermissions is called THEN create a specific prompt`() {
        // given
        val origin = "https://mozilla.org"
        val permission = ContentCrossOriginStorageAccess(id = "permission")
        val permissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(permission))
            whenever(id).thenReturn("id")
        }
        sitePermissionFeature.learnMoreUrlProvider =
            FakeSitePermissionsLearnMoreUrlProvider(expectedLink = "https://content-storage-url.com")

        // when
        sitePermissionFeature.handlingSingleContentPermissions(permissionRequest, permission, origin)

        // then
        verify(sitePermissionFeature)
            .createContentCrossOriginStorageAccessPermissionPrompt(
                context = testContext,
                origin,
                permissionRequest,
                showDoNotAskAgainCheckBox = false,
                shouldSelectRememberChoice = true,
                learnMoreLink = "https://content-storage-url.com",
            )
    }

    @Test
    fun `GIVEN a ContentStorageAccess request WHEN createContentStorageAccessPermissionPrompt is called THEN create a specific SitePermissionsDialogFragment`() {
        // given
        val origin = "https://mozilla.org:443"
        val host = "mozilla.org"
        val permission = ContentCrossOriginStorageAccess(id = "permission")
        val permissionRequest: PermissionRequest = mock {
            whenever(permissions).thenReturn(listOf(permission))
            whenever(id).thenReturn("id")
        }

        // when
        sitePermissionFeature.learnMoreUrlProvider =
            FakeSitePermissionsLearnMoreUrlProvider(expectedLink = "https://content-storage-url.com")
        val dialog =
            sitePermissionFeature.createContentCrossOriginStorageAccessPermissionPrompt(
                testContext,
                origin,
                permissionRequest,
                showDoNotAskAgainCheckBox = false,
                shouldSelectRememberChoice = true,
                learnMoreLink = "https://content-storage-url.com",
            )

        // then
        assertEquals(SESSION_ID, dialog.sessionId)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_sitepermissions_storage_access_title,
                host,
                URL(selectedTab.content.url).host,
            ),
            dialog.title,
        )
        assertEquals(iconsR.drawable.mozac_ic_cookies_24, dialog.icon)
        assertEquals(permissionRequest.id, dialog.permissionRequestId)
        assertEquals(sitePermissionFeature, dialog.feature)
        assertEquals(false, dialog.shouldShowDoNotAskAgainCheckBox)
        assertEquals(true, dialog.shouldPreselectDoNotAskAgainCheckBox)
        assertEquals(false, dialog.isNotificationRequest)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_sitepermissions_storage_access_message,
                host,
            ),
            dialog.message,
        )
        assertEquals(
            testContext.getString(R.string.mozac_feature_sitepermissions_storage_access_not_allow),
            dialog.negativeButtonText,
        )
        assertEquals("https://content-storage-url.com", dialog.learnMoreLink)
    }

    @Test
    fun `GIVEN permissionRequest and containsVideoAndAudioSources true WHEN createPrompt THEN createSinglePermissionPrompt is called`() {
        // given
        val permissionRequest: PermissionRequest =
            object : PermissionRequest {
                override val uri: String
                    get() = "http://www.mozilla.org"

                override val id: String
                    get() = PERMISSION_ID

                override val permissions: List<Permission>
                    get() =
                        listOf(
                            ContentVideoCapture("", "back camera"),
                            ContentVideoCamera("", "front camera"),
                            ContentAudioMicrophone(),
                        )

                override fun grant(permissions: List<Permission>) {}

                override fun containsVideoAndAudioSources() = true

                override fun reject() = Unit

                override fun merge(permissionRequest: PermissionRequest) = Unit
            }
        val sitePermissionsDialogFragment = SitePermissionsDialogFragment()
        doReturn(sitePermissionsDialogFragment)
            .`when`(sitePermissionFeature)
            .createSinglePermissionPrompt(
                context = any(),
                origin = ArgumentMatchers.anyString(),
                permissionRequest = any(),
                titleId = ArgumentMatchers.anyInt(),
                iconId = ArgumentMatchers.anyInt(),
                showDoNotAskAgainCheckBox = ArgumentMatchers.anyBoolean(),
                doNotAskAgainCheckBoxLabel = ArgumentMatchers.anyInt(),
                shouldSelectRememberChoice = ArgumentMatchers.anyBoolean(),
                isNotificationRequest = ArgumentMatchers.anyBoolean(),
                negativeButtonResId = ArgumentMatchers.anyInt(),
                learnMoreLink = anyString(),
            )

        // when
        sitePermissionFeature.createPrompt(permissionRequest, URL)

        // then
        verify(sitePermissionFeature)
            .createSinglePermissionPrompt(
                context = any(),
                origin = ArgumentMatchers.anyString(),
                permissionRequest = any(),
                titleId = ArgumentMatchers.anyInt(),
                iconId = ArgumentMatchers.anyInt(),
                showDoNotAskAgainCheckBox = ArgumentMatchers.anyBoolean(),
                doNotAskAgainCheckBoxLabel = ArgumentMatchers.isNull(),
                shouldSelectRememberChoice = ArgumentMatchers.anyBoolean(),
                isNotificationRequest = ArgumentMatchers.anyBoolean(),
                negativeButtonResId = ArgumentMatchers.isNull(),
                learnMoreLink = ArgumentMatchers.isNull(),
            )
    }

    @Test
    fun `GIVEN a request for one permission WHEN a prompt is created THEN emit a fact`() {
        CollectionProcessor.withFactCollection { facts ->
            val sitePermissionsDialogFragment = SitePermissionsDialogFragment()
            val mockPermissionRequest: PermissionRequest = mock {
                whenever(permissions).thenReturn(listOf(ContentGeoLocation(id = "permission")))
            }
            doReturn(sitePermissionsDialogFragment)
                .`when`(sitePermissionFeature)
                .handlingSingleContentPermissions(any(), any(), any())

            sitePermissionFeature.createPrompt(mockPermissionRequest, URL)

            assertEquals(1, facts.size)
            assertEquals(FEATURE_SITEPERMISSIONS, facts[0].component)
            assertEquals(Action.DISPLAY, facts[0].action)
            assertEquals(SitePermissionsFacts.Items.PERMISSIONS, facts[0].item)
            assertEquals("ContentGeoLocation", facts[0].value)
        }
    }

    @Test
    fun `GIVEN a request for a permission with video and audio sources WHEN a prompt is created THEN emit a fact`() {
        CollectionProcessor.withFactCollection { facts ->
            val permissionRequest: PermissionRequest =
                object : PermissionRequest {
                    override val uri = "http://www.mozilla.org"
                    override val id = PERMISSION_ID

                    override val permissions: List<Permission>
                        get() = listOf(ContentVideoCapture(), ContentVideoCamera(), ContentAudioMicrophone())

                    override fun grant(permissions: List<Permission>) {}

                    override fun containsVideoAndAudioSources() = true

                    override fun reject() = Unit

                    override fun merge(permissionRequest: PermissionRequest) = Unit
                }
            val sitePermissionsDialogFragment = SitePermissionsDialogFragment()
            doReturn(sitePermissionsDialogFragment)
                .`when`(sitePermissionFeature)
                .createSinglePermissionPrompt(
                    context = any(),
                    origin = ArgumentMatchers.anyString(),
                    permissionRequest = any(),
                    titleId = ArgumentMatchers.anyInt(),
                    iconId = ArgumentMatchers.anyInt(),
                    showDoNotAskAgainCheckBox = ArgumentMatchers.anyBoolean(),
                    doNotAskAgainCheckBoxLabel = ArgumentMatchers.anyInt(),
                    shouldSelectRememberChoice = ArgumentMatchers.anyBoolean(),
                    isNotificationRequest = ArgumentMatchers.anyBoolean(),
                    negativeButtonResId = ArgumentMatchers.anyInt(),
                    learnMoreLink = ArgumentMatchers.anyString(),
                )

            sitePermissionFeature.createPrompt(permissionRequest, URL)

            assertEquals(1, facts.size)
            assertEquals(FEATURE_SITEPERMISSIONS, facts[0].component)
            assertEquals(Action.DISPLAY, facts[0].action)
            assertEquals(SitePermissionsFacts.Items.PERMISSIONS, facts[0].item)
            assertEquals(permissionRequest.permissions.joinToString { it.id!! }, facts[0].value)
        }
    }

    @Test
    fun `is SitePermission granted in the storage`() = runTest {
        val sitePermissionsList =
            listOf(
                ContentGeoLocation(),
                ContentNotification(),
                ContentAudioCapture(),
                ContentAudioMicrophone(),
                ContentVideoCamera(),
                ContentVideoCapture(),
                ContentPersistentStorage(),
                ContentAutoPlayAudible(),
                ContentAutoPlayInaudible(),
                ContentMediaKeySystemAccess(),
                ContentLocalDeviceAccess(),
                ContentLocalNetworkAccess(),
            )

        sitePermissionsList.forEach { permission ->
            val request: PermissionRequest = mock()
            val sitePermissionFromStorage: SitePermissions = mock()

            doReturn(listOf(permission)).`when`(request).permissions
            doReturn(sitePermissionFromStorage)
                .`when`(mockStorage)
                .findSitePermissionsBy(anyString(), anyBoolean(), anyBoolean())
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).location
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).notification
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).camera
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).microphone
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).localStorage
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).crossOriginStorageAccess
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).mediaKeySystemAccess
            doReturn(AutoplayStatus.ALLOWED).`when`(sitePermissionFromStorage).autoplayAudible
            doReturn(AutoplayStatus.ALLOWED).`when`(sitePermissionFromStorage).autoplayInaudible
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).localDeviceAccess
            doReturn(ALLOWED).`when`(sitePermissionFromStorage).localNetworkAccess

            val isAllowed = sitePermissionFromStorage.isGranted(request)
            assertTrue(isAllowed)
        }
    }

    @Test
    fun `is SitePermission blocked in the storage`() = runTest {
        val sitePermissionsList =
            listOf(
                ContentGeoLocation(),
                ContentNotification(),
                ContentAudioCapture(),
                ContentAudioMicrophone(),
                ContentVideoCamera(),
                ContentVideoCapture(),
                ContentCrossOriginStorageAccess(),
                ContentLocalDeviceAccess(),
                ContentLocalNetworkAccess(),
                Generic(),
            )

        var exceptionThrown = false
        sitePermissionsList.forEach { permission ->
            val request: PermissionRequest = mock()
            val sitePermissionFromStorage: SitePermissions = mock()

            doReturn(listOf(permission)).`when`(request).permissions
            doReturn(sitePermissionFromStorage)
                .`when`(mockStorage)
                .findSitePermissionsBy(anyString(), anyBoolean(), anyBoolean())
            doReturn(BLOCKED).`when`(sitePermissionFromStorage).location
            doReturn(BLOCKED).`when`(sitePermissionFromStorage).notification
            doReturn(BLOCKED).`when`(sitePermissionFromStorage).camera
            doReturn(BLOCKED).`when`(sitePermissionFromStorage).microphone
            doReturn(BLOCKED).`when`(sitePermissionFromStorage).crossOriginStorageAccess
            doReturn(BLOCKED).`when`(sitePermissionFromStorage).localDeviceAccess
            doReturn(BLOCKED).`when`(sitePermissionFromStorage).localNetworkAccess

            try {
                val isAllowed = sitePermissionFromStorage.isGranted(request)
                assertFalse(isAllowed)
            } catch (e: InvalidParameterException) {
                exceptionThrown = true
            }
        }
        assertTrue(exceptionThrown)
    }

    @Test
    fun `feature will re-attach to already existing fragment`() {
        doReturn(false).`when`(sitePermissionFeature).noPermissionRequests(any())

        val fragment: SitePermissionsDialogFragment = mock()
        doReturn(selectedTab.id).`when`(fragment).sessionId
        doReturn(fragment).`when`(mockFragmentManager).findFragmentByTag(any())

        sitePermissionFeature.start()
        verify(fragment).feature = sitePermissionFeature
    }

    @Test
    fun `already existing fragment will be removed if session has none permissions request set anymore`() {
        // given
        val session = selectedTab
        val fragment: SitePermissionsDialogFragment = mock()
        doReturn(session.id).`when`(fragment).sessionId
        val transaction: FragmentTransaction = mock()
        doReturn(fragment).`when`(mockFragmentManager).findFragmentByTag(any())
        doReturn(transaction).`when`(mockFragmentManager).beginTransaction()
        doReturn(transaction).`when`(transaction).remove(fragment)
        doReturn(mockContentState).`when`(sitePermissionFeature).getCurrentContentState()
        doNothing().`when`(sitePermissionFeature).setupPermissionRequestsCollector()
        doNothing().`when`(sitePermissionFeature).setupAppPermissionRequestsCollector()

        // when
        sitePermissionFeature.start()

        // then
        verify(mockFragmentManager).beginTransaction()
        verify(transaction).remove(fragment)
    }

    @Test
    fun `already existing fragment will be removed if session does not exist anymore`() {
        val fragment: SitePermissionsDialogFragment = mock()
        doReturn(UUID.randomUUID().toString()).`when`(fragment).sessionId
        doReturn(mockContentState).`when`(sitePermissionFeature).getCurrentContentState()
        doNothing().`when`(sitePermissionFeature).setupPermissionRequestsCollector()
        doNothing().`when`(sitePermissionFeature).setupAppPermissionRequestsCollector()

        val transaction: FragmentTransaction = mock()

        doReturn(fragment).`when`(mockFragmentManager).findFragmentByTag(any())
        doReturn(transaction).`when`(mockFragmentManager).beginTransaction()
        doReturn(transaction).`when`(transaction).remove(fragment)

        sitePermissionFeature.start()
        verify(mockFragmentManager).beginTransaction()
        verify(transaction).remove(fragment)
    }

    @Test
    fun `getInitialSitePermissions - WHEN sitePermissionsRules is present the function MUST use the sitePermissionsRules values to create a SitePermissions object`() =
        runTest {
            val rules =
                SitePermissionsRules(
                    location = SitePermissionsRules.Action.BLOCKED,
                    camera = SitePermissionsRules.Action.ASK_TO_ALLOW,
                    notification = SitePermissionsRules.Action.ASK_TO_ALLOW,
                    microphone = SitePermissionsRules.Action.BLOCKED,
                    autoplayAudible = SitePermissionsRules.AutoplayAction.BLOCKED,
                    autoplayInaudible = SitePermissionsRules.AutoplayAction.ALLOWED,
                    persistentStorage = SitePermissionsRules.Action.BLOCKED,
                    crossOriginStorageAccess = SitePermissionsRules.Action.ALLOWED,
                    mediaKeySystemAccess = SitePermissionsRules.Action.ASK_TO_ALLOW,
                    localDeviceAccess = SitePermissionsRules.Action.ASK_TO_ALLOW,
                    localNetworkAccess = SitePermissionsRules.Action.ASK_TO_ALLOW,
                )

            sitePermissionFeature.sitePermissionsRules = rules

            val sitePermissions = sitePermissionFeature.getInitialSitePermissions(URL)

            assertEquals(URL, sitePermissions.origin)
            assertEquals(BLOCKED, sitePermissions.location)
            assertEquals(NO_DECISION, sitePermissions.camera)
            assertEquals(NO_DECISION, sitePermissions.notification)
            assertEquals(BLOCKED, sitePermissions.microphone)
            assertEquals(BLOCKED, sitePermissions.autoplayAudible.toStatus())
            assertEquals(ALLOWED, sitePermissions.autoplayInaudible.toStatus())
            assertEquals(BLOCKED, sitePermissions.localStorage)
            assertEquals(ALLOWED, sitePermissions.crossOriginStorageAccess)
            assertEquals(NO_DECISION, sitePermissions.localDeviceAccess)
            assertEquals(NO_DECISION, sitePermissions.localNetworkAccess)
            assertEquals(NO_DECISION, sitePermissions.mediaKeySystemAccess)
        }

    @Test
    fun `any media request must be rejected WHEN system permissions are not granted first`() = runTest {
        val permissions =
            listOf(
                ContentVideoCapture("", "back camera"),
                ContentVideoCamera("", "front camera"),
                ContentAudioCapture(),
                ContentAudioMicrophone(),
            )

        permissions.forEach { permission ->
            var grantWasCalled = false

            val permissionRequest: PermissionRequest =
                object : PermissionRequest {
                    override val uri: String?
                        get() = "http://www.mozilla.org"

                    override val id: String
                        get() = PERMISSION_ID

                    override val permissions: List<Permission>
                        get() = listOf(permission)

                    override fun grant(permissions: List<Permission>) {
                        grantWasCalled = true
                    }

                    override fun reject() = Unit

                    override fun merge(permissionRequest: PermissionRequest) = Unit
                }

            mockStorage = mock()

            val prompt = sitePermissionFeature.onContentPermissionRequested(permissionRequest, URL)
            assertNull(prompt)
            assertFalse(grantWasCalled)
        }
    }

    @Test
    fun `stop will hide site permissions prompt if shouldHide returns true`() {
        val fragment: SitePermissionsDialogFragment = mock()
        val transaction: FragmentTransaction = mock()

        doReturn(fragment).`when`(mockFragmentManager).findFragmentByTag(PROMPT_FRAGMENT_TAG)
        doReturn(transaction).`when`(mockFragmentManager).beginTransaction()
        doReturn(transaction).`when`(transaction).remove(any())

        val feature =
            spy(
                SitePermissionsFeature(
                    context = testContext,
                    storage = mockStorage,
                    onNeedToRequestPermissions = mock(),
                    onShouldShowRequestPermissionRationale = { false },
                    store = browserStore,
                    fragmentManager = mockFragmentManager,
                    coroutineScope = testScope,
                    shouldHide = { true },
                )
            )

        feature.stop()

        verify(feature).hideSitePermissionsPrompt()
        verify(transaction).remove(fragment)
        verify(transaction).commitAllowingStateLoss()
    }

    @Test
    fun `stop will not hide site permissions prompt if shouldHide returns false`() {
        val fragment: SitePermissionsDialogFragment = mock()
        val transaction: FragmentTransaction = mock()

        doReturn(fragment).`when`(mockFragmentManager).findFragmentByTag(PROMPT_FRAGMENT_TAG)
        doReturn(transaction).`when`(mockFragmentManager).beginTransaction()
        doReturn(transaction).`when`(transaction).remove(any())

        val feature =
            spy(
                SitePermissionsFeature(
                    context = testContext,
                    storage = mockStorage,
                    onNeedToRequestPermissions = mock(),
                    onShouldShowRequestPermissionRationale = { false },
                    store = browserStore,
                    fragmentManager = mockFragmentManager,
                    coroutineScope = testScope,
                    shouldHide = { false },
                )
            )

        feature.stop()

        verify(feature, never()).hideSitePermissionsPrompt()
        verify(mockFragmentManager, never()).beginTransaction()
        verify(transaction, never()).remove(any())
    }

    @Test
    fun `WHEN permission result handled THEN permission request state is reset`() {
        doReturn(mockAppPermissionRequest).`when`(sitePermissionFeature).findRequestedAppPermission(any())

        sitePermissionFeature.onPermissionsResult(
            arrayOf("permission"),
            arrayOf(PERMISSION_GRANTED).toIntArray(),
        )

        captureActionsMiddleware.assertLastAction(
            SystemPermissionRequestAction.SystemPermissionStateRequestNotInProgress::class
        )
    }

    private fun mockFragmentManager(): FragmentManager {
        val fragmentManager: FragmentManager = mock()
        val transaction: FragmentTransaction = mock()
        doReturn(transaction).`when`(fragmentManager).beginTransaction()
        return fragmentManager
    }

    private class FakeSitePermissionsLearnMoreUrlProvider(var expectedLink: String? = null) :
        SitePermissionsLearnMoreUrlProvider {
        override fun getUrl(permission: Permission): String? = expectedLink
    }
}

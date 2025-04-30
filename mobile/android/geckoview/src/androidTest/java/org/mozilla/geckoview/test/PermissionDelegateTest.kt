/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

@file:Suppress("ktlint:standard:no-wildcard-imports")

package org.mozilla.geckoview.test

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.LocationManager
import android.os.Build
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry
import org.hamcrest.Matchers.*
import org.json.JSONArray
import org.junit.Assert.fail
import org.junit.Ignore
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.NavigationDelegate
import org.mozilla.geckoview.GeckoSession.PermissionDelegate
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.ContentPermission
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.MediaCallback
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.MediaSource
import org.mozilla.geckoview.GeckoSessionSettings
import org.mozilla.geckoview.StorageController.ClearFlags
import org.mozilla.geckoview.test.TrackingPermissionService.TrackingPermissionInstance
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.ClosedSessionAtStart
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.RejectedPromiseException

@RunWith(AndroidJUnit4::class)
@MediumTest
class PermissionDelegateTest : BaseSessionTest() {
    private val targetContext
        get() = InstrumentationRegistry.getInstrumentation().targetContext

    private fun hasPermission(permission: String): Boolean {
        if (Build.VERSION.SDK_INT < 23) {
            return true
        }
        return PackageManager.PERMISSION_GRANTED ==
            InstrumentationRegistry.getInstrumentation().targetContext.checkSelfPermission(permission)
    }

    private fun isEmulator(): Boolean = "generic" == Build.DEVICE || Build.DEVICE.startsWith("generic_")

    private val storageController
        get() = sessionRule.runtime.storageController

    @Test fun media() {
        assertInAutomationThat(
            "Should have camera permission",
            hasPermission(Manifest.permission.CAMERA),
            equalTo(true),
        )

        assertInAutomationThat(
            "Should have microphone permission",
            hasPermission(Manifest.permission.RECORD_AUDIO),
            equalTo(true),
        )

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        val devices =
            mainSession.evaluateJS(
                "window.navigator.mediaDevices.enumerateDevices()",
            ) as JSONArray

        var hasVideo = false
        var hasAudio = false
        for (i in 0 until devices.length()) {
            if (devices.getJSONObject(i).getString("kind") == "videoinput") {
                hasVideo = true
            }
            if (devices.getJSONObject(i).getString("kind") == "audioinput") {
                hasAudio = true
            }
        }

        assertThat(
            "Device list should contain camera device",
            hasVideo,
            equalTo(true),
        )
        assertThat(
            "Device list should contain microphone device",
            hasAudio,
            equalTo(true),
        )

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onMediaPermissionRequest(
                    session: GeckoSession,
                    uri: String,
                    video: Array<out MediaSource>?,
                    audio: Array<out MediaSource>?,
                    callback: MediaCallback,
                ) {
                    assertThat("URI should match", uri, endsWith(HELLO_HTML_PATH))
                    assertThat("Video source should be valid", video, not(emptyArray()))

                    if (isEmulator()) {
                        callback.grant(video!![0], null)
                    } else {
                        assertThat("Audio source should be valid", audio, not(emptyArray()))
                        callback.grant(video!![0], audio!![0])
                    }
                }
            },
        )

        // Start a video stream, with audio if on a real device.
        val code =
            if (isEmulator()) {
                """this.stream = window.navigator.mediaDevices.getUserMedia({
                           video: { width: 320, height: 240, frameRate: 10 },
                       });"""
            } else {
                """this.stream = window.navigator.mediaDevices.getUserMedia({
                           video: { width: 320, height: 240, frameRate: 10 },
                           audio: true
                       });"""
            }

        // Stop the stream and check active flag and id
        val isActive =
            mainSession.waitForJS(
                """$code
                   this.stream.then(stream => {
                     if (!stream.active || stream.id == '') {
                       return false;
                     }

                     stream.getTracks().forEach(track => track.stop());
                     return true;
                   })
                """.trimMargin(),
            ) as Boolean

        assertThat("Stream should be active and id should not be empty.", isActive, equalTo(true))

        // Now test rejecting the request.
        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onMediaPermissionRequest(
                    session: GeckoSession,
                    uri: String,
                    video: Array<out MediaSource>?,
                    audio: Array<out MediaSource>?,
                    callback: MediaCallback,
                ) {
                    callback.reject()
                }
            },
        )

        try {
            if (isEmulator()) {
                mainSession.waitForJS(
                    """
                        window.navigator.mediaDevices.getUserMedia({ video: true })""",
                )
            } else {
                mainSession.waitForJS(
                    """
                        window.navigator.mediaDevices.getUserMedia({ audio: true, video: true })""",
                )
            }
            fail("Request should have failed")
        } catch (e: RejectedPromiseException) {
            assertThat(
                "Error should be correct",
                e.reason as String,
                containsString("NotAllowedError"),
            )
        }
    }

    @Test fun geolocation() {
        assertInAutomationThat(
            "Should have location permission",
            hasPermission(Manifest.permission.ACCESS_FINE_LOCATION),
            equalTo(true),
        )

        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        // Set location for test
        sessionRule.setPrefsUntilTestEnd(mapOf("geo.provider.testing" to false))
        var context = InstrumentationRegistry.getInstrumentation().targetContext
        var locManager = context.getSystemService(Context.LOCATION_SERVICE) as LocationManager
        var locProvider =
            sessionRule.MockLocationProvider(
                locManager,
                "permissionsLocationProvider",
                1.1111,
                2.2222,
                false,
            )
        locProvider.postLocation()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                // Ensure the content permission is asked first, before the Android permission.
                @AssertCalled(count = 1, order = [1])
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    assertThat("URI should match", perm.uri, endsWith(url))
                    assertThat(
                        "Type should match",
                        perm.permission,
                        equalTo(PermissionDelegate.PERMISSION_GEOLOCATION),
                    )
                    return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW)
                }

                @AssertCalled(count = 1, order = [2])
                override fun onAndroidPermissionsRequest(
                    session: GeckoSession,
                    permissions: Array<out String>?,
                    callback: PermissionDelegate.Callback,
                ) {
                    assertThat(
                        "Permissions list should include ACCESS_FINE_LOCATION",
                        listOf(*permissions!!),
                        hasItems(Manifest.permission.ACCESS_FINE_LOCATION),
                    )
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        checkCoarseLocation(permissions)
                    }
                    callback.grant()
                }

                @SdkSuppress(minSdkVersion = Build.VERSION_CODES.S)
                fun checkCoarseLocation(permissions: Array<out String>?) {
                    assertThat(
                        "Permissions list should include ACCESS_COARSE_LOCATION on Android S and above",
                        listOf(*permissions!!),
                        hasItems(Manifest.permission.ACCESS_COARSE_LOCATION),
                    )
                }
            },
        )

        try {
            val hasPosition =
                mainSession.waitForJS(
                    """new Promise((resolve, reject) =>
                    window.navigator.geolocation.getCurrentPosition(
                        position => resolve(
                            position.coords.latitude !== undefined &&
                            position.coords.longitude !== undefined),
                        error => reject(error.code)))""",
                ) as Boolean

            assertThat("Request should succeed", hasPosition, equalTo(true))
        } catch (ex: RejectedPromiseException) {
            assertThat(
                "Error should not because the permission was denied.",
                ex.reason as String,
                not("1"),
            )
        }

        val perms = sessionRule.waitForResult(storageController.getPermissions(url))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_GEOLOCATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_ALLOW
            ) {
                permFound = true
            }
        }

        assertThat("Geolocation permission should be set to allow", permFound, equalTo(true))

        mainSession.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    var permFound2 = false
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_GEOLOCATION &&
                            perm.value == ContentPermission.VALUE_ALLOW
                        ) {
                            permFound2 = true
                        }
                    }
                    assertThat("Geolocation permission must be present on refresh", permFound2, equalTo(true))
                }
            },
        )
        mainSession.reload()
        mainSession.waitForPageStop()
        locProvider.removeMockLocationProvider()
    }

    @Test fun geolocation_reject() {
        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> = GeckoResult.fromValue(ContentPermission.VALUE_DENY)

                @AssertCalled(count = 0)
                override fun onAndroidPermissionsRequest(
                    session: GeckoSession,
                    permissions: Array<out String>?,
                    callback: PermissionDelegate.Callback,
                ) {
                }
            },
        )

        val errorCode =
            mainSession.waitForJS(
                """new Promise((resolve, reject) =>
                window.navigator.geolocation.getCurrentPosition(reject,
                  error => resolve(error.code)
                ))""",
            )

        // Error code 1 means permission denied.
        assertThat("Request should fail", errorCode as Double, equalTo(1.0))

        val perms = sessionRule.waitForResult(storageController.getPermissions(url))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_GEOLOCATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_DENY
            ) {
                permFound = true
            }
        }

        assertThat("Geolocation permission should be set to allow", permFound, equalTo(true))

        mainSession.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    var permFound2 = false
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_GEOLOCATION &&
                            perm.value == ContentPermission.VALUE_DENY
                        ) {
                            permFound2 = true
                        }
                    }
                    assertThat("Geolocation permission must be present on refresh", permFound2, equalTo(true))
                }
            },
        )
        mainSession.reload()
        mainSession.waitForPageStop()
    }

    @ClosedSessionAtStart
    @Test
    fun trackingProtection() {
        // Tests that we get a tracking protection permission for every load, we
        // can set the value of the permission and that the permission persists
        // across sessions
        trackingProtection(privateBrowsing = false, permanent = true)
    }

    @ClosedSessionAtStart
    @Test
    fun trackingProtectionPrivateBrowsing() {
        // Tests that we get a tracking protection permission for every load, we
        // can set the value of the permission in private browsing and that the
        // permission does not persists across private sessions
        trackingProtection(privateBrowsing = true, permanent = false)
    }

    @ClosedSessionAtStart
    @Test
    fun trackingProtectionPrivateBrowsingPermanent() {
        // Tests that we get a tracking protection permission for every load, we
        // can set the value of the permission permanently in private browsing
        // and that the permanent permission _does_ persists across private sessions
        trackingProtection(privateBrowsing = true, permanent = true)
    }

    private fun trackingProtection(
        privateBrowsing: Boolean,
        permanent: Boolean,
    ) {
        // Make sure we start with a clean slate
        storageController.clearDataFromHost(TEST_HOST, ClearFlags.PERMISSIONS)

        assertThat(
            "Non-permanent only makes sense with private browsing " +
                "(because non-private browsing exceptions are always permanent",
            permanent || privateBrowsing,
            equalTo(true),
        )

        val runtime0 =
            TrackingPermissionInstance.start(
                targetContext,
                temporaryProfile.get(),
                privateBrowsing,
            )

        sessionRule.waitForResult(runtime0.loadTestPath(TRACKERS_PATH))
        var permission = sessionRule.waitForResult(runtime0.trackingPermission)

        assertThat(
            "Permission value should start at DENY",
            permission,
            equalTo(ContentPermission.VALUE_DENY),
        )

        if (privateBrowsing && permanent) {
            runtime0.setPrivateBrowsingPermanentTrackingPermission(
                ContentPermission.VALUE_ALLOW,
            )
        } else {
            runtime0.setTrackingPermission(ContentPermission.VALUE_ALLOW)
        }

        sessionRule.waitForResult(runtime0.reload())

        permission = sessionRule.waitForResult(runtime0.trackingPermission)
        assertThat(
            "Permission value should be ALLOW after setting",
            permission,
            equalTo(ContentPermission.VALUE_ALLOW),
        )

        sessionRule.waitForResult(runtime0.quit())

        // Restart the runtime and verifies that the value is still stored
        val runtime1 =
            TrackingPermissionInstance.start(
                targetContext,
                temporaryProfile.get(),
                privateBrowsing,
            )

        sessionRule.waitForResult(runtime1.loadTestPath(TRACKERS_PATH))

        val trackingPermission = sessionRule.waitForResult(runtime1.trackingPermission)
        assertThat(
            "Tracking permissions should persist only if permanent",
            trackingPermission,
            equalTo(
                when {
                    permanent -> ContentPermission.VALUE_ALLOW
                    else -> ContentPermission.VALUE_DENY
                },
            ),
        )

        sessionRule.waitForResult(runtime1.quit())
    }

    private fun assertTrackingProtectionPermission(value: Int?) {
        var found = false
        mainSession.waitUntilCalled(
            object : NavigationDelegate {
                @AssertCalled
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_TRACKING) {
                            if (value != null) {
                                assertThat(
                                    "Value should match",
                                    perm.value,
                                    equalTo(value),
                                )
                            }
                            found = true
                        }
                    }
                }
            },
        )

        assertThat(
            "Permission should have been found if expected",
            found,
            equalTo(value != null),
        )
    }

    // Tests that all pages have a PERMISSION_TRACKING permission,
    // except for pages that belong to Gecko like about:blank or about:config.
    @Test fun trackingProtectionPermissionOnAllPages() {
        val settings = sessionRule.runtime.settings
        val aboutConfigEnabled = settings.aboutConfigEnabled
        settings.aboutConfigEnabled = true

        mainSession.loadUri("about:config")
        assertTrackingProtectionPermission(null)

        settings.aboutConfigEnabled = aboutConfigEnabled

        mainSession.loadUri("about:blank")
        assertTrackingProtectionPermission(null)

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        assertTrackingProtectionPermission(ContentPermission.VALUE_DENY)
    }

    @Test fun notification() {
        sessionRule.setPrefsUntilTestEnd(mapOf("dom.webnotifications.requireuserinteraction" to false))
        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    assertThat("URI should match", perm.uri, endsWith(url))
                    assertThat(
                        "Type should match",
                        perm.permission,
                        equalTo(PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION),
                    )
                    return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW)
                }
            },
        )

        val result = mainSession.waitForJS("Notification.requestPermission()")

        assertThat(
            "Permission should be granted",
            result as String,
            equalTo("granted"),
        )

        val perms = sessionRule.waitForResult(storageController.getPermissions(url))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_ALLOW
            ) {
                permFound = true
            }
        }

        assertThat("Notification permission should be set to allow", permFound, equalTo(true))

        mainSession.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    var permFound2 = false
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                            perm.value == ContentPermission.VALUE_ALLOW
                        ) {
                            permFound2 = true
                        }
                    }
                    assertThat("Notification permission must be present on refresh", permFound2, equalTo(true))
                }
            },
        )
        mainSession.reload()
        mainSession.waitForPageStop()

        val result2 = mainSession.waitForJS("Notification.permission")

        assertThat(
            "Permission should be granted",
            result2 as String,
            equalTo("granted"),
        )
    }

    @Ignore("disable test for frequently failing Bug 1542525")
    @Test
    fun notification_reject() {
        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> = GeckoResult.fromValue(ContentPermission.VALUE_DENY)
            },
        )

        val result = mainSession.waitForJS("Notification.requestPermission()")

        assertThat(
            "Permission should not be granted",
            result as String,
            equalTo("denied"),
        )

        val perms = sessionRule.waitForResult(storageController.getPermissions(url))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_DENY
            ) {
                permFound = true
            }
        }

        assertThat("Notification permission should be set to allow", permFound, equalTo(true))

        mainSession.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    var permFound2 = false
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                            perm.value == ContentPermission.VALUE_DENY
                        ) {
                            permFound2 = true
                        }
                    }
                    assertThat("Notification permission must be present on refresh", permFound2, equalTo(true))
                }
            },
        )
        mainSession.reload()
        mainSession.waitForPageStop()
    }

    @Test
    fun autoplayReject() {
        // The profile used in automation sets this to false, so we need to hack it back to true here.
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "media.geckoview.autoplay.request" to true,
            ),
        )

        mainSession.loadTestPath(AUTOPLAY_PATH)

        mainSession.waitUntilCalled(
            object : PermissionDelegate {
                @AssertCalled(count = 2)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    val expectedType =
                        if (sessionRule.currentCall.counter ==
                            1
                        ) {
                            PermissionDelegate.PERMISSION_AUTOPLAY_AUDIBLE
                        } else {
                            PermissionDelegate.PERMISSION_AUTOPLAY_INAUDIBLE
                        }
                    assertThat("Type should match", perm.permission, equalTo(expectedType))
                    return GeckoResult.fromValue(ContentPermission.VALUE_DENY)
                }
            },
        )
    }

    @Test
    fun contextId() {
        sessionRule.setPrefsUntilTestEnd(mapOf("dom.webnotifications.requireuserinteraction" to false))
        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    assertThat("URI should match", perm.uri, endsWith(url))
                    assertThat(
                        "Type should match",
                        perm.permission,
                        equalTo(PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION),
                    )
                    assertThat("Context ID should match", perm.contextId, equalTo(mainSession.settings.contextId))
                    return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW)
                }
            },
        )

        val result = mainSession.waitForJS("Notification.requestPermission()")

        assertThat(
            "Permission should be granted",
            result as String,
            equalTo("granted"),
        )

        val perms = sessionRule.waitForResult(storageController.getPermissions(url, false))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_ALLOW
            ) {
                permFound = true
            }
        }

        assertThat("Notification permission should be set to allow", permFound, equalTo(true))

        mainSession.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    var permFound2 = false
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                            perm.value == ContentPermission.VALUE_ALLOW
                        ) {
                            permFound2 = true
                        }
                    }
                    assertThat("Notification permission must be present on refresh", permFound2, equalTo(true))
                }
            },
        )
        mainSession.reload()
        mainSession.waitForPageStop()

        val session2 =
            sessionRule.createOpenSession(
                GeckoSessionSettings
                    .Builder()
                    .contextId("foo")
                    .build(),
            )

        session2.loadUri(url)
        session2.waitForPageStop()

        session2.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    assertThat("URI should match", perm.uri, endsWith(url))
                    assertThat(
                        "Type should match",
                        perm.permission,
                        equalTo(PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION),
                    )
                    assertThat(
                        "Context ID should match",
                        perm.contextId,
                        equalTo(session2.settings.contextId),
                    )
                    return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW)
                }
            },
        )

        val result2 = session2.waitForJS("Notification.requestPermission()")

        assertThat(
            "Permission should be granted",
            result2 as String,
            equalTo("granted"),
        )

        val perms2 = sessionRule.waitForResult(storageController.getPermissions(url, false))

        assertThat("Permissions should not be null", perms, notNullValue())
        permFound = false
        for (perm in perms2) {
            if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_ALLOW
            ) {
                permFound = true
            }
        }

        assertThat("Notification permission should be set to allow", permFound, equalTo(true))

        session2.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    var permFound2 = false
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                            perm.value == ContentPermission.VALUE_ALLOW &&
                            perm.contextId == session2.settings.contextId
                        ) {
                            permFound2 = true
                        }
                    }
                    assertThat("Notification permission must be present on refresh", permFound2, equalTo(true))
                }
            },
        )
        session2.reload()
        session2.waitForPageStop()
    }

    @Test fun setPermissionAllow() {
        sessionRule.setPrefsUntilTestEnd(mapOf("dom.webnotifications.requireuserinteraction" to false))
        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    assertThat("URI should match", perm.uri, endsWith(url))
                    assertThat(
                        "Type should match",
                        perm.permission,
                        equalTo(PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION),
                    )
                    return GeckoResult.fromValue(ContentPermission.VALUE_DENY)
                }
            },
        )
        mainSession.waitForJS("Notification.requestPermission()")

        val perms = sessionRule.waitForResult(storageController.getPermissions(url))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        var notificationPerm: ContentPermission? = null
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_DENY
            ) {
                notificationPerm = perm
                permFound = true
            }
        }

        assertThat("Notification permission should be set to allow", permFound, equalTo(true))

        storageController.setPermission(
            notificationPerm!!,
            ContentPermission.VALUE_ALLOW,
        )

        mainSession.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    var permFound2 = false
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                            perm.value == ContentPermission.VALUE_ALLOW
                        ) {
                            permFound2 = true
                        }
                    }
                    assertThat("Notification permission must be present on refresh", permFound2, equalTo(true))
                }
            },
        )
        mainSession.reload()
        mainSession.waitForPageStop()

        val result = mainSession.waitForJS("Notification.permission")

        assertThat(
            "Permission should be granted",
            result as String,
            equalTo("granted"),
        )
    }

    @Test fun setPermissionDeny() {
        sessionRule.setPrefsUntilTestEnd(mapOf("dom.webnotifications.requireuserinteraction" to false))
        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    assertThat("URI should match", perm.uri, endsWith(url))
                    assertThat(
                        "Type should match",
                        perm.permission,
                        equalTo(PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION),
                    )
                    return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW)
                }
            },
        )

        val result = mainSession.waitForJS("Notification.requestPermission()")

        assertThat(
            "Permission should be granted",
            result as String,
            equalTo("granted"),
        )

        val perms = sessionRule.waitForResult(storageController.getPermissions(url))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        var notificationPerm: ContentPermission? = null
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_ALLOW
            ) {
                notificationPerm = perm
                permFound = true
            }
        }

        assertThat("Notification permission should be set to allow", permFound, equalTo(true))

        storageController.setPermission(
            notificationPerm!!,
            ContentPermission.VALUE_DENY,
        )

        mainSession.delegateDuringNextWait(
            object : NavigationDelegate {
                @AssertCalled(count = 1)
                override fun onLocationChange(
                    session: GeckoSession,
                    url: String?,
                    perms: MutableList<ContentPermission>,
                    hasUserGesture: Boolean,
                ) {
                    var permFound2 = false
                    for (perm in perms) {
                        if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                            perm.value == ContentPermission.VALUE_DENY
                        ) {
                            permFound2 = true
                        }
                    }
                    assertThat("Notification permission must be present on refresh", permFound2, equalTo(true))
                }
            },
        )
        mainSession.reload()
        mainSession.waitForPageStop()

        val result2 = mainSession.waitForJS("Notification.permission")

        assertThat(
            "Permission should be denied",
            result2 as String,
            equalTo("denied"),
        )
    }

    @Test fun setPermissionPrompt() {
        sessionRule.setPrefsUntilTestEnd(mapOf("dom.webnotifications.requireuserinteraction" to false))
        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    assertThat("URI should match", perm.uri, endsWith(url))
                    assertThat(
                        "Type should match",
                        perm.permission,
                        equalTo(PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION),
                    )
                    return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW)
                }
            },
        )

        val result = mainSession.waitForJS("Notification.requestPermission()")

        assertThat(
            "Permission should be granted",
            result as String,
            equalTo("granted"),
        )

        val perms = sessionRule.waitForResult(storageController.getPermissions(url))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        var notificationPerm: ContentPermission? = null
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_ALLOW
            ) {
                notificationPerm = perm
                permFound = true
            }
        }

        assertThat("Notification permission should be set to allow", permFound, equalTo(true))

        storageController.setPermission(
            notificationPerm!!,
            ContentPermission.VALUE_PROMPT,
        )

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> = GeckoResult.fromValue(ContentPermission.VALUE_PROMPT)
            },
        )

        val result2 = mainSession.waitForJS("Notification.requestPermission()")

        assertThat(
            "Permission should be default",
            result2 as String,
            equalTo("default"),
        )
    }

    @Test fun permissionJsonConversion() {
        sessionRule.setPrefsUntilTestEnd(mapOf("dom.webnotifications.requireuserinteraction" to false))
        val url = createTestUrl(HELLO_HTML_PATH)
        mainSession.loadUri(url)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PermissionDelegate {
                @AssertCalled(count = 1)
                override fun onContentPermissionRequest(
                    session: GeckoSession,
                    perm: ContentPermission,
                ): GeckoResult<Int> {
                    assertThat("URI should match", perm.uri, endsWith(url))
                    assertThat(
                        "Type should match",
                        perm.permission,
                        equalTo(PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION),
                    )
                    return GeckoResult.fromValue(ContentPermission.VALUE_ALLOW)
                }
            },
        )

        val result = mainSession.waitForJS("Notification.requestPermission()")

        assertThat(
            "Permission should be granted",
            result as String,
            equalTo("granted"),
        )

        val perms = sessionRule.waitForResult(storageController.getPermissions(url))

        assertThat("Permissions should not be null", perms, notNullValue())
        var permFound = false
        var notificationPerm: ContentPermission? = null
        for (perm in perms) {
            if (perm.permission == PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION &&
                url.startsWith(perm.uri) &&
                perm.value == ContentPermission.VALUE_ALLOW
            ) {
                notificationPerm = perm
                permFound = true
            }
        }

        assertThat("Notification permission should be set to allow", permFound, equalTo(true))

        val jsonPerm = notificationPerm?.toJson()
        assertThat("JSON export should not be null", jsonPerm, notNullValue())

        val importedPerm = ContentPermission.fromJson(jsonPerm!!)
        assertThat("JSON import should not be null", importedPerm, notNullValue())

        assertThat("URIs should match", importedPerm?.uri, equalTo(notificationPerm?.uri))
        assertThat("Types should match", importedPerm?.permission, equalTo(notificationPerm?.permission))
        assertThat("Values should match", importedPerm?.value, equalTo(notificationPerm?.value))
        assertThat("Context IDs should match", importedPerm?.contextId, equalTo(notificationPerm?.contextId))
        assertThat("Private mode should match", importedPerm?.privateMode, equalTo(notificationPerm?.privateMode))
    }

    // @Test fun persistentStorage() {
    //     mainSession.loadTestPath(HELLO_HTML_PATH)
    //     mainSession.waitForPageStop()

    //     // Persistent storage can be rejected
    //     mainSession.delegateDuringNextWait(object : PermissionDelegate {
    //         @AssertCalled(count = 1)
    //         override fun onContentPermissionRequest(
    //                 session: GeckoSession, uri: String?, type: Int,
    //                 callback: PermissionDelegate.Callback) {
    //             callback.reject()
    //         }
    //     })

    //     var success = mainSession.waitForJS("""window.navigator.storage.persist()""")

    //     assertThat("Request should fail",
    //             success as Boolean, equalTo(false))

    //     // Persistent storage can be granted
    //     mainSession.delegateDuringNextWait(object : PermissionDelegate {
    //         // Ensure the content permission is asked first, before the Android permission.
    //         @AssertCalled(count = 1, order = [1])
    //         override fun onContentPermissionRequest(
    //                 session: GeckoSession, uri: String?, type: Int,
    //                 callback: PermissionDelegate.Callback) {
    //             assertThat("URI should match", uri, endsWith(HELLO_HTML_PATH))
    //             assertThat("Type should match", type,
    //                     equalTo(PermissionDelegate.PERMISSION_PERSISTENT_STORAGE))
    //             callback.grant()
    //         }
    //     })

    //     success = mainSession.waitForJS("""window.navigator.storage.persist()""")

    //     assertThat("Request should succeed",
    //             success as Boolean,
    //             equalTo(true))

    //     // after permission granted further requests will always return true, regardless of response
    //     mainSession.delegateDuringNextWait(object : PermissionDelegate {
    //         @AssertCalled(count = 1)
    //         override fun onContentPermissionRequest(
    //                 session: GeckoSession, uri: String?, type: Int,
    //                 callback: PermissionDelegate.Callback) {
    //             callback.reject()
    //         }
    //     })

    //     success = mainSession.waitForJS("""window.navigator.storage.persist()""")

    //     assertThat("Request should succeed",
    //             success as Boolean, equalTo(true))
    // }
}

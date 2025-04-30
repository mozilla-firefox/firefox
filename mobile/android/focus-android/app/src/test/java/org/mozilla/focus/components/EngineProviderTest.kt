/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.components

import android.content.Context
import mozilla.components.browser.engine.gecko.fetch.GeckoViewFetchClient
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.CookiePolicy
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.Mockito.spy
import org.mockito.Mockito.`when`
import org.mockito.MockitoAnnotations
import org.mozilla.focus.Components
import org.mozilla.focus.FocusApplication
import org.mozilla.focus.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class EngineProviderTest {

    @Mock
    private lateinit var applicationContext: FocusApplication

    @Mock
    private lateinit var context: Context

    @Mock
    private lateinit var settings: Settings

    @Mock
    private lateinit var components: Components

    @Before
    fun setup() {
        MockitoAnnotations.openMocks(this)
        context = spy(testContext)

        `when`(context.applicationContext).thenReturn(applicationContext)
        `when`(applicationContext.components).thenReturn(components)

        `when`(components.settings).thenReturn(settings)
    }

    @Test
    fun `getOrCreateRuntime should return the same instance`() {
        val runtime1 = EngineProvider.getOrCreateRuntime(context)
        val runtime2 = EngineProvider.getOrCreateRuntime(context)
        assertEquals(runtime1, runtime2)
    }

    @Test
    fun `createClient should return a GeckoViewFetchClient`() {
        val client = EngineProvider.createClient(context)
        assertNotNull(client)
        assertTrue(client is GeckoViewFetchClient)
    }

    @Test
    fun `createTrackingProtectionPolicy should block social trackers when setting is true`() {
        whenever(settings.shouldBlockSocialTrackers()).thenReturn(true)
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        assertTrue(policy.trackingCategories.contains(EngineSession.TrackingProtectionPolicy.TrackingCategory.SOCIAL))
    }

    @Test
    fun `createTrackingProtectionPolicy should not block social trackers when setting is false`() {
        whenever(settings.shouldBlockSocialTrackers()).thenReturn(false)
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        assertTrue(!policy.trackingCategories.contains(EngineSession.TrackingProtectionPolicy.TrackingCategory.SOCIAL))
    }

    @Test
    fun `createTrackingProtectionPolicy should block ad trackers when setting is true`() {
        whenever(settings.shouldBlockAdTrackers()).thenReturn(true)
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        assertTrue(policy.trackingCategories.contains(EngineSession.TrackingProtectionPolicy.TrackingCategory.AD))
    }

    @Test
    fun `createTrackingProtectionPolicy should not block ad trackers when setting is false`() {
        whenever(settings.shouldBlockAdTrackers()).thenReturn(false)
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        assertTrue(!policy.trackingCategories.contains(EngineSession.TrackingProtectionPolicy.TrackingCategory.AD))
    }

    @Test
    fun `createTrackingProtectionPolicy should block analytics trackers when setting is true`() {
        whenever(settings.shouldBlockAnalyticTrackers()).thenReturn(true)
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        assertTrue(policy.trackingCategories.contains(EngineSession.TrackingProtectionPolicy.TrackingCategory.ANALYTICS))
    }

    @Test
    fun `createTrackingProtectionPolicy should not block analytics trackers when setting is false`() {
        whenever(settings.shouldBlockAnalyticTrackers()).thenReturn(false)
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        assertTrue(!policy.trackingCategories.contains(EngineSession.TrackingProtectionPolicy.TrackingCategory.ANALYTICS))
    }

    @Test
    fun `createTrackingProtectionPolicy should block other trackers when setting is true`() {
        whenever(settings.shouldBlockOtherTrackers()).thenReturn(true)
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        assertTrue(policy.trackingCategories.contains(EngineSession.TrackingProtectionPolicy.TrackingCategory.CONTENT))
    }

    @Test
    fun `createTrackingProtectionPolicy should not block other trackers when setting is false`() {
        whenever(settings.shouldBlockOtherTrackers()).thenReturn(false)
        val policy = EngineProvider.createTrackingProtectionPolicy(context)
        assertTrue(!policy.trackingCategories.contains(EngineSession.TrackingProtectionPolicy.TrackingCategory.CONTENT))
    }

    @Test
    fun `getCookiePolicy should return ACCEPT_NONE when setting is yes`() {
        whenever(settings.shouldBlockCookiesValue).thenReturn("yes")
        val cookiePolicy = EngineProvider.getCookiePolicy(context)
        assertEquals(CookiePolicy.ACCEPT_NONE, cookiePolicy)
    }

    @Test
    fun `getCookiePolicy should return ACCEPT_ALL when setting is no`() {
        whenever(settings.shouldBlockCookiesValue).thenReturn("no")
        val cookiePolicy = EngineProvider.getCookiePolicy(context)
        assertEquals(CookiePolicy.ACCEPT_ALL, cookiePolicy)
    }
}

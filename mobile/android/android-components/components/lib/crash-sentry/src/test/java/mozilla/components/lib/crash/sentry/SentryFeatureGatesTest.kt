/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.crash.sentry

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.sentry.Sentry
import io.sentry.android.core.SentryAndroidOptions
import io.sentry.protocol.SentryId
import mozilla.components.lib.crash.Crash
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

// Not a real DSN. A syntactically valid placeholder pointing at localhost, so that init runs to
// completion and the configured options can be read back. Events captured here are queued against
// an unreachable host and never reach a server.
private const val PLACEHOLDER_DSN = "https://test@localhost/0"

@RunWith(AndroidJUnit4::class)
class SentryFeatureGatesTest {

    @After
    fun tearDown() {
        Sentry.close()
    }

    @Test
    fun `WHEN Sentry is initialized THEN default options should match last audit`() {
        SentryService(testContext, PLACEHOLDER_DSN).initSentry()
        val options = Sentry.getCurrentScopes().options as SentryAndroidOptions

        assertTrue(options.isEnabled)

        // Automatic event sources should all be disabled.
        assertFalse(options.isEnableUncaughtExceptionHandler)
        assertFalse(options.isAnrEnabled)
        assertFalse(options.isEnableNdk)
        assertFalse(options.isMemoryLimiterEnabled)
        assertFalse(options.isTombstoneEnabled)
        assertFalse(options.isEnableAutoSessionTracking)
        assertFalse(options.logs.isEnabled)
        assertFalse(options.metrics.isEnabled)

        // These event sources use sample-rate instead of is-enabled so the checks
        // are slightly different from above.
        assertNull(options.tracesSampleRate)
        assertNull(options.tracesSampler)
        assertNull(options.profilesSampleRate)
        assertNull(options.profileSessionSampleRate)
        assertNull(options.sessionReplay.sessionSampleRate)
        assertNull(options.sessionReplay.onErrorSampleRate)

        // Optional data enrichment features we are not using
        assertFalse(options.isSendClientReports)
        assertFalse(options.isEnableScopePersistence)
        assertFalse(options.isEnableFramesTracking)
        assertFalse(options.isEnableLegacyProfiling)
        assertFalse(options.isEnablePerformanceV2)
        assertFalse(options.isEnableRootCheck)
        assertFalse(options.dataCollectionResolver.isUserInfo)
        assertFalse(options.dataCollectionResolver.isFilePaths)
        assertFalse(options.isAttachScreenshot)
        assertFalse(options.isAttachViewHierarchy)
        assertFalse(options.isCollectExternalStorageContext)

        // Most automatic breadcrumbs are turned off
        assertFalse(options.isEnableSystemEventBreadcrumbs)
        assertFalse(options.isEnableNetworkEventBreadcrumbs)
        assertFalse(options.isEnableUserInteractionBreadcrumbs)

        // This is enrichment features we do opt in to
        assertTrue(options.isCollectAdditionalContext)
        assertTrue(options.isSendModules)
        assertTrue(options.isEnableActivityLifecycleBreadcrumbs)
        assertTrue(options.isEnableAppComponentBreadcrumbs)
        assertTrue(options.isEnableAppLifecycleBreadcrumbs)
    }

    @Test
    fun `WHEN an uncaught exception is reported THEN Sentry captures an event`() {
        val service = SentryService(testContext, PLACEHOLDER_DSN)
        val throwable = RuntimeException("HelloException")
        val crash = Crash.UncaughtExceptionCrash(0, throwable, arrayListOf())

        // Uncaught exceptions that make it this far are always sent (if we have a DSN)
        // and we expect the caller to generate this to decide if we should or not.
        val id = service.report(crash)

        // Sentry reports a dropped event by returning EMPTY_ID rather than null, so comparing
        // against it is what distinguishes a captured event from a discarded one.
        assertNotEquals(SentryId.EMPTY_ID.toString(), id)
    }
}

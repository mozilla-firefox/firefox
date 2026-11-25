/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.app.Activity
import android.content.ComponentName
import android.content.Intent
import android.net.Uri
import androidx.navigation.NavController
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.coEvery
import io.mockk.mockk
import io.mockk.slot
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.sync.SyncConfig
import mozilla.components.service.fxa.FxaScope
import mozilla.components.service.fxa.FxaService
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.test.robolectric.testContext
import org.junit.runner.RunWith
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.settings.account.AuthIntentReceiverActivity
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.Shadows.shadowOf

@RunWith(AndroidJUnit4::class)
class FxaAuthFlowIntentProcessorTest {

    private val settings: Settings = mockk()

    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `GIVEN an intent to process, WHEN the intent is not a deeplink, THEN it is not processed`() =
        runTest(testDispatcher) {
            val accountManager = mockk<FxaAccountManager>()
            val processor = createProcessor(accountManager)

            val processed = processor.process(Intent(), NavController(testContext), Intent(), settings)
            testScheduler.advanceUntilIdle()

            assertFalse(processed)
        }

    @Test
    fun `GIVEN an intent with deep link, WHEN the host is not an fxa auth flow, THEN it is not processed`() =
        runTest(testDispatcher) {
            val accountManager = mockk<FxaAccountManager>()
            val processor = createProcessor(accountManager)
            val uri =
                Uri.Builder()
                    .scheme(BuildConfig.DEEP_LINK_SCHEME)
                    .authority("home")
                    .appendQueryParameter("entry_point", FenixFxAEntryPoint.IPProtectionSettings.entryName)
                    .appendQueryParameter("service", "vpn")
                    .build()
            val intent = Intent(Intent.ACTION_VIEW, uri)

            val processed = processor.process(intent, NavController(testContext), Intent(), settings)
            testScheduler.advanceUntilIdle()

            assertFalse(processed)
        }

    @Test
    fun `GIVEN an fxa auth flow deeplink intent, WHEN the entry point query param is missing, THEN it is not processed`() =
        runTest(testDispatcher) {
            val processor = createProcessor(mockk())
            val intent = authIntent("service" to "vpn")

            val processed = processor.process(intent, NavController(testContext), Intent(), settings)

            assertFalse(processed)
        }

    @Test
    fun `GIVEN an fxa auth flow deeplink intent, WHEN the service query param is missing, THEN it is not processed`() =
        runTest(testDispatcher) {
            val processor = createProcessor(mockk())
            val intent = authIntent("entry_point" to FenixFxAEntryPoint.IPProtectionSettings.entryName)

            assertFalse(processor.process(intent, NavController(testContext), Intent(), settings))
        }

    @Test
    fun `GIVEN an fxa auth flow deeplink intent, WHEN the entry point query param does not match a known entry point, THEN it is not processed`() =
        runTest(testDispatcher) {
            val processor = createProcessor(mockk())
            val intent = authIntent("entry_point" to "invalid-entry-point", "service" to "vpn")

            assertFalse(processor.process(intent, NavController(testContext), Intent(), settings))
        }

    @Test
    fun `GIVEN an fxa auth flow deeplink intent, WHEN the service query param does not match a known service, THEN it is not processed`() =
        runTest(testDispatcher) {
            val processor = createProcessor(mockk())
            val intent =
                authIntent(
                    "entry_point" to FenixFxAEntryPoint.IPProtectionSettings.entryName,
                    "service" to "invalid-service",
                )

            assertFalse(processor.process(intent, NavController(testContext), Intent(), settings))
        }

    @Test
    fun `GIVEN a valid auth flow deeplink intent, WHEN the account manager fails to begin the auth flow, THEN the activity is not started`() =
        runTest(testDispatcher) {
            val processor = createProcessor(mockAccountManager(authUrl = null))
            val intent =
                authIntent(
                    "entry_point" to FenixFxAEntryPoint.IPProtectionSettings.entryName,
                    "service" to "vpn",
                )

            withActivity { activity ->
                assertTrue(processor.process(intent, NavController(activity), Intent(), settings))
                testScheduler.advanceUntilIdle()

                assertNull(shadowOf(activity).nextStartedActivity)
            }
        }

    @Test
    fun `GIVEN a valid auth flow deeplink intent, WHEN the account manager begins the auth flow correctly, THEN the auth activity is started`() =
        runTest(testDispatcher) {
            val authUrl = "https://example.com/oauth"
            val processor = createProcessor(mockAccountManager(authUrl))
            val intent =
                authIntent(
                    "entry_point" to FenixFxAEntryPoint.IPProtectionSettings.entryName,
                    "service" to "vpn",
                )

            withActivity { activity ->
                assertTrue(processor.process(intent, NavController(activity), Intent(), settings))
                testScheduler.advanceUntilIdle()

                val startedIntent = assertNotNull(shadowOf(activity).nextStartedActivity)
                assertEquals(
                    ComponentName(activity, AuthIntentReceiverActivity::class.java),
                    startedIntent.component,
                )
                assertEquals(authUrl, startedIntent.dataString)
            }
        }

    @Test
    fun `GIVEN an IP Protection auth deep link and sync decoupling is not enabled, WHEN authentication begins, THEN Sync scopes are included`() =
        runTest(testDispatcher) {
            val entryPoint = FenixFxAEntryPoint.IPProtectionSettings
            val service = FxaService.IpProtection
            val scopes = slot<Set<String>>()
            val accountManager = mockk<FxaAccountManager>()
            coEvery {
                accountManager.beginAuthentication(
                    entrypoint = entryPoint,
                    service = service.identifier,
                    authScopes = capture(scopes),
                )
            } returns null
            val processor = createProcessor(accountManager)
            val intent = authIntent("entry_point" to entryPoint.entryName, "service" to service.identifier)

            assertTrue(processor.process(intent, NavController(testContext), Intent(), settings))
            testScheduler.advanceUntilIdle()

            assertEquals(
                setOf(FxaScope.IP_PROTECTION.value, FxaScope.PROFILE.value, FxaScope.SYNC.value),
                scopes.captured,
            )
        }

    @Test
    fun `GIVEN an IP Protection auth deep link and sync decoupling is enabled, WHEN authentication begins, THEN Sync scopes are not included for non-sync service`() =
        runTest(testDispatcher) {
            val entryPoint = FenixFxAEntryPoint.IPProtectionSettings
            val service = FxaService.Relay
            val scopes = slot<Set<String>>()
            val accountManager = mockk<FxaAccountManager>()
            coEvery {
                accountManager.beginAuthentication(
                    entrypoint = entryPoint,
                    service = service.identifier,
                    authScopes = capture(scopes),
                )
            } returns null
            val processor =
                createProcessor(
                    accountManager = accountManager,
                    syncConfig =
                        SyncConfig(
                            supportedEngines = emptySet(),
                            periodicSyncConfig = null,
                            syncDecouplingEnabled = true,
                        ),
                )
            val intent = authIntent("entry_point" to entryPoint.entryName, "service" to service.identifier)

            assertTrue(processor.process(intent, NavController(testContext), Intent(), settings))
            testScheduler.advanceUntilIdle()

            assertEquals(
                setOf(FxaScope.RELAY.value, FxaScope.PROFILE.value),
                scopes.captured,
            )
        }

    private fun authIntent(vararg queryParams: Pair<String, String>): Intent {
        val uri = Uri.Builder().scheme(BuildConfig.DEEP_LINK_SCHEME).authority("fxa_begin_auth")
        queryParams.forEach { (key, value) -> uri.appendQueryParameter(key, value) }
        return Intent(Intent.ACTION_VIEW, uri.build())
    }

    private fun mockAccountManager(authUrl: String?): FxaAccountManager = mockk {
        coEvery {
            beginAuthentication(
                entrypoint = FenixFxAEntryPoint.IPProtectionSettings,
                service = "vpn",
                authScopes = setOf(FxaScope.IP_PROTECTION.value, FxaScope.SYNC.value, FxaScope.PROFILE.value),
            )
        } returns authUrl
    }

    private fun TestScope.createProcessor(
        accountManager: FxaAccountManager,
        syncConfig: SyncConfig =
            SyncConfig(supportedEngines = emptySet(), periodicSyncConfig = null, syncDecouplingEnabled = false),
    ): FxaAuthFlowIntentProcessor =
        FxaAuthFlowIntentProcessor(
            accountManager = accountManager,
            syncConfig = syncConfig,
            coroutineScope = this,
            ioDispatcher = testDispatcher,
            mainDispatcher = testDispatcher,
        )

    private fun withActivity(block: (Activity) -> Unit) {
        Robolectric.buildActivity(Activity::class.java).setup().use { controller ->
            val activity = controller.get()
            activity.setTheme(R.style.NormalTheme)
            block(activity)
        }
    }
}

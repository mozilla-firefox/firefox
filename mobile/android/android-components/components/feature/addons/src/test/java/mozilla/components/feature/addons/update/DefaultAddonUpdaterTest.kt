/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.update

import androidx.concurrent.futures.await
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.Configuration
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.testing.WorkManagerTestInitHelper
import java.util.concurrent.TimeUnit
import junit.framework.TestCase.assertEquals
import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertNull
import junit.framework.TestCase.assertTrue
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.webextension.DisabledFlags
import mozilla.components.concept.engine.webextension.Metadata
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.feature.addons.R
import mozilla.components.feature.addons.update.AddonUpdaterWorker.Companion.KEY_DATA_EXTENSIONS_ID
import mozilla.components.feature.addons.update.DefaultAddonUpdater.Companion.WORK_TAG_IMMEDIATE
import mozilla.components.feature.addons.update.DefaultAddonUpdater.Companion.WORK_TAG_PERIODIC
import mozilla.components.support.base.worker.Frequency
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class DefaultAddonUpdaterTest {

    private val testDispatcher = StandardTestDispatcher()

    private lateinit var store: BrowserStore

    @Before
    fun setUp() {
        val configuration = Configuration.Builder().build()

        // Initialize WorkManager (early) for instrumentation tests.
        WorkManagerTestInitHelper.initializeTestWorkManager(testContext, configuration)

        store = BrowserStore(BrowserState())
    }

    @Test
    fun `registerForFutureUpdates - schedule work for future update`() =
        runTest(testDispatcher) {
            val frequency = Frequency(1, TimeUnit.DAYS)
            val updater = DefaultAddonUpdater(testContext, frequency, store)
            val addonId = "addonId"

            val workId = updater.getUniquePeriodicWorkName(addonId)

            val workManager = WorkManager.getInstance(testContext)
            var workData = workManager.getWorkInfosForUniqueWork(workId).await()

            assertTrue(workData.isEmpty())

            updater.registerForFutureUpdates(addonId)
            workData = workManager.getWorkInfosForUniqueWork(workId).await()

            assertFalse(workData.isEmpty())

            assertExtensionIsRegisteredFoUpdates(updater, addonId)

            // Cleaning work manager
            workManager.cancelUniqueWork(workId)
        }

    @Test
    fun `update - schedule work for immediate update`() =
        runTest(testDispatcher) {
            val updater =
                DefaultAddonUpdater(
                    testContext,
                    store = store,
                )
            val addonId = "addonId"

            val workId = updater.getUniqueImmediateWorkName(addonId)

            val workManager = WorkManager.getInstance(testContext)
            var workData = workManager.getWorkInfosForUniqueWork(workId).await()

            assertTrue(workData.isEmpty())

            updater.update(addonId)
            workData = workManager.getWorkInfosForUniqueWork(workId).await()

            assertFalse(workData.isEmpty())

            val work = workData.first()

            assertEquals(WorkInfo.State.ENQUEUED, work.state)
            assertTrue(work.tags.contains(workId))
            assertTrue(work.tags.contains(WORK_TAG_IMMEDIATE))

            // Cleaning work manager
            workManager.cancelUniqueWork(workId)
        }

    @Test
    fun `onUpdatePermissionRequest - records the request in the store without answering it`() {
        val updater = DefaultAddonUpdater(testContext, store = store)
        var answer: Boolean? = null

        updater.onUpdatePermissionRequest(
            extension = extension("addonId"),
            newPermissions = listOf("privacy"),
            newOrigins = emptyList(),
            newDataCollectionPermissions = listOf("healthInfo"),
        ) {
            answer = it
        }

        assertNull(answer)

        val pendingUpdate = store.state.pendingExtensionUpdates.getValue("addonId")
        assertEquals(listOf("privacy"), pendingUpdate.permissions)
        assertEquals(listOf("healthInfo"), pendingUpdate.dataCollectionPermissions)

        pendingUpdate.complete(granted = true)
        assertEquals(true, answer)
    }

    @Test
    fun `onUpdatePermissionRequest - grants without prompting when no permission has a description`() {
        val updater = DefaultAddonUpdater(testContext, store = store)
        var answer: Boolean? = null

        updater.onUpdatePermissionRequest(
            extension = extension("addonId"),
            newPermissions = listOf("testUnknownPermission"),
            newOrigins = emptyList(),
            newDataCollectionPermissions = emptyList(),
        ) {
            answer = it
        }

        assertEquals(true, answer)
        assertTrue(store.state.pendingExtensionUpdates.isEmpty())
    }

    @Test
    fun `onUpdatePermissionRequest - a second request replaces the pending one`() {
        val updater = DefaultAddonUpdater(testContext, store = store)
        var firstAnswer: Boolean? = null
        var secondAnswer: Boolean? = null

        updater.onUpdatePermissionRequest(
            extension = extension("addonId"),
            newPermissions = listOf("privacy"),
            newOrigins = emptyList(),
            newDataCollectionPermissions = emptyList(),
        ) {
            firstAnswer = it
        }

        updater.onUpdatePermissionRequest(
            extension = extension("addonId"),
            newPermissions = listOf("privacy", "management"),
            newOrigins = emptyList(),
            newDataCollectionPermissions = emptyList(),
        ) {
            secondAnswer = it
        }

        assertEquals(false, firstAnswer)
        assertNull(secondAnswer)

        val pendingUpdate = store.state.pendingExtensionUpdates.getValue("addonId")
        assertEquals(listOf("privacy", "management"), pendingUpdate.permissions)
    }

    @Test
    fun `createContentText - notification content must adapt to the amount of valid permissions`() {
        val updater =
            DefaultAddonUpdater(
                testContext,
                store = store,
            )
        val newPermissions = listOf("privacy", "management")
        val newDataCollectionPermissions = emptyList<String>()

        var content = updater.createContentText(newPermissions, newDataCollectionPermissions).split("\n")
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_notification_short_intro),
            content[0],
        )
        assertEquals(
            "New required permissions: Read and modify privacy settings. Monitor extension usage and manage themes.",
            content[1],
        )

        val newPermissionsWithInvalidPerm = listOf("privacy", "invalid")
        content = updater.createContentText(newPermissionsWithInvalidPerm, newDataCollectionPermissions).split("\n")
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_notification_short_intro),
            content[0],
        )
        assertEquals("New required permissions: Read and modify privacy settings.", content[1])
    }

    @Test
    fun `createContentText - notification content supports data collection permissions`() {
        val updater =
            DefaultAddonUpdater(
                testContext,
                store = store,
            )
        val newPermissions = emptyList<String>()
        val newDataCollectionPermissions = listOf("healthInfo", "technicalAndInteraction")

        val content = updater.createContentText(newPermissions, newDataCollectionPermissions).split("\n")
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_notification_short_intro),
            content[0],
        )
        assertEquals(
            "New required data collection: The developer says the extension will collect " +
                "health information, technical and interaction data.",
            content[1],
        )
    }

    @Test
    fun `createContentText - abbreviates long permission paragraphs in the notification content`() {
        val updater =
            DefaultAddonUpdater(
                testContext,
                store = store,
            )
        val newPermissions =
            listOf(
                "bookmarks",
                "browsingData",
                "tabs",
                "userScripts",
            )
        val newDataCollectionPermissions = GeckoWebExtension.DATA_COLLECTION_PERMISSIONS.filter { it != "none" }

        val content = updater.createContentText(newPermissions, newDataCollectionPermissions).split("\n")
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_notification_short_intro),
            content[0],
        )

        assertEquals(DefaultAddonUpdater.NARROW_MAX_LENGTH, content[1].length)
        assertTrue(content[1].startsWith("New required permissions:"))

        assertEquals(DefaultAddonUpdater.NARROW_MAX_LENGTH, content[2].length)
        assertTrue(content[2].startsWith("New required data collection:"))
    }

    @Test
    fun `createContentText - abbreviates the new permissions paragraph in the notification content`() {
        val updater =
            DefaultAddonUpdater(
                testContext,
                store = store,
            )
        val newPermissions =
            listOf(
                "bookmarks",
                "browsingData",
                "devtools",
                "downloads",
                "history",
                "privacy",
                "tabs",
                "userScripts",
            )
        val newDataCollectionPermissions = emptyList<String>()

        val content = updater.createContentText(newPermissions, newDataCollectionPermissions).split("\n")
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_notification_short_intro),
            content[0],
        )

        assertEquals(DefaultAddonUpdater.WIDE_MAX_LENGTH, content[1].length)
        assertTrue(content[1].startsWith("New required permissions:"))
    }

    @Test
    fun `createContentText - abbreviates the new data collection permissions paragraph in the notification content`() {
        val updater =
            DefaultAddonUpdater(
                testContext,
                store = store,
            )
        val newPermissions = emptyList<String>()
        val newDataCollectionPermissions = GeckoWebExtension.DATA_COLLECTION_PERMISSIONS.filter { it != "none" }

        val content = updater.createContentText(newPermissions, newDataCollectionPermissions).split("\n")
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_notification_short_intro),
            content[0],
        )

        assertEquals(DefaultAddonUpdater.WIDE_MAX_LENGTH, content[1].length)
        assertTrue(content[1].startsWith("New required data collection:"))
    }

    @Test
    fun `createAllowAction - will create an intent with the correct addon id and allow action`() {
        val updater =
            spy(
                DefaultAddonUpdater(
                    testContext,
                    store = store,
                )
            )
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("addonId")

        updater.createAllowAction(ext, 1)

        verify(updater).createNotificationIntent(ext.id, DefaultAddonUpdater.NOTIFICATION_ACTION_ALLOW)
    }

    @Test
    fun `createDenyAction - will create an intent with the correct addon id and deny action`() {
        val updater =
            spy(
                DefaultAddonUpdater(
                    testContext,
                    store = store,
                )
            )
        val ext: WebExtension = mock()
        whenever(ext.id).thenReturn("addonId")

        updater.createDenyAction(ext, 1)

        verify(updater).createNotificationIntent(ext.id, DefaultAddonUpdater.NOTIFICATION_ACTION_DENY)
    }

    @Test
    fun `createNotificationIntent - will generate an intent with an addonId and an action`() {
        val updater =
            DefaultAddonUpdater(
                testContext,
                store = store,
            )
        val addonId = "addonId"
        val action = "action"

        val intent = updater.createNotificationIntent(addonId, action)

        assertEquals(addonId, intent.getStringExtra(DefaultAddonUpdater.NOTIFICATION_EXTRA_ADDON_ID))
        assertEquals(action, intent.action)
    }

    @Test
    fun `unregisterForFutureUpdates - will remove scheduled work for future update`() =
        runTest(testDispatcher) {
            val frequency = Frequency(1, TimeUnit.DAYS)
            val updater = DefaultAddonUpdater(testContext, frequency, store, testDispatcher)

            val addonId = "addonId"

            updater.updateAttemptStorage = mock()

            val workId = updater.getUniquePeriodicWorkName(addonId)

            val workManager = WorkManager.getInstance(testContext)
            var workData = workManager.getWorkInfosForUniqueWork(workId).await()

            assertTrue(workData.isEmpty())

            updater.registerForFutureUpdates(addonId)
            workData = workManager.getWorkInfosForUniqueWork(workId).await()

            assertFalse(workData.isEmpty())

            assertExtensionIsRegisteredFoUpdates(updater, addonId)

            updater.unregisterForFutureUpdates(addonId)
            testDispatcher.scheduler.advanceUntilIdle()

            workData = workManager.getWorkInfosForUniqueWork(workId).await()
            assertEquals(WorkInfo.State.CANCELLED, workData.first().state)
            verify(updater.updateAttemptStorage).remove(addonId)
        }

    @Test
    fun `createPeriodicWorkerRequest - will contains the right parameters`() {
        val frequency = Frequency(1, TimeUnit.DAYS)
        val updater = DefaultAddonUpdater(testContext, frequency, store)
        val addonId = "addonId"

        val workId = updater.getUniquePeriodicWorkName(addonId)

        val workRequest = updater.createPeriodicWorkerRequest(addonId)

        assertTrue(workRequest.tags.contains(workId))
        assertTrue(workRequest.tags.contains(WORK_TAG_PERIODIC))

        assertEquals(updater.getWorkerConstraints(), workRequest.workSpec.constraints)

        assertEquals(addonId, workRequest.workSpec.input.getString(KEY_DATA_EXTENSIONS_ID))
    }

    @Test
    fun `registerForFutureUpdates - will register only unregistered extensions`() =
        runTest(testDispatcher) {
            val updater =
                DefaultAddonUpdater(
                    testContext,
                    store = store,
                )
            val registeredExt: WebExtension = mock()
            val notRegisteredExt: WebExtension = mock()
            whenever(registeredExt.id).thenReturn("registeredExt")
            whenever(notRegisteredExt.id).thenReturn("notRegisteredExt")

            updater.registerForFutureUpdates("registeredExt")

            val extensions = listOf(registeredExt, notRegisteredExt)

            assertExtensionIsRegisteredFoUpdates(updater, "registeredExt")

            updater.registerForFutureUpdates(extensions)

            extensions.forEach { ext ->
                assertExtensionIsRegisteredFoUpdates(updater, ext.id)
            }
        }

    @Test
    fun `registerForFutureUpdates - will not register built-in and unsupported extensions`() =
        runTest(testDispatcher) {
            val updater =
                DefaultAddonUpdater(
                    testContext,
                    store = store,
                )

            val regularExt: WebExtension = mock()
            whenever(regularExt.id).thenReturn("regularExt")

            val builtInExt: WebExtension = mock()
            whenever(builtInExt.id).thenReturn("builtInExt")
            whenever(builtInExt.isBuiltIn()).thenReturn(true)

            val unsupportedExt: WebExtension = mock()
            whenever(unsupportedExt.id).thenReturn("unsupportedExt")
            val metadata: Metadata = mock()
            whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(DisabledFlags.APP_SUPPORT))
            whenever(unsupportedExt.getMetadata()).thenReturn(metadata)

            val extensions = listOf(regularExt, builtInExt, unsupportedExt)
            updater.registerForFutureUpdates(extensions)

            assertExtensionIsRegisteredFoUpdates(updater, regularExt.id)

            assertExtensionIsNotRegisteredFoUpdates(updater, builtInExt.id)
            assertExtensionIsNotRegisteredFoUpdates(updater, unsupportedExt.id)
        }

    private fun extension(id: String): WebExtension {
        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn(id)
        return extension
    }

    private suspend fun assertExtensionIsRegisteredFoUpdates(updater: DefaultAddonUpdater, extId: String) {
        val workId = updater.getUniquePeriodicWorkName(extId)
        val workManager = WorkManager.getInstance(testContext)
        val workData = workManager.getWorkInfosForUniqueWork(workId).await()
        val work = workData.first()

        assertEquals(WorkInfo.State.ENQUEUED, work.state)
        assertTrue(work.tags.contains(workId))
        assertTrue(work.tags.contains(WORK_TAG_PERIODIC))
    }

    private suspend fun assertExtensionIsNotRegisteredFoUpdates(updater: DefaultAddonUpdater, extId: String) {
        val workId = updater.getUniquePeriodicWorkName(extId)
        val workManager = WorkManager.getInstance(testContext)
        val workData = workManager.getWorkInfosForUniqueWork(workId).await()
        assertTrue("$extId should not have been registered for updates", workData.isEmpty())
    }
}

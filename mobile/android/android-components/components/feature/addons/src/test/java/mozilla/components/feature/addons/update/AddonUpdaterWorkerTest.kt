/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.update

import android.content.Context
import androidx.concurrent.futures.await
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.ListenableWorker
import androidx.work.WorkerFactory
import androidx.work.WorkerParameters
import androidx.work.testing.TestListenableWorkerBuilder
import junit.framework.TestCase.assertEquals
import junit.framework.TestCase.assertTrue
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionException
import mozilla.components.feature.addons.AddonManager
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import mozilla.components.support.webextensions.WebExtensionSupport
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class AddonUpdaterWorkerTest {

    private val testDispatcher = StandardTestDispatcher()

    private lateinit var store: BrowserStore

    @Before
    fun setUp() {
        GlobalAddonDependencyProvider.addonManager = null
        store = BrowserStore()

        initWebExtensionSupport()
    }

    private fun initWebExtensionSupport() {
        val engine: Engine = mock()
        val extension: WebExtension = mock()
        whenever(extension.id).thenReturn("addonId")
        val callbackCaptor = argumentCaptor<((List<WebExtension>) -> Unit)>()
        whenever(engine.listInstalledWebExtensions(callbackCaptor.capture(), any())).thenAnswer {
            callbackCaptor.value.invoke(listOf(extension))
        }
        WebExtensionSupport.initialize(engine, store)
    }

    private fun createWorker(addonId: String): AddonUpdaterWorker {
        return TestListenableWorkerBuilder<AddonUpdaterWorker>(testContext)
            .setInputData(AddonUpdaterWorker.createWorkerData(addonId))
            .setWorkerFactory(
                object : WorkerFactory() {
                    override fun createWorker(
                        appContext: Context,
                        workerClassName: String,
                        workerParameters: WorkerParameters,
                    ): ListenableWorker {
                        return spy(AddonUpdaterWorker(appContext, workerParameters, testDispatcher))
                    }
                }
            )
            .build()
    }

    @After
    fun after() {
        GlobalAddonDependencyProvider.addonManager = null
    }

    @Test
    fun `doWork - will return Result_success when SuccessfullyUpdated`() =
        runTest(testDispatcher) {
            val updateAttemptStorage = mock<DefaultAddonUpdater.UpdateAttemptStorage>()
            val addonId = "addonId"
            val onFinishCaptor = argumentCaptor<((AddonUpdater.Status) -> Unit)>()
            val addonManager = spy(AddonManager(store, mock(), mock(), mock()))
            val worker = createWorker(addonId)

            doReturn(updateAttemptStorage).`when`(worker).updateAttemptStorage
            GlobalAddonDependencyProvider.initialize(addonManager, mock())

            doAnswer {
                    onFinishCaptor.value.invoke(AddonUpdater.Status.SuccessfullyUpdated)
                }
                .`when`(addonManager)
                .updateAddon(anyString(), onFinishCaptor.capture())

            doReturn(this).`when`(worker).attemptScope

            val result = worker.startWork().await()

            assertEquals(ListenableWorker.Result.success(), result)
            verify(worker).saveUpdateAttempt(addonId, AddonUpdater.Status.SuccessfullyUpdated)
        }

    @Test
    fun `doWork - will return Result_success when NoUpdateAvailable`() =
        runTest(testDispatcher) {
            val addonId = "addonId"
            val onFinishCaptor = argumentCaptor<((AddonUpdater.Status) -> Unit)>()
            val addonManager = spy(AddonManager(store, mock(), mock(), mock()))
            val worker = createWorker(addonId)

            GlobalAddonDependencyProvider.initialize(addonManager, mock())

            doAnswer {
                    onFinishCaptor.value.invoke(AddonUpdater.Status.NoUpdateAvailable)
                }
                .`when`(addonManager)
                .updateAddon(anyString(), onFinishCaptor.capture())

            val result = worker.startWork().await()

            assertEquals(ListenableWorker.Result.success(), result)
        }

    @Test
    fun `doWork - will return Result_failure when NotInstalled`() =
        runTest(testDispatcher) {
            val addonId = "addonId"
            val onFinishCaptor = argumentCaptor<((AddonUpdater.Status) -> Unit)>()
            val addonManager = spy(AddonManager(store, mock(), mock(), mock()))
            val worker = createWorker(addonId)

            GlobalAddonDependencyProvider.initialize(addonManager, mock())

            doAnswer {
                    onFinishCaptor.value.invoke(AddonUpdater.Status.NotInstalled)
                }
                .`when`(addonManager)
                .updateAddon(anyString(), onFinishCaptor.capture())

            val result = worker.startWork().await()

            assertEquals(ListenableWorker.Result.failure(), result)
        }

    @Test
    fun `doWork - will return Result_failure when an Error happens`() =
        runTest(testDispatcher) {
            val updateAttemptStorage = mock<DefaultAddonUpdater.UpdateAttemptStorage>()
            val addonId = "addonId"
            val onFinishCaptor = argumentCaptor<((AddonUpdater.Status) -> Unit)>()
            val addonManager = spy(AddonManager(store, mock(), mock(), mock()))
            val worker = createWorker(addonId)

            val exception = WebExtensionException(Exception())
            worker.updateAttemptStorage = updateAttemptStorage

            GlobalAddonDependencyProvider.initialize(addonManager, mock())

            doAnswer {
                    onFinishCaptor.value.invoke(AddonUpdater.Status.Error("error", exception))
                }
                .`when`(addonManager)
                .updateAddon(anyString(), onFinishCaptor.capture())

            val result = worker.startWork().await()

            assertEquals(ListenableWorker.Result.failure(), result)
            updateAttemptStorage.saveOrUpdate(any())
        }

    @Test
    fun `doWork - will return Result_failure when the update waits for the user`() =
        runTest(testDispatcher) {
            val addonId = "addonId"
            val addonManager = spy(AddonManager(store, mock(), mock(), mock()))
            val worker = createWorker(addonId)

            GlobalAddonDependencyProvider.initialize(addonManager, mock())

            // Gecko holds the update until its permission request is answered, so the update check does not call
            // back.
            doAnswer {
                    store.dispatch(
                        WebExtensionAction.UpdatePendingExtensionUpdateAction(
                            PendingExtensionUpdate(
                                extensionId = addonId,
                                permissions = listOf("privacy"),
                                origins = emptyList(),
                                dataCollectionPermissions = emptyList(),
                                onPermissionsGranted = {},
                            )
                        )
                    )
                }
                .`when`(addonManager)
                .updateAddon(anyString(), any())

            doReturn(this).`when`(worker).attemptScope

            val result = worker.startWork().await()

            assertEquals(ListenableWorker.Result.failure(), result)
            verify(worker).saveUpdateAttempt(eq(addonId), any<AddonUpdater.Status.Error>())
        }

    @Test
    fun `doWork - ignores the late outcome of an update that already waits for the user`() =
        runTest(testDispatcher) {
            val addonId = "addonId"
            val onFinishCaptor = argumentCaptor<((AddonUpdater.Status) -> Unit)>()
            val addonManager = spy(AddonManager(store, mock(), mock(), mock()))
            val worker = createWorker(addonId)

            GlobalAddonDependencyProvider.initialize(addonManager, mock())

            // The update is already pending when the check starts. While Gecko holds the downloaded update, its update
            // checker (UpdateChecker in XPIInstall.sys.mjs) reports no update for the same version.
            store.dispatch(
                WebExtensionAction.UpdatePendingExtensionUpdateAction(
                    PendingExtensionUpdate(
                        extensionId = addonId,
                        permissions = listOf("privacy"),
                        origins = emptyList(),
                        dataCollectionPermissions = emptyList(),
                        onPermissionsGranted = {},
                    )
                )
            )
            doAnswer {}.`when`(addonManager).updateAddon(anyString(), onFinishCaptor.capture())

            doReturn(this).`when`(worker).attemptScope

            val result = worker.startWork().await()
            onFinishCaptor.value.invoke(AddonUpdater.Status.NoUpdateAvailable)

            assertEquals(ListenableWorker.Result.failure(), result)
            verify(worker).saveUpdateAttempt(eq(addonId), any<AddonUpdater.Status.Error>())
            verify(worker, never()).saveUpdateAttempt(addonId, AddonUpdater.Status.NoUpdateAvailable)
        }

    @Test
    fun `doWork - will try pass any exceptions to the crashReporter`() =
        runTest(testDispatcher) {
            val addonId = "addonId"
            val onFinishCaptor = argumentCaptor<((AddonUpdater.Status) -> Unit)>()
            val addonManager = spy(AddonManager(store, mock(), mock(), mock()))
            val worker = createWorker(addonId)

            var crashWasReported = false
            val crashReporter: ((Throwable) -> Unit) = { _ ->
                crashWasReported = true
            }

            GlobalAddonDependencyProvider.initialize(addonManager, mock(), crashReporter)
            GlobalAddonDependencyProvider.addonManager = null

            doAnswer {
                    onFinishCaptor.value.invoke(AddonUpdater.Status.Error("error", Exception()))
                }
                .`when`(addonManager)
                .updateAddon(anyString(), onFinishCaptor.capture())

            val result = worker.startWork().await()

            assertEquals(ListenableWorker.Result.failure(), result)
            assertTrue(crashWasReported)
        }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.session.storage

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSessionState
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.mock
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoMoreInteractions
import org.mockito.Mockito.`when`

@RunWith(AndroidJUnit4::class)
class AutoSaveTest {

    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `AutoSave - when going to background`() {
        runTest(testDispatcher) {
            // Keep the "owner" in scope to avoid it getting garbage collected and therefore lifecycle events
            // not getting propagated (See #1428).
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)

            val sessionStorage: SessionStorage = mock()

            val state = BrowserState()
            val store = BrowserStore(state)
            val autoSave =
                AutoSave(
                        store = store,
                        sessionStorage = sessionStorage,
                        minimumIntervalMs = 0,
                        applicationScope = backgroundScope,
                        ioDispatcher = testDispatcher,
                    )
                    .whenGoingToBackground(lifecycle)

            assertNull(autoSave.saveJob)

            verifyNoMoreInteractions(sessionStorage)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_CREATE)
            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_START)
            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)
            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)

            testDispatcher.scheduler.advanceUntilIdle()

            verifyNoMoreInteractions(sessionStorage)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)

            runCurrent()

            verify(sessionStorage).save(any())
        }
    }

    @Test
    fun `AutoSave - saves again when the tab state arrives after the background save`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store = createStoreWithSelectedTab()
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            runCurrent()
            verify(sessionStorage, times(1)).save(any())

            val freshState: EngineSessionState = mock()
            store.dispatch(EngineAction.UpdateEngineSessionStateAction("mozilla", freshState))
            runCurrent()

            val captor = argumentCaptor<BrowserState>()
            verify(sessionStorage, times(2)).save(captor.capture())
            assertSame(freshState, captor.value.findTab("mozilla")?.engineState?.engineSessionState)
        }
    }

    @Test
    fun `AutoSave - saves only the first tab state update in background`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store = createStoreWithSelectedTab()
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            store.dispatch(EngineAction.UpdateEngineSessionStateAction("mozilla", mock()))
            runCurrent()
            store.dispatch(EngineAction.UpdateEngineSessionStateAction("mozilla", mock()))
            runCurrent()

            verify(sessionStorage, times(2)).save(any())
        }
    }

    @Test
    fun `AutoSave - saves the tab state that arrives before the background monitoring starts`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store = createStoreWithSelectedTab()
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            val freshState: EngineSessionState = mock()
            store.dispatch(EngineAction.UpdateEngineSessionStateAction("mozilla", freshState))
            runCurrent()

            val captor = argumentCaptor<BrowserState>()
            verify(sessionStorage, times(2)).save(captor.capture())
            assertSame(freshState, captor.value.findTab("mozilla")?.engineState?.engineSessionState)
        }
    }

    @Test
    fun `AutoSave - does not save on tab state updates in foreground`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store = createStoreWithSelectedTab()
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            store.dispatch(EngineAction.UpdateEngineSessionStateAction("mozilla", mock()))
            runCurrent()

            verify(sessionStorage, never()).save(any())
        }
    }

    @Test
    fun `AutoSave - saves once when the tab state arrived before going to background`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store = createStoreWithSelectedTab()
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            store.dispatch(EngineAction.UpdateEngineSessionStateAction("mozilla", mock()))
            runCurrent()
            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            runCurrent()

            verify(sessionStorage, times(1)).save(any())
        }
    }

    @Test
    fun `AutoSave - does not save on tab state updates after returning to foreground`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store = createStoreWithSelectedTab()
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            runCurrent()
            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_START)

            store.dispatch(EngineAction.UpdateEngineSessionStateAction("mozilla", mock()))
            runCurrent()

            verify(sessionStorage, times(1)).save(any())
        }
    }

    @Test
    fun `AutoSave - does not save when another tab's state changes in background`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store = createStoreWithSelectedTab()
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            runCurrent()

            store.dispatch(EngineAction.UpdateEngineSessionStateAction("firefox", mock()))
            runCurrent()

            verify(sessionStorage, times(1)).save(any())
        }
    }

    @Test
    fun `AutoSave - a new background period saves again`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store = createStoreWithSelectedTab()
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            runCurrent()
            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_START)
            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            runCurrent()
            verify(sessionStorage, times(2)).save(any())

            store.dispatch(EngineAction.UpdateEngineSessionStateAction("mozilla", mock()))
            runCurrent()
            verify(sessionStorage, times(3)).save(any())
        }
    }

    @Test
    fun `AutoSave - background save ignores the minimum interval`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(
                createStoreWithSelectedTab(),
                sessionStorage,
                lifecycle,
                minimumIntervalMs = 10_000,
            )

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            runCurrent()

            verify(sessionStorage, times(1)).save(any())
        }
    }

    @Test
    fun `AutoSave - does not save again when a private tab's state changes in background`() {
        runTest(testDispatcher) {
            val owner = mock(LifecycleOwner::class.java)
            val lifecycle = LifecycleRegistry(owner)
            val store =
                BrowserStore(
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.mozilla.org", id = "mozilla"),
                                createTab("https://www.firefox.com", id = "private", private = true),
                            ),
                        selectedTabId = "private",
                    )
                )
            val sessionStorage: SessionStorage = mock()
            createAutoSaveWhenGoingToBackground(store, sessionStorage, lifecycle)

            lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            runCurrent()
            store.dispatch(EngineAction.UpdateEngineSessionStateAction("private", mock()))
            runCurrent()

            verify(sessionStorage, times(1)).save(any())
        }
    }

    @Test
    fun `AutoSave - queued save runs right away when no save is running`() {
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()
            val autoSave = createAutoSave(sessionStorage)

            autoSave.triggerQueuedSave()
            runCurrent()

            verify(sessionStorage, times(1)).save(any())
        }
    }

    @Test
    fun `AutoSave - queued save runs after a save that is still running`() {
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()
            val autoSave = createAutoSave(sessionStorage)
            val fakeLongRunningSave = CompletableDeferred<Unit>()
            autoSave.saveJob = backgroundScope.launch(testDispatcher) { fakeLongRunningSave.await() }
            runCurrent()

            autoSave.triggerQueuedSave()
            runCurrent()
            verify(sessionStorage, never()).save(any())

            fakeLongRunningSave.complete(Unit)
            runCurrent()
            verify(sessionStorage, times(1)).save(any())
        }
    }

    @Test
    fun `AutoSave - queued saves run one after another after a running save`() {
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()
            val autoSave = createAutoSave(sessionStorage)
            val fakeLongRunningSave = CompletableDeferred<Unit>()
            autoSave.saveJob = backgroundScope.launch(testDispatcher) { fakeLongRunningSave.await() }
            runCurrent()

            autoSave.triggerQueuedSave()
            val secondQueuedSave = autoSave.triggerQueuedSave()
            runCurrent()
            verify(sessionStorage, never()).save(any())
            assertSame(secondQueuedSave, autoSave.saveJob)

            fakeLongRunningSave.complete(Unit)
            runCurrent()
            verify(sessionStorage, times(2)).save(any())
        }
    }

    @Test
    fun `AutoSave - queued save runs after a save that is still waiting for the minimum interval`() {
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()
            val autoSave = createAutoSave(sessionStorage, minimumIntervalMs = 10_000)

            autoSave.triggerSave()
            autoSave.triggerQueuedSave()
            runCurrent()
            verify(sessionStorage, never()).save(any())

            advanceTimeAndRun(10_000)
            verify(sessionStorage, times(2)).save(any())
        }
    }

    @Test
    fun `AutoSave - triggerSave skips while a queued save is pending`() {
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()
            val autoSave = createAutoSave(sessionStorage)
            val fakeLongRunningSave = CompletableDeferred<Unit>()
            autoSave.saveJob = backgroundScope.launch(testDispatcher) { fakeLongRunningSave.await() }
            runCurrent()

            val queuedSave = autoSave.triggerQueuedSave()

            assertSame(queuedSave, autoSave.triggerSave())
        }
    }

    @Test
    fun `AutoSave - when tab gets added`() {
        runTest(testDispatcher) {
            val state = BrowserState()
            val store = BrowserStore(state)

            val sessionStorage: SessionStorage = mock()

            val autoSave =
                AutoSave(
                        store = store,
                        sessionStorage = sessionStorage,
                        minimumIntervalMs = 0,
                        applicationScope = this,
                        ioDispatcher = testDispatcher,
                    )
                    .whenSessionsChange()

            testDispatcher.scheduler.advanceUntilIdle()

            assertNull(autoSave.saveJob)
            verify(sessionStorage, never()).save(any())

            store.dispatch(TabListAction.AddTabAction(createTab("https://www.mozilla.org")))

            testDispatcher.scheduler.advanceUntilIdle()

            verify(sessionStorage).save(any())
            autoSave.monitoringJob?.cancel()
        }
    }

    @Test
    fun `AutoSave - when tab gets removed`() {
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()

            val store =
                BrowserStore(
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.mozilla.org", id = "mozilla"),
                                createTab("https://www.firefox.com", id = "firefox"),
                            ),
                        selectedTabId = "mozilla",
                    )
                )

            val autoSave =
                AutoSave(
                        store = store,
                        sessionStorage = sessionStorage,
                        minimumIntervalMs = 0,
                        applicationScope = this,
                        ioDispatcher = testDispatcher,
                    )
                    .whenSessionsChange()

            testDispatcher.scheduler.advanceUntilIdle()

            assertNull(autoSave.saveJob)
            verify(sessionStorage, never()).save(any())

            store.dispatch(TabListAction.RemoveTabAction("mozilla"))

            testDispatcher.scheduler.advanceUntilIdle()

            verify(sessionStorage).save(any())
            autoSave.monitoringJob?.cancel()
        }
    }

    @Test
    fun `AutoSave - when all tabs get removed`() {
        runTest(testDispatcher) {
            val store =
                BrowserStore(
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.firefox.com", id = "firefox"),
                                createTab("https://www.mozilla.org", id = "mozilla"),
                            ),
                        selectedTabId = "mozilla",
                    )
                )

            val sessionStorage: SessionStorage = mock()

            val autoSave =
                AutoSave(
                        store = store,
                        sessionStorage = sessionStorage,
                        minimumIntervalMs = 0,
                        applicationScope = this,
                        ioDispatcher = testDispatcher,
                    )
                    .whenSessionsChange()

            testDispatcher.scheduler.advanceUntilIdle()

            assertNull(autoSave.saveJob)
            verify(sessionStorage, never()).save(any())

            store.dispatch(TabListAction.RemoveAllNormalTabsAction)

            testDispatcher.scheduler.advanceUntilIdle()

            verify(sessionStorage).save(any())
            autoSave.monitoringJob?.cancel()
        }
    }

    @Test
    fun `AutoSave - when no tabs are left`() {
        runTest(testDispatcher) {
            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.firefox.com", id = "firefox")),
                        selectedTabId = "firefox",
                    )
                )

            val sessionStorage: SessionStorage = mock()

            val autoSave =
                AutoSave(
                        store = store,
                        sessionStorage = sessionStorage,
                        minimumIntervalMs = 0,
                        applicationScope = this,
                        ioDispatcher = testDispatcher,
                    )
                    .whenSessionsChange()

            testDispatcher.scheduler.advanceUntilIdle()

            assertNull(autoSave.saveJob)
            verify(sessionStorage, never()).save(any())

            store.dispatch(TabListAction.RemoveTabAction("firefox"))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(sessionStorage).save(any())
            autoSave.monitoringJob?.cancel()
        }
    }

    @Test
    fun `AutoSave - when tab gets selected`() {
        runTest(testDispatcher) {
            val store =
                BrowserStore(
                    BrowserState(
                        tabs =
                            listOf(
                                createTab("https://www.firefox.com", id = "firefox"),
                                createTab("https://www.mozilla.org", id = "mozilla"),
                            ),
                        selectedTabId = "firefox",
                    )
                )

            val sessionStorage: SessionStorage = mock()

            val autoSave =
                AutoSave(
                        store = store,
                        sessionStorage = sessionStorage,
                        minimumIntervalMs = 0,
                        applicationScope = this,
                        ioDispatcher = testDispatcher,
                    )
                    .whenSessionsChange()

            testDispatcher.scheduler.advanceUntilIdle()

            assertNull(autoSave.saveJob)
            verify(sessionStorage, never()).save(any())

            store.dispatch(TabListAction.SelectTabAction("mozilla"))

            testDispatcher.scheduler.advanceUntilIdle()
            autoSave.saveJob?.cancel()

            verify(sessionStorage).save(any())
            autoSave.monitoringJob?.cancel()
        }
    }

    @Test
    fun `AutoSave - when tab loading state changes`() {
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()

            val store =
                BrowserStore(
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    )
                )

            val autoSave =
                AutoSave(
                        store = store,
                        sessionStorage = sessionStorage,
                        minimumIntervalMs = 0,
                        applicationScope = this,
                        ioDispatcher = testDispatcher,
                    )
                    .whenSessionsChange()

            store.dispatch(
                ContentAction.UpdateLoadingStateAction(
                    sessionId = "mozilla",
                    loading = true,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            assertNull(autoSave.saveJob)
            verify(sessionStorage, never()).save(any())

            store.dispatch(
                ContentAction.UpdateLoadingStateAction(
                    sessionId = "mozilla",
                    loading = false,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            verify(sessionStorage).save(any())
            autoSave.monitoringJob?.cancel()
        }
    }

    @Test
    fun `AutoSave - periodically in foreground`() = runTest {
        val engine: Engine = mock()
        val scheduler: ScheduledExecutorService = mock()
        val scheduledFuture = mock(ScheduledFuture::class.java)
        `when`(
                scheduler.scheduleAtFixedRate(
                    any(),
                    eq(300L),
                    eq(300L),
                    eq(TimeUnit.SECONDS),
                )
            )
            .thenReturn(scheduledFuture)

        // LifecycleRegistry only keeps a weak reference to the owner, so it is important to keep
        // a reference here too during the test run.
        // See https://github.com/mozilla-mobile/android-components/issues/5166
        val owner = mock(LifecycleOwner::class.java)
        val lifecycle = LifecycleRegistry(owner)

        val state = BrowserState()
        val store = BrowserStore(state)
        val storage = SessionStorage(testContext, engine, applicationScope = this)
        storage.autoSave(store).periodicallyInForeground(300, TimeUnit.SECONDS, scheduler, lifecycle)

        verifyNoMoreInteractions(scheduler)

        lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_START)

        testDispatcher.scheduler.advanceUntilIdle()

        verify(scheduler)
            .scheduleAtFixedRate(
                any(),
                eq(300L),
                eq(300L),
                eq(TimeUnit.SECONDS),
            )

        verifyNoMoreInteractions(scheduler)

        lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_STOP)

        testDispatcher.scheduler.advanceUntilIdle()

        verify(scheduledFuture).cancel(false)
    }

    @Test
    fun `AutoSave - No new job triggered while save in flight`() =
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()

            val state = BrowserState()
            val store = BrowserStore(state)
            val autoSave =
                AutoSave(
                    store = store,
                    sessionStorage = sessionStorage,
                    minimumIntervalMs = 0,
                    applicationScope = this,
                    ioDispatcher = testDispatcher,
                )

            val runningJob: Job = mock()
            doReturn(true).`when`(runningJob).isActive

            val saveJob = autoSave.triggerSave()
            assertSame(saveJob, saveJob)
        }

    @Test
    fun `AutoSave - New job triggered if current job is done`() =
        runTest(testDispatcher) {
            val sessionStorage: SessionStorage = mock()

            val state = BrowserState()
            val store = BrowserStore(state)
            val autoSave =
                AutoSave(
                    store = store,
                    sessionStorage = sessionStorage,
                    minimumIntervalMs = 0,
                    applicationScope = this,
                    ioDispatcher = testDispatcher,
                )

            val completed: Job = mock()
            doReturn(false).`when`(completed).isActive

            val saveJob = autoSave.triggerSave()
            assertNotSame(completed, saveJob)
        }

    @OptIn(ExperimentalCoroutinesApi::class)
    private fun advanceTimeAndRun(millis: Long) {
        testDispatcher.scheduler.advanceTimeBy(millis)
        testDispatcher.scheduler.runCurrent()
    }

    @OptIn(ExperimentalCoroutinesApi::class)
    private fun runCurrent() {
        testDispatcher.scheduler.runCurrent()
    }

    private fun createStoreWithSelectedTab() =
        BrowserStore(
            BrowserState(
                tabs =
                    listOf(
                        createTab("https://www.mozilla.org", id = "mozilla"),
                        createTab("https://www.firefox.com", id = "firefox"),
                    ),
                selectedTabId = "mozilla",
            )
        )

    private fun TestScope.createAutoSave(sessionStorage: SessionStorage, minimumIntervalMs: Long = 0) =
        AutoSave(
            store = createStoreWithSelectedTab(),
            sessionStorage = sessionStorage,
            minimumIntervalMs = minimumIntervalMs,
            applicationScope = backgroundScope,
            ioDispatcher = testDispatcher,
        )

    /** Registers the background trigger on [TestScope.backgroundScope] and starts the app in the foreground. */
    private fun TestScope.createAutoSaveWhenGoingToBackground(
        store: BrowserStore,
        sessionStorage: SessionStorage,
        lifecycle: LifecycleRegistry,
        minimumIntervalMs: Long = 0,
    ): AutoSave {
        val autoSave =
            AutoSave(
                    store = store,
                    sessionStorage = sessionStorage,
                    minimumIntervalMs = minimumIntervalMs,
                    applicationScope = backgroundScope,
                    ioDispatcher = testDispatcher,
                )
                .whenGoingToBackground(lifecycle)
        lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_START)
        testDispatcher.scheduler.advanceUntilIdle()
        return autoSave
    }
}

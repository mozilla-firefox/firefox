/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.app.Application
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import io.mockk.MockKAnnotations
import io.mockk.Runs
import io.mockk.every
import io.mockk.impl.annotations.MockK
import io.mockk.impl.annotations.RelaxedMockK
import io.mockk.just
import io.mockk.mockk
import io.mockk.slot
import io.mockk.unmockkAll
import io.mockk.verify
import kotlin.test.assertIs
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.concept.engine.Engine
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Components
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.shadows.ShadowApplication

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class ProfilerViewModelTest {

    @MockK lateinit var mockProfiler: Profiler

    @RelaxedMockK lateinit var mockApplication: Application

    @RelaxedMockK lateinit var mockComponents: Components

    @RelaxedMockK lateinit var mockEngine: Engine

    @MockK lateinit var mockProfilerUtils: ProfilerUtils

    private lateinit var viewModel: ProfilerViewModel
    private lateinit var context: Context
    private lateinit var shadowApplication: ShadowApplication
    private val testDispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        MockKAnnotations.init(this, relaxUnitFun = true)
        context = ApplicationProvider.getApplicationContext()
        shadowApplication = shadowOf(context as Application)
        val mockFenixApplicationContext = mockk<FenixApplication>(relaxed = true)

        // Forward service calls from the mock to the real context for Robolectric to track.
        every { mockApplication.startForegroundService(any()) } answers
            {
                context.startForegroundService(firstArg())
            }
        every { mockApplication.startService(any()) } answers
            {
                context.startService(firstArg())
            }

        // Extension function Context.components requires applicationContext to be FenixApplication
        every { mockFenixApplicationContext.components } returns mockComponents
        every { mockApplication.applicationContext } returns mockFenixApplicationContext
        every { mockFenixApplicationContext.getSystemService(any()) } returns
            context.getSystemService(Context.CLIPBOARD_SERVICE)
        every { mockFenixApplicationContext.cacheDir } returns context.cacheDir

        // Set up component chain: components -> core -> engine -> profiler
        every { mockComponents.core.engine } returns mockEngine
        every { mockComponents.core.engine.profiler } returns mockProfiler
        every { mockProfiler.isProfilerActive() } returns false

        every { mockApplication.getSystemService(Context.CLIPBOARD_SERVICE) } returns
            context.getSystemService(Context.CLIPBOARD_SERVICE)
        every { mockApplication.cacheDir } returns context.cacheDir

        every { mockProfilerUtils.saveProfileUrlToClipboard(any(), any()) } returns "http://example.com/profile/123"
        every { mockProfilerUtils.finishProfileSave(any(), any()) } just Runs
    }

    @After
    fun tearDown() {
        unmockkAll()
    }

    private fun initializeViewModel(
        isInitiallyActive: Boolean,
        mainDispatcher: CoroutineDispatcher = testDispatcher,
        ioDispatcher: CoroutineDispatcher = testDispatcher,
    ) {
        every { mockProfiler.isProfilerActive() } returns isInitiallyActive
        viewModel =
            ProfilerViewModel(
                application = mockApplication,
                mainDispatcher = mainDispatcher,
                ioDispatcher = ioDispatcher,
                profilerUtils = mockProfilerUtils,
            )
    }

    @Test
    fun `WHEN the ViewModel is created THEN its initial UI state and active status correctly reflect the profiler's state`() =
        runTest(testDispatcher) {
            initializeViewModel(isInitiallyActive = false)

            val collectedUiStates = mutableListOf<ProfilerUiState>()
            val uiCollectJob = launch {
                viewModel.uiState.toList(collectedUiStates)
            }
            val collectedActiveStates = mutableListOf<Boolean>()
            val activeCollectJob = launch {
                viewModel.isProfilerActive.toList(collectedActiveStates)
            }

            runCurrent()
            assertEquals(listOf(ProfilerUiState.Idle), collectedUiStates)
            assertEquals(listOf(false), collectedActiveStates)

            uiCollectJob.cancel()
            activeCollectJob.cancel()

            collectedUiStates.clear()
            collectedActiveStates.clear()

            initializeViewModel(isInitiallyActive = true)
            val uiCollectJob2 = launch {
                viewModel.uiState.toList(collectedUiStates)
            }
            val activeCollectJob2 = launch {
                viewModel.isProfilerActive.toList(collectedActiveStates)
            }

            runCurrent()
            assertEquals(listOf(ProfilerUiState.Idle), collectedUiStates)
            assertEquals(listOf(true), collectedActiveStates)

            uiCollectJob2.cancel()
            activeCollectJob2.cancel()
        }

    @Test
    fun `GIVEN the profiler is unavailable WHEN start profiling is tried THEN the state should be moved to error`() =
        runTest(testDispatcher) {
            every { mockComponents.core.engine.profiler } returns null

            initializeViewModel(
                isInitiallyActive = false,
                mainDispatcher = testDispatcher,
                ioDispatcher = testDispatcher,
            )
            runCurrent()

            assertEquals(ProfilerUiState.Idle, viewModel.uiState.value)
            viewModel.initiateProfilerStartProcess(ProfilerSettings.Firefox)
            runCurrent()

            val finalState = viewModel.uiState.value
            assertIs<ProfilerUiState.Error>(finalState)
            assertEquals(R.string.profiler_error, finalState.messageResId)
            assertTrue(finalState.errorDetails.contains("Profiler not available"))
            verify(exactly = 0) { mockProfiler.startProfiler(any(), any()) }
        }

    @Test
    fun `GIVEN the profiler is already running WHEN profiling again THEN the UI state remains Running and no new profiler session is started`() =
        runTest(testDispatcher) {
            initializeViewModel(
                isInitiallyActive = true,
                mainDispatcher = testDispatcher,
                ioDispatcher = testDispatcher,
            )
            runCurrent()
            assertEquals(ProfilerUiState.Idle, viewModel.uiState.value)
            viewModel.initiateProfilerStartProcess(ProfilerSettings.Firefox)
            runCurrent()
            assertEquals(ProfilerUiState.Running, viewModel.uiState.value)
            verify(exactly = 0) { mockProfiler.startProfiler(any(), any()) }
        }

    @Test
    fun `WHEN profiling starts successfully THEN the profiler starts and service launches with notification`() =
        runTest(testDispatcher) {
            initializeViewModel(
                isInitiallyActive = false,
                mainDispatcher = testDispatcher,
                ioDispatcher = testDispatcher,
            )
            val settings = ProfilerSettings.Firefox

            val collectedStates = mutableListOf<ProfilerUiState>()
            backgroundScope.launch {
                viewModel.uiState.toList(collectedStates)
            }
            runCurrent()

            viewModel.initiateProfilerStartProcess(settings)
            every { mockProfiler.isProfilerActive() } returns true

            runCurrent()

            val expectedSequence =
                listOf(
                    ProfilerUiState.Idle::class,
                    ProfilerUiState.Starting::class,
                )

            val actualSequence = collectedStates.map { it::class }
            assertEquals("The sequence of UI states was not as expected", expectedSequence, actualSequence)

            verify { mockProfiler.startProfiler(settings.threads, settings.features) }
        }

    @Test
    fun `GIVEN the profiler is not currently active WHEN an attempt is made to stop and save a profile THEN the UI state becomes Finished and no profile URL is available`() =
        runTest(testDispatcher) {
            initializeViewModel(
                isInitiallyActive = false,
                mainDispatcher = testDispatcher,
                ioDispatcher = testDispatcher,
            )
            runCurrent()

            viewModel.stopProfilerAndSave()
            runCurrent()

            val lastState = viewModel.uiState.value
            assertIs<ProfilerUiState.Finished>(lastState)
            assertNull(lastState.profileUrl)

            verify(exactly = 0) { mockProfiler.stopProfiler(any(), any()) }
        }

    @Test
    fun `WHEN a profile is successfully saved THEN the UI transitions through toast and finished states`() =
        runTest(testDispatcher) {
            initializeViewModel(
                isInitiallyActive = true,
                mainDispatcher = testDispatcher,
                ioDispatcher = testDispatcher,
            )

            val fakeProfileData = "profile_data".toByteArray()
            val expectedUrl = "http://example.com/profile/123"
            val successCallbackSlot = slot<(ByteArray?) -> Unit>()

            every {
                mockProfiler.stopProfiler(capture(successCallbackSlot), any())
            } answers
                {
                    successCallbackSlot.captured(fakeProfileData)
                }

            val collectedStates = mutableListOf<ProfilerUiState>()
            backgroundScope.launch {
                viewModel.uiState.toList(collectedStates)
            }

            runCurrent()

            viewModel.stopProfilerAndSave()

            // Execute everything up to the delay(50.milliseconds)
            runCurrent()

            assertEquals(
                listOf(
                    ProfilerUiState.Idle::class,
                    ProfilerUiState.Gathering::class,
                    ProfilerUiState.ShowToast::class,
                ),
                collectedStates.map { it::class },
            )

            val toastState = collectedStates.last()
            assertIs<ProfilerUiState.ShowToast>(toastState)
            assertEquals(
                R.string.profiler_uploaded_url_to_clipboard,
                toastState.messageResId,
            )

            // Advance past the delay before Finished is emitted.
            advanceTimeBy(viewModel.delayToUpdateStatus)
            runCurrent()

            assertEquals(
                listOf(
                    ProfilerUiState.Idle::class,
                    ProfilerUiState.Gathering::class,
                    ProfilerUiState.ShowToast::class,
                    ProfilerUiState.Finished::class,
                ),
                collectedStates.map { it::class },
            )

            val finishedState = collectedStates.last()
            assertIs<ProfilerUiState.Finished>(finishedState)
            assertEquals(expectedUrl, finishedState.profileUrl)

            verify { mockProfiler.stopProfiler(any(), any()) }
            verify { mockProfilerUtils.saveProfileUrlToClipboard(fakeProfileData, mockApplication) }
            verify { mockProfilerUtils.finishProfileSave(mockApplication, expectedUrl) }
        }

    @Test
    fun `WHEN the profiler fails during saving the profile THEN state changes to error and the error message is displayed`() =
        runTest(testDispatcher) {
            initializeViewModel(
                isInitiallyActive = true,
                mainDispatcher = testDispatcher,
                ioDispatcher = testDispatcher,
            )

            val exception = RuntimeException("Profiler Stop Failed")
            val errorCallbackSlot = slot<(Throwable) -> Unit>()

            every {
                mockProfiler.stopProfiler(any(), capture(errorCallbackSlot))
            } answers
                {
                    errorCallbackSlot.captured(exception)
                }

            val collectedStates = mutableListOf<ProfilerUiState>()
            backgroundScope.launch {
                viewModel.uiState.toList(collectedStates)
            }
            runCurrent()

            viewModel.stopProfilerAndSave()
            runCurrent()

            assertTrue(collectedStates.any { it is ProfilerUiState.Idle })
            assertTrue(collectedStates.any { it is ProfilerUiState.Gathering })
            val errorState = collectedStates.last()
            assertIs<ProfilerUiState.Error>(errorState)
            assertEquals(R.string.profiler_error, errorState.messageResId)
            assertEquals("Profiler Stop Failed", errorState.errorDetails)

            verify { mockProfiler.stopProfiler(any(), any()) }
            verify(exactly = 0) { mockProfilerUtils.saveProfileUrlToClipboard(any(), any()) }
        }

    @Test
    fun `WHEN data save after stopping the profiler THEN toast shows error and service stops`() =
        runTest(testDispatcher) {
            initializeViewModel(
                isInitiallyActive = true,
                mainDispatcher = testDispatcher,
                ioDispatcher = testDispatcher,
            )

            val fakeProfileData = "profile_data".toByteArray()
            val saveException = java.io.IOException("Disk full")
            val successCallbackSlot = slot<(ByteArray?) -> Unit>()

            every {
                mockProfiler.stopProfiler(capture(successCallbackSlot), any())
            } answers
                {
                    successCallbackSlot.captured(fakeProfileData)
                }

            every {
                mockProfilerUtils.saveProfileUrlToClipboard(fakeProfileData, mockApplication)
            } throws saveException

            val collectedStates = mutableListOf<ProfilerUiState>()
            backgroundScope.launch {
                viewModel.uiState.toList(collectedStates)
            }
            runCurrent()

            viewModel.stopProfilerAndSave()
            runCurrent()

            val expectedSequence =
                listOf(
                    ProfilerUiState.Idle::class,
                    ProfilerUiState.Gathering::class,
                    ProfilerUiState.Error::class,
                )
            val actualSequence = collectedStates.map { it::class }
            assertEquals("The sequence of UI states was not as expected", expectedSequence, actualSequence)

            val errorState = collectedStates.last() as ProfilerUiState.Error
            assertEquals(R.string.profiler_io_error, errorState.messageResId)
            assertEquals("Disk full", errorState.errorDetails)

            verify { mockProfiler.stopProfiler(any(), any()) }
            verify { mockProfilerUtils.saveProfileUrlToClipboard(fakeProfileData, mockApplication) }
            verify(exactly = 0) { mockProfilerUtils.finishProfileSave(any(), any()) }
        }

    @Test
    fun `WHEN the profiler is stopped without saving THEN the UI shows the profiler is stopped without a URL copied`() =
        runTest(testDispatcher) {
            initializeViewModel(
                isInitiallyActive = true,
                mainDispatcher = testDispatcher,
                ioDispatcher = testDispatcher,
            )

            val successCallbackSlot = slot<(ByteArray?) -> Unit>()
            every {
                mockProfiler.stopProfiler(capture(successCallbackSlot), any())
            } answers
                {
                    successCallbackSlot.captured(null)
                }

            val collectedUiStates = mutableListOf<ProfilerUiState>()
            backgroundScope.launch {
                viewModel.uiState.toList(collectedUiStates)
            }
            runCurrent()

            viewModel.stopProfilerWithoutSaving()
            runCurrent()

            assertEquals("Expected 3 state emissions: Initial, Stopping, Finished", 3, collectedUiStates.size)
            assertIs<ProfilerUiState.Idle>(collectedUiStates[0], "First collected state should be Idle")
            assertIs<ProfilerUiState.Stopping>(collectedUiStates[1], "Second collected state should be Stopping")
            val finalState = collectedUiStates[2]
            assertIs<ProfilerUiState.Finished>(finalState, "Third collected state should be Finished")
            assertNull("Profile URL should be null in the final Finished state", finalState.profileUrl)

            verify { mockProfiler.stopProfiler(any(), any()) }
            verify(exactly = 0) { mockProfilerUtils.saveProfileUrlToClipboard(any(), any()) }
        }

    @Test
    fun `GIVEN all profiler presets THEN they must include the java feature for polling to work`() {
        // This test ensures that all ProfilerSettings presets include the "java" feature.
        // The "java" feature is required for GeckoJavaSampler to start, which makes
        // isProfilerActive() return true. Without it, the polling mechanism in
        // pollUntilProfilerActiveAndThen() will timeout and fail (see bug 1994995).
        ProfilerSettings.entries.forEach { setting ->
            assertTrue(
                "ProfilerSettings.${setting.name} must include 'java' feature for isProfilerActive() to work",
                setting.features.contains("java"),
            )
        }
    }
}

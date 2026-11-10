/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.importer

import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.delay
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.fileimporter.FileImporter
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.concept.fileimporter.FileImporterException
import mozilla.components.concept.fileimporter.ImportResult
import org.junit.runner.RunWith

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class ImporterMiddlewareTest {

    private val scope = TestScope()
    private val importer = TestImporter()
    private lateinit var middleware: ImporterMiddleware

    @BeforeTest
    fun setUp() {
        middleware = ImporterMiddleware(importer = importer, lifecycleScope = scope)
    }

    @Test
    fun `when ImportStarted is received, the state is transitioned to Loading`() = runTest {
        val store = middleware.makeStore()

        store.dispatch(ImporterAction.ImportStarted)

        assertEquals(ImporterState.Loading, store.state)
    }

    @Test
    fun `when ImportCancelled action is received while import is in progress, the state is transitioned to Cancelled`() =
        runTest {
            // Given a store
            val store = middleware.makeStore()

            // Given that a file has been selected and the import is slow
            importer.simulateSlowImport = true
            store.dispatch(ImporterAction.FileSelected(uri = Uri.EMPTY))

            // Given that we have now imported halfway through
            scope.advanceTimeBy(TestImporter.ImportDelay / 2)

            // When we receive the canceled action
            store.dispatch(ImporterAction.ImportCancelled)

            // Then verify that the state is updated to canceled
            assertEquals(
                ImporterState.Finished(result = ImporterEvent.Canceled),
                store.state,
            )
        }

    @Test
    fun `when ImportCancelled action is received after a failed import, the state finishes with the same failures`() =
        runTest {
            // Given a store
            val store = middleware.makeStore()

            // And that a file has been selected and the import failed
            importer.expectedImportResult = Result.failure(Throwable("Booo"))
            store.dispatch(ImporterAction.FileSelected(uri = Uri.EMPTY))

            // And that the import has finished
            scope.advanceUntilIdle()

            // When we receive the canceled action
            store.dispatch(ImporterAction.ImportCancelled)

            // Then verify that the state remains "failed"
            val finalState = store.state
            assertIs<ImporterState.Finished>(finalState)
            assertIs<ImporterEvent.Failure>(finalState.result)
        }

    @Test
    fun `when ImportCancelled action is received after a successful import, the state finishes with the same success`() =
        runTest {
            // Given a store
            val store = middleware.makeStore()

            // And that a file has been selected and the import succeeded
            importer.expectedImportResult = Result.success(TestImportResult(count = 5))
            store.dispatch(ImporterAction.FileSelected(uri = Uri.EMPTY))

            // And that the import has finished
            scope.advanceUntilIdle()

            // When we receive the canceled action
            store.dispatch(ImporterAction.ImportCancelled)

            // Then verify that the state remains "success"
            val finalState = store.state
            assertIs<ImporterState.Finished>(finalState)
            assertIs<ImporterEvent.Success>(finalState.result)
        }

    @Test
    fun `when import fails with a FileImporterException, its error type is preserved`() = runTest {
        // Given a store
        val store = middleware.makeStore()

        // And that a file has been selected and the import failed with a specific error
        val error = FileImporterException(FileImporterError.ReadError, cause = Throwable("cannot open file"))
        importer.expectedImportResult = Result.failure(error)
        store.dispatch(ImporterAction.FileSelected(uri = Uri.EMPTY))

        // And that the import has finished
        scope.advanceUntilIdle()

        // Then verify that the state is "failed" and the error type is preserved
        val result = (store.state as? ImporterState.Finished)?.result

        assertIs<ImporterEvent.Failure>(result)
        assertEquals(FileImporterError.ReadError, result.error)
    }

    @Test
    fun `when import fails with a generic error, it is reported as an UnknownError`() = runTest {
        // Given a store
        val store = middleware.makeStore()

        // And that a file has been selected and the import failed with a generic throwable
        val cause = Throwable("Booo")
        importer.expectedImportResult = Result.failure(cause)
        store.dispatch(ImporterAction.FileSelected(uri = Uri.EMPTY))

        // And that the import has finished
        scope.advanceUntilIdle()

        // Then verify that the result is a failure reported as an UnknownError
        val result = (store.state as? ImporterState.Finished)?.result
        assertIs<ImporterEvent.Failure>(result)
        assertEquals(FileImporterError.UnknownError, result.error)
    }

    @Test
    fun `GIVEN no file was picked WHEN FileSelected is received THEN the import is not started and the state is Canceled`() =
        runTest {
            val store = middleware.makeStore()

            store.dispatch(ImporterAction.FileSelected(uri = null))
            scope.advanceUntilIdle()

            assertEquals(0, importer.importCount)
            assertEquals(ImporterState.Finished(result = ImporterEvent.Canceled), store.state)
        }

    private fun ImporterMiddleware.makeStore(initialState: ImporterState = ImporterState.Inert): ImporterStore {
        return ImporterStore(
            initialState = initialState,
            middleware = listOf(this),
            reducer = { _, action -> importerReducer(action) },
        )
    }

    private data class TestImportResult(override val count: Int) : ImportResult

    private class TestImporter : FileImporter<TestImportResult> {

        override val mimeType = "text/plain"

        var expectedImportResult: Result<TestImportResult> = Result.failure(Throwable("Uh uh"))
        var simulateSlowImport = false
        var importCount = 0

        override suspend fun importFromUri(uri: Uri): Result<TestImportResult> {
            importCount++
            if (simulateSlowImport) {
                delay(ImportDelay)
            }
            return expectedImportResult
        }

        companion object {
            val ImportDelay = 10.seconds
        }
    }
}

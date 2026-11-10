/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.importer

import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import mozilla.components.concept.fileimporter.FileImporter
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.concept.fileimporter.FileImporterException
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * [Middleware] that handles side-effects for [ImporterAction]s.
 *
 * @param importer The [FileImporter] that imports the selected file.
 * @param lifecycleScope [CoroutineScope] used to launch import work.
 */
class ImporterMiddleware(
    private val importer: FileImporter<*>,
    private val lifecycleScope: CoroutineScope,
) : Middleware<ImporterState, ImporterAction> {
    private var importJob: Job? = null

    override fun invoke(
        store: Store<ImporterState, ImporterAction>,
        next: (ImporterAction) -> Unit,
        action: ImporterAction,
    ) {
        val actionResult =
            when (action) {
                is ImporterAction.FileSelected -> {
                    action.uri?.let {
                        importJob = lifecycleScope.launch {
                            // We want to make sure we stay in the loading state for at least one second
                            // during an import to prevent the dialog from flashing before the user can
                            // comprehend what is currently happening.
                            // Previous implementation of this waiting strategy in bug 2035342 used
                            // the async delay awaitAll approach.
                            // However, we often ran into edge cases where users canceled within the
                            // "minimum wait" time, and because we already started (maybe finished) inserting
                            // the bookmarks, the cancelation had no effect, thereby, confusing the users
                            // We will try and simplify using a simple delay to achieve a similar effect.
                            delay(1.seconds)
                            importer
                                .importFromUri(it)
                                .onFailure { exception ->
                                    val error =
                                        (exception as? FileImporterException)?.errorType
                                            ?: FileImporterError.UnknownError
                                    store.dispatch(ImporterAction.ImportFailed(error))
                                }
                                .onSuccess { importResult ->
                                    store.dispatch(ImporterAction.ImportFinished(importResult.count))
                                }
                        }
                    }
                    ActionResult.ContinueChain
                }

                is ImporterAction.ImportCancelled -> {
                    // Bug 2039867: we want to make sure that if the import is ongoing, we cancel the job
                    // and propagate the canceled state.
                    //
                    // Otherwise, we swallow the action and do not pass it down
                    // to prevent state change, since we did not really cancel anything.
                    if (importJob?.isActive == true) {
                        importJob?.cancel()
                        importJob = null
                        ActionResult.ContinueChain
                    } else {
                        ActionResult.SwallowAction
                    }
                }

                ImporterAction.ImportStarted,
                ImporterAction.ViewAppeared,
                is ImporterAction.ImportFinished,
                is ImporterAction.ImportFailed -> ActionResult.ContinueChain
            }

        when (actionResult) {
            ActionResult.ContinueChain -> next(action)
            else -> Unit
        }
    }

    /**
     * Enum type to decide what we do after processing an action. We need this in this middleware because there are
     * cases where we want to prevent an action from being passed down the chain (reducer + middleware).
     */
    private enum class ActionResult {

        /** A result based on which, we want to continue the reducer + middleware chain */
        ContinueChain,

        /** A result based on which, we want to drop the action from the chain and swallow it */
        SwallowAction,
    }
}

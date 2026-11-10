/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.importer

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.lifecycle.viewmodel.compose.viewModel
import mozilla.components.concept.fileimporter.FileImporter
import mozilla.components.lib.state.ext.observeAsComposableState

/**
 * Self-contained import flow that drives file selection, the in-progress dialog, and completion via an internal
 * [ImporterStore].
 *
 * @param importer The [FileImporter] that imports the selected file.
 * @param dialogTitle Title of the in-progress dialog, describing what is being imported.
 * @param onEventReceived Invoked when the import flow has received an import event.
 */
@Composable
fun ImporterScreen(
    importer: FileImporter<*>,
    dialogTitle: String,
    onEventReceived: (ImporterEvent) -> Unit,
) {
    val store = viewModel<ImporterViewModel>(factory = ImporterViewModel.factory(importer)).store
    val state = store.observeAsComposableState { it }.value

    when (state) {
        ImporterState.Inert -> {
            LaunchedEffect(Unit) {
                store.dispatch(ImporterAction.ViewAppeared)
            }
        }
        ImporterState.SelectingFile -> {
            FilePicker(
                mimeType = importer.mimeType,
                onFileSelected = { uri -> store.dispatch(ImporterAction.FileSelected(uri)) },
            )
        }
        ImporterState.Loading -> {
            ImporterDialog(
                title = dialogTitle,
                onImportStarted = {
                    store.dispatch(ImporterAction.ImportStarted)
                    onEventReceived(ImporterEvent.Started)
                },
                onCancel = {
                    store.dispatch(ImporterAction.ImportCancelled)
                },
            )
        }
        is ImporterState.Finished -> {
            LaunchedEffect(state) {
                onEventReceived(state.result)
            }
        }
    }
}

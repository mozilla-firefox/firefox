/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.importer

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import mozilla.components.concept.fileimporter.FileImporter

/**
 * [ViewModel] that owns the [ImporterStore] for the import flow so that its state and side-effects survive
 * configuration changes.
 */
internal class ImporterViewModel(importer: FileImporter<*>) : ViewModel() {
    val store =
        ImporterStore(
            initialState = ImporterState.Inert,
            reducer = { _, action -> importerReducer(action) },
            middleware = listOf(ImporterMiddleware(importer, viewModelScope)),
        )

    companion object {
        fun factory(importer: FileImporter<*>) =
            object : ViewModelProvider.Factory {
                @Suppress("UNCHECKED_CAST")
                override fun <T : androidx.lifecycle.ViewModel> create(modelClass: Class<T>): T {
                    return ImporterViewModel(importer) as T
                }
            }
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.importer

import mozilla.components.feature.importer.ImporterEvent.Canceled
import mozilla.components.feature.importer.ImporterEvent.Failure
import mozilla.components.feature.importer.ImporterEvent.Success
import mozilla.components.feature.importer.ImporterState.Finished
import mozilla.components.feature.importer.ImporterState.Loading
import mozilla.components.feature.importer.ImporterState.SelectingFile

/** Reduces the given [action] into a new [ImporterState]. */
fun importerReducer(action: ImporterAction): ImporterState =
    when (action) {
        ImporterAction.ViewAppeared -> SelectingFile
        is ImporterAction.FileSelected -> if (action.uri == null) Finished(Canceled) else Loading
        ImporterAction.ImportStarted -> Loading
        is ImporterAction.ImportFinished -> Finished(Success(action.itemsCount))
        is ImporterAction.ImportFailed -> Finished(Failure(action.error))
        ImporterAction.ImportCancelled -> Finished(Canceled)
    }

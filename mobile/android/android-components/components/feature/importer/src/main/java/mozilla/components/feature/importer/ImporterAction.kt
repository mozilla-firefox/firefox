/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.importer

import android.net.Uri
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.lib.state.Action

/** Actions for the [ImporterStore]. */
sealed interface ImporterAction : Action {

    /** The import UI became visible. */
    data object ViewAppeared : ImporterAction

    /**
     * The file picker returned.
     *
     * @property uri the picked file, or `null` if selection was canceled.
     */
    data class FileSelected(val uri: Uri?) : ImporterAction

    /** An import started. */
    data object ImportStarted : ImporterAction

    /**
     * The in-progress import completed successfully.
     *
     * @property itemsCount the number of items imported.
     */
    data class ImportFinished(val itemsCount: Int) : ImporterAction

    /** The in-progress import completed with a failure. */
    data class ImportFailed(val error: FileImporterError) : ImporterAction

    /** The user canceled the in-progress import. */
    data object ImportCancelled : ImporterAction
}

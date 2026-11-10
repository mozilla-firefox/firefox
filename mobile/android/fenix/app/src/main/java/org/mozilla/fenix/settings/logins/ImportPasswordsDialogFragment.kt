/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.annotation.VisibleForTesting
import androidx.compose.ui.res.stringResource
import androidx.fragment.app.DialogFragment
import androidx.fragment.compose.content
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.coroutineScope
import androidx.lifecycle.withStarted
import kotlinx.coroutines.launch
import mozilla.components.concept.fileimporter.FileImporter
import mozilla.components.feature.importer.ImporterEvent
import mozilla.components.feature.importer.ImporterScreen
import mozilla.components.feature.importer.R as importerR
import mozilla.components.lib.passwords.file.csvImporter
import mozilla.components.support.utils.ext.getParcelableCompat
import org.mozilla.fenix.ext.requireComponents

internal class ImportPasswordsDialogFragment : DialogFragment() {
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = content {
        ImporterScreen(
            importer =
                FileImporter.csvImporter(
                    context = requireContext(),
                    loginsStorage = requireComponents.core.passwordsStorage,
                ),
            dialogTitle = stringResource(importerR.string.mozac_feature_passwords_importer_dialog_title),
            onEventReceived = { event ->
                parentFragmentManager.setFragmentResult(
                    REQUEST_KEY,
                    Bundle().apply { putParcelable(KEY_RESULT, event) },
                )
                dismissWhenFinished(event, lifecycle) { dismiss() }
            },
        )
    }

    companion object {
        const val REQUEST_KEY = "import_passwords_request"
        const val TAG = "import_passwords_dialog"

        private const val KEY_RESULT = "result"

        fun decodeResult(bundle: Bundle): ImporterEvent? =
            bundle.getParcelableCompat(KEY_RESULT, ImporterEvent::class.java)
    }
}

@VisibleForTesting
internal fun dismissWhenFinished(event: ImporterEvent, lifecycle: Lifecycle, dismiss: () -> Unit) {
    if (event is ImporterEvent.Started) {
        return
    }

    lifecycle.coroutineScope.launch { lifecycle.withStarted(dismiss) }
}

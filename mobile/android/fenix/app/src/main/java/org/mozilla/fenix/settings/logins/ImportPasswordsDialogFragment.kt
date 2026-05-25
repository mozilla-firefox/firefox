/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.DialogFragment
import androidx.fragment.compose.content
import mozilla.components.concept.passwords.file.PasswordsFileImporter
import mozilla.components.feature.password.importer.PasswordsImporter
import mozilla.components.feature.password.importer.PasswordsImporterResult
import mozilla.components.lib.passwords.file.csvImporter
import org.mozilla.fenix.ext.requireComponents

internal class ImportPasswordsDialogFragment : DialogFragment() {
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = content {
        PasswordsImporter(
            importer = PasswordsFileImporter.csvImporter(
                context = requireContext(),
                loginsStorage = requireComponents.core.passwordsStorage,
            ),
            onFinished = { result ->
                parentFragmentManager.setFragmentResult(REQUEST_KEY, encodeResult(result))
                dismiss()
            },
        )
    }

    companion object {
        const val REQUEST_KEY = "import_passwords_request"
        const val TAG = "import_passwords_dialog"

        private const val KEY_RESULT = "result"
        private const val KEY_COUNT = "count"
        private const val RESULT_SUCCESS = "success"
        private const val RESULT_FAILURE = "failure"
        private const val RESULT_CANCELLED = "cancelled"

        fun decodeResult(bundle: Bundle): PasswordsImporterResult? =
            when (bundle.getString(KEY_RESULT)) {
                RESULT_SUCCESS -> PasswordsImporterResult.Success(importCount = bundle.getInt(KEY_COUNT))
                RESULT_FAILURE -> PasswordsImporterResult.Failure
                RESULT_CANCELLED -> PasswordsImporterResult.Canceled
                else -> null
            }

        private fun encodeResult(result: PasswordsImporterResult): Bundle = Bundle().apply {
            when (result) {
                is PasswordsImporterResult.Success -> {
                    putString(KEY_RESULT, RESULT_SUCCESS)
                    putInt(KEY_COUNT, result.importCount)
                }
                PasswordsImporterResult.Failure -> putString(KEY_RESULT, RESULT_FAILURE)
                PasswordsImporterResult.Canceled -> putString(KEY_RESULT, RESULT_CANCELLED)
            }
        }
    }
}

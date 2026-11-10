/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import androidx.annotation.VisibleForTesting
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.lib.password.parser.csv.PasswordsParseError
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Logins

internal class LoginsTelemetryMiddleware : Middleware<LoginsState, LoginsAction> {

    override fun invoke(
        store: Store<LoginsState, LoginsAction>,
        next: (LoginsAction) -> Unit,
        action: LoginsAction,
    ) {
        next(action)
        when (action) {
            is LoginClicked -> {
                Logins.managementLoginsTapped.record(NoExtras())
                Logins.openIndividualLogin.record(NoExtras())
            }
            AddLoginAction.InitAdd -> {
                Logins.managementAddTapped.record(NoExtras())
            }
            AddLoginAction.AddLoginSaveClicked -> {
                Logins.saved.add()
            }
            is DetailLoginAction.CopyPasswordClicked -> {
                Logins.copyLogin.record(NoExtras())
            }
            is DetailLoginAction.CopyUsernameClicked -> {
                Logins.copyLogin.record(NoExtras())
            }
            is DetailLoginAction.PasswordVisibilityChanged -> {
                if (action.isPasswordVisible) {
                    Logins.viewPasswordLogin.record(NoExtras())
                }
            }
            is DetailLoginMenuAction.EditLoginMenuItemClicked -> {
                Logins.openLoginEditor.record(NoExtras())
            }
            is EditLoginAction.PasswordVisibilityChanged -> {
                if (action.isPasswordVisible) {
                    Logins.viewPasswordLogin.record(NoExtras())
                }
            }
            is EditLoginAction.SaveEditClicked -> {
                Logins.saveEditedLogin.record(NoExtras())
                Logins.modified.add()
            }
            is ImportAction -> handleImportAction(action)
            LoginDeletionDialogAction.DeleteTapped -> {
                Logins.deleteSavedLogin.record(NoExtras())
                Logins.deleted.add()
            }
            is AddLoginAction.HostChanged,
            is AddLoginAction.PasswordChanged,
            is AddLoginAction.UsernameChanged,
            AddLoginBackClicked,
            is DetailLoginAction.GoToSiteClicked,
            is DetailLoginMenuAction.DeleteLoginMenuItemClicked,
            is EditLoginAction.PasswordChanged,
            is EditLoginAction.UsernameChanged,
            EditLoginBackClicked,
            LearnMoreAboutSync,
            LoginDeletionDialogAction.CancelTapped,
            LoginsDetailBackClicked,
            LoginsListAppeared,
            LoginsListBackClicked,
            LoginsListSortMenuAction.OrderByLastUsedClicked,
            LoginsListSortMenuAction.OrderByNameClicked,
            is LoginsLoaded,
            is SearchLogins,
            is ImportPasswordsOverflowMenuClicked,
            is ImportPasswordsOverflowMenuDismissed -> Unit
        }
    }

    private fun handleImportAction(action: ImportAction) =
        when (action) {
            is ImportAction.ImportFileClicked -> Logins.importFromFileMenuClick.record(NoExtras())
            is ImportAction.ImportStarted -> Logins.importStarted.record(NoExtras())
            is ImportAction.ImportSucceeded ->
                Logins.importSuccessful.record(extra = Logins.ImportSuccessfulExtra(loginsCount = action.count))
            is ImportAction.ImportFailed ->
                Logins.importFailed.record(
                    extra = Logins.ImportFailedExtra(errorCode = action.error.telemetryCode().toString())
                )
            is ImportAction.ImportCancelled -> Logins.importCancelled.record(NoExtras())
        }

    private fun FileImporterError.telemetryCode(): Int =
        when (this) {
            FileImporterError.ReadError -> ImportErrorCode.FILE_READ_ERROR
            is FileImporterError.ParseError ->
                when (error) {
                    PasswordsParseError.UnsupportedContentType -> ImportErrorCode.PARSE_ERROR_UNSUPPORTED_CONTENT_TYPE
                    PasswordsParseError.FileFormat -> ImportErrorCode.PARSE_ERROR_INVALID_FORMAT
                    PasswordsParseError.ConflictingColumns -> ImportErrorCode.PARSE_ERROR_CONFLICTING_COLUMNS
                    else -> ImportErrorCode.PARSE_ERROR_UNKNOWN
                }
            FileImporterError.SaveError -> ImportErrorCode.PASSWORDS_SAVE_ERROR
            else -> ImportErrorCode.UNKNOWN_ERROR
        }
}

/** Error codes recorded with the `logins.import_failed` event, as documented in metrics.yaml. */
@VisibleForTesting
internal object ImportErrorCode {
    const val FILE_READ_ERROR = 1001
    const val PARSE_ERROR_UNSUPPORTED_CONTENT_TYPE = 2001
    const val PARSE_ERROR_INVALID_FORMAT = 2002
    const val PARSE_ERROR_UNKNOWN = 2003
    const val PARSE_ERROR_CONFLICTING_COLUMNS = 2004
    const val PASSWORDS_SAVE_ERROR = 3001
    const val UNKNOWN_ERROR = 4001
}

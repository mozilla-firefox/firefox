/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.lib.state.Action

/** Actions relating to the Logins list screen and its various subscreens. */
internal sealed interface LoginsAction : Action

internal data object LoginsListAppeared : LoginsAction

internal data object LoginsListBackClicked : LoginsAction

/**
 * Logins have been loaded from the storage layer.
 *
 * @property loginItems The login items loaded, transformed into a displayable type.
 */
internal data class LoginsLoaded(val loginItems: List<LoginItem>) : LoginsAction

internal sealed class LoginsListSortMenuAction : LoginsAction {
    data object OrderByNameClicked : LoginsListSortMenuAction()

    data object OrderByLastUsedClicked : LoginsListSortMenuAction()
}

internal data class SearchLogins(val searchText: String, val loginItems: List<LoginItem>) : LoginsAction

internal data object ImportPasswordsOverflowMenuClicked : LoginsAction

internal data object ImportPasswordsOverflowMenuDismissed : LoginsAction

internal sealed class ImportAction : LoginsAction {
    internal data object ImportFileClicked : ImportAction()

    internal data object ImportStarted : ImportAction()

    internal data class ImportSucceeded(val count: Int) : ImportAction()

    internal data class ImportFailed(val error: FileImporterError) : ImportAction()

    internal data object ImportCancelled : ImportAction()
}

internal data object LearnMoreAboutSync : LoginsAction

internal data class LoginClicked(val item: LoginItem) : LoginsAction

internal sealed class DetailLoginMenuAction : LoginsAction {
    data class EditLoginMenuItemClicked(val item: LoginItem) : DetailLoginMenuAction()

    data class DeleteLoginMenuItemClicked(val item: LoginItem) : DetailLoginMenuAction()
}

internal sealed class LoginDeletionDialogAction : LoginsAction {
    data object CancelTapped : LoginDeletionDialogAction()

    data object DeleteTapped : LoginDeletionDialogAction()
}

internal data object LoginsDetailBackClicked : LoginsAction

internal data object AddLoginBackClicked : LoginsAction

internal data object EditLoginBackClicked : LoginsAction

internal sealed class EditLoginAction : LoginsAction {
    data class UsernameChanged(val usernameChanged: String) : EditLoginAction()

    data class PasswordChanged(val passwordChanged: String) : EditLoginAction()

    data class PasswordVisibilityChanged(val isPasswordVisible: Boolean) : EditLoginAction()

    data class SaveEditClicked(val login: LoginItem) : EditLoginAction()
}

internal sealed class AddLoginAction : LoginsAction {
    data object InitAdd : AddLoginAction()

    data object AddLoginSaveClicked : AddLoginAction()

    data class HostChanged(val hostChanged: String) : AddLoginAction()

    data class UsernameChanged(val usernameChanged: String) : AddLoginAction()

    data class PasswordChanged(val passwordChanged: String) : AddLoginAction()
}

internal sealed class DetailLoginAction : LoginsAction {
    data class GoToSiteClicked(val url: String) : DetailLoginAction()

    data class CopyUsernameClicked(val username: String) : DetailLoginAction()

    data class CopyPasswordClicked(val password: String) : DetailLoginAction()

    data class PasswordVisibilityChanged(val isPasswordVisible: Boolean) : DetailLoginAction()
}

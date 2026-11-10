/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

/** Function for reducing a new logins state based on the received action. */
internal fun loginsReducer(state: LoginsState, action: LoginsAction) =
    when (action) {
        is LoginsLoaded -> {
            state.handleLoginsLoadedAction(action)
        }
        is LoginsListSortMenuAction -> {
            state.handleSortMenuAction(action)
        }
        is SearchLogins -> {
            state.handleSearchLogins(action)
        }
        is LoginClicked -> state.copy(loginsLoginDetailState = LoginsLoginDetailState(action.item))
        is LoginsDetailBackClicked -> state.respondToLoginsDetailBackClick()
        is EditLoginAction ->
            state.loginsEditLoginState?.let {
                state.handleEditLoginAction(action)
            } ?: state
        is AddLoginAction ->
            state.loginsAddLoginState?.let {
                state.handleAddLoginAction(action)
            }
                ?: state.copy(
                    loginsAddLoginState =
                        LoginsAddLoginState(
                            host = "",
                            username = "",
                            password = "",
                        ),
                    newLoginState = NewLoginState.None,
                )
        is DetailLoginAction -> state
        is DetailLoginMenuAction.EditLoginMenuItemClicked ->
            state.copy(
                loginsEditLoginState =
                    LoginsEditLoginState(
                        login = action.item,
                        newUsername = action.item.username,
                        newPassword = action.item.password,
                        isPasswordVisible = true,
                    ),
                updateLoginState = UpdateLoginState.None,
            )
        is DetailLoginMenuAction.DeleteLoginMenuItemClicked ->
            state.copy(loginDeletionDialogState = LoginDeletionDialogState.Presenting(action.item.guid))
        is LoginDeletionDialogAction.DeleteTapped ->
            state
                .withDeletedLoginRemoved()
                .copy(loginDeletionDialogState = LoginDeletionDialogState.None, loginsLoginDetailState = null)
        is LoginDeletionDialogAction.CancelTapped ->
            state.copy(loginDeletionDialogState = LoginDeletionDialogState.None)
        is LoginsListBackClicked -> state.respondToLoginsListBackClick()
        is AddLoginBackClicked -> state.respondToAddLoginBackClick()
        is EditLoginBackClicked -> state.respondToEditLoginBackClick()
        is LoginsListAppeared,
        LearnMoreAboutSync -> state
        is ImportPasswordsOverflowMenuClicked -> state.copy(importPasswordsMenuShown = true)
        is ImportPasswordsOverflowMenuDismissed -> state.copy(importPasswordsMenuShown = false)
        is ImportAction.ImportFileClicked -> state.copy(importPasswordsMenuShown = false)
        is ImportAction.ImportStarted,
        is ImportAction.ImportSucceeded,
        is ImportAction.ImportFailed,
        is ImportAction.ImportCancelled -> state
    }

private fun LoginsState.withDeletedLoginRemoved(): LoginsState =
    when {
        loginDeletionDialogState is LoginDeletionDialogState.Presenting ->
            copy(loginItems = loginItems.filterNot { it.guid == loginDeletionDialogState.guidToDelete })

        else -> this
    }

private fun LoginsState.handleSearchLogins(action: SearchLogins): LoginsState =
    copy(
        searchText = action.searchText,
        loginItems =
            action.loginItems.filter {
                it.url.contains(
                    other = action.searchText,
                    ignoreCase = true,
                ) ||
                    it.username.contains(
                        other = action.searchText,
                        ignoreCase = true,
                    )
            },
    )

private fun LoginsState.handleLoginsLoadedAction(action: LoginsLoaded): LoginsState =
    copy(
        loginItems =
            if (searchText.isNullOrEmpty()) {
                action.loginItems.sortedWith(sortOrder.comparator)
            } else {
                action.loginItems.sortedWith(sortOrder.comparator).filter {
                    it.url.contains(
                        other = searchText,
                        ignoreCase = true,
                    ) || it.username.contains(other = searchText, ignoreCase = true)
                }
            }
    )

private fun LoginsState.respondToLoginsListBackClick(): LoginsState =
    when {
        loginsListState != null -> copy(loginsListState = null)
        else -> this
    }

private fun LoginsState.handleSortMenuAction(action: LoginsListSortMenuAction): LoginsState =
    when (action) {
        LoginsListSortMenuAction.OrderByLastUsedClicked -> copy(sortOrder = LoginsSortOrder.LastUsed)
        LoginsListSortMenuAction.OrderByNameClicked -> copy(sortOrder = LoginsSortOrder.Alphabetical)
    }.let {
        it.copy(loginItems = it.loginItems.sortedWith(it.sortOrder.comparator))
    }

private fun LoginsState.handleEditLoginAction(action: EditLoginAction): LoginsState =
    when (action) {
        is EditLoginAction.UsernameChanged ->
            copy(
                loginsEditLoginState = this.loginsEditLoginState?.copy(newUsername = action.usernameChanged),
                updateLoginState =
                    if (
                        loginItems.hasDuplicate(
                            host = this.loginsEditLoginState?.login?.url,
                            username = action.usernameChanged,
                            guid = this.loginsEditLoginState?.login?.guid,
                        )
                    ) {
                        UpdateLoginState.Duplicate
                    } else {
                        UpdateLoginState.None
                    },
            )
        is EditLoginAction.PasswordChanged ->
            copy(loginsEditLoginState = this.loginsEditLoginState?.copy(newPassword = action.passwordChanged))
        is EditLoginAction.PasswordVisibilityChanged ->
            copy(loginsEditLoginState = this.loginsEditLoginState?.copy(isPasswordVisible = action.isPasswordVisible))
        else -> this
    }

private fun LoginsState.handleAddLoginAction(action: AddLoginAction): LoginsState =
    when (action) {
        is AddLoginAction.InitAdd ->
            copy(
                loginsAddLoginState =
                    LoginsAddLoginState(
                        host = "",
                        username = "",
                        password = "",
                    )
            )
        is AddLoginAction.AddLoginSaveClicked -> copy(newLoginState = NewLoginState.None)
        is AddLoginAction.HostChanged ->
            copy(
                loginsAddLoginState = this.loginsAddLoginState?.copy(host = action.hostChanged),
                newLoginState =
                    if (
                        loginItems.hasDuplicate(
                            action.hostChanged,
                            this.loginsAddLoginState?.username,
                        )
                    ) {
                        NewLoginState.Duplicate
                    } else {
                        NewLoginState.None
                    },
            )
        is AddLoginAction.UsernameChanged ->
            copy(
                loginsAddLoginState = this.loginsAddLoginState?.copy(username = action.usernameChanged),
                newLoginState =
                    if (
                        loginItems.hasDuplicate(
                            this.loginsAddLoginState?.host,
                            action.usernameChanged,
                        )
                    ) {
                        NewLoginState.Duplicate
                    } else {
                        NewLoginState.None
                    },
            )
        is AddLoginAction.PasswordChanged ->
            copy(loginsAddLoginState = this.loginsAddLoginState?.copy(password = action.passwordChanged))
    }

private fun LoginsState.respondToLoginsDetailBackClick(): LoginsState =
    when {
        loginsLoginDetailState != null ->
            copy(
                loginsLoginDetailState = null,
                loginDeletionDialogState = LoginDeletionDialogState.None,
            )

        else -> this
    }

private fun LoginsState.respondToAddLoginBackClick(): LoginsState =
    when {
        loginsAddLoginState != null ->
            copy(
                newLoginState = null,
                loginsAddLoginState = null,
            )

        else -> this
    }

private fun LoginsState.respondToEditLoginBackClick(): LoginsState =
    when {
        loginsEditLoginState != null ->
            copy(
                loginsEditLoginState = null,
                updateLoginState = UpdateLoginState.None,
            )

        else -> this
    }

private fun List<LoginItem>.hasDuplicate(
    host: String?,
    username: String?,
    guid: String? = null,
): Boolean = this.isNotEmpty() && this.any { it.url == host && it.username == username && guid != it.guid }

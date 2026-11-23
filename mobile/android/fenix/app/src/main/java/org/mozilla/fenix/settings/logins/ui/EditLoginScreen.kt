/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.input.TextObfuscationMode
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.map
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.textfield.SecureTextField
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.compose.base.textfield.rememberSyncedTextFieldState
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

@Composable
internal fun EditLoginScreen(store: LoginsStore) {
    val state by store.stateFlow.collectAsState()
    val editState = state.loginsEditLoginState ?: return

    Scaffold(
        topBar = {
            EditLoginTopBar(
                store = store,
                loginItem = editState.login,
            )
        },
        modifier = Modifier.semantics { testTagsAsResourceId = true },
    ) { paddingValues ->
        Column(
            modifier = Modifier.padding(paddingValues).fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
            EditLoginUrl(url = editState.login.url)
            Spacer(modifier = Modifier.height(8.dp))
            EditLoginUsername(store = store, user = editState.login.username)
            Spacer(modifier = Modifier.height(8.dp))
            EditLoginPassword(store = store, pass = editState.login.password)
        }
    }
}

@Composable
internal fun EditLoginTopBar(store: LoginsStore, loginItem: LoginItem) {
    val state by remember {
        store.stateFlow.map { it.loginsEditLoginState }
    }
        .collectAsState(initial = store.state.loginsEditLoginState)
    val updateState by remember {
        store.stateFlow.map { it.updateLoginState }
    }
        .collectAsState(initial = store.state.updateLoginState)
    val username = state?.newUsername ?: loginItem.username
    val password = state?.newPassword ?: loginItem.password

    val validModifiedUser =
        username.isNotBlank() && username != loginItem.username && updateState != UpdateLoginState.Duplicate
    val validModifiedPassword = password.isNotBlank() && password != loginItem.password
    val isLoginValid = validModifiedUser || validModifiedPassword

    TopAppBar(
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
        title = {
            Text(
                text = stringResource(R.string.edit_2),
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = { store.dispatch(EditLoginBackClicked) },
                contentDescription = stringResource(R.string.edit_login_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        actions = {
            IconButton(
                onClick = {
                    store.dispatch(EditLoginAction.SaveEditClicked(loginItem))
                },
                contentDescription = stringResource(R.string.edit_login_button_content_description),
                enabled = isLoginValid,
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_checkmark_24),
                    contentDescription = null,
                )
            }
        },
    )
}

@Composable
private fun EditLoginUrl(url: String) {
    Text(
        text = stringResource(R.string.preferences_passwords_saved_logins_site),
        style = FirefoxTheme.typography.caption,
        modifier =
            Modifier.padding(horizontal = FirefoxTheme.layout.space.static200)
                .width(FirefoxTheme.layout.size.containerMaxWidth),
    )

    Text(
        text = url,
        style = FirefoxTheme.typography.subtitle1,
        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f),
        modifier =
            Modifier.padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = FirefoxTheme.layout.space.static100,
                )
                .width(FirefoxTheme.layout.size.containerMaxWidth),
    )
}

@Composable
private fun EditLoginUsername(store: LoginsStore, user: String) {
    val editState by remember {
        store.stateFlow.map { it.loginsEditLoginState }
    }
        .collectAsState(initial = store.state.loginsEditLoginState)
    val updateLoginState by remember {
        store.stateFlow.map { it.updateLoginState }
    }
        .collectAsState(initial = store.state.updateLoginState)
    val username = editState?.newUsername ?: user

    TextField(
        value = username,
        onValueChange = { newUsername ->
            store.dispatch(EditLoginAction.UsernameChanged(newUsername))
        },
        placeholder = "",
        errorText =
            if (username.isBlank()) {
                stringResource(R.string.saved_login_username_required_2)
            } else {
                stringResource(R.string.saved_login_duplicate)
            },
        isError = username.isBlank() || updateLoginState == UpdateLoginState.Duplicate,
        modifier =
            Modifier.padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = FirefoxTheme.layout.space.static100,
                )
                .width(FirefoxTheme.layout.size.containerMaxWidth)
                .semantics {
                    testTag = LoginsTestingTags.EDIT_LOGIN_USERNAME_TEXT_FIELD
                },
        label = stringResource(R.string.preferences_passwords_saved_logins_username),
        trailingIcon = {
            if (editState?.newUsername?.isNotEmpty() == true) {
                CrossTextFieldButton(contentDescription = Text.Resource(R.string.saved_login_clear_username)) {
                    store.dispatch(EditLoginAction.UsernameChanged(""))
                }
            }
        },
    )
}

@Composable
private fun EditLoginPassword(store: LoginsStore, pass: String) {
    val editState by remember {
        store.stateFlow.map { it.loginsEditLoginState }
    }
        .collectAsState(initial = store.state.loginsEditLoginState)
    val isPasswordVisible = editState?.isPasswordVisible ?: true
    val password = editState?.newPassword ?: pass
    val passwordState =
        rememberSyncedTextFieldState(value = password) { newPassword ->
            store.dispatch(EditLoginAction.PasswordChanged(newPassword))
        }

    val focusRequester = remember { FocusRequester() }
    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
    }

    Row(verticalAlignment = Alignment.CenterVertically) {
        SecureTextField(
            state = passwordState,
            placeholder = "",
            errorText = stringResource(R.string.saved_login_password_required_2),
            isError = password.isBlank(),
            modifier =
                Modifier.padding(
                        horizontal = FirefoxTheme.layout.space.static200,
                        vertical = FirefoxTheme.layout.space.static100,
                    )
                    .width(FirefoxTheme.layout.size.containerMaxWidth)
                    .semantics {
                        testTag = LoginsTestingTags.EDIT_LOGIN_PASSWORD_TEXT_FIELD
                    }
                    .focusRequester(focusRequester),
            label = stringResource(R.string.preferences_passwords_saved_logins_password),
            trailingIcon = {
                EyePasswordIconButton(
                    contentDescription =
                        if (isPasswordVisible) {
                            Text.Resource(R.string.saved_login_hide_password)
                        } else {
                            Text.Resource(R.string.saved_login_reveal_password)
                        },
                    isPasswordVisible = isPasswordVisible,
                    onTrailingIconClick = {
                        store.dispatch(EditLoginAction.PasswordVisibilityChanged(!isPasswordVisible))
                    },
                )
                if (editState?.newPassword?.isNotEmpty() == true) {
                    CrossTextFieldButton(contentDescription = Text.Resource(R.string.saved_logins_clear_password)) {
                        store.dispatch(EditLoginAction.PasswordChanged(""))
                    }
                }
            },
            textObfuscationMode =
                if (isPasswordVisible) {
                    TextObfuscationMode.Visible
                } else {
                    TextObfuscationMode.System
                },
        )
    }
}

private fun createStore() =
    LoginsStore(
        initialState =
            LoginsState.default.copy(
                loginsEditLoginState =
                    LoginsEditLoginState(
                        login =
                            LoginItem(
                                guid = "123",
                                url = "https://www.justanothersite123.com",
                                username = "username 123",
                                password = "password 123",
                            ),
                        newUsername = "username 456",
                        newPassword = "password 456",
                        isPasswordVisible = true,
                    )
            )
    )

@FlexibleWindowPreview
@Composable
private fun EditLoginScreenPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        Surface {
            EditLoginScreen(store = createStore())
        }
    }
}

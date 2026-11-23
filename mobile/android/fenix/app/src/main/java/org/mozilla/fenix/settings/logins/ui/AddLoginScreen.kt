/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import android.util.Patterns
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
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
import mozilla.components.support.ktx.util.URLStringUtils.isHttpOrHttps
import mozilla.components.support.ktx.util.URLStringUtils.isValidHost
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

@Composable
internal fun AddLoginScreen(store: LoginsStore) {
    Scaffold(
        topBar = {
            AddLoginTopBar(store)
        },
        modifier =
            Modifier.semantics {
                testTagsAsResourceId = true
            },
    ) { paddingValues ->
        Column(
            modifier = Modifier.padding(paddingValues).fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
            AddLoginHost(store = store)
            Spacer(modifier = Modifier.height(8.dp))
            AddLoginUsername(store = store)
            Spacer(modifier = Modifier.height(8.dp))
            AddLoginPassword(store = store)
        }
    }
}

@Composable
private fun AddLoginTopBar(store: LoginsStore) {
    val state by remember {
        store.stateFlow.map { it.loginsAddLoginState }
    }
        .collectAsState(store.state.loginsAddLoginState)
    val newLoginState by remember {
        store.stateFlow.map { it.newLoginState }
    }
        .collectAsState(initial = store.state.newLoginState)
    val host = state?.host ?: ""
    val username = state?.username ?: ""
    val password = state?.password ?: ""
    val isLoginValid =
        isValidHost(host) && username.isNotBlank() && newLoginState != NewLoginState.Duplicate && password.isNotBlank()

    TopAppBar(
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
        title = {
            Text(
                text = stringResource(R.string.add_login_2),
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = { store.dispatch(AddLoginBackClicked) },
                contentDescription = stringResource(R.string.add_login_navigate_back_button_content_description),
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
                    store.dispatch(AddLoginAction.AddLoginSaveClicked)
                },
                contentDescription = stringResource(R.string.add_login_save_new_login_button_content_description),
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
private fun AddLoginHost(store: LoginsStore) {
    val state by remember {
        store.stateFlow.map { it.loginsAddLoginState }
    }
        .collectAsState(initial = store.state.loginsAddLoginState)
    val host = state?.host ?: ""
    var isFocused by remember { mutableStateOf(false) }

    TextField(
        value = host,
        onValueChange = { newHost ->
            store.dispatch(AddLoginAction.HostChanged(newHost))
        },
        placeholder = stringResource(R.string.add_login_hostname_hint_text),
        errorText = stringResource(R.string.add_login_hostname_invalid_text_2),
        isError = host.isNotBlank() && !Patterns.WEB_URL.matcher(host).matches(),
        modifier =
            Modifier.onFocusChanged { isFocused = it.isFocused }
                .padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = FirefoxTheme.layout.space.static100,
                )
                .width(FirefoxTheme.layout.size.containerMaxWidth)
                .semantics {
                    testTag = LoginsTestingTags.ADD_LOGIN_HOST_NAME_TEXT_FIELD
                },
        label = stringResource(R.string.preferences_passwords_saved_logins_site),
        trailingIcon = {
            if (isFocused && isValidHost(host)) {
                CrossTextFieldButton(contentDescription = Text.Resource(R.string.saved_login_clear_hostname)) {
                    store.dispatch(AddLoginAction.HostChanged(""))
                }
            }
        },
    )

    if ((host.isEmpty() || isValidHost(host)) && !isHttpOrHttps(host)) {
        Spacer(modifier = Modifier.height(4.dp))

        Text(
            text = stringResource(R.string.add_login_hostname_invalid_text_3),
            modifier =
                Modifier.padding(horizontal = FirefoxTheme.layout.space.static200)
                    .width(FirefoxTheme.layout.size.containerMaxWidth),
        )
    }
}

@Composable
private fun AddLoginUsername(store: LoginsStore) {
    val addLoginState by remember {
        store.stateFlow.map { it.loginsAddLoginState }
    }
        .collectAsState(initial = store.state.loginsAddLoginState)
    val newLoginState by remember {
        store.stateFlow.map { it.newLoginState }
    }
        .collectAsState(initial = store.state.newLoginState)
    val username = addLoginState?.username ?: ""
    var isFocused by remember { mutableStateOf(false) }

    TextField(
        value = username,
        onValueChange = { newUsername ->
            store.dispatch(AddLoginAction.UsernameChanged(newUsername))
        },
        placeholder = "",
        errorText = stringResource(R.string.saved_login_duplicate),
        isError = newLoginState == NewLoginState.Duplicate,
        modifier =
            Modifier.onFocusChanged { isFocused = it.isFocused }
                .padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = FirefoxTheme.layout.space.static100,
                )
                .width(FirefoxTheme.layout.size.containerMaxWidth)
                .semantics {
                    testTag = LoginsTestingTags.ADD_LOGIN_USER_NAME_TEXT_FIELD
                },
        label = stringResource(R.string.preferences_passwords_saved_logins_username),
        trailingIcon = {
            if (isFocused && addLoginState?.username?.isNotEmpty() == true) {
                CrossTextFieldButton(contentDescription = Text.Resource(R.string.saved_login_clear_username)) {
                    store.dispatch(AddLoginAction.UsernameChanged(""))
                }
            }
        },
    )
}

@Composable
private fun AddLoginPassword(store: LoginsStore) {
    val state by remember {
        store.stateFlow.map { it.loginsAddLoginState }
    }
        .collectAsState(initial = store.state.loginsAddLoginState)
    val password = state?.password ?: ""
    var isFocused by remember { mutableStateOf(false) }
    val passwordState =
        rememberSyncedTextFieldState(value = password) { newPassword ->
            store.dispatch(AddLoginAction.PasswordChanged(newPassword))
        }

    SecureTextField(
        state = passwordState,
        placeholder = "",
        errorText = stringResource(R.string.saved_login_password_required_2),
        isError = isFocused && password.isBlank(),
        modifier =
            Modifier.onFocusChanged { isFocused = it.isFocused }
                .padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = FirefoxTheme.layout.space.static100,
                )
                .width(FirefoxTheme.layout.size.containerMaxWidth)
                .semantics {
                    testTag = LoginsTestingTags.ADD_LOGIN_PASSWORD_TEXT_FIELD
                },
        label = stringResource(R.string.preferences_passwords_saved_logins_password),
        trailingIcon = {
            if (isFocused && state?.password?.isNotEmpty() == true) {
                CrossTextFieldButton(contentDescription = Text.Resource(R.string.saved_logins_clear_password)) {
                    store.dispatch(AddLoginAction.PasswordChanged(""))
                }
            }
        },
    )
}

@FlexibleWindowPreview
@Composable
private fun AddLoginScreenPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val store = LoginsStore(initialState = LoginsState.default)
    FirefoxTheme(theme) {
        AddLoginScreen(store)
    }
}

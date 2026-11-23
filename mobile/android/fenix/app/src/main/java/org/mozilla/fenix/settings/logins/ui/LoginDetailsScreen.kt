/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import android.os.Build
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.TextObfuscationMode
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.snackbar.Snackbar
import mozilla.components.compose.base.snackbar.displaySnackbar
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.textfield.SecureTextField
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.simplifiedUrl
import org.mozilla.fenix.theme.FirefoxTheme

@Composable
internal fun LoginDetailsScreen(store: LoginsStore) {
    val state by store.stateFlow.collectAsState()
    val detailState = state.loginsLoginDetailState ?: return
    val snackbarHostState = remember { SnackbarHostState() }
    val scrollState = rememberScrollState()
    val deletionDialogState = state.loginDeletionDialogState
    if (deletionDialogState is LoginDeletionDialogState.Presenting) {
        LoginDeletionDialog(
            onCancelTapped = { store.dispatch(LoginDeletionDialogAction.CancelTapped) },
            onDeleteTapped = { store.dispatch(LoginDeletionDialogAction.DeleteTapped) },
        )
    }

    Scaffold(
        topBar = {
            LoginDetailTopBar(
                store = store,
                loginItem = detailState.login,
                onBackClick = { store.dispatch(LoginsDetailBackClicked) },
            )
        },
        snackbarHost = {
            SnackbarHost(
                hostState = snackbarHostState,
                modifier = Modifier.imePadding(),
            ) {
                Snackbar(snackbarData = it)
            }
        },
    ) { paddingValues ->
        Column(
            modifier = Modifier.padding(paddingValues).fillMaxWidth().verticalScroll(scrollState),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
            LoginDetailsUrl(store = store, url = detailState.login.url)
            Spacer(modifier = Modifier.height(8.dp))
            LoginDetailsUsername(
                store = store,
                snackbarHostState = snackbarHostState,
                username = detailState.login.username,
            )
            Spacer(modifier = Modifier.height(8.dp))
            LoginDetailsPassword(
                store = store,
                snackbarHostState = snackbarHostState,
                password = detailState.login.password,
            )
        }
    }
}

@Composable
private fun LoginDetailTopBar(
    store: LoginsStore,
    loginItem: LoginItem,
    onBackClick: () -> Unit,
) {
    var showMenu by remember { mutableStateOf(false) }

    TopAppBar(
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
        title = {
            Text(
                text = loginItem.url.simplifiedUrl(),
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = onBackClick,
                contentDescription = stringResource(R.string.login_details_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        actions = {
            Box {
                IconButton(
                    onClick = { showMenu = true },
                    contentDescription = stringResource(R.string.login_detail_menu_button_content_description),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_more_vertical_round_24),
                        contentDescription = null,
                    )
                }

                LoginDetailMenu(
                    showMenu = showMenu,
                    onDismissRequest = { showMenu = false },
                    loginItem = loginItem,
                    store = store,
                )
            }
        },
    )
}

@Composable
private fun LoginDetailMenu(
    showMenu: Boolean,
    onDismissRequest: () -> Unit,
    loginItem: LoginItem,
    store: LoginsStore,
) {
    DropdownMenu(
        menuItems =
            listOf(
                MenuItem.TextItem(
                    text = Text.Resource(R.string.login_detail_menu_edit_button),
                    onClick = { store.dispatch(DetailLoginMenuAction.EditLoginMenuItemClicked(loginItem)) },
                ),
                MenuItem.TextItem(
                    text = Text.Resource(R.string.login_detail_menu_delete_button),
                    onClick = {
                        store.dispatch(DetailLoginMenuAction.DeleteLoginMenuItemClicked(loginItem))
                    },
                ),
            ),
        expanded = showMenu,
        onDismissRequest = onDismissRequest,
    )
}

@Composable
private fun LoginDetailsUrl(store: LoginsStore, url: String) {
    TextField(
        value = url,
        onValueChange = {},
        isEnabled = false,
        placeholder = "",
        errorText = "",
        label = stringResource(R.string.preferences_passwords_saved_logins_site),
        modifier =
            Modifier.padding(horizontal = FirefoxTheme.layout.space.static200)
                .wrapContentHeight()
                .width(FirefoxTheme.layout.size.containerMaxWidth),
        trailingIcon = {
            IconButton(
                onClick = {
                    store.dispatch(DetailLoginAction.GoToSiteClicked(url))
                },
                contentDescription = stringResource(R.string.saved_login_open_site),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_external_link_24),
                    contentDescription = null,
                )
            }
        },
    )
}

@Composable
private fun LoginDetailsUsername(
    store: LoginsStore,
    snackbarHostState: SnackbarHostState,
    username: String,
) {
    val usernameSnackbarText = stringResource(R.string.logins_username_copied)
    val coroutineScope = rememberCoroutineScope()

    TextField(
        value = username,
        onValueChange = {},
        isEnabled = false,
        placeholder = "",
        errorText = "",
        label = stringResource(R.string.preferences_passwords_saved_logins_username),
        modifier =
            Modifier.padding(horizontal = FirefoxTheme.layout.space.static200)
                .wrapContentHeight()
                .width(FirefoxTheme.layout.size.containerMaxWidth),
        trailingIcon = {
            IconButton(
                onClick = {
                    store.dispatch(DetailLoginAction.CopyUsernameClicked(username))
                    if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
                        showTextCopiedSnackbar(
                            message = usernameSnackbarText,
                            coroutineScope = coroutineScope,
                            snackbarHostState = snackbarHostState,
                        )
                    }
                },
                contentDescription = stringResource(R.string.saved_login_copy_username),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_copy_24),
                    contentDescription = null,
                )
            }
        },
    )
}

@Composable
private fun LoginDetailsPassword(
    store: LoginsStore,
    snackbarHostState: SnackbarHostState,
    password: String,
) {
    var isPasswordVisible by remember { mutableStateOf(false) }
    val coroutineScope = rememberCoroutineScope()
    val passwordSnackbarText = stringResource(R.string.logins_password_copied)

    SecureTextField(
        state = remember(password) { TextFieldState(password) },
        isEnabled = false,
        placeholder = "",
        errorText = "",
        label = stringResource(R.string.preferences_passwords_saved_logins_password),
        modifier =
            Modifier.padding(horizontal = FirefoxTheme.layout.space.static200)
                .wrapContentHeight()
                .width(FirefoxTheme.layout.size.containerMaxWidth)
                .semantics {
                    testTagsAsResourceId = true
                    testTag = LoginsTestingTags.LOGIN_DETAILS_PASSWORD_TEXT_FIELD
                },
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
                    isPasswordVisible = !isPasswordVisible
                    store.dispatch(DetailLoginAction.PasswordVisibilityChanged(isPasswordVisible))
                },
            )

            IconButton(
                onClick = {
                    store.dispatch(DetailLoginAction.CopyPasswordClicked(password))
                    if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
                        showTextCopiedSnackbar(
                            message = passwordSnackbarText,
                            coroutineScope = coroutineScope,
                            snackbarHostState = snackbarHostState,
                        )
                    }
                },
                contentDescription = stringResource(R.string.saved_logins_copy_password),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_copy_24),
                    contentDescription = null,
                )
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

@Composable
private fun LoginDeletionDialog(
    onCancelTapped: () -> Unit,
    onDeleteTapped: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onCancelTapped,
        text = {
            Text(
                text = stringResource(R.string.login_deletion_confirmation_2),
                style = FirefoxTheme.typography.body2,
            )
        },
        confirmButton = {
            TextButton(
                text = stringResource(R.string.bookmark_menu_delete_button),
                onClick = onDeleteTapped,
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.bookmark_delete_negative),
                onClick = onCancelTapped,
            )
        },
    )
}

private fun showTextCopiedSnackbar(
    message: String,
    coroutineScope: CoroutineScope,
    snackbarHostState: SnackbarHostState,
) {
    coroutineScope.launch {
        snackbarHostState.displaySnackbar(message = message)
    }
}

private fun createStore() =
    LoginsStore(
        initialState =
            LoginsState.default.copy(
                loginsLoginDetailState =
                    LoginsLoginDetailState(
                        login =
                            LoginItem(
                                guid = "123",
                                url = "https://www.justanothersite123.com",
                                username = "username 123",
                                password = "password 123",
                            )
                    )
            )
    )

@FlexibleWindowPreview
@Composable
private fun LoginDetailsScreenPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        LoginDetailsScreen(store = createStore())
    }
}

@Preview
@Composable
private fun LoginDeletionDialogPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        LoginDeletionDialog(
            onCancelTapped = {},
            onDeleteTapped = {},
        )
    }
}

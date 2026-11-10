/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import kotlinx.coroutines.flow.map
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.ktx.kotlin.trimmed
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.compose.list.SelectableFaviconListItem
import org.mozilla.fenix.settings.biometric.ui.SecureScreen
import org.mozilla.fenix.settings.logins.ui.LoginsSortOrder.Alphabetical.isGuidToDelete
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * The UI host for the Saved Logins list screen and related sub screens.
 *
 * @param buildStore A builder function to construct a [LoginsStore] using the NavController that's local to the nav
 *   graph for the Logins view hierarchy.
 * @param exitLogins A callback invoked when the user indicates to exit the secure screen.
 * @param startDestination the screen on which to initialize [SavedLoginsScreen] with.
 */
@Composable
internal fun SavedLoginsScreen(
    buildStore: (NavHostController) -> LoginsStore,
    exitLogins: () -> Unit = {},
    startDestination: String = LoginsDestinations.LIST,
) {
    val navController = rememberNavController()
    val store = remember { buildStore(navController) }

    SecureScreen(
        title = stringResource(R.string.logins_biometric_prompt_message_2),
        onExit = exitLogins,
    ) {
        NavHost(
            navController = navController,
            startDestination = startDestination,
        ) {
            composable(route = LoginsDestinations.LIST) {
                BackHandler { store.dispatch(LoginsListBackClicked) }
                LoginsList(store = store)
            }
            composable(route = LoginsDestinations.ADD_LOGIN) {
                BackHandler { store.dispatch(AddLoginBackClicked) }
                AddLoginScreen(store = store)
            }
            composable(route = LoginsDestinations.EDIT_LOGIN) {
                BackHandler { store.dispatch(EditLoginBackClicked) }
                EditLoginScreen(store = store)
            }
            composable(route = LoginsDestinations.LOGIN_DETAILS) {
                BackHandler { store.dispatch(LoginsDetailBackClicked) }
                LoginDetailsScreen(store = store)
            }
        }
    }
}

internal object LoginsDestinations {
    const val LIST = "list"
    const val ADD_LOGIN = "add login"
    const val EDIT_LOGIN = "edit login"
    const val LOGIN_DETAILS = "login details"
}

@Composable
private fun LoginsList(store: LoginsStore) {
    val state by store.stateFlow.collectAsState()

    LaunchedEffect(Unit) {
        store.dispatch(LoginsListAppeared)
    }

    Scaffold(
        topBar = {
            LoginsListTopBar(
                store = store,
                text = state.searchText ?: "",
            )
        },
        contentWindowInsets = WindowInsets(0.dp),
        modifier = Modifier.semantics { testTagsAsResourceId = true },
    ) { paddingValues ->
        if (state.searchText.isNullOrEmpty() && state.loginItems.isEmpty()) {
            EmptyList(dispatcher = store::dispatch, paddingValues = paddingValues)
            return@Scaffold
        }

        Column(
            modifier = Modifier.fillMaxSize(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            LazyColumn(
                modifier =
                    Modifier.padding(paddingValues)
                        .width(FirefoxTheme.layout.size.containerMaxWidth)
                        .weight(1f, false)
                        .semantics {
                            testTag = LoginsTestingTags.SAVED_LOGINS_LIST
                            collectionInfo = CollectionInfo(rowCount = state.loginItems.size, columnCount = 1)
                        }
            ) {
                itemsIndexed(state.loginItems) { _, item ->
                    if (state.isGuidToDelete(item.guid)) {
                        return@itemsIndexed
                    }

                    SelectableFaviconListItem(
                        label = item.url.trimmed(),
                        url = item.url,
                        isSelected = false,
                        onClick = { store.dispatch(LoginClicked(item)) },
                        description = item.username.trimmed(),
                        modifier =
                            Modifier.semantics {
                                testTag = LoginsTestingTags.SAVED_LOGINS_LIST_ITEM + ".${item.url.trimmed()}"
                            },
                    )
                }
            }

            AddPasswordItem(
                modifier = Modifier.width(FirefoxTheme.layout.size.containerMaxWidth),
                onAddPasswordClicked = { store.dispatch(AddLoginAction.InitAdd) },
            )
        }
    }
}

@Composable
private fun AddPasswordItem(
    modifier: Modifier = Modifier,
    onAddPasswordClicked: () -> Unit,
) {
    IconListItem(
        label = stringResource(R.string.preferences_logins_add_login_2),
        modifier = modifier,
        beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_plus_24),
        description = stringResource(R.string.saved_logins_add_new_login_button_content_description),
        onClick = { onAddPasswordClicked() },
    )
}

@Composable
@Suppress("MaxLineLength")
private fun EmptyList(
    dispatcher: (LoginsAction) -> Unit,
    paddingValues: PaddingValues,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier.padding(paddingValues).fillMaxSize(),
        contentAlignment = Alignment.TopCenter,
    ) {
        Column(
            modifier = Modifier.padding(16.dp).width(FirefoxTheme.layout.size.containerMaxWidth),
            horizontalAlignment = Alignment.Start,
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text(
                text =
                    String.format(
                        stringResource(R.string.preferences_passwords_saved_logins_description_empty_text_2),
                        stringResource(R.string.app_name),
                    ),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = FirefoxTheme.typography.body2,
            )

            LinkText(
                text = stringResource(R.string.preferences_passwords_saved_logins_description_empty_learn_more_link_2),
                linkTextStates =
                    listOf(
                        LinkTextState(
                            text =
                                stringResource(
                                    R.string.preferences_passwords_saved_logins_description_empty_learn_more_link_2
                                ),
                            url = "",
                            onClick = { dispatcher(LearnMoreAboutSync) },
                        )
                    ),
                linkTextDecoration = TextDecoration.Underline,
            )

            AddPasswordItem(onAddPasswordClicked = { dispatcher(AddLoginAction.InitAdd) })
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
@Suppress("LongMethod", "CognitiveComplexMethod")
private fun LoginsListTopBar(
    store: LoginsStore,
    text: String,
) {
    var showMenu by remember { mutableStateOf(false) }
    var searchActive by remember { mutableStateOf(false) }

    TopAppBar(
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
        title = {
            if (!searchActive) {
                Text(
                    text = stringResource(R.string.preferences_passwords_saved_logins_2),
                    style = FirefoxTheme.typography.headline5,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            } else {
                SearchBar(text, store)
            }
        },
        navigationIcon = {
            IconButton(
                onClick = {
                    if (!searchActive) {
                        store.dispatch(LoginsListBackClicked)
                    } else {
                        searchActive = false
                    }
                },
                contentDescription = stringResource(R.string.logins_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        actions = {
            if (searchActive) return@TopAppBar

            IconButton(
                onClick = { searchActive = true },
                contentDescription = stringResource(R.string.preferences_passwords_saved_logins_search_2),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_search_24),
                    contentDescription = null,
                )
            }

            Box {
                IconButton(
                    onClick = {
                        showMenu = true
                    },
                    contentDescription =
                        stringResource(R.string.saved_logins_menu_dropdown_chevron_icon_content_description_2),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_sort_24),
                        contentDescription = null,
                    )
                }

                LoginListSortMenu(
                    showMenu = showMenu,
                    onDismissRequest = {
                        showMenu = false
                    },
                    store = store,
                )
            }

            if (store.state.showPasswordsImport) {
                Box {
                    IconButton(
                        onClick = { store.dispatch(ImportPasswordsOverflowMenuClicked) },
                        contentDescription = stringResource(R.string.passwords_import_menu_button),
                    ) {
                        Icon(
                            painter = painterResource(iconsR.drawable.mozac_ic_more_vertical_round_24),
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.onSurface,
                        )
                    }

                    ImportPasswordsOverflowMenu(store)
                }
            }
        },
    )
}

@Composable
private fun SearchBar(
    text: String,
    store: LoginsStore,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default,
    keyboardActions: KeyboardActions = KeyboardActions.Default,
) {
    val focusRequester = remember { FocusRequester() }
    SideEffect {
        focusRequester.requestFocus()
    }

    TextField(
        value = text,
        placeholder = stringResource(R.string.preferences_passwords_saved_logins_search_2),
        onValueChange = {
            store.dispatch(SearchLogins(searchText = it, loginItems = store.state.loginItems))
        },
        errorText = "",
        modifier =
            Modifier.semantics {
                    testTag = LoginsTestingTags.SAVED_LOGINS_PASSWORD_SEARCH_FIELD
                }
                .fillMaxWidth()
                .focusRequester(focusRequester),
        trailingIcon = {
            if (text.isNotBlank()) {
                IconButton(
                    onClick = {
                        store.dispatch(
                            SearchLogins(
                                searchText = "",
                                loginItems = store.state.loginItems,
                            )
                        )
                    },
                    contentDescription =
                        stringResource(R.string.saved_logins_clear_search_text_button_content_description),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_cross_24),
                        contentDescription = null,
                    )
                }
            }
        },
        keyboardOptions = keyboardOptions,
        keyboardActions = keyboardActions,
    )
}

@Composable
private fun LoginListSortMenu(
    showMenu: Boolean,
    onDismissRequest: () -> Unit,
    store: LoginsStore,
) {
    val sortOrder by remember {
        store.stateFlow.map { store.state.sortOrder }
    }
        .collectAsState(store.state.sortOrder)
    DropdownMenu(
        menuItems =
            listOf(
                MenuItem.CheckableItem(
                    text =
                        mozilla.components.compose.base.text.Text.Resource(
                            R.string.saved_logins_sort_strategy_alphabetically
                        ),
                    onClick = { store.dispatch(LoginsListSortMenuAction.OrderByNameClicked) },
                    isChecked = sortOrder == LoginsSortOrder.Alphabetical,
                ),
                MenuItem.CheckableItem(
                    text =
                        mozilla.components.compose.base.text.Text.Resource(
                            R.string.saved_logins_sort_strategy_last_used
                        ),
                    onClick = { store.dispatch(LoginsListSortMenuAction.OrderByLastUsedClicked) },
                    isChecked = sortOrder == LoginsSortOrder.LastUsed,
                ),
            ),
        expanded = showMenu,
        onDismissRequest = onDismissRequest,
    )
}

@Composable
private fun ImportPasswordsOverflowMenu(store: LoginsStore) {
    val showMenu by remember {
        store.stateFlow.map { store.state.importPasswordsMenuShown }
    }
        .collectAsState(initial = store.state.importPasswordsMenuShown)

    val menuItems =
        listOf(
            MenuItem.TextItem(
                text = Text.Resource(R.string.passwords_import_menu_button),
                onClick = { store.dispatch(ImportAction.ImportFileClicked) },
            )
        )

    DropdownMenu(
        menuItems = menuItems,
        expanded = showMenu,
        onDismissRequest = { store.dispatch(ImportPasswordsOverflowMenuDismissed) },
    )
}

private const val LOGINS_LIST_SIZE = 15
private val loginItems =
    List(LOGINS_LIST_SIZE) {
        LoginItem(
            guid = "$it",
            url = "https://www.justanothersite$it.com",
            username = "username $it",
            password = "password $it",
        )
    }

private fun createStore() =
    LoginsStore(
        initialState =
            LoginsState.default.copy(
                loginItems = loginItems,
                searchText = "",
            )
    )

@FlexibleWindowPreview
@Composable
private fun LoginsListScreenPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        LoginsList(store = createStore())
    }
}

@FlexibleWindowPreview
@Composable
private fun EmptyLoginsListScreenPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        LoginsList(store = LoginsStore())
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.navigation

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.lib.state.helpers.StoreProvider.Companion.composableStore
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.settings.data.SettingsRepository
import org.mozilla.fenix.settings.middleware.SettingsMiddleware
import org.mozilla.fenix.settings.store.SettingsAction
import org.mozilla.fenix.settings.store.SettingsAction.SearchStarted
import org.mozilla.fenix.settings.store.SettingsState
import org.mozilla.fenix.settings.store.SettingsStore
import org.mozilla.fenix.settings.ui.SearchSettingsScreen
import org.mozilla.fenix.settings.ui.SettingsScreen
import org.mozilla.fenix.theme.FirefoxTheme
import mozilla.components.ui.icons.R as iconsR

/**
 * Host of all settings screens.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsHost() {
    val context = LocalContext.current
    val settingsRepository = remember {
        SettingsRepository(
            context = context,
            settings = context.components.settings,
            browserStore = context.components.core.store,
        )
    }
    val navController = rememberSettingsNavigationState()
    val lifecycleScope = rememberCoroutineScope()
    val settingsStore by composableStore(SettingsState()) {
            SettingsStore(
                initialState = it,
                middlewares = listOf(
                    SettingsMiddleware(
                        uiContext = context,
                        components = context.components,
                        settings = context.components.settings,
                        repository = settingsRepository,
                        navController = navController,
                        scope = lifecycleScope,
                    ),
                ),
            )
        }

    Scaffold(
        modifier = Modifier.fillMaxSize(),
        topBar = {
            if (navController.currentDestination == SettingsDestination.SearchResults) {
                SearchSettingsInput(settingsStore, navController)
            } else {
                SettingsDisplayTopBar(settingsStore, navController)
            }
        },
    ) { innerPadding ->
        SettingsNavHost(
            navigationState = navController,
            modifier = Modifier
                .padding(top = innerPadding.calculateTopPadding())
                .fillMaxSize(),
        ) { destination ->
            when (destination) {
                is SettingsDestination.Root -> {
                    SettingsScreen(destination, settingsStore)
                }

                is SettingsDestination.SearchResults -> {
                    SearchSettingsScreen(settingsStore)
                }

                else -> PlaceholderScreen("Placeholder for ${context.getString(destination.title)} settings")
            }
        }
    }
}

/**
 * Simple placeholder screen for demonstration purposes.
 */
@Composable
private fun PlaceholderScreen(title: String) {
    FirefoxTheme {
        Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.headlineMedium,
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SettingsDisplayTopBar(
    settingsStore: SettingsStore,
    navController: SettingsNavController,
) {
    TopAppBar(
        title = {
            Text(
                style = FirefoxTheme.typography.headline5,
                text = stringResource(navController.currentDestination.title),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        },
        navigationIcon = {
            IconButton(onClick = { navController.navigateBack() }) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = "Go back",
                )
            }
        },
        actions = {
            IconButton(onClick = {
                navController.navigateTo(SettingsDestination.SearchResults)
                settingsStore.dispatch(SearchStarted)
            }) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_search_24),
                    contentDescription = "Search settings",
                )
            }
        },
        windowInsets = WindowInsets(
            top = 0.dp,
            bottom = 0.dp,
        ),
    )
}

/**
 * Composable for the settings search bar.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SearchSettingsInput(
    settingsStore: SettingsStore,
    navController: SettingsNavController,
) {
    val state by settingsStore.observeAsComposableState { it }
    val focusRequester = remember { FocusRequester() }

    TopAppBar(
        modifier = Modifier
            .wrapContentHeight(),
        title = {
            TextField(
                value = state.searchQuery,
                onValueChange = { value ->
                    settingsStore.dispatch(SettingsAction.SearchQueryUpdated(value))
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .focusRequester(focusRequester),
                placeholder = stringResource(R.string.settings_search_title),
                singleLine = true,
                errorText = stringResource(R.string.settings_search_error_message),
                colors = TextFieldDefaults.colors(
                    focusedIndicatorColor = Color.Transparent,
                    unfocusedIndicatorColor = Color.Transparent,
                    errorIndicatorColor = Color.Transparent,
                ),
                trailingIcon = {
                    if (state.searchQuery.isNotBlank()) {
                        ClearTextButton(
                            onClick = {
                                settingsStore.dispatch(SettingsAction.SearchQueryUpdated(""))
                            },
                        )
                    }
                },
            )
        },
        navigationIcon = {
            mozilla.components.compose.base.button.IconButton(
                onClick = { navController.navigateBack() },
                contentDescription =
                    stringResource(
                        R.string.content_description_settings_search_navigate_back,
                    ),
            ) {
                Icon(
                    painter = painterResource(
                        iconsR.drawable.mozac_ic_back_24,
                    ),
                    contentDescription = null,
                    tint = FirefoxTheme.colors.textPrimary,
                )
            }
        },
        windowInsets = WindowInsets(
            top = 0.dp,
            bottom = 0.dp,
        ),
    )

    SideEffect {
        focusRequester.requestFocus()
    }
}

@Composable
private fun ClearTextButton(
    onClick: () -> Unit,
) {
    mozilla.components.compose.base.button.IconButton(
        onClick = onClick,
        contentDescription = stringResource(
            R.string.content_description_settings_search_clear_search,
        ),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_cross_circle_fill_24),
            contentDescription = null,
            tint = FirefoxTheme.colors.textPrimary,
        )
    }
}

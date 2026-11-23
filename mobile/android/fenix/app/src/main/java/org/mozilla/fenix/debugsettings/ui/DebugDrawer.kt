/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.ui

import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.debugsettings.navigation.DebugDrawerDestination
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * The debug drawer UI.
 *
 * @param navController [NavHostController] used to perform navigation actions on the [NavHost].
 * @param destinations The list of [DebugDrawerDestination]s (excluding home) used to populate the [NavHost] with
 *   screens.
 * @param onBackButtonClick Invoked when the user taps on the back button in the app bar.
 */
@Composable
fun DebugDrawer(
    navController: NavHostController,
    destinations: List<DebugDrawerDestination>,
    onBackButtonClick: () -> Unit,
) {
    var backButtonVisible by remember { mutableStateOf(false) }
    var toolbarTitle by remember { mutableStateOf("") }

    Scaffold(
        modifier = Modifier.fillMaxSize(),
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = toolbarTitle,
                        style = FirefoxTheme.typography.headline5,
                    )
                },
                modifier = Modifier.shadow(elevation = 5.dp),
                navigationIcon = {
                    if (backButtonVisible) {
                        IconButton(
                            onClick = onBackButtonClick,
                            contentDescription =
                                stringResource(id = R.string.debug_drawer_back_button_content_description),
                        ) {
                            Icon(
                                painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                                contentDescription = null,
                            )
                        }
                    }
                },
                windowInsets =
                    WindowInsets(
                        top = 0.dp,
                        bottom = 0.dp,
                    ),
            )
        },
    ) { paddingValues ->
        NavHost(
            navController = navController,
            startDestination = DEBUG_DRAWER_HOME_ROUTE,
            modifier = Modifier.fillMaxSize().padding(paddingValues),
        ) {
            composable(route = DEBUG_DRAWER_HOME_ROUTE) {
                toolbarTitle = stringResource(id = R.string.debug_drawer_title)
                backButtonVisible = false
                DebugDrawerHome(destinations = destinations.filter { !it.isChildDestination })
            }

            destinations.forEach { destination ->
                composable(route = destination.route) {
                    toolbarTitle = stringResource(id = destination.title)
                    backButtonVisible = true

                    destination.content()
                }
            }
        }
    }
}

@Preview
@Composable
private fun DebugDrawerPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val navController = rememberNavController()
    val destinations = remember {
        List(size = 15) { index ->
            DebugDrawerDestination(
                route = "screen_$index",
                title = R.string.debug_drawer_title,
                onClick = {
                    navController.navigate(route = "screen_$index")
                },
                content = {
                    Text(
                        text = "Tool $index",
                        style = FirefoxTheme.typography.headline6,
                    )
                },
            )
        }
    }

    FirefoxTheme(theme) {
        DebugDrawer(
            navController = navController,
            destinations = destinations,
            onBackButtonClick = {
                navController.popBackStack()
            },
        )
    }
}

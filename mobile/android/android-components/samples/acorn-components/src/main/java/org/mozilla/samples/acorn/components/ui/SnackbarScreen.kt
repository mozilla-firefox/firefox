/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.acorn.components.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.snackbar.Snackbar
import mozilla.components.compose.base.snackbar.SnackbarVisuals
import mozilla.components.compose.base.snackbar.displaySnackbar
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.icons.R as iconsR

/** Displays demos of the Acorn snackbar component. */
@Composable
fun SnackbarScreen(onNavigateUp: () -> Unit = {}) {
    val snackbarHostState = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "Snackbar",
                        style = AcornTheme.typography.headline5,
                    )
                },
                navigationIcon = {
                    IconButton(
                        onClick = onNavigateUp,
                        contentDescription = "Navigate back",
                    ) {
                        Icon(
                            painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                            contentDescription = null,
                        )
                    }
                },
                actions = { ThemeToggleButton() },
            )
        },
        snackbarHost = {
            SnackbarHost(hostState = snackbarHostState) { data ->
                Snackbar(snackbarData = data)
            }
        },
    ) { innerPadding ->
        SnackbarContent(
            modifier = Modifier.fillMaxSize().padding(innerPadding).padding(16.dp),
            snackbarHostState = snackbarHostState,
            scope = scope,
        )
    }
}

@Composable
private fun SnackbarContent(
    snackbarHostState: SnackbarHostState,
    scope: CoroutineScope,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        FilledButton(
            text = "Show Snackbar",
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                scope.launch {
                    snackbarHostState.displaySnackbar(message = "This is a snackbar message")
                }
            },
        )

        FilledButton(
            text = "Show Snackbar with Action",
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                scope.launch {
                    snackbarHostState.displaySnackbar(
                        message = "Item deleted",
                        actionLabel = "Undo",
                    )
                }
            },
        )

        FilledButton(
            text = "Show Snackbar with Dismiss",
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                scope.launch {
                    snackbarHostState.displaySnackbar(
                        message = "Bookmark saved",
                        withDismissAction = true,
                    )
                }
            },
        )

        FilledButton(
            text = "Show Snackbar with Sub-message",
            modifier = Modifier.fillMaxWidth(),
            onClick = {
                scope.launch {
                    snackbarHostState.displaySnackbar(
                        visuals =
                            SnackbarVisuals(
                                message = "Download complete",
                                subMessage = "document.pdf",
                                actionLabel = "Open",
                                withDismissAction = true,
                            )
                    )
                }
            },
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun SnackbarScreenPreview() {
    AcornTheme {
        Surface {
            SnackbarScreen()
        }
    }
}

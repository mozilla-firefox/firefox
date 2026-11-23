/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.acorn.components.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.DestructiveButton
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.FloatingActionButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.icons.R as iconsR

/** Displays demos of the Acorn button components. */
@Composable
fun ButtonsScreen(onNavigateUp: () -> Unit = {}) {
    var buttonLabel by remember { mutableStateOf("Label") }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "Buttons",
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
        }
    ) { innerPadding ->
        Column(
            modifier =
                Modifier.fillMaxSize()
                    .padding(innerPadding)
                    .verticalScroll(rememberScrollState())
                    .padding(vertical = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            TextField(
                value = buttonLabel,
                onValueChange = { buttonLabel = it },
                placeholder = "Enter button label",
                errorText = "",
                label = "Button Label",
                modifier = Modifier.padding(horizontal = 16.dp),
            )
            HorizontalDivider()

            FilledButtonSection(label = buttonLabel)
            HorizontalDivider()

            OutlinedButtonSection(label = buttonLabel)
            HorizontalDivider()

            DestructiveButtonSection(label = buttonLabel)
            HorizontalDivider()

            TextButtonSection(label = buttonLabel)
            HorizontalDivider()

            FloatingActionButtonSection(label = buttonLabel)
        }
    }
}

@Composable
private fun FilledButtonSection(label: String) {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(text = "Filled Button", style = AcornTheme.typography.subtitle1)

        FilledButton(text = label, onClick = {})

        FilledButton(
            text = label,
            icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
            onClick = {},
        )

        FilledButton(
            text = label,
            enabled = false,
            icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
            onClick = {},
        )
    }
}

@Composable
private fun OutlinedButtonSection(label: String) {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(text = "Outlined Button", style = AcornTheme.typography.subtitle1)

        OutlinedButton(text = label, onClick = {})

        OutlinedButton(
            text = label,
            icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
            onClick = {},
        )

        OutlinedButton(
            text = label,
            enabled = false,
            icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
            onClick = {},
        )
    }
}

@Composable
private fun DestructiveButtonSection(label: String) {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(text = "Destructive Button", style = AcornTheme.typography.subtitle1)

        DestructiveButton(text = label, onClick = {})

        DestructiveButton(
            text = label,
            icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
            onClick = {},
        )
    }
}

@Composable
private fun TextButtonSection(label: String) {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(text = "Text Button", style = AcornTheme.typography.subtitle1)

        TextButton(text = label, onClick = {})

        TextButton(text = label, enabled = false, onClick = {})
    }
}

@Composable
private fun FloatingActionButtonSection(label: String) {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(text = "Floating Action Button", style = AcornTheme.typography.subtitle1)

        FloatingActionButton(
            icon = painterResource(iconsR.drawable.mozac_ic_plus_24),
            onClick = {},
        )

        FloatingActionButton(
            icon = painterResource(iconsR.drawable.mozac_ic_plus_24),
            label = label,
            onClick = {},
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun ButtonsScreenPreview() {
    AcornTheme {
        Surface {
            ButtonsScreen()
        }
    }
}

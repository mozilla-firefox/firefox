/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.acorn.components.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.theme.AcornTheme
import org.mozilla.samples.acorn.components.Destinations

private val componentCategories =
    listOf(
        Destinations.ICONS to "Icons",
        Destinations.COLORS to "Colors",
        Destinations.BANNER to "Banner",
        Destinations.BUTTONS to "Buttons",
        Destinations.SNACKBAR to "Snackbar",
    )

/** Displays a list of available Acorn Design System components. */
@Composable
fun ComponentListScreen(onNavigate: (String) -> Unit) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "Acorn Components",
                        style = AcornTheme.typography.headline5,
                    )
                },
                actions = { ThemeToggleButton() },
            )
        }
    ) { innerPadding ->
        Column(modifier = Modifier.fillMaxSize().padding(innerPadding).verticalScroll(rememberScrollState())) {
            componentCategories.forEach { (route, title) ->
                Text(
                    text = title,
                    style = AcornTheme.typography.subtitle1,
                    modifier = Modifier.fillMaxWidth().clickable { onNavigate(route) }.padding(all = 16.dp),
                )
            }
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun ComponentListScreenPreview() {
    AcornTheme {
        Surface {
            ComponentListScreen(onNavigate = {})
        }
    }
}

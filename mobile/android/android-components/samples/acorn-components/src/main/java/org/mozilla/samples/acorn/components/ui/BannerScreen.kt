/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.acorn.components.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
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
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.Banner
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.icons.R as iconsR

/** Displays demos of the Acorn banner component. */
@Composable
fun BannerScreen(onNavigateUp: () -> Unit = {}) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = "Banner",
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
            BasicBannerSection()
            HorizontalDivider()
            BannerWithTitleSection()
            HorizontalDivider()
            BannerWithActionsSection()
        }
    }
}

@Composable
private fun BasicBannerSection() {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(text = "Basic Banner", style = AcornTheme.typography.subtitle1)

        Banner(
            messageText = "This is a banner message.",
            modifier = Modifier.fillMaxWidth(),
            onCloseButtonClick = {},
        )
    }
}

@Composable
private fun BannerWithTitleSection() {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(text = "Banner with Title", style = AcornTheme.typography.subtitle1)

        Banner(
            messageText = "Supporting line text lorem ipsum dolor sit amet consectetur.",
            titleText = "Title",
            modifier = Modifier.fillMaxWidth(),
            onCloseButtonClick = {},
        )
    }
}

@Composable
private fun BannerWithActionsSection() {
    Column(
        modifier = Modifier.padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(text = "Banner with Actions", style = AcornTheme.typography.subtitle1)

        Banner(
            messageText = "Supporting line text lorem ipsum dolor sit amet consectetur.",
            titleText = "Title",
            modifier = Modifier.fillMaxWidth(),
            positiveButtonText = "Accept",
            negativeButtonText = "Cancel",
            positiveOnClick = {},
            negativeOnClick = {},
            onCloseButtonClick = {},
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun BannerScreenPreview() {
    AcornTheme {
        Surface {
            BannerScreen()
        }
    }
}

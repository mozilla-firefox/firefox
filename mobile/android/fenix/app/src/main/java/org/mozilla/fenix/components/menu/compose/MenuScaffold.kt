/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A scaffold for a menu UI that implements the basic layout structure with [header] and [content].
 *
 * @param modifier [Modifier] to be applied to the layout.
 * @param scrollState The [ScrollState] used for vertical scrolling.
 * @param header The Composable header block to render.
 * @param content The Composable content block to render.
 */
@Composable
internal fun MenuScaffold(
    modifier: Modifier = Modifier,
    scrollState: ScrollState = rememberScrollState(),
    header: @Composable ColumnScope.() -> Unit,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(modifier = modifier) {
        header()

        Row(modifier = Modifier.verticalScroll(rememberScrollState()).fillMaxWidth()) {
            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))
        }

        if (scrollState.canScrollBackward) {
            HorizontalDivider()
        }

        Column(
            modifier =
                Modifier.verticalScroll(scrollState)
                    .padding(
                        start = FirefoxTheme.layout.space.static200,
                        top = FirefoxTheme.layout.space.static150,
                        end = FirefoxTheme.layout.space.static200,
                        bottom = FirefoxTheme.layout.space.static400,
                    ),
            verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
        ) {
            content()
        }
    }
}

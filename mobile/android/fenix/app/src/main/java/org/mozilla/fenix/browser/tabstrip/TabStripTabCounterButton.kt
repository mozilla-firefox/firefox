/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.tabstrip

import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.ui.tabcounter.TabCounter
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A button showing number of tabs in the tab strip, encapsulating [TabCounter] and [DropdownMenu].
 * When long pressed, the [DropdownMenu] will appear.
 *
 * @param tabCount The number of tabs to display in the counter.
 * @param size The size of the button.
 * @param menuItems The list of [MenuItem] to display in the dropdown menu.
 * @param privacyBadgeVisible Whether to show the privacy badge.
 * @param modifier The [modifier] applied to the composable.
 * @param onClick Invoked when the user clicks the button.
 */
@Composable
fun TabStripTabCounterButton(
    tabCount: Int,
    size: Dp,
    menuItems: List<MenuItem>,
    privacyBadgeVisible: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    var menuExpanded by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .size(72.dp)
            .clip(CircleShape)
            .combinedClickable(
                onClick = onClick,
                role = Role.Button,
                onLongClick = {
                    menuExpanded = true
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        TabCounter(
            tabCount = tabCount,
            showPrivacyBadge = privacyBadgeVisible,
        )

        DropdownMenu(
            menuItems = menuItems,
            expanded = menuExpanded,
            offset = DpOffset(
                x = 0.dp,
                y = -size,
            ),
            onDismissRequest = { menuExpanded = false },
        )
    }
}

@PreviewLightDark
@Composable
private fun TabStripTabCounterButtonPreview() {
    FirefoxTheme {
        Surface {
            Column(
                modifier = Modifier.padding(FirefoxTheme.layout.space.dynamic400),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.dynamic200),
            ) {
                Text(
                    text = "TabStripTabCounterButton",
                    style = FirefoxTheme.typography.body1,
                )

                Text(
                    text = """
                    Clicking the button will increment the tab count. Long press the button to open the dropdown menu.
                    """.trimIndent(),
                    style = FirefoxTheme.typography.caption,
                )

                var privacyBadgeVisible by remember { mutableStateOf(false) }
                var tabCount by remember { mutableIntStateOf(1) }
                TabStripTabCounterButton(
                    tabCount = tabCount,
                    size = 56.dp,
                    menuItems = listOf(
                        TabCounterMenuItem.IconItem.NewTab { },
                        TabCounterMenuItem.IconItem.NewPrivateTab { },
                        TabCounterMenuItem.Divider,
                        TabCounterMenuItem.IconItem.CloseTab { },
                    ).map { it.toMenuItem() },
                    onClick = { tabCount++ },
                    privacyBadgeVisible = privacyBadgeVisible,
                )

                FilledButton(
                    text = "Toggle privacy badge",
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    privacyBadgeVisible = !privacyBadgeVisible
                }
            }
        }
    }
}

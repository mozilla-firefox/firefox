/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.compose.list

import android.content.res.Configuration
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Expandable header for sections of lists
 *
 * @param headerText The title of the header.
 * @param headerTextStyle The text style of the header.
 * @param headerTextColor The [Color] of the [headerText].
 * @param iconTint The [Color] of the expand/collapse icon.
 * @param contentPadding The [PaddingValues] of the header.
 * @param expanded Indicates whether the section of content is expanded. If null, the Icon will be hidden.
 * @param expandActionContentDescription The content description for expanding the section.
 * @param collapseActionContentDescription The content description for collapsing the section.
 * @param onClick Optional lambda for handling header clicks.
 * @param headerTextWrapper Optional Composable wrapping the [headerText] content.
 * @param actions Optional Composable for adding UI to the end of the header.
 */
@Composable
fun ExpandableListHeader(
    headerText: String,
    headerTextStyle: TextStyle = FirefoxTheme.typography.headline8,
    headerTextColor: Color = MaterialTheme.colorScheme.onPrimaryContainer,
    iconTint: Color = MaterialTheme.colorScheme.onPrimaryContainer,
    contentPadding: PaddingValues = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
    expanded: Boolean? = null,
    expandActionContentDescription: String? = null,
    collapseActionContentDescription: String? = null,
    onClick: (() -> Unit)? = null,
    headerTextWrapper: @Composable (headerText: @Composable () -> Unit) -> Unit = { it() },
    actions: @Composable () -> Unit = {},
) {
    Row(
        modifier =
            when (onClick != null) {
                true -> Modifier.clickable { onClick() }
                false -> Modifier
            }.then(Modifier.fillMaxWidth()),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier.weight(1f).padding(contentPadding),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
                headerTextWrapper {
                    Text(
                        text = headerText,
                        color = headerTextColor,
                        style = headerTextStyle,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }

            expanded?.let {
                Spacer(modifier = Modifier.width(8.dp))

                Icon(
                    painter =
                        painterResource(
                            id =
                                if (expanded) {
                                    iconsR.drawable.mozac_ic_chevron_up_24
                                } else {
                                    iconsR.drawable.mozac_ic_chevron_down_24
                                }
                        ),
                    contentDescription =
                        if (expanded) {
                            collapseActionContentDescription
                        } else {
                            expandActionContentDescription
                        },
                    modifier = Modifier.size(20.dp),
                    tint = iconTint,
                )
            }
        }

        actions()
    }
}

@Composable
@Preview(uiMode = Configuration.UI_MODE_NIGHT_YES)
private fun TextOnlyHeaderPreview() {
    FirefoxTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surface)) {
            ExpandableListHeader(headerText = "Section title")
        }
    }
}

@Composable
@Preview(uiMode = Configuration.UI_MODE_NIGHT_YES)
private fun CollapsibleHeaderPreview() {
    FirefoxTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surface)) {
            ExpandableListHeader(
                headerText = "Collapsible section title",
                expanded = true,
                expandActionContentDescription = "",
                collapseActionContentDescription = "",
                onClick = { println("Clicked section header") },
            )
        }
    }
}

@Composable
@Preview(uiMode = Configuration.UI_MODE_NIGHT_NO)
private fun HeaderWithClickableIconPreview() {
    FirefoxTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surface)) {
            ExpandableListHeader(headerText = "Section title") {
                Box(
                    modifier =
                        Modifier.clickable(onClick = { println("delete clicked") })
                            .padding(horizontal = 16.dp, vertical = 8.dp)
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                        contentDescription = "click me",
                        modifier = Modifier.size(20.dp),
                        tint = MaterialTheme.colorScheme.onPrimaryContainer,
                    )
                }
            }
        }
    }
}

@Composable
@Preview(uiMode = Configuration.UI_MODE_NIGHT_NO)
private fun CollapsibleHeaderWithClickableIconPreview() {
    FirefoxTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surface)) {
            ExpandableListHeader(
                headerText = "Section title",
                expanded = true,
            ) {
                Box(
                    modifier =
                        Modifier.clickable(onClick = { println("delete clicked") })
                            .padding(horizontal = 16.dp, vertical = 8.dp)
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                        contentDescription = "click me",
                        modifier = Modifier.size(20.dp),
                        tint = MaterialTheme.colorScheme.onPrimaryContainer,
                    )
                }
            }
        }
    }
}

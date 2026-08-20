/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabitems

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import kotlin.time.Duration.Companion.seconds
import mozilla.components.compose.base.PromoCard
import mozilla.components.ui.animation.AnimatedIllustration
import org.mozilla.fenix.R
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.theme.FirefoxTheme

private val OnboardingGridItemMinHeight = 104.dp

/**
 * Onboarding for tab groups in the tab manager when in grid view.
 *
 * @param onDismiss Invoked when the dismiss button is clicked.
 * @param modifier The Modifier.
 */
@Composable
fun TabGroupOnboardingGridItem(
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    PromoCard(
        description = stringResource(R.string.tab_group_onboarding_grid_item_description),
        modifier =
            modifier
                .fillMaxWidth()
                .heightIn(min = OnboardingGridItemMinHeight)
                .testTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_GRID_ITEM)
                .focusable(true)
                .semantics(mergeDescendants = true) {},
        title = stringResource(R.string.tab_group_onboarding_item_title),
        illustration = {
            AnimatedIllustration(
                animationResource = R.raw.mozac_ic_kit_tab_groups_animation,
                staticDrawableResource = R.drawable.mozac_ic_kit_tab_groups,
                modifier =
                    Modifier.height(OnboardingGridItemMinHeight)
                        .padding(vertical = FirefoxTheme.layout.space.static150)
                        .testTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_ILLUSTRATION),
                contentDescription = null,
                iterationDelay = 8.seconds,
            )
        },
        contentSpacing = 0.dp,
        verticalAlignment = Alignment.CenterVertically,
        closeButtonContentDescription = stringResource(R.string.tab_group_onboarding_item_dismiss_content_description),
        onDismiss = onDismiss,
    )
}

/**
 * Onboarding for tab groups in the tab manager when in list view.
 *
 * @param onDismiss Invoked when the dismiss button is clicked.
 * @param modifier The Modifier.
 */
@Composable
fun TabGroupOnboardingListItem(
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .heightIn(min = TabListItemHeight)
                .background(MaterialTheme.colorScheme.primaryContainer)
                .padding(
                    start = FirefoxTheme.layout.space.dynamic200,
                    top = FirefoxTheme.layout.space.static100,
                    bottom = FirefoxTheme.layout.space.static100,
                )
                .testTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_LIST_ITEM)
                .focusable(true)
                .semantics(mergeDescendants = true) {},
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier.weight(1f),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Image(
                painter = painterResource(R.drawable.mozac_ic_kit_tab_groups_list_view),
                contentDescription = null,
                modifier = Modifier.testTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_ILLUSTRATION),
            )

            Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static200))

            Column(
                modifier = Modifier.weight(1f),
                horizontalAlignment = Alignment.Start,
            ) {
                Text(
                    text = stringResource(R.string.tab_group_onboarding_item_title),
                    color = MaterialTheme.colorScheme.onSurface,
                    style = FirefoxTheme.typography.headline8,
                    textAlign = TextAlign.Start,
                )

                Text(
                    text = stringResource(R.string.tab_group_onboarding_list_item_description),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = FirefoxTheme.typography.body2,
                    textAlign = TextAlign.Start,
                )
            }
        }

        ListItemDismissButton(
            onClick = onDismiss,
            contentDescription = stringResource(R.string.tab_group_onboarding_item_dismiss_content_description),
            modifier = Modifier.testTag(TabsTrayTestTag.TAB_GROUP_ONBOARDING_ITEM_DISMISS),
        )
    }
}

@PreviewLightDark
@Composable
private fun TabGroupOnboardingGridItemPreview() {
    FirefoxTheme {
        Surface {
            TabGroupOnboardingGridItem(
                modifier = Modifier.padding(FirefoxTheme.layout.space.static100),
                onDismiss = {},
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun TabGroupOnboardingListItemPreview() {
    FirefoxTheme {
        Surface {
            TabGroupOnboardingListItem(onDismiss = {})
        }
    }
}

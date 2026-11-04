/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.theme.Theme
import org.mozilla.fenix.R
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE_PRIVATE_BROWSING_LEARN_MORE_LINK
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Private Browsing Mode description.
 *
 * @param onLearnMoreClick Invoked when the user clicks on the learn more link.
 */
@Composable
fun PrivateBrowsingDescription(onLearnMoreClick: () -> Unit) {
    Surface {
        Column(
            modifier =
                Modifier.padding(horizontal = horizontalMargin)
                    .width(FirefoxTheme.layout.size.containerMaxWidth)
                    .padding(horizontal = FirefoxTheme.layout.space.static200),
            verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static100),
        ) {
            Text(
                text = stringResource(id = R.string.felt_privacy_desc_card_title),
                style = FirefoxTheme.typography.headline7,
            )

            Text(
                text =
                    stringResource(
                        id = R.string.felt_privacy_info_card_subtitle_3,
                        stringResource(id = R.string.app_name),
                    ),
                style = FirefoxTheme.typography.body2,
            )

            Box(
                modifier =
                    Modifier.semantics {
                        testTagsAsResourceId = true
                        testTag = HOMEPAGE_PRIVATE_BROWSING_LEARN_MORE_LINK
                    }
            ) {
                LinkText(
                    text = stringResource(id = R.string.felt_privacy_info_card_subtitle_link_text),
                    linkTextStates =
                        listOf(
                            LinkTextState(
                                text = stringResource(id = R.string.felt_privacy_info_card_subtitle_link_text),
                                url = "",
                                onClick = { onLearnMoreClick() },
                            )
                        ),
                    style = FirefoxTheme.typography.body2,
                    linkTextColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    linkTextDecoration = TextDecoration.Underline,
                )
            }
        }
    }
}

@Composable
@Preview
private fun PrivacyBrowsingDescriptionPreview() {
    FirefoxTheme(theme = Theme.Private) {
        Surface {
            PrivateBrowsingDescription(onLearnMoreClick = {})
        }
    }
}

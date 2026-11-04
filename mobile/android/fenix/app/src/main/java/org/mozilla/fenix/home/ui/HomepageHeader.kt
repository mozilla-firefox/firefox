/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.IconToggleButton
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.home.ui.HomepageTestTag.PRIVATE_BROWSING_HOMEPAGE_BUTTON
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.wallpapers.WallpaperTheme

/**
 * Header for the homepage.
 *
 * @param browsingMode The current [BrowsingMode].
 * @param showStoriesButton Whether to show the stories button or not.
 * @param showButtons Whether to show the stories and private browsing buttons.
 * @param browsingModeChanged Callback for when the browsing mode is toggled.
 * @param onStoriesTapped Callback for when the stories button is tapped.
 */
@Composable
fun HomepageHeader(
    browsingMode: BrowsingMode,
    showStoriesButton: Boolean,
    showButtons: Boolean,
    browsingModeChanged: (BrowsingMode) -> Unit,
    onStoriesTapped: () -> Unit,
) {
    // In normal mode the wordmark and private-browsing button follow the wallpaper text color; in
    // private mode there is no wallpaper, so the wordmark is untinted and the button uses its
    // dedicated private-mode icon color.
    val wordmarkTextColor =
        if (browsingMode.isPrivate) {
            null
        } else {
            WallpaperTheme.onWallpaper
        }
    val privateBrowsingButtonColor =
        if (browsingMode.isPrivate) {
            colorResource(getAttr(iconsR.attr.mozac_ic_private_mode_circle_fill_icon_color))
        } else {
            WallpaperTheme.onWallpaper
        }

    Column(
        modifier =
            Modifier.fillMaxWidth()
                .wrapContentHeight()
                .padding(top = FirefoxTheme.layout.space.static100)
                .thenConditional(Modifier.padding(bottom = FirefoxTheme.layout.space.static500)) {
                    !browsingMode.isPrivate
                }
                .padding(horizontal = FirefoxTheme.layout.space.static100),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        if (showButtons) {
            Row(
                modifier = Modifier.fillMaxWidth().wrapContentHeight(),
                verticalAlignment = Alignment.Top,
            ) {
                if (showStoriesButton) {
                    StoriesButton(color = WallpaperTheme.onWallpaper, onClick = onStoriesTapped)
                }

                Spacer(modifier = Modifier.weight(1f))

                PrivateBrowsingButton(
                    color = privateBrowsingButtonColor,
                    browsingMode = browsingMode,
                    browsingModeChanged = browsingModeChanged,
                )
            }
        }

        if (!browsingMode.isPrivate) {
            WordmarkAndLogo(wordmarkTextColor = wordmarkTextColor)
        }
    }
}

@Composable
private fun StoriesButton(
    color: Color,
    onClick: () -> Unit,
) {
    IconButton(onClick = onClick, contentDescription = null) {
        Icon(
            tint = color,
            painter = painterResource(iconsR.drawable.mozac_ic_reading_list_24),
            contentDescription = stringResource(R.string.homepage_all_stories),
        )
    }
}

@Composable
private fun PrivateBrowsingButton(
    color: Color,
    browsingMode: BrowsingMode,
    browsingModeChanged: (BrowsingMode) -> Unit,
) {
    IconToggleButton(
        modifier =
            Modifier.background(
                    color = colorResource(getAttr(iconsR.attr.mozac_ic_private_mode_circle_fill_background_color)),
                    shape = CircleShape,
                )
                .semantics {
                    role = Role.Switch
                    testTagsAsResourceId = true
                    testTag = PRIVATE_BROWSING_HOMEPAGE_BUTTON
                },
        checked = browsingMode.isPrivate,
        onCheckedChange = {
            browsingModeChanged(BrowsingMode.fromBoolean(!browsingMode.isPrivate))
        },
    ) {
        Icon(
            tint = color,
            painter = painterResource(iconsR.drawable.mozac_ic_private_mode_24),
            contentDescription = stringResource(R.string.content_description_private_browsing),
        )
    }
}

@Composable
internal fun getAttr(resId: Int): Int {
    val typedArray = LocalContext.current.obtainStyledAttributes(intArrayOf(resId))
    val newResId = typedArray.getResourceId(0, 0)
    typedArray.recycle()

    return newResId
}

@Preview
@Composable
private fun HomepageHeaderPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        Surface {
            HomepageHeader(
                browsingMode = BrowsingMode.Normal,
                showStoriesButton = true,
                showButtons = true,
                browsingModeChanged = {},
                onStoriesTapped = {},
            )
        }
    }
}

@Preview
@Composable
private fun HomepageHeaderPrivatePreview() {
    FirefoxTheme(theme = Theme.Private) {
        Surface {
            HomepageHeader(
                browsingMode = BrowsingMode.Private,
                showStoriesButton = false,
                showButtons = true,
                browsingModeChanged = {},
                onStoriesTapped = {},
            )
        }
    }
}

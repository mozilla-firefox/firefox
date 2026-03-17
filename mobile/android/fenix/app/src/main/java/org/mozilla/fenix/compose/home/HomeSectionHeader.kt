/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.compose.home

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.utils.inComposePreview
import mozilla.components.lib.state.ext.observeAsComposableState
import org.mozilla.fenix.R
import org.mozilla.fenix.components.components
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.wallpapers.Wallpaper
import mozilla.components.ui.icons.R as iconsR

/**
 * Homepage header.
 *
 * @param headerText The header string.
 * @param modifier Modifier to apply.
 * @param description The content description for the button.
 * @param buttonText The text to show for the button.
 * @param onButtonClick Invoked when the button is clicked.
 */
@Composable
fun HomeSectionHeader(
    headerText: String,
    modifier: Modifier = Modifier,
    description: String = "",
    buttonText: String = stringResource(id = R.string.recent_tabs_show_all),
    onButtonClick: (() -> Unit)? = null,
) {
    if (inComposePreview) {
        HomeSectionHeaderContent(
            headerText = headerText,
            modifier = modifier,
            description = description,
            onButtonClick = onButtonClick,
        )
    } else {
        val wallpaperState = components.appStore
            .observeAsComposableState { state -> state.wallpaperState }.value
        val isWallpaperDefault = wallpaperState.currentWallpaper == Wallpaper.Default

        HomeSectionHeaderContent(
            headerText = headerText,
            modifier = modifier,
            textColor = wallpaperState.textColor,
            description = description,
            buttonColor = if (isWallpaperDefault) {
                MaterialTheme.colorScheme.onSurface
            } else {
                wallpaperState.textColor
            },
            buttonText = buttonText,
            onButtonClick = onButtonClick,
        )
    }
}

/**
 * Homepage header content.
 *
 * @param headerText The header string.
 * @param modifier Modifier to apply.
 * @param textColor [Color] to apply to the text.
 * @param description The content description for the button.
 * @param buttonColor [Color] for the button contents.
 * @param buttonText The text to show for the button.
 * @param onButtonClick Invoked when the button is clicked.
 */
@Composable
private fun HomeSectionHeaderContent(
    headerText: String,
    modifier: Modifier = Modifier,
    textColor: Color = AcornTheme.colors.textNormal,
    description: String = "",
    buttonColor: Color = AcornTheme.colors.textNormal,
    buttonText: String = stringResource(id = R.string.recent_tabs_show_all),
    onButtonClick: (() -> Unit)? = null,
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = headerText,
            modifier = Modifier
                .weight(1f)
                .wrapContentHeight(align = Alignment.Top)
                .semantics { heading() },
            color = textColor,
            overflow = TextOverflow.Ellipsis,
            maxLines = 2,
            style = FirefoxTheme.typography.subtitle1,
        )

        Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.dynamic100))

        onButtonClick?.let {
            TextButton(
                onClick = { onButtonClick() },
                modifier = Modifier
                    .clip(RoundedCornerShape(999.dp))
                    .height(height = LocalResources.current.getDimensionPixelSize(com.android.car.navioui.R.dimen.dsDefaultHeight).dp)
                    .background(
                        color = AcornTheme.colors.controlsButtonPrimaryNormal  // ✅ Цвет фона
                    ),
                colors = ButtonDefaults.textButtonColors(
                    contentColor = AcornTheme.colors.textInverse,
                ),
                // Добавь padding для контента внутри кнопки
                contentPadding = PaddingValues(horizontal = 28.dp)
            ) {
                Row(  // Оберни в Row для правильного выравнивания
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                ) {
                    Text(
                        text = buttonText,
                        modifier = Modifier // Занимает оставшееся место
                            .semantics {
                                contentDescription = description
                            },
                        style = FirefoxTheme.typography.subtitle1,
                    )

//                    Icon(
//                        painter = painterResource(id = iconsR.drawable.mozac_ic_chevron_right_16),
//                        contentDescription = null,
//                    )
                }
            }
        }

    }
}

@Preview
@Composable
private fun HomeSectionsHeaderPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        Surface {
            HomeSectionHeader(
                headerText = stringResource(R.string.home_bookmarks_title),
                modifier = Modifier.padding(horizontal = FirefoxTheme.layout.size.static300),
                description = stringResource(R.string.home_bookmarks_show_all_content_description),
                onButtonClick = {},
            )
        }
    }
}

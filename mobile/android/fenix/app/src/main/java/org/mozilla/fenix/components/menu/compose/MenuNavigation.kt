/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.Hyphens
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import mozilla.components.ui.icons.R as iconsR

@Suppress("LongParameterList")
@Composable
internal fun MenuNavigation(
    isSiteLoading: Boolean,
    isExtensionsExpanded: Boolean,
    isMoreMenuExpanded: Boolean,
    onBackButtonClick: (longPress: Boolean) -> Unit,
    onForwardButtonClick: (longPress: Boolean) -> Unit,
    onRefreshButtonClick: (longPress: Boolean) -> Unit,
    onStopButtonClick: () -> Unit,
    onShareButtonClick: () -> Unit,
    state: MenuItemState = MenuItemState.ENABLED,
    goBackState: MenuItemState = MenuItemState.ENABLED,
    goForwardState: MenuItemState = MenuItemState.ENABLED,
) {
    val navigationHeaderContentDescription =
        stringResource(id = R.string.browser_main_menu_content_description_navigation_header)

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(AcornTheme.colors.layer3
            )
            .padding(horizontal = 4.dp, vertical = 12.dp)
            .verticalScroll(rememberScrollState())
            .semantics(mergeDescendants = true) {
                contentDescription = navigationHeaderContentDescription
            },
        horizontalArrangement = Arrangement.spacedBy(4.dp),
        verticalAlignment = Alignment.Top,
    ) {
        MenuNavItem(
            modifier = Modifier.weight(1f),
            state = goBackState,
            painter = painterResource(id = iconsR.drawable.mozac_ic_back_24),
            label = stringResource(id = R.string.browser_menu_back),
            onClick = { onBackButtonClick(false) },
            onLongClick = { onBackButtonClick(true) },
        )

        MenuNavItem(
            modifier = Modifier.weight(1f),
            state = goForwardState,
            painter = painterResource(id = iconsR.drawable.mozac_ic_forward_24),
            label = stringResource(id = R.string.browser_menu_forward),
            onClick = { onForwardButtonClick(false) },
            onLongClick = { onForwardButtonClick(true) },
        )

        MenuNavItem(
            modifier = Modifier.weight(1f),
            state = state,
            painter = painterResource(id = iconsR.drawable.mozac_ic_share_android_24),
            label = stringResource(id = R.string.browser_menu_share),
            onClick = onShareButtonClick,
        )

        if (isSiteLoading) {
            MenuNavItem(
                modifier = Modifier.weight(1f),
                state = state,
                painter = painterResource(id = iconsR.drawable.mozac_ic_stop),
                label = stringResource(id = R.string.browser_menu_stop),
                onClick = onStopButtonClick,
            )
        } else {
            MenuNavItem(
                modifier = Modifier.weight(1f),
                state = state,
                painter = painterResource(id = iconsR.drawable.mozac_ic_arrow_clockwise_24),
                label = stringResource(id = R.string.browser_menu_refresh),
                onClick = { onRefreshButtonClick(false) },
                onLongClick = { onRefreshButtonClick(true) },
            )
        }
    }
}

@Composable
private fun MenuNavItem(
    modifier: Modifier = Modifier,
    state: MenuItemState = MenuItemState.ENABLED,
    painter: Painter,
    label: String,
    onClick: () -> Unit = {},
    onLongClick: (() -> Unit)? = null,
) {
    Column(
        modifier = modifier
            .fillMaxHeight()
            .combinedClickable(
                interactionSource = null,
                indication = LocalIndication.current,
                enabled = state != MenuItemState.DISABLED,
                onClick = onClick,
                onLongClick = onLongClick,
                role = Role.Button,
            ),
        verticalArrangement = Arrangement.Top,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(
            painter = painter,
            contentDescription = null,
            tint = null,//getIconTint(state = state),
            modifier = Modifier.alpha(
                if (state == MenuItemState.DISABLED) 0.5f else 1.0f
            )
        )

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static50))

        Text(
            text = label,
            style = FirefoxTheme.typography.caption.copy(hyphens = Hyphens.Auto),
            color = getLabelTextColor(state = state),
            maxLines = 2,
            softWrap = true,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
private fun getLabelTextColor(state: MenuItemState): Color {
    return when (state) {
        MenuItemState.ACTIVE -> AcornTheme.colors.textNormal
        MenuItemState.WARNING -> MaterialTheme.colorScheme.error
        MenuItemState.DISABLED -> AcornTheme.colors.textNormal.copy(alpha = 0.38f)
        else -> AcornTheme.colors.textNormal
    }
}

@Composable
private fun getIconTint(state: MenuItemState): Color {
    return when (state) {
        MenuItemState.ACTIVE -> MaterialTheme.colorScheme.tertiary
        MenuItemState.WARNING -> MaterialTheme.colorScheme.error
        MenuItemState.DISABLED -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
        else -> MaterialTheme.colorScheme.onSurface
    }
}

@Preview
@Composable
private fun MenuNavigationPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        MenuNavigation(
            isSiteLoading = false,
            isExtensionsExpanded = false,
            isMoreMenuExpanded = false,
            onBackButtonClick = {},
            onForwardButtonClick = {},
            onRefreshButtonClick = {},
            onStopButtonClick = {},
            onShareButtonClick = {},
        )
    }
}

@Preview
@Composable
private fun MenuNavigationExpandedPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        MenuNavigation(
            isSiteLoading = false,
            isExtensionsExpanded = true,
            isMoreMenuExpanded = false,
            onBackButtonClick = {},
            onForwardButtonClick = {},
            onRefreshButtonClick = {},
            onStopButtonClick = {},
            onShareButtonClick = {},
        )
    }
}

@Preview
@Composable
private fun MenuNavigationSiteLoadingPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        MenuNavigation(
            isSiteLoading = true,
            isExtensionsExpanded = false,
            isMoreMenuExpanded = false,
            onBackButtonClick = {},
            onForwardButtonClick = {},
            onRefreshButtonClick = {},
            onStopButtonClick = {},
            onShareButtonClick = {},
        )
    }
}

@Preview
@Composable
private fun MenuNavigationExpandedSiteLoadingPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        MenuNavigation(
            isSiteLoading = true,
            isExtensionsExpanded = true,
            isMoreMenuExpanded = false,
            onBackButtonClick = {},
            onForwardButtonClick = {},
            onRefreshButtonClick = {},
            onStopButtonClick = {},
            onShareButtonClick = {},
        )
    }
}

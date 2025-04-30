/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.Icon
import androidx.compose.material.IconToggleButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.Theme

/**
 * Header for the homepage.
 */
@Composable
fun HomepageHeader(browsingMode: BrowsingMode, browsingModeChanged: (BrowsingMode) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .wrapContentHeight()
            .padding(start = 16.dp, end = 16.dp, top = 18.dp, bottom = 32.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        WordmarkLogo()

        WordmarkText()

        Spacer(modifier = Modifier.weight(1f))

        PrivateBrowsingButton(
            browsingMode = browsingMode,
            browsingModeChanged = browsingModeChanged,
        )
    }
}

@Composable
private fun WordmarkLogo() {
    Image(
        modifier = Modifier
            .height(40.dp)
            .padding(end = 10.dp),
        painter = painterResource(getAttr(R.attr.fenixWordmarkLogo)),
        contentDescription = null,
    )
}

@Composable
private fun WordmarkText() {
    Image(
        modifier = Modifier.height(dimensionResource(R.dimen.wordmark_text_height)),
        painter = painterResource(getAttr(R.attr.fenixWordmarkText)),
        contentDescription = stringResource(R.string.app_name),
    )
}

@Composable
private fun PrivateBrowsingButton(
    browsingMode: BrowsingMode,
    browsingModeChanged: (BrowsingMode) -> Unit,
) {
    IconToggleButton(
        modifier = Modifier
            .background(
                color = colorResource(getAttr(R.attr.mozac_ic_private_mode_circle_fill_background_color)),
                shape = CircleShape,
            )
            .size(40.dp),
        checked = browsingMode.isPrivate,
        onCheckedChange = {
            browsingModeChanged(BrowsingMode.fromBoolean(!browsingMode.isPrivate))
        },
    ) {
        Icon(
            tint = colorResource(getAttr(R.attr.mozac_ic_private_mode_circle_fill_icon_color)),
            painter = painterResource(R.drawable.mozac_ic_private_mode_24),
            contentDescription = stringResource(R.string.content_description_private_browsing),
        )
    }
}

@Composable
private fun getAttr(resId: Int): Int {
    val typedArray = LocalContext.current.obtainStyledAttributes(intArrayOf(resId))
    val newResId = typedArray.getResourceId(0, 0)
    typedArray.recycle()

    return newResId
}

@Composable
@PreviewLightDark
private fun HomepageHeaderPreview() {
    FirefoxTheme {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(color = FirefoxTheme.colors.layer1),
        ) {
            HomepageHeader(browsingMode = BrowsingMode.Normal, browsingModeChanged = {})
        }
    }
}

@Composable
@Preview
private fun PrivateHomepageHeaderPreview() {
    FirefoxTheme(theme = Theme.Private) {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(color = FirefoxTheme.colors.layer1),
        ) {
            HomepageHeader(browsingMode = BrowsingMode.Private, browsingModeChanged = {})
        }
    }
}

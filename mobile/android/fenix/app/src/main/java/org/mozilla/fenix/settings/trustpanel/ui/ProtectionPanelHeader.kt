/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.minimumInteractiveComponentSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.layout
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.settings.trustpanel.store.WebsiteInfoState
import org.mozilla.fenix.theme.FirefoxTheme

private val ICON_SIZE = 16.dp
private val ICON_PADDING = 8.dp
private val INNER_ICON_SHAPE = RoundedCornerShape(0.dp)
private const val SECURE_CONNECTION_LINK_MAX_WIDTH_FRACTION = 0.5f

internal const val SECURE_CONNECTION_LINK_TEST_TAG = "unified.trust.panel.secure.connection"

/**
 * Header of the protection panel, showing the current website and, when available, a link to its certificate details.
 *
 * @param websiteInfoState The [WebsiteInfoState] of the current website.
 * @param icon The favicon of the current website, or null to fall back to a generated icon.
 * @param modifier [Modifier] to be applied to the layout.
 * @param onSecureConnectionClick Optional callback invoked when the "Secure connection" link is clicked to open the
 *   certificate details, or null to not show the link.
 * @param focusSecureConnectionLink Whether to focus the "Secure connection" link, for example when returning from the
 *   certificate details.
 * @param onSecureConnectionLinkFocusHandled Invoked once the focus request has been handled, either by focusing the
 *   "Secure connection" link or, if the link is not shown, by dropping the request.
 */
@Composable
internal fun ProtectionPanelHeader(
    websiteInfoState: WebsiteInfoState,
    icon: Bitmap?,
    modifier: Modifier = Modifier,
    onSecureConnectionClick: (() -> Unit)? = null,
    focusSecureConnectionLink: Boolean = false,
    onSecureConnectionLinkFocusHandled: () -> Unit = {},
) {
    LaunchedEffect(focusSecureConnectionLink, onSecureConnectionClick == null) {
        // The link cannot be focused while it is hidden, so drop the request instead of focusing it once it reappears.
        if (focusSecureConnectionLink && onSecureConnectionClick == null) {
            onSecureConnectionLinkFocusHandled()
        }
    }

    Row(
        modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ProtectionPanelIcon(url = websiteInfoState.websiteUrl, icon = icon)

        Spacer(modifier = Modifier.width(16.dp))

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text =
                    websiteInfoState.websiteTitle.ifEmpty {
                        websiteInfoState.websiteUrl.tryGetHostFromUrl()
                    },
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                style = FirefoxTheme.typography.headline7,
                overflow = TextOverflow.Ellipsis,
                modifier =
                    Modifier.semantics {
                        testTagsAsResourceId = true
                        testTag = "unified.trust.panel.website"
                    },
            )

            if (websiteInfoState.websiteTitle.isNotEmpty()) {
                Text(
                    text = websiteInfoState.websiteUrl.tryGetHostFromUrl(),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = FirefoxTheme.typography.body2,
                    modifier =
                        Modifier.semantics {
                            testTagsAsResourceId = true
                            testTag = "unified.trust.panel.website.url"
                        },
                )
            }
        }

        if (onSecureConnectionClick != null) {
            Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static100))

            SecureConnectionLink(
                onClick = onSecureConnectionClick,
                requestFocus = focusSecureConnectionLink,
                onFocused = onSecureConnectionLinkFocusHandled,
                modifier =
                    Modifier.layout { measurable, constraints ->
                        // Bound the link so that the website title keeps a share of the row at large font scales.
                        val maxWidth = (constraints.maxWidth * SECURE_CONNECTION_LINK_MAX_WIDTH_FRACTION).toInt()
                        val placeable = measurable.measure(constraints.copy(minWidth = 0, maxWidth = maxWidth))
                        layout(placeable.width, placeable.height) { placeable.place(0, 0) }
                    },
            )
        }
    }
}

@Composable
private fun SecureConnectionLink(
    onClick: () -> Unit,
    requestFocus: Boolean,
    onFocused: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val focusRequester = remember { FocusRequester() }
    LaunchedEffect(requestFocus) {
        if (requestFocus) {
            focusRequester.requestFocus()
            onFocused()
        }
    }

    Row(
        modifier =
            modifier
                .minimumInteractiveComponentSize()
                .focusRequester(focusRequester)
                .clip(MaterialTheme.shapes.small)
                .clickable(
                    onClickLabel = stringResource(R.string.connection_security_panel_view_certificate_details),
                    role = Role.Button,
                    onClick = onClick,
                )
                .padding(horizontal = FirefoxTheme.layout.space.static50)
                .semantics {
                    testTagsAsResourceId = true
                    testTag = SECURE_CONNECTION_LINK_TEST_TAG
                },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurfaceVariant) {
            Icon(painter = painterResource(id = iconsR.drawable.mozac_ic_lock_16), contentDescription = null)

            Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static50))

            Text(
                text = stringResource(R.string.connection_security_panel_secure),
                modifier = Modifier.weight(weight = 1f, fill = false),
                style = FirefoxTheme.typography.body2,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )

            Icon(painter = painterResource(id = iconsR.drawable.mozac_ic_chevron_right_16), contentDescription = null)
        }
    }
}

@Composable
internal fun ProtectionPanelIcon(
    url: String,
    icon: Bitmap?,
) {
    if (icon != null && !icon.isRecycled) {
        Image(
            bitmap = icon.asImageBitmap(),
            contentDescription = null,
            modifier =
                Modifier.background(
                        color = MaterialTheme.colorScheme.surfaceContainerLowest,
                        shape = MaterialTheme.shapes.extraSmall,
                    )
                    .padding(all = ICON_PADDING)
                    .size(ICON_SIZE),
        )
    } else {
        Favicon(
            url = url,
            modifier =
                Modifier.background(
                        color = MaterialTheme.colorScheme.surfaceContainerLowest,
                        shape = MaterialTheme.shapes.extraSmall,
                    )
                    .padding(all = ICON_PADDING),
            size = ICON_SIZE,
            shape = INNER_ICON_SHAPE,
        )
    }
}

@Preview
@Composable
private fun ProtectionPanelHeaderPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        ProtectionPanelHeader(
            websiteInfoState =
                WebsiteInfoState(
                    isSecured = true,
                    websiteUrl = "https://www.mozilla.org",
                    websiteTitle = "Mozilla",
                    certificate = null,
                ),
            icon = null,
            modifier = Modifier.background(color = MaterialTheme.colorScheme.surface),
        )
    }
}

@Preview
@Composable
private fun ProtectionPanelHeaderUrlAsTitlePreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        ProtectionPanelHeader(
            websiteInfoState =
                WebsiteInfoState(
                    isSecured = true,
                    websiteUrl = "https://www.mozilla.org",
                    websiteTitle = "",
                    certificate = null,
                ),
            icon = null,
            modifier = Modifier.background(color = MaterialTheme.colorScheme.surface),
        )
    }
}

@Preview(fontScale = 2f)
@Composable
private fun ProtectionPanelHeaderSecureConnectionLargeFontPreview() {
    FirefoxTheme {
        ProtectionPanelHeader(
            websiteInfoState =
                WebsiteInfoState(
                    isSecured = true,
                    websiteUrl = "https://www.mozilla.org",
                    websiteTitle = "",
                    certificate = null,
                ),
            icon = null,
            modifier = Modifier.background(color = MaterialTheme.colorScheme.surface),
            onSecureConnectionClick = {},
        )
    }
}

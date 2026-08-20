/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf.ui

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.button.IconButton
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.pdf.PdfToolsTestTag
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Creates UI for drawing a PDF toolbox on PDFs viewed in Firefox.
 *
 * @param isLargeWindow Used to determine if the device should be treated as a tablet.
 * @param onSignClick Invoked when the user activates the sign PDF button.
 * @param onDownloadClick Invoked when the user activates the download PDF button.
 * @param onPrintClick Invoked when the user activates the print PDF button.
 * @param onShareClick Invoked when the user activates the share PDF button.
 * @param modifier [Modifier] to be applied to the layout.
 */
@Composable
fun PdfTools(
    isLargeWindow: Boolean,
    onSignClick: () -> Unit,
    onDownloadClick: () -> Unit,
    onPrintClick: () -> Unit,
    onShareClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val containerModifier =
        modifier
            .fillMaxWidth()
            .padding(
                start = FirefoxTheme.layout.space.static200,
                top = FirefoxTheme.layout.space.static100,
                end = FirefoxTheme.layout.space.static200,
                bottom = FirefoxTheme.layout.space.static200,
            )

    if (isLargeWindow) {
        Box(
            modifier = containerModifier,
            contentAlignment = Alignment.Center,
        ) {
            PdfToolbarButtons(
                onDownloadClick = onDownloadClick,
                onPrintClick = onPrintClick,
                onShareClick = onShareClick,
                onSignClick = onSignClick,
            )
        }
    } else {
        Row(
            modifier = containerModifier,
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            SignPdfFab(onClick = onSignClick)

            PdfToolbarButtons(
                onDownloadClick = onDownloadClick,
                onPrintClick = onPrintClick,
                onShareClick = onShareClick,
            )
        }
    }
}

/**
 * The toolbar for containing the buttons. [onSignClick] may be omitted.
 *
 * @param onDownloadClick Invoked when the user activates the download PDF button.
 * @param onPrintClick Invoked when the user activates the print PDF button.
 * @param onShareClick Invoked when the user activates the share PDF button.
 * @param onSignClick Invoked when the user activates the sign PDF button.
 */
@Composable
private fun PdfToolbarButtons(
    onDownloadClick: () -> Unit,
    onPrintClick: () -> Unit,
    onShareClick: () -> Unit,
    onSignClick: (() -> Unit)? = null,
) {
    PdfToolsSurface(modifier = Modifier.testTag(PdfToolsTestTag.BAR)) {
        Row(
            modifier = Modifier.padding(horizontal = FirefoxTheme.layout.space.static100),
            horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static50),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (onSignClick != null) {
                PdfToolbarButton(
                    icon = iconsR.drawable.mozac_ic_signature_24,
                    contentDescription = R.string.pdf_tools_sign_content_description,
                    testTag = PdfToolsTestTag.SIGN_BUTTON,
                    onClick = onSignClick,
                )
            }

            PdfToolbarButton(
                icon = iconsR.drawable.mozac_ic_download_24,
                contentDescription = R.string.pdf_tools_download_content_description,
                testTag = PdfToolsTestTag.DOWNLOAD_BUTTON,
                onClick = onDownloadClick,
            )

            PdfToolbarButton(
                icon = iconsR.drawable.mozac_ic_print_24,
                contentDescription = R.string.pdf_tools_print_content_description,
                testTag = PdfToolsTestTag.PRINT_BUTTON,
                onClick = onPrintClick,
            )

            PdfToolbarButton(
                icon = iconsR.drawable.mozac_ic_share_android_24,
                contentDescription = R.string.pdf_tools_share_content_description,
                testTag = PdfToolsTestTag.SHARE_BUTTON,
                onClick = onShareClick,
            )
        }
    }
}

/**
 * The floating toolbar surface for the buttons and toolbar.
 *
 * @param modifier [Modifier] to apply to the surface.
 * @param content The content that should be contained on the floating toolbar.
 */
@Composable
private fun PdfToolsSurface(
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Card(
        modifier = modifier.height(FirefoxTheme.layout.size.static600),
        shape = CircleShape,
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainer,
                contentColor = MaterialTheme.colorScheme.onSurface,
            ),
        elevation = CardDefaults.elevatedCardElevation(defaultElevation = FirefoxTheme.layout.elevation.level3),
    ) {
        content()
    }
}

/**
 * A PDF toolbar button.
 *
 * @param icon The resource ID for the button.
 * @param contentDescription The content description for screen readers.
 * @param testTag Test tag for identifying the button.
 * @param onClick Invoked when the user activates the button.
 * @param modifier [Modifier] to apply to the button.
 */
@Composable
private fun PdfToolbarButton(
    @DrawableRes icon: Int,
    @StringRes contentDescription: Int,
    testTag: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    IconButton(
        onClick = onClick,
        contentDescription = stringResource(id = contentDescription),
        modifier = modifier.testTag(testTag),
    ) {
        Icon(
            painter = painterResource(id = icon),
            contentDescription = null,
        )
    }
}

/**
 * The floating sign button.
 *
 * @param onClick Invoked when the user activates the button.
 */
@Composable
private fun SignPdfFab(onClick: () -> Unit) {
    PdfToolsSurface(modifier = Modifier.width(FirefoxTheme.layout.size.static600)) {
        PdfToolbarButton(
            icon = iconsR.drawable.mozac_ic_signature_24,
            contentDescription = R.string.pdf_tools_sign_content_description,
            testTag = PdfToolsTestTag.SIGN_FAB,
            onClick = onClick,
            modifier = Modifier.fillMaxSize(),
        )
    }
}

@PreviewLightDark
@Composable
private fun PdfToolsPhonePreview() {
    FirefoxTheme {
        PdfTools(
            isLargeWindow = false,
            onSignClick = {},
            onDownloadClick = {},
            onPrintClick = {},
            onShareClick = {},
        )
    }
}

@PreviewLightDark
@Composable
private fun PdfToolsTabletPreview() {
    FirefoxTheme {
        PdfTools(
            isLargeWindow = true,
            onSignClick = {},
            onDownloadClick = {},
            onPrintClick = {},
            onShareClick = {},
        )
    }
}

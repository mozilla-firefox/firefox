/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredHeight
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.summarize.R
import mozilla.components.feature.summarize.ui.gradient.summaryLoadingGradient
import mozilla.components.ui.icons.R as iconsR

private const val DRAG_HANDLE_CORNER_RATIO = 50

/**
 * Content shown while a page summary is being generated.
 * Displays a Firefox logo icon and loading text over the animated gradient background.
 */
@Composable
internal fun SummarizingContent(
    modifier: Modifier = Modifier,
    title: String = stringResource(R.string.mozac_feature_summarize_loading_title),
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(top = 80.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        val contentColor = if (isSystemInDarkTheme()) {
            MaterialTheme.colorScheme.onSurface
        } else {
            MaterialTheme.colorScheme.onPrimary
        }

        Icon(
            painter = painterResource(id = iconsR.drawable.mozac_ic_logo_firefox_24),
            contentDescription = null,
            modifier = Modifier.size(48.dp),
            tint = contentColor,
        )

        Text(
            text = title,
            textAlign = TextAlign.Center,
            color = contentColor.copy(alpha = 0.5f),
            fontSize = 16.sp,
            fontWeight = FontWeight.SemiBold,
            lineHeight = 21.sp,
            letterSpacing = (-0.31).sp,
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun SummarizingContentPreview() {
    AcornTheme {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.BottomCenter,
        ) {
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(336.dp),
                shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
            ) {
                Box(modifier = Modifier.fillMaxSize().summaryLoadingGradient()) {
                    Column(modifier = Modifier.fillMaxWidth()) {
                        Box(
                            modifier = Modifier.fillMaxWidth().requiredHeight(36.dp),
                            contentAlignment = Alignment.Center,
                        ) {
                            Box(
                                modifier = Modifier
                                    .requiredSize(width = 32.dp, height = 4.dp)
                                    .background(
                                        color = MaterialTheme.colorScheme.outline,
                                        shape = RoundedCornerShape(DRAG_HANDLE_CORNER_RATIO),
                                    ),
                            )
                        }
                        SummarizingContent()
                    }
                }
            }
        }
    }
}

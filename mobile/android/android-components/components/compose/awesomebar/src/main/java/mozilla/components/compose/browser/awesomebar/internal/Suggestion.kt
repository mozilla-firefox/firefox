/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.awesomebar.internal

import android.graphics.Bitmap
import android.graphics.drawable.Drawable
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.graphics.drawable.toBitmap
import mozilla.components.compose.browser.awesomebar.AwesomeBarColors
import mozilla.components.compose.browser.awesomebar.AwesomeBarOrientation
import mozilla.components.compose.browser.awesomebar.R
import mozilla.components.concept.awesomebar.AwesomeBar
import mozilla.components.ui.icons.R as iconsR

// We only show one row of text, covering at max screen width.
// Limit bigger texts that could cause slowdowns or even crashes.
private const val SUGGESTION_TEXT_MAX_LENGTH = 100

@Composable
internal fun Suggestion(
    suggestion: AwesomeBar.Suggestion,
    colors: AwesomeBarColors,
    orientation: AwesomeBarOrientation,
    onSuggestionClicked: () -> Unit,
    onAutoComplete: () -> Unit,
    onRemoveClicked: () -> Unit,
) {
    Row(
        modifier = Modifier
            .clickable { onSuggestionClicked() }
            .defaultMinSize(minHeight = 64.dp)
            .testTag("mozac.awesomebar.suggestion")
            .padding(start = 16.dp, top = 8.dp, bottom = 8.dp, end = 8.dp),
    ) {
        val icon = suggestion.icon
        if (icon != null) {
            SuggestionIcon(
                icon = icon,
                indicator = suggestion.indicatorIcon,
                modifier = Modifier.align(Alignment.CenterVertically),
            )
        }
        SuggestionTitleAndDescription(
            title = suggestion.title?.take(SUGGESTION_TEXT_MAX_LENGTH),
            description = suggestion.description?.take(SUGGESTION_TEXT_MAX_LENGTH),
            colors = colors,
            modifier = Modifier
                .weight(1f)
                .align(Alignment.CenterVertically),
        )
        if (suggestion.editSuggestion != null) {
            AutocompleteButton(
                onAutoComplete = onAutoComplete,
                orientation = orientation,
                colors = colors,
                modifier = Modifier.align(Alignment.CenterVertically)
                    .padding(end = 20.dp),
            )
        }
        if (suggestion.isRemovalAllowed) {
            RemoveButton(
                onRemoveClicked = onRemoveClicked,
                colors = colors,
                modifier = Modifier.align(Alignment.CenterVertically)
                    .padding(end = 20.dp),
            )
        }
    }
}

@Composable
private fun SuggestionTitleAndDescription(
    title: String?,
    description: String?,
    colors: AwesomeBarColors,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
    ) {
        Text(
            text = if (title.isNullOrEmpty()) {
                description ?: ""
            } else {
                title
            },
            color = colors.title,
            fontSize = 24.sp,
            fontFamily = FontFamily(Font(com.android.car.navioui.R.font.vesta2_bold)),
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier
                .width(IntrinsicSize.Max)
                .padding(start = 12.dp, end = 8.dp),
        )
        if (description?.isNotEmpty() == true) {
            Text(
                text = description,
                color = colors.description,
                fontSize = 20.sp,
                fontFamily = FontFamily(Font(com.android.car.navioui.R.font.vesta2_medium)),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier
                    .width(IntrinsicSize.Max)
                    .padding(start = 12.dp, end = 8.dp),
            )
        }
    }
}

@Composable
private fun SuggestionIcon(
    icon: Bitmap,
    indicator: Drawable?,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .width(48.dp)
            .height(56.dp),
    ) {
        Image(
            icon.asImageBitmap(),
            contentDescription = null,
            modifier = Modifier
                .padding(top = 16.dp)
                .clip(RoundedCornerShape(2.dp))
                .width(36.dp)
                .height(36.dp),
            contentScale = ContentScale.Crop,
        )
        if (indicator != null) {
            Image(
                indicator.toBitmap().asImageBitmap(),
                contentDescription = null,
                modifier = Modifier
                    .padding(top = 22.dp, start = 20.dp)
                    .width(36.dp)
                    .height(36.dp),
            )
        }
    }
}

@Composable
@Suppress("MagicNumber")
private fun AutocompleteButton(
    onAutoComplete: () -> Unit,
    colors: AwesomeBarColors,
    orientation: AwesomeBarOrientation,
    modifier: Modifier,
) {
    Image(
        painterResource(iconsR.drawable.mozac_ic_append_up_left_24),
        colorFilter = null,//ColorFilter.tint(colors.autocompleteIcon),
        contentDescription = stringResource(R.string.mozac_browser_awesomebar_edit_suggestion),
        modifier = modifier
            .size(48.dp)
            .rotate(
                if (orientation == AwesomeBarOrientation.BOTTOM) {
                    270f
                } else {
                    0f
                },
            )
            .clickable { onAutoComplete() }
        .padding(8.dp)
    )
}

@Composable
private fun RemoveButton(
    onRemoveClicked: () -> Unit,
    colors: AwesomeBarColors,
    modifier: Modifier,
) {
    Image(
        painterResource(iconsR.drawable.mozac_ic_cross_24),
        colorFilter = null,//ColorFilter.tint(colors.autocompleteIcon),
        contentDescription = stringResource(R.string.mozac_browser_awesomebar_remove_suggestion),
        modifier = modifier
            .size(48.dp)
            .clickable { onRemoveClicked() }
    )
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.ui.tabcounter

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalGridApi
import androidx.compose.foundation.layout.Grid
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.compose.base.theme.privateColorPalette
import mozilla.components.compose.base.utils.toLocaleString
import mozilla.components.ui.icons.R as iconsR
import mozilla.components.ui.tabcounter.TabCounterTestTags.NORMAL_TABS_COUNTER

private const val MAX_SINGLE_DIGIT = 9
private const val MAX_VISIBLE_TABS = 99
private const val ONE_DIGIT_SIZE_RATIO = 0.5f
private const val TWO_DIGITS_SIZE_RATIO = 0.4f

/**
 * UI for displaying the number of opened tabs.
 *
 * This composable uses LocalContentColor, provided by CompositionLocalProvider, to set the color of its icons and text.
 *
 * @param tabCount The number to be displayed inside the counter.
 * @param showPrivacyBadge Whether the privacy badge is visible.
 * @param showTabCount Whether the tab count is visible.
 * @param contentColor The content color to be used for the text and border of the tab counter.
 */
@Composable
fun TabCounter(
    tabCount: Int,
    showPrivacyBadge: Boolean = false,
    showTabCount: Boolean = true,
    contentColor: Color = MaterialTheme.colorScheme.onSurface,
) {
    val formattedTabCount = remember(tabCount) { tabCount.toLocaleString() }
    val (counterBoxBackground, counterBoxTestTag) =
        when (tabCount > MAX_VISIBLE_TABS) {
            true -> Pair(iconsR.drawable.mozac_ic_infinite_tabs_24, TabCounterTestTags.TAB_COUNTER_INFINITY_ICON)
            false -> Pair(iconsR.drawable.mozac_ic_tab_24, TabCounterTestTags.TAB_COUNTER_ICON)
        }
    val tabsCounterDescription =
        if (showPrivacyBadge) {
            stringResource(R.string.mozac_tab_counter_private, formattedTabCount)
        } else {
            stringResource(R.string.mozac_open_tab_counter_tab_tray, formattedTabCount)
        }

    Box(
        modifier =
            Modifier.semantics(mergeDescendants = false) {
                this.contentDescription = tabsCounterDescription
                testTag = NORMAL_TABS_COUNTER
            },
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            painter = painterResource(id = counterBoxBackground),
            contentDescription = null,
            modifier = Modifier.testTag(counterBoxTestTag),
            tint = contentColor,
        )

        if (tabCount <= MAX_VISIBLE_TABS) {
            TabCounterText(
                tabCount = tabCount,
                formattedTabCount = formattedTabCount,
                showTabCount = showTabCount,
                contentColor = contentColor,
            )
        }

        if (showPrivacyBadge) {
            Image(
                painter = painterResource(id = iconsR.drawable.mozac_ic_private_mode_circle_fill_stroke_20),
                contentDescription = null,
                modifier =
                    Modifier.testTag(TabCounterTestTags.PRIVACY_BADGE)
                        .align(Alignment.TopEnd)
                        .padding(0.dp)
                        .offset(x = 8.dp, y = (-8).dp),
            )
        }
    }
}

@Composable
private fun TabCounterText(
    tabCount: Int,
    formattedTabCount: String,
    showTabCount: Boolean,
    contentColor: Color,
) {
    val normalTabCountText by
        remember(tabCount) {
            derivedStateOf {
                // Showing more than 99 tabs will be done through a different drawable / background
                // so we don't need to show any text.
                when (tabCount > MAX_VISIBLE_TABS) {
                    true -> ""
                    false -> formattedTabCount
                }
            }
        }
    val tabCountTextRatio by
        remember(tabCount) {
            derivedStateOf {
                when (tabCount > MAX_SINGLE_DIGIT) {
                    true -> TWO_DIGITS_SIZE_RATIO
                    false -> ONE_DIGIT_SIZE_RATIO
                }
            }
        }
    val counterBoxWidthDp = dimensionResource(id = R.dimen.mozac_tab_counter_box_width_height)
    val counterBoxWidthPx = LocalDensity.current.run { counterBoxWidthDp.roundToPx() }
    val counterTabsTextSize by
        remember(tabCountTextRatio) {
            mutableIntStateOf((tabCountTextRatio * counterBoxWidthPx).toInt())
        }

    AnimatedVisibility(
        visible = showTabCount,
        enter = fadeIn(animationSpec = tween()),
        exit = fadeOut(animationSpec = tween()),
    ) {
        Text(
            text = normalTabCountText,
            modifier =
                Modifier.clearAndSetSemantics {
                    testTag = TabCounterTestTags.TAB_COUNTER_TEXT + normalTabCountText
                }.padding(top = 1.dp),
            color = contentColor,
            fontSize = with(LocalDensity.current) { counterTabsTextSize.toDp().toSp() },
            fontWeight = FontWeight.W700,
            textAlign = TextAlign.Center,
            lineHeight = 16.sp,
        )
    }
}

/** Test tags for the [TabCounter] composable. */
object TabCounterTestTags {
    private const val TAG = "TabCounterTestTags"
    const val NORMAL_TABS_COUNTER = "$TAG.tabCounter"

    const val TAB_COUNTER_ICON = "$TAG.icon"
    const val TAB_COUNTER_INFINITY_ICON = "$TAG.infinity_icon"

    const val TAB_COUNTER_TEXT = "$TAG.text"

    const val PRIVACY_BADGE = "$TAG.privacy_badge"
}

@OptIn(ExperimentalGridApi::class)
@PreviewLightDark
@Preview(locale = "ar")
@Composable
private fun TabCounterSingleDigitPreview() {
    AcornTheme {
        Surface {
            Box {
                Grid(config = {
                    column(12.dp)
                    column(12.dp)
                    row(12.dp)
                    row(12.dp)
                    gap(0.5.dp)
                }) {
                    Surface(color = Color.Red, modifier = Modifier.fillMaxSize()) { }
                    Surface(color = Color.Red, modifier = Modifier.fillMaxSize()) { }
                    Surface(color = Color.Red, modifier = Modifier.fillMaxSize()) { }
                    Surface(color = Color.Red, modifier = Modifier.fillMaxSize()) { }
                }

                TabCounter(tabCount = 0)
            }
        }
    }
}

@PreviewLightDark
@Preview(locale = "ar")
@Composable
private fun TabCounterDoubleDigitPreview() {
    AcornTheme {
        Surface {
            TabCounter(tabCount = 1)
        }
    }
}

//@Preview
//@Composable
//private fun TabCounterPrivatePreview() {
//    AcornTheme(
//        colors = privateColorPalette,
//        colorScheme = acornPrivateColorScheme(),
//    ) {
//        Surface {
//            TabCounter(tabCount = 55)
//        }
//    }
//}

@OptIn(ExperimentalGridApi::class)
@PreviewLightDark
@Preview(locale = "ar")
@Composable
private fun InfiniteTabCounterPreview() {
    AcornTheme {
        Surface {
            Box {
                Grid(config = {
                    column(12.dp)
                    column(12.dp)
                    row(12.dp)
                    row(12.dp)
                    gap(0.5.dp)
                }) {
                    Surface(color = Color.Red, modifier = Modifier.fillMaxSize()) { }
                    Surface(color = Color.Red, modifier = Modifier.fillMaxSize()) { }
                    Surface(color = Color.Red, modifier = Modifier.fillMaxSize()) { }
                    Surface(color = Color.Red, modifier = Modifier.fillMaxSize()) { }
                }

                TabCounter(tabCount = 100)
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun HiddenTabCounterPreview() {
    AcornTheme {
        Surface {
            TabCounter(
                tabCount = 55,
                showTabCount = false,
            )
        }
    }
}

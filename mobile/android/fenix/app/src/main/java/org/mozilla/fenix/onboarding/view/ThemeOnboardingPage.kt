/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.BoxWithConstraintsScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.times
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.utils.inComposePreview
import org.mozilla.fenix.R
import org.mozilla.fenix.onboarding.store.OnboardingStore
import org.mozilla.fenix.onboarding.store.applyThemeIfRequired
import org.mozilla.fenix.theme.FirefoxTheme
import mozilla.components.ui.icons.R as iconsR

/**
 * The default ratio of the image height to the parent height.
 */
private const val OPTION_IMAGE_HEIGHT_RATIO_DEFAULT = 1.0f

/**
 * The ratio of the image height to the parent height for medium sized devices.
 */
private const val OPTION_IMAGE_HEIGHT_RATIO_MEDIUM = 0.7f

/**
 * The ratio of the image height to the parent height for small devices like Nexus 4, Nexus 1.
 */
private const val OPTION_IMAGE_HEIGHT_RATIO_SMALL = 0.4f

/**
 * The default ratio of the image height to the parent height.
 */
private const val IMAGE_HEIGHT_RATIO_DEFAULT = 0.4f

/**
 * The ratio of the image height to the parent height for medium sized devices.
 */
private const val IMAGE_HEIGHT_RATIO_MEDIUM = 0.3f

/**
 * The ratio of the image height to the parent height for small devices.
 */
private const val IMAGE_HEIGHT_RATIO_SMALL = 0.2f

/**
 * This width ensures we can fit the "system auto" option and have consistent spacing.
 */
private val columnWidth = 72.dp

/**
 * This is the width of the 'select theme' image resources.
 */
private val imageResourceWidth = 60.dp

/**
 * A Composable for displaying theme selection onboarding page content.
 *
 * @param onboardingStore The [OnboardingStore] that holds the theme selection state.
 * @param pageState The page content that's displayed.
 * @param onThemeSelectionClicked Callback for when a theme selection is clicked.
 */
@Suppress("LongMethod")
@Composable
fun ThemeOnboardingPage(
    onboardingStore: OnboardingStore,
    pageState: OnboardingPageState,
    onThemeSelectionClicked: (ThemeOptionType) -> Unit,
) {
    Surface {
        BoxWithConstraints {
            val boxWithConstraintsScope = this
            // Base
            Column(
                modifier = Modifier
                    .padding(horizontal = 16.dp, vertical = 24.dp)
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.SpaceBetween,
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                with(pageState) {
                    // Main content group
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Spacer(Modifier)

                        Image(
                            painter = painterResource(id = imageRes),
                            contentDescription = stringResource(
                                R.string.onboarding_customize_theme_main_image_content_description,
                            ),
                            modifier = Modifier.height(mainImageHeight(boxWithConstraintsScope)),
                        )

                        Spacer(Modifier.height(32.dp))

                        Text(
                            text = title,
                            textAlign = TextAlign.Center,
                            style = FirefoxTheme.typography.headline5,
                        )

                        Spacer(Modifier.height(16.dp))

                        Text(
                            text = description,
                            textAlign = TextAlign.Center,
                            style = FirefoxTheme.typography.body2,
                        )

                        Spacer(Modifier.height(32.dp))

                        val state by onboardingStore.stateFlow.collectAsState()

                        if (!inComposePreview) {
                            LaunchedEffect(onboardingStore.state.themeOptionSelected) {
                                applyThemeIfRequired(onboardingStore.state.themeOptionSelected)
                            }
                        }

                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) {
                            themeOptions?.let {
                                ThemeOptions(
                                    boxWithConstraintsScope = boxWithConstraintsScope,
                                    options = it,
                                    selectedOption = state.themeOptionSelected,
                                    onClick = onThemeSelectionClicked,
                                )
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(16.dp))

                    FilledButton(
                        text = primaryButton.text,
                        modifier = Modifier
                            .width(width = FirefoxTheme.layout.size.maxWidth.small)
                            .semantics { testTag = title + "onboarding_card.positive_button" },
                        onClick = { primaryButton.onClick() },
                    )
                }
            }

            LaunchedEffect(pageState) {
                pageState.onRecordImpressionEvent()
            }
        }
    }
}

@Composable
private fun ThemeOptions(
    boxWithConstraintsScope: BoxWithConstraintsScope,
    options: List<ThemeOption>,
    selectedOption: ThemeOptionType,
    onClick: (ThemeOptionType) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Center,
    ) {
        options.forEach {
            SelectableImageItem(
                boxWithConstraintsScope = boxWithConstraintsScope,
                themeOption = it,
                selectedOption = selectedOption,
                onClick = onClick,
            )

            if (it != options.last()) {
                Spacer(Modifier.width(14.dp))
            }
        }
    }
}

@Composable
private fun SelectableImageItem(
    boxWithConstraintsScope: BoxWithConstraintsScope,
    themeOption: ThemeOption,
    selectedOption: ThemeOptionType,
    onClick: (ThemeOptionType) -> Unit,
) {
    var isMultilineLabel by remember { mutableStateOf(false) }
    val isSelectedOption = themeOption.themeType == selectedOption

    Column(
        modifier = Modifier
            .width(if (isMultilineLabel) LocalDensity.current.fontScale * columnWidth else columnWidth)
            .clickable(
                onClickLabel = stringResource(R.string.onboarding_customize_theme_a11y_action_label_select),
                onClick = {
                    if (!isSelectedOption) {
                        onClick(themeOption.themeType)
                    }
                },
            )
            .semantics {
                selected = isSelectedOption
                role = Role.RadioButton
            },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Image(
            painter = painterResource(id = themeOption.imageRes),
            contentDescription = stringResource(
                R.string.onboarding_customize_theme_content_description,
                themeOption.label,
            ),
            modifier = if (isSelectedOption) {
                val borderModifier = borderSize(boxWithConstraintsScope)
                Modifier
                    .width(optionImageWidth(boxWithConstraintsScope))
                    .border(
                        2.dp,
                        MaterialTheme.colorScheme.primary,
                        RoundedCornerShape(borderModifier),
                    )
            } else {
                Modifier.width(optionImageWidth(boxWithConstraintsScope))
            },
        )

        Text(
            text = themeOption.label,
            onTextLayout = {
                if (!isMultilineLabel) {
                    isMultilineLabel = it.lineCount > 1
                }
            },
            modifier = Modifier.padding(vertical = 6.dp),
            style = FirefoxTheme.typography.caption,
        )

        if (isSelectedOption) {
            Box(
                modifier = Modifier
                    .background(
                        color = MaterialTheme.colorScheme.primary,
                        shape = CircleShape,
                    )
                    .size(24.dp),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_checkmark_24),
                    contentDescription = null, // decorative only.
                    modifier = Modifier.size(12.dp),
                    tint = MaterialTheme.colorScheme.onPrimary,
                )
            }
        } else {
            Box(
                modifier = Modifier
                    .size(24.dp)
                    .border(2.dp, MaterialTheme.colorScheme.onSurfaceVariant, CircleShape),
                contentAlignment = Alignment.Center,
            ) {
            }
        }
    }
}

private fun mainImageHeight(boxWithConstraintsScope: BoxWithConstraintsScope): Dp {
    val imageHeightRatio: Float = when {
        boxWithConstraintsScope.maxHeight <= ONBOARDING_SMALL_DEVICE -> IMAGE_HEIGHT_RATIO_SMALL
        boxWithConstraintsScope.maxHeight <= ONBOARDING_MEDIUM_DEVICE -> IMAGE_HEIGHT_RATIO_MEDIUM
        else -> IMAGE_HEIGHT_RATIO_DEFAULT
    }
    return boxWithConstraintsScope.maxHeight.times(imageHeightRatio)
}

private fun borderSize(boxWithConstraintsScope: BoxWithConstraintsScope) = when {
    boxWithConstraintsScope.maxHeight <= ONBOARDING_SMALL_DEVICE -> 4.dp
    boxWithConstraintsScope.maxHeight <= ONBOARDING_MEDIUM_DEVICE -> 7.dp
    else -> 10.dp
}

private fun optionImageWidth(boxWithConstraintsScope: BoxWithConstraintsScope): Dp {
    val imageHeightRatio: Float = when {
        boxWithConstraintsScope.maxHeight <= ONBOARDING_SMALL_DEVICE -> OPTION_IMAGE_HEIGHT_RATIO_SMALL
        boxWithConstraintsScope.maxHeight <= ONBOARDING_MEDIUM_DEVICE -> OPTION_IMAGE_HEIGHT_RATIO_MEDIUM
        else -> OPTION_IMAGE_HEIGHT_RATIO_DEFAULT
    }
    return imageResourceWidth.times(imageHeightRatio)
}

// *** Code below used for previews only *** //

@FlexibleWindowLightDarkPreview
@Composable
private fun OnboardingPagePreview() {
    FirefoxTheme {
        ThemeOnboardingPage(
            onboardingStore = OnboardingStore(),
            pageState = OnboardingPageState(
                imageRes = R.drawable.ic_pick_a_theme,
                title = stringResource(id = R.string.onboarding_customize_theme_title),
                description = stringResource(id = R.string.onboarding_customize_theme_description),
                primaryButton = Action(
                    text = stringResource(
                        id = R.string.onboarding_save_and_continue_button,
                    ),
                    onClick = {},
                ),
                themeOptions = listOf(
                    ThemeOption(
                        themeType = ThemeOptionType.THEME_DARK,
                        imageRes = R.drawable.ic_pick_a_theme_dark,
                        label = stringResource(R.string.onboarding_customize_theme_dark_option),
                    ),
                    ThemeOption(
                        themeType = ThemeOptionType.THEME_LIGHT,
                        imageRes = R.drawable.ic_pick_a_theme_light,
                        label = stringResource(R.string.onboarding_customize_theme_light_option),
                    ),
                    ThemeOption(
                        themeType = ThemeOptionType.THEME_SYSTEM,
                        imageRes = R.drawable.ic_pick_a_theme_system_auto,
                        label = stringResource(R.string.onboarding_customize_theme_system_option),
                    ),
                ),
                onRecordImpressionEvent = {},
            ),
            onThemeSelectionClicked = {},
        )
    }
}

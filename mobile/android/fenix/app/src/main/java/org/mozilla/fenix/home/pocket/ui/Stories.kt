/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket.ui

import android.graphics.Rect
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt
import mozilla.components.compose.base.modifier.onShown
import mozilla.components.compose.base.utils.inComposePreview
import mozilla.components.service.pocket.PocketStory
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import mozilla.components.service.pocket.PocketStory.PocketRecommendedStory
import mozilla.components.service.pocket.PocketStory.PocketSponsoredStory
import mozilla.components.service.pocket.PocketStory.SponsoredContent
import org.mozilla.fenix.R
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.IMAGE_SIZE
import org.mozilla.fenix.compose.ListItemTabSurface
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE_SPONSORED_STORY
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE_STORY
import org.mozilla.fenix.home.ui.HomepageTestTag.POCKET_STORIES
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.wallpapers.WallpaperTheme

private const val DEFAULT_MAX_LINES = 2
private const val ACCESSIBILITY_MAX_LINES_SCALE_FACTOR = 1.2f

/**
 * Displays a single [SponsoredContent].
 *
 * @param sponsoredContent The [SponsoredContent] to be displayed.
 * @param backgroundColor The background [Color] of the sponsored content.
 * @param onClick Callback for when the user taps on the sponsored content.
 */
@Composable
fun SponsoredContentStory(
    sponsoredContent: SponsoredContent,
    backgroundColor: Color,
    onClick: (SponsoredContent) -> Unit,
) {
    ListItemTabSurface(
        imageUrl = sponsoredContent.imageUrl,
        backgroundColor = backgroundColor,
        onClick = { onClick(sponsoredContent) },
    ) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.SpaceEvenly,
        ) {
            Text(
                text = sponsoredContent.title,
                modifier =
                    Modifier.semantics {
                        testTagsAsResourceId = true
                        testTag = "pocket.sponsoredContent.title"
                    },
                overflow = TextOverflow.Ellipsis,
                maxLines = maxLines(),
                style = FirefoxTheme.typography.body2,
            )

            Text(
                text = stringResource(R.string.pocket_stories_sponsor_indication),
                modifier =
                    Modifier.semantics {
                        testTagsAsResourceId = true
                        testTag = "pocket.sponsoredContent.identifier"
                    },
                overflow = TextOverflow.Ellipsis,
                maxLines = 1,
                style = FirefoxTheme.typography.caption,
            )
        }
    }
}

/**
 * Displays a single [ContentRecommendation].
 *
 * @param recommendation The [ContentRecommendation] to be displayed.
 * @param backgroundColor The background [Color] of the recommendation.
 * @param onClick Callback for when the user taps on the recommendation.
 */
@Composable
fun ContentRecommendationStory(
    recommendation: ContentRecommendation,
    backgroundColor: Color,
    onClick: (ContentRecommendation) -> Unit,
) {
    val imageUrl =
        recommendation.imageUrl.replace(
            "{wh}",
            with(LocalDensity.current) {
                "${IMAGE_SIZE.dp.toPx().roundToInt()}x${
                IMAGE_SIZE.dp.toPx().roundToInt()
            }"
            },
        )

    ListItemTabSurface(
        imageUrl = imageUrl,
        backgroundColor = backgroundColor,
        onClick = { onClick(recommendation) },
    ) {
        Text(
            text = recommendation.title,
            modifier =
                Modifier.semantics {
                    testTagsAsResourceId = true
                    testTag = "pocket.contentRecommendation.title"
                },
            overflow = TextOverflow.Ellipsis,
            maxLines = maxLines(),
            style = FirefoxTheme.typography.body2,
        )

        Text(
            text = recommendation.publisher,
            overflow = TextOverflow.Ellipsis,
            maxLines = 1,
            style = FirefoxTheme.typography.caption,
        )
    }
}

/**
 * Displays a list of [PocketStory]s.
 *
 * @param stories The list of [PocketStory]s to be displayed.
 * @param contentPadding Dimension for padding the content after it has been clipped. This space will be used for
 *   shadows and also content rendering when the list is scrolled.
 * @param backgroundColor The background [Color] of each story.
 * @param onStoryShown Callback for when a certain story is visible to the user.
 * @param onStoryClicked Callback for when the user taps on a recommended story.
 */
@Suppress("CyclomaticComplexMethod", "LongMethod", "CognitiveComplexMethod")
@Composable
fun Stories(
    @PreviewParameter(StoryProvider::class) stories: List<PocketStory>,
    contentPadding: Dp,
    backgroundColor: Color = WallpaperTheme.cardBackgroundColor,
    onStoryShown: (PocketStory, Triple<Int, Int, Int>) -> Unit,
    onStoryClicked: (PocketStory, Triple<Int, Int, Int>) -> Unit,
) {
    Row(
        modifier =
            Modifier.height(intrinsicSize = IntrinsicSize.Max)
                .horizontalScroll(rememberScrollState())
                .padding(start = contentPadding, end = contentPadding)
                .semantics {
                    testTagsAsResourceId = true
                    testTag = POCKET_STORIES
                },
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        stories.forEachIndexed { columnIndex, story ->
            val rowIndex = 0
            Box(
                modifier =
                    Modifier.fillMaxHeight().semantics {
                        testTagsAsResourceId = true
                        testTag =
                            when (story) {
                                is PocketRecommendedStory,
                                is ContentRecommendation -> HOMEPAGE_STORY

                                else -> HOMEPAGE_SPONSORED_STORY
                            }
                    }
            ) {
                when (story) {
                    is PocketRecommendedStory,
                    is PocketSponsoredStory -> {
                        // no-op, don't handle these [PocketStory] types as they are no longer
                        // supported.
                    }

                    is ContentRecommendation -> {
                        ContentRecommendationStory(
                            recommendation = story,
                            backgroundColor = backgroundColor,
                        ) {
                            onStoryClicked(
                                story,
                                Triple(rowIndex, columnIndex, stories.indexOf(story)),
                            )
                        }
                    }

                    is SponsoredContent -> {
                        val screenBounds =
                            Rect()
                                .apply { LocalView.current.getWindowVisibleDisplayFrame(this) }
                                .apply {
                                    // Check if this is in a preview because `settings()` breaks previews
                                    if (!inComposePreview) {
                                        val verticalOffset =
                                            with(LocalDensity.current) {
                                                dimensionResource(id = R.dimen.browser_toolbar_height).roundToPx()
                                            }

                                        if (components.settings.shouldUseBottomToolbar) {
                                            bottom -= verticalOffset
                                        } else {
                                            top += verticalOffset
                                        }
                                    }
                                }

                        Box(
                            modifier =
                                Modifier.onShown(
                                    threshold = 0.5f,
                                    onVisible = {
                                        onStoryShown(
                                            story,
                                            Triple(
                                                rowIndex,
                                                columnIndex,
                                                stories.indexOf(story),
                                            ),
                                        )
                                    },
                                    screenBounds = screenBounds,
                                )
                        ) {
                            SponsoredContentStory(
                                sponsoredContent = story,
                                backgroundColor = backgroundColor,
                            ) {
                                onStoryClicked(
                                    story,
                                    Triple(rowIndex, columnIndex, stories.indexOf(story)),
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
@PreviewLightDark
private fun StoriesPreview() {
    FirefoxTheme {
        Surface {
            Stories(
                stories =
                    listOf(
                        FakeHomepagePreview.contentRecommendation(),
                        FakeHomepagePreview.sponsoredContent(),
                        FakeHomepagePreview.contentRecommendation(),
                        FakeHomepagePreview.sponsoredContent(),
                    ),
                contentPadding = 0.dp,
                onStoryShown = { _, _ -> },
                onStoryClicked = { _, _ -> },
            )
        }
    }
}

@Composable @ReadOnlyComposable private fun maxLines() = if (limitMaxLines()) DEFAULT_MAX_LINES else Int.MAX_VALUE

@Composable
@ReadOnlyComposable
private fun limitMaxLines() = LocalConfiguration.current.fontScale <= ACCESSIBILITY_MAX_LINES_SCALE_FACTOR

private class StoryProvider : PreviewParameterProvider<PocketStory> {
    override val values = FakeHomepagePreview.stories(limit = 7).asSequence()
    override val count = 8
}

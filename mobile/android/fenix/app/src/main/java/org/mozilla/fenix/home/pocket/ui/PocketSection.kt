/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.home.HomeSectionHeader
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.pocket.PocketState
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource
import org.mozilla.fenix.home.pocket.interactor.PocketStoriesInteractor
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.wallpapers.WallpaperState

/**
 * Pocket section for the homepage.
 *
 * @param state The [PocketState] representing the UI state.
 * @param cardBackgroundColor The [Color] of the card backgrounds.
 * @param interactor [PocketStoriesInteractor] for interactions with the UI.
 * @param horizontalPadding Horizontal padding to apply to outermost column.
 */
@Composable
fun PocketSection(
    state: PocketState,
    cardBackgroundColor: Color,
    interactor: PocketStoriesInteractor,
    horizontalPadding: Dp = dimensionResource(R.dimen.home_item_horizontal_margin),
) {
    LaunchedEffect(state.stories) {
        // We should report back when a certain story is actually being displayed.
        // Cannot do it reliably so for now we'll just mass report everything as being displayed.
        state.stories.let {
            interactor.onStoriesShown(
                storiesShown = it,
                source = StoriesImpressionSource.HOMEPAGE,
            )
        }
    }

    Column {
        HomeSectionHeader(
            headerText = stringResource(R.string.pocket_stories_header_2),
            modifier = Modifier.padding(horizontal = horizontalPadding),
            description = stringResource(R.string.stories_discover_more_content_description),
            buttonText = stringResource(R.string.homepage_all_stories),
            onButtonClick = interactor::onDiscoverMoreClicked,
        )

        Spacer(Modifier.height(16.dp))

        Stories(
            stories = state.stories,
            contentPadding = horizontalPadding,
            backgroundColor = cardBackgroundColor,
            onStoryShown = interactor::onStoryShown,
            onStoryClicked = { story, position ->
                interactor.onStoryClicked(story, position, StoriesImpressionSource.HOMEPAGE)
            },
        )
    }
}

@Preview
@Composable
private fun PocketSectionPreview() {
    FirefoxTheme {
        Surface {
            PocketSection(
                state = FakeHomepagePreview.pocketState(),
                cardBackgroundColor = WallpaperState.default.cardBackgroundColor,
                interactor = FakeHomepagePreview.homepageInteractor,
                horizontalPadding = 0.dp,
            )
        }
    }
}

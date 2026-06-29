/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import mozilla.components.compose.base.utils.BackInvokedHandler
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.recommendations.ContentRecommendationsState
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource
import org.mozilla.fenix.home.pocket.interactor.PocketStoriesInteractor
import org.mozilla.fenix.home.ui.LeftChevronPillButton
import org.mozilla.fenix.theme.FirefoxTheme
import mozilla.components.ui.icons.R as iconsR

/**
 * Stories screen.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StoriesScreen(
    state: ContentRecommendationsState,
    entryPointExperimentEnabled: Boolean,
    interactor: PocketStoriesInteractor,
    onNavigationIconClick: () -> Unit,
) {
    LaunchedEffect(Unit) {
        interactor.onDiscoverMoreScreenViewed()
    }

    // We should report back when a certain story is actually being displayed.
    // Cannot do it reliably so for now we'll just mass report everything as being displayed.
    LaunchedEffect(state.pocketStories) {
        interactor.onStoriesShown(
            storiesShown = state.pocketStories,
            source = StoriesImpressionSource.STORIES_SCREEN,
        )
    }

    BackInvokedHandler {
        onNavigationIconClick()
    }

    val scrollBehavior = TopAppBarDefaults.enterAlwaysScrollBehavior()

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.stories_screen_text_news),
                        style = FirefoxTheme.typography.headline5,
                    )
                },
                navigationIcon = {
                    if (entryPointExperimentEnabled) {
                        HomeButton(
                            onClick = onNavigationIconClick,
                        )
                    } else {
                        IconButton(
                            onClick = onNavigationIconClick,
                            contentDescription = stringResource(R.string.stories_back_button_content_description),
                        ) {
                            Icon(
                                painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                                contentDescription = null,
                            )
                        }
                    }
                },
                windowInsets = WindowInsets(
                    top = 0.dp,
                    bottom = 0.dp,
                ),
                scrollBehavior = scrollBehavior,
            )
        },
        modifier = Modifier
            .fillMaxSize()
            .nestedScroll(scrollBehavior.nestedScrollConnection),
    ) { paddingValues ->
        StoriesScreenContent(
            state = state,
            paddingValues = paddingValues,
            interactor = interactor,
        )
    }
}

@Composable
private fun StoriesScreenContent(
    state: ContentRecommendationsState,
    paddingValues: PaddingValues,
    interactor: PocketStoriesInteractor,
) {
    Column(
        modifier = Modifier.padding(
            top = paddingValues.calculateTopPadding(),
            start = FirefoxTheme.layout.space.dynamic200,
            end = FirefoxTheme.layout.space.dynamic200,
        ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Stories(
            state = state,
            interactor = interactor,
        )
    }
}

@Composable
private fun Stories(
    state: ContentRecommendationsState,
    interactor: PocketStoriesInteractor,
) {
    val windowSizeClass = FirefoxTheme.windowSize
    val columnCount = when (windowSizeClass) {
        AcornWindowSize.Small -> 1
        AcornWindowSize.Medium -> 2
        AcornWindowSize.Large -> 3
    }

    val verticalPadding = if (windowSizeClass != AcornWindowSize.Small) {
        16.dp
    } else {
        12.dp
    }

    LazyVerticalGrid(
        columns = GridCells.Fixed(columnCount),
        verticalArrangement = Arrangement.spacedBy(verticalPadding),
        horizontalArrangement = Arrangement.spacedBy(
            16.dp,
            alignment = Alignment.CenterHorizontally,
        ),
    ) {
        itemsIndexed(state.pocketStories) { index, story ->
            StoryCard(
                story = story,
                onClick = { clickedStory, position ->
                    interactor.onStoryClicked(
                        clickedStory,
                        position,
                        StoriesImpressionSource.STORIES_SCREEN,
                    )
                },
            )
        }
    }
}

@Composable
private fun HomeButton(onClick: () -> Unit) {
    LeftChevronPillButton(onClick = onClick) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_home_24),
            contentDescription = stringResource(R.string.content_description_normal_browsing),
        )
    }
}

@Composable
@FlexibleWindowLightDarkPreview
private fun StoriesScreenPreviews() {
    FirefoxTheme {
        StoriesScreen(
            state = ContentRecommendationsState(
                pocketStories = FakeHomepagePreview.stories(),
            ),
            interactor = FakeHomepagePreview.storiesInteractor,
            entryPointExperimentEnabled = false,
            onNavigationIconClick = {},
        )
    }
}

@Composable
@FlexibleWindowLightDarkPreview
private fun StoriesScreenExperimentPreviews() {
    FirefoxTheme {
        StoriesScreen(
            state = ContentRecommendationsState(
                pocketStories = FakeHomepagePreview.stories(),
            ),
            interactor = FakeHomepagePreview.storiesInteractor,
            entryPointExperimentEnabled = true,
            onNavigationIconClick = {},
        )
    }
}

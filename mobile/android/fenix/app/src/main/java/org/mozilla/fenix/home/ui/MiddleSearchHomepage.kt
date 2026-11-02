/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.home.interactor.HomepageInteractor
import org.mozilla.fenix.home.store.HeaderState
import org.mozilla.fenix.home.store.HomepageState
import org.mozilla.fenix.home.store.MiddleSearchState
import org.mozilla.fenix.home.toolbar.HomeToolbarComposable
import org.mozilla.fenix.home.topsites.TopSiteColors
import org.mozilla.fenix.home.topsites.TopSiteState
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Top level composable for the middle search homepage.
 *
 * @param state State representing the homepage.
 * @param interactor [HomepageInteractor] for interactions with the homepage UI.
 * @param onMiddleSearchBarVisibilityChanged Invoked when the middle search is shown/hidden.
 * @param onTopSitesItemBound Invoked during the composition of a top site item.
 * @param onAddShortcutClicked Invoked when the user clicks on the "Add shortcut" tile.
 * @param navigationBarContent Optional composable rendered at the bottom of the homepage when the toolbar is positioned
 *   at the top. When the toolbar is at the bottom, the navigation bar is rendered by [HomeToolbarComposable] instead
 *   and this content is not shown.
 */
@Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
@Composable
internal fun MiddleSearchHomepage(
    state: HomepageState,
    interactor: HomepageInteractor,
    onMiddleSearchBarVisibilityChanged: (isVisible: Boolean) -> Unit,
    onTopSitesItemBound: () -> Unit,
    onAddShortcutClicked: () -> Unit,
    navigationBarContent: (@Composable () -> Unit)? = null,
) {
    val scrollState = rememberScrollState()

    BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier =
                Modifier.fillMaxSize()
                    .semantics {
                        testTagsAsResourceId = true
                        testTag = HOMEPAGE
                    }
                    .pointerInput(state.isSearchInProgress) {
                        if (state.isSearchInProgress) {
                            awaitEachGesture {
                                awaitFirstDown(false, PointerEventPass.Initial)
                                interactor.onHomeContentFocusedWhileSearchIsActive()
                            }
                        }
                    }
                    .verticalScroll(scrollState),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Spacer(modifier = Modifier.height(16.dp))

            if (state.firstFrameDrawn) {
                with(state) {
                    when (this) {
                        is HomepageState.Private -> {
                            LaunchedEffect(key1 = state) {
                                onMiddleSearchBarVisibilityChanged(false)
                            }

                            Box(modifier = Modifier.padding(horizontal = horizontalMargin)) {
                                PrivateBrowsingDescription(onLearnMoreClick = interactor::onLearnMoreClicked)
                            }
                        }

                        is HomepageState.Normal -> {
                            if (topSiteState != null) {
                                TopSitesSection(
                                    state = topSiteState,
                                    interactor = interactor,
                                    onTopSitesItemBound = onTopSitesItemBound,
                                    onAddShortcutClicked = onAddShortcutClicked,
                                )
                            }

                            Spacer(modifier = Modifier.weight(1f))

                            LaunchedEffect(key1 = middleSearchState) {
                                onMiddleSearchBarVisibilityChanged(middleSearchState.isShown)
                            }

                            if (middleSearchState.isShown) {
                                SearchBar(
                                    modifier =
                                        Modifier.padding(horizontal = horizontalMargin).graphicsLayer {
                                            this.alpha = alpha
                                        },
                                    onClick = interactor::onNavigateSearch,
                                )
                            }

                            Spacer(modifier = Modifier.weight(1f))
                        }
                    }
                }
            }
        }

        if (state.isSearchInProgress) {
            Scrim(onDismiss = interactor::onHomeContentFocusedWhileSearchIsActive)
        }

        if (navigationBarContent != null && components.settings.toolbarPosition == ToolbarPosition.TOP) {
            Box(modifier = Modifier.align(Alignment.BottomCenter)) {
                navigationBarContent()
            }
        }
    }
}

@Composable
private fun Scrim(onDismiss: () -> Unit) {
    Box(
        modifier =
            Modifier.background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.75f)).fillMaxSize().pointerInput(Unit) {
                detectTapGestures(onTap = { onDismiss() })
            }
    )
}

@Composable
@PreviewLightDark
private fun MiddleSearchHomepagePreview() {
    FirefoxTheme {
        MiddleSearchHomepage(
            HomepageState.Normal(
                shouldShowPrivacyNoticeBanner = false,
                nimbusMessage = null,
                topSiteState =
                    TopSiteState(
                        topSites = FakeHomepagePreview.topSites(),
                        colors = TopSiteColors.colors(),
                    ),
                recentlyVisited = FakeHomepagePreview.recentHistory(),
                collectionsState = FakeHomepagePreview.collectionState(),
                showPrivacyReport = true,
                longfoxEnabled = true,
                showLongfoxAnimation = true,
                trackersBlockedCount = 754,
                headerState = HeaderState(showStoriesButton = true),
                middleSearchState = MiddleSearchState(searchBarVisible = true, searchBarEnabled = true),
                firstFrameDrawn = true,
                setupChecklistState = null,
                isSearchInProgress = false,
                bottomPadding = 68,
                showTopSitesHeader = true,
            ),
            interactor = FakeHomepagePreview.homepageInteractor,
            onTopSitesItemBound = {},
            onAddShortcutClicked = {},
            onMiddleSearchBarVisibilityChanged = {},
        )
    }
}

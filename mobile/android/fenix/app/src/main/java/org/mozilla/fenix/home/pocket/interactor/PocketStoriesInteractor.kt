/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.pocket.interactor

import mozilla.components.service.pocket.PocketStory
import mozilla.components.service.pocket.PocketStory.PocketRecommendedStory
import org.mozilla.fenix.home.pocket.PocketRecommendedStoriesCategory
import org.mozilla.fenix.home.pocket.controller.PocketStoriesController
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource

/**
 * Contract for all possible user interactions with the Pocket recommended stories feature.
 */
interface PocketStoriesInteractor {
    /**
     * Callback for when a certain story is shown to the user.
     *
     * @param storyShown The story shown to the user.
     * @param storyPosition `row x column x index` matrix representing the grid and index position
     * of the shown story.
     */
    fun onStoryShown(storyShown: PocketStory, storyPosition: Triple<Int, Int, Int>)

    /**
     * Callback for then new stories are shown to the user.
     *
     * @param storiesShown The new list of [PocketRecommendedStory]es shown to the user.
     * @param source The surface where the stories were shown.
     */
    fun onStoriesShown(storiesShown: List<PocketStory>, source: StoriesImpressionSource)

    /**
     * Callback for when the user clicks a specific category.
     *
     * @param categoryClicked The just clicked [PocketRecommendedStoriesCategory].
     */
    fun onCategoryClicked(categoryClicked: PocketRecommendedStoriesCategory)

    /**
     * Callback for when the user clicks on a specific story.
     *
     * @param storyClicked The just clicked [PocketStory].
     * @param storyPosition `row x column x index` matrix representing the grid and index position
     * of the clicked story.
     * @param source The surface where the clicked story was shown.
     */
    fun onStoryClicked(
        storyClicked: PocketStory,
        storyPosition: Triple<Int, Int, Int>,
        source: StoriesImpressionSource,
    )

    /**
     * Callback when an user clicks on the "Discover more" nutton for stories on the homepage.
     */
    fun onDiscoverMoreClicked()

    /**
     * Sends telemetry related to the "Discover more" screen being viewed.
     */
    fun onDiscoverMoreScreenViewed()
}

/**
 * Default implementation of [PocketStoriesInteractor].
 */
class DefaultPocketStoriesInteractor(
    private val controller: PocketStoriesController,
) : PocketStoriesInteractor {
    override fun onStoryShown(storyShown: PocketStory, storyPosition: Triple<Int, Int, Int>) {
        controller.handleStoryShown(storyShown, storyPosition)
    }

    override fun onStoriesShown(storiesShown: List<PocketStory>, source: StoriesImpressionSource) {
        controller.handleStoriesShown(storiesShown, source)
    }

    override fun onCategoryClicked(categoryClicked: PocketRecommendedStoriesCategory) {
        controller.handleCategoryClick(categoryClicked)
    }

    override fun onStoryClicked(
        storyClicked: PocketStory,
        storyPosition: Triple<Int, Int, Int>,
        source: StoriesImpressionSource,
    ) {
        controller.handleStoryClicked(storyClicked, storyPosition, source)
    }

    override fun onDiscoverMoreClicked() {
        controller.handleDiscoverMoreClicked()
    }

    override fun onDiscoverMoreScreenViewed() {
        controller.handleDiscoverMoreScreenViewed()
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.middleware

import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.service.pocket.PocketStory
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.HomeContentArticle
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.TestUtils
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.pocket.PocketImpression
import org.mozilla.fenix.home.pocket.controller.StoriesImpressionSource
import kotlin.test.assertNotNull

@RunWith(AndroidJUnit4::class)
class HomeTelemetryMiddlewareTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `WHEN a recommendation is clicked THEN record the click telemetry`() {
        val store = createStore()
        val recommendation = TestUtils.getFakeContentRecommendations(limit = 1).first()
        val position = 100

        assertNull(HomeContentArticle.click.testGetValue())

        var pingReceived = false
        val job = Pings.home.testBeforeNextSubmit {
            assertNotNull(HomeContentArticle.click.testGetValue())

            val snapshot = HomeContentArticle.click.testGetValue()!!
            assertEquals(1, snapshot.size)

            val extraValues = snapshot.first().extra!!
            assertEquals(recommendation.corpusItemId, extraValues["corpus_item_id"])
            assertEquals(
                recommendation.scheduledCorpusItemId,
                extraValues["scheduled_corpus_item_id"],
            )
            assertEquals(recommendation.tileId.toString(), extraValues["tile_id"])
            assertEquals(recommendation.recommendedAt.toString(), extraValues["recommended_at"])
            assertEquals(recommendation.receivedRank.toString(), extraValues["received_rank"])
            assertEquals(recommendation.topic, extraValues["topic"])
            assertEquals(position.toString(), extraValues["position"])
            assertEquals("false", extraValues["is_sponsored"])
            assertEquals(StoriesImpressionSource.HOMEPAGE.sourceName, extraValues["source"])

            pingReceived = true
        }

        store.dispatch(
            ContentRecommendationsAction.ContentRecommendationClicked(
                recommendation = recommendation,
                position = position,
                source = StoriesImpressionSource.HOMEPAGE,
            ),
        )

        job.join()
        assertTrue(pingReceived)
    }

    @Test
    fun `WHEN a recommendation is clicked on the stories screen THEN record the click telemetry with the stories screen source`() {
        val store = createStore()
        val recommendation = TestUtils.getFakeContentRecommendations(limit = 1).first()
        val position = 100

        assertNull(HomeContentArticle.click.testGetValue())

        var pingReceived = false
        val job = Pings.home.testBeforeNextSubmit {
            assertNotNull(HomeContentArticle.click.testGetValue())

            val snapshot = HomeContentArticle.click.testGetValue()!!
            assertEquals(1, snapshot.size)
            assertEquals("stories_screen", snapshot.first().extra!!["source"])

            pingReceived = true
        }

        store.dispatch(
            ContentRecommendationsAction.ContentRecommendationClicked(
                recommendation = recommendation,
                position = position,
                source = StoriesImpressionSource.STORIES_SCREEN,
            ),
        )

        job.join()
        assertTrue(pingReceived)
    }

    @Test
    fun `WHEN a list of recommendations are shown THEN record the impression telemetry`() {
        val store = createStore()
        val impressions = TestUtils.getFakeContentRecommendations(limit = 3)
            .mapIndexed { index, contentRecommendation ->
                PocketImpression(
                    story = contentRecommendation,
                    position = index,
                )
            }

        assertNull(HomeContentArticle.impression.testGetValue())

        var pingReceived = false
        val job = Pings.home.testBeforeNextSubmit {
            assertNotNull(HomeContentArticle.impression.testGetValue())

            val snapshot = HomeContentArticle.impression.testGetValue()!!
            assertEquals(3, snapshot.size)

            for ((story, position) in impressions) {
                val recommendation = story as PocketStory.ContentRecommendation
                val extraValues = snapshot[position].extra!!
                assertEquals(recommendation.corpusItemId, extraValues["corpus_item_id"])
                assertEquals(
                    recommendation.scheduledCorpusItemId,
                    extraValues["scheduled_corpus_item_id"],
                )
                assertEquals(recommendation.tileId.toString(), extraValues["tile_id"])
                assertEquals(recommendation.recommendedAt.toString(), extraValues["recommended_at"])
                assertEquals(recommendation.receivedRank.toString(), extraValues["received_rank"])
                assertEquals(recommendation.topic, extraValues["topic"])
                assertEquals(position.toString(), extraValues["position"])
                assertEquals("false", extraValues["is_sponsored"])
                assertEquals("homepage", extraValues["source"])
            }

            pingReceived = true
        }

        store.dispatch(
            ContentRecommendationsAction.PocketStoriesShown(
                impressions = impressions,
                source = StoriesImpressionSource.HOMEPAGE,
            ),
        )

        job.join()
        assertTrue(pingReceived)
    }

    @Test
    fun `WHEN a list of recommendations are shown on the stories screen THEN record the impression telemetry with the stories screen source`() {
        val store = createStore()
        val impressions = TestUtils.getFakeContentRecommendations(limit = 3)
            .mapIndexed { index, contentRecommendation ->
                PocketImpression(
                    story = contentRecommendation,
                    position = index,
                )
            }

        assertNull(HomeContentArticle.impression.testGetValue())

        var pingReceived = false
        val job = Pings.home.testBeforeNextSubmit {
            assertNotNull(HomeContentArticle.impression.testGetValue())

            val snapshot = HomeContentArticle.impression.testGetValue()!!
            assertEquals(3, snapshot.size)
            snapshot.forEach {
                assertEquals("stories_screen", it.extra!!["source"])
            }

            pingReceived = true
        }

        store.dispatch(
            ContentRecommendationsAction.PocketStoriesShown(
                impressions = impressions,
                source = StoriesImpressionSource.STORIES_SCREEN,
            ),
        )

        job.join()
        assertTrue(pingReceived)
    }

    private fun createStore() = AppStore(
        middlewares = listOf(
            HomeTelemetryMiddleware(),
        ),
    )
}

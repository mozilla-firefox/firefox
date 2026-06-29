/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.middleware

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.service.pocket.PocketStory.ContentRecommendation
import org.mozilla.fenix.GleanMetrics.HomeContentArticle
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.ContentRecommendationsAction
import org.mozilla.fenix.components.appstate.AppState

/**
 * A [Middleware] for recording homepage related telemetry based on [AppAction]s that are
 * dispatch to the [AppStore].
 */
class HomeTelemetryMiddleware : Middleware<AppState, AppAction> {
    override fun invoke(
        store: Store<AppState, AppAction>,
        next: (AppAction) -> Unit,
        action: AppAction,
    ) {
        next(action)

        when (action) {
            is ContentRecommendationsAction.ContentRecommendationClicked -> {
                val recommendation = action.recommendation

                HomeContentArticle.click.record(
                    extra = HomeContentArticle.ClickExtra(
                        corpusItemId = recommendation.corpusItemId,
                        isSponsored = false,
                        position = action.position,
                        receivedRank = recommendation.receivedRank,
                        recommendedAt = recommendation.recommendedAt.toInt(),
                        scheduledCorpusItemId = recommendation.scheduledCorpusItemId,
                        tileId = recommendation.tileId.toInt(),
                        topic = recommendation.topic,
                        source = action.source.sourceName,
                    ),
                )

                Pings.home.submit()
            }

            is ContentRecommendationsAction.PocketStoriesShown -> {
                for ((story, position) in action.impressions) {
                    when (story) {
                        is ContentRecommendation -> {
                            HomeContentArticle.impression.record(
                                extra = HomeContentArticle.ImpressionExtra(
                                    corpusItemId = story.corpusItemId,
                                    isSponsored = false,
                                    position = position,
                                    receivedRank = story.receivedRank,
                                    recommendedAt = story.recommendedAt.toInt(),
                                    scheduledCorpusItemId = story.scheduledCorpusItemId,
                                    tileId = story.tileId.toInt(),
                                    topic = story.topic,
                                    source = action.source.sourceName,
                                ),
                            )
                        }
                        else -> {
                            // no-op
                        }
                    }
                }

                Pings.home.submit()
            }

            else -> {
                // no-op
            }
        }
    }
}

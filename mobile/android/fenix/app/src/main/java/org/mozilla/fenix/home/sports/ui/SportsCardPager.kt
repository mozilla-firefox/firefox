/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.DragInteraction
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerState
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.isTraversalGroup
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import androidx.compose.ui.util.lerp
import kotlin.math.min
import kotlinx.coroutines.flow.collectIndexed
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.sports.SportsCardImpressionSource
import org.mozilla.fenix.home.sports.SportsCardType
import org.mozilla.fenix.home.sports.fake.FakeMatchCardScenario
import org.mozilla.fenix.theme.FirefoxTheme

private const val PAGER_SIZE_ANIMATION_DURATION_MS = 180
private val PAGER_SIZE_ANIMATION_EASING = CubicBezierEasing(0.2f, 0.0f, 0.0f, 1.0f)

// Process-scoped memory of the card (by identity, not index) the user last *swiped* to, so a
// freshly-built widget (e.g. the homepage of a newly opened tab, or after a config change) opens
// on that same card rather than snapping back to the live card. Keying on identity means a round
// transition - where the remembered card no longer exists - re-focuses the live / next-upcoming
// card instead of reusing a stale index. Only a deliberate swipe writes this: automatic landings
// (initial open, the re-focus after match data loads, programmatic re-lands) must not, otherwise a
// transient loading-state card (e.g. the promo shown before matches arrive) would be pinned here.
// Null until the first swipe; reset when the process is killed, so a cold start falls back to the
// live / next-upcoming card.
private var sessionSportsPagerCardKey: String? = null

/**
 * Pairs a [SportsCardType] with its rendering composable so the pager can identify which card is settled on each page
 * without a parallel list that could drift from [pages]. [key] is a stable identity for the card, used to restore the
 * user's position across widget rebuilds and to detect when the remembered card no longer exists (e.g. a round
 * transition); match-backed cards key on their set of match ids, promo/error cards on their [type].
 */
data class SportsPage(
    val type: SportsCardType,
    val key: String,
    val content: @Composable (pageNumber: Int, pageCount: Int) -> Unit,
)

/**
 * A horizontally-swipeable pager that accepts arbitrary card composables, with a fixed header above the pages and a
 * page indicator overlaid at the bottom of the card. The indicator is hidden when there is only one page.
 *
 * Each page receives its 1-based position and the total page count so it can annotate its primary heading for assistive
 * technology (e.g. "FA Cup, page 1 of 2"). The pager itself is not a single TalkBack focus stop — children inside each
 * page remain individually focusable.
 *
 * @param pages Pages to display, paired with their [SportsCardType] so the pager can emit card-typed telemetry without
 *   a parallel structure. Order determines swipe order. Each [SportsPage.content] is invoked with `(pageNumber,
 *   pageCount)` where `pageNumber` is 1-based.
 * @param header Header displayed above the pages, which stays in place while swiping (e.g. [SportsWidgetHeader]).
 * @param modifier [Modifier] to apply to the outer container.
 * @param onCardShown Invoked once per pages-list mount for the initially-visible card
 *   ([SportsCardImpressionSource.IMPRESSION]) and once per subsequent settle on a different page
 *   ([SportsCardImpressionSource.SWIPE]). With a single page only the impression fires.
 * @param initialPage Page the pager opens on for a fresh composition — typically the live (or next upcoming) card, so
 *   the user lands on the most relevant match regardless of its position.
 */
@Composable
fun SportsCardPager(
    pages: List<SportsPage>,
    header: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    onCardShown: (SportsCardType, SportsCardImpressionSource) -> Unit = { _, _ -> },
    initialPage: Int = 0,
) {
    // Open on the card the user last settled on if it still exists in the current pages, otherwise
    // fall back to the live / next-upcoming / last card ([initialPage]). Keyed on card identity
    // rather than a raw index, so a round transition (the remembered card is gone) re-focuses the
    // relevant card instead of stranding the user on an unrelated card at the same index, while a
    // tab-switch-and-return (the card is still present) restores it.
    val initialTargetPage = pages.indexOfFirst { it.key == sessionSportsPagerCardKey }.takeIf { it >= 0 } ?: initialPage
    // Re-seed the pager state whenever the set of cards changes (matched by identity, so score
    // refreshes that keep the same fixtures don't recreate it). Without this, a page-set change -
    // e.g. the leading promo being replaced by match cards once data loads - leaves the pager on
    // its old index, which now points at a different card; it draws that card for a frame before
    // SportsPagerLandingEffect scrolls to the real target, producing a visible flash (e.g. keep
    // tabs -> champions -> finals). Opening the fresh state directly on the target skips that frame.
    val pageIdentityKey = remember(pages) { pages.joinToString(separator = "|") { it.key } }
    val pagerState =
        key(pageIdentityKey) {
            rememberPagerState(initialPage = initialTargetPage.coerceIn(0, (pages.size - 1).coerceAtLeast(0))) {
                pages.size
            }
        }
    SportsPagerStateEffects(
        pagerState = pagerState,
        pages = pages,
        initialPage = initialPage,
        onCardShown = onCardShown,
    )
    val showIndicator = pages.size > 1

    Column(modifier = modifier.sportsCardPagerContainer(showIndicator = showIndicator)) {
        header()

        Box(
            modifier =
                Modifier.fillMaxWidth()
                    .animateContentSize(
                        animationSpec =
                            tween(
                                durationMillis = PAGER_SIZE_ANIMATION_DURATION_MS,
                                easing = PAGER_SIZE_ANIMATION_EASING,
                            )
                    )
        ) {
            SportsCardPagerContent(
                pagerState = pagerState,
                pages = pages,
            )
        }

        if (showIndicator) {
            SportsPagerIndicator(
                pagerState = pagerState,
                activeColor = MaterialTheme.colorScheme.onSurface,
                inactiveColor = MaterialTheme.colorScheme.surfaceTint,
                modifier = Modifier.align(Alignment.CenterHorizontally).clearAndSetSemantics {},
            )
        }
    }
}

/**
 * Hosts the side effects that drive [pagerState]: re-landing on the user's remembered card (or the live / next-upcoming
 * / last card), emitting impression/swipe telemetry, and recording the settled card's identity into the process-scoped
 * record when the user swipes.
 */
@Composable
private fun SportsPagerStateEffects(
    pagerState: PagerState,
    pages: List<SportsPage>,
    initialPage: Int,
    onCardShown: (SportsCardType, SportsCardImpressionSource) -> Unit,
) {
    SportsPagerLandingEffect(pagerState, pages, initialPage)
    SportsPagerImpressionEffect(pagerState, pages, onCardShown)
    SportsPagerPositionRecorder(pagerState, pages)
}

/**
 * Re-evaluates the landing whenever the pages change: keeps the user on their card if it is still present (matched by
 * identity, even if its index shifted on a data refresh), otherwise re-focuses the live / next-upcoming / last card
 * ([initialPage]). This is what moves the pager off a stale position when the tournament advances to a new round.
 */
@Composable
private fun SportsPagerLandingEffect(
    pagerState: PagerState,
    pages: List<SportsPage>,
    initialPage: Int,
) {
    LaunchedEffect(pages) {
        if (pages.isNotEmpty()) {
            val rememberedIndex = pages.indexOfFirst { it.key == sessionSportsPagerCardKey }.takeIf { it >= 0 }
            val target = (rememberedIndex ?: initialPage).coerceIn(0, pages.lastIndex)
            if (target != pagerState.currentPage) {
                pagerState.scrollToPage(target)
            }
        }
    }
}

/** Emits card-typed telemetry: one IMPRESSION per pages mount, SWIPE on later settles. */
@Composable
private fun SportsPagerImpressionEffect(
    pagerState: PagerState,
    pages: List<SportsPage>,
    onCardShown: (SportsCardType, SportsCardImpressionSource) -> Unit,
) {
    LaunchedEffect(pages) {
        snapshotFlow { pagerState.settledPage }
            .distinctUntilChanged()
            .collectIndexed { emissionIndex, settledPage ->
                val source =
                    if (emissionIndex == 0) {
                        SportsCardImpressionSource.IMPRESSION
                    } else {
                        SportsCardImpressionSource.SWIPE
                    }
                pages.getOrNull(settledPage)?.let { page -> onCardShown(page.type, source) }
            }
    }
}

/**
 * Records the user's card by identity so a later home instance opens on it - but only when THEY moved the pager (a
 * drag). Automatic settles (the initial open, the re-focus after match data loads, or a programmatic re-land) are
 * ignored, so a transient loading-state card such as the promo shown before matches arrive is never pinned as the
 * remembered card. A drag can only occur on a visible, interactive widget, so this also covers the off-screen startup
 * case.
 */
@Composable
private fun SportsPagerPositionRecorder(
    pagerState: PagerState,
    pages: List<SportsPage>,
) {
    // Latest pages, readable from this long-lived effect without restarting it.
    val currentPages by rememberUpdatedState(pages)
    LaunchedEffect(pagerState) {
        var pendingUserDrag = false
        launch {
            pagerState.interactionSource.interactions.collect { interaction ->
                if (interaction is DragInteraction.Start) pendingUserDrag = true
            }
        }
        snapshotFlow { pagerState.settledPage }
            .distinctUntilChanged()
            .collect { settledPage ->
                if (pendingUserDrag) {
                    pendingUserDrag = false
                    sessionSportsPagerCardKey = currentPages.getOrNull(settledPage)?.key
                }
            }
    }
}

// Largest number of dots shown at once. Beyond this the indicator becomes a sliding window.
private const val MAX_VISIBLE_INDICATOR_DOTS = 15
private val IndicatorDotSize = 6.dp
private val IndicatorDotSpacing = 8.dp

// Sizes (relative to a full dot) for the dots at a window edge that still hides pages: the
// outermost is smallest, the next one in is medium, then dots are full size.
private const val INDICATOR_EDGE_SMALL_SCALE = 0.4f
private const val INDICATOR_EDGE_MEDIUM_SCALE = 0.7f

/**
 * When every page fits it renders one dot per page. Once there are more than [MAX_VISIBLE_INDICATOR_DOTS] pages it
 * shows a fixed-width window of dots that slides with the current page, keeping it centred; the dots shrink toward
 * whichever edge still has off-screen pages, signaling that more cards exist in that direction.
 */
@Composable
private fun SportsPagerIndicator(
    pagerState: PagerState,
    activeColor: Color,
    inactiveColor: Color,
    modifier: Modifier = Modifier,
) {
    val pageCount = pagerState.pageCount
    if (pageCount <= 0) return

    val visibleCount = min(pageCount, MAX_VISIBLE_INDICATOR_DOTS)
    val stride = IndicatorDotSize + IndicatorDotSpacing
    val rowWidth = stride * visibleCount - IndicatorDotSpacing

    // Current page plus the in-flight swipe fraction, so the window glides instead of jumping.
    val position = pagerState.currentPage + pagerState.currentPageOffsetFraction
    val maxStart = (pageCount - visibleCount).coerceAtLeast(0)
    // Index (fractional) of the leftmost dot in the window: centre the current page, then pin at
    // the ends so the window never scrolls past the first or last page.
    val windowStart = (position - (visibleCount - 1) / 2f).coerceIn(0f, maxStart.toFloat())
    val hasPagesBefore = windowStart > 0f
    val hasPagesAfter = windowStart < maxStart.toFloat()

    Box(modifier = modifier.width(rowWidth).height(IndicatorDotSize).clipToBounds()) {
        repeat(pageCount) { page ->
            val slot = page - windowStart
            var scale = 1f
            if (hasPagesBefore) scale = min(scale, edgeDotScale(slot))
            if (hasPagesAfter) scale = min(scale, edgeDotScale((visibleCount - 1) - slot))

            Box(
                modifier = Modifier.offset(x = stride * slot).size(IndicatorDotSize),
                contentAlignment = Alignment.Center,
            ) {
                Box(
                    modifier =
                        Modifier.size(IndicatorDotSize * scale)
                            .background(
                                color = if (page == pagerState.currentPage) activeColor else inactiveColor,
                                shape = CircleShape,
                            )
                )
            }
        }
    }
}

// Scale for a dot [distance] slots from a window edge that still hides pages: smallest at the very
// edge, medium one slot in, full size from two slots in.
private fun edgeDotScale(distance: Float): Float =
    when {
        distance >= 2f -> 1f
        distance >= 1f -> lerp(INDICATOR_EDGE_MEDIUM_SCALE, 1f, distance - 1f)
        distance >= 0f -> lerp(INDICATOR_EDGE_SMALL_SCALE, INDICATOR_EDGE_MEDIUM_SCALE, distance)
        else -> INDICATOR_EDGE_SMALL_SCALE
    }

@Composable
private fun Modifier.sportsCardPagerContainer(showIndicator: Boolean): Modifier =
    this.semantics { isTraversalGroup = true }
        .background(color = MaterialTheme.colorScheme.surfaceBright, shape = MaterialTheme.shapes.large)
        .padding(
            start = FirefoxTheme.layout.space.static100,
            end = FirefoxTheme.layout.space.static100,
            top = FirefoxTheme.layout.space.static100,
        )
        .thenConditional(Modifier.padding(bottom = FirefoxTheme.layout.space.static200)) { showIndicator }

@Composable
private fun SportsCardPagerContent(
    pagerState: PagerState,
    pages: List<SportsPage>,
) {
    HorizontalPager(
        state = pagerState,
        verticalAlignment = Alignment.Top,
        modifier = Modifier.fillMaxWidth().padding(top = FirefoxTheme.layout.space.static200).clipToBounds(),
    ) { page ->
        pages[page].content(page + 1, pages.size)
    }
}

@PreviewLightDark
@Composable
private fun SportsCardPagerPreview() {
    val scenarios =
        listOf(
            FakeMatchCardScenario.Live,
            FakeMatchCardScenario.Scheduled,
            FakeMatchCardScenario.Final,
        )

    FirefoxTheme {
        Surface {
            SportsCardPager(
                pages =
                    scenarios.map { scenario ->
                        val state = scenario.build().first()
                        SportsPage(
                            type = SportsCardType.MATCH,
                            key = "preview-${scenario.name}",
                        ) { pageNumber, pageCount ->
                            MatchCard(
                                state = state,
                                errorState = null,
                                modifier = Modifier.fillMaxWidth(),
                                onRefresh = {},
                                onMatchClicked = { _, _, _ -> },
                                pageNumber = pageNumber,
                                pageCount = pageCount,
                            )
                        }
                    },
                header = { SportsCardPagerPreviewHeader() },
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

private class SportsCardPagerErrorPreviewProvider : PreviewParameterProvider<SportCardErrorState> {
    override val values = SportCardErrorState.entries.asSequence()
}

@PreviewLightDark
@Composable
private fun SportsCardPagerErrorPreview(
    @PreviewParameter(SportsCardPagerErrorPreviewProvider::class) error: SportCardErrorState
) {
    FirefoxTheme {
        Surface {
            SportsCardPager(
                pages =
                    listOf(
                        SportsPage(
                            type = SportsCardType.fromError(error),
                            key = "preview-${error.name}",
                        ) { _, _ ->
                            SportsWidgetErrorCard(
                                state = error,
                                onRefresh = {},
                            )
                        }
                    ),
                header = { SportsCardPagerPreviewHeader() },
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

@Composable
private fun SportsCardPagerPreviewHeader() {
    SportsWidgetHeader(
        teamName = "Moz ABC",
        drawableResId = iconsR.drawable.mozac_ic_soccer_ball_24,
        onResults = {},
        onStandings = {},
        onWallpapers = {},
        onShare = {},
        onRemove = {},
    )
}

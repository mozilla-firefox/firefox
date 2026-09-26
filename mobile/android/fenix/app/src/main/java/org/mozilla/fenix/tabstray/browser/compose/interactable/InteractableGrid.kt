/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.browser.compose.interactable

import androidx.annotation.VisibleForTesting
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.AnimationVector2D
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.VectorConverter
import androidx.compose.animation.core.VisibilityThreshold
import androidx.compose.animation.core.spring
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.scrollBy
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.lazy.grid.LazyGridItemInfo
import androidx.compose.foundation.lazy.grid.LazyGridItemScope
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.toOffset
import androidx.compose.ui.unit.toSize
import androidx.compose.ui.zIndex
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import mozilla.components.compose.base.modifier.thenConditional
import org.mozilla.fenix.tabstray.browser.compose.ItemInteractionState
import org.mozilla.fenix.tabstray.controller.ItemInteractionHandler
import org.mozilla.fenix.tabstray.ui.tabitems.Elevation
import org.mozilla.fenix.tabstray.ui.tabitems.defaultGridItemAnimation

/**
 * Remember the interactable state for grid items.
 *
 * @param gridState State of the grid.
 * @param itemInteractionHandler Handles item interactions such as moves and drag and drop.
 * @param ignoredItems Set of keys for non-draggable items.
 * @param liveReorderEnabled Whether item reorders should happen 'live' during a drag.
 * @param onLongPress Optional callback to be invoked when long pressing an item.
 * @param dragAndDropEnabled Whether drag and drop should be considered in the list of candidates. Note that this is
 *   trivially true, but if we use this grid for other pages, the setting is available.
 */
@Composable
fun createGridInteractionState(
    gridState: LazyGridState,
    itemInteractionHandler: ItemInteractionHandler,
    ignoredItems: Set<Any>,
    liveReorderEnabled: Boolean,
    onLongPress: (LazyGridItemInfo) -> Unit = {},
    dragAndDropEnabled: Boolean = true,
): GridInteractionState {
    val scope = rememberCoroutineScope()
    val touchSlop = LocalViewConfiguration.current.touchSlop
    val hapticFeedback = LocalHapticFeedback.current
    val state =
        remember(gridState) {
            GridInteractionStateImpl(
                gridState = gridState,
                touchSlop = touchSlop,
                itemInteractionHandler = itemInteractionHandler,
                scope = scope,
                ignoredItems = ignoredItems,
                onLongPress = onLongPress,
                hapticFeedback = hapticFeedback,
                dragAndDropEnabled = dragAndDropEnabled,
                liveReorderEnabled = liveReorderEnabled,
            )
        }
    return state
}

/** Stable snapshot interface for a grid's interaction state. */
@Stable
interface GridInteractionState {
    /** LayoutCoordinates used to map between grid and screen space. */
    val gridLayoutCoordinates: LayoutCoordinates?

    /** The currently dragged item. Can be [InteractionState.Grid.None] */
    val draggedItem: InteractionState.Grid

    /** The currently hovered item. Can be [InteractionState.Grid.None] */
    val hoveredItem: InteractionState.Grid

    /** The [Rect] used to display a reorder placement indicator */
    val highlightedRect: Rect?

    /** The current [InteractionMode], e.g. reordering, scrolling, drag and drop */
    val interactionMode: InteractionMode.Grid

    /** The previously dragged item's key */
    val previousKeyOfDraggedItem: TabItemKey?

    /** Cached offset used to animate the item from a cancelled drag back into place */
    val previousItemAnimatableOffset: Animatable<Offset, AnimationVector2D>

    /** An item's size */
    val itemSize: IntSize?

    /**
     * Called to update the cached offset of a dragged item by its [LayoutCoordinates]
     *
     * @param itemCoordinates the item's layout coordinates
     */
    fun onDraggedItemPositioned(itemCoordinates: LayoutCoordinates)

    /**
     * Computes the offset of an item at a given index.
     *
     * @param index the item's index
     */
    fun computeItemOffset(index: Int): Offset

    /**
     * Called when a slop threshold has been exceeded to start a drag event.
     *
     * @param offset The offset for the drag event
     * @param shouldLongPress Whether long press is needed to initiate a drag event
     */
    fun onTouchSlopPassed(offset: Offset, shouldLongPress: Boolean)

    /**
     * Called when a drag event is updated.
     *
     * @param offset the latest offset for the drag event
     * @param preserveSelectMode whether select mode should be preserved
     */
    fun onDrag(offset: Offset, preserveSelectMode: Boolean)

    /** Called when a drag event ends. */
    fun onDragEnd()

    /** Called when a drag is cancelled, for example, when a user lets go without performing an action. */
    fun onDragCancelled()

    /** Updates the stored layout coordinates in order to map grid space to screen space. */
    fun updateGridLayoutCoordinates(coordinates: LayoutCoordinates)

    /** Called to indicate to the grid that the drop handling has been completed and the state can be reset. */
    fun reset()
}

/**
 * Class containing details about the current state of dragging in grid.
 *
 * @param gridState State of the grid.
 * @param touchSlop Distance in pixels the user can wander until we consider they started dragging.
 * @param scope [CoroutineScope] used for scrolling to the target item.
 * @param hapticFeedback [HapticFeedback] used for performing haptic feedback on item long press.
 * @param dragAndDropEnabled: Whether drag and drop is enabled for this grid. If not enabled, it will be excluded as a
 *   candidate for interaction when computing the most likely gesture candidate.
 * @param itemInteractionHandler Handles item interactions such as moves and drag and drop.
 * @param onLongPress Optional callback to be invoked when long pressing an item.
 * @param ignoredItems List of keys for non-draggable items.
 * @param liveReorderEnabled Whether item reorders should happen 'live' during a drag.
 */
class GridInteractionStateImpl
internal constructor(
    private val gridState: LazyGridState,
    private val touchSlop: Float,
    private val scope: CoroutineScope,
    private val hapticFeedback: HapticFeedback,
    private val dragAndDropEnabled: Boolean,
    private val itemInteractionHandler: ItemInteractionHandler,
    private val onLongPress: (LazyGridItemInfo) -> Unit = {},
    private val ignoredItems: Set<Any> = emptySet(),
    private val liveReorderEnabled: Boolean,
) : GridInteractionState {

    override var gridLayoutCoordinates: LayoutCoordinates? by mutableStateOf(null)
        private set

    private var cachedDraggedItemLayoutOffset: Offset? = null

    override var draggedItem by mutableStateOf<InteractionState.Grid>(InteractionState.Grid.None)
        private set

    override var hoveredItem by mutableStateOf<InteractionState.Grid>(InteractionState.Grid.None)
        private set

    override var highlightedRect by mutableStateOf<Rect?>(null)
        private set

    override var interactionMode by mutableStateOf<InteractionMode.Grid>(InteractionMode.Grid.None)
        private set

    private var moved by mutableStateOf(false)

    override var previousKeyOfDraggedItem by mutableStateOf<TabItemKey?>(null)
        private set

    override val previousItemAnimatableOffset = Animatable(Offset.Zero, Offset.VectorConverter)

    private var scrollJob by mutableStateOf<Job?>(null)

    override val itemSize: IntSize?
        get() = gridState.layoutInfo.visibleItemsInfo.firstOrNull { it.key !in ignoredItems }?.size

    override fun onDraggedItemPositioned(itemCoordinates: LayoutCoordinates) {
        gridLayoutCoordinates?.let {
            cachedDraggedItemLayoutOffset = it.localPositionOf(itemCoordinates, Offset.Zero)
        }
    }

    override fun computeItemOffset(index: Int): Offset {
        val itemAtIndex = gridState.layoutInfo.visibleItemsInfo.firstOrNull { info -> info.index == index }
        if (itemAtIndex != null) {
            return draggedItem.initialOffset + draggedItem.cumulatedOffset - itemAtIndex.offset.toOffset()
        }
        val cachedOffset = cachedDraggedItemLayoutOffset ?: return Offset.Zero
        return draggedItem.initialOffset + draggedItem.cumulatedOffset - cachedOffset
    }

    override fun onTouchSlopPassed(offset: Offset, shouldLongPress: Boolean) {
        gridState.findItem(offset)?.also { item ->
            val key = item.key as? String
            key?.let {
                draggedItem =
                    InteractionState.Grid.Active(
                        index = item.index,
                        key = it,
                        initialOffset = item.offset.toOffset(),
                    )
            }
            if (shouldLongPress) {
                hapticFeedback.performHapticFeedback(HapticFeedbackType.LongPress)
                onLongPress(item)
            }
        }
    }

    override fun onDragEnd() {
        if (draggedItem is InteractionState.Grid.Active) {
            handleDragEnd(interactionMode)
        }
    }

    override fun updateGridLayoutCoordinates(coordinates: LayoutCoordinates) {
        gridLayoutCoordinates = coordinates
    }

    override fun reset() {
        resetState()
    }

    private fun doReorder(mode: InteractionMode.Grid.Reordering) {
        if (draggedItem.index == gridState.firstVisibleItemIndex) {
            itemSize?.height?.let {
                autoScroll(-it.toFloat())
            }
        }
        itemInteractionHandler.onMove(
            sourceKey = mode.source.key,
            targetKey = mode.target.key,
            placeAfter = mode.placeAfter,
        )
    }

    private fun handleDragEnd(mode: InteractionMode.Grid) {
        when (mode) {
            is InteractionMode.Grid.DragAndDrop -> {
                itemInteractionHandler.onDrop(
                    mode.source.key,
                    mode.target.key,
                )
            }

            is InteractionMode.Grid.Reordering -> {
                if (!liveReorderEnabled) {
                    doReorder(mode)
                }
                itemInteractionHandler.onDragCancel()
                resetState()
            }

            is InteractionMode.Grid.Scroll,
            is InteractionMode.Grid.None -> {
                // No action is taken
                if (moved) {
                    itemInteractionHandler.onDragCancel()
                }
                resetState()
            }
        }
    }

    override fun onDragCancelled() {
        if (moved) {
            itemInteractionHandler.onDragCancel()
        }
        resetState()
    }

    /**
     * Restricts all scroll actions to run in a single job. If a scroll job is currently executing when a new one is
     * asked for, it is cancelled.
     */
    fun autoScroll(amount: Float) {
        scrollJob?.cancel()
        scrollJob = scope.launch {
            gridState.scrollBy(amount)
        }
    }

    private fun animateItems() {
        if (draggedItem is InteractionState.Grid.Active) {
            val draggingItem = draggedItem as InteractionState.Grid.Active
            previousKeyOfDraggedItem = draggedItem.key
            val startOffset = computeItemOffset(draggingItem.index)
            scope.launch {
                previousItemAnimatableOffset.snapTo(startOffset)
                previousItemAnimatableOffset.animateTo(
                    Offset.Zero,
                    spring(
                        stiffness = Spring.StiffnessMediumLow,
                        visibilityThreshold = Offset.VisibilityThreshold,
                    ),
                )
                previousKeyOfDraggedItem = null
            }
        }
    }

    private fun resetState() {
        animateItems()
        draggedItem = InteractionState.Grid.None
        hoveredItem = InteractionState.Grid.None
        highlightedRect = null
        interactionMode = InteractionMode.Grid.None
        moved = false
        scrollJob?.cancel()
        scrollJob = null
    }

    private fun handleReorderingModeOnDrag(mode: InteractionMode.Grid.Reordering) {
        hoveredItem = InteractionState.Grid.None
        if (liveReorderEnabled) {
            doReorder(mode)
            // Update the dragged item's index as reorders happen
            val newIndex = gridState.layoutInfo.visibleItemsInfo.firstOrNull { it.key == draggedItem.key }?.index ?: -1
            if (newIndex != -1) {
                draggedItem = draggedItem.copyWithNewIndex(newIndex)
            }
        } else {
            // Update the gesture indicator
            highlightedRect = mode.rect
        }
    }

    private fun handleDragAndDropModeOnDrag(mode: InteractionMode.Grid.DragAndDrop) {
        highlightedRect = null
        if (hoveredItem != mode.target) {
            hoveredItem = mode.target
        }
    }

    private fun handleNoInteractionModeOnDrag(itemOffset: GridItemOffset) {
        highlightedRect = null
        hoveredItem = InteractionState.Grid.None
        val overscroll =
            findOverscroll(
                draggedItem = draggedItem,
                itemOffset = itemOffset,
                gridState = gridState,
            )
        if (overscroll != 0f) {
            autoScroll(overscroll)
        }
    }

    private fun handleScrollInteractionModeOnDrag(itemOffset: GridItemOffset) {
        hoveredItem = InteractionState.Grid.None
        highlightedRect = null
        val overscroll =
            findOverscroll(
                draggedItem = draggedItem,
                itemOffset = itemOffset,
                gridState = gridState,
            )
        if (overscroll != 0f) {
            autoScroll(overscroll)
        }
    }

    override fun onDrag(offset: Offset, preserveSelectMode: Boolean) {
        draggedItem = draggedItem.incrementCumulatedOffset(offset)
        if (!moved && draggedItem.cumulatedOffset.getDistance() > touchSlop) {
            (draggedItem as? InteractionState.Grid.Active)?.let { active ->
                itemInteractionHandler.onDragStart(
                    sourceKey = active.key,
                    preserveSelectMode = preserveSelectMode,
                )
            }
            moved = true
        }

        val draggingItem = draggedItem as? InteractionState.Grid.Active ?: return
        val itemOffset =
            GridItemOffset(
                draggingItem,
                computeItemOffset(draggingItem.index),
                itemSize ?: IntSize(0, 0),
            )

        val mode =
            determineInteractionMode(
                gridState = gridState,
                draggedItem = draggedItem,
                itemOffset = itemOffset,
                ignoredItems = ignoredItems,
                dragAndDropEnabled = dragAndDropEnabled,
            )
        // debounce spurious reorder events
        // note that repeated, identical scroll events are valid
        if (mode is InteractionMode.Grid.Reordering && interactionMode == mode) {
            return
        }
        interactionMode = mode

        when (mode) {
            is InteractionMode.Grid.DragAndDrop -> {
                handleDragAndDropModeOnDrag(mode = mode)
            }

            is InteractionMode.Grid.Reordering -> {
                handleReorderingModeOnDrag(mode = mode)
            }

            is InteractionMode.Grid.None -> {
                handleNoInteractionModeOnDrag(itemOffset = itemOffset)
            }

            is InteractionMode.Grid.Scroll -> {
                handleScrollInteractionModeOnDrag(itemOffset = itemOffset)
            }
        }
    }
}

private fun determineInteractionMode(
    gridState: LazyGridState,
    draggedItem: InteractionState,
    itemOffset: GridItemOffset,
    ignoredItems: Set<Any>,
    dragAndDropEnabled: Boolean,
): InteractionMode.Grid {
    if (gridState.isScrollInProgress) return InteractionMode.Grid.None
    if (draggedItem is InteractionState.Grid.None) return InteractionMode.Grid.None
    val topCandidate =
        gatherCandidates(
                gridState = gridState,
                draggedItemOffset = itemOffset,
                draggedItem = draggedItem,
                ignoredItems = ignoredItems,
            )
            .filter { item ->
                // Filter out the drag and drop interaction type if it is disabled, e.g. for private tabs
                dragAndDropEnabled || item.type !is InteractionType.Overlap
            }
            .minByOrNull { it.score }

    // Convert the LazyGridItemInfo Any into a usable TabItem id
    val key = topCandidate?.anchorItem?.key as? String
    return when {
        topCandidate == null || key == null -> {
            InteractionMode.Grid.None
        }

        topCandidate.type is InteractionType.None -> {
            InteractionMode.Grid.None
        }

        topCandidate.type is InteractionType.Overlap -> {
            InteractionMode.Grid.DragAndDrop(
                source = draggedItem as InteractionState.Grid.Active,
                target =
                    InteractionState.Grid.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toOffset(),
                    ),
            )
        }

        topCandidate.type is InteractionType.LeftGutter -> {
            InteractionMode.Grid.Reordering(
                source = draggedItem as InteractionState.Grid.Active,
                target =
                    InteractionState.Grid.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toOffset(),
                    ),
                placeAfter = false,
                rect = topCandidate.type.rect,
            )
        }

        topCandidate.type is InteractionType.RightGutter -> {
            InteractionMode.Grid.Reordering(
                source = draggedItem as InteractionState.Grid.Active,
                target =
                    InteractionState.Grid.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toOffset(),
                    ),
                placeAfter = true,
                rect = topCandidate.type.rect,
            )
        }

        topCandidate.type is InteractionType.Scroll -> {
            InteractionMode.Grid.Scroll(topCandidate.type.scroll)
        }

        else -> InteractionMode.Grid.None
    }
}

/**
 * Calculates the distance from the closest point on a [Rect] object to a given point in space represented as an
 * [Offset]. Uses getDistanceSquared() for performance reasons, which is appropriate for comparisons to other distances
 * calculated with the same method. Returns a float value representing the distance.
 *
 * @param offset: [Offset] representing a comparison point in spce.
 */
@VisibleForTesting
internal fun Rect.closestDistanceTo(offset: Offset): Float {
    return (this.closestPointTo(offset) - offset).getDistanceSquared()
}

/**
 * Calculates the closest point on a [Rect] to a given point represented as an [Offset].
 *
 * @param offset: [Offset] representing a comparison point in space.
 */
@VisibleForTesting
internal fun Rect.closestPointTo(offset: Offset): Offset {
    val clampedX = offset.x.coerceIn(this.left, this.right)
    val clampedY = offset.y.coerceIn(this.top, this.bottom)
    return Offset(clampedX, clampedY)
}

private fun getScrollCandidates(
    gridState: LazyGridState,
    draggedItemOffset: GridItemOffset,
): List<GridInteractionCandidate> {
    val firstVisible = gridState.layoutInfo.visibleItemsInfo.firstOrNull() ?: return emptyList()
    val candidates = mutableListOf<GridInteractionCandidate>()
    val scrollRectSize =
        Size(
            width = gridState.layoutInfo.viewportSize.width.toFloat(),
            height = firstVisible.size.height / 3.0f,
        )
    // Scroll up can only be a candidate if we are not at the top of the view
    if (gridState.firstVisibleItemIndex > 0) {
        val scrollUpRect =
            Rect(
                offset = Offset(0f, 0f),
                size = scrollRectSize,
            )
        if (scrollUpRect.bottom > draggedItemOffset.start.y) {
            candidates.add(
                GridInteractionCandidate(
                    type = InteractionType.Scroll(scroll = draggedItemOffset.start.y.minus(scrollUpRect.bottom)),
                    anchorItem = firstVisible,
                    score = scrollUpRect.closestDistanceTo(draggedItemOffset.center),
                )
            )
        }
    }
    // Scroll down can only be a candidate if we are not at the bottom of the view
    val lastVisible = gridState.layoutInfo.visibleItemsInfo.lastOrNull() ?: return candidates
    if (lastVisibleItemIndex(gridState) < gridState.layoutInfo.totalItemsCount - 1) {
        val scrollDownRect =
            Rect(
                offset = Offset(0f, gridState.layoutInfo.viewportSize.height - scrollRectSize.height),
                size = scrollRectSize,
            )
        if (scrollDownRect.top < draggedItemOffset.end.y) {
            candidates.add(
                GridInteractionCandidate(
                    type = InteractionType.Scroll(scroll = draggedItemOffset.end.y.minus(scrollDownRect.top)),
                    anchorItem = lastVisible,
                    score = scrollDownRect.closestDistanceTo(draggedItemOffset.center),
                )
            )
        }
    }
    return candidates
}

private fun lastVisibleItemIndex(gridState: LazyGridState): Int {
    return gridState.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: 0
}

@VisibleForTesting
internal fun gatherCandidates(
    gridState: LazyGridState,
    draggedItemOffset: GridItemOffset,
    draggedItem: InteractionState,
    ignoredItems: Set<TabItemKey>,
): List<GridInteractionCandidate> {
    val candidates = mutableListOf<GridInteractionCandidate>()

    candidates.addAll(
        elements =
            getScrollCandidates(
                gridState = gridState,
                draggedItemOffset = draggedItemOffset,
            )
    )

    for (itemCandidate in gridState.layoutInfo.visibleItemsInfo) {
        if (itemCandidate.key in ignoredItems) continue
        val gutterSpacing = gridState.layoutInfo.mainAxisItemSpacing
        val verticalGutterSize = Size(width = gutterSpacing.toFloat(), height = itemCandidate.size.height.toFloat())

        // Body candidate
        val bodyRect =
            Rect(
                offset = itemCandidate.offset.toOffset(),
                size = itemCandidate.size.toSize(),
            )

        // Set up a no-op candidate to prevent stutters if the dragged item is held over its
        // current position in the grid.  This is most noticeable with live reordering enabled.
        if (itemCandidate.key == draggedItem.key) {
            candidates.add(
                GridInteractionCandidate(
                    type = InteractionType.None,
                    anchorItem = itemCandidate,
                    score = bodyRect.closestDistanceTo(draggedItemOffset.center),
                )
            )
        } else {
            // prefer the item's center point for scoring over hitting the closest space within the body
            // otherwise, it becomes very difficult to position over the gutters on the edges
            val distanceToCenter = (bodyRect.center - draggedItemOffset.center).getDistanceSquared()
            candidates.add(
                GridInteractionCandidate(
                    type = InteractionType.Overlap,
                    anchorItem = itemCandidate,
                    score = distanceToCenter,
                )
            )

            // Left gutter candidate
            val leftGutter =
                Rect(
                    offset =
                        Offset(
                            itemCandidate.offset.x.toFloat(),
                            itemCandidate.offset.y.toFloat() + gridState.layoutInfo.beforeContentPadding,
                        ),
                    size = verticalGutterSize,
                )
            candidates.add(
                GridInteractionCandidate(
                    type = InteractionType.LeftGutter(rect = leftGutter),
                    anchorItem = itemCandidate,
                    score = leftGutter.closestDistanceTo(draggedItemOffset.center),
                )
            )

            // Right gutter candidate
            val rightGutter =
                Rect(
                    offset =
                        Offset(
                            itemCandidate.endOffset.x.toFloat() + gutterSpacing.toFloat(),
                            itemCandidate.offset.y.toFloat() + gridState.layoutInfo.beforeContentPadding,
                        ),
                    size = verticalGutterSize,
                )
            candidates.add(
                GridInteractionCandidate(
                    type = InteractionType.RightGutter(rect = rightGutter),
                    anchorItem = itemCandidate,
                    score = rightGutter.closestDistanceTo(draggedItemOffset.center),
                )
            )
        }
    }

    return candidates
}

private fun findOverscroll(
    draggedItem: InteractionState.Grid,
    itemOffset: GridItemOffset,
    gridState: LazyGridState,
): Float {
    return when {
        draggedItem.cumulatedOffset.y > 0 ->
            (itemOffset.end.y - gridState.layoutInfo.viewportEndOffset).coerceAtLeast(0f)

        draggedItem.cumulatedOffset.y < 0 ->
            (itemOffset.start.y - gridState.layoutInfo.viewportStartOffset).coerceAtMost(0f)

        else -> 0f
    }
}

/**
 * Container for draggable grid item.
 *
 * @param state State of the lazy grid.
 * @param key Key of the item to be displayed.
 * @param position Position in the grid of the item to be displayed.
 * @param swipingActive Whether the container is being swiped.
 * @param enteringItemKey The key of the item entering composition, if any. Can be null.
 * @param enteringItemDecoration Decoration to be applied to items entering composition.
 * @param content Content of the item to be displayed.
 */
@Composable
fun LazyGridItemScope.InteractableDragItemContainer(
    state: GridInteractionState,
    key: String,
    position: Int,
    swipingActive: Boolean,
    enteringItemKey: String? = null,
    enteringItemDecoration: @Composable (ItemInteractionState) -> Modifier = { Modifier },
    content: @Composable (interactionState: ItemInteractionState) -> Unit,
) {
    val itemInteractionState =
        ItemInteractionState(
            isHoveredByItem = key == state.hoveredItem.key,
            isDragged = key == state.draggedItem.key,
            isEntering = key == enteringItemKey,
        )
    /*
     * This outer box allows us to retrieve the global layout coordinates, so we can continue to render
     * an off-screen LazyGridItem as the user drags it, since we will lose the item's position as a reference
     * and graphicsLayer translations are local offsets against an item.  graphicsLayer translations are included in
     * LayoutCoordinates measurements, so the translations happen in an inner Box in order to separate concerns.
     */
    Box(
        modifier =
            Modifier.zIndex(
                    if (swipingActive) {
                        Elevation.SWIPE_ACTIVE
                    } else if (key == enteringItemKey) {
                        Elevation.ENTERING_ITEM
                    } else if (key == state.draggedItem.key || key == state.previousKeyOfDraggedItem) {
                        Elevation.DRAGGED_ITEM
                    } else {
                        Elevation.NO_INTERACTION
                    }
                )
                .onGloballyPositioned {
                    if (key == state.draggedItem.key) {
                        state.onDraggedItemPositioned(it)
                    }
                }
                // The entrance decoration animation values must be hoisted above the rest of the grid
                // to prevent clipping if an item is oversized past its bounds.
                // This only impacts the grid view.
                .thenConditional(
                    modifier = enteringItemDecoration(itemInteractionState),
                    predicate = { itemInteractionState.isEntering },
                )
                .thenConditional(
                    modifier =
                        Modifier.defaultGridItemAnimation(
                            lazyGridItemScope = this,
                            suppressTransitions = enteringItemKey != null,
                        ),
                    predicate = { key != state.draggedItem.key && key != state.previousKeyOfDraggedItem },
                )
    ) {
        Box(
            modifier =
                Modifier.then(
                    when (key) {
                        state.draggedItem.key -> {
                            Modifier.graphicsLayer {
                                translationX = state.computeItemOffset(position).x
                                translationY = state.computeItemOffset(position).y
                            }
                        }

                        state.previousKeyOfDraggedItem -> {
                            Modifier.graphicsLayer {
                                translationX = state.previousItemAnimatableOffset.value.x
                                translationY = state.previousItemAnimatableOffset.value.y
                            }
                        }

                        else -> {
                            Modifier
                        }
                    }
                ),
            propagateMinConstraints = true,
        ) {
            content(itemInteractionState)
        }
    }
}

/** Calculate the offset of an item taking its width and height into account. */
private val LazyGridItemInfo.endOffset: IntOffset
    get() = IntOffset(offset.x + size.width, offset.y + size.height)

/**
 * Find item based on position on screen.
 *
 * @param offset Position on screen used to find the item.
 */
private fun LazyGridState.findItem(offset: Offset) =
    layoutInfo.visibleItemsInfo.firstOrNull { item ->
        offset.x.toInt() in item.offset.x..item.endOffset.x && offset.y.toInt() in item.offset.y..item.endOffset.y
    }

/**
 * Detects press, long press and drag gestures.
 *
 * @param reorderState Grid reordering state used for dragging callbacks.
 * @param isInMultiSelectMode Whether or not multi-select mode is active for the grid being reordered
 */
fun Modifier.detectGridPressAndDragGestures(
    reorderState: GridInteractionState,
    isInMultiSelectMode: Boolean,
): Modifier =
    pointerInput(isInMultiSelectMode) {
        // In multi-select mode, drag gestures will be detected without a long press and the reorder state
        // will attempt to preserve the select mode state.
        if (isInMultiSelectMode) {
            detectDragGestures(
                onDragStart = { offset -> reorderState.onTouchSlopPassed(offset, false) },
                onDrag = { change, dragAmount ->
                    change.consume()
                    reorderState.onDrag(offset = dragAmount, preserveSelectMode = true)
                },
                onDragEnd = reorderState::onDragEnd,
                onDragCancel = reorderState::onDragCancelled,
            )
        } else {
            detectDragGesturesAfterLongPress(
                onDragStart = { offset -> reorderState.onTouchSlopPassed(offset, true) },
                onDrag = { change, dragAmount ->
                    change.consume()
                    reorderState.onDrag(offset = dragAmount, preserveSelectMode = false)
                },
                onDragEnd = reorderState::onDragEnd,
                onDragCancel = reorderState::onDragCancelled,
            )
        }
    }

private typealias TabItemKey = Any

/** Class representing a grid item's [Offset] values - start, center, and end. */
data class GridItemOffset(
    val draggedItem: InteractionState.Grid.Active,
    val draggingItemOffset: Offset,
    val itemSize: IntSize,
) {
    val start: Offset by lazy {
        draggedItem.initialOffset + draggedItem.cumulatedOffset
    }

    val end by lazy {
        Offset(
            start.x + itemSize.width,
            start.y + itemSize.height,
        )
    }

    val center by lazy {
        start + (end - start) / 2f
    }
}

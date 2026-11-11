/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animate
import androidx.compose.animation.core.spring
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.scrollBy
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.absoluteOffset
import androidx.compose.foundation.lazy.LazyItemScope
import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
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
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.zIndex
import kotlin.math.abs
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

/**
 * Remember the reordering state for reordering list items.
 *
 * List items MUST supply stable keys, since interactions track items by key. By default, Compose uses indices, which
 * will break after any move.
 *
 * @param listState State of the list.
 * @param ignoredItems Set of keys for items that cannot be dragged and that other items cannot be moved next to or
 *   dropped onto.
 * @param liveReorderEnabled Whether reordering should happen 'live' while a drag is active.
 * @param onLongPress Callback to be invoked when long pressing an item.
 * @param itemInteractionHandler Handler for item interactions.
 * @param dragAndDropEnabled Whether the drag and drop feature is enabled for this list.
 * @param fixedItems Set of keys for items that cannot be dragged but that other items can be moved next to or dropped
 *   onto.
 * @param scrollZoneHeight Height of the areas at the start and at the end of the list in which the center of a dragged
 *   item scrolls it, once the item went past the edge of the list there. `null` for a third of the first visible item
 *   other than the dragged one, whether the dragged item went past the edge or not.
 * @param interceptInteractionMode Called with the dragged item and the interaction the list found for it, whenever the
 *   item is dragged, to return the interaction to use instead, for example to retarget it or to reject it with
 *   [InteractionMode.List.None]. The interaction it returns is shown and acted upon when the user lets go.
 */
@Composable
fun createListInteractionState(
    listState: LazyListState,
    ignoredItems: Set<Any>,
    liveReorderEnabled: Boolean,
    onLongPress: (LazyListItemInfo) -> Unit = {},
    itemInteractionHandler: ItemInteractionHandler,
    dragAndDropEnabled: Boolean = true,
    fixedItems: Set<Any> = emptySet(),
    scrollZoneHeight: Dp? = null,
    interceptInteractionMode: (InteractionState.List.Active, InteractionMode.List) -> InteractionMode.List =
        { _, mode ->
            mode
        },
): ListInteractionState {
    val scope = rememberCoroutineScope()
    val touchSlop = LocalViewConfiguration.current.touchSlop
    val hapticFeedback = LocalHapticFeedback.current
    val scrollZoneHeightPx = scrollZoneHeight?.let { with(LocalDensity.current) { it.toPx() } }
    val state =
        remember(listState) {
            ListInteractionStateImpl(
                listState = listState,
                scope = scope,
                touchSlop = touchSlop,
                hapticFeedback = hapticFeedback,
                ignoredItems = ignoredItems,
                fixedItems = fixedItems,
                scrollZoneHeight = scrollZoneHeightPx,
                onLongPress = onLongPress,
                itemInteractionHandler = itemInteractionHandler,
                dragAndDropEnabled = dragAndDropEnabled,
                liveReorderEnabled = liveReorderEnabled,
                interceptInteractionMode = interceptInteractionMode,
            )
        }
    SideEffect {
        state.ignoredItems = ignoredItems
        state.fixedItems = fixedItems
        state.scrollZoneHeight = scrollZoneHeightPx
        state.itemInteractionHandler = itemInteractionHandler
        state.dragAndDropEnabled = dragAndDropEnabled
        state.liveReorderEnabled = liveReorderEnabled
        state.onLongPress = onLongPress
        state.interceptInteractionMode = interceptInteractionMode
    }
    return state
}

/** Stable snapshot interface for a list's interaction state. */
@Stable
interface ListInteractionState {
    /** The currently dragged item. Can be [InteractionState.List.None] */
    val draggedItem: InteractionState.List

    /** The currently hovered item. Can be [InteractionState.List.None] */
    val hoveredItem: InteractionState.List

    /** The [Rect] used to display a reorder placement indicator */
    val highlightedRect: Rect?

    /** The current [InteractionMode], e.g. reordering, scrolling, drag and drop */
    val interactionMode: InteractionMode.List

    /** The previously dragged item's key */
    val previousKeyOfDraggedItem: Any?

    /** The list's orientation */
    val orientation: Orientation

    /**
     * Called when a slop threshold has been exceeded to start a drag event.
     *
     * @param offset The offset for the drag event
     * @param shouldLongPress Whether long press is needed to initiate a drag event
     */
    fun onTouchSlopPassed(offset: Float, shouldLongPress: Boolean)

    /**
     * Called when a drag event is updated.
     *
     * @param offset the latest offset for the drag event
     * @param preserveSelectMode whether select mode should be preserved
     */
    fun onDrag(offset: Float, preserveSelectMode: Boolean)

    /** Called when the user lets go of the dragged item. */
    fun onDragEnd()

    /** Called when a drag is cancelled before the user lets go of the dragged item, for example by the system. */
    fun onDragCancelled()

    /**
     * Computes the offset of an item at a given index.
     *
     * @param index the item's index
     */
    fun computeItemOffset(index: Int): Float

    /**
     * Called when the dragged item is placed, keeping it under the finger even when it is no longer visible. Needs the
     * list to be known with [updateListLayoutCoordinates].
     *
     * @param itemCoordinates The [LayoutCoordinates] of the dragged item, not moved to follow the finger.
     */
    fun onDraggedItemPositioned(itemCoordinates: LayoutCoordinates)

    /**
     * Called when the list is placed, to find where the dragged item is laid out in it.
     *
     * @param coordinates The [LayoutCoordinates] of the list.
     */
    fun updateListLayoutCoordinates(coordinates: LayoutCoordinates)

    /**
     * Returns the offset that draws the item with [previousKeyOfDraggedItem] where it was let go of, shrinking to zero
     * as it animates to where the list lays it out. Read while placing the item, to use its current position in the
     * list.
     */
    fun computeDroppedItemOffset(): Float

    /** Called to indicate to the list that the drop handling has been completed and the state can be reset. */
    fun reset()
}

/**
 * Class containing details about the current state of dragging in list.
 *
 * @param listState State of the list.
 * @param scope [CoroutineScope] used for scrolling to the target item.
 * @param hapticFeedback [HapticFeedback] used for performing haptic feedback on item long press.
 * @param touchSlop Distance in pixels the user can wander until we consider they started dragging.
 * @param ignoredItems Set of keys for items that cannot be dragged and that other items cannot be moved next to or
 *   dropped onto.
 * @param itemInteractionHandler Handler for item interactions.
 * @param dragAndDropEnabled Whether the drag and drop feature is enabled for this list.
 * @param liveReorderEnabled Whether reordering should happen 'live' while a drag is active.
 * @param onLongPress Optional callback to be invoked when long pressing an item.
 * @param fixedItems Set of keys for items that cannot be dragged but that other items can be moved next to or dropped
 *   onto.
 * @param scrollZoneHeight Height in pixels of the areas at the start and at the end of the list in which the center of
 *   a dragged item scrolls it, once the item went past the edge of the list there. `null` for a third of the first
 *   visible item other than the dragged one, whether the dragged item went past the edge or not.
 * @param interceptInteractionMode Called with the dragged item and the interaction found for it, whenever the item is
 *   dragged, to return the interaction to use instead.
 */
internal class ListInteractionStateImpl(
    private val listState: LazyListState,
    private val scope: CoroutineScope,
    private val hapticFeedback: HapticFeedback,
    private val touchSlop: Float,
    var ignoredItems: Set<Any>,
    var itemInteractionHandler: ItemInteractionHandler,
    var dragAndDropEnabled: Boolean,
    var liveReorderEnabled: Boolean,
    var onLongPress: (LazyListItemInfo) -> Unit = {},
    var fixedItems: Set<Any> = emptySet(),
    var scrollZoneHeight: Float? = null,
    var interceptInteractionMode: (InteractionState.List.Active, InteractionMode.List) -> InteractionMode.List =
        { _, mode ->
            mode
        },
) : ListInteractionState {
    override var draggedItem by mutableStateOf<InteractionState.List>(InteractionState.List.None)
        private set

    override var hoveredItem by mutableStateOf<InteractionState.List>(InteractionState.List.None)
        private set

    override var highlightedRect by mutableStateOf<Rect?>(null)
        private set

    override var interactionMode by mutableStateOf<InteractionMode.List>(InteractionMode.List.None)
        private set

    private var scrollJob: Job? = null

    internal var moved by mutableStateOf(false)
        private set

    override var previousKeyOfDraggedItem by mutableStateOf<Any?>(null)
        private set

    override val orientation: Orientation
        get() = listState.layoutInfo.orientation

    private var listLayoutCoordinates: LayoutCoordinates? = null

    // Where the dragged item is laid out in the list, also when it is not visible. Observed to redraw the item when the
    // list lays it out somewhere else.
    private var draggedItemLayoutOffset by mutableStateOf<Float?>(null)

    // Where the item was let go of in the list, while it is animated to its place.
    private var droppedItemOffset = 0f

    // Drop animation progress: 1 when the item is let go of, 0 once it is in its place.
    private var dropProgress by mutableFloatStateOf(0f)

    private var dropAnimationJob: Job? = null

    override fun computeItemOffset(index: Int): Float =
        offsetFromLayout(listState.layoutInfo.visibleItemsInfo.firstOrNull { info -> info.index == index })

    private fun computeDraggedItemOffset(): Float = offsetFromLayout(draggedItem.getLazyListItemInfo(listState))

    /** Offset of the dragged item from where the list lays out [itemInfo], to follow the finger. */
    private fun offsetFromLayout(itemInfo: LazyListItemInfo?): Float {
        val layoutOffset = itemInfo?.offset?.toFloat() ?: draggedItemLayoutOffset ?: 0f
        return draggedItem.initialOffset + draggedItem.cumulatedOffset - layoutOffset
    }

    override fun onDraggedItemPositioned(itemCoordinates: LayoutCoordinates) {
        val listCoordinates = listLayoutCoordinates?.takeIf { it.isAttached } ?: return
        val positionInList = listCoordinates.localPositionOf(itemCoordinates, Offset.Zero)
        val mainAxisPosition =
            when (orientation) {
                Orientation.Vertical -> positionInList.y
                Orientation.Horizontal -> positionInList.x
            }
        // The offsets of the list items do not include the content padding before the first item.
        draggedItemLayoutOffset = mainAxisPosition - listState.layoutInfo.beforeContentPadding
    }

    override fun updateListLayoutCoordinates(coordinates: LayoutCoordinates) {
        listLayoutCoordinates = coordinates
    }

    override fun computeDroppedItemOffset(): Float {
        val droppedItemInfo = listState.layoutInfo.visibleItemsInfo.firstOrNull { it.key == previousKeyOfDraggedItem }
        return droppedItemInfo?.let { dropProgress * (droppedItemOffset - it.offset) } ?: 0f
    }

    /** Animate the dropped item from where it was let go of to where the list lays it out. */
    private fun animateDroppedItem() {
        dropAnimationJob?.cancel()
        dropAnimationJob = scope.launch {
            animate(
                initialValue = 1f,
                targetValue = 0f,
                animationSpec = spring(stiffness = Spring.StiffnessMediumLow, visibilityThreshold = 0.001f),
            ) { progress, _ ->
                dropProgress = progress
            }
            previousKeyOfDraggedItem = null
        }
    }

    override fun onTouchSlopPassed(offset: Float, shouldLongPress: Boolean) {
        listState
            .findItem(offset)
            ?.takeIf { it.key !in ignoredItems && it.key !in fixedItems }
            ?.also { item ->
                if (shouldLongPress) {
                    hapticFeedback.performHapticFeedback(HapticFeedbackType.LongPress)
                    onLongPress(item)
                }
                draggedItem =
                    InteractionState.List.Active(
                        index = item.index,
                        key = item.key,
                        initialOffset = item.offset.toFloat(),
                        size = item.size,
                    )
            }
    }

    override fun reset() {
        resetState()
    }

    override fun onDragEnd() {
        if (draggedItem is InteractionState.List.Active) {
            handleDragEnd(interactionMode)
        }
    }

    private fun doReorder(mode: InteractionMode.List.Reordering) {
        if (draggedItem.getLazyListItemInfo(listState)?.index == listState.firstVisibleItemIndex) {
            autoScroll(mode.source.size.toFloat())
        }
        itemInteractionHandler.onMove(
            sourceKey = mode.source.key,
            targetKey = mode.target.key,
            placeAfter = mode.placeAfter,
        )
    }

    private fun handleDragEnd(mode: InteractionMode.List) {
        when (mode) {
            is InteractionMode.List.DragAndDrop -> {
                itemInteractionHandler.onDrop(
                    sourceKey = mode.source.key,
                    targetKey = mode.target.key,
                )
            }

            is InteractionMode.List.Reordering -> {
                if (!liveReorderEnabled) {
                    doReorder(mode)
                }
                itemInteractionHandler.onDragEnd()
                resetState()
            }

            is InteractionMode.List.Scroll,
            is InteractionMode.List.None -> {
                // No action is taken
                if (moved) {
                    itemInteractionHandler.onDragEnd()
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

    private fun resetState() {
        if (draggedItem is InteractionState.List.Active) {
            previousKeyOfDraggedItem = draggedItem.key
            droppedItemOffset = draggedItem.initialOffset + draggedItem.cumulatedOffset
            // Set right away, for the item to be drawn where it was let go of in the frame in which the list moves it.
            dropProgress = 1f
            animateDroppedItem()
        }
        draggedItem = InteractionState.List.None
        hoveredItem = InteractionState.List.None
        highlightedRect = null
        interactionMode = InteractionMode.List.None
        draggedItemLayoutOffset = null
        moved = false
        scrollJob?.cancel()
        scrollJob = null
    }

    /**
     * Restricts all scroll actions to run in a single job. If a scroll job is currently executing when a new one is
     * asked for, it is cancelled.
     */
    private fun autoScroll(amount: Float) {
        scrollJob?.cancel()
        scrollJob = scope.launch {
            listState.scrollBy(amount)
        }
    }

    private fun handleReorderingModeOnDrag(mode: InteractionMode.List.Reordering) {
        hoveredItem = InteractionState.List.None
        if (liveReorderEnabled) {
            doReorder(mode)
        } else {
            highlightedRect = mode.rect
        }
    }

    private fun handleDragAndDropModeOnDrag(mode: InteractionMode.List.DragAndDrop) {
        highlightedRect = null
        if (hoveredItem != mode.target) {
            hoveredItem = mode.target
        }
    }

    private fun handleNoInteractionModeOnDrag(itemOffset: ListItemOffset) {
        highlightedRect = null
        hoveredItem = InteractionState.List.None
        scrollPastEdge(itemOffset)
    }

    /** Scroll the list if the dragged item went past its edge. */
    private fun scrollPastEdge(itemOffset: ListItemOffset) {
        val overscroll =
            findOverscroll(
                draggedItem = draggedItem,
                itemOffset = itemOffset,
                listState = listState,
            )
        if (overscroll != 0f) {
            autoScroll(overscroll)
        }
    }

    override fun onDrag(offset: Float, preserveSelectMode: Boolean) {
        draggedItem = draggedItem.incrementCumulatedOffset(offset)
        if (!moved && abs(draggedItem.cumulatedOffset) > touchSlop) {
            draggedItem = draggedItem.markAsMoved()
            (draggedItem as? InteractionState.List.Active)?.let { active ->
                itemInteractionHandler.onDragStart(
                    sourceKey = active.key,
                    preserveSelectMode = preserveSelectMode,
                )
            }
            moved = true
        }
        val draggingItem = draggedItem as? InteractionState.List.Active ?: return
        val itemOffset =
            ListItemOffset(
                draggedItem = draggingItem,
                draggingItemOffset = computeDraggedItemOffset(),
                itemSize = draggingItem.size,
            )
        val foundMode =
            determineInteractionMode(
                listState = listState,
                draggedItem = draggedItem,
                itemOffset = itemOffset,
                ignoredItems = ignoredItems,
                dragAndDropEnabled = dragAndDropEnabled,
                scrollZoneHeight = scrollZoneHeight,
            )
        val mode = interceptInteractionMode(draggingItem, foundMode)
        // An item dragged past the edge of the list scrolls it, also when it is retargeted to be dropped somewhere.
        val wouldScroll = foundMode is InteractionMode.List.Scroll || foundMode is InteractionMode.List.None
        if (wouldScroll && (mode is InteractionMode.List.Reordering || mode is InteractionMode.List.DragAndDrop)) {
            scrollPastEdge(itemOffset)
        }
        // Debounce duplicate reorder events.
        // Note that multiple duplicate scroll events are valid
        if (mode == interactionMode && interactionMode is InteractionMode.List.Reordering) {
            return
        }
        interactionMode = mode
        when (mode) {
            is InteractionMode.List.DragAndDrop -> {
                handleDragAndDropModeOnDrag(mode = mode)
            }

            is InteractionMode.List.Reordering -> {
                handleReorderingModeOnDrag(mode = mode)
            }

            is InteractionMode.List.None,
            is InteractionMode.List.Scroll -> {
                handleNoInteractionModeOnDrag(itemOffset = itemOffset)
            }
        }
    }
}

private fun determineInteractionMode(
    listState: LazyListState,
    draggedItem: InteractionState.List,
    itemOffset: ListItemOffset,
    ignoredItems: Set<Any>,
    dragAndDropEnabled: Boolean,
    scrollZoneHeight: Float?,
): InteractionMode.List {
    if (listState.isScrollInProgress) return InteractionMode.List.None
    if (draggedItem is InteractionState.List.None) return InteractionMode.List.None

    val topCandidate =
        gatherCandidates(
                listState = listState,
                draggedItemOffset = itemOffset,
                draggedItem = draggedItem,
                ignoredItems = ignoredItems,
                scrollZoneHeight = scrollZoneHeight,
            )
            .filter { item ->
                // Filter out the drag and drop interaction type if it is disabled, as in private tabs
                dragAndDropEnabled || item.type !is InteractionType.Overlap
            }
            .minByOrNull { it.score }

    val key = topCandidate?.anchorItem?.key
    return when {
        topCandidate == null || key == null -> {
            InteractionMode.List.None
        }

        topCandidate.type is InteractionType.Overlap -> {
            InteractionMode.List.DragAndDrop(
                source = draggedItem as InteractionState.List.Active,
                target =
                    InteractionState.List.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toFloat(),
                        size = topCandidate.anchorItem.size,
                    ),
            )
        }

        topCandidate.type is InteractionType.TopGutter -> {
            InteractionMode.List.Reordering(
                source = draggedItem as InteractionState.List.Active,
                target =
                    InteractionState.List.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toFloat(),
                        size = topCandidate.anchorItem.size,
                    ),
                placeAfter = false,
                rect = topCandidate.type.rect,
            )
        }

        topCandidate.type is InteractionType.BottomGutter -> {
            InteractionMode.List.Reordering(
                source = draggedItem as InteractionState.List.Active,
                target =
                    InteractionState.List.Active(
                        key = key,
                        index = topCandidate.anchorItem.index,
                        initialOffset = topCandidate.anchorItem.offset.toFloat(),
                        size = topCandidate.anchorItem.size,
                    ),
                placeAfter = true,
                rect = topCandidate.type.rect,
            )
        }

        topCandidate.type is InteractionType.Scroll -> {
            InteractionMode.List.Scroll(topCandidate.type.scroll)
        }

        else -> InteractionMode.List.None
    }
}

/**
 * Calculates the distance from the closest point on a [Rect] object to a given point in space represented as an
 * [Offset]. Uses getDistanceSquared() for performance reasons, which is appropriate for comparisons to other distances
 * calculated with the same method. Returns a float value representing the distance.
 *
 * @param offset: [Offset] representing a comparison point in spce.
 */
@androidx.annotation.VisibleForTesting
internal fun Rect.closestDistanceTo(offset: Float): Float {
    return (this.closestPointTo(offset) - offset).pow(2f)
}

internal fun Rect.closestPointTo(offset: Float): Float {
    return offset.coerceIn(this.top, this.bottom)
}

private fun getScrollCandidates(
    listState: LazyListState,
    draggedItemOffset: ListItemOffset,
    scrollZoneHeight: Float?,
): List<ListInteractionCandidate> {
    val visibleItems = listState.layoutInfo.visibleItemsInfo
    val firstVisible = visibleItems.firstOrNull() ?: return emptyList()
    // The dragged item may take no space, so it is not used to size the areas.
    val sizingItem = visibleItems.firstOrNull { it.key != draggedItemOffset.draggedItem.key } ?: firstVisible
    val candidates = mutableListOf<ListInteractionCandidate>()
    val scrollRectSize =
        Size(
            width = listState.layoutInfo.viewportSize.width.toFloat(),
            height = scrollZoneHeight ?: (sizingItem.size / 3.0f),
        )
    // Given a height, the areas only scroll when the dragged item went past the edge of the list, as that scrolls it.
    val overscroll = scrollZoneHeight?.let {
        findOverscroll(draggedItemOffset.draggedItem, draggedItemOffset, listState)
    }
    // Scroll up can only be a candidate if we are not at the top of the view
    if (listState.firstVisibleItemIndex > 0 && (overscroll == null || overscroll < 0f)) {
        val scrollUpRect =
            Rect(
                offset = Offset(0f, 0f),
                size = scrollRectSize,
            )
        if (scrollUpRect.bottom > draggedItemOffset.start) {
            candidates.add(
                ListInteractionCandidate(
                    type = InteractionType.Scroll(scroll = draggedItemOffset.start.minus(scrollUpRect.bottom)),
                    anchorItem = firstVisible,
                    score = scrollUpRect.closestDistanceTo(draggedItemOffset.center),
                )
            )
        }
    }
    // Scroll down can only be a candidate if we are not at the bottom of the view
    val lastVisible = listState.layoutInfo.visibleItemsInfo.lastOrNull() ?: return candidates
    if (
        lastVisibleItemIndex(listState) < listState.layoutInfo.totalItemsCount - 1 &&
            (overscroll == null || overscroll > 0f)
    ) {
        val scrollDownRect =
            Rect(
                offset = Offset(0f, listState.layoutInfo.viewportSize.height - scrollRectSize.height),
                size = scrollRectSize,
            )
        if (scrollDownRect.top < draggedItemOffset.end) {
            candidates.add(
                ListInteractionCandidate(
                    type = InteractionType.Scroll(scroll = draggedItemOffset.end.minus(scrollDownRect.top)),
                    anchorItem = lastVisible,
                    score = scrollDownRect.closestDistanceTo(draggedItemOffset.center),
                )
            )
        }
    }
    return candidates
}

private fun lastVisibleItemIndex(listState: LazyListState): Int {
    return listState.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: 0
}

internal fun gatherCandidates(
    listState: LazyListState,
    draggedItemOffset: ListItemOffset,
    draggedItem: InteractionState,
    ignoredItems: Set<Any>,
    scrollZoneHeight: Float? = null,
): List<ListInteractionCandidate> {
    val candidates = mutableListOf<ListInteractionCandidate>()
    candidates.addAll(
        elements =
            getScrollCandidates(
                listState = listState,
                draggedItemOffset = draggedItemOffset,
                scrollZoneHeight = scrollZoneHeight,
            )
    )
    for (itemCandidate in listState.layoutInfo.visibleItemsInfo) {
        if (itemCandidate.key in ignoredItems || itemCandidate.key == draggedItem.key) continue
        val gutterSpacing = listState.layoutInfo.mainAxisItemSpacing
        val horizontalGutterSize =
            Size(
                width = listState.layoutInfo.viewportSize.width.toFloat(),
                height = gutterSpacing.toFloat(),
            )

        // body candidate
        val body =
            Rect(
                offset = Offset(x = 0f, y = itemCandidate.offset.toFloat()),
                size =
                    Size(
                        width = listState.layoutInfo.viewportSize.width.toFloat(),
                        height = itemCandidate.size.toFloat(),
                    ),
            )
        val distanceToCenter = (body.center.y - draggedItemOffset.center).pow(2)
        candidates.add(
            ListInteractionCandidate(
                type = InteractionType.Overlap,
                anchorItem = itemCandidate,
                score = distanceToCenter,
            )
        )

        // top gutter candidate
        val topGutter =
            Rect(
                offset =
                    Offset(
                        x = 0f,
                        y = itemCandidate.offset.toFloat(),
                    ),
                size = horizontalGutterSize,
            )
        candidates.add(
            ListInteractionCandidate(
                type = InteractionType.TopGutter(rect = topGutter),
                anchorItem = itemCandidate,
                score = topGutter.closestDistanceTo(draggedItemOffset.center),
            )
        )

        // bottom gutter candidate
        val bottomGutter =
            Rect(
                offset =
                    Offset(
                        x = 0f,
                        y = itemCandidate.endOffset.toFloat(),
                    ),
                size = horizontalGutterSize,
            )
        candidates.add(
            ListInteractionCandidate(
                type = InteractionType.BottomGutter(rect = bottomGutter),
                anchorItem = itemCandidate,
                score = bottomGutter.closestDistanceTo(draggedItemOffset.center),
            )
        )
    }
    return candidates
}

private fun findOverscroll(
    draggedItem: InteractionState.List,
    itemOffset: ListItemOffset,
    listState: LazyListState,
): Float {
    return when {
        draggedItem.cumulatedOffset > 0 -> (itemOffset.end - listState.layoutInfo.viewportEndOffset).coerceAtLeast(0f)

        draggedItem.cumulatedOffset < 0 ->
            (itemOffset.start - listState.layoutInfo.viewportStartOffset).coerceAtMost(0f)

        else -> 0f
    }
}

/**
 * Container for draggable list item. Keeps the item composed while it is dragged, also when it is scrolled out of view.
 *
 * @param state List reordering state.
 * @param key Key of the item to be displayed.
 * @param position Position in the list of the item to be displayed.
 * @param modifier [Modifier] for where the item is laid out, not moved to follow the finger while dragged.
 * @param enteringItemKey The key of the item entering composition, if any. Can be null.
 * @param content Content of the item to be displayed.
 */
@Composable
fun LazyItemScope.InteractableDragItemContainer(
    state: ListInteractionState,
    key: Any,
    position: Int,
    modifier: Modifier = Modifier,
    enteringItemKey: Any? = null,
    content: @Composable (itemInteractionState: ItemInteractionState) -> Unit,
) {
    // Keeps the dragged item composed when it is scrolled out of view, to keep it under the finger.
    val isDragged by remember(state, key) { derivedStateOf { state.draggedItem.key == key } }
    PinnedWhile(isDragged)
    /*
     * The outer box tells where the dragged item is laid out, to keep it under the finger also when it is no longer
     * visible. graphicsLayer translations are included in LayoutCoordinates measurements, so the translations
     * following the finger happen in an inner box.
     */
    Box(
        modifier =
            modifier
                .zIndex(
                    if (key == state.draggedItem.key || key == state.previousKeyOfDraggedItem) {
                        Elevation.DRAGGED_ITEM
                    } else {
                        Elevation.NO_INTERACTION
                    }
                )
                .reportDragPlacement(state = state, key = key)
                .defaultListItemAnimation(
                    lazyListItemScope = this,
                    // Skip default animations during group entrance and while an item is dragged or animated after a
                    // drop.
                    suppressTransitions =
                        enteringItemKey != null ||
                            key == state.draggedItem.key ||
                            key == state.previousKeyOfDraggedItem,
                ),
        propagateMinConstraints = true,
    ) {
        Box(
            modifier = Modifier.dragTranslation(state = state, key = key, position = position),
            propagateMinConstraints = true,
        ) {
            content(
                ItemInteractionState(
                    isHoveredByItem = key == state.hoveredItem.key,
                    isDragged = key == state.draggedItem.key,
                    isEntering = key == enteringItemKey,
                )
            )
        }
    }
}

/**
 * Tell [state] where the item with [key] is placed while it is dragged. Other items are not observed.
 *
 * @param state List reordering state.
 * @param key Key of the item.
 */
private fun Modifier.reportDragPlacement(state: ListInteractionState, key: Any): Modifier =
    if (key == state.draggedItem.key) this.onGloballyPositioned(state::onDraggedItemPositioned) else this

/**
 * Move the item with [key] to follow the finger while it is dragged, and from where it was let go of while it is
 * animated to its place.
 *
 * The dragged item is translated when drawing. After it is let go of, its offset is instead computed when placing it,
 * so that a move by the list cannot make it jump for one frame.
 *
 * @param state List reordering state.
 * @param key Key of the item.
 * @param position Position in the list of the item.
 */
private fun Modifier.dragTranslation(state: ListInteractionState, key: Any, position: Int): Modifier =
    when (key) {
        state.draggedItem.key -> {
            this.graphicsLayer {
                when (state.orientation) {
                    Orientation.Vertical -> translationY = state.computeItemOffset(position)
                    Orientation.Horizontal -> translationX = state.computeItemOffset(position)
                }
            }
        }

        state.previousKeyOfDraggedItem -> {
            this.absoluteOffset {
                val offset = state.computeDroppedItemOffset().roundToInt()
                when (state.orientation) {
                    Orientation.Vertical -> IntOffset(0, offset)
                    Orientation.Horizontal -> IntOffset(offset, 0)
                }
            }
        }

        else -> this
    }

/** Calculates the offset of an item taking its height into account. */
private val LazyListItemInfo.endOffset: Int
    get() = offset + size

/**
 * Find item based on position on screen.
 *
 * @param offset Position on screen used to find the item.
 */
private fun LazyListState.findItem(offset: Float) =
    layoutInfo.visibleItemsInfo.firstOrNull { item ->
        offset.toInt() in item.offset..item.endOffset
    }

/**
 * Detects press, long press and drag gestures. Also tells [interactionState] where the list is, so this has to be
 * applied to the list or to a node which starts where the list does.
 *
 * @param listState State of the list.
 * @param interactionState List interaction state used for dragging callbacks.
 * @param shouldLongPressToDrag Whether an item should be long pressed to start the dragging gesture.
 */
fun Modifier.detectListPressAndDrag(
    listState: LazyListState,
    interactionState: ListInteractionState,
    shouldLongPressToDrag: Boolean,
): Modifier =
    this then
        Modifier.onGloballyPositioned(interactionState::updateListLayoutCoordinates) then
        Modifier.pointerInput(listState, shouldLongPressToDrag) {
            if (shouldLongPressToDrag) {
                detectDragGesturesAfterLongPress(
                    onDragStart = { offset ->
                        val offsetInOrientation =
                            when (listState.layoutInfo.orientation) {
                                Orientation.Vertical -> offset.y
                                Orientation.Horizontal -> offset.x
                            }
                        interactionState.onTouchSlopPassed(offsetInOrientation, true)
                    },
                    onDrag = { change, dragAmount ->
                        change.consume()
                        val dragOffset =
                            when (listState.layoutInfo.orientation) {
                                Orientation.Vertical -> dragAmount.y
                                Orientation.Horizontal -> dragAmount.x
                            }
                        interactionState.onDrag(dragOffset, false)
                    },
                    onDragEnd = interactionState::onDragEnd,
                    onDragCancel = interactionState::onDragCancelled,
                )
            }
        }

/**
 * Data class that simplifies calculations of the center, start, and end of a list item.
 *
 * @property draggedItem The current item being dragged
 * @property draggingItemOffset The offset of the dragged item
 * @property itemSize The item's size, in Int
 */
data class ListItemOffset(
    val draggedItem: InteractionState.List.Active,
    val draggingItemOffset: Float,
    val itemSize: Int,
) {
    val start by lazy {
        draggedItem.initialOffset + draggedItem.cumulatedOffset
    }

    val end by lazy {
        start + itemSize
    }

    val center by lazy {
        start + (end - start) / 2f
    }
}

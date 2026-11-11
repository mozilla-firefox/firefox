# [Android Components](../../../README.md) > Compose > Draggable

Draggable, reorderable Compose lists and grids.

## Usage

This component can be used to create a list or grid that supports drag actions:
1. scrolling up or down in the grid while dragging an item to the bottom of the screen
2. reordering items, either:
   - 'live' so that the items swap as you drag one item over another
   - after release of the drag, with a line indicator for where the reorder action is hovering
3. dragging one item onto another (drag and drop), to merge an item or place it in a target zone.

This component supports lists (single-column) and grid (multi-column) layouts.

### Grid
Set up a grid interaction state:

```kotlin
val gridState = rememberLazyGridState()
val gridInteractionState =
    createGridInteractionState(
        gridState = gridState, // a compose affordance
        itemInteractionHandler = itemHandler, // handler interface for onMove, onDrop, etc, callbacks
        onLongPress = { _ -> }, // (LazyGridItemInfo) -> Unit to respond to a long press.
        liveReorderEnabled = false, // whether reordering should happen 'live' or after release
        ignoredItems = emptySet(), // a set of items in your list that can't be dragged or targeted
    )
```

Create a lazy grid in your compose view that passes in the interaction state.
You may wish to pass in collectionInfo here in the semantics block.

```kotlin
// Depending on your implementation, you may find it helpful to place the interactableGrid modifier
// on an outer box, or directly to the modifier inside the LazyVerticalGrid
Box(
    modifier = Modifier.interactableGrid(
        state = gridInteractionState,
        isInMultiSelectMode = isInMultiSelectMode,
    ),
) {
    LazyVerticalGrid(
        columns = GridCells.Fixed(count = columns),
        modifier = modifier
            .semantics {
                testTag = TEST_TAG
                collectionInfo = collectionInfo
            },
        state = gridState,
        userScrollEnabled = gridInteractionState.draggedItem == InteractionState.Grid.None,
        contentPadding = contentPadding,
        verticalArrangement = Arrangement.spacedBy(space = verticalPadding),
        horizontalArrangement = Arrangement.spacedBy(space = horizontalPadding),
    ) {
        // optionally
        renderHeaderItem()

        itemsIndexed(
            items = itemList,
            // it is very important to define unique, stable keys, because by default
            // the key is the position, which is updated if the dataset changes,
            // causing data loss and unnecessary recompositions.
            // see: https://developer.android.com/develop/ui/compose/lists
            key = { _, item -> item.id },
        ) { index, item ->
            // If you also support clicking on the items, you may want to track whether drag is active
            // and disable click listening when a drag is in progress.
            val shouldClickListen = gridInteractionState.draggedItem.key != item.id

            // Similarly, this component supports awareness of swipe-to-dismiss handling:
            // you can safely pass "false" to swipingActive if you do not want to support this.
            // rememberSwipeToDismissBoxState() is an app-specific call which you may re-implement.
            val swipeToDismissBoxState = rememberSwipeToDismissBoxState(id = item.id)
            val swipingActive by
                remember(swipeToDismissBoxState) {
                    derivedStateOf {
                        swipeToDismissBoxState.dismissDirection != SwipeToDismissBoxValue.Settled
                    }
                }

            // Wrap your items in the interactable container, which handles animations and global layout
            // coordinate tracking, and keeps the dragged item rendered when it is scrolled off the grid/list.
            InteractableDragItemContainer(
                state = gridInteractionState,
                position = index, // likely your grid index, but you may increment this if you have headers
                key = item.id,
                swipingActive = swipingActive,
                // optionally, you can declare when an item is entering composition
                // and define an animation to play
                enteringItemKey = keyOfItemEnteringComposition,
                enteringItemDecoration = { interactionState ->
                    Modifier.specialEntranceAnimation()
                },
            ) {
                renderListItem(shouldClickListen = shouldClickListen)
            }
        }

        // optionally
        renderFooterItem()
    }
}
```

### List

The list implementation is quite similar, but shown here for reference:
Set up a list interaction state:

```kotlin
val lazyListState = rememberLazyListState()
val listInteractionState =
    createListInteractionState(
        listState = lazyListState, // compose affordance
        ignoredItems = ignoredItems, // a set of items in your list that can't be dragged or targeted
        onLongPress = { _ -> }, // (LazyListItemInfo) -> Unit to respond to a long press.
        itemInteractionHandler = itemInteractionHandler, // respond to onMove, onDrop, etc, events
        dragAndDropEnabled = true, // should drag and drop (merging items) be enabled?
        liveReorderEnabled = false, // should reorders happen live or after the drag is released?
    )
```

Set up a LazyColumn with items wrapped in the InteractableDragItemContainer:
The list can similarly support some of the complexities described in the Grid implementation above.

```kotlin
Box(
    modifier = Modifier.detectListPressAndDrag(
        listState = lazyListState,
        interactionState = listInteractionState,
        shouldLongPressToDrag = true,
    ),
) {
    LazyColumn(state = lazyListState, verticalArrangement = Arrangement.spacedBy(verticalPadding)) {
        itemsIndexed(items = itemList, key = { _, item -> item.id }) { index, item ->
            InteractableDragItemContainer(
                state = listInteractionState,
                position = index,
                key = item.id,
            ) {
                renderItem()
            }
        }
    }
}
```

### Setting up the dependency

Use Gradle to download the library from [maven.mozilla.org](https://maven.mozilla.org/) ([Setup repository](../../../README.md#maven-repository)):

```Groovy
implementation "org.mozilla.components:compose-draggable:{latest-version}"
```

## License

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/

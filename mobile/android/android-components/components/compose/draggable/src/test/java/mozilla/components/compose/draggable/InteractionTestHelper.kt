/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListLayoutInfo
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.unit.IntSize
import org.mockito.stubbing.OngoingStubbing

fun fakeGridActiveState(key: String = "key"): InteractionState.Grid.Active {
    return InteractionState.Grid.Active(
        index = 0,
        key = key,
        size = IntSize(10, 10),
        initialOffset = Offset.Zero,
    )
}

fun fakeListActiveState(key: String = "key"): InteractionState.List.Active {
    return InteractionState.List.Active(
        index = 0,
        key = key,
        size = 10,
        initialOffset = 0f,
    )
}

fun fakeDraggedGridItemOffset(): GridItemOffset {
    val draggedItem =
        InteractionState.Grid.Active(
            index = 0,
            key = "key",
            size = IntSize(2, 2),
            initialOffset = Offset.Zero,
        )
    return GridItemOffset(
        draggedItem = draggedItem,
        draggingItemOffset = Offset.Zero,
        itemSize = draggedItem.size,
    )
}

fun fakeDraggedListItemOffset(): ListItemOffset {
    val draggedItem =
        InteractionState.List.Active(
            index = 0,
            key = "key",
            size = 2,
            initialOffset = 0f,
        )
    return ListItemOffset(
        draggedItem = draggedItem,
        draggingItemOffset = 0f,
        itemSize = draggedItem.size,
    )
}

/** [ItemInteractionHandler] that records every callback it receives, in order, in [calls]. */
class RecordingItemInteractionHandler : ItemInteractionHandler {
    sealed interface Call {
        data class Move(val sourceKey: Any, val targetKey: Any?, val placeAfter: Boolean) : Call

        data class Drop(val sourceKey: Any, val targetKey: Any) : Call

        data class DragStart(val sourceKey: Any, val preserveSelectMode: Boolean) : Call

        data object DragCancel : Call

        data object DragEnd : Call
    }

    val calls = mutableListOf<Call>()

    override fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean) {
        calls += Call.Move(sourceKey, targetKey, placeAfter)
    }

    override fun onDrop(sourceKey: Any, targetKey: Any) {
        calls += Call.Drop(sourceKey, targetKey)
    }

    override fun onDragCancel() {
        calls += Call.DragCancel
    }

    override fun onDragEnd() {
        calls += Call.DragEnd
    }

    override fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean) {
        calls += Call.DragStart(sourceKey, preserveSelectMode)
    }
}

/** [HapticFeedback] that records every feedback type it is asked to perform, in order, in [performed]. */
class RecordingHapticFeedback : HapticFeedback {
    val performed = mutableListOf<HapticFeedbackType>()

    override fun performHapticFeedback(hapticFeedbackType: HapticFeedbackType) {
        performed += hapticFeedbackType
    }
}

data class FakeListItemInfo(
    override val key: Any,
    override val index: Int,
    override val size: Int,
    override val offset: Int,
) : LazyListItemInfo

data class FakeListLayoutInfo(
    override val visibleItemsInfo: List<LazyListItemInfo>,
    override val totalItemsCount: Int,
    override val viewportStartOffset: Int = 0,
    override val viewportEndOffset: Int = 10,
    override val viewportSize: IntSize = IntSize(10, 10),
    override val mainAxisItemSpacing: Int = 10,
    override val beforeContentPadding: Int = 10,
) : LazyListLayoutInfo

/**
 * Stubs a call returning a Kotlin value class such as [IntSize] and returns the unboxed value. On the JVM a getter
 * returns the underlying primitive, so `thenReturn` will reject the boxed value.
 */
fun <T : Any> OngoingStubbing<T>.thenReturnUnboxed(value: T): OngoingStubbing<T> {
    val unboxed = value.javaClass.getMethod("unbox-impl").invoke(value)
    @Suppress("UNCHECKED_CAST")
    return thenAnswer { unboxed as T }
}

object TabKeys {
    const val HEADER = "header"
    const val TAB_ALPHA = "alpha"
    const val TAB_BETA = "beta"
    const val SPAN = "span"
}

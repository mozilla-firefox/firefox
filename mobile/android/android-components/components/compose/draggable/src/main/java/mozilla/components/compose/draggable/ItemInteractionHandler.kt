/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.draggable

/** Interface invoked to handle item interaction behavior */
interface ItemInteractionHandler {
    /**
     * Moves a source item with key [sourceKey] next to a destination item with key [targetKey].
     *
     * @param sourceKey Key of source item
     * @param targetKey Key of target item that the source will be placed next to.
     * @param placeAfter Whether the item should be placed before or after its target
     */
    fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean)

    /**
     * Drops a source item on a destination item
     *
     * @param sourceKey Key of source item
     * @param targetKey Key of target item
     */
    fun onDrop(sourceKey: Any, targetKey: Any)

    /** Called when an item drag is cancelled, and also when it ends if [onDragEnd] is not overridden. */
    fun onDragCancel()

    /** Called when the user lets go of a dragged item without dropping it onto another item. */
    fun onDragEnd() = onDragCancel()

    /**
     * Called when a drag starts
     *
     * @param sourceKey Key of the item being dragged.
     * @param preserveSelectMode Whether select mode should be preserved on a drag.
     */
    fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean)
}

/** No op [ItemInteractionHandler]. Intended for previews */
object NoOpItemInteractionHandler : ItemInteractionHandler {
    override fun onMove(sourceKey: Any, targetKey: Any?, placeAfter: Boolean) {
        // no op
    }

    override fun onDrop(sourceKey: Any, targetKey: Any) {
        // no op
    }

    override fun onDragCancel() {
        // no op
    }

    override fun onDragStart(sourceKey: Any, preserveSelectMode: Boolean) {
        // no op
    }
}

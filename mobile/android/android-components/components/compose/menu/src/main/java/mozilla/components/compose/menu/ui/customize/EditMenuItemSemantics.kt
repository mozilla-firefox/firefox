/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.stateDescription
import mozilla.components.compose.base.R as baseR
import mozilla.components.compose.base.text.value
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.ui.icons.R as iconsR

/**
 * Make a row of an item a single node for accessibility services, reading its name then, for an item shown in the menu,
 * its position among the shown items. Its content and drag handle are not separate nodes, and the dimmed content of a
 * hidden item is not read as disabled. An expandable item is read as a button with its expanded or collapsed state,
 * which toggles it when activated, and has the custom action toggling it too, then the items have the custom actions
 * moving, hiding or showing them.
 *
 * @param row The row of the item.
 * @param item The item the row shows.
 * @param actions What the rows do.
 */
@Composable
internal fun Modifier.editMenuItemSemantics(
    row: EditMenuItemsRow.Item,
    item: MenuItem,
    actions: EditMenuItemsActions,
): Modifier {
    val name = item.a11yName()
    val position = actions.items.shownPositionOf(row.key)
    val description =
        if (position == null) {
            name
        } else {
            stringResource(
                R.string.mozac_menu_customize_shown_item,
                name,
                position,
                actions.items.shownWithSubItemsCount,
            )
        }
    val expansionAction = if (item is ExpandableMenuItem) expansionAction(row, name, actions) else null
    val customActions =
        listOfNotNull(expansionAction) +
            editMenuItemActions(row, name, actions).mapNotNull { entry ->
                entry.result?.let { action ->
                    CustomAccessibilityAction(entry.label) {
                        if (action.isRefused) {
                            actions.onRejected(row.item.id)
                        } else {
                            actions.onMove(row.item.id, action.result, entry.announcement)
                        }
                        true
                    }
                }
            }
    val expansionState =
        stringResource(
            if (row.isExpanded) {
                baseR.string.mozac_compose_base_a11y_state_label_expanded
            } else {
                baseR.string.mozac_compose_base_a11y_state_label_collapsed
            }
        )

    return clearAndSetSemantics {
        contentDescription = description
        if (customActions.isNotEmpty()) this.customActions = customActions
        expansionAction?.let {
            role = Role.Button
            stateDescription = expansionState
            onClick(label = it.label, action = it.action)
        }
    }
}

/**
 * The actions moving, hiding or showing the item of [row], in the order the sheet opened from its drag handle lists
 * them in, with `null` [CustomizeMenuAction.result] for the ones the item cannot do now. Refused actions keep their
 * [EditMenuItemAction] result, for accessibility services to still offer them and tell the user why they did not run.
 */
@Composable
internal fun editMenuItemActions(
    row: EditMenuItemsRow.Item,
    name: String,
    actions: EditMenuItemsActions,
): List<CustomizeMenuAction<EditMenuItemAction>> {
    val available = actions.items.actionsOf(row.key).associateBy { it.kind }
    val moveIntoName = firstExpandableName(actions)
    val moveOutOfName = parentName(row, actions)
    return actions.items.sheetKindsOf(row.key).map { kind ->
        val action = available[kind]
        val targetName = if (kind == EditMenuItemAction.Kind.MOVE_INTO) moveIntoName else moveOutOfName
        CustomizeMenuAction(
            label = actionLabel(kind, targetName),
            icon = actionIcon(kind),
            result = action,
            announcement = if (action == null) "" else actionAnnouncement(row, name, action, targetName, actions),
        )
    }
}

@Composable
private fun actionLabel(kind: EditMenuItemAction.Kind, targetName: String): String =
    when (kind) {
        EditMenuItemAction.Kind.MOVE_UP -> stringResource(R.string.mozac_menu_customize_move_up)
        EditMenuItemAction.Kind.MOVE_DOWN -> stringResource(R.string.mozac_menu_customize_move_down)
        EditMenuItemAction.Kind.MOVE_INTO -> stringResource(R.string.mozac_menu_customize_move_into, targetName)
        EditMenuItemAction.Kind.MOVE_OUT_OF -> stringResource(R.string.mozac_menu_customize_move_out_of, targetName)
        EditMenuItemAction.Kind.HIDE -> stringResource(R.string.mozac_menu_customize_hide_item)
        EditMenuItemAction.Kind.UNHIDE -> stringResource(R.string.mozac_menu_customize_unhide_item)
    }

private fun actionIcon(kind: EditMenuItemAction.Kind): Int =
    when (kind) {
        EditMenuItemAction.Kind.MOVE_UP -> iconsR.drawable.mozac_ic_chevron_up_24
        EditMenuItemAction.Kind.MOVE_DOWN -> iconsR.drawable.mozac_ic_chevron_down_24
        EditMenuItemAction.Kind.MOVE_INTO,
        EditMenuItemAction.Kind.MOVE_OUT_OF -> iconsR.drawable.mozac_ic_folder_arrow_right_24
        EditMenuItemAction.Kind.HIDE -> iconsR.drawable.mozac_ic_eye_slash_24
        EditMenuItemAction.Kind.UNHIDE -> iconsR.drawable.mozac_ic_eye_24
    }

@Composable
private fun actionAnnouncement(
    row: EditMenuItemsRow.Item,
    name: String,
    action: EditMenuItemAction,
    targetName: String,
    actions: EditMenuItemsActions,
): String =
    when (action.kind) {
        EditMenuItemAction.Kind.MOVE_UP,
        EditMenuItemAction.Kind.MOVE_DOWN -> movedAnnouncement(row, name, action, actions)
        EditMenuItemAction.Kind.MOVE_INTO ->
            stringResource(R.string.mozac_menu_customize_item_moved_into, name, targetName)
        EditMenuItemAction.Kind.MOVE_OUT_OF ->
            stringResource(R.string.mozac_menu_customize_item_moved_out_of, name, targetName)
        EditMenuItemAction.Kind.HIDE -> stringResource(R.string.mozac_menu_customize_item_hidden, name)
        EditMenuItemAction.Kind.UNHIDE -> stringResource(R.string.mozac_menu_customize_item_shown, name)
    }

@Composable
private fun firstExpandableName(actions: EditMenuItemsActions): String =
    actions.items.firstShownExpandable?.item?.a11yName().orEmpty()

@Composable
private fun parentName(row: EditMenuItemsRow.Item, actions: EditMenuItemsActions): String =
    actions.items.shown.firstOrNull { it.id == row.parentId }?.item?.a11yName().orEmpty()

@Composable
private fun movedAnnouncement(
    row: EditMenuItemsRow.Item,
    name: String,
    action: EditMenuItemAction,
    actions: EditMenuItemsActions,
): String {
    val moved = action.result
    val parentId = row.parentId
    return if (parentId == null) {
        stringResource(
            R.string.mozac_menu_customize_item_moved,
            name,
            checkNotNull(moved.shownPositionOf(row.key)),
            moved.shownWithSubItemsCount,
        )
    } else {
        stringResource(
            R.string.mozac_menu_customize_sub_item_moved,
            name,
            checkNotNull(moved.shownSubItemPositionOf(parentId, row.key)),
            moved.shownSubItemsCountOf(parentId),
            parentName(row, actions),
        )
    }
}

@Composable
private fun expansionAction(
    row: EditMenuItemsRow.Item,
    name: String,
    actions: EditMenuItemsActions,
): CustomAccessibilityAction {
    val label =
        stringResource(
            if (row.isExpanded) R.string.mozac_menu_customize_collapse else R.string.mozac_menu_customize_expand
        )
    val announcement =
        stringResource(
            if (row.isExpanded) {
                R.string.mozac_menu_customize_item_collapsed
            } else {
                R.string.mozac_menu_customize_item_expanded
            },
            name,
        )
    return CustomAccessibilityAction(label) {
        actions.onExpansionToggle(row.item.id)
        actions.announce(announcement)
        true
    }
}

/** The name of this item read by accessibility services, with its summary, if any, unless it has a description. */
@Composable
internal fun MenuItem.a11yName(): String {
    contentDescription?.let {
        return it.value
    }
    val titleText = title.text.value
    val summaryText = summary?.text?.value?.takeIf { it.isNotBlank() } ?: return titleText
    return "$titleText, $summaryText"
}

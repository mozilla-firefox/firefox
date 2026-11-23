/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.menu.AnchoredDropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.icons.R as iconsR

/**
 * A dropdown form field that displays a contextual menu to select an item to populate the field.
 *
 * @param label Text to be displayed above the dropdown.
 * @param placeholder The text to be displayed when no [dropdownItems] are selected.
 * @param dropdownItems The [MenuItem.CheckableItem]s that should be shown when the dropdown is expanded.
 * @param modifier Modifier to be applied to the dropdown layout.
 */
@Composable
fun Dropdown(
    label: String,
    placeholder: String,
    dropdownItems: List<MenuItem.CheckableItem>,
    modifier: Modifier = Modifier,
) {
    val checkedItemText by
        remember(dropdownItems) {
            derivedStateOf {
                dropdownItems.find { it.isChecked }?.text
            }
        }

    val displayText = checkedItemText?.value ?: placeholder

    var expanded by remember { mutableStateOf(false) }

    val interactionSource = remember { MutableInteractionSource() }

    val inputTextColor =
        if (checkedItemText == null) {
            MaterialTheme.colorScheme.onSurfaceVariant
        } else {
            MaterialTheme.colorScheme.onSurface
        }

    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it },
        modifier = modifier.semantics(mergeDescendants = true) { role = Role.DropdownList },
    ) {
        Box(
            modifier = Modifier.menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable).fillMaxWidth(),
            propagateMinConstraints = true,
        ) {
            OutlinedTextFieldDefaults.DecorationBox(
                value = displayText,
                innerTextField = {
                    Text(
                        text = displayText,
                        color = inputTextColor,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        style = AcornTheme.typography.body1,
                    )
                },
                enabled = true,
                singleLine = true,
                visualTransformation = @Suppress("DEPRECATION") VisualTransformation.None,
                interactionSource = interactionSource,
                label = {
                    Text(
                        text = label,
                        style = AcornTheme.typography.caption,
                    )
                },
                trailingIcon = {
                    Icon(
                        painter = painterResource(id = iconsR.drawable.mozac_ic_chevron_down_24),
                        contentDescription = null,
                    )
                },
                colors =
                    OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = MaterialTheme.colorScheme.outline,
                        unfocusedBorderColor = MaterialTheme.colorScheme.outline,
                        focusedLabelColor = MaterialTheme.colorScheme.onSurfaceVariant,
                        unfocusedLabelColor = MaterialTheme.colorScheme.onSurfaceVariant,
                        focusedTrailingIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                        unfocusedTrailingIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    ),
            )
        }

        AnchoredDropdownMenu(
            menuItems = dropdownItems,
            expanded = expanded,
            onDismissRequest = { expanded = false },
        )
    }
}

private fun getDropdownItems(): List<Text> =
    List(DEFAULT_PREVIEW_DROPDOWN_ITEMS) { index -> Text.String("Item $index") }

private fun getSelectedDropdownItems(): List<Text> =
    listOf(
        Text.String("Item 1"),
        Text.String("Item 2"),
        Text.String("Item 3"),
        Text.String("Super super super long item exceeding width of small width"),
        Text.String("Super super super super super super super super super long item exceeding width of medium width"),
        Text.String(
            "Super super super super super super super super super super super " +
                "super super super super super super super super super super long item exceeding " +
                "width of large width"
        ),
    )

@Composable
private fun StatefulDropdownPreview(
    label: String,
    placeholder: String,
    items: List<Text>,
    initialSelectedIndex: Int? = null,
) {
    var selectedIndex by remember { mutableStateOf(initialSelectedIndex) }

    Dropdown(
        label = label,
        placeholder = placeholder,
        dropdownItems =
            items.mapIndexed { index, text ->
                MenuItem.CheckableItem(
                    text = text,
                    isChecked = index == selectedIndex,
                    onClick = { selectedIndex = index },
                )
            },
        modifier = Modifier.fillMaxWidth(),
    )
}

@FlexibleWindowLightDarkPreview
@Composable
private fun DropdownPreview() {
    AcornTheme {
        Surface {
            Column(modifier = Modifier.fillMaxSize()) {
                Spacer(modifier = Modifier.height(AcornTheme.layout.space.dynamic150))

                StatefulDropdownPreview(
                    label = "Placeholder and nothing selected",
                    placeholder = "Placeholder",
                    items = getDropdownItems(),
                )

                Spacer(modifier = Modifier.height(AcornTheme.layout.space.dynamic150))

                StatefulDropdownPreview(
                    label = "Placeholder and item selected",
                    placeholder = "Placeholder",
                    items = getSelectedDropdownItems(),
                    initialSelectedIndex = 0,
                )

                Spacer(modifier = Modifier.height(AcornTheme.layout.space.dynamic150))

                StatefulDropdownPreview(
                    label = "Long placeholder",
                    placeholder = "Long Long Long Long Long Long Long Long Long Long Placeholder",
                    items = getDropdownItems(),
                )
            }
        }
    }
}

private const val DEFAULT_PREVIEW_DROPDOWN_ITEMS = 10

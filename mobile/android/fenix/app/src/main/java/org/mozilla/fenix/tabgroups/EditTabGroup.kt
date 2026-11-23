/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.SpringSpec
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.LocalTextSelectionColors
import androidx.compose.foundation.text.selection.TextSelectionColors
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.SheetValue
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.delay
import mozilla.components.compose.base.BottomSheetHandle
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.theme.AcornCorners
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.theme.layout.AcornWindowSize.Companion.isLargeWindow
import org.mozilla.fenix.R
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.tabstray.TabsTrayTestTag.BOTTOM_SHEET_COLOR_LIST
import org.mozilla.fenix.tabstray.TabsTrayTestTag.EDIT_BOTTOM_SHEET_SAVE
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.theme.FirefoxTheme

private val formFieldShape: Shape
    @Composable get() = MaterialTheme.shapes.large
private const val COLOR_PICKER_MAX_ITEMS_PER_ROW = 5
internal const val MAX_TAB_GROUP_NAME_LENGTH = 256
private val FOCUS_REQUEST_DELAY = 50.milliseconds

/**
 * Prompt to edit a tab group.
 *
 * @param formState The current snapshot of [TabGroupFormState].
 * @param onTabGroupNameChange Invoked when the tab group's name updates.
 * @param onTabGroupThemeChange Invoked when the tab group's theme updates.
 * @param onConfirmSave Invoked when the clicks to save the tab group.
 */
@Composable
fun EditTabGroup(
    formState: TabGroupFormState,
    onTabGroupNameChange: (String) -> Unit,
    onTabGroupThemeChange: (TabGroupTheme) -> Unit,
    onConfirmSave: () -> Unit,
) {
    val title =
        stringResource(if (formState.inEditState) R.string.edit_tab_group_title else R.string.create_tab_group_title)

    val defaultName =
        stringResource(
            R.string.create_tab_group_form_default_name,
            formState.nextTabGroupNumber,
        )
    val initialName = formState.getInitialName(defaultName)

    var tabGroupName by remember {
        mutableStateOf(
            TextFieldValue(
                text = initialName,
                selection = TextRange(0, initialName.length),
            )
        )
    }

    // In create mode, the visible default name is derived from a string resource.
    // Align the tab group form with the same value.
    LaunchedEffect(Unit) {
        if (!formState.inEditState) {
            onTabGroupNameChange(initialName)
        }
    }

    Column(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(vertical = FirefoxTheme.layout.space.static150),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = title,
                modifier = Modifier.weight(1f).padding(start = 24.dp),
                style = FirefoxTheme.typography.headline7,
            )

            FilledButton(
                text = stringResource(R.string.create_tab_group_save_button),
                modifier = Modifier.padding(end = FirefoxTheme.layout.space.static150).testTag(EDIT_BOTTOM_SHEET_SAVE),
                onClick = onConfirmSave,
            )
        }

        Surface(
            shape = formFieldShape,
            color = MaterialTheme.colorScheme.surfaceBright,
            modifier = Modifier.fillMaxWidth().padding(horizontal = FirefoxTheme.layout.space.dynamic200),
        ) {
            TabGroupNameTextField(
                tabGroupName = tabGroupName,
                onTabGroupNameChange = { newName ->
                    tabGroupName = newName
                    onTabGroupNameChange(newName.text)
                },
                modifier =
                    Modifier.fillMaxWidth()
                        .testTag(TabsTrayTestTag.GROUP_NAME)
                        .padding(horizontal = 24.dp, vertical = 16.dp),
            )
        }

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static300))

        TabGroupColorPicker(theme = formState.theme, onTabGroupThemeChange = onTabGroupThemeChange)

        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static150))
    }
}

@Composable
private fun TabGroupColorPicker(theme: TabGroupTheme, onTabGroupThemeChange: (TabGroupTheme) -> Unit) {
    var selectedTheme by remember {
        mutableStateOf(theme.name)
    }

    Surface(
        shape = formFieldShape,
        color = MaterialTheme.colorScheme.surfaceBright,
        modifier = Modifier.fillMaxWidth().padding(horizontal = FirefoxTheme.layout.space.dynamic200),
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(vertical = FirefoxTheme.layout.space.static200),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(space = FirefoxTheme.layout.space.static200),
        ) {
            TabGroupTheme.entries.chunked(COLOR_PICKER_MAX_ITEMS_PER_ROW).forEach { themeRow ->
                Row(horizontalArrangement = Arrangement.spacedBy(space = FirefoxTheme.layout.space.static200)) {
                    themeRow.forEach { theme ->
                        TabGroupColorPickerItem(
                            theme = theme,
                            selected = theme.name == selectedTheme,
                            onClicked = { theme ->
                                selectedTheme = theme.name
                                onTabGroupThemeChange(theme)
                            },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun TabGroupColorPickerItem(
    theme: TabGroupTheme,
    selected: Boolean,
    onClicked: (TabGroupTheme) -> Unit,
) {
    // An object with a corner radius half its own size is a circle.
    // Animating from 8.dp to 1000.dp causes a janky UX
    val iconSize = if (isLargeWindow()) FirefoxTheme.layout.size.static600 else FirefoxTheme.layout.size.static400
    val circularRadius = iconSize / 2
    val animatedCorner by
        animateDpAsState(
            targetValue =
                if (selected) {
                    circularRadius
                } else {
                    AcornCorners.small
                },
            animationSpec = colorPickerAnimationSpec(),
        )
    val interactionSource = remember {
        MutableInteractionSource()
    }
    val outerBorderWidth by
        animateDpAsState(
            targetValue =
                if (selected) {
                    3.dp
                } else {
                    0.dp
                },
            animationSpec = colorPickerAnimationSpec(),
        )
    val innerBorderWidth by
        animateDpAsState(
            targetValue =
                if (selected) {
                    6.dp // 3dp is showing, half is covered by the outer border
                } else {
                    0.dp
                },
            animationSpec = colorPickerAnimationSpec(),
        )
    val contentLabel = theme.contentLabel

    Box(
        modifier =
            Modifier.size(iconSize + (FirefoxTheme.layout.space.static100 * 2))
                .padding(FirefoxTheme.layout.space.static100)
                .thenConditional(
                    Modifier.border(
                            outerBorderWidth,
                            color = MaterialTheme.colorScheme.primary,
                            shape = CircleShape,
                        )
                        .border(
                            innerBorderWidth,
                            color = MaterialTheme.colorScheme.surfaceBright,
                            shape = CircleShape,
                        ),
                    predicate = { selected },
                )
                .clip(shape = RoundedCornerShape(animatedCorner))
                .background(color = theme.primary)
                .testTag("$BOTTOM_SHEET_COLOR_LIST.${theme.name}")
                .clickable(
                    enabled = true,
                    interactionSource = interactionSource,
                    onClickLabel = contentLabel,
                    onClick = {
                        onClicked(theme)
                    },
                )
                .semantics(mergeDescendants = true) {
                    contentDescription = contentLabel
                    role = Role.Button
                }
    )
}

@Composable
private fun TabGroupNameTextField(
    tabGroupName: TextFieldValue,
    onTabGroupNameChange: (TextFieldValue) -> Unit,
    modifier: Modifier = Modifier,
) {
    val focusRequester = remember { FocusRequester() }
    val keyboardController = LocalSoftwareKeyboardController.current

    LaunchedEffect(Unit) {
        // On some devices (e.g. Samsung/HTC), the keyboard is not automatically triggered.
        // A small delay ensures the view is ready to receive focus and show the keyboard.
        delay(FOCUS_REQUEST_DELAY)
        focusRequester.requestFocus()
        keyboardController?.show()
    }

    val selectionColors =
        TextSelectionColors(
            handleColor = LocalTextSelectionColors.current.handleColor,
            backgroundColor = MaterialTheme.colorScheme.primaryContainer,
        )

    OutlinedTextField(
        value = tabGroupName,
        onValueChange = {
            onTabGroupNameChange(it.copy(text = it.text.take(MAX_TAB_GROUP_NAME_LENGTH)))
        },
        label = {
            Text(
                text = stringResource(R.string.create_tab_group_name_label),
                style = FirefoxTheme.typography.caption,
            )
        },
        singleLine = true,
        modifier = modifier.focusRequester(focusRequester),
        colors = OutlinedTextFieldDefaults.colors(selectionColors = selectionColors),
    )
}

private fun <T> colorPickerAnimationSpec(): SpringSpec<T> =
    spring(
        dampingRatio = Spring.DampingRatioNoBouncy,
        stiffness = Spring.StiffnessMedium,
    )

private class TabGroupFormStateParameterProvider : PreviewParameterProvider<TabGroupFormState> {
    val data =
        listOf(
            Pair(
                "Create tab group",
                TabGroupFormState(
                    tabGroupId = null,
                    name = "",
                    nextTabGroupNumber = 1,
                    edited = false,
                ),
            ),
            Pair(
                "Edit tab group",
                TabGroupFormState(
                    tabGroupId = "1",
                    name = "Test group",
                    edited = false,
                ),
            ),
            Pair(
                "Edit tab group with blank name",
                TabGroupFormState(
                    tabGroupId = "1",
                    name = "",
                    edited = true,
                ),
            ),
            Pair(
                "Edit tab group with first color selected",
                TabGroupFormState(
                    tabGroupId = "1",
                    name = "First color",
                    edited = false,
                    theme = TabGroupTheme.entries.first(),
                ),
            ),
            Pair(
                "Edit tab group with last color selected",
                TabGroupFormState(
                    tabGroupId = "1",
                    name = "Last color",
                    edited = false,
                    theme = TabGroupTheme.entries.last(),
                ),
            ),
        )

    override fun getDisplayName(index: Int): String {
        return data[index].first
    }

    override val values: Sequence<TabGroupFormState>
        get() = data.map { it.second }.asSequence()
}

@PreviewLightDark
@Composable
private fun EditTabGroupContentPreview(
    @PreviewParameter(TabGroupFormStateParameterProvider::class) formState: TabGroupFormState
) {
    FirefoxTheme {
        Surface {
            EditTabGroup(
                formState = formState,
                onConfirmSave = {},
                onTabGroupNameChange = {},
                onTabGroupThemeChange = {},
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@FlexibleWindowPreview
@Composable
private fun EditTabGroupBottomSheetPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val tabsTrayStore = remember {
        TabsTrayStore(
            initialState =
                TabsTrayState(
                    tabGroupState =
                        TabsTrayState.TabGroupState(
                            formState =
                                TabGroupFormState(
                                    tabGroupId = null,
                                    name = "",
                                    nextTabGroupNumber = 1,
                                    edited = false,
                                )
                        )
                )
        )
    }
    val state by tabsTrayStore.stateFlow.collectAsState()

    FirefoxTheme(theme) {
        Surface {
            ModalBottomSheet(
                // Enabling only the expanded/partially-expanded values (no hidden) allows this sheet to
                // display properly for Previews
                sheetState =
                    rememberBottomSheetState(
                        initialValue = SheetValue.Expanded,
                        enabledValues = setOf(SheetValue.PartiallyExpanded, SheetValue.Expanded),
                    ),
                dragHandle = {
                    BottomSheetHandle(
                        onRequestDismiss = {},
                        contentDescription = "",
                        modifier = Modifier.padding(all = 16.dp),
                    )
                },
                onDismissRequest = {},
            ) {
                EditTabGroup(
                    formState = state.tabGroupState.formState!!,
                    onTabGroupNameChange = { newName ->
                        tabsTrayStore.dispatch(TabGroupAction.NameChanged(newName))
                    },
                    onTabGroupThemeChange = { newTheme ->
                        tabsTrayStore.dispatch(TabGroupAction.ThemeChanged(newTheme))
                    },
                    onConfirmSave = {
                        tabsTrayStore.dispatch(TabGroupAction.SaveClicked)
                    },
                )
            }
        }
    }
}

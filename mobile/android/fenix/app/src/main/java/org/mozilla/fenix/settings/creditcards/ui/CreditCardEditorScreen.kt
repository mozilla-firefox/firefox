/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.autofill.ContentDataType
import androidx.compose.ui.autofill.ContentType
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDataType
import androidx.compose.ui.semantics.contentType
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.Dropdown
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.DestructiveButton
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.compose.base.theme.ThemedValue
import mozilla.components.compose.base.theme.ThemedValueProvider
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.creditcards.ui.CreditCardEditorAction.DeleteDialogAction
import org.mozilla.fenix.settings.creditcards.ui.CreditCardEditorAction.FieldChanged
import org.mozilla.fenix.theme.FirefoxTheme

/** Weight for the expiration month dropdown. */
private const val EXPIRATION_MONTH_WEIGHT = 5f

/** Weight for the expiration year dropdown. */
private const val EXPIRATION_YEAR_WEIGHT = 4f

/**
 * Composable for the credit card editor screen.
 *
 * @param store The [CreditCardEditorStore] that manages the state in the screen
 */
@Composable
fun CreditCardEditorScreen(store: CreditCardEditorStore) {
    val state by store.stateFlow.collectAsState()

    Scaffold(
        modifier =
            Modifier.semantics {
                testTagsAsResourceId = true
            },
        topBar = {
            EditorTopBar(
                inEditMode = state.inEditMode,
                onBackClicked = {
                    store.dispatch(CreditCardEditorAction.NavigateBack)
                },
                onSaveActionClicked = {
                    store.dispatch(CreditCardEditorAction.Save)
                },
                onDeleteActionClicked = {
                    store.dispatch(CreditCardEditorAction.DeleteClicked)
                },
            )
        },
    ) {
        if (state.showDeleteDialog) {
            DeleteCreditCardDialog(
                onCancel = {
                    store.dispatch(DeleteDialogAction.Cancel)
                },
                onConfirm = {
                    store.dispatch(DeleteDialogAction.Confirm)
                },
            )
        }

        Box(
            modifier = Modifier.fillMaxWidth().padding(it),
            contentAlignment = Alignment.TopCenter,
        ) {
            EditorContent(
                state = state,
                onCardNumberChanged = { cardNumber ->
                    store.dispatch(FieldChanged.CardNumberChanged(cardNumber))
                },
                onNameOnCardChanged = { nameOnCard ->
                    store.dispatch(FieldChanged.NameOnCardChanged(nameOnCard))
                },
                onMonthSelected = { month ->
                    store.dispatch(FieldChanged.MonthSelected(month))
                },
                onYearSelected = { year ->
                    store.dispatch(FieldChanged.YearSelected(year))
                },
                onSecurityCodeChanged = { securityCode ->
                    store.dispatch(FieldChanged.SecurityCodeChanged(securityCode))
                },
                onSecurityCodeCleared = {
                    store.dispatch(FieldChanged.SecurityCodeCleared)
                },
                onDeleteClicked = {
                    store.dispatch(CreditCardEditorAction.DeleteClicked)
                },
                onCancelClicked = {
                    store.dispatch(CreditCardEditorAction.Cancel)
                },
                onSaveClicked = {
                    store.dispatch(CreditCardEditorAction.Save)
                },
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class) // TopAppBar
@Composable
private fun EditorTopBar(
    modifier: Modifier = Modifier,
    inEditMode: Boolean,
    onDeleteActionClicked: () -> Unit,
    onSaveActionClicked: () -> Unit,
    onBackClicked: () -> Unit,
) {
    TopAppBar(
        modifier = modifier,
        title = {
            Text(
                text =
                    if (inEditMode) {
                        stringResource(R.string.credit_cards_edit_card)
                    } else {
                        stringResource(R.string.credit_cards_add_card)
                    },
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = onBackClicked,
                contentDescription = stringResource(R.string.credit_cards_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        actions = {
            if (inEditMode) {
                IconButton(
                    onClick = onDeleteActionClicked,
                    contentDescription = stringResource(R.string.credit_cards_menu_delete_card),
                    modifier = Modifier.testTag(CreditCardEditorTestTags.TOPBAR_DELETE_BUTTON),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                        contentDescription = null,
                    )
                }
            }

            IconButton(
                onClick = onSaveActionClicked,
                contentDescription = stringResource(R.string.credit_cards_menu_save),
                modifier = Modifier.testTag(CreditCardEditorTestTags.TOPBAR_SAVE_BUTTON),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_checkmark_24),
                    contentDescription = null,
                )
            }
        },
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
    )
}

@Composable
private fun EditorContent(
    state: CreditCardEditorState,
    onCardNumberChanged: (String) -> Unit = {},
    onNameOnCardChanged: (String) -> Unit = {},
    onMonthSelected: (Int) -> Unit = {},
    onYearSelected: (Int) -> Unit = {},
    onSecurityCodeChanged: (String) -> Unit = {},
    onSecurityCodeCleared: () -> Unit = {},
    onDeleteClicked: () -> Unit = {},
    onCancelClicked: () -> Unit = {},
    onSaveClicked: () -> Unit = {},
) {
    Column(
        modifier =
            Modifier.widthIn(max = FirefoxTheme.layout.size.containerMaxWidth)
                .verticalScroll(rememberScrollState())
                .padding(FirefoxTheme.layout.space.static200)
                .imePadding(),
        verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
    ) {
        val focusRequester = remember { FocusRequester() }

        LaunchedEffect(Unit) {
            focusRequester.requestFocus()
        }

        TextInput(
            modifier =
                Modifier.fillMaxWidth()
                    .testTag(CreditCardEditorTestTags.CARD_NUMBER_FIELD)
                    .semantics { contentDataType = ContentDataType.None }
                    .focusRequester(focusRequester),
            errorText = stringResource(R.string.credit_cards_number_validation_error_message_2),
            label = stringResource(R.string.credit_cards_card_number),
            isError = state.showCardNumberError,
            keyboardOptions =
                KeyboardOptions(
                    keyboardType = KeyboardType.Number,
                    imeAction = ImeAction.Next,
                ),
            value = state.cardNumber,
            onValueChange = onCardNumberChanged,
        )

        TextInput(
            modifier =
                Modifier.fillMaxWidth().testTag(CreditCardEditorTestTags.NAME_ON_CARD_FIELD).semantics {
                    contentDataType = ContentDataType.None
                },
            errorText = stringResource(R.string.credit_cards_name_on_card_validation_error_message_2),
            label = stringResource(R.string.credit_cards_name_on_card),
            isError = state.showNameOnCardError,
            value = state.nameOnCard,
            onValueChange = onNameOnCardChanged,
        )

        ExpirationDateRow(
            months = state.expiryMonths,
            years = state.expiryYears,
            selectedMonthIndex = state.selectedExpiryMonthIndex,
            selectedYearIndex = state.selectedExpiryYearIndex,
            onMonthSelected = onMonthSelected,
            onYearSelected = onYearSelected,
        )

        if (state.showSecurityCodeField) {
            SecurityCodeField(
                securityCode = state.securityCode,
                isError = state.showSecurityCodeError,
                isAmexCard = state.isAmexCard(),
                onSecurityCodeChanged = onSecurityCodeChanged,
                onSecurityCodeCleared = onSecurityCodeCleared,
            )
        }

        ButtonsRow(
            inEditMode = state.inEditMode,
            onDeleteClicked = onDeleteClicked,
            onCancelClicked = onCancelClicked,
            onSaveClicked = onSaveClicked,
        )
    }
}

@Composable
private fun ExpirationDateRow(
    months: List<String>,
    selectedMonthIndex: Int,
    years: List<String>,
    selectedYearIndex: Int,
    onMonthSelected: (Int) -> Unit,
    onYearSelected: (Int) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
    ) {
        ExpirationDateDropdown(
            modifier =
                Modifier.weight(EXPIRATION_MONTH_WEIGHT)
                    .testTag(CreditCardEditorTestTags.EXPIRATION_MONTH_FIELD)
                    .semantics { contentType = ContentType.CreditCardExpirationMonth },
            label = stringResource(R.string.credit_cards_expiration_date_month),
            items = months,
            selectedIndex = selectedMonthIndex,
            onItemSelected = onMonthSelected,
        )

        ExpirationDateDropdown(
            modifier =
                Modifier.weight(EXPIRATION_YEAR_WEIGHT)
                    .testTag(CreditCardEditorTestTags.EXPIRATION_YEAR_FIELD)
                    .semantics { contentType = ContentType.CreditCardExpirationYear },
            label = stringResource(R.string.credit_cards_expiration_date_year),
            items = years,
            selectedIndex = selectedYearIndex,
            onItemSelected = onYearSelected,
        )
    }
}

@Composable
private fun SecurityCodeField(
    securityCode: String,
    isError: Boolean,
    isAmexCard: Boolean,
    onSecurityCodeChanged: (String) -> Unit,
    onSecurityCodeCleared: () -> Unit,
) {
    TextField(
        value = securityCode,
        onValueChange = onSecurityCodeChanged,
        modifier =
            Modifier.fillMaxWidth().testTag(CreditCardEditorTestTags.SECURITY_CODE_FIELD).semantics {
                contentDataType = ContentDataType.None
            },
        label = stringResource(R.string.credit_cards_cvv_optional),
        placeholder = "",
        isError = isError,
        errorText =
            if (isAmexCard) {
                stringResource(R.string.credit_cards_cvv_amex_validation_error_message)
            } else {
                stringResource(R.string.credit_cards_cvv_validation_error_message)
            },
        trailingIcon =
            if (securityCode.isNotEmpty()) {
                { CrossTextFieldButton(onTrailingIconClick = onSecurityCodeCleared) }
            } else {
                null
            },
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions =
            KeyboardOptions(
                keyboardType = KeyboardType.NumberPassword,
                imeAction = ImeAction.Done,
            ),
    )
}

@Composable
private fun ButtonsRow(
    inEditMode: Boolean,
    onDeleteClicked: () -> Unit,
    onCancelClicked: () -> Unit,
    onSaveClicked: () -> Unit,
) {
    Row {
        if (inEditMode) {
            DestructiveButton(
                text = stringResource(R.string.credit_cards_delete_card_button),
                modifier = Modifier.testTag(CreditCardEditorTestTags.DELETE_BUTTON),
                onClick = onDeleteClicked,
            )
        }

        Spacer(Modifier.weight(1f))

        OutlinedButton(
            modifier = Modifier.testTag(CreditCardEditorTestTags.CANCEL_BUTTON),
            text = stringResource(R.string.credit_cards_cancel_button),
            onClick = onCancelClicked,
        )

        Spacer(Modifier.width(FirefoxTheme.layout.space.static200))

        FilledButton(
            modifier = Modifier.testTag(CreditCardEditorTestTags.SAVE_BUTTON),
            text = stringResource(R.string.credit_cards_save_button),
            onClick = onSaveClicked,
        )
    }
}

@Composable
private fun ExpirationDateDropdown(
    label: String,
    selectedIndex: Int,
    items: List<String>,
    modifier: Modifier = Modifier,
    onItemSelected: (Int) -> Unit,
) {
    Dropdown(
        modifier = modifier,
        label = label,
        dropdownItems =
            items.mapIndexed { index, itemLabel ->
                MenuItem.CheckableItem(
                    text = Text.String(value = itemLabel),
                    isChecked = index == selectedIndex,
                    onClick = {
                        onItemSelected(index)
                    },
                )
            },
        placeholder = "",
    )
}

@Composable
private fun TextInput(
    modifier: Modifier = Modifier,
    label: String,
    value: String,
    isError: Boolean,
    errorText: String,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default,
    onValueChange: (String) -> Unit = {},
) {
    TextField(
        value = value,
        onValueChange = onValueChange,
        modifier = modifier,
        label = label,
        placeholder = "",
        isError = isError,
        errorText = errorText,
        keyboardOptions = keyboardOptions,
    )
}

private data class CreditCardEditorPreviewState(
    val guid: String = "1234",
    val cardNumber: String = "5555444433331111",
    val nameOnCard: String = "Jane Doe",
    val expiryMonths: List<String> = listOf("January (01)", "February (02)"),
    val selectedExpiryMonthIndex: Int = 1,
    val expiryYears: List<String> = listOf("2025", "2026", "2027"),
    val selectedExpiryYearIndex: Int = 2,
    val inEditMode: Boolean = false,
    val showDeleteDialog: Boolean = false,
    val securityCode: String = "",
    val showSecurityCodeError: Boolean = false,
)

private class CreditCardEditorPreviewProvider :
    ThemedValueProvider<CreditCardEditorPreviewState>(
        sequenceOf(
            CreditCardEditorPreviewState(),
            CreditCardEditorPreviewState(inEditMode = true, securityCode = "123"),
            CreditCardEditorPreviewState(securityCode = "12", showSecurityCodeError = true),
            CreditCardEditorPreviewState(inEditMode = true, showDeleteDialog = true),
        )
    )

@FlexibleWindowLightDarkPreview
@Composable
private fun CreditCardEditorScreenPreview(
    @PreviewParameter(CreditCardEditorPreviewProvider::class) state: ThemedValue<CreditCardEditorPreviewState>
) {
    val uiState =
        CreditCardEditorState(
            guid = state.value.guid,
            cardNumber = state.value.cardNumber,
            nameOnCard = state.value.nameOnCard,
            expiryMonths = state.value.expiryMonths,
            selectedExpiryMonthIndex = state.value.selectedExpiryMonthIndex,
            expiryYears = state.value.expiryYears,
            selectedExpiryYearIndex = state.value.selectedExpiryYearIndex,
            inEditMode = state.value.inEditMode,
            showDeleteDialog = state.value.showDeleteDialog,
            showSecurityCodeField = true,
            securityCode = state.value.securityCode,
            showSecurityCodeError = state.value.showSecurityCodeError,
        )
    FirefoxTheme(state.theme) {
        CreditCardEditorScreen(store = CreditCardEditorStore(initialState = uiState))
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.input.InputTransformation
import androidx.compose.foundation.text.input.byValue
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.tooling.preview.PreviewLightDark
import java.text.DateFormatSymbols
import java.util.Calendar
import mozilla.components.compose.base.Dropdown
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.textfield.SecureTextField
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.compose.base.textfield.rememberSyncedTextFieldState
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.feature.prompts.R
import mozilla.components.support.utils.creditCardIIN
import mozilla.components.support.utils.ext.toCreditCardNumber
import mozilla.components.support.utils.validateCreditCardNumber

private const val MAX_SECURITY_CODE_LENGTH = 4
private const val MIN_SECURITY_CODE_LENGTH = 3
private const val EXPIRY_YEARS_AHEAD = 20
private const val MONTHS_IN_YEAR = 12
private const val CARD_NUMBER_GROUP_SIZE = 4

/**
 * Content of the save credit card prompt while the user edits the credit card. Nothing is stored from here: [onSave]
 * only returns the edited [CreditCardEntry] to the prompt.
 *
 * @param creditCard The [CreditCardEntry] to edit.
 * @param onSave Invoked with the edited [CreditCardEntry] when the user saves the edits.
 * @param onCancel Invoked when the user discards the edits.
 */
@Composable
internal fun CreditCardEditContent(
    creditCard: CreditCardEntry,
    onSave: (CreditCardEntry) -> Unit,
    onCancel: () -> Unit,
) {
    val currentYear = remember { Calendar.getInstance().get(Calendar.YEAR) }
    var number by rememberSaveable { mutableStateOf(creditCard.number.groupedForDisplay()) }
    var name by rememberSaveable { mutableStateOf(creditCard.name) }
    var month by rememberSaveable {
        mutableStateOf(creditCard.expiryMonth.toIntOrNull()?.takeIf { it in 1..MONTHS_IN_YEAR })
    }
    var year by rememberSaveable { mutableStateOf(creditCard.expiryYear.toIntOrNull()) }
    var securityCode by rememberSaveable { mutableStateOf(creditCard.cardCvv.orEmpty()) }

    val isNumberValid = number.toCreditCardNumber().validateCreditCardNumber()
    val isNumberChanged = number.filter { it.isDigit() } != creditCard.number.filter { it.isDigit() }
    val isSecurityCodeValid = securityCode.isEmpty() || securityCode.length >= MIN_SECURITY_CODE_LENGTH

    Surface(color = MaterialTheme.colorScheme.surface) {
        Column(
            modifier =
                Modifier.fillMaxWidth()
                    .imePadding()
                    .verticalScroll(rememberScrollState())
                    .padding(AcornTheme.layout.space.static200),
            verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static200),
        ) {
            Text(
                text = stringResource(R.string.mozac_feature_prompts_edit_credit_card_title),
                style = AcornTheme.typography.headline7,
                fontWeight = FontWeight.Medium,
                color = MaterialTheme.colorScheme.onSurface,
            )

            Column(verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static150)) {
                CardNumberField(
                    number = number,
                    isInvalid = isNumberChanged && !isNumberValid,
                    onNumberChange = { number = it },
                )

                NameOnCardField(name = name, onNameChange = { name = it })

                ExpiryDateRow(
                    month = month,
                    year = year,
                    currentYear = currentYear,
                    onMonthSelected = { month = it },
                    onYearSelected = { year = it },
                )

                SecurityCodeField(
                    securityCode = securityCode,
                    isInvalid = !isSecurityCodeValid,
                    onSecurityCodeChange = { securityCode = it },
                )
            }

            EditButtonsRow(
                isSaveEnabled =
                    canSaveEdits(
                        isNumberChanged = isNumberChanged,
                        isNumberValid = isNumberValid,
                        isSecurityCodeValid = isSecurityCodeValid,
                        month = month,
                        year = year,
                    ),
                onCancel = onCancel,
                onSave = {
                    onSave(
                        creditCard.withEdits(
                            number = number,
                            name = name,
                            expiryMonth = month ?: return@EditButtonsRow,
                            expiryYear = year ?: return@EditButtonsRow,
                            securityCode = securityCode,
                        )
                    )
                },
            )
        }
    }
}

@Composable
private fun CardNumberField(
    number: String,
    isInvalid: Boolean,
    onNumberChange: (String) -> Unit,
) {
    TextField(
        value = number,
        onValueChange = { input -> onNumberChange(input.filter { it.isDigit() || it == ' ' }) },
        placeholder = "",
        errorText = stringResource(R.string.mozac_feature_prompts_edit_credit_card_number_error),
        modifier = Modifier.fillMaxWidth(),
        label = stringResource(R.string.mozac_feature_prompts_edit_credit_card_number),
        isError = isInvalid,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
    )
}

@Composable
private fun NameOnCardField(
    name: String,
    onNameChange: (String) -> Unit,
) {
    TextField(
        value = name,
        onValueChange = onNameChange,
        placeholder = "",
        errorText = "",
        modifier = Modifier.fillMaxWidth(),
        label = stringResource(R.string.mozac_feature_prompts_edit_credit_card_name),
        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Words),
    )
}

@Composable
private fun ExpiryDateRow(
    month: Int?,
    year: Int?,
    currentYear: Int,
    onMonthSelected: (Int) -> Unit,
    onYearSelected: (Int) -> Unit,
) {
    val monthNames = remember { DateFormatSymbols.getInstance().months }
    Row(horizontalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static150)) {
        Dropdown(
            label = stringResource(R.string.mozac_feature_prompts_edit_credit_card_expiry_month),
            placeholder = "",
            dropdownItems =
                (1..MONTHS_IN_YEAR).map { item ->
                    MenuItem.CheckableItem(
                        text =
                            Text.String(
                                stringResource(
                                    R.string.mozac_feature_prompts_edit_credit_card_expiry_month_option,
                                    monthNames[item - 1],
                                    "%02d".format(item),
                                )
                            ),
                        isChecked = item == month,
                        onClick = { onMonthSelected(item) },
                    )
                },
            modifier = Modifier.weight(1f),
        )
        Dropdown(
            label = stringResource(R.string.mozac_feature_prompts_edit_credit_card_expiry_year),
            placeholder = "",
            dropdownItems =
                expiryYears(currentYear, year ?: currentYear).map { item ->
                    MenuItem.CheckableItem(
                        text = Text.String(item.toString()),
                        isChecked = item == year,
                        onClick = { onYearSelected(item) },
                    )
                },
            modifier = Modifier.weight(1f),
        )
    }
}

@Composable
private fun SecurityCodeField(
    securityCode: String,
    isInvalid: Boolean,
    onSecurityCodeChange: (String) -> Unit,
) {
    var isFocused by remember { mutableStateOf(false) }
    var hasLostFocus by rememberSaveable { mutableStateOf(false) }

    SecureTextField(
        state = rememberSyncedTextFieldState(value = securityCode, onValueChange = onSecurityCodeChange),
        placeholder = "",
        errorText = stringResource(R.string.mozac_feature_prompts_edit_credit_card_security_code_error),
        modifier =
            Modifier.fillMaxWidth().onFocusChanged { state ->
                if (isFocused && !state.isFocused) {
                    hasLostFocus = true
                }
                isFocused = state.isFocused
            },
        label = stringResource(R.string.mozac_feature_prompts_edit_credit_card_security_code),
        isError = isInvalid && hasLostFocus && !isFocused,
        trailingIcon =
            if (securityCode.isNotEmpty()) {
                {
                    CrossTextFieldButton(
                        contentDescription =
                            Text.Resource(R.string.mozac_feature_prompts_edit_credit_card_security_code_clear),
                        onTrailingIconClick = { onSecurityCodeChange("") },
                    )
                }
            } else {
                null
            },
        inputTransformation =
            InputTransformation.byValue { _, proposed ->
                proposed.filter { it.isDigit() }.take(MAX_SECURITY_CODE_LENGTH)
            },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
    )
}

@Composable
private fun EditButtonsRow(
    isSaveEnabled: Boolean,
    onCancel: () -> Unit,
    onSave: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.End,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        OutlinedButton(
            text = stringResource(R.string.mozac_feature_prompts_cancel),
            onClick = onCancel,
        )
        Spacer(modifier = Modifier.width(AcornTheme.layout.space.static100))
        FilledButton(
            text = stringResource(R.string.mozac_feature_prompt_save_confirmation),
            enabled = isSaveEnabled,
            onClick = onSave,
        )
    }
}

/** Returns the digits of this card number in groups of [CARD_NUMBER_GROUP_SIZE], separated by spaces. */
private fun String.groupedForDisplay(): String = filter {
    it.isDigit()
}
    .chunked(CARD_NUMBER_GROUP_SIZE)
    .joinToString(" ")

/** Returns the years offered in the expiry year dropdown, always including [selectedYear]. */
private fun expiryYears(currentYear: Int, selectedYear: Int): List<Int> =
    (minOf(currentYear, selectedYear)..maxOf(currentYear + EXPIRY_YEARS_AHEAD, selectedYear)).toList()

/** Returns whether the edits in the edit card sheet can be saved. */
internal fun canSaveEdits(
    isNumberChanged: Boolean,
    isNumberValid: Boolean,
    isSecurityCodeValid: Boolean,
    month: Int?,
    year: Int?,
): Boolean = (!isNumberChanged || isNumberValid) && isSecurityCodeValid && month != null && year != null

/** Returns a copy of this [CreditCardEntry] with the values from the edit card sheet. */
internal fun CreditCardEntry.withEdits(
    number: String,
    name: String,
    expiryMonth: Int,
    expiryYear: Int,
    securityCode: String,
): CreditCardEntry {
    val digits = number.filter { it.isDigit() }
    return copy(
        guid = guid.takeIf { digits == this.number.filter { it.isDigit() } },
        number = digits,
        name = name.trim(),
        expiryMonth = expiryMonth.toString(),
        expiryYear = expiryYear.toString(),
        cardType = digits.creditCardIIN()?.creditCardIssuerNetwork?.name ?: "",
        // A cleared security code becomes null, which keeps a security code that is already stored. Removing stored
        // security codes is a follow-up in bug 2092794.
        cardCvv = securityCode.ifEmpty { null },
    )
}

@PreviewLightDark
@Composable
private fun CreditCardEditContentPreview() {
    AcornTheme {
        CreditCardEditContent(
            creditCard =
                CreditCardEntry(
                    guid = null,
                    name = "Firstname Lastname",
                    number = "5555555555551881",
                    expiryMonth = "8",
                    expiryYear = "2031",
                    cardType = "mastercard",
                    cardCvv = "123",
                ),
            onSave = {},
            onCancel = {},
        )
    }
}

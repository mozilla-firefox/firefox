/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.Switch
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.feature.prompts.R
import mozilla.components.support.utils.creditCardIssuerNetwork
import mozilla.components.ui.icons.R as iconsR

// Bullet followed by a narrow space, with word joiners to prevent line breaks between mask characters.
private const val SECURITY_CODE_MASK_CHAR = "\u2022\u2060\u2006\u2060"

/**
 * Content of the save credit card prompt when the security code can be saved.
 *
 * @param creditCard The [CreditCardEntry] to save.
 * @param appName The name of the app, shown in the body text.
 * @param isUpdate Whether the credit card updates an already saved credit card.
 * @param saveSecurityCode The state of the "Save security codes" toggle.
 * @param onSaveSecurityCodeChange Invoked when the user changes the toggle.
 * @param onEditClick Invoked when the user taps the edit button on the credit card.
 * @param onSave Invoked when the user saves the credit card, or null to disable the Save button.
 * @param onDismiss Invoked when the user dismisses the prompt without saving.
 */
@Suppress("LongParameterList")
@Composable
internal fun CreditCardSaveDialogContent(
    creditCard: CreditCardEntry,
    appName: String,
    isUpdate: Boolean,
    saveSecurityCode: Boolean,
    onSaveSecurityCodeChange: (Boolean) -> Unit,
    onEditClick: () -> Unit,
    onSave: (() -> Unit)?,
    onDismiss: () -> Unit,
) {
    Surface(color = MaterialTheme.colorScheme.surface) {
        Column(modifier = Modifier.fillMaxWidth().padding(AcornTheme.layout.space.static200)) {
            Text(
                text =
                    stringResource(
                        if (isUpdate) {
                            R.string.mozac_feature_prompts_update_payment_method_prompt_title
                        } else {
                            R.string.mozac_feature_prompts_save_payment_method_prompt_title
                        }
                    ),
                style = AcornTheme.typography.headline7,
                fontWeight = FontWeight.Medium,
                color = MaterialTheme.colorScheme.onSurface,
            )

            Spacer(modifier = Modifier.height(AcornTheme.layout.space.static200))

            Text(
                text =
                    stringResource(
                        if (saveSecurityCode) {
                            R.string.mozac_feature_prompts_save_payment_method_prompt_body_with_security_code
                        } else {
                            R.string.mozac_feature_prompts_save_payment_method_prompt_body_without_security_code
                        },
                        appName,
                    ),
                style = AcornTheme.typography.body2,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Spacer(modifier = Modifier.height(AcornTheme.layout.space.static200))

            CreditCardDetails(
                creditCard = creditCard,
                showSecurityCode = saveSecurityCode && !creditCard.cardCvv.isNullOrEmpty(),
                onEditClick = onEditClick,
            )

            Spacer(modifier = Modifier.height(AcornTheme.layout.space.static200))

            SaveSecurityCodeToggle(
                checked = saveSecurityCode,
                onCheckedChange = onSaveSecurityCodeChange,
            )

            Spacer(modifier = Modifier.height(AcornTheme.layout.space.static200))

            FooterButtons(
                isUpdate = isUpdate,
                onSave = onSave,
                onDismiss = onDismiss,
            )
        }
    }
}

@Composable
private fun FooterButtons(
    isUpdate: Boolean,
    onSave: (() -> Unit)?,
    onDismiss: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.End,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        OutlinedButton(
            text = stringResource(R.string.mozac_feature_prompts_save_payment_method_dismiss),
            onClick = onDismiss,
        )
        Spacer(modifier = Modifier.width(AcornTheme.layout.space.static100))
        FilledButton(
            text =
                stringResource(
                    if (isUpdate) {
                        R.string.mozac_feature_prompt_update_confirmation
                    } else {
                        R.string.mozac_feature_prompt_save_confirmation
                    }
                ),
            enabled = onSave != null,
            onClick = { onSave?.invoke() },
        )
    }
}

@Composable
private fun CreditCardDetails(
    creditCard: CreditCardEntry,
    showSecurityCode: Boolean,
    onEditClick: () -> Unit,
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.medium,
        border = BorderStroke(AcornTheme.layout.border.default, MaterialTheme.colorScheme.outlineVariant),
        color = MaterialTheme.colorScheme.surface,
    ) {
        Row(
            modifier =
                Modifier.padding(
                    start = AcornTheme.layout.space.static200,
                    top = AcornTheme.layout.space.static200,
                    bottom = AcornTheme.layout.space.static200,
                    end = AcornTheme.layout.space.static50,
                )
        ) {
            Image(
                painter = painterResource(creditCard.cardType.creditCardIssuerNetwork().icon),
                contentDescription = null,
                modifier = Modifier.size(AcornTheme.layout.size.static300),
            )
            Spacer(modifier = Modifier.width(AcornTheme.layout.space.static200))
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static25),
            ) {
                Text(
                    text = creditCard.obfuscatedCardNumber,
                    style = AcornTheme.typography.body1,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                Text(
                    text = creditCard.expiryDate,
                    style = AcornTheme.typography.body2,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (showSecurityCode) {
                    val securityCodeDescription =
                        stringResource(R.string.mozac_feature_prompts_save_payment_method_security_code_description)
                    Text(
                        text =
                            stringResource(
                                R.string.mozac_feature_prompts_save_payment_method_security_code,
                                SECURITY_CODE_MASK_CHAR.repeat(creditCard.cardCvv.orEmpty().length),
                            ),
                        modifier = Modifier.semantics { contentDescription = securityCodeDescription },
                        style = AcornTheme.typography.body2,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            IconButton(
                onClick = onEditClick,
                contentDescription = stringResource(R.string.mozac_feature_prompts_edit_credit_card_title),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_edit_24),
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun SaveSecurityCodeToggle(
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.medium,
        border = BorderStroke(AcornTheme.layout.border.default, MaterialTheme.colorScheme.outlineVariant),
        color = MaterialTheme.colorScheme.surface,
    ) {
        Row(
            modifier =
                Modifier.toggleable(value = checked, role = Role.Switch, onValueChange = onCheckedChange)
                    .padding(
                        horizontal = AcornTheme.layout.space.static200,
                        vertical = AcornTheme.layout.space.static150,
                    ),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.mozac_feature_prompts_always_save_security_codes),
                    style = AcornTheme.typography.body1,
                    color = MaterialTheme.colorScheme.onSurface,
                )
            }
            Spacer(modifier = Modifier.width(AcornTheme.layout.space.static200))
            // The toggleable modifier on the row handles this change.
            Switch(checked = checked, onCheckedChange = null)
        }
    }
}

private val previewCreditCard =
    CreditCardEntry(
        guid = null,
        name = "Firstname Lastname",
        number = "5555555555551881",
        expiryMonth = "8",
        expiryYear = "2031",
        cardType = "mastercard",
        cardCvv = "123",
    )

@PreviewLightDark
@Composable
private fun CreditCardSaveDialogContentPreview() {
    AcornTheme {
        CreditCardSaveDialogContent(
            creditCard = previewCreditCard,
            appName = "Firefox",
            isUpdate = false,
            saveSecurityCode = true,
            onSaveSecurityCodeChange = {},
            onEditClick = {},
            onSave = {},
            onDismiss = {},
        )
    }
}

@PreviewLightDark
@Composable
private fun CreditCardUpdateDialogContentPreview() {
    AcornTheme {
        CreditCardSaveDialogContent(
            creditCard = previewCreditCard.copy(guid = "1"),
            appName = "Firefox",
            isUpdate = true,
            saveSecurityCode = false,
            onSaveSecurityCodeChange = {},
            onEditClick = {},
            onSave = {},
            onDismiss = {},
        )
    }
}

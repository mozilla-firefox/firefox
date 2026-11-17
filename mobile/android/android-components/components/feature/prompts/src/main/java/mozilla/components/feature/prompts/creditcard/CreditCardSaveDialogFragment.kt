/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import android.app.Dialog
import android.content.DialogInterface
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import androidx.activity.compose.BackHandler
import androidx.annotation.VisibleForTesting
import androidx.appcompat.widget.AppCompatTextView
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.core.view.isVisible
import androidx.fragment.compose.content
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.Dispatchers.Main
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.CreditCardValidationDelegate.Result
import mozilla.components.feature.prompts.R
import mozilla.components.feature.prompts.dialog.KEY_PROMPT_UID
import mozilla.components.feature.prompts.dialog.KEY_SESSION_ID
import mozilla.components.feature.prompts.dialog.KEY_SHOULD_DISMISS_ON_LOAD
import mozilla.components.feature.prompts.dialog.PromptDialogFragment
import mozilla.components.feature.prompts.facts.emitCreditCardAutofillCreatedFact
import mozilla.components.feature.prompts.facts.emitCreditCardAutofillUpdatedFact
import mozilla.components.support.ktx.android.content.appName
import mozilla.components.support.ktx.android.view.toScope
import mozilla.components.support.utils.creditCardIssuerNetwork
import mozilla.components.support.utils.ext.getParcelableCompat

private const val KEY_CREDIT_CARD = "KEY_CREDIT_CARD"
private const val KEY_SECURITY_CODE_SAVE_ENABLED = "KEY_SECURITY_CODE_SAVE_ENABLED"
private const val KEY_SAVE_SECURITY_CODE = "KEY_SAVE_SECURITY_CODE"

/**
 * [android.support.v4.app.DialogFragment] implementation to display a dialog that allows user to save a new credit card
 * or update an existing credit card.
 */
internal class CreditCardSaveDialogFragment : PromptDialogFragment() {

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val creditCard by lazy {
        safeArguments.getParcelableCompat(KEY_CREDIT_CARD, CreditCardEntry::class.java)!!
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val isSecurityCodeSaveEnabled by lazy { safeArguments.getBoolean(KEY_SECURITY_CODE_SAVE_ENABLED) }

    private val initialSaveSecurityCode by lazy { safeArguments.getBoolean(KEY_SAVE_SECURITY_CODE) }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal var confirmResult: Result by mutableStateOf(Result.CanBeCreated)

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        return BottomSheetDialog(requireContext(), R.style.MozDialogStyle).apply {
            setCancelable(true)
            setOnShowListener {
                val bottomSheet = findViewById<View>(materialR.id.design_bottom_sheet) as FrameLayout
                val behavior = BottomSheetBehavior.from(bottomSheet)
                if (isSecurityCodeSaveEnabled) {
                    behavior.peekHeight = context.resources.displayMetrics.heightPixels
                }
                behavior.state = BottomSheetBehavior.STATE_EXPANDED
            }
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        if (isSecurityCodeSaveEnabled) {
            return content {
                var card by rememberSaveable { mutableStateOf(creditCard) }
                var isEditing by rememberSaveable { mutableStateOf(false) }
                var saveSecurityCode by rememberSaveable { mutableStateOf(initialSaveSecurityCode) }

                var checkedCard by remember { mutableStateOf<CreditCardEntry?>(null) }

                LaunchedEffect(card) {
                    checkIfCardIsNewOrSaved(card)
                    checkedCard = card
                }
                LaunchedEffect(isEditing) { isCancelable = !isEditing }
                BackHandler(enabled = isEditing) { isEditing = false }

                AcornTheme {
                    if (isEditing) {
                        CreditCardEditContent(
                            creditCard = card,
                            onSave = { editedCard ->
                                card = editedCard
                                isEditing = false
                            },
                            onCancel = { isEditing = false },
                        )
                    } else {
                        CreditCardSaveDialogContent(
                            creditCard = card,
                            appName = requireContext().appName,
                            isUpdate = confirmResult is Result.CanBeUpdated,
                            saveSecurityCode = saveSecurityCode,
                            onSaveSecurityCodeChange = { saveSecurityCode = it },
                            onEditClick = { isEditing = true },
                            onSave =
                                if (checkedCard == card) {
                                    { onSaveClicked(saveSecurityCode, card) }
                                } else {
                                    null
                                },
                            onDismiss = ::onDismissClicked,
                        )
                    }
                }
            }
        }

        return LayoutInflater.from(requireContext())
            .inflate(
                R.layout.mozac_feature_prompt_save_credit_card_prompt,
                container,
                false,
            )
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        if (isSecurityCodeSaveEnabled) {
            return
        }

        view
            .findViewById<ImageView>(R.id.credit_card_logo)
            .setImageResource(creditCard.cardType.creditCardIssuerNetwork().icon)

        view.findViewById<TextView>(R.id.save_credit_card_message).text =
            getString(
                R.string.mozac_feature_prompts_save_credit_card_prompt_body_2,
                context?.appName,
            )

        view.findViewById<TextView>(R.id.credit_card_number).text = creditCard.obfuscatedCardNumber
        view.findViewById<TextView>(R.id.credit_card_expiration_date).text = creditCard.expiryDate

        view.findViewById<Button>(R.id.save_confirm).setOnClickListener {
            onSaveClicked(saveSecurityCode = false)
        }

        view.findViewById<Button>(R.id.save_cancel).setOnClickListener { onDismissClicked() }

        updateUI(view)
    }

    /**
     * Confirms [card], which is the credit card from Gecko or the user's edits of it. The security code is saved only
     * when [isSecurityCodeSaveEnabled] and [saveSecurityCode] are true, and the toggle state is reported only when it
     * was shown.
     */
    @VisibleForTesting
    internal fun onSaveClicked(saveSecurityCode: Boolean, card: CreditCardEntry = creditCard) {
        val saveSecurityCode = isSecurityCodeSaveEnabled && saveSecurityCode
        if (isSecurityCodeSaveEnabled) {
            feature?.onSaveSecurityCodeChanged(saveSecurityCode)
        }
        feature?.onConfirm(
            sessionId = sessionId,
            promptRequestUID = promptRequestUID,
            value = card.copy(cardCvv = card.cardCvv.takeIf { saveSecurityCode }),
        )
        dismiss()
        emitSaveUpdateFact()
    }

    @VisibleForTesting
    internal fun onDismissClicked() {
        feature?.onCancel(
            sessionId = sessionId,
            promptRequestUID = promptRequestUID,
        )
        dismiss()
    }

    /** Emit the save or update fact based on the confirm action for the credit card. */
    @VisibleForTesting
    internal fun emitSaveUpdateFact() {
        when (confirmResult) {
            is Result.CanBeCreated -> {
                emitCreditCardAutofillCreatedFact()
            }
            is Result.CanBeUpdated -> {
                emitCreditCardAutofillUpdatedFact()
            }
        }
    }

    override fun onCancel(dialog: DialogInterface) {
        super.onCancel(dialog)
        feature?.onCancel(
            sessionId = sessionId,
            promptRequestUID = promptRequestUID,
        )
    }

    /** Updates the dialog based on whether a save or update credit card state should be displayed. */
    private fun updateUI(view: View) =
        view.toScope().launch(Main) {
            when (checkIfCardIsNewOrSaved(creditCard) ?: return@launch) {
                is Result.CanBeCreated ->
                    setViewText(
                        view = view,
                        header =
                            requireContext().getString(R.string.mozac_feature_prompts_save_credit_card_prompt_title),
                        cancelButtonText = requireContext().getString(R.string.mozac_feature_prompt_not_now),
                        confirmButtonText = requireContext().getString(R.string.mozac_feature_prompt_save_confirmation),
                    )
                is Result.CanBeUpdated ->
                    setViewText(
                        view = view,
                        header =
                            requireContext().getString(R.string.mozac_feature_prompts_update_credit_card_prompt_title),
                        cancelButtonText = requireContext().getString(R.string.mozac_feature_prompts_cancel),
                        confirmButtonText =
                            requireContext().getString(R.string.mozac_feature_prompt_update_confirmation),
                        showMessageBody = false,
                    )
            }
        }

    /**
     * Finds out whether [card] is a new card or an update to a card that is already saved, and stores the answer in
     * [confirmResult].
     *
     * @return The answer, or null when no validation delegate is attached.
     */
    private suspend fun checkIfCardIsNewOrSaved(card: CreditCardEntry): Result? {
        val validationDelegate = feature?.creditCardValidationDelegate ?: return null
        return withContext(IO) { validationDelegate.shouldCreateOrUpdate(card) }.also { confirmResult = it }
    }

    /**
     * Updates the header and button text in the dialog.
     *
     * @param view The view associated with the dialog.
     * @param header The header text to be displayed.
     * @param cancelButtonText The cancel button text to be displayed.
     * @param confirmButtonText The confirm button text to be displayed.
     * @param showMessageBody Whether or not to show the dialog message body text.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun setViewText(
        view: View,
        header: String,
        cancelButtonText: String,
        confirmButtonText: String,
        showMessageBody: Boolean = true,
    ) {
        view.findViewById<AppCompatTextView>(R.id.save_credit_card_message).isVisible = showMessageBody
        view.findViewById<AppCompatTextView>(R.id.save_credit_card_header).text = header
        view.findViewById<Button>(R.id.save_cancel).text = cancelButtonText
        view.findViewById<Button>(R.id.save_confirm).text = confirmButtonText
    }

    companion object {
        fun newInstance(
            sessionId: String,
            promptRequestUID: String,
            shouldDismissOnLoad: Boolean,
            creditCard: CreditCardEntry,
            isSecurityCodeSaveEnabled: Boolean,
            saveSecurityCode: Boolean,
        ): CreditCardSaveDialogFragment {
            val fragment = CreditCardSaveDialogFragment()
            val arguments = fragment.arguments ?: Bundle()

            with(arguments) {
                putString(KEY_SESSION_ID, sessionId)
                putString(KEY_PROMPT_UID, promptRequestUID)
                putBoolean(KEY_SHOULD_DISMISS_ON_LOAD, shouldDismissOnLoad)
                putParcelable(KEY_CREDIT_CARD, creditCard)
                putBoolean(KEY_SECURITY_CODE_SAVE_ENABLED, isSecurityCodeSaveEnabled)
                putBoolean(KEY_SAVE_SECURITY_CODE, saveSecurityCode)
            }

            fragment.arguments = arguments
            return fragment
        }
    }
}

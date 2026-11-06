/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import android.widget.Button
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.widget.AppCompatTextView
import androidx.core.view.isVisible
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.CreditCardValidationDelegate
import mozilla.components.feature.prompts.PromptFeature
import mozilla.components.feature.prompts.R
import mozilla.components.feature.prompts.facts.CreditCardAutofillDialogFacts
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.test.any
import mozilla.components.support.test.ext.appCompatContext
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class CreditCardSaveDialogFragmentTest {

    private val creditCard =
        CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "amex",
            cardCvv = null,
        )
    private val sessionId = "sessionId"
    private val promptRequestUID = "uid"

    @Test
    fun `WHEN the credit card save dialog fragment view is created THEN the credit card entry is displayed`() {
        val fragment =
            spy(
                CreditCardSaveDialogFragment.newInstance(
                    sessionId = sessionId,
                    promptRequestUID = promptRequestUID,
                    shouldDismissOnLoad = true,
                    creditCard = creditCard,
                )
            )

        doReturn(appCompatContext).`when`(fragment).requireContext()
        doAnswer {
                FrameLayout(appCompatContext).apply {
                    addView(
                        AppCompatTextView(appCompatContext).apply {
                            id = R.id.save_credit_card_header
                        }
                    )
                    addView(
                        AppCompatTextView(appCompatContext).apply {
                            id = R.id.save_credit_card_message
                        }
                    )
                    addView(Button(appCompatContext).apply { id = R.id.save_confirm })
                    addView(Button(appCompatContext).apply { id = R.id.save_cancel })
                    addView(ImageView(appCompatContext).apply { id = R.id.credit_card_logo })
                    addView(TextView(appCompatContext).apply { id = R.id.credit_card_number })
                    addView(TextView(appCompatContext).apply { id = R.id.credit_card_expiration_date })
                }
            }
            .`when`(fragment)
            .onCreateView(any(), any(), any())

        val view = fragment.onCreateView(mock(), mock(), mock())
        fragment.onViewCreated(view, mock())

        val cardNumberTextView = view.findViewById<TextView>(R.id.credit_card_number)
        val iconImageView = view.findViewById<ImageView>(R.id.credit_card_logo)
        val expiryDateView = view.findViewById<TextView>(R.id.credit_card_expiration_date)

        assertEquals(creditCard.obfuscatedCardNumber, cardNumberTextView.text)
        assertEquals(creditCard.expiryDate, expiryDateView.text)
        assertNotNull(iconImageView.drawable)
    }

    @Test
    fun `WHEN setViewText is called with new header and button text THEN the header and button text are updated in the view`() {
        val fragment =
            spy(
                CreditCardSaveDialogFragment.newInstance(
                    sessionId = sessionId,
                    promptRequestUID = promptRequestUID,
                    shouldDismissOnLoad = true,
                    creditCard = creditCard,
                )
            )

        doReturn(appCompatContext).`when`(fragment).requireContext()
        doAnswer {
                FrameLayout(appCompatContext).apply {
                    addView(
                        AppCompatTextView(appCompatContext).apply {
                            id = R.id.save_credit_card_header
                        }
                    )
                    addView(
                        AppCompatTextView(appCompatContext).apply {
                            id = R.id.save_credit_card_message
                        }
                    )
                    addView(Button(appCompatContext).apply { id = R.id.save_confirm })
                    addView(Button(appCompatContext).apply { id = R.id.save_cancel })
                    addView(ImageView(appCompatContext).apply { id = R.id.credit_card_logo })
                    addView(TextView(appCompatContext).apply { id = R.id.credit_card_number })
                    addView(TextView(appCompatContext).apply { id = R.id.credit_card_expiration_date })
                }
            }
            .`when`(fragment)
            .onCreateView(any(), any(), any())

        val view = fragment.onCreateView(mock(), mock(), mock())
        fragment.onViewCreated(view, mock())

        val headerTextView = view.findViewById<AppCompatTextView>(R.id.save_credit_card_header)
        val messageTextView = view.findViewById<AppCompatTextView>(R.id.save_credit_card_message)
        val cancelButtonView = view.findViewById<Button>(R.id.save_cancel)
        val confirmButtonView = view.findViewById<Button>(R.id.save_confirm)

        val header = "header"
        val cancelButtonText = "cancelButtonText"
        val confirmButtonText = "confirmButtonText"

        fragment.setViewText(
            view = view,
            header = header,
            cancelButtonText = cancelButtonText,
            confirmButtonText = confirmButtonText,
            showMessageBody = false,
        )

        assertEquals(header, headerTextView.text)
        assertEquals(cancelButtonText, cancelButtonView.text)
        assertEquals(confirmButtonText, confirmButtonView.text)
        assertFalse(messageTextView.isVisible)

        fragment.setViewText(
            view = view,
            header = header,
            cancelButtonText = cancelButtonText,
            confirmButtonText = confirmButtonText,
            showMessageBody = true,
        )

        assertTrue(messageTextView.isVisible)
    }

    @Test
    fun `WHEN the confirm button is clicked THEN the prompt feature is notified`() {
        val mockFeature: PromptFeature = mock()
        val fragment =
            spy(
                CreditCardSaveDialogFragment.newInstance(
                    sessionId = sessionId,
                    promptRequestUID = promptRequestUID,
                    shouldDismissOnLoad = true,
                    creditCard = creditCard,
                )
            )

        fragment.feature = mockFeature

        doReturn(appCompatContext).`when`(fragment).requireContext()
        doAnswer {
                FrameLayout(appCompatContext).apply {
                    addView(
                        AppCompatTextView(appCompatContext).apply {
                            id = R.id.save_credit_card_header
                        }
                    )
                    addView(
                        AppCompatTextView(appCompatContext).apply {
                            id = R.id.save_credit_card_message
                        }
                    )
                    addView(Button(appCompatContext).apply { id = R.id.save_confirm })
                    addView(Button(appCompatContext).apply { id = R.id.save_cancel })
                    addView(ImageView(appCompatContext).apply { id = R.id.credit_card_logo })
                    addView(TextView(appCompatContext).apply { id = R.id.credit_card_number })
                    addView(TextView(appCompatContext).apply { id = R.id.credit_card_expiration_date })
                }
            }
            .`when`(fragment)
            .onCreateView(any(), any(), any())
        doNothing().`when`(fragment).dismiss()

        val view = fragment.onCreateView(mock(), mock(), mock())
        fragment.onViewCreated(view, mock())

        val buttonView = view.findViewById<Button>(R.id.save_confirm)

        buttonView.performClick()

        verify(mockFeature)
            .onConfirm(
                sessionId = sessionId,
                promptRequestUID = promptRequestUID,
                value = creditCard,
            )
    }

    @Test
    fun `WHEN the cancel button is clicked THEN the prompt feature is notified`() {
        val mockFeature: PromptFeature = mock()
        val fragment =
            spy(
                CreditCardSaveDialogFragment.newInstance(
                    sessionId = sessionId,
                    promptRequestUID = promptRequestUID,
                    shouldDismissOnLoad = true,
                    creditCard = creditCard,
                )
            )

        fragment.feature = mockFeature

        doReturn(appCompatContext).`when`(fragment).requireContext()
        doAnswer {
                FrameLayout(appCompatContext).apply {
                    addView(
                        AppCompatTextView(appCompatContext).apply {
                            id = R.id.save_credit_card_header
                        }
                    )
                    addView(
                        AppCompatTextView(appCompatContext).apply {
                            id = R.id.save_credit_card_message
                        }
                    )
                    addView(Button(appCompatContext).apply { id = R.id.save_confirm })
                    addView(Button(appCompatContext).apply { id = R.id.save_cancel })
                    addView(ImageView(appCompatContext).apply { id = R.id.credit_card_logo })
                    addView(TextView(appCompatContext).apply { id = R.id.credit_card_number })
                    addView(TextView(appCompatContext).apply { id = R.id.credit_card_expiration_date })
                }
            }
            .`when`(fragment)
            .onCreateView(any(), any(), any())
        doNothing().`when`(fragment).dismiss()

        val view = fragment.onCreateView(mock(), mock(), mock())
        fragment.onViewCreated(view, mock())

        val buttonView = view.findViewById<Button>(R.id.save_cancel)

        buttonView.performClick()

        verify(mockFeature)
            .onCancel(
                sessionId = sessionId,
                promptRequestUID = promptRequestUID,
            )
    }

    @Test
    fun `WHEN the confirm save button is clicked THEN the appropriate fact is emitted`() {
        val fragment =
            spy(
                CreditCardSaveDialogFragment.newInstance(
                    sessionId = sessionId,
                    promptRequestUID = promptRequestUID,
                    shouldDismissOnLoad = true,
                    creditCard = creditCard,
                )
            )

        fragment.confirmResult = CreditCardValidationDelegate.Result.CanBeCreated

        CollectionProcessor.withFactCollection { facts ->
            fragment.emitSaveUpdateFact()

            assertEquals(1, facts.size)
            val fact = facts.single()
            assertEquals(Component.FEATURE_PROMPTS, fact.component)
            assertEquals(Action.CONFIRM, fact.action)
            assertEquals(
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_CREATED,
                fact.item,
            )
        }
    }

    @Test
    fun `WHEN the confirm update button is clicked THEN the appropriate fact is emitted`() {
        val fragment =
            spy(
                CreditCardSaveDialogFragment.newInstance(
                    sessionId = sessionId,
                    promptRequestUID = promptRequestUID,
                    shouldDismissOnLoad = true,
                    creditCard = creditCard,
                )
            )

        fragment.confirmResult = CreditCardValidationDelegate.Result.CanBeUpdated(mock())

        CollectionProcessor.withFactCollection { facts ->
            fragment.emitSaveUpdateFact()

            assertEquals(1, facts.size)
            val fact = facts.single()
            assertEquals(Component.FEATURE_PROMPTS, fact.component)
            assertEquals(Action.CONFIRM, fact.action)
            assertEquals(
                CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_UPDATED,
                fact.item,
            )
        }
    }
}

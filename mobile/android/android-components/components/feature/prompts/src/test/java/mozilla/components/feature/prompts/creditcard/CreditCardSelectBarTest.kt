/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import android.widget.LinearLayout
import androidx.appcompat.widget.AppCompatTextView
import androidx.core.view.isVisible
import androidx.recyclerview.widget.RecyclerView
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.feature.prompts.R
import mozilla.components.feature.prompts.concept.ExpandablePrompt
import mozilla.components.feature.prompts.concept.SelectablePromptView
import mozilla.components.feature.prompts.concept.ToggleablePrompt
import mozilla.components.feature.prompts.facts.CreditCardAutofillDialogFacts
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.facts.FactProcessor
import mozilla.components.support.base.facts.Facts
import mozilla.components.support.test.ext.appCompatContext
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class CreditCardSelectBarTest {

    private lateinit var creditCardSelectBar: CreditCardSelectBar

    private val creditCard =
        CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111110",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = "",
            cardCvv = null,
        )

    @Before
    fun setup() {
        creditCardSelectBar = CreditCardSelectBar(appCompatContext)
    }

    @Test
    fun `GIVEN a list of credit cards WHEN prompt is shown THEN credit cards are shown`() {
        creditCardSelectBar.showPrompt()

        assertTrue(creditCardSelectBar.isVisible)
    }

    @Test
    fun `WHEN the prompt is hidden THEN view is hidden`() {
        creditCardSelectBar.hidePrompt()

        assertFalse(creditCardSelectBar.isVisible)
    }

    @Test
    fun `GIVEN a listener WHEN manage credit cards button is clicked THEN onManageOptions is called`() {
        val listener: SelectablePromptView.Listener<CreditCardEntry> = mock()

        assertNull(creditCardSelectBar.selectablePromptListener)

        creditCardSelectBar.selectablePromptListener = listener

        creditCardSelectBar.showPrompt()
        creditCardSelectBar.populate(listOf(creditCard))
        creditCardSelectBar.findViewById<AppCompatTextView>(R.id.manage_credit_cards).performClick()

        verify(listener).onManageOptions()
    }

    @Test
    fun `GIVEN a listener WHEN a credit card is selected THEN onOptionSelect is called`() = runTest {
        val listener: SelectablePromptView.Listener<CreditCardEntry> = mock()
        creditCardSelectBar.selectablePromptListener = listener

        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            }
        )

        creditCardSelectBar.showPrompt()
        creditCardSelectBar.populate(listOf(creditCard))

        val adapter =
            creditCardSelectBar.findViewById<RecyclerView>(R.id.credit_cards_list).adapter as CreditCardsAdapter
        val holder = adapter.onCreateViewHolder(LinearLayout(testContext), 0)
        adapter.bindViewHolder(holder, 0)

        holder.itemView.performClick()

        assertEquals(1, facts.size)

        facts[0].apply {
            assertEquals(Component.FEATURE_PROMPTS, component)
            assertEquals(Action.INTERACTION, action)
            assertEquals(CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_SUCCESS, item)
        }
        verify(listener).onOptionSelect(creditCard)
    }

    @Test
    fun `WHEN the header is clicked THEN view is expanded or collapsed`() {
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            }
        )

        creditCardSelectBar.showPrompt()
        creditCardSelectBar.populate(listOf(creditCard))

        creditCardSelectBar.findViewById<AppCompatTextView>(R.id.select_credit_card_header).performClick()

        assertTrue(creditCardSelectBar.findViewById<RecyclerView>(R.id.credit_cards_list).isVisible)
        assertTrue(creditCardSelectBar.findViewById<AppCompatTextView>(R.id.manage_credit_cards).isVisible)

        assertEquals(1, facts.size)

        facts[0].apply {
            assertEquals(Component.FEATURE_PROMPTS, component)
            assertEquals(Action.INTERACTION, action)
            assertEquals(CreditCardAutofillDialogFacts.Items.AUTOFILL_CREDIT_CARD_PROMPT_EXPANDED, item)
        }

        creditCardSelectBar.findViewById<AppCompatTextView>(R.id.select_credit_card_header).performClick()

        assertFalse(creditCardSelectBar.findViewById<RecyclerView>(R.id.credit_cards_list).isVisible)
        assertFalse(creditCardSelectBar.findViewById<AppCompatTextView>(R.id.manage_credit_cards).isVisible)
    }

    @Test
    fun `WHEN the prompt is shown THEN update state to reflect that and inform listeners about it`() {
        val bar = CreditCardSelectBar(testContext)
        val listener: ToggleablePrompt.Listener = mock()
        bar.toggleablePromptListener = listener

        bar.showPrompt()

        assertTrue(bar.isPromptDisplayed)
        verify(listener).onShown()
    }

    @Test
    fun `WHEN the prompt is hidden THEN update state to reflect that and inform listeners about it`() {
        val bar = CreditCardSelectBar(testContext)
        val listener: ToggleablePrompt.Listener = mock()
        bar.toggleablePromptListener = listener

        bar.hidePrompt()

        assertFalse(bar.isPromptDisplayed)
        verify(listener).onHidden()
    }

    @Test
    fun `WHEN the prompt is expanded THEN inform listeners about it`() {
        val listener: ExpandablePrompt.Listener = mock()
        creditCardSelectBar.expandablePromptListener = listener

        creditCardSelectBar.expand()

        verify(listener).onExpanded()
    }

    @Test
    fun `WHEN the prompt is collapsed THEN inform listeners about it`() {
        val listener: ExpandablePrompt.Listener = mock()
        creditCardSelectBar.expandablePromptListener = listener

        creditCardSelectBar.collapse()

        verify(listener).onCollapsed()
    }
}

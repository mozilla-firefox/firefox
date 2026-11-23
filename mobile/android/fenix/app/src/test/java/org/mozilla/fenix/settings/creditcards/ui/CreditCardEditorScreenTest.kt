/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTextInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CreditCardEditorScreenTest {

    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `WHEN non-digits or more than 4 digits are typed as the security code THEN the field and the store keep the first 4 digits`() {
        val store = CreditCardEditorStore(initialState = createState(showSecurityCodeField = true))
        composeTestRule.setContent { CreditCardEditorScreen(store) }

        val field = composeTestRule.onNodeWithTag(CreditCardEditorTestTags.SECURITY_CODE_FIELD)
        "1a2345".forEach { field.performTextInput(it.toString()) }
        composeTestRule.waitForIdle()

        assertEquals("1234", store.state.securityCode)
        assertEquals(4, field.fetchSemanticsNode().config[SemanticsProperties.EditableText].length)
    }
}

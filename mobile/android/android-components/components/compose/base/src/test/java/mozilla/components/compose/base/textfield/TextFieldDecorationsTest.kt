/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.textfield

import androidx.compose.foundation.text.input.TextObfuscationMode
import androidx.compose.foundation.text.input.rememberTextFieldState
import androidx.compose.material3.Text
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TextFieldDecorationsTest {

    @get:Rule val composeTestRule = createComposeRule()

    private fun setTextField(isError: Boolean) = composeTestRule.setContent {
        TextField(
            value = "",
            onValueChange = {},
            placeholder = "placeholder",
            errorText = "error",
            supportingText = "supporting",
            isError = isError,
            trailingIcon = { Text("trailing") },
        )
    }

    private fun setSecureTextField(isError: Boolean) = composeTestRule.setContent {
        SecureTextField(
            state = rememberTextFieldState(),
            placeholder = "placeholder",
            errorText = "error",
            isError = isError,
            trailingIcon = { Text("trailing") },
        )
    }

    @Test
    fun `WHEN a text field has an error THEN the error text replaces the supporting text and trailing icon`() {
        setTextField(isError = true)

        composeTestRule.onNodeWithText("error").assertExists()
        composeTestRule.onNodeWithText("supporting").assertDoesNotExist()
        composeTestRule.onNodeWithText("trailing").assertDoesNotExist()
    }

    @Test
    fun `WHEN a text field has no error THEN the supporting text and trailing icon are shown`() {
        setTextField(isError = false)

        composeTestRule.onNodeWithText("supporting").assertExists()
        composeTestRule.onNodeWithText("trailing").assertExists()
        composeTestRule.onNodeWithText("error").assertDoesNotExist()
    }

    @Test
    fun `WHEN a secure text field has an error THEN the error text replaces the trailing icon`() {
        setSecureTextField(isError = true)

        composeTestRule.onNodeWithText("error").assertExists()
        composeTestRule.onNodeWithText("trailing").assertDoesNotExist()
    }

    @Test
    fun `WHEN a secure text field has no error THEN the trailing icon is shown without error text`() {
        setSecureTextField(isError = false)

        composeTestRule.onNodeWithText("trailing").assertExists()
        composeTestRule.onNodeWithText("error").assertDoesNotExist()
    }

    private fun assertObfuscated(obfuscated: Boolean) {
        composeTestRule
            .onNode(hasSetTextAction())
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.IsPasswordObfuscated, obfuscated))
    }

    @Test
    fun `WHEN a secure text field uses the default obfuscation mode THEN its input is obfuscated`() {
        setSecureTextField(isError = false)

        assertObfuscated(true)
    }

    @Test
    fun `WHEN a secure text field uses the visible obfuscation mode THEN its input is not obfuscated`() {
        composeTestRule.setContent {
            SecureTextField(
                state = rememberTextFieldState(),
                placeholder = "placeholder",
                errorText = "error",
                textObfuscationMode = TextObfuscationMode.Visible,
            )
        }

        assertObfuscated(false)
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine.prompt

import java.util.Date
import kotlin.test.assertNotNull
import mozilla.components.concept.engine.prompt.PromptRequest.Alert
import mozilla.components.concept.engine.prompt.PromptRequest.Authentication
import mozilla.components.concept.engine.prompt.PromptRequest.Color
import mozilla.components.concept.engine.prompt.PromptRequest.Confirm
import mozilla.components.concept.engine.prompt.PromptRequest.File
import mozilla.components.concept.engine.prompt.PromptRequest.Folder
import mozilla.components.concept.engine.prompt.PromptRequest.FolderUploadPrompt
import mozilla.components.concept.engine.prompt.PromptRequest.MenuChoice
import mozilla.components.concept.engine.prompt.PromptRequest.MultipleChoice
import mozilla.components.concept.engine.prompt.PromptRequest.Popup
import mozilla.components.concept.engine.prompt.PromptRequest.Repost
import mozilla.components.concept.engine.prompt.PromptRequest.SaveLoginPrompt
import mozilla.components.concept.engine.prompt.PromptRequest.SelectCreditCard
import mozilla.components.concept.engine.prompt.PromptRequest.SelectLoginPrompt
import mozilla.components.concept.engine.prompt.PromptRequest.SingleChoice
import mozilla.components.concept.engine.prompt.PromptRequest.TextPrompt
import mozilla.components.concept.engine.prompt.PromptRequest.TimeSelection
import mozilla.components.concept.engine.prompt.PromptRequest.TimeSelection.Type
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PromptRequestTest {

    @Test
    fun `SingleChoice`() {
        val single = SingleChoice(emptyArray(), {}, {})
        single.onConfirm(Choice(id = "", label = ""))
        assertNotNull(single.choices)
    }

    @Test
    fun `MultipleChoice`() {
        val multiple = MultipleChoice(emptyArray(), {}, {})
        multiple.onConfirm(arrayOf(Choice(id = "", label = "")))
        assertNotNull(multiple.choices)
    }

    @Test
    fun `MenuChoice`() {
        val menu = MenuChoice(emptyArray(), {}, {})
        menu.onConfirm(Choice(id = "", label = ""))
        assertNotNull(menu.choices)
    }

    @Test
    fun `Alert`() {
        val alert = Alert("title", "message", true, {}, {})

        assertEquals(alert.title, "title")
        assertEquals(alert.message, "message")
        assertEquals(alert.hasShownManyDialogs, true)

        alert.onDismiss()
        alert.onConfirm(true)

        assertEquals(alert.title, "title")
        assertEquals(alert.message, "message")
        assertEquals(alert.hasShownManyDialogs, true)

        alert.onDismiss()
        alert.onConfirm(true)
    }

    @Test
    fun `TextPrompt`() {
        val textPrompt =
            TextPrompt(
                "title",
                "label",
                "value",
                true,
                { _, _ -> },
                {},
            )

        assertEquals(textPrompt.title, "title")
        assertEquals(textPrompt.inputLabel, "label")
        assertEquals(textPrompt.inputValue, "value")
        assertEquals(textPrompt.hasShownManyDialogs, true)

        textPrompt.onDismiss()
        textPrompt.onConfirm(true, "")
    }

    @Test
    fun `TimeSelection`() {
        val dateRequest =
            TimeSelection(
                "title",
                Date(),
                Date(),
                Date(),
                "1",
                Type.DATE,
                { _ -> },
                {},
                {},
            )

        assertEquals(dateRequest.title, "title")
        assertEquals(dateRequest.type, Type.DATE)
        assertEquals("1", dateRequest.stepValue)
        assertNotNull(dateRequest.initialDate)
        assertNotNull(dateRequest.minimumDate)
        assertNotNull(dateRequest.maximumDate)

        dateRequest.onConfirm(Date())
        dateRequest.onClear()
    }

    @Test
    fun `File`() {
        val filePickerRequest =
            File(
                emptyArray(),
                true,
                PromptRequest.File.FacingMode.NONE,
                { _, _ -> },
                { _, _ -> },
                {},
            )

        assertTrue(filePickerRequest.mimeTypes.isEmpty())
        assertTrue(filePickerRequest.isMultipleFilesSelection)
        assertEquals(filePickerRequest.captureMode, PromptRequest.File.FacingMode.NONE)

        filePickerRequest.onSingleFileSelected(mock(), mock())
        filePickerRequest.onMultipleFilesSelected(mock(), emptyArray())
        filePickerRequest.onDismiss()
    }

    @Test
    fun `Folder`() {
        var onSelectedWasCalled = false
        var onDismissWasCalled = false

        val folderPickerRequest =
            Folder(
                { _, _ -> onSelectedWasCalled = true },
                { onDismissWasCalled = true },
            )

        folderPickerRequest.onSelected(mock(), mock())
        folderPickerRequest.onDismiss()
        assertTrue(onSelectedWasCalled)
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `Authentication`() {
        val promptRequest =
            Authentication(
                "example.org",
                "title",
                "message",
                "username",
                "password",
                PromptRequest.Authentication.Method.HOST,
                PromptRequest.Authentication.Level.NONE,
                false,
                false,
                false,
                { _, _ -> },
                {},
            )

        assertEquals(promptRequest.title, "title")
        assertEquals(promptRequest.message, "message")
        assertEquals(promptRequest.userName, "username")
        assertEquals(promptRequest.password, "password")
        assertFalse(promptRequest.onlyShowPassword)
        assertFalse(promptRequest.previousFailed)
        assertFalse(promptRequest.isCrossOrigin)

        promptRequest.onConfirm("", "")
        promptRequest.onDismiss()
    }

    @Test
    fun `Color`() {
        val onConfirm: (String) -> Unit = {}
        val onDismiss: () -> Unit = {}

        val colorRequest = Color("defaultColor", onConfirm, onDismiss)

        assertEquals(colorRequest.defaultColor, "defaultColor")

        colorRequest.onConfirm("")
        colorRequest.onDismiss()
    }

    @Test
    fun `Popup`() {
        val popupRequest = Popup("http://mozilla.slack.com/", {}, {})

        assertEquals(popupRequest.targetUri, "http://mozilla.slack.com/")

        popupRequest.onAllow()
        popupRequest.onDeny()
    }

    @Test
    fun `Confirm`() {
        val onConfirmPositiveButton: (Boolean) -> Unit = {}
        val onConfirmNegativeButton: (Boolean) -> Unit = {}
        val onConfirmNeutralButton: (Boolean) -> Unit = {}

        val confirmRequest =
            Confirm(
                "title",
                "message",
                false,
                "positive",
                "negative",
                "neutral",
                onConfirmPositiveButton,
                onConfirmNegativeButton,
                onConfirmNeutralButton,
                {},
            )

        assertEquals(confirmRequest.title, "title")
        assertEquals(confirmRequest.message, "message")
        assertEquals(confirmRequest.positiveButtonTitle, "positive")
        assertEquals(confirmRequest.negativeButtonTitle, "negative")
        assertEquals(confirmRequest.neutralButtonTitle, "neutral")

        confirmRequest.onConfirmPositiveButton(true)
        confirmRequest.onConfirmNegativeButton(true)
        confirmRequest.onConfirmNeutralButton(true)
    }

    @Test
    fun `SaveLoginPrompt`() {
        val onLoginDismiss: () -> Unit = {}
        val onLoginConfirm: (LoginEntry) -> Unit = {}
        val entry = LoginEntry("origin", username = "username", password = "password")

        val loginSaveRequest = SaveLoginPrompt(0, listOf(entry), onLoginConfirm, onLoginDismiss)

        assertEquals(loginSaveRequest.logins, listOf(entry))
        assertEquals(loginSaveRequest.hint, 0)

        loginSaveRequest.onConfirm(entry)
        loginSaveRequest.onDismiss()
    }

    @Test
    fun `SelectLoginPrompt`() {
        val onLoginDismiss: () -> Unit = {}
        val onLoginConfirm: (Login) -> Unit = {}
        val login = Login(guid = "test-guid", origin = "origin", username = "username", password = "password")
        val generatedPassword = "generatedPassword123#"

        val loginSelectRequest = SelectLoginPrompt(listOf(login), generatedPassword, onLoginConfirm, onLoginDismiss)

        assertEquals(loginSelectRequest.logins, listOf(login))
        assertEquals(loginSelectRequest.generatedPassword, generatedPassword)

        loginSelectRequest.onConfirm(login)
        loginSelectRequest.onDismiss()
    }

    @Test
    fun `Repost`() {
        var onAcceptWasCalled = false
        var onDismissWasCalled = false

        val repostRequest =
            Repost(
                onConfirm = {
                    onAcceptWasCalled = true
                },
                onDismiss = {
                    onDismissWasCalled = true
                },
            )

        repostRequest.onConfirm()
        repostRequest.onDismiss()

        assertTrue(onAcceptWasCalled)
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `FolderUploadPrompt`() {
        var onAcceptWasCalled = false
        var onDismissWasCalled = false

        val confirmRequest =
            FolderUploadPrompt(
                folderName = "",
                onConfirm = {
                    onAcceptWasCalled = true
                },
                onDismiss = {
                    onDismissWasCalled = true
                },
            )

        confirmRequest.onConfirm()
        confirmRequest.onDismiss()

        assertTrue(onAcceptWasCalled)
        assertTrue(onDismissWasCalled)
    }

    @Test
    fun `GIVEN a list of credit cards WHEN SelectCreditCard is confirmed or dismissed THEN their respective callback is invoked`() {
        val creditCard =
            CreditCardEntry(
                guid = "id",
                name = "Banana Apple",
                number = "4111111111111110",
                expiryMonth = "5",
                expiryYear = "2030",
                cardType = "amex",
                cardCvv = null,
            )
        var onDismissCalled = false
        var onConfirmCalled = false
        var confirmedCreditCard: CreditCardEntry? = null

        val selectCreditCardRequest =
            SelectCreditCard(
                creditCards = listOf(creditCard),
                onDismiss = {
                    onDismissCalled = true
                },
                onConfirm = {
                    confirmedCreditCard = it
                    onConfirmCalled = true
                },
            )

        assertEquals(selectCreditCardRequest.creditCards, listOf(creditCard))

        selectCreditCardRequest.onConfirm(creditCard)

        assertTrue(onConfirmCalled)
        assertFalse(onDismissCalled)
        assertEquals(creditCard, confirmedCreditCard)

        onConfirmCalled = false
        confirmedCreditCard = null

        selectCreditCardRequest.onDismiss()

        assertTrue(onDismissCalled)
        assertFalse(onConfirmCalled)
        assertNull(confirmedCreditCard)
    }

    @Test
    fun `WHEN calling confirm or dismiss on the SelectAddress prompt request THEN the respective callback is invoked`() {
        val address =
            Address(
                guid = "1",
                name = "Firefox",
                organization = "-",
                streetAddress = "street",
                addressLevel3 = "address3",
                addressLevel2 = "address2",
                addressLevel1 = "address1",
                postalCode = "1",
                country = "Country",
                tel = "1",
                email = "@",
            )
        var onDismissCalled = false
        var onConfirmCalled = false
        var confirmedAddress: Address? = null

        val selectAddresPromptRequest =
            PromptRequest.SelectAddress(
                addresses = listOf(address),
                onDismiss = {
                    onDismissCalled = true
                },
                onConfirm = {
                    confirmedAddress = it
                    onConfirmCalled = true
                },
            )

        assertEquals(selectAddresPromptRequest.addresses, listOf(address))

        selectAddresPromptRequest.onConfirm(address)

        assertTrue(onConfirmCalled)
        assertFalse(onDismissCalled)
        assertEquals(address, confirmedAddress)

        onConfirmCalled = false

        selectAddresPromptRequest.onDismiss()

        assertTrue(onDismissCalled)
        assertFalse(onConfirmCalled)
    }
}

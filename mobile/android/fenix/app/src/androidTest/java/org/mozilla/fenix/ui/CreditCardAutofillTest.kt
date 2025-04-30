/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.bringAppToForeground
import org.mozilla.fenix.helpers.AppAndSystemHelper.putAppToBackground
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import java.time.LocalDate

class CreditCardAutofillTest : TestSetup() {
    object MockCreditCard1 {
        const val MOCK_CREDIT_CARD_NUMBER = "5555555555554444"
        const val MOCK_LAST_CARD_DIGITS = "4444"
        const val MOCK_NAME_ON_CARD = "Mastercard"
        const val MOCK_EXPIRATION_MONTH = "February"
        val MOCK_EXPIRATION_YEAR = (LocalDate.now().year + 1).toString()
        val MOCK_EXPIRATION_MONTH_AND_YEAR = "02/${(LocalDate.now().year + 1)}"
    }

    object MockCreditCard2 {
        const val MOCK_CREDIT_CARD_NUMBER = "2720994326581252"
        const val MOCK_LAST_CARD_DIGITS = "1252"
        const val MOCK_NAME_ON_CARD = "Mastercard"
        const val MOCK_EXPIRATION_MONTH = "March"
        val MOCK_EXPIRATION_YEAR = (LocalDate.now().year + 2).toString()
        val MOCK_EXPIRATION_MONTH_AND_YEAR = "03/${(LocalDate.now().year + 2)}"
    }

    @get:Rule
    val activityIntentTestRule = HomeActivityIntentTestRule.withDefaultSettingsOverrides()

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512792
    @SmokeTest
    @Test
    fun verifyCreditCardAutofillTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            // Opening Manage cards to dismiss here the Secure your credit prompt
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
        }.goBackToAutofillSettings {
        }.goBack {
        }.goBack {
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            clickCreditCardNumberTextBox()
            clickPageObject(itemWithResId("$packageName:id/select_credit_card_header"))
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/credit_card_number",
                    MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                ),
            )
            verifyAutofilledCreditCard(MockCreditCard1.MOCK_CREDIT_CARD_NUMBER)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512798
    @SmokeTest
    @Test
    fun deleteSavedCreditCardUsingToolbarButtonTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            clickSavedCreditCard()
            clickDeleteCreditCardToolbarButton()
            clickCancelDeleteCreditCardButton()
            verifyEditCreditCardToolbarTitle()
            clickDeleteCreditCardToolbarButton()
            clickConfirmDeleteCreditCardButton()
            verifyAddCreditCardsButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2271192
    @SmokeTest
    @Test
    fun deleteSavedCreditCardUsingMenuButtonTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            clickSavedCreditCard()
            clickDeleteCreditCardMenuButton()
            clickCancelDeleteCreditCardButton()
            verifyEditCreditCardToolbarTitle()
            clickDeleteCreditCardMenuButton()
            clickConfirmDeleteCreditCardButton()
            verifyAddCreditCardsButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512788
    @Test
    fun verifyCreditCardsSectionTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            verifySavedCreditCardsSection(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1859917
    @Test
    fun verifyManageCreditCardsPromptOptionTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            clickCreditCardNumberTextBox()
            clickPageObject(itemWithResId("$packageName:id/select_credit_card_header"))
        }.clickManageCreditCardsButton {
        }.goBackToBrowser {
            verifySelectCreditCardPromptExists(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512790
    @Test
    fun verifyCreditCardsAutofillToggleTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            clickCreditCardNumberTextBox()
            verifySelectCreditCardPromptExists(true)
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            clickSaveAndAutofillCreditCardsOption()
            verifyCreditCardsAutofillSection(false, true)
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            clickCreditCardNumberTextBox()
            verifySelectCreditCardPromptExists(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512795
    @Test
    fun verifyEditCardsViewTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            verifySavedCreditCardsSection(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
            clickSavedCreditCard()
            verifyEditCreditCardView(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
        }.goBackToSavedCreditCards {
            verifySavedCreditCardsSection(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512796
    @Test
    fun verifyEditedCardIsSavedTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            verifySavedCreditCardsSection(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
            clickSavedCreditCard()
            fillAndSaveCreditCard(
                MockCreditCard2.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard2.MOCK_NAME_ON_CARD,
                MockCreditCard2.MOCK_EXPIRATION_MONTH,
                MockCreditCard2.MOCK_EXPIRATION_YEAR,
            )
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            clickCreditCardNumberTextBox()
            clickPageObject(itemWithResId("$packageName:id/select_credit_card_header"))
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/credit_card_number",
                    MockCreditCard2.MOCK_LAST_CARD_DIGITS,
                ),
            )
            verifyAutofilledCreditCard(MockCreditCard2.MOCK_CREDIT_CARD_NUMBER)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512797
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=1935999"])
    fun verifyCreditCardCannotBeSavedWithoutCardNumberOrNameTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            verifySavedCreditCardsSection(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
            clickSavedCreditCard()
            clearCreditCardNumber()
            clickSaveCreditCardToolbarButton()
            verifyEditCreditCardToolbarTitle()
            verifyCreditCardNumberErrorMessage()
        }.goBackToSavedCreditCards {
            clickSavedCreditCard()
            clearNameOnCreditCard()
            clickSaveCreditCardToolbarButton()
            verifyEditCreditCardToolbarTitle()
            verifyNameOnCreditCardErrorMessage()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512794
    @Test
    fun verifyMultipleCreditCardsCanBeAddedTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard2.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard2.MOCK_NAME_ON_CARD,
                MockCreditCard2.MOCK_EXPIRATION_MONTH,
                MockCreditCard2.MOCK_EXPIRATION_YEAR,
            )
            verifySavedCreditCardsSection(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
            verifySavedCreditCardsSection(
                MockCreditCard2.MOCK_LAST_CARD_DIGITS,
                MockCreditCard2.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            clickCreditCardNumberTextBox()
            clickPageObject(itemWithResId("$packageName:id/select_credit_card_header"))
            verifyCreditCardSuggestion(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard2.MOCK_LAST_CARD_DIGITS,
            )
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/credit_card_number",
                    MockCreditCard2.MOCK_LAST_CARD_DIGITS,
                ),
            )
            verifyAutofilledCreditCard(MockCreditCard2.MOCK_CREDIT_CARD_NUMBER)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2271304
    @Test
    fun verifyDoNotSaveCreditCardFromPromptTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
            clickNegativeSaveCreditCardPromptButton()
            verifyUpdateOrSaveCreditCardPromptExists(exists = false)
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1779194
    @Test
    fun verifySaveCreditCardFromPromptTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
            clickPageObject(itemWithResId("$packageName:id/save_confirm"))
            verifyUpdateOrSaveCreditCardPromptExists(exists = false)
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, true)
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            verifySavedCreditCardsSection(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2271305
    @Test
    fun verifyCancelCreditCardUpdatePromptTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard2.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard2.MOCK_NAME_ON_CARD,
                MockCreditCard2.MOCK_EXPIRATION_MONTH,
                MockCreditCard2.MOCK_EXPIRATION_YEAR,
            )
            // Opening Manage cards to dismiss here the Secure your credit prompt
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            clickCreditCardNumberTextBox()
            clickPageObject(itemWithResId("$packageName:id/select_credit_card_header"))
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/credit_card_number",
                    MockCreditCard2.MOCK_LAST_CARD_DIGITS,
                ),
            )
            verifyAutofilledCreditCard(MockCreditCard2.MOCK_CREDIT_CARD_NUMBER)
            changeCreditCardExpiryDate(MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR)
            clickCreditCardFormSubmitButton()
            clickNegativeSaveCreditCardPromptButton()
            verifyUpdateOrSaveCreditCardPromptExists(false)
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, true)
            clickManageSavedCreditCardsButton()
            verifySavedCreditCardsSection(
                MockCreditCard2.MOCK_LAST_CARD_DIGITS,
                MockCreditCard2.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1779195
    @Test
    fun verifyConfirmCreditCardUpdatePromptTest() {
        val creditCardFormPage = TestAssetHelper.getCreditCardFormAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard2.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard2.MOCK_NAME_ON_CARD,
                MockCreditCard2.MOCK_EXPIRATION_MONTH,
                MockCreditCard2.MOCK_EXPIRATION_YEAR,
            )
            // Opening Manage cards to dismiss here the Secure your credit prompt
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(creditCardFormPage.url) {
            clickCreditCardNumberTextBox()
            clickPageObject(itemWithResId("$packageName:id/select_credit_card_header"))
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/credit_card_number",
                    MockCreditCard2.MOCK_LAST_CARD_DIGITS,
                ),
            )
            verifyAutofilledCreditCard(MockCreditCard2.MOCK_CREDIT_CARD_NUMBER)
            changeCreditCardExpiryDate(MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR)
            clickCreditCardFormSubmitButton()
            clickPageObject(itemWithResId("$packageName:id/save_confirm"))
            verifyUpdateOrSaveCreditCardPromptExists(false)
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, true)
            clickManageSavedCreditCardsButton()
            verifySavedCreditCardsSection(
                MockCreditCard2.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1512791
    @Test
    fun verifyCreditCardRedirectionsToAutofillSectionAfterInterruptionTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyCreditCardsAutofillSection(true, false)
            clickAddCreditCardButton()
            fillAndSaveCreditCard(
                MockCreditCard1.MOCK_CREDIT_CARD_NUMBER,
                MockCreditCard1.MOCK_NAME_ON_CARD,
                MockCreditCard1.MOCK_EXPIRATION_MONTH,
                MockCreditCard1.MOCK_EXPIRATION_YEAR,
            )
            clickManageSavedCreditCardsButton()
            clickSecuredCreditCardsLaterButton()
            clickSavedCreditCard()
            putAppToBackground()
            bringAppToForeground()
            verifyAutofillToolbarTitle()
            clickManageSavedCreditCardsButton()
            verifySavedCreditCardsSection(
                MockCreditCard1.MOCK_LAST_CARD_DIGITS,
                MockCreditCard1.MOCK_EXPIRATION_MONTH_AND_YEAR,
            )
            putAppToBackground()
            bringAppToForeground()
            verifyAutofillToolbarTitle()
        }
    }
}

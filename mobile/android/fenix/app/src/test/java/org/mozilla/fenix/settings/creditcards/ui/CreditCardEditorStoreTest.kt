/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.NewCreditCardFields
import mozilla.components.concept.storage.UpdatableCreditCardFields
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.CreditCardNetworkType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.CreditCards
import org.mozilla.fenix.helpers.FenixGleanTestRule

@RunWith(AndroidJUnit4::class)
class CreditCardEditorStoreTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val creditCardsStorage = FakeCreditCardsStorage()
    private val calendarDataProvider =
        FakeCalendarDataProvider(
            expectedMonths = listOf("January", "February", "March"),
            expectedYears = listOf("2025", "2026", "2027"),
        )

    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `WHEN DeleteClicked event is received, delete confirmation dialog is shown`() =
        runTest(testDispatcher) {
            val store = makeStore(scope = this)

            store.dispatch(CreditCardEditorAction.DeleteClicked)
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(
                "Delete confirmation dialog is not shown",
                store.state.showDeleteDialog,
            )
        }

    @Test
    fun `WHEN delete confirmation is cancelled, the dialog is hidden`() =
        runTest(testDispatcher) {
            val store = makeStore(scope = this)

            store.dispatch(CreditCardEditorAction.DeleteClicked)
            store.dispatch(CreditCardEditorAction.DeleteDialogAction.Cancel)
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(
                "Expected delete confirmation dialog to be hidden",
                store.state.showDeleteDialog,
            )
        }

    @Test
    fun `WHEN delete confirmation is confirmed, the card is deleted`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state = createState(guid = "card-id"),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.DeleteDialogAction.Confirm)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                "Expected that the deleted card has guid 'card-id'",
                "card-id",
                creditCardsStorage.deletedCard,
            )
        }

    @Test
    fun `GIVEN valid form in 'edit' mode, WHEN save action is received, the card is saved`() =
        runTest(testDispatcher) {
            val validMasterCard = "5555444433331111"
            val store =
                makeStore(
                    state =
                        createState(
                            guid = "1234",
                            inEditMode = true,
                            nameOnCard = "Jane Doe",
                            cardNumber = validMasterCard,
                            expiryYears = listOf("2025", "2026", "2027"),
                            selectedExpiryYearIndex = 1,
                            expiryMonths = listOf("January", "February", "March"),
                            selectedExpiryMonthIndex = 0,
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            val updatedCardGuid = creditCardsStorage.updatedCard?.first
            val updatedCardFields = creditCardsStorage.updatedCard?.second
            assertEquals(
                "Expected that a card is updated with the guid is 1234",
                "1234",
                updatedCardGuid,
            )
            assertEquals(
                "Expected that a card is updated with the right fields",
                UpdatableCreditCardFields(
                    billingName = "Jane Doe",
                    cardNumber = validMasterCard,
                    expiryMonth = 1,
                    expiryYear = 2026L,
                    cardType = "mastercard",
                    cardCvv = null,
                ),
                updatedCardFields,
            )
        }

    @Test
    fun `GIVEN valid form in 'create' mode, WHEN save action is received, the card is saved`() =
        runTest(testDispatcher) {
            val validMasterCard = "5555444433331111"
            val store =
                makeStore(
                    state =
                        createState(
                            nameOnCard = "Jane Doe",
                            cardNumber = validMasterCard,
                            expiryYears = listOf("2025", "2026", "2027"),
                            selectedExpiryYearIndex = 1,
                            expiryMonths = listOf("January", "February", "March"),
                            selectedExpiryMonthIndex = 0,
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                "Expected that a card is successfully saved with the right values",
                NewCreditCardFields(
                    billingName = "Jane Doe",
                    cardNumber = validMasterCard,
                    expiryMonth = 1,
                    expiryYear = 2026L,
                    cardType = "mastercard",
                    cardCvv = null,
                ),
                creditCardsStorage.newAddedCard,
            )
        }

    @Test
    fun `GIVEN invalid card number, WHEN save action is received, an error is shown on the card field`() =
        runTest(testDispatcher) {
            val americanExpressCardInvalid = "371449635398432"
            val store =
                makeStore(
                    state = createState(cardNumber = americanExpressCardInvalid),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(
                "Expected that an error is shown on the card number field",
                store.state.showCardNumberError,
            )
        }

    @Test
    fun `GIVEN empty card number, WHEN save action is received, an error is shown on the card field`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state = createState(cardNumber = ""),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(
                "Expected that an error is shown on the card number field",
                store.state.showCardNumberError,
            )
        }

    @Test
    fun `GIVEN an empty name on card, WHEN save action is received, an error is shown on the name field`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state = createState(nameOnCard = ""),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(
                "Expected that an error is shown on the name field",
                store.state.showNameOnCardError,
            )
        }

    @Test
    fun `WHEN cancel action is received, user is navigated back`() =
        runTest(testDispatcher) {
            var navigatedBack = false
            val store =
                makeStore(
                    environment =
                        CreditCardEditorEnvironment.Default.copy(
                            navigateBack = {
                                navigatedBack = true
                            }
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Cancel)

            assertTrue(
                "Expected that we navigate back",
                navigatedBack,
            )
        }

    @Test
    fun `WHEN 'navigate back' action is received, user is navigated back`() =
        runTest(testDispatcher) {
            var navigatedBack = false
            val store =
                makeStore(
                    environment =
                        CreditCardEditorEnvironment.Default.copy(
                            navigateBack = {
                                navigatedBack = true
                            }
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.NavigateBack)

            assertTrue(
                "Expected that we navigate back",
                navigatedBack,
            )
        }

    @Test
    fun `WHEN Initializing without a credit card, the state is correct`() =
        runTest(testDispatcher) {
            calendarDataProvider.expectedMonths = listOf("January", "February")
            calendarDataProvider.expectedYears = listOf("2025", "2026")

            val initialState = CreditCardEditorState.Default
            val store = makeStore(state = initialState, scope = this)

            store.dispatch(CreditCardEditorAction.Initialization.InitStarted(creditCard = null))

            assertEquals(
                "Expected that the state is loaded with everything empty",
                CreditCardEditorState(
                    guid = "",
                    cardNumber = "",
                    showCardNumberError = false,
                    nameOnCard = "",
                    showNameOnCardError = false,
                    expiryMonths = listOf("January", "February"),
                    selectedExpiryMonthIndex = 0,
                    expiryYears = listOf("2025", "2026"),
                    selectedExpiryYearIndex = 0,
                    inEditMode = false,
                    showDeleteDialog = false,
                ),
                store.state,
            )
        }

    @Test
    fun `WHEN Initializing with a credit card, the state is initialized with the card details`() =
        runTest(testDispatcher) {
            calendarDataProvider.expectedMonths = listOf("January", "February", "March")
            calendarDataProvider.expectedYears = listOf("2025", "2026")
            val expectedPlainCardNumber = "5555444433331111"

            val creditCard =
                CreditCard(
                    guid = "id",
                    billingName = "Banana Apple",
                    cardNumber = expectedPlainCardNumber,
                    cardNumberLast4 = "1111",
                    expiryMonth = 2,
                    expiryYear = 2025,
                    cardType = CreditCardNetworkType.MASTERCARD.cardName,
                    timeCreated = 1L,
                    timeLastUsed = 1L,
                    timeLastModified = 1L,
                    timesUsed = 1L,
                    cardCvv = null,
                )

            val store = makeStore(state = CreditCardEditorState.Default, scope = this)

            store.dispatch(CreditCardEditorAction.Initialization.InitStarted(creditCard = creditCard))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                "Expected that the state is initialized with the card details",
                CreditCardEditorState(
                    guid = "id",
                    cardNumber = expectedPlainCardNumber,
                    showCardNumberError = false,
                    nameOnCard = "Banana Apple",
                    showNameOnCardError = false,
                    expiryMonths = listOf("January", "February", "March"),
                    selectedExpiryMonthIndex = 1,
                    expiryYears = listOf("2025", "2026"),
                    selectedExpiryYearIndex = 0,
                    inEditMode = true,
                    showDeleteDialog = false,
                ),
                store.state,
            )
        }

    @Test
    fun `WHEN a card is deleted, then a telemetry event is sent`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state = createState(guid = "card-id"),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.DeleteDialogAction.Confirm)
            testDispatcher.scheduler.advanceUntilIdle()

            // verify that the event is sent
            assertNotNull(CreditCards.deleted.testGetValue())
        }

    @Test
    fun `WHEN a card is saved, THEN a telemetry event is sent`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state =
                        createState(
                            nameOnCard = "Jane Doe",
                            cardNumber = "5555444433331111",
                            expiryYears = listOf("2025", "2026", "2027"),
                            selectedExpiryYearIndex = 1,
                            expiryMonths = listOf("January", "February", "March"),
                            selectedExpiryMonthIndex = 0,
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            // verify that the event is sent
            assertNotNull(CreditCards.saved.testGetValue())
        }

    @Test
    fun `WHEN a card is updated, THEN a telemetry event is sent`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state =
                        createState(
                            guid = "1234",
                            inEditMode = true,
                            nameOnCard = "Jane Doe",
                            cardNumber = "5555444433331111",
                            expiryYears = listOf("2025", "2026", "2027"),
                            selectedExpiryYearIndex = 1,
                            expiryMonths = listOf("January", "February", "March"),
                            selectedExpiryMonthIndex = 0,
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            // verify that the event is sent
            assertNotNull(CreditCards.modified.testGetValue())
        }

    private fun makeStore(
        state: CreditCardEditorState = createState(),
        monthsProvider: CalendarDataProvider = calendarDataProvider,
        storage: CreditCardsAddressesStorage = creditCardsStorage,
        scope: CoroutineScope,
        environment: CreditCardEditorEnvironment = CreditCardEditorEnvironment.Default,
    ): CreditCardEditorStore {
        return CreditCardEditorStore(
            initialState = state,
            middleware =
                listOf(
                    CreditCardEditorMiddleware(
                        environment = environment,
                        calendarDataProvider = monthsProvider,
                        storage = storage,
                        coroutineScope = scope,
                        ioDispatcher = testDispatcher,
                        mainDispatcher = testDispatcher,
                    )
                ),
        )
    }

    @Test
    fun `GIVEN a security code in 'create' mode WHEN saving THEN the card is saved with the security code`() =
        runTest(testDispatcher) {
            val store = makeStore(state = createState(showSecurityCodeField = true, securityCode = "123"), scope = this)

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals("123", creditCardsStorage.newAddedCard?.cardCvv)
        }

    @Test
    fun `GIVEN an empty security code in 'create' mode WHEN saving THEN the card is saved without a security code`() =
        runTest(testDispatcher) {
            val store = makeStore(state = createState(showSecurityCodeField = true, securityCode = ""), scope = this)

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertNotNull(creditCardsStorage.newAddedCard)
            assertEquals(null, creditCardsStorage.newAddedCard?.cardCvv)
        }

    @Test
    fun `GIVEN a card without a security code in 'edit' mode WHEN saving THEN the card is saved without a security code`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state =
                        createState(
                            guid = "1234",
                            inEditMode = true,
                            showSecurityCodeField = true,
                            securityCode = "",
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.FieldChanged.NameOnCardChanged("New Name"))
            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(store.state.showSecurityCodeError)
            assertEquals("1234", creditCardsStorage.updatedCard?.first)
            assertEquals("New Name", creditCardsStorage.updatedCard?.second?.billingName)
            assertEquals(null, creditCardsStorage.updatedCard?.second?.cardCvv)
            assertEquals(null, creditCardsStorage.newAddedCard)
        }

    @Test
    fun `GIVEN the security code field is hidden WHEN saving THEN the card is saved without a security code`() =
        runTest(testDispatcher) {
            val store =
                makeStore(state = createState(showSecurityCodeField = false, securityCode = "123"), scope = this)

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(null, creditCardsStorage.newAddedCard?.cardCvv)
        }

    @Test
    fun `GIVEN an unchanged security code in 'edit' mode WHEN saving THEN the stored security code is kept`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state =
                        createState(
                            guid = "1234",
                            inEditMode = true,
                            showSecurityCodeField = true,
                            securityCode = "123",
                            isSecurityCodeChanged = false,
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                "1234" to
                    UpdatableCreditCardFields(
                        billingName = "Jane Doe",
                        cardNumber = "5555444433331111",
                        expiryMonth = 1,
                        expiryYear = 2026L,
                        cardType = "mastercard",
                        cardCvv = null,
                    ),
                creditCardsStorage.updatedCard,
            )
        }

    @Test
    fun `GIVEN an unchanged stored security code of the wrong length in 'edit' mode WHEN saving THEN the card is saved and the code is kept`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state =
                        createState(
                            guid = "1234",
                            inEditMode = true,
                            cardNumber = "5555444433331111",
                            showSecurityCodeField = true,
                            securityCode = "1234",
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.FieldChanged.NameOnCardChanged("New Name"))
            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(store.state.showSecurityCodeError)
            assertEquals("New Name", creditCardsStorage.updatedCard?.second?.billingName)
            assertEquals(null, creditCardsStorage.updatedCard?.second?.cardCvv)
        }

    @Test
    fun `GIVEN a changed security code in 'edit' mode WHEN saving THEN the security code is replaced`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state = createState(guid = "1234", inEditMode = true, showSecurityCodeField = true),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.FieldChanged.SecurityCodeChanged("456"))
            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals("456", creditCardsStorage.updatedCard?.second?.cardCvv)
        }

    @Test
    fun `GIVEN a cleared security code in 'edit' mode WHEN saving THEN the stored security code is removed`() =
        runTest(testDispatcher) {
            val store =
                makeStore(
                    state =
                        createState(
                            guid = "1234",
                            inEditMode = true,
                            showSecurityCodeField = true,
                            securityCode = "123",
                        ),
                    scope = this,
                )

            store.dispatch(CreditCardEditorAction.FieldChanged.SecurityCodeCleared)
            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals("", creditCardsStorage.updatedCard?.second?.cardCvv)
        }

    @Test
    fun `GIVEN an invalid security code WHEN saving THEN an error is shown and the card is not saved`() =
        runTest(testDispatcher) {
            val store = makeStore(state = createState(showSecurityCodeField = true), scope = this)

            store.dispatch(CreditCardEditorAction.FieldChanged.SecurityCodeChanged("12"))
            store.dispatch(CreditCardEditorAction.Save)
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(store.state.showSecurityCodeError)
            assertEquals(null, creditCardsStorage.newAddedCard)
        }

    @Test
    fun `GIVEN the security code field is shown WHEN initializing with a card THEN the stored security code is shown`() =
        runTest(testDispatcher) {
            val store =
                makeStore(state = CreditCardEditorState.Default.copy(showSecurityCodeField = true), scope = this)

            store.dispatch(CreditCardEditorAction.Initialization.InitStarted(creditCard = creditCardWithCvv()))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals("123", store.state.securityCode)
            assertFalse(store.state.isSecurityCodeChanged)
        }

    @Test
    fun `GIVEN the security code field is hidden WHEN initializing with a card THEN the stored security code is not loaded`() =
        runTest(testDispatcher) {
            val store = makeStore(state = CreditCardEditorState.Default, scope = this)

            store.dispatch(CreditCardEditorAction.Initialization.InitStarted(creditCard = creditCardWithCvv()))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals("", store.state.securityCode)
        }

    private fun creditCardWithCvv() =
        CreditCard(
            guid = "id",
            billingName = "Banana Apple",
            cardNumber = "5555444433331111",
            cardNumberLast4 = "1111",
            expiryMonth = 2,
            expiryYear = 2025,
            cardType = CreditCardNetworkType.MASTERCARD.cardName,
            cardCvv = "123",
        )
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.ui

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.NewCreditCardFields
import mozilla.components.concept.storage.UpdatableCreditCardFields
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.utils.creditCardIIN
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.CreditCards
import org.mozilla.fenix.settings.creditcards.ui.CreditCardEditorAction.DeleteDialogAction

/**
 * Middleware for the credit card editor feature
 *
 * @param environment The [CreditCardEditorEnvironment] to use for external lifecycle-sensitive things.
 * @param storage The [CreditCardsAddressesStorage] to use for adding and updating credit cards.
 * @param calendarDataProvider The [CalendarDataProvider] to use for providing calendar data.
 * @param coroutineScope The [CoroutineScope] to use for launching coroutines.
 * @param ioDispatcher The [CoroutineDispatcher] to use for executing IO operations.
 * @param mainDispatcher The [CoroutineDispatcher] to use for executing main-thread operations.
 */
internal class CreditCardEditorMiddleware(
    private var environment: CreditCardEditorEnvironment? = null,
    private val storage: CreditCardsAddressesStorage,
    private val calendarDataProvider: CalendarDataProvider = DefaultCalendarDataProvider(),
    private val coroutineScope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : Middleware<CreditCardEditorState, CreditCardEditorAction> {

    override fun invoke(
        store: Store<CreditCardEditorState, CreditCardEditorAction>,
        next: (CreditCardEditorAction) -> Unit,
        action: CreditCardEditorAction,
    ) {
        next(action)
        when (action) {
            is CreditCardEditorAction.Initialization -> action.handleInitAction(store)
            is DeleteDialogAction -> action.handleDeleteDialog(store)

            is CreditCardEditorAction.Save -> handleSaveAction(store)
            is CreditCardEditorAction.NavigateBack,
            is CreditCardEditorAction.Cancel -> navigateBack()

            else -> Unit
        }
    }

    private fun DeleteDialogAction.handleDeleteDialog(store: Store<CreditCardEditorState, CreditCardEditorAction>) {
        when (this) {
            DeleteDialogAction.Confirm -> {
                coroutineScope.launch(ioDispatcher) {
                    storage.deleteCreditCard(store.state.guid)

                    withContext(mainDispatcher) {
                        navigateBack()
                    }
                    CreditCards.deleted.add()
                }
            }

            else -> Unit
        }
    }

    private fun navigateBack() {
        environment?.navigateBack()
    }

    private fun handleSaveAction(store: Store<CreditCardEditorState, CreditCardEditorAction>) {
        val state = store.state

        if (!state.showCardNumberError && !state.showNameOnCardError) {
            addOrUpdateCard(state)
        }
    }

    private fun addOrUpdateCard(state: CreditCardEditorState) {
        if (state.inEditMode) {
            updateCreditCard(state)
        } else {
            addCreditCard(state)
        }
    }

    private fun updateCreditCard(state: CreditCardEditorState) {
        coroutineScope.launch(ioDispatcher) {
            val fields =
                UpdatableCreditCardFields(
                    billingName = state.nameOnCard,
                    cardNumber = state.cardNumber,
                    expiryMonth = state.selectedExpiryMonthIndex + 1L,
                    expiryYear = state.expiryYears[state.selectedExpiryYearIndex].toLong(),
                    cardType = state.cardNumber.creditCardIIN()?.creditCardIssuerNetwork?.name ?: "",
                    cardCvv = null,
                )

            storage.updateCreditCard(state.guid, fields)

            withContext(mainDispatcher) {
                navigateBack()
            }
            CreditCards.modified.record(NoExtras())
        }
    }

    private fun addCreditCard(state: CreditCardEditorState) {
        coroutineScope.launch(ioDispatcher) {
            val fields =
                NewCreditCardFields(
                    billingName = state.nameOnCard,
                    cardNumber = state.cardNumber,
                    expiryMonth = state.selectedExpiryMonthIndex + 1L,
                    expiryYear = state.expiryYears[state.selectedExpiryYearIndex].toLong(),
                    cardType = state.cardNumber.creditCardIIN()?.creditCardIssuerNetwork?.name ?: "",
                    cardCvv = null,
                )

            storage.addCreditCard(fields)

            withContext(mainDispatcher) {
                navigateBack()
            }
            CreditCards.saved.add()
        }
    }

    private fun CreditCardEditorAction.Initialization.handleInitAction(
        store: Store<CreditCardEditorState, CreditCardEditorAction>
    ) {
        when (this) {
            is CreditCardEditorAction.Initialization.InitStarted -> {
                if (creditCard != null) {
                    initializeFromCard(store, creditCard)
                } else {
                    initializeFromScratch(store)
                }
            }

            else -> Unit
        }
    }

    private fun initializeFromScratch(store: Store<CreditCardEditorState, CreditCardEditorAction>) {
        val state = store.state
        store.dispatch(
            CreditCardEditorAction.Initialization.InitCompleted(
                state =
                    state.copy(
                        expiryMonths = calendarDataProvider.months(),
                        selectedExpiryMonthIndex = 0,
                        expiryYears = calendarDataProvider.years(),
                        selectedExpiryYearIndex = 0,
                        inEditMode = false,
                        showDeleteDialog = false,
                    )
            )
        )
    }

    private fun initializeFromCard(
        store: Store<CreditCardEditorState, CreditCardEditorAction>,
        creditCard: CreditCard,
    ) {
        coroutineScope.launch(ioDispatcher) {
            val state = store.state

            val years = calendarDataProvider.years(creditCard.expiryYear)

            store.dispatch(
                CreditCardEditorAction.Initialization.InitCompleted(
                    state =
                        state.copy(
                            guid = creditCard.guid,
                            nameOnCard = creditCard.billingName,
                            cardNumber = creditCard.cardNumber,
                            expiryMonths = calendarDataProvider.months(),
                            selectedExpiryMonthIndex = creditCard.expiryMonth.toInt() - 1,
                            expiryYears = years,
                            selectedExpiryYearIndex =
                                years.indexOfFirst { year ->
                                    year == creditCard.expiryYear.toString()
                                },
                            inEditMode = true,
                            showDeleteDialog = false,
                        )
                )
            )
        }
    }
}

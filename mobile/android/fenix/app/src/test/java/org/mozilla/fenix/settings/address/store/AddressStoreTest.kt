/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.address.store

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.engine.autofill.AddressStructure
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.UpdatableAddressFields
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AddressStoreTest {

    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `GIVEN a store WHEN a user edits an address THEN the address structure is loaded`() =
        runTest(testDispatcher) {
            val expectedAddressStructure =
                AddressStructure(
                    listOf(
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.Name,
                            AddressStructure.Field.LocalizationKey.Name,
                        ),
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.Organization,
                            AddressStructure.Field.LocalizationKey.Organization,
                        ),
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.StreetAddress,
                            AddressStructure.Field.LocalizationKey.StreetAddress,
                        ),
                    )
                )

            val store =
                makeStore(this) {
                    copy(getAddressStructure = { _ -> expectedAddressStructure })
                }

            store.dispatch(ViewAppeared)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                AddressStructureState.Loaded(expectedAddressStructure),
                store.state.structureState,
            )
        }

    @Test
    fun `GIVEN a store WHEN a user edits an address and changes the country THEN the address structure is loaded`() =
        runTest(testDispatcher) {
            val expectedAddressStructure =
                AddressStructure(
                    listOf(
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.Name,
                            AddressStructure.Field.LocalizationKey.Name,
                        ),
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.Organization,
                            AddressStructure.Field.LocalizationKey.Organization,
                        ),
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.StreetAddress,
                            AddressStructure.Field.LocalizationKey.StreetAddress,
                        ),
                        AddressStructure.Field.SelectField(
                            AddressStructure.Field.ID.AddressLevel1,
                            AddressStructure.Field.LocalizationKey.Province,
                            "",
                            listOf(AddressStructure.Field.SelectField.Option("AL", "Alberta")),
                        ),
                    )
                )

            val countries = mutableListOf<String>()
            val store =
                makeStore(
                    state =
                        AddressState.initial()
                            .copy(
                                address =
                                    emptyUpdatableAddress.copy(
                                        addressLevel1 = "WA",
                                        country = "US",
                                    )
                            ),
                    scope = this,
                ) {
                    copy(
                        getAddressStructure = { countryCode ->
                            countries.add(countryCode)
                            expectedAddressStructure
                        }
                    )
                }

            assertEquals("US", store.state.address.country)
            assertEquals("WA", store.state.address.addressLevel1)

            store.dispatch(ViewAppeared)
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(FormChange.Country("CA"))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals("CA", store.state.address.country)
            assertEquals("AL", store.state.address.addressLevel1)

            assertEquals(
                listOf("US", "CA"),
                countries,
            )
        }

    @Test
    fun `GIVEN a store WHEN an address structure is loaded with an Unknown LocalizationKey THEN submit an exception`() =
        runTest(testDispatcher) {
            val expectedAddressStructure =
                AddressStructure(
                    listOf(
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.Name,
                            AddressStructure.Field.LocalizationKey.Name,
                        ),
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.Organization,
                            AddressStructure.Field.LocalizationKey.Organization,
                        ),
                        AddressStructure.Field.TextField(
                            AddressStructure.Field.ID.StreetAddress,
                            AddressStructure.Field.LocalizationKey.Unknown("unknown-key"),
                        ),
                    )
                )

            var actualThrowable: Throwable? = null
            val store =
                makeStore(this) {
                    copy(
                        getAddressStructure = { _ -> expectedAddressStructure },
                        submitCaughtException = { actualThrowable = it },
                    )
                }

            store.dispatch(ViewAppeared)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                UnknownLocalizationKey("US", "unknown-key"),
                actualThrowable,
            )
        }

    @Test
    fun `GIVEN a store WHEN a user updates the address THEN the address is updated`() =
        runTest(testDispatcher) {
            val store =
                makeStore(this) {
                    copy(
                        getAddressStructure = { _ ->
                            AddressStructure(
                                listOf(
                                    AddressStructure.Field.TextField(
                                        AddressStructure.Field.ID.Name,
                                        AddressStructure.Field.LocalizationKey.Name,
                                    ),
                                    AddressStructure.Field.TextField(
                                        AddressStructure.Field.ID.Organization,
                                        AddressStructure.Field.LocalizationKey.Organization,
                                    ),
                                    AddressStructure.Field.TextField(
                                        AddressStructure.Field.ID.StreetAddress,
                                        AddressStructure.Field.LocalizationKey.Unknown("unknown-key"),
                                    ),
                                    AddressStructure.Field.SelectField(
                                        AddressStructure.Field.ID.AddressLevel1,
                                        AddressStructure.Field.LocalizationKey.State,
                                        defaultSelectionKey = "",
                                        options = listOf(AddressStructure.Field.SelectField.Option("AL", "Alabama")),
                                    ),
                                )
                            )
                        }
                    )
                }

            assertEquals(store.state.address, emptyUpdatableAddress)

            listOf(
                    FormChange.Name("Work"),
                    FormChange.StreetAddress("Mozilla Lane"),
                    FormChange.AddressLevel2("Level 2"),
                    FormChange.AddressLevel1("This Should Change"),
                    FormChange.PostalCode("31337"),
                    FormChange.Country("CA"),
                    FormChange.Tel("555-555-5555"),
                    FormChange.Email("mo@zilla.com"),
                )
                .forEach(store::dispatch)
            testDispatcher.scheduler.advanceUntilIdle()

            val expected =
                UpdatableAddressFields(
                    name = "Work",
                    organization = "",
                    streetAddress = "Mozilla Lane",
                    addressLevel1 = "AL",
                    addressLevel2 = "Level 2",
                    addressLevel3 = "",
                    postalCode = "31337",
                    country = "CA",
                    tel = "555-555-5555",
                    email = "mo@zilla.com",
                )
            assertEquals(expected, store.state.address)
        }

    @Test
    fun `GIVEN a state with no guid WHEN a user taps save THEN create and navigateBack is called on the environment`() =
        runTest(testDispatcher) {
            var navigateBackCalled = false
            var createdAddress: UpdatableAddressFields? = null

            val store =
                makeStore(this) {
                    copy(
                        navigateBack = { navigateBackCalled = true },
                        createAddress = {
                            createdAddress = it
                        },
                    )
                }

            store.dispatch(FormChange.Name("Work"))
            store.dispatch(SaveTapped)
            testDispatcher.scheduler.advanceUntilIdle()

            val expected = emptyUpdatableAddress.copy(name = "Work")

            assertTrue(navigateBackCalled)
            assertEquals(expected, createdAddress!!)
        }

    @Test
    fun `GIVEN a state with an existing address WHEN a user taps save THEN update and navigateBack is called on the environment`() =
        runTest(testDispatcher) {
            var navigateBackCalled = false
            var updatedAddress: Pair<String, UpdatableAddressFields>? = null

            val initialAddress = Address("BEEF", "Work", "Mozilla", "", "", "", "", "", "", "", "")

            val store =
                makeStore(this, AddressState.initial(address = initialAddress)) {
                    copy(
                        navigateBack = { navigateBackCalled = true },
                        updateAddress = { guid, address -> updatedAddress = Pair(guid, address) },
                    )
                }

            store.dispatch(FormChange.Name("Home"))
            store.dispatch(SaveTapped)
            testDispatcher.scheduler.advanceUntilIdle()

            val expected = Pair("BEEF", emptyUpdatableAddress.copy(name = "Home", organization = "Mozilla"))

            assertTrue(navigateBackCalled)
            assertEquals(expected, updatedAddress)
        }

    @Test
    fun `GIVEN a state with an existing address WHEN a user taps save THEN delete and navigateBack is called on the environment`() =
        runTest(testDispatcher) {
            var navigateBackCalled = false
            var deletedGuid: String? = null

            val initialAddress = Address("BEEF", "Work", "Mozilla", "", "", "", "", "", "", "", "")

            val store =
                makeStore(this, AddressState.initial(address = initialAddress)) {
                    copy(
                        navigateBack = { navigateBackCalled = true },
                        deleteAddress = { deletedGuid = it },
                    )
                }

            assertEquals(DialogState.Inert, store.state.deleteDialog)

            store.dispatch(DeleteTapped)
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(DialogState.Presenting, store.state.deleteDialog)

            store.dispatch(DeleteDialogAction.DeleteTapped)
            testDispatcher.scheduler.advanceUntilIdle()

            val expected = "BEEF"

            assertTrue(navigateBackCalled)
            assertEquals(expected, deletedGuid)
        }

    private fun makeStore(
        scope: CoroutineScope,
        state: AddressState = AddressState.initial(),
        transform: AddressEnvironment.() -> AddressEnvironment = { this },
    ): AddressStore {
        val environment = AddressEnvironment.empty.transform()

        return AddressStore(
            state,
            listOf(
                AddressMiddleware(environment, scope, testDispatcher, testDispatcher),
                AddressStructureMiddleware(environment, scope, testDispatcher),
            ),
        )
    }
}

private val emptyUpdatableAddress = UpdatableAddressFields("", "", "", "", "", "", "", "US", "", "")

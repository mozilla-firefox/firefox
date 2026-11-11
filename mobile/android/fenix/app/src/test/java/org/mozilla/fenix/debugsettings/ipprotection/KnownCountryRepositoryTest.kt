/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.ipprotection

import mozilla.components.feature.ipprotection.store.state.Country
import org.junit.Assert.assertEquals
import org.junit.Test

class KnownCountryRepositoryTest {

    private val austria = Country(countryCode = "AT", available = true)
    private val germany = Country(countryCode = "DE", available = false)

    @Test
    fun `WHEN countries are saved THEN they are all reported as visible options`() {
        val repository = KnownCountryRepository()
        val countries = listOf(austria, germany)

        repository.save(countries)

        assertEquals(
            listOf(
                CountryCustomizationOption(country = austria),
                CountryCustomizationOption(country = germany),
            ),
            repository.customizationOptions(countries),
        )
    }

    @Test
    fun `GIVEN a saved country WHEN it is missing from the current list THEN it is reported as hidden`() {
        val repository = KnownCountryRepository()
        repository.save(listOf(austria, germany))

        assertEquals(
            listOf(
                CountryCustomizationOption(country = austria),
                CountryCustomizationOption(country = germany, shouldHide = true),
            ),
            repository.customizationOptions(listOf(austria)),
        )
    }

    @Test
    fun `GIVEN a saved country WHEN a later save omits it THEN its availability is retained`() {
        val repository = KnownCountryRepository()
        repository.save(listOf(austria, germany))

        repository.save(listOf(austria.copy(available = false)))

        assertEquals(
            listOf(
                CountryCustomizationOption(country = austria.copy(available = false)),
                CountryCustomizationOption(country = germany, shouldHide = true),
            ),
            repository.customizationOptions(listOf(austria)),
        )
    }
}

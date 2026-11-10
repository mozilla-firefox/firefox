/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package org.mozilla.fenix.settings.ipprotection

import androidx.compose.material3.SnackbarHostState
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertHasNoClickAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.Locale
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.compose.base.theme.Theme
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.Location
import mozilla.components.feature.ipprotection.store.state.Recommended
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.theme.FirefoxTheme

private val LOCALE = Locale.US
private val JAPAN = Country(countryCode = "jp", available = true)
private val GERMANY = Country(countryCode = "de", available = true)
private val LOCATIONS = listOf(Recommended, GERMANY, JAPAN)

@RunWith(AndroidJUnit4::class)
class IPProtectionLocationsScreenTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN an available country WHEN the user taps it THEN it is selected`() {
        val selections = mutableListOf<Location>()

        setScreen(selectedLocation = JAPAN) { selections.add(it) }

        composeTestRule.onNodeWithText(GERMANY.displayName(LOCALE)).assertHasClickAction().performClick()

        assertEquals(listOf<Location>(GERMANY), selections)
    }

    @Test
    fun `GIVEN a country is unavailable THEN the location has no click action`() {
        val disabledLocation = Country(countryCode = "gb", available = false)
        val locations = LOCATIONS + disabledLocation
        setScreen(selectedLocation = JAPAN, locations = locations)

        composeTestRule.onNodeWithText(disabledLocation.displayName(LOCALE)).assertHasNoClickAction()
    }

    private fun setScreen(
        selectedLocation: Location,
        locations: List<Location> = LOCATIONS,
        onLocationSelected: (Location) -> Unit = {},
    ) {
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionLocationsScreen(
                    selectedLocation = selectedLocation,
                    locations = locations,
                    snackbarHostState = SnackbarHostState(),
                    onNavigateBack = {},
                    onLocationSelected = onLocationSelected,
                )
            }
        }
    }
}

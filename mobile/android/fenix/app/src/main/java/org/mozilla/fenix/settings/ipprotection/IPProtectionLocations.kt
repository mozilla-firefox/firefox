/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package org.mozilla.fenix.settings.ipprotection

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.Location
import mozilla.components.feature.ipprotection.store.state.Recommended
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.compose.MenuGroup
import org.mozilla.fenix.components.menu.compose.MenuTextItem
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme

/**
 * The IP Protection location selection screen.
 *
 * @param selectedLocation The currently selected location.
 * @param locations A list of available locations for user to choose from.
 * @param snackbarHostState The [SnackbarHostState] used to display snackbars.
 * @param onNavigateBack Called when the back navigation icon is tapped.
 * @param onLocationSelected Called with the user taps on a location.
 */
@Composable
fun IPProtectionLocationsScreen(
    selectedLocation: Location,
    locations: List<Location>,
    snackbarHostState: SnackbarHostState,
    onNavigateBack: () -> Unit,
    onLocationSelected: (Location) -> Unit,
) {
    val screenTitle = stringResource(R.string.ip_protection_locations_title)

    Scaffold(
        modifier = Modifier.semantics { paneTitle = screenTitle },
        topBar = {
            IPProtectionLocationsTopAppBar(onNavigateBack = onNavigateBack)
        },
        snackbarHost = {
            SnackbarHost(hostState = snackbarHostState)
        },
    ) { paddingValues ->
        Surface(
            modifier = Modifier.fillMaxSize().padding(paddingValues),
            color = MaterialTheme.colorScheme.surface,
        ) {
            LocationList(
                selectedLocation = selectedLocation,
                locations = locations,
                onLocationSelected = onLocationSelected,
            )
        }
    }
}

@Composable
private fun LocationList(
    selectedLocation: Location,
    locations: List<Location>,
    onLocationSelected: (Location) -> Unit,
) {
    val recommended = locations.filterIsInstance<Recommended>().firstOrNull()
    val countries = locations.filterIsInstance<Country>()

    Column(
        modifier =
            Modifier.verticalScroll(rememberScrollState())
                .padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = FirefoxTheme.layout.space.static150,
                ),
        verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
    ) {
        if (recommended != null) {
            MenuGroup {
                LocationOption(
                    label = stringResource(R.string.ip_protection_location_recommended_label),
                    description = stringResource(R.string.ip_protection_location_fastest_description),
                    isSelected = selectedLocation == recommended,
                    onClick = { onLocationSelected(recommended) },
                )
            }
        }

        if (countries.isNotEmpty()) {
            MenuGroup {
                countries.forEach { country ->
                    LocationOption(
                        label = country.displayName,
                        isSelected = country == selectedLocation,
                        description =
                            stringResource(R.string.ip_protection_location_unavailable_description).takeIf {
                                !country.available
                            },
                        enabled = country.available,
                        onClick = { onLocationSelected(country) },
                    )
                }
            }
        } else {
            LocationsEmptyState()
        }
    }
}

@Composable
private fun LocationsEmptyState() {
    MenuGroup {
        Column(
            modifier =
                Modifier.fillMaxWidth()
                    .background(color = MaterialTheme.colorScheme.surfaceBright)
                    .padding(
                        paddingValues =
                            PaddingValues(
                                horizontal = FirefoxTheme.layout.space.dynamic200,
                                vertical = FirefoxTheme.layout.space.static150,
                            )
                    ),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Image(
                painter = painterResource(R.drawable.kit_search_error),
                contentDescription = null, // Decorative only
            )

            Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

            Column(
                verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static50),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(
                    text = stringResource(R.string.ip_protection_locations_unavailable_title),
                    color = MaterialTheme.colorScheme.onSurface,
                    style = FirefoxTheme.typography.headline7,
                )

                Text(
                    text = stringResource(R.string.ip_protection_locations_unavailable_description),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = FirefoxTheme.typography.caption,
                )
            }
        }
    }
}

@Composable
private fun LocationOption(
    label: String,
    isSelected: Boolean,
    description: String? = null,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    MenuTextItem(
        label = label,
        modifier =
            Modifier.semantics(mergeDescendants = true) {
                selected = isSelected
                role = Role.RadioButton
            },
        description = description,
        enabled = enabled,
        iconPainter =
            if (isSelected) {
                painterResource(iconsR.drawable.mozac_ic_checkmark_24)
            } else {
                null
            },
        onClick = onClick,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun IPProtectionLocationsTopAppBar(onNavigateBack: () -> Unit) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.ip_protection_locations_title),
                style = FirefoxTheme.typography.headline5,
                modifier = Modifier.semantics { heading() },
            )
        },
        navigationIcon = {
            IconButton(
                onClick = onNavigateBack,
                contentDescription =
                    stringResource(R.string.ip_protection_locations_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        windowInsets = WindowInsets(),
    )
}

@FlexibleWindowPreview
@Composable
private fun IPProtectionLocationsRecommendedPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionLocationsScreen(
            selectedLocation = SAMPLE_LOCATIONS.first(),
            locations = SAMPLE_LOCATIONS,
            snackbarHostState = SnackbarHostState(),
            onNavigateBack = {},
            onLocationSelected = {},
        )
    }
}

@FlexibleWindowPreview
@Composable
private fun IPProtectionLocationsCountrySelectedPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionLocationsScreen(
            selectedLocation = SAMPLE_LOCATIONS[1],
            locations = SAMPLE_LOCATIONS,
            snackbarHostState = SnackbarHostState(),
            onNavigateBack = {},
            onLocationSelected = {},
        )
    }
}

@FlexibleWindowPreview
@Composable
private fun IPProtectionLocationsEmptyPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionLocationsScreen(
            selectedLocation = SAMPLE_LOCATIONS.first(),
            locations = listOf(SAMPLE_LOCATIONS.first()),
            snackbarHostState = SnackbarHostState(),
            onNavigateBack = {},
            onLocationSelected = {},
        )
    }
}

private val SAMPLE_LOCATIONS =
    listOf(
        Recommended,
        Country(countryCode = "dk", available = true),
        Country(countryCode = "fr", available = true),
        Country(countryCode = "gb", available = false),
        Country(countryCode = "us", available = true),
    )

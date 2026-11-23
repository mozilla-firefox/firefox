/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.info.ui

import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import org.mozilla.fenix.compose.rememberModalSheetState
import org.mozilla.fenix.debugsettings.info.DebugInfoSection
import org.mozilla.fenix.debugsettings.info.toJson

private const val DEBUG_INFO_ROUTE = "debug_info"
private const val DEBUG_INFO_JSON_ROUTE = "debug_info_json"

/**
 * The debug info bottom sheet.
 *
 * @param sections The list of [DebugInfoSection]s to display.
 * @param onDismissRequest Invoked when the user dismisses the sheet.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DebugInfoBottomSheet(
    sections: List<DebugInfoSection>,
    onDismissRequest: () -> Unit,
) {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        sheetState = sheetState,
        containerColor = MaterialTheme.colorScheme.surface,
    ) {
        val navController = rememberNavController()

        NavHost(
            navController = navController,
            startDestination = DEBUG_INFO_ROUTE,
        ) {
            composable(route = DEBUG_INFO_ROUTE) {
                DebugInfoContent(
                    sections = sections,
                    onViewJsonClick = { navController.navigate(DEBUG_INFO_JSON_ROUTE) },
                )
            }

            composable(route = DEBUG_INFO_JSON_ROUTE) {
                val json = remember(sections) { sections.toJson() }

                DebugInfoJsonReport(
                    json = json,
                    onBackClick = { navController.popBackStack() },
                )
            }
        }
    }
}

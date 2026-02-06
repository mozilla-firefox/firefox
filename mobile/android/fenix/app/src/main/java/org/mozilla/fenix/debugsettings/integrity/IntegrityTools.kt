/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.integrity

import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken

/**
 * Debug drawer view to test an [IntegrityClient].
 *
 * @param integrityClient used to request a token.
 */
@Composable
fun IntegrityTools(
    integrityClient: IntegrityClient,
) {
    var token by remember { mutableStateOf<IntegrityToken?>(null) }

    LaunchedEffect(Unit) {
        token = integrityClient.request().getOrNull()
    }

    SelectionContainer {
        Text(token?.value ?: "Not present")
    }
}

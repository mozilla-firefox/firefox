package org.mozilla.fenix.debugsettings.integrity

import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken

@Composable
fun IntegrityTools(
    integrityClient: IntegrityClient,
) {
    var token by remember { mutableStateOf<IntegrityToken?>(null) }

    LaunchedEffect(Unit) {
        token = integrityClient.request().getOrNull()
    }

    Text(token?.value ?: "Not present")
}

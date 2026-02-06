/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.integrity.googleplay.ext

import com.google.android.play.core.integrity.StandardIntegrityManager
import com.google.android.play.core.integrity.StandardIntegrityManager.PrepareIntegrityTokenRequest
import com.google.android.play.core.integrity.StandardIntegrityManager.StandardIntegrityTokenProvider
import com.google.android.play.core.integrity.StandardIntegrityManager.StandardIntegrityTokenRequest
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.lib.integrity.googleplay.GoogleProjectNumber
import mozilla.components.lib.integrity.googleplay.RequestHashProvider
import mozilla.components.lib.integrity.googleplay.TokenProvider
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine

private val StandardIntegrityTokenProvider.tokenProvider
    get() = TokenProvider { requestHashProvider ->
        this.request(requestHashProvider)
    }

internal suspend fun StandardIntegrityTokenProvider.request(requestHashProvider: RequestHashProvider) =
    suspendCoroutine { continuation ->
        val tokenRequest = StandardIntegrityTokenRequest.builder()
            .setRequestHash(requestHashProvider.generateHash())
            .build()

        request(tokenRequest)
            .addOnSuccessListener { continuation.resume(Result.success(IntegrityToken(it.token()))) }
            .addOnFailureListener { continuation.resume(Result.failure(it)) }
    }

internal suspend fun StandardIntegrityManager.prepare(cloudProjectNumber: GoogleProjectNumber.Valid) =
    suspendCoroutine { continuation ->
        val tokenRequest = PrepareIntegrityTokenRequest.builder()
            .setCloudProjectNumber(cloudProjectNumber.value)
            .build()
        prepareIntegrityToken(tokenRequest)
            .addOnSuccessListener { continuation.resume(Result.success(it.tokenProvider)) }
            .addOnFailureListener { continuation.resume(Result.failure(it)) }
    }

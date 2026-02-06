/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.integrity.googleplay

import android.content.Context
import com.google.android.play.core.integrity.IntegrityManagerFactory
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.lib.integrity.googleplay.ext.prepare
import java.util.UUID
import kotlin.uuid.Uuid

@JvmInline
value class CloudProjectNumber(val value: Long) {
    companion object {
        val test = CloudProjectNumber(31337L)
    }
}

fun interface TokenProvider {
    suspend fun request(requestHashProvider: RequestHashProvider): Result<IntegrityToken>
}

fun interface TokenProviderFactory {
    suspend fun create(): Result<TokenProvider>

    companion object {
        fun create(context: Context, cloudProjectNumber: CloudProjectNumber): TokenProviderFactory {
            return GooglePlayTokenProviderFactory(context, cloudProjectNumber)
        }
    }
}

fun interface RequestHashProvider {
    fun generateHash(): String

    companion object {
        fun randomHashProvider() = RequestHashProvider {
            UUID.randomUUID().toString()
        }
    }
}

private class GooglePlayTokenProviderFactory(
    context: Context,
    private val cloudProjectNumber: CloudProjectNumber,
) : TokenProviderFactory {
    private val integrityManager = IntegrityManagerFactory.createStandard(context)
    override suspend fun create(): Result<TokenProvider> = integrityManager.prepare(cloudProjectNumber)
}

class GooglePlayIntegrityClient(
    private val tokenProviderFactory: TokenProviderFactory,
    private val requestHashProvider: RequestHashProvider,
) : IntegrityClient {
    var tokenProvider: TokenProvider? = null

    suspend fun warmup() {
        if (tokenProvider == null) {
            tokenProvider = tokenProviderFactory.create().getOrNull()
        }
    }

    override suspend fun request() = tokenProvider?.request(requestHashProvider)
        ?: Result.failure(MissingTokenProvider())
}

class MissingTokenProvider : IllegalStateException("GooglePlayIntegrityClient is missing a token provider")

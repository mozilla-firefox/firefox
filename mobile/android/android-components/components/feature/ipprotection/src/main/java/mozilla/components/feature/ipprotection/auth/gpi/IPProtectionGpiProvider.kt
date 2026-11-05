/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.auth.gpi

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.feature.ipprotection.auth.IPProtectionAuthProvider
import mozilla.components.lib.integrity.googleplay.GooglePlayIntegrityClient
import mozilla.components.lib.integrity.googleplay.IntegrityConsumer
import mozilla.components.support.base.log.logger.Logger

/**
 * [IPProtectionAuthProvider] that wires Google Play Integrity (GPI) warm-up and token retrieval into the IP protection
 * handler.
 *
 * N.B: This is intentionally kept separate from [mozilla.components.feature.ipprotection.IPProtectionFeature] so GPI
 * support can be composed in without affecting the default behavior.
 *
 * @param integrityClient the [GooglePlayIntegrityClient] to supply GPI tokens to the proxy service.
 */
class IPProtectionGpiProvider(private val integrityClient: GooglePlayIntegrityClient) : IPProtectionAuthProvider {
    private val logger = Logger("IPP:Gpi")

    override fun configure(handler: IPProtectionHandler, scope: CoroutineScope) {
        val ipProtectionIntegrityClient = integrityClient.forConsumer(IntegrityConsumer.IpProtection)
        handler.setGpiProvider(
            object : IPProtectionHandler.GpiProvider {
                override fun warmUp(onComplete: (Boolean) -> Unit) {
                    scope.launch {
                        logger.info("Warm up GPI")
                        runCatching { integrityClient.warmUp() }
                            .onSuccess { warmedUp ->
                                logger.info("Warm up successful: $warmedUp")
                                onComplete(warmedUp)
                            }
                            .onFailure {
                                logger.error("Warm up failed", it)
                                onComplete(false)
                                if (it is CancellationException) throw it
                            }
                    }
                }

                override fun getToken(onComplete: (String?) -> Unit) {
                    logger.info("Get token from GPI")
                    scope.launch {
                        val token =
                            ipProtectionIntegrityClient
                                .request()
                                .onSuccess { logger.info("Token request succeeded") }
                                .onFailure { logger.error("GPI token request failed", it) }
                                .getOrNull()
                                ?.value
                        onComplete(token)
                    }
                }
            }
        )
    }
}

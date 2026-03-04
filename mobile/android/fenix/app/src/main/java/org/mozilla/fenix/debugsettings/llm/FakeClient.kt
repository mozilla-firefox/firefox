/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.debugsettings.llm

import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.lib.llm.mlpa.UserIdProvider
import mozilla.components.lib.llm.mlpa.service.UserId
import org.mozilla.fenix.components.ClientUUID

/**
 * A Fake [Client] to be used in the debug drawer preview.
 */
class FakeClient : Client() {
    override fun fetch(request: Request): Response {
        return Response(
            url = "noop",
            status = Response.SUCCESS,
            headers = MutableHeaders(),
            body = Response.Body.empty(),
        )
    }
}

/**
 * A Fake [IntegrityClient] to be used in the debug drawer preview.
 */
class FakeIntegrityClient : IntegrityClient {
    override suspend fun request(): Result<IntegrityToken> {
        return Result.success(IntegrityToken("my-preview-integrity-token"))
    }
}

/**
 * A Fake [IntegrityClient] to be used in the debug drawer preview.
 */
class FakeClientUUID : ClientUUID {
    override fun getUserId() = UserId("fake-userid")
    override fun generateHash() = "generated-hash"
}

/**
 * A Fake [UserIdProvider] to be used in the debug drawer preview.
 */
class FakeUserIdProvider : UserIdProvider {
    override fun getUserId() = UserId("preview-user-id")
}

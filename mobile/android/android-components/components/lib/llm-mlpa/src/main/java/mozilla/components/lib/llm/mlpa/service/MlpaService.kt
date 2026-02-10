/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.service

import kotlinx.serialization.KSerializer
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.descriptors.PrimitiveKind
import kotlinx.serialization.descriptors.PrimitiveSerialDescriptor
import kotlinx.serialization.descriptors.SerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder
import mozilla.components.concept.integrity.IntegrityToken

/**
 * Thrown when the MLPA verification service fails to process or validate a request.
 *
 * @param reason A human-readable explanation of the failure.
 */
class VerificationServiceFailed(reason: String) : Exception("Verification Service Failed: $reason")

/**
 * Thrown when the MLPA chat/completion service fails to process a request.
 *
 * @param reason A human-readable explanation of the failure.
 */
class ChatServiceFailed(reason: String) : Exception("Verification Service Failed: $reason")

/**
 * Configuration for connecting to MLPA services.
 *
 * @property baseUrl The base URL used for all MLPA API calls.
 */
data class MlpaConfig(
    val baseUrl: String,
) {
    companion object {
        /**
         * Preconfigured MLPA configuration targeting the live (non-prod stage) environment.
         */
        val live
            get() = MlpaConfig(
                baseUrl = "https://mlpa-nonprod-stage-mozilla.global.ssl.fastly.net/v1",
            )
    }
}

/**
 * Represents an MLPA authorization token used to authenticate API calls.
 *
 * @property value The raw authorization token string.
 */
@JvmInline
@Serializable
value class AuthorizationToken(val value: String)

/**
 * Represents a unique identifier for a user in MLPA requests.
 *
 * @property value The raw user identifier.
 */
@JvmInline
@Serializable
value class UserId(val value: String)

/**
 * Aggregated MLPA service interface combining:
 * - [AuthenticationService] for token verification.
 * - [ChatService] for chat/completion requests.
 */
interface MlpaService : AuthenticationService, ChatService

/**
 * Service responsible for verifying integrity tokens and issuing access tokens.
 */
fun interface AuthenticationService {
    /**
     * Verifies an integrity token and exchanges it for an access token.
     *
     * @param request The verification request payload.
     * @return A [Result] containing a [Response] on success, or a failure otherwise.
     */
    suspend fun verify(request: Request): Result<Response>

    /**
     * Request payload for token verification.
     *
     * @property userId The identifier of the user requesting verification.
     * @property integrityToken The integrity token obtained from the client.
     */
    @Serializable
    data class Request(
        @SerialName("user_id") val userId: UserId,
        @Serializable(with = IntegrityTokenSerializer::class) val integrityToken: IntegrityToken,
    )

    /**
     * Response payload returned after successful verification.
     *
     * @property accessToken The issued authorization token.
     * @property tokenType The type of token (e.g., "Bearer").
     * @property expiresIn Expiration time in seconds.
     */
    @Serializable
    data class Response(
        @SerialName("access_token") val accessToken: AuthorizationToken,
        @SerialName("token_type") val tokenType: String,
        @SerialName("expires_in") val expiresIn: Int,
    )
}

/**
 * Service responsible for requesting chat/completion responses from MLPA.
 */
fun interface ChatService {
    /**
     * Requests a model completion.
     *
     * @param authorizationToken A valid [AuthorizationToken] used to authorize the request.
     * @param request The completion request payload.
     * @return A [Result] containing a [Response] on success, or a failure otherwise.
     */
    suspend fun completion(
        authorizationToken: AuthorizationToken,
        request: Request,
    ): Result<Response>

    /**
     * Response returned from a completion request.
     *
     * @property choices A list of model-generated choices.
     */
    @Serializable
    data class Response(
        val choices: List<Choice>,
    ) {
        /**
         * A single completion choice returned by the model.
         *
         * @property message The generated message.
         */
        @Serializable
        data class Choice(
            val message: Message,
        )

        /**
         * A generated message from the model.
         *
         * @property content The textual content of the message.
         */
        @Serializable
        data class Message(
            val content: String,
        )
    }

    /**
     * Request payload for a chat/completion call.
     *
     * @property model The identifier of the model to use.
     * @property messages The conversation history provided to the model.
     */
    @Serializable
    data class Request(
        val model: ModelID,
        val messages: List<Message>,
    ) {
        /**
         * Identifier of a model supported by MLPA.
         *
         * @property value The raw model identifier string.
         */
        @JvmInline
        @Serializable
        value class ModelID(val value: String) {
            companion object {
                /**
                 * Predefined model identifier for the Mistral Small model hosted via Vertex AI.
                 */
                val mistral: ModelID
                    get() = ModelID("vertex_ai/mistral-small-2503")
            }
        }

        /**
         * Represents a single message in the conversation.
         *
         * @property role The role of the message sender.
         * @property content The textual content of the message.
         */
        @Serializable
        data class Message(val role: Role, val content: String) {
            /**
             * Supported message roles.
             */
            @Serializable
            enum class Role {
                /**
                 * A message originating from the end user.
                 */
                @SerialName("user")
                User,
            }

            companion object {
                /**
                 * Convenience factory for creating a user message.
                 *
                 * @param content The message content.
                 */
                fun user(content: String) = Message(Role.User, content)
            }
        }
    }
}

private object IntegrityTokenSerializer : KSerializer<IntegrityToken> {
    override val descriptor: SerialDescriptor =
        PrimitiveSerialDescriptor("integrity_token", PrimitiveKind.STRING)

    override fun serialize(encoder: Encoder, value: IntegrityToken) {
        encoder.encodeString(value.value) // or however you access the string
    }

    override fun deserialize(decoder: Decoder): IntegrityToken {
        return IntegrityToken(decoder.decodeString())
    }
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.service

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.collect
import kotlinx.serialization.EncodeDefault
import kotlinx.serialization.KSerializer
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.descriptors.PrimitiveKind
import kotlinx.serialization.descriptors.PrimitiveSerialDescriptor
import kotlinx.serialization.descriptors.SerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder
import kotlinx.serialization.json.JsonElement
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.concept.llm.ErrorCode
import mozilla.components.concept.llm.Llm

private val INTEGRITY_HANDSHAKE_FAILURE = ErrorCode(1002)
private val VERIFICATION_SERVICE_FAILED = ErrorCode(1003)
private val INVALID_TOKEN = ErrorCode(1004)
private val USER_BLOCKED = ErrorCode(1005)
private val REQUEST_TOO_LARGE = ErrorCode(1006)
private val BUDGET_EXCEEDED = ErrorCode(1007)
private val RATE_LIMITED = ErrorCode(1008)
private val UPSTREAM_ERROR = ErrorCode(1009)
private val SERVER_ERROR = ErrorCode(1010)
private val NETWORK_ERROR = ErrorCode(1011)
private val RESPONSE_PARSE_ERROR = ErrorCode(1012)
private val RATE_LIMIT_RESPONSE_PARSE_ERROR = ErrorCode(1013)
private val UPSTREAM_RESPONSE_PARSE_ERROR = ErrorCode(1014)
private val STREAM_CONTENT_PARSE_ERROR = ErrorCode(1015)
private val STREAM_EVENT_PARSE_ERROR = ErrorCode(1016)

/**
 * Thrown when the Integrity client experiences a failure, propagating its error message.
 */
class IntegrityHandshakeFailure(message: String) : Llm.Exception(message, INTEGRITY_HANDSHAKE_FAILURE)

/**
 * Thrown when the MLPA verification service fails to process or validate a request.
 *
 * @param reason A human-readable explanation of the failure.
 */
class VerificationServiceFailed(reason: String) :
    Llm.Exception("Verification Service Failed: $reason", VERIFICATION_SERVICE_FAILED)

/**
 * Sealed class for describing the type of error a [ChatService] can return.
 */
sealed class ChatServiceError(message: String, errorCode: ErrorCode) : Llm.Exception(message, errorCode) {
    /** Token expired or invalid. Re-authenticate via [AuthenticationService.verify]. */
    class InvalidToken : ChatServiceError("Invalid token", INVALID_TOKEN)

    /** The user has been blocked from accessing the service. */
    class UserBlocked : ChatServiceError("User blocked", USER_BLOCKED)

    /** The request body exceeded the 10MB limit. */
    class RequestTooLarge : ChatServiceError("Request too large", REQUEST_TOO_LARGE)

    /**
     * The user's total budget has been exhausted.
     *
     * @property retryAfter Duration in seconds before the budget resets (typically 86400s).
     */
    data class BudgetExceeded(val retryAfter: Long?) : ChatServiceError("Budget exceeded", BUDGET_EXCEEDED)

    /**
     * Requests per minute or tokens per minute limit reached.
     *
     * @property retryAfter Duration in seconds before the limit resets (typically 60s).
     */
    data class RateLimited(val retryAfter: Long?) : ChatServiceError("Rate limited", RATE_LIMITED)

    /** The upstream LLM was unreachable or returned an error (502). */
    data class UpstreamError(val reason: String) : ChatServiceError("Upstream error: $reason", UPSTREAM_ERROR)

    /**
     * An unexpected server-side error occurred.
     *
     * @property statusCode The HTTP status code returned.
     */
    data class ServerError(val statusCode: Int) : ChatServiceError("Server error: $statusCode", SERVER_ERROR)

    /**
     * A network error occurred while communicating with the service.
     *
     * @param cause The underlying network exception.
     */
    class NetworkError(cause: Exception) : ChatServiceError("Network error: ${cause.message}", NETWORK_ERROR)

    /**
     * The server response could not be parsed.
     *
     * @param cause The underlying serialization exception.
     */
    class ResponseParseError(cause: Exception) :
        ChatServiceError("Response parse error: ${cause.message}", RESPONSE_PARSE_ERROR)

    /**
     * The rate-limit error response body (HTTP 429) could not be parsed.
     *
     * @param cause The underlying serialization exception.
     */
    class RateLimitResponseParseError(cause: Exception) :
        ChatServiceError("Rate limit response parse error: ${cause.message}", RATE_LIMIT_RESPONSE_PARSE_ERROR)

    /**
     * The upstream error response body (HTTP 502) could not be parsed.
     *
     * @param cause The underlying serialization exception.
     */
    class UpstreamResponseParseError(cause: Exception) :
        ChatServiceError("Upstream response parse error: ${cause.message}", UPSTREAM_RESPONSE_PARSE_ERROR)

    /**
     * A streamed response could not be parsed.
     *
     * @param cause The underlying serialization exception.
     */
    class StreamEventParseError(cause: Exception) :
        ChatServiceError("Stream event parse error: ${cause.message}", STREAM_CONTENT_PARSE_ERROR)

    /**
     * A streamed response event included an error message.
     *
     * @param cause The underlying serialization exception.
     */
    class StreamError(cause: Exception) :
        ChatServiceError("Stream event error: ${cause.message}", STREAM_EVENT_PARSE_ERROR)
}

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
        val nonProd
            get() = MlpaConfig(
                baseUrl = "https://mlpa-nonprod-dev-mozilla.global.ssl.fastly.net",
            )

        /**
         * Preconfigured MLPA configuration targeting the live (prod-prod) environment.
         */
        val prodProd
            get() = MlpaConfig(
                baseUrl = "https://mlpa-prod-prod-mozilla.global.ssl.fastly.net",
            )
    }
}

/**
 * Represents a bearer token used to authenticate API calls.
 *
 * @property value The raw authorization token string.
 */
sealed interface AuthorizationToken {
    val value: String

    /**
     * An integrity-based authorization token issued by the MLPA verification service.
     *
     * @property value The raw token string.
     */
    @JvmInline
    @Serializable
    value class Integrity(override val value: String) : AuthorizationToken

    /**
     * A Firefox Accounts (FxA) authorization token.
     *
     * @property value The raw token string.
     */
    @JvmInline
    @Serializable
    value class Fxa(override val value: String) : AuthorizationToken
}

/**
 * Represents a unique identifier for a user in MLPA requests.
 *
 * @property value The raw user identifier.
 */
@JvmInline
@Serializable
value class UserId(val value: String)

/**
 * Represents the name of a package in MLPA requests.
 *
 * @property value The raw package name.
 */
@JvmInline
@Serializable
value class PackageName(val value: String)

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
     * @property packageName The package name for the app requesting verification.
     */
    @Serializable
    data class Request(
        @SerialName("user_id") val userId: UserId,
        @SerialName("integrity_token")
        @Serializable(with = IntegrityTokenSerializer::class)
        val integrityToken: IntegrityToken,
        @SerialName("package_name") val packageName: PackageName,
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
        @SerialName("access_token") val accessToken: AuthorizationToken.Integrity,
        @SerialName("token_type") val tokenType: String,
        @SerialName("expires_in") val expiresIn: Int,
    )
}

/**
 * Service responsible for requesting chat/completion responses from MLPA.
 */
interface ChatService {
    /**
     * Requests a streaming model completion.
     *
     * @param authorizationToken A valid [AuthorizationToken] used to authorize the request.
     * @param request The completion request payload.
     * @return A [Flow] of text tokens.
     */
    fun completion(
        authorizationToken: AuthorizationToken,
        request: Request,
    ): Flow<String>

    /**
     * Requests a model completion that may return tool calls.
     *
     * The default implementation collects the streaming [completion] as plain text.
     * Implementations that support native tool calling should override this method.
     *
     * @param authorizationToken A valid [AuthorizationToken] used to authorize the request.
     * @param request The completion request payload.
     * @return A [ToolAwareResponse] that is either text or a set of tool calls.
     */
    suspend fun completionWithTools(
        authorizationToken: AuthorizationToken,
        request: Request,
    ): ToolAwareResponse {
        val builder = StringBuilder()
        completion(authorizationToken, request).collect { builder.append(it) }
        return ToolAwareResponse.Text(builder.toString())
    }

    /**
     * A parsed tool call returned by the model.
     *
     * @property id Unique identifier for correlating this call with its result.
     * @property toolName The name of the tool to invoke.
     * @property arguments A JSON string of the model-supplied arguments.
     */
    data class ToolCallRecord(val id: String, val toolName: String, val arguments: String)

    /**
     * The result of a tool-aware completion request.
     */
    sealed class ToolAwareResponse {
        /** The model produced a plain-text response. */
        data class Text(val content: String) : ToolAwareResponse()

        /** The model requested one or more tool calls. */
        data class ToolCalls(val calls: List<ToolCallRecord>) : ToolAwareResponse()
    }

    /**
     * Body of an error response with a code.
     *
     * @property error the error number the [ChatService] returned.
     */
    @Serializable
    data class ResponseErrorCode(val error: Int)

    /**
     * Body of an error response with a reason.
     *
     * @property error the error reason the [ChatService] returned.
     */
    @Serializable
    data class ResponseErrorReason(val error: String)

    /**
     * Response returned from a non-streaming completion request.
     *
     * @property choices A list of model-generated choices.
     */
    @Serializable
    data class Response(
        val choices: List<Choice>,
    ) {
        /**
         * A single completion choice returned by the model.
         */
        @Serializable
        data class Choice(
            val message: Message,
            @SerialName("finish_reason") val finishReason: String? = null,
        )

        /**
         * A generated message from the model, which may be text or tool calls.
         *
         * @property content The textual content; null when the model returned tool calls instead.
         * @property toolCalls Tool calls requested by the model; null for plain-text responses.
         */
        @Serializable
        data class Message(
            val content: String? = null,
            @SerialName("tool_calls") val toolCalls: List<ToolCall>? = null,
        ) {
            @Serializable
            data class ToolCall(
                val id: String,
                val type: String = "function",
                val function: Function,
            ) {
                @Serializable
                data class Function(val name: String, val arguments: String)
            }
        }
    }

    /**
     * Request payload for a chat/completion call.
     *
     * @property model The identifier of the model to use.
     * @property messages The conversation history provided to the model.
     * @property tools Tool definitions available to the model; omitted when empty.
     * @property toolChoice How the model should select tools; omitted when no tools are provided.
     */
    @Serializable
    data class Request(
        val model: ModelID,
        val messages: List<Message>,
        val stream: Boolean = true,
        val temperature: Float = 0.1f,
        @SerialName("top_p") val topP: Float = 0.01f,
        @EncodeDefault(EncodeDefault.Mode.NEVER)
        val tools: List<Tool>? = null,
        @EncodeDefault(EncodeDefault.Mode.NEVER)
        @SerialName("tool_choice")
        val toolChoice: String? = null,
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
                val mozSummarization: ModelID
                    get() = ModelID("moz-summarization")
            }
        }

        /**
         * A tool definition passed to the model in a request.
         *
         * @property function The function description the model can invoke.
         */
        @Serializable
        data class Tool(
            val type: String = "function",
            val function: Function,
        ) {
            /**
             * @property name Unique name the model uses to identify this tool.
             * @property description Human-readable description of the tool's purpose.
             * @property parameters JSON Schema describing the tool's input parameters.
             */
            @Serializable
            data class Function(
                val name: String,
                val description: String,
                val parameters: JsonElement,
            )
        }

        /**
         * Represents a single message in the conversation.
         *
         * @property role The role of the message sender.
         * @property content The textual content; null for assistant tool-call messages.
         * @property toolCalls Tool calls requested by the model; present only on assistant turns.
         * @property toolCallId ID linking a tool-result message to its originating call.
         */
        @Serializable
        data class Message(
            val role: Role,
            @EncodeDefault(EncodeDefault.Mode.NEVER)
            val content: String? = null,
            @EncodeDefault(EncodeDefault.Mode.NEVER)
            @SerialName("tool_calls")
            val toolCalls: List<ToolCall>? = null,
            @EncodeDefault(EncodeDefault.Mode.NEVER)
            @SerialName("tool_call_id")
            val toolCallId: String? = null,
        ) {
            /**
             * Supported message roles.
             */
            @Serializable
            enum class Role {
                @SerialName("user") User,
                @SerialName("system") System,
                @SerialName("assistant") Assistant,
                @SerialName("tool") Tool,
            }

            @Serializable
            data class ToolCall(
                val id: String,
                val type: String = "function",
                val function: Function,
            ) {
                @Serializable
                data class Function(val name: String, val arguments: String)
            }

            companion object {
                fun user(content: String) = Message(Role.User, content)
                fun system(content: String) = Message(Role.System, content)
                fun assistant(content: String) = Message(Role.Assistant, content)
                fun assistantToolCall(calls: List<ToolCall>) = Message(Role.Assistant, toolCalls = calls)
                fun tool(toolCallId: String, content: String) =
                    Message(Role.Tool, content = content, toolCallId = toolCallId)
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

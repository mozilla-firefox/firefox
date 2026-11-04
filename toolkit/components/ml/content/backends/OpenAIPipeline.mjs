/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-nocheck - TODO - Remove this to type check this file.

/**
 * @typedef {import("../../content/Utils.sys.mjs").ProgressAndStatusCallbackParams} ProgressAndStatusCallbackParams
 */

/* eslint-disable-next-line mozilla/reject-import-system-module-from-non-system */
import { Progress } from "chrome://global/content/ml/Utils.sys.mjs";

/* eslint-disable-next-line mozilla/reject-import-system-module-from-non-system */
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

/**
 * Log level set by the pipeline.
 *
 * @type {string}
 */
let _logLevel = "Error";

/**
 * Lazy initialization container.
 *
 * @type {object}
 */
const lazy = {};

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    maxLogLevel: _logLevel, // we can't use maxLogLevelPref in workers.
    prefix: "GeckoMLOpenAIPipeline",
  });
});
ChromeUtils.defineESModuleGetters(
  lazy,
  {
    setLogLevel: "chrome://global/content/ml/Utils.sys.mjs",
  },
  { global: "current" }
);

/**
 * How long to keep reading after finish_reason for the trailing usage chunk.
 * Only reached when a server stops talking without closing the stream, and must
 * stay under browser.ml.modelCacheTimeout or the engine is reaped mid-drain.
 *
 * @type {number}
 */
const TOOL_CALL_DRAIN_TIMEOUT_MS = 2000;

export class OpenAIPipeline {
  #errorFactory = null;
  #options = null;
  #abortControllers = new Map();
  OpenAILib = null;

  constructor(options, errorFactory) {
    this.#errorFactory = errorFactory;
    this.#options = options;
  }

  static async initialize(mlEngineWorker, wasm, options = {}, errorFactory) {
    let initStart = ChromeUtils.now();
    lazy.console.debug("Initializing OpenAI pipeline");
    let importStart = ChromeUtils.now();
    if (AppConstants.NIGHTLY_BUILD) {
      OpenAIPipeline.OpenAILib =
        await import("chrome://global/content/ml/openai-dev.mjs");
    } else {
      OpenAIPipeline.OpenAILib =
        await import("chrome://global/content/ml/openai.mjs");
    }
    ChromeUtils.addProfilerMarker(
      "MLEngine:OpenAI",
      { startTime: importStart },
      `Library loaded`
    );
    if (options.logLevel) {
      _logLevel = options.logLevel;
      lazy.setLogLevel(options.logLevel); // setting Utils log level
    }
    let config = {};
    options.applyToConfig(config);
    config.backend = config.backend || "openai";

    // reapply logLevel if it has changed.
    if (lazy.console.logLevel != config.logLevel) {
      lazy.console.logLevel = config.logLevel;
    }

    ChromeUtils.addProfilerMarker(
      "MLEngine:OpenAI",
      { startTime: initStart },
      `Initialized`
    );

    return new OpenAIPipeline(config, errorFactory);
  }

  /**
   * Sends progress updates to both the port and inference progress callback.
   *
   * @private
   * @param {object} args - The arguments object
   * @param {string} args.content - The text content to send in the progress update
   * @param {string|null} args.requestId - Unique identifier for the request
   * @param {Function|null} args.inferenceProgressCallback - Callback function to report inference progress
   * @param {MessagePort|null} args.port - Port for posting messages to the caller
   * @param {boolean} args.isDone - Whether this is the final progress update
   */
  #sendProgress(args) {
    const {
      content,
      requestId,
      inferenceProgressCallback,
      port,
      isDone,
      toolCalls,
      usage,
    } = args;
    port?.postMessage({
      text: content,
      ...(toolCalls ? { toolCalls } : {}),
      ...(isDone && { done: true, finalOutput: content }),
      ok: true,
    });
    inferenceProgressCallback?.({
      ok: true,
      metadata: {
        text: content,
        requestId,
        tokens: [],
        ...(toolCalls ? { toolCalls } : {}),
        ...(usage ? { usage } : {}),
      },
      type: Progress.ProgressType.INFERENCE,
      statusText: isDone
        ? Progress.ProgressStatusText.DONE
        : Progress.ProgressStatusText.IN_PROGRESS,
    });
  }

  /**
   * Finalizes the tool calls by sorting them by index and returning an array of call objects.
   *
   * @param {map} acc
   * @returns {Array}
   */

  #finalizeToolCalls(acc) {
    // Convert Map entries ([index, call]) to an array
    const entries = Array.from(acc.entries());
    // Sort by the numeric index
    entries.sort((a, b) => a[0] - b[0]);
    // Return just the call objects (drop the index)
    return entries.map(([_index, call]) => call);
  }

  /**
   * Because we are streaming here, we may get multiple partial tool_calls deltas and need to merge them together.
   * This helper does that by looking at the index property on each tool call fragment.
   *
   * @param {map} acc - Accumulated tool calls map
   * @param {Array} deltas - New tool call fragments to merge
   * @returns {map} Merged tool calls map
   */
  #mergeToolDeltas(acc, deltas) {
    // If no deltas, return acc unchanged
    if (!Array.isArray(deltas) || deltas.length === 0) {
      return acc;
    }

    let next = new Map(acc); // shallow copy to keep immutability

    for (const toolCall of deltas) {
      const idx = toolCall.index ?? 0;
      const existing = next.get(idx) || {
        id: null,
        type: "function",
        function: { name: "", arguments: "" },
      };
      const nameFrag = toolCall.function?.name ?? "";
      const argsFrag = toolCall.function?.arguments ?? "";

      // Merge fragments into previous entry
      const merged = {
        id: toolCall.id ?? existing.id,
        type: "function",
        function: {
          name: nameFrag
            ? existing.function.name + nameFrag
            : existing.function.name,
          arguments: argsFrag
            ? existing.function.arguments + argsFrag
            : existing.function.arguments,
        },
      };
      next.set(idx, merged);
    }
    return next;
  }

  /**
   * Handles streaming response from the OpenAI API.
   * Processes each chunk as it arrives and sends progress updates.
   *
   * @private
   * @param {object} args - The arguments object
   * @param {OpenAI} args.client - OpenAI client instance
   * @param {object} args.completionParams - Parameters for the completion request
   * @param {string|null} args.requestId - Unique identifier for the request
   * @param {Function|null} args.inferenceProgressCallback - Callback function to report inference progress
   * @param {MessagePort|null} args.port - Port for posting messages to the caller
   * @returns {Promise<object>} Result object with done, finalOutput, ok, and metrics properties
   */
  async #handleStreamingResponse(args) {
    const {
      client,
      completionParams,
      requestId,
      inferenceProgressCallback,
      port,
      signal,
    } = args;

    const createStartTime = ChromeUtils.now();
    const stream = await client.chat.completions.create(completionParams, {
      signal,
    });
    let lastChunkTime = ChromeUtils.now();
    ChromeUtils.addProfilerMarker(
      "MLEngine:OpenAI",
      createStartTime,
      "Stream opened"
    );

    let streamOutput = "";
    let toolAcc = new Map();
    let sawToolCallsFinish = false;
    let usage = null;
    let chunkIndex = 0;

    // Armed once finish_reason arrives, so a server that never closes the
    // stream cannot hold the engine open. Aborting the request interrupts the
    // pending read, which the stream generator treats as a clean end.
    let drainTimeoutId = null;

    try {
      for await (const chunk of stream) {
        const chunkTime = ChromeUtils.now();
        const choice = chunk?.choices?.[0];
        const delta = choice?.delta ?? {};

        // Chunk 0 measures stream open to first chunk; later ones the gap.
        let chunkKind = "empty";
        if (delta.content) {
          chunkKind = "content";
        } else if (delta.tool_calls) {
          chunkKind = "tool_calls";
        } else if (chunk?.usage) {
          chunkKind = "usage";
        }
        ChromeUtils.addProfilerMarker(
          "MLEngine:OpenAI",
          lastChunkTime,
          `Raw chunk #${chunkIndex} ${chunkKind}`
        );
        chunkIndex++;
        lastChunkTime = chunkTime;

        // Normal text tokens
        if (delta.content) {
          streamOutput += delta.content;
          this.#sendProgress({
            content: delta.content,
            requestId,
            inferenceProgressCallback,
            port,
            isDone: false,
          });
        }

        if (Array.isArray(delta.tool_calls) && delta.tool_calls.length) {
          toolAcc = this.#mergeToolDeltas(toolAcc, delta.tool_calls);
        }

        if (chunk?.usage) {
          usage = chunk.usage;
        }

        // If the model signals it wants tools now
        if (choice?.finish_reason === "tool_calls") {
          sawToolCallsFinish = true;
          const toolCalls = this.#finalizeToolCalls(toolAcc);

          // Emit the completed tool calls to the caller so they can execute
          // them.
          this.#sendProgress({
            content: "", // no user-visible text here
            requestId,
            inferenceProgressCallback,
            port,
            isDone: false,
            toolCalls,
          });

          // Keep reading: the usage chunk arrives after finish_reason.
          drainTimeoutId ??= setTimeout(() => {
            ChromeUtils.addProfilerMarker(
              "MLEngine:OpenAI",
              {},
              "Tool-call drain timed out; cancelling"
            );
            stream.controller.abort();
          }, TOOL_CALL_DRAIN_TIMEOUT_MS);
        }
      }
    } finally {
      clearTimeout(drainTimeoutId);
    }

    // Final message: does not carry full content to avoid duplication
    this.#sendProgress({
      requestId,
      inferenceProgressCallback,
      port,
      isDone: true,
      ...(usage ? { usage } : {}),
    });

    return {
      finalOutput: streamOutput,
      metrics: [],
      ...(sawToolCallsFinish
        ? { toolCalls: this.#finalizeToolCalls(toolAcc) }
        : {}),
    };
  }

  /**
   * Handles non-streaming response from the OpenAI API.
   * Waits for the complete response and sends it as a single update.
   *
   * @private
   * @param {object} args - The arguments object
   * @param {OpenAI} args.client - OpenAI client instance
   * @param {object} args.completionParams - Parameters for the completion request
   * @param {string|null} args.requestId - Unique identifier for the request
   * @param {Function|null} args.inferenceProgressCallback - Callback function to report inference progress
   * @param {MessagePort|null} args.port - Port for posting messages to the caller
   * @returns {Promise<object>} Result object with done, finalOutput, ok, and metrics properties
   */
  async #handleNonStreamingResponse(args) {
    const {
      client,
      completionParams,
      requestId,
      inferenceProgressCallback,
      port,
      signal,
    } = args;

    const createStartTime = ChromeUtils.now();
    const completion = await client.chat.completions.create(completionParams, {
      signal,
    });
    ChromeUtils.addProfilerMarker(
      "MLEngine:OpenAI",
      createStartTime,
      "Completion received"
    );
    const message = completion.choices[0].message;
    const output = message.content || "";
    const toolCalls = message.tool_calls || null;
    // Backend-specific data carried alongside the message, e.g. the `citations`
    // Exa returns on its answer service. Only surfaced here: it has no
    // equivalent on the streaming path, where the deltas carry content alone.
    const providerSpecificFields = message.provider_specific_fields || null;

    this.#sendProgress({
      content: output,
      requestId,
      inferenceProgressCallback,
      port,
      isDone: true,
      toolCalls,
    });

    return {
      finalOutput: output,
      metrics: [],
      ...(toolCalls ? { toolCalls } : {}),
      ...(providerSpecificFields ? { providerSpecificFields } : {}),
    };
  }

  /**
   * Executes the OpenAI pipeline with the given request.
   * Supports both streaming and non-streaming modes based on options configuration.
   *
   * @param {object} request - The request object containing the messages
   * @param {Array} request.args - Array of message objects for the chat completion
   * @param {string|null} [requestId=null] - Unique identifier for this request
   * @param {Function|null} [inferenceProgressCallback=null] - Callback function to report progress during inference
   * @param {MessagePort|null} [port=null] - Port for posting messages back to the caller
   * @returns {Promise<object>} Result object containing completion status, output, and metrics
   * @throws {Error} Throws backend error if the API request fails
   */
  async run(
    request,
    requestId = null,
    inferenceProgressCallback = null,
    port = null
  ) {
    lazy.console.debug("Running OpenAI pipeline");
    const controller = new AbortController();
    this.#abortControllers.set(requestId, controller);
    try {
      const {
        baseURL,
        apiKey,
        modelId,
        serviceType,
        purpose,
        extraHeaders,
        engineId,
        maxRetries,
      } = this.#options;
      const token = request.token ?? null;
      const chatId = request.chatId;

      const userAgent = navigator.userAgent;
      const client = new OpenAIPipeline.OpenAILib.OpenAI({
        baseURL: baseURL ? baseURL : "http://localhost:11434/v1",
        apiKey: apiKey || token || "apiKey",
        maxRetries,
        defaultHeaders: {
          "user-agent": userAgent,
          ...extraHeaders,
          "service-type": serviceType || "ai",
          // Omitted when unset rather than defaulted to "chat": a service that
          // enumerates the purposes it accepts rejects the ones it does not
          // know, and every feature with a purpose passes one explicitly.
          ...(purpose ? { purpose } : {}),
          "x-engine-id": engineId,
          "chat-id": chatId,
        },
      });
      const stream = request.streamOptions?.enabled || false;
      const tools = request.tools || [];

      // `stream` and `tools` are omitted rather than sent falsy/empty: absent
      // means the same thing to a chat completion, and services that do not
      // offer streaming or tools at all reject the keys outright.
      const completionParams = {
        ...request.inferenceParams,
        model: modelId,
        messages: request.args,
        ...(stream ? { stream } : {}),
        ...(tools.length ? { tools } : {}),
      };
      if (stream) {
        completionParams.stream_options = { include_usage: true };
      }
      const args = {
        client,
        completionParams,
        requestId,
        inferenceProgressCallback,
        port,
        signal: controller.signal,
      };

      return stream
        ? await this.#handleStreamingResponse(args)
        : await this.#handleNonStreamingResponse(args);
    } catch (error) {
      const backendError = this.#errorFactory(error);
      port?.postMessage({ done: true, ok: false, error: backendError });
      inferenceProgressCallback?.({
        ok: false,
        metadata: {
          text: "",
          requestId,
          tokens: [],
          errorMessage: error.error,
        },
        type: Progress.ProgressType.INFERENCE,
        statusText: Progress.ProgressStatusText.DONE,
        ...(error.status && { status: error.status }),
      });

      throw backendError;
    } finally {
      this.#abortControllers.delete(requestId);
    }
  }

  async cancel(requestId) {
    this.#abortControllers.get(requestId)?.abort();
  }
}

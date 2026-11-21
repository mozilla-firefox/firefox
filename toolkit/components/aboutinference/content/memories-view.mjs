/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* global HWInference */

import { html, nothing } from "chrome://global/content/vendor/lit.all.mjs";
import { MozLitElement } from "chrome://global/content/lit-utils.mjs";

const { loadMemoriesWorkload, MEMORIES_MODEL } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/aboutinference/content/MemoriesWorkload.sys.mjs"
);

const WORKLOADS = ["generate", "merge", "filter"];
const ACTIVE = new Set(["downloading", "loading", "running"]);
const TIMEOUT_MS = 5 * 60 * 1000;
const DASH = "—";
const CACHE_KEY = {
  model: `${new URL(MEMORIES_MODEL.modelHubRootUrl).hostname}/${MEMORIES_MODEL.modelId}`,
  revision: MEMORIES_MODEL.modelRevision,
};

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  createEngine: "chrome://global/content/ml/EngineProcess.sys.mjs",
  DownloadUtils: "resource://gre/modules/DownloadUtils.sys.mjs",
  MLTelemetry: "chrome://global/content/ml/MLTelemetry.sys.mjs",
  ModelHub: "chrome://global/content/ml/ModelHub.sys.mjs",
  ProgressStatusText: "chrome://global/content/ml/Utils.sys.mjs",
  ProgressType: "chrome://global/content/ml/Utils.sys.mjs",
});

const l10n = (id, args) =>
  html`<span
    data-l10n-id="about-inference-memories-${id}"
    data-l10n-args=${JSON.stringify(args)}
  ></span>`;

const item = (id, value) =>
  html`<div>
    <dt data-l10n-id="about-inference-memories-${id}"></dt>
    <dd>${value ?? DASH}</dd>
  </div>`;

const bytes = value =>
  value == null ? DASH : lazy.DownloadUtils.getTransferTotal(value);

const duration = ms =>
  ms == null ? DASH : l10n("duration", { seconds: ms / 1000 });

const tokenRate = (tokens, ms) => {
  const rate = (tokens * 1000) / ms;
  return Number.isFinite(rate) ? l10n("token-rate", { rate }) : DASH;
};

const mebibytesPerSecond = (size, ms) => (size * 1000) / ms / 2 ** 20;

const durationFormat = new Intl.DurationFormat(
  Services.locale.appLocalesAsBCP47,
  { style: "narrow" }
);

/** On-device memories experiment. */
export class MemoriesView extends MozLitElement {
  static properties = {
    status: { state: true },
    error: { state: true },
    run: { state: true },
    running: { state: true },
    cacheSize: { state: true },
    system: { state: true },
  };

  #controller;
  #engine;
  #hub = new lazy.ModelHub({ rootUrl: MEMORIES_MODEL.modelHubRootUrl });
  #cacheReady;

  constructor() {
    super();
    this.status = "idle";
    this.error = null;
    this.run = null;
    this.running = false;
    this.cacheSize = null;
    this.system = null;
  }

  connectedCallback() {
    super.connectedCallback();
    this.documentGlobal.addEventListener("pagehide", this.#cancel);
    this.#cacheReady = this.#readCache();
    this.#readSystem();
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.documentGlobal.removeEventListener("pagehide", this.#cancel);
    this.#cancel();
  }

  #cancel = () => this.#controller?.abort();

  async #readCache() {
    const { files } = await this.#hub
      .listFiles(CACHE_KEY)
      .catch(() => ({ files: [] }));
    this.cacheSize =
      files.find(({ path }) => path === MEMORIES_MODEL.modelFile)?.headers
        .fileSize ?? null;
  }

  async #readSystem() {
    const { name, cores } = await Services.sysinfo.processInfo.catch(
      () => ({})
    );
    this.system = {
      name,
      cores,
      memory: Services.sysinfo.getProperty("memsize"),
    };
  }

  async #describeDevice(id) {
    try {
      const devices = id ? await HWInference.listDevices() : [];
      return devices.find(device => device.id === id)?.description;
    } catch {
      return null;
    }
  }

  async #cleanUp() {
    if (this.running || this.status === "cleaning") {
      return;
    }
    this.status = "cleaning";
    this.error = null;
    try {
      await this.#cacheReady;
      await this.#hub.deleteModels({
        ...CACHE_KEY,
        filterFn: ({ file }) => file === MEMORIES_MODEL.modelFile,
        deletedBy: MEMORIES_MODEL.engineId,
      });
      this.run = null;
      this.cacheSize = null;
      this.status = "idle";
    } catch (error) {
      await this.#readCache();
      this.status = "failed";
      this.error = String(error);
    }
  }

  #onProgress(model, data) {
    if (data.type === lazy.ProgressType.DOWNLOAD) {
      if (data.statusText === lazy.ProgressStatusText.INITIATE) {
        model.downloadStart = ChromeUtils.now();
      } else if (data.statusText === lazy.ProgressStatusText.DONE) {
        model.downloadEnd = ChromeUtils.now();
      }
      if (ACTIVE.has(this.status)) {
        this.status =
          model.downloadStart && !model.downloadEnd ? "downloading" : "loading";
      }
      model.loaded = data.totalLoaded || model.loaded;
      model.size = data.total || model.size;
    } else if (data.type === lazy.ProgressType.LOAD_FROM_CACHE) {
      model.cached = true;
    }
    this.requestUpdate();
  }

  async #run() {
    if (this.running || this.status === "cleaning") {
      return;
    }
    const controller = new AbortController();
    const { signal } = controller;
    signal.addEventListener(
      "abort",
      () => {
        if (ACTIVE.has(this.status)) {
          this.status = "cancelling";
        }
        this.#engine?.cancel();
      },
      { once: true }
    );
    this.#controller = controller;
    this.running = true;
    this.status = "loading";
    this.error = null;
    const run = { model: { loaded: 0, size: 0 }, calls: [] };
    this.run = run;
    const telemetry = new lazy.MLTelemetry({
      featureId: MEMORIES_MODEL.featureId,
    });
    telemetry.sessionStart({ interaction: "button_click" });
    let timeout;
    try {
      await this.#cacheReady;
      const workload = await loadMemoriesWorkload();
      signal.throwIfAborted();
      const start = ChromeUtils.now();
      this.#engine = await lazy.createEngine(
        { ...MEMORIES_MODEL, flowId: telemetry.flowId },
        data => this.#onProgress(run.model, data),
        signal
      );
      signal.throwIfAborted();
      run.model.loadMs = ChromeUtils.now() - (run.model.downloadEnd ?? start);
      run.model.device = this.#engine.deviceKind;
      run.model.accelerator = await this.#describeDevice(this.#engine.device);
      signal.throwIfAborted();
      this.status = "running";
      timeout = setTimeout(
        () => controller.abort(new DOMException("", "TimeoutError")),
        TIMEOUT_MS
      );
      for (const { id, featureId, prompt } of workload) {
        const call = { id, featureId, status: "running" };
        run.calls.push(call);
        this.requestUpdate();
        call.result = await this.#engine.run({
          args: prompt,
          featureId,
          inferenceParams: { temperature: 0 },
        });
        signal.throwIfAborted();
        call.status = "done";
      }
      this.status = "done";
    } catch (error) {
      const { aborted, reason } = signal;
      if (!aborted) {
        this.status = "failed";
        this.error = String(error);
      } else {
        this.status =
          reason?.name === "TimeoutError" ? "timed-out" : "cancelled";
      }
      const call = run.calls.at(-1);
      if (call?.status === "running") {
        call.status = this.status;
      }
    } finally {
      clearTimeout(timeout);
      await this.#engine?.terminate().catch(error => {
        this.status = "failed";
        this.error = String(error);
      });
      this.#engine = null;
      this.#controller = null;
      telemetry.endSession(this.status);
      await this.#readCache();
      this.running = false;
    }
  }

  #renderControls() {
    const { loaded, size, downloadStart } = this.run?.model ?? {};
    const elapsed = ChromeUtils.now() - downloadStart;
    const status =
      this.status === "running"
        ? `running-${this.run.calls.at(-1).id}`
        : this.status;
    return html`
      <div class="controls">
        <div class="actions">
          ${this.running
            ? html`<moz-button
                id="cancel"
                iconsrc="chrome://global/skin/icons/loading.svg"
                data-l10n-id="about-inference-memories-cancel-button"
                ?disabled=${!ACTIVE.has(this.status)}
                @click=${this.#cancel}
              ></moz-button>`
            : html`<moz-button
                id="run"
                type="primary"
                data-l10n-id="about-inference-memories-run-button"
                ?disabled=${this.status === "cleaning"}
                @click=${this.#run}
              ></moz-button>`}
          <moz-button
            id="cleanup"
            data-l10n-id="about-inference-memories-cleanup-button"
            ?disabled=${this.running || this.status === "cleaning"}
            @click=${this.#cleanUp}
          ></moz-button>
        </div>
        <p class="status">
          <span
            id="status"
            role="status"
            data-l10n-id="about-inference-memories-${status}"
          ></span>
          ${this.status === "downloading" && loaded && size
            ? l10n("time-left", {
                minutes: Math.ceil(((size - loaded) * elapsed) / loaded / 6e4),
                speed: mebibytesPerSecond(loaded, elapsed),
              })
            : nothing}
          ${this.error
            ? html`<span class="error" title=${this.error}>${this.error}</span>`
            : nothing}
        </p>
      </div>
    `;
  }

  #renderSetup() {
    const { device, accelerator } = this.run?.model ?? {};
    const kind = device?.toUpperCase();
    const { name, cores, memory } = this.system ?? {};
    return html`
      <section>
        <h2 data-l10n-id="about-inference-memories-setup"></h2>
        <dl class="specs">
          ${item("model", html`<code>${MEMORIES_MODEL.modelFile}</code>`)}
          ${item(
            "accelerator",
            accelerator
              ? l10n("accelerator-value", { name: accelerator, kind })
              : kind
          )}
          ${item(
            "processor",
            name ? l10n("processor-value", { name, cores: cores ?? 0 }) : null
          )}
          ${item("system-memory", bytes(memory))}
        </dl>
      </section>
    `;
  }

  #renderDownload() {
    const { loaded, size, cached, downloadStart, downloadEnd } =
      this.run?.model ?? {};
    if (this.status === "downloading") {
      return size
        ? l10n("download-progress", {
            size: bytes(size),
            percent: Math.floor((loaded * 100) / size),
          })
        : null;
    }
    if (cached) {
      return l10n("cached", { size: bytes(this.cacheSize) });
    }
    const ms = downloadEnd - downloadStart;
    const seconds = Math.max(1, Math.round(ms / 1000));
    return ms > 0 && size
      ? l10n("downloaded", {
          size: bytes(size),
          duration: durationFormat.format({
            minutes: Math.floor(seconds / 60),
            seconds: seconds % 60,
          }),
          speed: mebibytesPerSecond(size, ms),
        })
      : null;
  }

  #renderSummary() {
    const completed = this.run?.calls.filter(call => call.result) ?? [];
    const total = name =>
      completed.length
        ? completed.reduce((sum, { result }) => sum + result.metrics[name], 0)
        : null;
    const memory = completed
      .map(({ result }) => result.resourcesAfter?.memory)
      .filter(value => value != null);
    return html`
      <section>
        <h2 data-l10n-id="about-inference-memories-summary"></h2>
        <dl class="summary">
          ${item("download", this.#renderDownload())}
          ${item("load", duration(this.run?.model.loadMs))}
          ${item("memory", bytes(memory.length ? Math.max(...memory) : null))}
          ${item("col-time", duration(total("inferenceTime")))}
          ${item(
            "prefill-speed",
            tokenRate(total("inputTokens"), total("timeToFirstToken"))
          )}
          ${item(
            "decode-speed",
            tokenRate(total("outputTokens"), total("decodingTime"))
          )}
        </dl>
      </section>
    `;
  }

  #renderCall(id) {
    const call = this.run?.calls.find(c => c.id === id);
    const metrics = call?.result?.metrics;
    return html`
      <tr data-status=${call?.status ?? nothing}>
        <th scope="row" data-l10n-id="about-inference-memories-${id}"></th>
        <td>${metrics?.inputTokens ?? DASH}</td>
        <td>${metrics?.outputTokens ?? DASH}</td>
        <td>${duration(metrics?.inferenceTime)}</td>
        <td>${bytes(call?.result?.resourcesAfter?.memory)}</td>
      </tr>
    `;
  }

  render() {
    return html`
      <link rel="stylesheet" href="chrome://global/content/memories-view.css" />
      <header>
        <h1 data-l10n-id="about-inference-memories-title"></h1>
        <p data-l10n-id="about-inference-memories-description"></p>
      </header>
      ${this.#renderControls()} ${this.#renderSetup()} ${this.#renderSummary()}
      <section>
        <h2
          id="results-title"
          data-l10n-id="about-inference-memories-results"
        ></h2>
        <table aria-labelledby="results-title">
          <thead>
            <tr>
              ${[
                "about-inference-memories-col-task",
                "about-inference-text-generation-col-prompt-tokens",
                "about-inference-text-generation-col-output-tokens",
                "about-inference-memories-col-time",
                "about-inference-memories-memory",
              ].map(
                column => html`<th scope="col" data-l10n-id=${column}></th>`
              )}
            </tr>
          </thead>
          <tbody>
            ${WORKLOADS.map(id => this.#renderCall(id))}
          </tbody>
        </table>
      </section>
    `;
  }
}

customElements.define("memories-view", MemoriesView);

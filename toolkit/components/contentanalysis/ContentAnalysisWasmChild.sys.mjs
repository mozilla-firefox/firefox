/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Content-process half of the content-analysis WebAssembly runner. This actor
 * runs the module in a null-principal sandbox so that compiling it is permitted.
 */

const SANDBOX_SCRIPT =
  "moz-src:///toolkit/components/contentanalysis/content-analysis-wasm-sandbox.js";

/**
 * Compiles and runs the DLP module in a null-principal sandbox.
 */
export class ContentAnalysisWasmChild extends JSProcessActorChild {
  // The null-principal sandbox that hosts the compiled module, created lazily
  // and reused across requests.
  #sandbox = null;
  // Version of the module currently compiled in the sandbox, so we only recompile
  // when the parent switches to a different version.
  #loadedVersion = null;

  #ensureSandbox() {
    if (!this.#sandbox) {
      const principal = Services.scriptSecurityManager.createNullPrincipal({});
      const sandbox = Cu.Sandbox(principal, {
        wantGlobalProperties: ["TextEncoder"],
        sandboxName: "content-analysis-wasm",
      });
      Services.scriptloader.loadSubScript(SANDBOX_SCRIPT, sandbox);
      this.#sandbox = sandbox;
    }
    return this.#sandbox;
  }

  async receiveMessage(message) {
    if (message.name !== "Analyze") {
      return undefined;
    }

    const { version, moduleBytes, requestBytes, contentBytes, rules } =
      message.data;
    const sandbox = this.#ensureSandbox();

    // Recompile only when the module changed since the last request.
    if (this.#loadedVersion !== version) {
      sandbox.loadModule(Cu.cloneInto(moduleBytes, sandbox));
      this.#loadedVersion = version;
    }

    const responseBytes = sandbox.runAnalyze(
      Cu.cloneInto({ requestBytes, contentBytes, rules }, sandbox)
    );
    // Bulk-copy out of the sandbox compartment into a Uint8Array of our own,
    // which IPC structured-clones and C++ reads directly.
    return new Uint8Array(responseBytes);
  }
}

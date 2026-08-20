/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

export const ProcessType = Object.freeze({
  /**
   * Converts a key string to a fluent ID defined in processTypes.ftl.
   */
  kProcessTypeMap: {
    // Keys defined in xpcom/build/GeckoProcessTypes.h
    default: "process-type-default",
    gpu: "process-type-gpu",
    tab: "process-type-tab",
    rdd: "process-type-rdd",
    socket: "process-type-socket",
    utility: "process-type-utility",
    forkServer: "process-type-forkserver",

    // Utility with actor names
    utility_audioDecoder_Generic:
      "process-type-utility-actor-audio-decoder-generic",
    utility_audioDecoder_AppleMedia:
      "process-type-utility-actor-audio-decoder-applemedia",
    utility_audioDecoder_WMF: "process-type-utility-actor-audio-decoder-wmf",
    utility_mfMediaEngineCDM: "process-type-utility-actor-mf-media-engine",
    utility_jSOracle: "process-type-utility-actor-js-oracle",
    utility_windowsUtils: "process-type-utility-actor-windows-utils",
    utility_windowsFileDialog: "process-type-utility-actor-windows-file-dialog",
    utility_hwInference: "process-type-utility-actor-hw-inference",

    // Keys defined in dom/ipc/RemoteType.h
    extension: "process-type-extension",
    file: "process-type-file",
    inference: "process-type-inference",
    prealloc: "process-type-prealloc",
    privilegedabout: "process-type-privilegedabout",
    privilegedmozilla: "process-type-privilegedmozilla",
    web: "process-type-web",
    webIsolated: "process-type-webisolated",
    webServiceWorker: "process-type-webserviceworker",
  },

  kFallback: "process-type-unknown",

  fluentNameFromProcessTypeString(type) {
    return this.kProcessTypeMap[type] || this.kFallback;
  },
});

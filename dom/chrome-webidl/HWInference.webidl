/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

enum HWInferenceDeviceKind {
  "cpu",
  "gpu",
  "npu",
};

enum HWInferenceBackend {
  "llama.cpp",
};

[GenerateConversionToJS]
dictionary HWInferenceDevice {
  /**
   * Physical identity where the backend reports one (PCI bus id), else the
   * backend's own device name.
   */
  required UTF8String id;
  /** Vendor string, for display. */
  required UTF8String description;
  required HWInferenceDeviceKind kind;
  /** Backends whose real initialisation of this device succeeded. */
  required sequence<HWInferenceBackend> backends;
};

[ChromeOnly, Exposed=Window]
namespace HWInference {
  /** Starts the browser inference process if needed. */
  [Throws]
  Promise<sequence<HWInferenceDevice>> listDevices();
};

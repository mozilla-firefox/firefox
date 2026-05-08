/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  createEngine,
  FEATURES,
} from "chrome://global/content/ml/EngineProcess.sys.mjs";

import { AIFeature } from "chrome://global/content/ml/AIFeature.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AutofillTelemetry: "resource://gre/modules/shared/AutofillTelemetry.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
});

const FORM_AUTOFILL_FEATURE_ID = "formfill-classification";
const ML_TASKNAME = "text-classification";

const FormFill_Config = {
  timeoutMS: 2 * 60 * 1000, // 2 minutes
  taskName: ML_TASKNAME,
  featureId: FORM_AUTOFILL_FEATURE_ID,
  engineId: FEATURES[FORM_AUTOFILL_FEATURE_ID].engineId,
  backend: "onnx-native",
  fallbackBackend: "onnx",
  modelId: "mozilla/tinybert-address-autofill",
  modelRevision: "v0.1.0",
  // The dtype will need to be updated as needed.
  dtype: "fp32",
};

export class FormAutofillML extends AIFeature {
  static async id() {
    return "formfill-ml";
  }

  // For now, these are just placeholders.
  static async enable() {}
  static async block() {}
  static async makeAvailable() {}
  static async isEnabled() {
    return true;
  }
  static async isAllowed() {
    return true;
  }
  static async isBlocked() {
    return false;
  }
  static async isManagedByPolicy() {
    return false;
  }

  static addToHash(hash, str) {
    for (let i of str) {
      hash = ((hash << 5) - hash + i.charCodeAt(0)) | 0;
    }
    return hash;
  }

  static async detectFields(fieldDetails) {
    let engine;
    try {
      engine = await createEngine(FormFill_Config);
    } catch (ex) {
      return;
    }

    // Hash of the data for the form
    let hash = 0;

    let results = [];
    for (let fd of fieldDetails) {
      const request = {
        args: [fd.extraInfo.mlData],
        options: { pooling: "mean", normalize: true },
      };

      hash = this.addToHash(hash, fd.extraInfo.mlData);

      let result = await engine.run(request);
      results.push(result[0].label == "other" ? "" : result[0].label);
    }

    let mlEnabled = lazy.FormAutofillUtils.enableMLAutofill;

    // If ML is enabled, then it will be used for autofill.
    // Otherwise, we just calculate the ML inferred fields for
    // telemetry but don't use them for autofill.
    for (let f = 0; f < fieldDetails.length; f++) {
      fieldDetails[f].mlFieldName = results[f];
      if (mlEnabled) {
        fieldDetails[f].fieldName = results[f];
      }
    }

    lazy.AutofillTelemetry.recordMLDetection(fieldDetails, hash);
  }
}

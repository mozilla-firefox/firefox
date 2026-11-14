/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const MODEL_ICON_URLS = {
  0: "chrome://browser/content/aiwindow/assets/model-choice-0.svg",
  1: "chrome://browser/content/aiwindow/assets/model-choice-gemini.svg",
  2: "chrome://browser/content/aiwindow/assets/model-choice-qwen.svg",
  3: "chrome://browser/content/aiwindow/assets/model-choice-mistral.svg",
  4: "chrome://browser/content/aiwindow/assets/model-choice-auto.svg",
};

export function getModelIconSrc(choiceId) {
  return MODEL_ICON_URLS[choiceId];
}

/* Copyright 2024 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(ANDROID)

// Enabled for testing signatures bug 2061833 will turn it on for release when ready
#ifdef NIGHTLY_BUILD
  pref("pdfjs.annotationEditorMode", 0);
  pref("pdfjs.enableSignatureEditor", true);
#else
  pref("pdfjs.annotationEditorMode", -1);
#endif

  pref("pdfjs.capCanvasAreaFactor", 100);

#else

  pref("pdfjs.enableUpdatedAddImage", true);
  pref("pdfjs.enableSignatureEditor", true);
  pref("pdfjs.enableComment", true);
  pref("pdfjs.enableHighlightFloatingButton", true);

  pref("pdfjs.enableAltTextForEnglish", false);
  pref("pdfjs.enableAltText", true);
  pref("pdfjs.enableAltTextModelDownload", false);
  pref("pdfjs.enableGuessAltText", false);

  pref("pdfjs.enableHWA", true);

  pref("pdfjs.enableSplitMerge", true);
  pref("pdfjs.enableMerge", true);

#endif

pref("pdfjs.enableOptimizedPartialRendering", true);

#if defined(NIGHTLY_BUILD) || defined(MOZ_DEV_EDITION)
  pref("pdfjs.disableWorkerRendering", false);
#endif

// Off by default until the UX is exercised in Enterprise. Flip via
// `about:config`, a profile pref, or the `enableSignatureVerification`
// variable of the `pdfjs` Nimbus feature. The viewer also short-circuits its
// own toolbar button when this is false (web/app_options.js).
pref("pdfjs.enableSignatureVerification", false);

#ifdef MOZ_THUNDERBIRD
  // Work-around until bug 2070962 is resolved.
  pref("pdfjs.enableSelectionRendering", false);
#endif

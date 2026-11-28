/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import * as codemirror from "codemirror";
import * as codemirrorAutocomplete from "@codemirror/autocomplete";
import * as codemirrorView from "@codemirror/view";
import * as codemirrorState from "@codemirror/state";
import * as codemirrorSearch from "@codemirror/search";
import * as codemirrorLanguage from "@codemirror/language";
import * as codemirrorLangJavascript from "@codemirror/lang-javascript";
import * as codemirrorLangCss from "@codemirror/lang-css";
import * as codemirrorLangHtml from "@codemirror/lang-html";
import * as codemirrorLangJson from "@codemirror/lang-json";
import * as codemirrorLangXml from "@codemirror/lang-xml";
import * as codemirrorLangVue from "@codemirror/lang-vue";
import * as lezerHighlight from "@lezer/highlight";

// XXX When adding new exports, you need to update the codemirror6.bundle.mjs file,
// running `npm install && npm run build-cm6` from the devtools/client/shared/sourceeditor folder

export {
  codemirror,
  codemirrorAutocomplete,
  codemirrorView,
  codemirrorState,
  codemirrorSearch,
  codemirrorLanguage,
  codemirrorLangJavascript,
  codemirrorLangJson,
  codemirrorLangCss,
  codemirrorLangHtml,
  codemirrorLangXml,
  codemirrorLangVue,
  lezerHighlight,
};

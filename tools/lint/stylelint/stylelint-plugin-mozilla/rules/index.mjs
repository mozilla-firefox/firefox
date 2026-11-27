/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import mediaQueryNoInvalid from "./media-query-no-invalid.mjs";
import noBaseDesignTokens from "./no-base-design-tokens.mjs";
import noBrowserRefsInToolkit from "./no-browser-refs-in-toolkit.mjs";
import noHasSelector from "./no-has-selector.mjs";
import useDesignTokens from "./use-design-tokens.mjs";
import usePairedColorTokens from "./use-paired-color-tokens.mjs";

export default {
  "media-query-no-invalid": mediaQueryNoInvalid,
  "no-base-design-tokens": noBaseDesignTokens,
  "no-browser-refs-in-toolkit": noBrowserRefsInToolkit,
  "no-has-selector": noHasSelector,
  "use-design-tokens": useDesignTokens,
  "use-paired-color-tokens": usePairedColorTokens,
};

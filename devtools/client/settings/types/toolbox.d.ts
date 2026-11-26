/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Toolbox as BaseToolbox } from "../../framework/toolbox.js";

export interface Toolbox extends BaseToolbox {
  commands: {
    descriptorFront: {
      isTabDescriptor?: boolean;
    };
    targetConfigurationCommand: {
      isJavascriptEnabled(): Promise<boolean>;
      updateConfiguration(configuration: {
        javascriptEnabled: boolean;
      }): Promise<void>;
    };
  };
}

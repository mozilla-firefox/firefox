/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { existsSync, readdirSync } from "node:fs";
import { basename, join } from "node:path";
import { createDesktopFormat } from "../desktop-format/create-desktop-format.mjs";
import { COMPONENT_TOKEN_PATHS } from "./token-paths.mjs";
import { OVERRIDE_IDENTIFIERS } from "./override-identifiers.mjs";

export const getComponentInfo = () => {
  return COMPONENT_TOKEN_PATHS.filter(({ dir }) =>
    existsSync(join(import.meta.dirname, "../../..", dir))
  ).flatMap(
    ({ dir, isGlobal = false, isWeb = false, nameTransform = n => n }) => {
      const srcDir = join(import.meta.dirname, "../../..", dir);
      return readdirSync(srcDir, { recursive: true })
        .filter(f => typeof f === "string")
        .filter(
          f =>
            f.endsWith(".tokens.json") &&
            !OVERRIDE_IDENTIFIERS.some(({ id }) =>
              f.endsWith(`.${id}.tokens.json`)
            )
        )
        .map(relativePath => ({
          componentName: nameTransform(
            basename(relativePath).replace(".tokens.json", "")
          ),
          destination: isGlobal
            ? null
            : `${dir}/${relativePath.replace(".tokens.json", ".tokens.css")}`,
          isWeb,
        }));
    }
  );
};

export const getExternalComponentInfo = () =>
  /** @type {{ componentName: string, destination: string, isWeb: boolean }[]} */
  (getComponentInfo().filter(({ destination }) => destination !== null));

export const getExternalComponentFormatConfig = () =>
  getExternalComponentInfo().reduce(
    (config, { componentName }) => ({
      ...config,
      [`css/variables/${componentName}`]: createDesktopFormat({
        componentName,
      }),
    }),
    {}
  );

export const getExternalComponentFileConfig = () =>
  getExternalComponentInfo().map(({ componentName, destination }) => ({
    destination,
    format: `css/variables/${componentName}`,
  }));

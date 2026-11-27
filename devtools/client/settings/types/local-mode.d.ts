/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export interface Mapping {
  origin: string;
  path: string;
  disabled: boolean;
  prefPrefix: string;
}

export interface MappingState extends Mapping {
  folderExists: boolean;
  draftOrigin?: string;
}

export interface LocalModeMappingsModule {
  LocalModeMappings: Omit<
    typeof import("../../framework/LocalModeMappings.sys.mjs").LocalModeMappings,
    "on" | "getAllMappings"
  > & {
    on(
      event: "updated",
      listener: () => void,
      options?: { signal: AbortSignal }
    ): void;
    getAllMappings(): Mapping[];
  };
}

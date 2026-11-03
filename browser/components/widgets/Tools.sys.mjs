/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export const WRITE_FILE = "write_file";

export const WRITE_FILE_TOOL_CONFIG = {
  type: "function",
  function: {
    name: WRITE_FILE,
    description:
      "Adds one of the widget's files to its extension package, replacing any file at that path.",
    parameters: {
      type: "object",
      properties: {
        file_path: {
          type: "string",
          description:
            "The file's path from the package root, such as manifest.json or lib/dates.js.",
        },
        content: { type: "string", description: "The whole file." },
      },
      required: ["file_path", "content"],
    },
  },
};

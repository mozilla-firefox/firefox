/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export const WRITE_FILE = "write_file";

export const WRITE_FILE_TOOL_CONFIG = {
  type: "function",
  function: {
    name: WRITE_FILE,
    description:
      "Adds one of the widget's text files to its extension package, replacing any file at that path.",
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

export const EDIT_FILE = "edit_file";

export const EDIT_FILE_TOOL_CONFIG = {
  type: "function",
  function: {
    name: EDIT_FILE,
    description: "Replaces an exact passage in one of the widget's text files.",
    parameters: {
      type: "object",
      properties: {
        file_path: {
          type: "string",
          description:
            "The file's path from the package root, such as widget.html.",
        },
        old_string: {
          type: "string",
          description:
            "The exact passage to replace. It must appear once in the file unless replace_all is set.",
        },
        new_string: {
          type: "string",
          description: "The text that replaces it.",
        },
        replace_all: {
          type: "boolean",
          description: "Replaces every match of old_string.",
        },
      },
      required: ["file_path", "old_string", "new_string"],
    },
  },
};

export const READ_FILE = "read_file";

export const READ_FILE_TOOL_CONFIG = {
  type: "function",
  function: {
    name: READ_FILE,
    description: "Returns the current text of one of the widget's files.",
    parameters: {
      type: "object",
      properties: {
        file_path: {
          type: "string",
          description:
            "The file's path from the package root, such as widget.html.",
        },
      },
      required: ["file_path"],
    },
  },
};

export const DELETE_FILE = "delete_file";

export const DELETE_FILE_TOOL_CONFIG = {
  type: "function",
  function: {
    name: DELETE_FILE,
    description:
      "Removes one of the widget's files from its extension package.",
    parameters: {
      type: "object",
      properties: {
        file_path: {
          type: "string",
          description:
            "The file's path from the package root, such as lib/dates.js.",
        },
      },
      required: ["file_path"],
    },
  },
};

export const DECLINE_REQUEST = "decline_request";

export const DECLINE_KINDS = Object.freeze({
  UNSUPPORTED: "unsupported",
  UNCLEAR: "unclear",
  POLICY: "policy",
});

export const DECLINE_REQUEST_TOOL_CONFIG = {
  type: "function",
  function: {
    name: DECLINE_REQUEST,
    description: "Declines a request that can't be made into a widget.",
    parameters: {
      type: "object",
      properties: {
        kind: {
          type: "string",
          enum: Object.values(DECLINE_KINDS),
          description:
            "unclear: the request holds no widget. unsupported: the goal is clear but needs something a widget can't reach. policy: the widget would break the acceptable use rules.",
        },
        reason: {
          type: "string",
          description: "One sentence on why, for the developers' logs.",
        },
      },
      required: ["kind"],
    },
  },
};

export const TOOLS = [
  WRITE_FILE_TOOL_CONFIG,
  EDIT_FILE_TOOL_CONFIG,
  READ_FILE_TOOL_CONFIG,
  DELETE_FILE_TOOL_CONFIG,
  DECLINE_REQUEST_TOOL_CONFIG,
];

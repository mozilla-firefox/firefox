/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  DECLINE_REQUEST,
  DELETE_FILE,
  EDIT_FILE,
  READ_FILE,
  TOOLS,
  WRITE_FILE,
} from "moz-src:///browser/components/widgets/Tools.sys.mjs";

/**
 * Maps each file path to its contents and provides file methods.
 */
class Files extends Map {
  /**
   * @param {string} path
   * @returns {string|undefined} the file's text
   */
  read(path) {
    return this.get(path);
  }

  /**
   * @param {string} path
   * @param {string} text
   */
  write(path, text) {
    this.set(path, text);
  }

  /**
   * @param {string} path
   * @param {string} passage
   * @returns {number} how many times the passage appears in the file
   */
  count(path, passage) {
    return this.read(path).split(passage).length - 1;
  }

  /**
   * Replaces the first match of the passage, or every match.
   *
   * @param {string} path
   * @param {string} passage - the text to replace
   * @param {string} replacement - the text that replaces it
   * @param {boolean} all - whether to replace every match
   */
  edit(path, passage, replacement, all) {
    let text = this.read(path);
    let replace = () => replacement;
    if (all) {
      text = text.replaceAll(passage, replace);
    } else {
      text = text.replace(passage, replace);
    }
    this.write(path, text);
  }

  /**
   * @param {string} path
   */
  remove(path) {
    this.delete(path);
  }
}

/**
 * Holds the widget's files and runs the model's tool calls on them.
 */
export class Workspace {
  /** @type {Files} */
  files;

  #changed = false;

  /**
   * @param {Iterable<[string, string]>} [files] - existing files, by path
   */
  constructor(files) {
    this.files = new Files(files);
  }

  /**
   * Whether a tool call has written, edited or deleted a file.
   *
   * @type {boolean}
   */
  get changed() {
    return this.#changed;
  }

  /**
   * Checks a tool call's name, arguments and file_path, then runs it on the
   * files. A leading ./ or / is stripped from the path.
   *
   * @param {object} call - a tool call from the model's reply
   * @returns {{reply?: string, error?: string, decline?: object}} the result
   */
  run(call) {
    let { name, args, error } = this.#parse(call);
    if (error) {
      return { error };
    }
    if (name === DECLINE_REQUEST) {
      return {
        decline: {
          kind: args.kind,
          reason: typeof args.reason === "string" ? args.reason : undefined,
        },
      };
    }
    // Strips any run of leading "./" or "/" to get the path to lookup.
    let path = args.file_path.replace(/^(\.?\/)+/, "");
    if (!path) {
      return { error: `The ${name} call has no file_path.` };
    }
    if (name !== WRITE_FILE && !this.files.has(path)) {
      return { error: `There is no file at ${path}.` };
    }
    switch (name) {
      case WRITE_FILE:
        return this.#write(path, args.content);
      case READ_FILE:
        return { reply: this.files.read(path) };
      case EDIT_FILE:
        return this.#edit(path, args);
      case DELETE_FILE:
        return this.#delete(path);
    }
    return {};
  }

  /**
   * Finds the tool and checks the call's arguments against its parameters.
   *
   * @param {object} call - a tool call from the model's reply
   * @returns {{name?: string, args?: object, error?: string}}
   */
  #parse(call) {
    let name = call.function?.name;
    let tool = TOOLS.find(({ function: f }) => f.name === name);
    if (!tool) {
      return { error: `Unknown tool ${name}.` };
    }
    let args;
    try {
      args = JSON.parse(call.function.arguments);
    } catch {
      return { error: `The ${name} arguments are not valid JSON.` };
    }
    if (typeof args !== "object" || !args || Array.isArray(args)) {
      return { error: `The ${name} arguments are not a JSON object.` };
    }
    let { properties, required } = tool.function.parameters;
    for (let key of required) {
      let { type, enum: values } = properties[key];
      if (typeof args[key] !== type) {
        return { error: `The ${name} call needs ${key} as a ${type}.` };
      }
      if (values && !values.includes(args[key])) {
        return { error: `The ${key} must be one of ${values.join(", ")}.` };
      }
    }
    return { name, args };
  }

  #write(path, content) {
    this.files.write(path, content);
    this.#changed = true;
    return { reply: `Wrote ${path}.` };
  }

  #edit(path, { old_string, new_string, replace_all }) {
    if (!old_string) {
      return { error: `The edit_file call has no old_string.` };
    }
    let all = replace_all === true;
    let matches = this.files.count(path, old_string);
    if (matches == 0) {
      return {
        error: `The old_string is not in ${path}. Read it with read_file and copy the passage exactly, or use write_file with the whole file.`,
      };
    }
    if (matches > 1 && !all) {
      return {
        error: `The old_string appears ${matches} times in ${path}. Include more of the surrounding text so it matches once, or set replace_all to replace every match.`,
      };
    }
    this.files.edit(path, old_string, new_string, all);
    this.#changed = true;
    return { reply: `Edited ${path}.` };
  }

  #delete(path) {
    this.files.remove(path);
    this.#changed = true;
    return { reply: `Deleted ${path}.` };
  }
}

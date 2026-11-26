/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let localization;
function getLocalization() {
  if (!localization) {
    localization = new Localization(
      [
        "devtools/shared/webconsole.ftl",
        "devtools/shared/webconsole-commands.ftl",
      ],
      true
    );
  }
  return localization;
}

let dateFormatter;
let timestampFormatter;

const l10n = {
  /**
   * Generates a formatted timestamp string for displaying in console messages.
   *
   * @param integer [milliseconds]
   *        Optional, allows you to specify the timestamp in milliseconds since
   *        the UNIX epoch. If not provided, the current timestamp will be used.
   * @return string
   *         The timestamp formatted for display.
   */
  timestampString(milliseconds) {
    if (!timestampFormatter) {
      timestampFormatter = new Intl.DateTimeFormat(undefined, {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
        fractionalSecondDigits: 3,
        hourCycle: "h23",
        numberingSystem: "latn",
        timeZone: "UTC",
      });
    }
    // The formatter captures the time zone when created, so format the local
    // time as UTC to follow time zone changes.
    const time = milliseconds ?? Date.now();
    const offset = new Date(time).getTimezoneOffset() * 60 * 1000;
    return timestampFormatter.format(time - offset);
  },

  /**
   * Generates a formatted date string for displaying in console messages.
   *
   * @param integer [milliseconds]
   *        Optional, allows you to specify the timestamp in milliseconds since
   *        the UNIX epoch from which the date will be derived. If not provided,
   *        the current timestamp will be used.
   * @returns string
   *          The date formatted for display.
   */
  dateString(milliseconds) {
    if (!dateFormatter) {
      dateFormatter = new Intl.DateTimeFormat();
    }
    return dateFormatter.format(milliseconds || Date.now());
  },

  /**
   * Retrieve the value of a localized message.
   *
   * @param {string} id
   *        The Fluent message id.
   * @param {object} [args]
   *        The variables used to format the message.
   * @return {string}
   */
  getStr(id, args) {
    return getLocalization().formatValueSync(id, args);
  },

  /**
   * Retrieve the attributes of a localized message.
   *
   * @param {string} id
   *        The Fluent message id.
   * @return {object}
   *         An object mapping attribute names to their localized values.
   */
  getAttributes(id) {
    const [message] = getLocalization().formatMessagesSync([{ id }]);
    const attributes = {};
    for (const { name, value } of message?.attributes || []) {
      attributes[name] = value;
    }
    return attributes;
  },
};

module.exports = l10n;

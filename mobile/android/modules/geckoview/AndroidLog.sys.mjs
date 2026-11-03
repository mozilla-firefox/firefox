/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Native Android logging for JavaScript, wrapped for use by Log.sys.mjs.
 *
 * // Import it as a ESM:
 * import { AndroidAppender } from "moz-src:///mobile/android/modules/geckoview/AndroidLog.sys.mjs";
 *
 * // Add the appender to a Log.sys.mjs appender:
 * import { Log } from "resource://gre/modules/Log.sys.mjs";
 * let logger = Log.repository.getLogger("Example");
 * logger.addAppender(new AndroidAppender());
 *
 * // Log with the normal Log.sys.mjs API:
 * logger.info("This is an info message.");
 *
 * Note: Logger names will automatically be prepended with "Gecko" before being
 * logged to the system logger, if they do not already have a "Gecko" prefix.
 */

import { ctypes } from "resource://gre/modules/ctypes.sys.mjs";
import { Log } from "resource://gre/modules/Log.sys.mjs";

// From <https://android.googlesource.com/platform/system/core/+/master/include/android/log.h>.
const ANDROID_LOG_VERBOSE = 2;
const ANDROID_LOG_DEBUG = 3;
const ANDROID_LOG_INFO = 4;
const ANDROID_LOG_WARN = 5;
const ANDROID_LOG_ERROR = 6;

var liblog = ctypes.open("liblog.so"); // /system/lib/liblog.so
var __android_log_write = liblog.declare(
  "__android_log_write",
  ctypes.default_abi,
  ctypes.int, // return value: num bytes logged
  ctypes.int, // priority (ANDROID_LOG_* constant)
  ctypes.char.ptr, // tag
  ctypes.char.ptr
); // message

/**
 * A formatter that does not prepend time/name/level information to messages,
 * because those fields are logged separately when using the Android logger.
 */
class AndroidFormatter extends Log.BasicFormatter {
  format(message) {
    return this.formatText(message);
  }
}

/*
 * AndroidAppender
 * Logs to Android logcat using __android_log_write
 */
export class AndroidAppender extends Log.Appender {
  constructor(aFormatter) {
    super(aFormatter || new AndroidFormatter());
    this._name = "AndroidAppender";

    // Map log level to __android_log_write priority
    this._mapping = {
      [Log.Level.Fatal]: ANDROID_LOG_ERROR,
      [Log.Level.Error]: ANDROID_LOG_ERROR,
      [Log.Level.Warn]: ANDROID_LOG_WARN,
      [Log.Level.Info]: ANDROID_LOG_INFO,
      [Log.Level.Config]: ANDROID_LOG_DEBUG,
      [Log.Level.Debug]: ANDROID_LOG_DEBUG,
      [Log.Level.Trace]: ANDROID_LOG_VERBOSE,
    };
  }

  append(aMessage) {
    if (!aMessage) {
      return;
    }

    // We'll prepend "Gecko" to the tag, so we strip any leading "Gecko" here.
    // Also strip dots to save space.
    const tag = aMessage.loggerName.replace(/^Gecko|\./g, "");
    const msg = this._formatter.format(aMessage);

    // NOTE: android.util.Log.isLoggable throws IllegalArgumentException if a
    // tag length exceeds 23 characters, and we prepend five characters
    // ("Gecko") to every tag.  However, __android_log_write itself and other
    // android.util.Log methods don't seem to mind longer tags.
    __android_log_write(this._mapping[aMessage.level], "Gecko" + tag, msg);
  }
}

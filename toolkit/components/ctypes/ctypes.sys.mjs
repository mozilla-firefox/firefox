/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This is the js module for ctypes. Import it like so:
 *   const { ctypes } =
 *     ChromeUtils.importESModule("moz-src:///toolkit/components/ctypes/ctypes.sys.mjs");
 *
 * This will create a 'ctypes' object, which provides an interface to describe
 * and instantiate C types and call C functions from a dynamic library.
 */

// Initialize the ctypes object. You do not need to do this yourself.
const init = Cc["@mozilla.org/jsctypes;1"].createInstance();
const scope = {};
init(scope);
export const ctypes = scope.ctypes;

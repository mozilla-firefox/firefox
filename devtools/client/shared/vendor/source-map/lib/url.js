/* -*- Mode: js; js-indent-level: 2; -*- */
/*
 * Copyright 2011 Mozilla Foundation and contributors
 * Licensed under the New BSD license. See LICENSE or:
 * http://opensource.org/licenses/BSD-3-Clause
 */
"use strict";

// `URL` has been a global since Node 10 and this package requires Node >= 18,
// so there is no need to fall back to the 'url' core module. Requiring it would
// also break bundlers that no longer polyfill Node core modules (webpack >= 5).
module.exports = URL;

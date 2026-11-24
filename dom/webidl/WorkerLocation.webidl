/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://html.spec.whatwg.org/multipage/workers.html#worker-locations
 *
 * © Copyright 2004-2011 Apple Computer, Inc., Mozilla Foundation, and Opera
 * Software ASA.
 * You are granted a license to use, reproduce and create derivative works of
 * this document.
 */

[Exposed=Worker]
interface WorkerLocation {
  stringifier readonly attribute UTF8String href;
  readonly attribute UTF8String origin;
  readonly attribute UTF8String protocol;
  readonly attribute UTF8String host;
  readonly attribute UTF8String hostname;
  readonly attribute UTF8String port;
  readonly attribute UTF8String pathname;
  readonly attribute UTF8String search;
  readonly attribute UTF8String hash;
};

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* https://w3c.github.io/webtransport/#duplex-stream */

[Exposed=(Window,Worker), SecureContext]
interface WebTransportDatagramDuplexStream {
  [NewObject, Throws]
  WebTransportDatagramsWritable createWritable(
    optional WebTransportSendOptions options = {});
  readonly attribute ReadableStream readable;
  readonly attribute WritableStream writable;

  readonly attribute unsigned long maxDatagramSize;
  [Throws] attribute unrestricted double? incomingMaxAge;
  [Throws] attribute unrestricted double? outgoingMaxAge;
  attribute unsigned long incomingMaxBufferedDatagrams;
  attribute unsigned long outgoingMaxBufferedDatagrams;
};

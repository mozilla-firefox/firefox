/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/resource-timing/#sec-performanceresourcetiming
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

enum RenderBlockingStatusType { "blocking", "non-blocking" };

[Exposed=(Window,Worker)]
interface PerformanceResourceTiming : PerformanceEntry
{
  readonly attribute DOMString initiatorType;
  readonly attribute DOMString nextHopProtocol;

  readonly attribute DOMHighResTimeStamp workerStart;

  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp redirectStart;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp redirectEnd;

  readonly attribute DOMHighResTimeStamp fetchStart;

  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp domainLookupStart;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp domainLookupEnd;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp connectStart;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp connectEnd;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp secureConnectionStart;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp requestStart;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp responseStart;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp firstInterimResponseStart;
  [NeedsSubjectPrincipal]
  readonly attribute DOMHighResTimeStamp finalResponseHeadersStart;

  readonly attribute DOMHighResTimeStamp responseEnd;

  [NeedsSubjectPrincipal]
  readonly attribute unsigned long long transferSize;
  [NeedsSubjectPrincipal]
  readonly attribute unsigned long long encodedBodySize;
  [NeedsSubjectPrincipal]
  readonly attribute unsigned long long decodedBodySize;

  // https://w3c.github.io/resource-timing/#dom-performanceresourcetiming-responsestatus
  [NeedsSubjectPrincipal]
  readonly attribute unsigned short responseStatus;

  // https://w3c.github.io/server-timing/#extension-to-the-performanceresourcetiming-interface
  [NeedsSubjectPrincipal]
  readonly attribute DOMString contentType;

  // TODO: Use FrozenArray once available. (Bug 1236777)
  // readonly attribute FrozenArray<PerformanceServerTiming> serverTiming;
  [SecureContext, Frozen, Cached, Pure, NeedsSubjectPrincipal]
  readonly attribute sequence<PerformanceServerTiming> serverTiming;

  [Pref="dom.element.blocking.enabled"]
  readonly attribute RenderBlockingStatusType renderBlockingStatus;

  [Default] object toJSON();
};

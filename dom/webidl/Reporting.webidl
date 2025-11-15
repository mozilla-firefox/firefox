/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/reporting/#interface-reporting-observer
 */


[GenerateToJSON]
dictionary ReportBody {
};

dictionary Report {
  DOMString type;
  DOMString url;
  ReportBody body;
};

[Pref="dom.reporting.enabled",
 Exposed=(Window,Worker)]
interface ReportingObserver {
  [Throws]
  constructor(ReportingObserverCallback callback, optional ReportingObserverOptions options = {});
  undefined observe();
  undefined disconnect();
  ReportList takeRecords();
};

callback ReportingObserverCallback = undefined (sequence<Report> reports, ReportingObserver observer);

dictionary ReportingObserverOptions {
  sequence<DOMString> types;
  boolean buffered = false;
};

typedef sequence<Report> ReportList;

dictionary DeprecationReportBody : ReportBody {
  DOMString id;
  object? anticipatedRemoval;
  DOMString message;
  DOMString? sourceFile;
  unsigned long? lineNumber;
  unsigned long? columnNumber;
};

[Deprecated="DeprecatedTestingInterface",
 Pref="dom.reporting.testing.enabled",
 Exposed=(Window,DedicatedWorker)]
interface TestingDeprecatedInterface {
  constructor();

  [Deprecated="DeprecatedTestingMethod"]
  undefined deprecatedMethod();

  [Deprecated="DeprecatedTestingAttribute"]
  readonly attribute boolean deprecatedAttribute;
};

dictionary CSPViolationReportBody : ReportBody {
  USVString documentURL;
  USVString? referrer;
  USVString? blockedURL;
  DOMString effectiveDirective;
  DOMString originalPolicy;
  USVString? sourceFile;
  DOMString? sample;
  SecurityPolicyViolationEventDisposition disposition;
  unsigned short statusCode;
  unsigned long? lineNumber;
  unsigned long? columnNumber;
};

// Used internally to process the JSON
[GenerateInit]
dictionary ReportingHeaderValue {
  sequence<ReportingItem> items;
};

// Used internally to process the JSON
dictionary ReportingItem {
  // This is a long.
  any max_age;
  // This is a sequence of ReportingEndpoint.
  any endpoints;
  // This is a string. If missing, the value is 'default'.
  any group;
  boolean include_subdomains = false;
};

// Used internally to process the JSON
[GenerateInit]
dictionary ReportingEndpoint {
  // This is a string.
  any url;
  // This is an unsigned long.
  any priority;
  // This is an unsigned long.
  any weight;
};

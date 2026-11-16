/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

// Test-only interface to help writing tests for use-after-unlink bugs.
[Pref="dom.cc.unlink_test",
 Exposed=(Window,Worker)]
interface CycleCollectorUnlinkTester {
  constructor();

  readonly attribute boolean unlinked;

  undefined crashIfUnlinked();
};

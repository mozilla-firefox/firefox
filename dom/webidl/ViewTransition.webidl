/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/
 *
 * https://drafts.csswg.org/css-view-transitions-2/#the-domtransition-interface
 * https://drafts.csswg.org/css-view-transitions-2/#viewtransitiontypeset
 */

[Exposed=Window]
interface ViewTransitionTypeSet {
  setlike<DOMString>;
};

// Setlike methods that need to be overridden to keep the contents in sync with
// the C++ version, see bug 1799890 and bug 1822927.
partial interface ViewTransitionTypeSet {
  [Throws]
  ViewTransitionTypeSet add(DOMString type);
  [Throws]
  undefined clear();
  [Throws]
  boolean delete(DOMString type);
};

[Exposed=Window]
interface ViewTransition {
  [Throws] readonly attribute Promise<undefined> updateCallbackDone;
  [Throws] readonly attribute Promise<undefined> ready;
  [Throws] readonly attribute Promise<undefined> finished;
  undefined skipTransition();
  [SameObject] readonly attribute ViewTransitionTypeSet types;
};

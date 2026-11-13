/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://html.spec.whatwg.org/multipage/forms.html#the-dialog-element
 *
 * © Copyright 2004-2011 Apple Computer, Inc., Mozilla Foundation, and
 * Opera Software ASA. You are granted a license to use, reproduce
 * and create derivative works of this document.
 */

[Exposed=Window]
interface HTMLDialogElement : HTMLElement {
  [HTMLConstructor] constructor();

  [CEReactions, SetterThrows]
  attribute DOMString closedBy;

  [CEReactions, SetterThrows]
  attribute boolean open;
  attribute DOMString returnValue;
  [CEReactions, Throws, UseCounter]
  undefined show();
  [CEReactions, Throws]
  undefined showModal();
  [CEReactions]
  undefined close(optional DOMString returnValue);
  [CEReactions]
  undefined requestClose(optional DOMString returnValue);
};

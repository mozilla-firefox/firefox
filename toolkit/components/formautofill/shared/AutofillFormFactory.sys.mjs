/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A factory to generate AutofillForm objects that represent a set of autofillable fields
 * which aren't necessarily marked up with a <form> element.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FormLikeFactory: "resource://gre/modules/FormLikeFactory.sys.mjs",
});

export const AutofillFormFactory = {
  findRootForField(element) {
    let ignoreForm;
    try {
      const bc = element.ownerGlobal.browsingContext;
      ignoreForm = bc != bc.top;
    } catch {
      ignoreForm = false;
    }
    return lazy.FormLikeFactory.findRootForField(element, { ignoreForm });
  },

  createFromForm(aForm) {
    return lazy.FormLikeFactory.createFromForm(aForm);
  },

  createFromField(aField) {
    let ignoreForm;
    try {
      const bc = aField.ownerGlobal.browsingContext;
      ignoreForm = bc != bc.top;
    } catch {
      ignoreForm = false;
    }
    return lazy.FormLikeFactory.createFromField(aField, { ignoreForm });
  },

  createFromDocumentRoot(aDocRoot) {
    return lazy.FormLikeFactory.createFromDocumentRoot(aDocRoot);
  },
};

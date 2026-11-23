/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A factory to generate LoginForm objects that represent a set of login fields
 * which aren't necessarily marked up with a <form> element.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  FormLikeFactory: "resource://gre/modules/FormLikeFactory.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  LoginOriginUtils:
    "moz-src:///toolkit/components/passwordmgr/shared/LoginOriginUtils.sys.mjs",
  LoginHelperDOM:
    "moz-src:///toolkit/components/passwordmgr/shared/LoginHelperDOM.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  return lazy.LoginHelper.createLogger("LoginFormFactory");
});

export const LoginFormFactory = {
  /**
   * WeakMap of the root element of a LoginForm to the LoginForm representing its fields.
   *
   * This is used to be able to lookup an existing LoginForm for a given root element since multiple
   * calls to LoginFormFactory.createFrom* won't give the exact same object. When batching fills we don't always
   * want to use the most recent list of elements for a LoginForm since we may end up doing multiple
   * fills for the same set of elements when a field gets added between arming and running the
   * DeferredTask.
   *
   * @type {WeakMap}
   */
  _loginFormsByRootElement: new WeakMap(),

  /**
   * Maps all DOM content documents in this content process, including those in
   * frames, to a WeakSet of LoginForm.rootElement for the document.
   */
  _loginFormRootElementsByDocument: new WeakMap(),

  /**
   * Create a LoginForm object from a <form>.
   *
   * @param {HTMLFormElement} aForm
   * @return {LoginForm}
   * @throws Error if aForm isn't an HTMLFormElement
   */
  createFromForm(aForm) {
    let formLike = lazy.FormLikeFactory.createFromForm(aForm);
    formLike.action = lazy.LoginHelperDOM.getFormActionOrigin(aForm);

    this._addLoginFormToRootElementsSet(formLike);

    return formLike;
  },

  /**
   * Create a LoginForm object from an elememt that is the root of the document
   *
   * Currently all <input> not in a <form> are one LoginForm but this
   * shouldn't be relied upon as the heuristics may change to detect multiple
   * "forms" (e.g. registration and login) on one page with a <form>.
   *
   * @param {HTMLElement} aDocumentRoot
   * @return {LoginForm}
   * @throws Error if aDocumentRoot is null
   */
  createFromDocumentRoot(aDocumentRoot) {
    const formLike = lazy.FormLikeFactory.createFromDocumentRoot(aDocumentRoot);
    formLike.action = lazy.LoginOriginUtils.getLoginOrigin(
      aDocumentRoot.baseURI
    );

    lazy.log.debug(
      "Created non-form LoginForm for rootElement:",
      aDocumentRoot
    );

    this._addLoginFormToRootElementsSet(formLike);

    return formLike;
  },

  /**
   * Create a LoginForm object from a password or username field.
   *
   * If the field is in a <form>, construct the LoginForm from the form.
   * Otherwise, create a LoginForm with a rootElement (wrapper) according to
   * heuristics. Currently all <input> not in a <form> are one LoginForm but this
   * shouldn't be relied upon as the heuristics may change to detect multiple
   * "forms" (e.g. registration and login) on one page with a <form>.
   *
   * Note that two LoginForms created from the same field won't return the same LoginForm object.
   * Use the `rootElement` property on the LoginForm as a key instead.
   *
   * @param {HTMLInputElement} aField - a password or username field in a document
   * @return {LoginForm}
   * @throws Error if aField isn't a password or username field in a document
   */
  createFromField(aField) {
    if (
      !HTMLInputElement.isInstance(aField) ||
      (!aField.hasBeenTypePassword &&
        !lazy.LoginHelperDOM.isUsernameFieldType(aField)) ||
      !aField.ownerDocument
    ) {
      throw new Error(
        "createFromField requires a password or username field in a document"
      );
    }

    let form =
      aField.form ||
      lazy.FormLikeFactory.closestFormIgnoringShadowRoots(aField);
    if (form) {
      return this.createFromForm(form);
    } else if (aField.hasAttribute("form")) {
      lazy.log.debug(
        "createFromField: field has form attribute but no form: ",
        aField.getAttribute("form")
      );
    }

    let formLike = lazy.FormLikeFactory.createFromField(aField);
    formLike.action = lazy.LoginOriginUtils.getLoginOrigin(
      aField.ownerDocument.baseURI
    );
    lazy.log.debug(
      "Created non-form LoginForm for rootElement:",
      aField.ownerDocument.documentElement
    );

    this._addLoginFormToRootElementsSet(formLike);
    return formLike;
  },

  getRootElementsWeakSetForDocument(aDocument) {
    let rootElementsSet = this._loginFormRootElementsByDocument.get(aDocument);
    if (!rootElementsSet) {
      rootElementsSet = new WeakSet();
      this._loginFormRootElementsByDocument.set(aDocument, rootElementsSet);
    }
    return rootElementsSet;
  },

  getForRootElement(aRootElement) {
    return this._loginFormsByRootElement.get(aRootElement);
  },

  setForRootElement(aRootElement, aLoginForm) {
    return this._loginFormsByRootElement.set(aRootElement, aLoginForm);
  },

  _addLoginFormToRootElementsSet(formLike) {
    let rootElementsSet = this.getRootElementsWeakSetForDocument(
      formLike.ownerDocument
    );
    rootElementsSet.add(formLike.rootElement);
    lazy.log.debug(
      "adding",
      formLike.rootElement,
      "to root elements for",
      formLike.ownerDocument
    );

    this._loginFormsByRootElement.set(formLike.rootElement, formLike);
  },
};

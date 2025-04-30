/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
} = require("resource://devtools/client/shared/vendor/react.mjs");
const dom = require("resource://devtools/client/shared/vendor/react-dom-factories.js");
const PropTypes = require("resource://devtools/client/shared/vendor/react-prop-types.mjs");

/*
 * Response preview component
 * Display HTML content within a sandbox enabled iframe
 */
class HTMLPreview extends Component {
  static get propTypes() {
    return {
      responseContent: PropTypes.object.isRequired,
      responseHeaders: PropTypes.object,
      url: PropTypes.string.isRequired,
    };
  }

  componentDidMount() {
    const { container } = this.refs;
    const browser = container.ownerDocument.createXULElement("browser");
    this.browser = browser;
    browser.setAttribute("type", "content");
    browser.setAttribute("remote", "true");
    browser.setAttribute("maychangeremoteness", "true");
    browser.setAttribute("disableglobalhistory", "true");

    // Bug 1800916 allow interaction with the preview page until
    // we find a way to prevent navigation without preventing copy paste from it.
    //
    // browser.addEventListener("mousedown", e => e.preventDefault(), {
    //   capture: true,
    // });
    container.appendChild(browser);

    // browsingContext attribute is only available after the browser
    // is attached to the DOM Tree.
    browser.browsingContext.allowJavascript = false;

    this.#updatePreview();
  }

  componentDidUpdate() {
    this.#updatePreview();
  }

  componentWillUnmount() {
    this.browser.remove();
  }

  #updatePreview() {
    const { responseContent, responseHeaders, url } = this.props;
    const htmlBody = responseContent ? responseContent.content.text : "";
    const uri = Services.io.newURI(
      "data:text/html;charset=UTF-8," + encodeURIComponent(htmlBody)
    );

    let csp = null;
    const cspHeaders = responseHeaders?.headers.filter(
      e => e.name.toLowerCase() === "content-security-policy"
    );
    if (cspHeaders?.length) {
      // Merge multiple CSP headers with a comma.
      const merged = cspHeaders.map(e => e.value).join(",");
      csp = ChromeUtils.createCSPFromHeader(
        merged,
        new URL(url).URI,
        Services.scriptSecurityManager.createNullPrincipal({})
      );
    }

    const options = {
      csp,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    };
    this.browser.loadURI(uri, options);
  }

  render() {
    return dom.div({ className: "html-preview", ref: "container" });
  }
}

module.exports = HTMLPreview;

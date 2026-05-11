/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React, { useEffect, useRef } from "react";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";

const BROWSER_STYLES = [
  "height",
  "width",
  "border",
  "borderRadius",
  "flex",
  "margin",
  "padding",
];

export const EmbeddedBrowser = props => {
  // Conditionally render the component only if the environment supports XULElements (such as in Spotlight modals)
  return document.createXULElement && props.url ? (
    <EmbeddedBrowserInner {...props} />
  ) : null;
};

const EmbeddedBrowserInner = ({ url, style }) => {
  const ref = useRef(null);
  const browserRef = useRef(null);

  useEffect(() => {
    if (!ref.current || browserRef.current) {
      return;
    }

    const browserEl = document.createXULElement("browser");
    const remoteType = window.AWPredictRemoteType({
      browserEl,
      url,
    });
    const attributes = [
      ["disableglobalhistory", "true"],
      ["type", "content"],
      ["remote", "true"],
      ["maychangeremoteness", "true"],
      ["nodefaultsrc", "true"],
      ["remoteType", remoteType],
    ];
    attributes.forEach(([attr, val]) => browserEl.setAttribute(attr, val));
    browserRef.current = browserEl;

    ref.current.appendChild(browserEl);
    // Initialize the browser element only once when the component mounts. The
    // empty dependency array ensures this effect runs only on the first render.
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (browserRef.current) {
      browserRef.current.fixupAndLoadURIString(url, {
        triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
          {}
        ),
      });
    }
  }, [url]);

  useEffect(() => {
    if (browserRef.current && style) {
      const validStyles = MultiStageUtils.getValidStyle(style, BROWSER_STYLES);
      Object.keys(validStyles).forEach(key => {
        browserRef.current.style.setProperty(key, style[key]);
      });
    }
  }, [style]);

  return <div className="embedded-browser-container" ref={ref}></div>;
};

export default EmbeddedBrowser;

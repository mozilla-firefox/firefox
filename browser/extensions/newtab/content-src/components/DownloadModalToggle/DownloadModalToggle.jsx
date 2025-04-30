/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";

function DownloadModalToggle({ onClick, isActive }) {
  return (
    <button
      className={`mobile-download-promo ${isActive ? " is-active" : ""}`}
      onClick={onClick}
    >
      <div className="icon icon-device-phone"></div>
    </button>
  );
}

export { DownloadModalToggle };

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";

export const PersonalizedCard = ({ onDismiss }) => {
  const wavingFox =
    "chrome://newtab/content/data/content/assets/waving-fox.svg";

  return (
    <aside className="personalized-card-wrapper">
      <div className="personalized-card-dismiss">
        <moz-button
          type="icon ghost"
          iconSrc="chrome://global/skin/icons/close.svg"
          onClick={onDismiss}
          data-l10n-id="newtab-toast-dismiss-button"
        ></moz-button>
      </div>
      <div className="personalized-card-inner">
        <img src={wavingFox} alt="" />
        <h2>Personalized Just for You</h2>
        <p>
          We’re customizing your feed to show content that matters to you, while
          ensuring your privacy is always respected.
        </p>
        <moz-button type="primary" class="personalized-card-cta">
          Manage your settings
        </moz-button>
        <a href="https://www.mozilla.org/en-US/privacy/firefox/#notice">
          Learn how we protect and manage data
        </a>
      </div>
    </aside>
  );
};

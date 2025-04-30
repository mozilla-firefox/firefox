/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useEffect, useRef } from "react";

function ReportContentToast({ onDismissClick, onAnimationEnd }) {
  const mozMessageBarRef = useRef(null);

  useEffect(() => {
    const { current: mozMessageBarElement } = mozMessageBarRef;

    mozMessageBarElement.addEventListener(
      "message-bar:user-dismissed",
      onDismissClick,
      {
        once: true,
      }
    );

    return () => {
      mozMessageBarElement.removeEventListener(
        "message-bar:user-dismissed",
        onDismissClick
      );
    };
  }, [onDismissClick]);

  return (
    <moz-message-bar
      type="success"
      class="notification-feed-item"
      dismissable={true}
      data-l10n-id="newtab-toast-thanks-for-reporting"
      ref={mozMessageBarRef}
      onAnimationEnd={onAnimationEnd}
    ></moz-message-bar>
  );
}

export { ReportContentToast };

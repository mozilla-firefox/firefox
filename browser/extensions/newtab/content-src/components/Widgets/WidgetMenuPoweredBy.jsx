/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { WIDGET_SOURCES } from "common/WidgetsRegistry.mjs";

// Partner link for the top of a widget menu. Opens in a new tab and records
// `userAction`.
export const WidgetMenuPoweredBy = ({
  dispatch,
  url,
  l10nId,
  recordUserAction,
  userAction,
  onClick,
}) => {
  const handleClick = () => {
    dispatch(
      ac.OnlyToMain({
        type: at.OPEN_LINK,
        data: { url, where: "tab" },
      })
    );
    recordUserAction(userAction, { source: WIDGET_SOURCES.CONTEXT_MENU });
    onClick?.();
  };

  return <panel-item data-l10n-id={l10nId} onClick={handleClick} />;
};

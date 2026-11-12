/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionCreators as ac } from "../../common/Actions.mjs";
import { CUSTOMIZE_SUBPANELS } from "./constants.mjs";

export function getCustomizePanelTransitions(prevProps, props) {
  const panelOpened = Boolean(props.showing && !prevProps.showing);
  const subpanelOpened =
    props.showing &&
    props.activeSubpanel &&
    (panelOpened || prevProps.activeSubpanel !== props.activeSubpanel)
      ? props.activeSubpanel
      : null;
  return { panelOpened, subpanelOpened };
}

export function recordCustomizePanelTransitions(dispatch, prevProps, props) {
  const { panelOpened, subpanelOpened } = getCustomizePanelTransitions(
    prevProps,
    props
  );
  if (panelOpened) {
    const source = props.customizePanelSource;
    // The wallpaper feature highlight opens its category one update after the
    // panel shows, so its request stands in for the subpanel.
    const subpanel =
      props.activeSubpanel ||
      (props.customizePanelWallpaperCategory
        ? CUSTOMIZE_SUBPANELS.WALLPAPERS
        : null);
    dispatch(
      ac.UserEvent({
        event: "SHOW_PERSONALIZE",
        ...(source && { source }),
        ...(subpanel && { subpanel }),
      })
    );
  }
  if (subpanelOpened) {
    dispatch(
      ac.UserEvent({
        event: "SHOW_PERSONALIZE_SUBPANEL",
        source: subpanelOpened,
      })
    );
  }
}

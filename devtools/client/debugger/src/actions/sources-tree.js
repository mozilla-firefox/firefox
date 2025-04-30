/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import { getMainThread } from "../selectors/index";

export function setExpandedState(expanded) {
  return { type: "SET_EXPANDED_STATE", expanded };
}

export function focusItem(item) {
  return { type: "SET_FOCUSED_SOURCE_ITEM", item };
}

export function setProjectDirectoryRoot(
  newRootItemUniquePath,
  newName,
  newFullName
) {
  return ({ dispatch, getState }) => {
    dispatch({
      type: "SET_PROJECT_DIRECTORY_ROOT",
      uniquePath: newRootItemUniquePath,
      name: newName,
      fullName: newFullName,
      mainThread: getMainThread(getState()),
    });
  };
}

export function clearProjectDirectoryRoot() {
  return setProjectDirectoryRoot("", "", "");
}

export function setShowContentScripts(shouldShow) {
  return { type: "SHOW_CONTENT_SCRIPTS", shouldShow };
}

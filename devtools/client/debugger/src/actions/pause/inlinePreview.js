/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import {
  getSelectedFrameInlinePreviews,
  getSelectedLocation,
  getSelectedScope,
} from "../../selectors/index";
import { features } from "../../utils/prefs";
import { getEditor } from "../../utils/editor/index";
import { validateSelectedFrame } from "../../utils/context";

/**
 * Update the inline previews for the currently selected frame.
 */
export function generateInlinePreview(selectedFrame) {
  return async function (thunkArgs) {
    const { dispatch, getState } = thunkArgs;
    if (!features.inlinePreview) {
      return null;
    }

    // Avoid regenerating inline previews when we already have preview data
    if (getSelectedFrameInlinePreviews(getState())) {
      return null;
    }

    const scope = getSelectedScope(getState());
    if (!scope || !scope.bindings) {
      return null;
    }

    const allPreviews = await getPreviews(selectedFrame, scope, thunkArgs);
    // Sort previews by line and column so they're displayed in the right order in the editor
    allPreviews.sort((previewA, previewB) => {
      if (previewA.line < previewB.line) {
        return -1;
      }
      if (previewA.line > previewB.line) {
        return 1;
      }
      // If we have the same line number
      return previewA.column < previewB.column ? -1 : 1;
    });

    const previews = {};
    for (const preview of allPreviews) {
      const { line } = preview;
      if (!previews[line]) {
        previews[line] = [];
      }
      previews[line].push(preview);
    }

    validateSelectedFrame(getState(), selectedFrame);

    return dispatch({
      type: "ADD_INLINE_PREVIEW",
      selectedFrame,
      previews,
    });
  };
}
/**
 * Creates all the previews
 *
 * @param {object} selectedFrame
 * @param {object} scope - Scope from the platform
 * @param {object} thunkArgs
 * @returns
 */
async function getPreviews(selectedFrame, scope, thunkArgs) {
  const { client, getState } = thunkArgs;

  // It's important to use selectedLocation, because we don't know
  // if we'll be viewing the original or generated frame location
  const selectedLocation = getSelectedLocation(getState());
  if (!selectedLocation) {
    return [];
  }

  const editor = getEditor();
  if (editor.isWasm) {
    return [];
  }

  const allPreviews = [];
  const seenBindings = {};

  const bindingReferences = await editor.getBindingReferences(
    selectedLocation,
    scope
  );
  validateSelectedFrame(getState(), selectedFrame);

  for (const level in bindingReferences) {
    for (const name in bindingReferences[level]) {
      const valueActorID = bindingReferences[level][name].value?.actor;
      // Ignore any binding with the same value which has already been displayed.
      // This might occur if a variable gets hoisted and is available to the local and global scope.
      if (seenBindings[name] && seenBindings[name] == valueActorID) {
        continue;
      }
      const previews = await generatePreviewsForBinding(
        bindingReferences[level][name],
        selectedLocation.line,
        name,
        client,
        selectedFrame,
        getState
      );
      seenBindings[name] = valueActorID;
      allPreviews.push(...previews);
    }
  }
  return allPreviews;
}

/**
 * Generates the previews from the binding information
 *
 * @param {object} bindingData - Scope binding data from the AST about a particular variable/argument at a particular level in the scope.
 * @param {number} pausedOnLine - The current line we are paused on
 * @param {string} name - Name of binding from the platfom scopes
 * @param {object} client - Client object for loading properties
 * @param {object} selectedFrame - Frame used to get the expressions values
 * @param {Function} getState - Redux getState, used to check that the frame is still selected
 * @returns
 */
async function generatePreviewsForBinding(
  bindingData,
  pausedOnLine,
  name,
  client,
  selectedFrame,
  getState
) {
  if (!bindingData) {
    return [];
  }

  // Show a variable only once ( an object and it's child property are
  // counted as different )
  const identifiers = new Set();
  const previews = [];
  // We start from end as we want to show values besides variable
  // located nearest to the breakpoint
  for (let i = bindingData.refs.length - 1; i >= 0; i--) {
    const ref = bindingData.refs[i];
    // Lines in CM6 is 1-based
    const line = ref.start.line;
    const column = ref.start.column;
    // We don't want to render inline preview below the paused line
    if (line >= pausedOnLine) {
      continue;
    }

    if (bindingData.value == undefined) {
      continue;
    }

    validateSelectedFrame(getState(), selectedFrame);
    const { displayName, displayValue } = await getExpressionNameAndValue(
      name,
      bindingData.value,
      ref,
      client,
      selectedFrame.thread
    );

    // Variable with same name exists, display value of current or
    // closest to the current scope's variable
    if (identifiers.has(displayName)) {
      continue;
    }
    identifiers.add(displayName);

    previews.push({
      line,
      column,
      // This attribute helps distinguish pause from trace previews
      type: "paused",
      name: displayName,
      value: displayValue,
    });
  }
  return previews;
}

/**
 * Get the name and value details to be displayed in the inline preview
 *
 * @param {string} name - Binding name
 * @param {string} value - Binding value which is the Enviroment object actor form
 * @param {object} ref - Binding reference
 * @param {object} client - Client object for loading properties
 * @param {string} thread - Thread used to get the expression values
 * @returns
 */
async function getExpressionNameAndValue(name, value, ref, client, thread) {
  let displayName = name;
  let displayValue = value;
  // We want to show values of properties of objects only and not
  // function calls on other data types like someArr.forEach etc..
  let properties = null;
  if (value.actor && value.class === "Object") {
    properties = await client.loadObjectProperties(
      {
        name,
        path: name,
        contents: { value },
      },
      thread
    );
  }

  let { meta } = ref;
  // Presence of meta property means expression contains child property
  // reference eg: objName.propName
  while (meta) {
    // Only variables of type Object will have properties
    if (!properties) {
      displayName += `.${meta.property}`;
      // Initially properties will be an array, after that it will be an object
    } else if (displayValue === value) {
      const property = properties.find(prop => prop.name === meta.property);
      displayValue = property?.contents.value;
      displayName += `.${meta.property}`;
    } else if (displayValue?.preview?.ownProperties) {
      const { ownProperties } = displayValue.preview;
      Object.keys(ownProperties).forEach(prop => {
        if (prop === meta.property) {
          displayValue = ownProperties[prop].value;
          displayName += `.${meta.property}`;
        }
      });
    }
    meta = meta.parent;
  }

  return { displayName, displayValue };
}

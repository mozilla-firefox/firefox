/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { useEffect, useGlobals, useChannel } from "storybook/preview-api";
import {
  DIRECTIONS,
  DIRECTION_BY_STRATEGY,
  UPDATE_STRATEGY_EVENT,
  FLUENT_CHANGED,
} from "./constants.mjs";
import { provideFluent } from "../fluent-utils.mjs";

/**
 * withPseudoLocalization is a Storybook decorator that handles emitting an
 * event to update translations when a new pseudo localization strategy is
 * applied. It also handles setting a "dir" attribute on the root element in the
 * Storybook iframe.
 *
 * @param {Function} StoryFn - Provided by Storybook, used to render the story.
 * @param {object} context - Provided by Storybook, data about the story.
 * @returns {Function} StoryFn with a modified "dir" attr set.
 */
export const withPseudoLocalization = (StoryFn, context) => {
  const [{ pseudoStrategy }] = useGlobals();
  const direction = DIRECTION_BY_STRATEGY[pseudoStrategy] || DIRECTIONS.ltr;
  const isInDocs = context.viewMode === "docs";
  const emit = useChannel({});

  useEffect(() => {
    if (pseudoStrategy) {
      emit(UPDATE_STRATEGY_EVENT, pseudoStrategy);
    }
  }, [pseudoStrategy]);

  useEffect(() => {
    if (isInDocs) {
      document.documentElement.setAttribute("dir", DIRECTIONS.ltr);
      let storyElements = document.querySelectorAll(".docs-story");
      storyElements.forEach(element => element.setAttribute("dir", direction));
    } else {
      document.documentElement.setAttribute("dir", direction);
    }
  }, [direction, isInDocs]);

  return StoryFn();
};

/**
 * withFluentStrings is a Storybook decorator that handles emitting an
 * event to update the Fluent strings shown in the Fluent panel.
 *
 * @param {Function} StoryFn - Provided by Storybook, used to render the story.
 * @param {object} context - Provided by Storybook, data about the story.
 * @returns {Function} StoryFn unmodified.
 */
export const withFluentStrings = (StoryFn, context) => {
  const [{ fluentStrings }, updateGlobals] = useGlobals();
  const emit = useChannel({});
  const fileName = context.component + ".ftl";
  let strings = [];

  if (context.parameters?.fluent && fileName) {
    if (fluentStrings.hasOwnProperty(fileName)) {
      strings = fluentStrings[fileName];
    } else {
      let resource = provideFluent(context.parameters.fluent, fileName);
      for (let message of resource.body) {
        strings.push([
          message.id,
          [
            message.value,
            ...Object.entries(message.attributes).map(
              ([key, value]) => `  .${key} = ${value}`
            ),
          ].join("\n"),
        ]);
      }
      updateGlobals({
        fluentStrings: { ...fluentStrings, [fileName]: strings },
      });
    }
  }

  emit(FLUENT_CHANGED, strings, fileName);

  return StoryFn();
};

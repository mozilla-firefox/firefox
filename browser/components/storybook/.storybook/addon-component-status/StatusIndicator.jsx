/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React from "react";
import { useParameter } from "storybook/manager-api";
import {
  // eslint-disable-next-line no-unused-vars
  Badge,
  // eslint-disable-next-line no-unused-vars
  WithTooltip,
  // eslint-disable-next-line no-unused-vars
  TooltipMessage,
  // eslint-disable-next-line no-unused-vars
  IconButton,
} from "storybook/internal/components";
import { TOOL_ID, STATUS_PARAM_KEY } from "./constants.mjs";

const VALID_STATUS_MAP = {
  stable: {
    label: "Stable",
    badgeType: "positive",
    description:
      "This component is widely used in Firefox, in both the chrome and in-content pages.",
  },
  "in-development": {
    label: "In Development",
    badgeType: "warning",
    description:
      "This component is in active development and starting to be used in Firefox. It may not yet be usable in both the chrome and in-content pages.",
  },
  unstable: {
    label: "Unstable",
    badgeType: "negative",
    description:
      "This component is still in the early stages of development and may not be ready for general use in Firefox.",
  },
};

/**
 * Displays a badge with the components status in the Storybook toolbar.
 * 
 * Statuses are set via story parameters.
 * We support either passing `status: "statusType"` for using defaults or
 * `status: {
      type: "stable" | "in-development" | "unstable",
      description: "Your description here"
      links: [
        {
          title: "Link title",
          href: "www.example.com",
        },
      ],
    }`
  * when we want to customize the description or add links.
 */
export const StatusIndicator = () => {
  let componentStatus = useParameter(STATUS_PARAM_KEY, null);
  let statusData = VALID_STATUS_MAP[componentStatus?.type ?? componentStatus];

  if (!componentStatus || !statusData) {
    return "";
  }

  // The tooltip message is added/removed from the DOM when visibility changes.
  // We need to update the aira-describedby button relationship accordingly.
  let onVisibilityChange = isVisible => {
    let button = document.getElementById("statusButton");
    if (isVisible) {
      button.setAttribute("aria-describedby", "statusMessage");
    } else {
      button.removeAttribute("aria-describedby");
    }
  };

  let description = componentStatus.description || statusData.description;
  let links = componentStatus.links || [];

  return (
    <WithTooltip
      key={TOOL_ID}
      placement="top"
      trigger="click"
      style={{
        display: "flex",
      }}
      onVisibleChange={onVisibilityChange}
      tooltip={() => (
        <div id="statusMessage">
          <TooltipMessage
            title={statusData.label}
            desc={description}
            links={links}
          />
        </div>
      )}
    >
      <IconButton
        id="statusButton"
        title={`Component status: ${statusData.label}`}
      >
        <Badge
          status={statusData.badgeType}
          style={{
            boxShadow: "currentColor 0 0 0 1px inset",
          }}
        >
          {statusData.label}
        </Badge>
      </IconButton>
    </WithTooltip>
  );
};

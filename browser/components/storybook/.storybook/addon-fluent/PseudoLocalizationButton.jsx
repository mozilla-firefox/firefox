/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React from "react";
import { useGlobals } from "storybook/manager-api";
import {
  // eslint-disable-next-line no-unused-vars
  IconButton,
  // eslint-disable-next-line no-unused-vars
  WithTooltip,
  // eslint-disable-next-line no-unused-vars
  TooltipLinkList,
} from "storybook/internal/components";
// eslint-disable-next-line no-unused-vars
import { TransferIcon } from "@storybook/icons";
import { TOOL_ID, STRATEGY_DEFAULT, PSEUDO_STRATEGIES } from "./constants.mjs";

// React component for a button + tooltip that gets added to the Storybook toolbar.
export const PseudoLocalizationButton = () => {
  const [{ pseudoStrategy = STRATEGY_DEFAULT }, updateGlobals] = useGlobals();

  const updatePseudoStrategy = strategy => {
    updateGlobals({ pseudoStrategy: strategy });
  };

  const getTooltipLinks = ({ onHide }) => {
    return PSEUDO_STRATEGIES.map(strategy => ({
      id: strategy,
      title: strategy.charAt(0).toUpperCase() + strategy.slice(1),
      onClick: () => {
        updatePseudoStrategy(strategy);
        onHide();
      },
      active: pseudoStrategy === strategy,
    }));
  };

  return (
    <WithTooltip
      placement="top"
      trigger="click"
      tooltip={props => <TooltipLinkList links={getTooltipLinks(props)} />}
    >
      <IconButton
        key={TOOL_ID}
        active={pseudoStrategy && pseudoStrategy !== STRATEGY_DEFAULT}
        title="Apply pseudo localization"
      >
        <TransferIcon />
      </IconButton>
    </WithTooltip>
  );
};

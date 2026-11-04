/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useEffect, useRef, useState } from "react";
import {
  CARD_STACK_TRANSITION_OUT_TIME,
  MultiStageAboutWelcome,
} from "./MultiStageAboutWelcome";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";

export function MultistageWithDismiss({ config, handleDismiss, handleBlock }) {
  function onDismiss() {
    handleBlock?.();
    handleDismiss?.();
  }

  // The card-stack template has its own inline dismiss button, so it doesn't
  // need the corner one.
  const isCardStack = config.screens?.[0]?.content?.position === "card-stack";
  const transitions = config.transitions ?? false;
  const animateCardStack = isCardStack && transitions;
  const [isExiting, setIsExiting] = useState(false);
  const exitTimeout = useRef(null);

  useEffect(() => {
    if (!animateCardStack) {
      return undefined;
    }
    const finish = window.AWFinish;
    window.AWFinish = () => {
      if (exitTimeout.current) {
        return;
      }
      setIsExiting(true);
      exitTimeout.current = setTimeout(finish, CARD_STACK_TRANSITION_OUT_TIME);
    };
    return () => {
      window.AWFinish = finish;
      clearTimeout(exitTimeout.current);
      exitTimeout.current = null;
    };
  }, [animateCardStack]);

  const wrapperClasses = ["multistage-newtab-wrapper"];
  if (animateCardStack) {
    wrapperClasses.push("card-stack-animated");
  }
  if (isExiting) {
    wrapperClasses.push("card-stack-exiting");
  }

  return (
    <div
      className={wrapperClasses.join(" ")}
      style={
        config.wrapper_content_style
          ? MultiStageUtils.getValidStyle(config.wrapper_content_style, [
              "height",
            ])
          : { height: "500px" }
      }
    >
      {isCardStack ? null : (
        <moz-button
          type="icon ghost"
          size="small"
          iconsrc="chrome://global/skin/icons/close.svg"
          data-l10n-id="newtab-activation-window-message-dismiss-button"
          onClick={onDismiss}
        />
      )}
      <MultiStageAboutWelcome
        defaultScreens={config.screens}
        message_id={config.id}
        transitions={transitions}
        backdrop={config.backdrop}
        ariaRole={"dialog"}
        startScreen={0}
        updateHistory={false}
      />
    </div>
  );
}

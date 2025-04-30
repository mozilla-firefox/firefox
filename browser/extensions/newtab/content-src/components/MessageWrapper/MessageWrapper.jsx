/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useCallback, useEffect, useState } from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { useSelector } from "react-redux";
import { useIntersectionObserver } from "../../lib/utils";

function MessageWrapper({ children, dispatch, hiddenOverride, onDismiss }) {
  const message = useSelector(state => state.Messages);
  const [isIntersecting, setIsIntersecting] = useState(false);
  const [hasRun, setHasRun] = useState();

  const handleIntersection = useCallback(() => {
    setIsIntersecting(true);
    const isVisible =
      document?.visibilityState && document.visibilityState === "visible";
    // only send impression if messageId is defined and tab is visible
    if (isVisible && message.messageData.id) {
      setHasRun(true);
      dispatch(
        ac.AlsoToMain({
          type: at.MESSAGE_IMPRESSION,
          data: message.messageData,
        })
      );
    }
  }, [dispatch, message]);

  useEffect(() => {
    const handleVisibilityChange = () => {
      if (document.visibilityState === "visible" && !hasRun) {
        handleIntersection();
      }
    };

    document.addEventListener("visibilitychange", handleVisibilityChange);
    return () => {
      document.removeEventListener("visibilitychange", handleVisibilityChange);
    };
  }, [handleIntersection, hasRun]);

  const ref = useIntersectionObserver(handleIntersection);

  const handleClose = useCallback(() => {
    const action = {
      type: at.MESSAGE_TOGGLE_VISIBILITY,
      data: true,
    };
    if (message.portID) {
      dispatch(ac.OnlyToOneContent(action, message.portID));
    } else {
      dispatch(ac.AlsoToMain(action));
    }
    onDismiss();
  }, [dispatch, message, onDismiss]);

  function handleDismiss() {
    const { id } = message.messageData;
    if (id) {
      dispatch(
        ac.OnlyToMain({
          type: at.MESSAGE_DISMISS,
          data: { message: message.messageData },
        })
      );
    }
    handleClose();
  }

  function handleBlock() {
    const { id } = message.messageData;
    if (id) {
      dispatch(
        ac.OnlyToMain({
          type: at.MESSAGE_BLOCK,
          data: id,
        })
      );
    }
  }

  function handleClick(elementId) {
    const { id } = message.messageData;
    if (id) {
      dispatch(
        ac.OnlyToMain({
          type: at.MESSAGE_CLICK,
          data: { message: message.messageData, source: elementId || "" },
        })
      );
    }
  }

  // only display the message if `isHidden` is false
  return (
    (!message.isHidden || hiddenOverride) && (
      <div
        ref={el => {
          ref.current = [el];
        }}
        className="message-wrapper"
      >
        {React.cloneElement(children, {
          isIntersecting,
          handleDismiss,
          handleClick,
          handleBlock,
          handleClose,
        })}
      </div>
    )
  );
}

export { MessageWrapper };

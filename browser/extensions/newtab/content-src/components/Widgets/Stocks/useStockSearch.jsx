/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { useCallback, useLayoutEffect, useRef, useState } from "react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";

// Monotonic across every Stocks instance so a widget that unmounts and remounts
// never reuses a request id that an earlier, still-running request could answer.
let searchRequestSeq = 0;

/**
 * Owns the ticker-search panel lifecycle: whether it is open, sending a search,
 * and returning focus when it closes. The watchlist mutation on add stays in the
 * parent; this hook only opens, closes, and submits.
 *
 * @param {object} options
 * @param {Function} options.dispatch The store's dispatch.
 * @param {Function} options.recordUserAction Telemetry recorder from useWidgetTelemetry.
 * @param {object} options.menuButtonRef Ref to the widget menu button, refocused on close.
 * @returns {{ active: boolean, open: () => void, close: () => void, submit: (query: string) => void }}
 */
export function useStockSearch({ dispatch, recordUserAction, menuButtonRef }) {
  const [active, setActive] = useState(false);
  const wasActiveRef = useRef(false);

  // Once search closes, return focus to the widget menu button.
  useLayoutEffect(() => {
    if (active) {
      wasActiveRef.current = true;
    } else if (wasActiveRef.current) {
      wasActiveRef.current = false;
      menuButtonRef.current?.focus();
    }
  }, [active, menuButtonRef]);

  const openPanel = useCallback(() => {
    // Start clean so results from a previous search never flash on reopen.
    dispatch({ type: at.WIDGETS_STOCKS_SEARCH_CLEAR });
    setActive(true);
  }, [dispatch]);

  const closePanel = useCallback(() => {
    setActive(false);
    dispatch({ type: at.WIDGETS_STOCKS_SEARCH_CLEAR });
  }, [dispatch]);

  const submit = useCallback(
    query => {
      const trimmed = query.trim();
      if (!trimmed) {
        return;
      }
      const requestId = String(++searchRequestSeq);
      dispatch({
        type: at.WIDGETS_STOCKS_SEARCH_STARTED,
        data: { requestId, query: trimmed },
      });
      dispatch(
        ac.OnlyToMain({
          type: at.WIDGETS_STOCKS_SEARCH_REQUEST,
          data: { requestId, query: trimmed },
        })
      );
      recordUserAction("search_submit", { source: "search" });
    },
    [dispatch, recordUserAction]
  );

  return { active, open: openPanel, close: closePanel, submit };
}

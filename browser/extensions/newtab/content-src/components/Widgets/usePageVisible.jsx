/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { useEffect, useState } from "react";

// Tracks page visibility. New tabs are preloaded and render while hidden, so a
// one-shot effect on mount would spend itself before the user could see it.
export const usePageVisible = () => {
  const [isPageVisible, setIsPageVisible] = useState(
    typeof document === "undefined" || document.visibilityState === "visible"
  );

  useEffect(() => {
    const onVisibility = () =>
      setIsPageVisible(document.visibilityState === "visible");
    document.addEventListener("visibilitychange", onVisibility);
    // The page may have changed visibility between render and this effect.
    onVisibility();
    return () => document.removeEventListener("visibilitychange", onVisibility);
  }, []);

  return isPageVisible;
};

// Returns whether the page has been visible since this component mounted.
export const usePageShown = () => {
  const isPageVisible = usePageVisible();
  const [wasShown, setWasShown] = useState(isPageVisible);
  if (isPageVisible && !wasShown) {
    setWasShown(true);
  }
  return wasShown;
};

// Returns src once the page has been visible, and undefined until then. A
// framed widget's document reads its state (a solved crossword, a game in
// progress) when it loads, and nothing tells it about changes after that. New
// tabs are preloaded, so a document loaded then would show the state from when
// the tab was preloaded rather than when the user opened it. Waiting until the
// page is shown also keeps a preloaded tab the user never opens from loading
// the document and reporting its events.
export const useSrcAfterPageShown = src => (usePageShown() ? src : undefined);

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { render } from "@testing-library/react";
import { useLayoutEffect } from "react";
import { usePageVisible } from "content-src/components/Widgets/usePageVisible";

const setVisibility = value => {
  Object.defineProperty(document, "visibilityState", {
    configurable: true,
    value,
  });
  document.dispatchEvent(new Event("visibilitychange"));
};

describe("usePageVisible", () => {
  afterEach(() => setVisibility("visible"));

  it("catches a change between render and its effect", () => {
    setVisibility("hidden");
    let visible;
    function Probe() {
      visible = usePageVisible();
      // Layout effects run before the hook's effect adds its listener.
      useLayoutEffect(() => setVisibility("visible"), []);
      return null;
    }
    const { unmount } = render(<Probe />);
    expect(visible).toBe(true);
    unmount();
  });
});

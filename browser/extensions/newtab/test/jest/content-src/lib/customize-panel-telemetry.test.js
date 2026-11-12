/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { readFileSync } from "fs";
import path from "path";
import { actionCreators as ac } from "common/Actions.mjs";
import {
  CUSTOMIZE_PANEL_SOURCES,
  CUSTOMIZE_SUBPANELS,
} from "content-src/lib/constants";
import {
  getCustomizePanelTransitions,
  recordCustomizePanelTransitions,
} from "content-src/lib/customize-panel-telemetry";

const METRICS_YAML = readFileSync(
  path.resolve(__dirname, "../../../../../../components/newtab/metrics.yaml"),
  "utf8"
);

// The value lists in these metrics.yaml descriptions are the "- value" lines
// that follow the sentence naming this test, up to the first other line.
function documentedValues(metric, extra) {
  const [, afterMetric] = METRICS_YAML.split(new RegExp(`^  ${metric}:$`, "m"));
  const [block] = afterMetric.split(/^ {2}\S/m);
  const [, afterExtra] = block.split(new RegExp(`^      ${extra}:$`, "m"));
  const [extraBlock] = afterExtra.split(/^\s+type:/m);
  const [, list] = extraBlock.match(
    /customize-panel-telemetry\.test\.js[^:]*:\n((?:\s*- [a-z_]+\n)+)/
  );
  return list.match(/[a-z_]+/g).sort();
}

describe("customize-panel-telemetry", () => {
  describe("metrics.yaml", () => {
    it("documents every customize panel source on customize_panel_open", () => {
      expect(documentedValues("customize_panel_open", "source")).toEqual(
        Object.values(CUSTOMIZE_PANEL_SOURCES).sort()
      );
    });

    it("documents every subpanel on customize_panel_open", () => {
      expect(documentedValues("customize_panel_open", "subpanel")).toEqual(
        Object.values(CUSTOMIZE_SUBPANELS).sort()
      );
    });

    it("documents every subpanel on customize_panel_subpanel_open", () => {
      expect(
        documentedValues("customize_panel_subpanel_open", "panel")
      ).toEqual(Object.values(CUSTOMIZE_SUBPANELS).sort());
    });
  });

  describe("getCustomizePanelTransitions", () => {
    const OPEN = { showing: true, activeSubpanel: null };
    const CLOSED = { showing: false, activeSubpanel: null };

    it("reports a panel open when showing turns on", () => {
      expect(getCustomizePanelTransitions(CLOSED, OPEN)).toEqual({
        panelOpened: true,
        subpanelOpened: null,
      });
    });

    it("reports a subpanel open when one becomes active while showing", () => {
      expect(
        getCustomizePanelTransitions(OPEN, {
          ...OPEN,
          activeSubpanel: CUSTOMIZE_SUBPANELS.THEMES,
        })
      ).toEqual({
        panelOpened: false,
        subpanelOpened: CUSTOMIZE_SUBPANELS.THEMES,
      });
    });

    it("reports both when the panel opens straight into a subpanel", () => {
      expect(
        getCustomizePanelTransitions(CLOSED, {
          showing: true,
          activeSubpanel: CUSTOMIZE_SUBPANELS.WIDGETS,
        })
      ).toEqual({
        panelOpened: true,
        subpanelOpened: CUSTOMIZE_SUBPANELS.WIDGETS,
      });
    });

    it("reports a switch from one subpanel to another as an open of the new one", () => {
      expect(
        getCustomizePanelTransitions(
          { ...OPEN, activeSubpanel: CUSTOMIZE_SUBPANELS.THEMES },
          { ...OPEN, activeSubpanel: CUSTOMIZE_SUBPANELS.WALLPAPERS }
        )
      ).toEqual({
        panelOpened: false,
        subpanelOpened: CUSTOMIZE_SUBPANELS.WALLPAPERS,
      });
    });

    it("reports nothing when a subpanel changes while the panel is hidden", () => {
      expect(
        getCustomizePanelTransitions(CLOSED, {
          ...CLOSED,
          activeSubpanel: CUSTOMIZE_SUBPANELS.THEMES,
        })
      ).toEqual({ panelOpened: false, subpanelOpened: null });
    });

    it("reports nothing on a re-render with the same subpanel open", () => {
      const themesOpen = {
        ...OPEN,
        activeSubpanel: CUSTOMIZE_SUBPANELS.THEMES,
      };
      expect(getCustomizePanelTransitions(themesOpen, themesOpen)).toEqual({
        panelOpened: false,
        subpanelOpened: null,
      });
    });

    it("reports nothing on a re-render, on close, or on back to the root", () => {
      expect(getCustomizePanelTransitions(OPEN, OPEN)).toEqual({
        panelOpened: false,
        subpanelOpened: null,
      });
      expect(getCustomizePanelTransitions(OPEN, CLOSED)).toEqual({
        panelOpened: false,
        subpanelOpened: null,
      });
      expect(
        getCustomizePanelTransitions(
          { ...OPEN, activeSubpanel: CUSTOMIZE_SUBPANELS.THEMES },
          OPEN
        )
      ).toEqual({ panelOpened: false, subpanelOpened: null });
    });

    it("reports a panel open and its subpanel when the panel reopens before the subpanel resets", () => {
      expect(
        getCustomizePanelTransitions(
          { showing: false, activeSubpanel: CUSTOMIZE_SUBPANELS.THEMES },
          { showing: true, activeSubpanel: CUSTOMIZE_SUBPANELS.THEMES }
        )
      ).toEqual({
        panelOpened: true,
        subpanelOpened: CUSTOMIZE_SUBPANELS.THEMES,
      });
    });
  });

  describe("recordCustomizePanelTransitions", () => {
    it("dispatches the matching user events", () => {
      const dispatch = jest.fn();
      recordCustomizePanelTransitions(
        dispatch,
        { showing: false, activeSubpanel: null },
        { showing: true, activeSubpanel: CUSTOMIZE_SUBPANELS.WIDGETS }
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({
          event: "SHOW_PERSONALIZE",
          subpanel: CUSTOMIZE_SUBPANELS.WIDGETS,
        })
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({
          event: "SHOW_PERSONALIZE_SUBPANEL",
          source: CUSTOMIZE_SUBPANELS.WIDGETS,
        })
      );
    });

    it("adds the source and the active subpanel to the panel open", () => {
      const dispatch = jest.fn();
      recordCustomizePanelTransitions(
        dispatch,
        { showing: false, activeSubpanel: null },
        {
          showing: true,
          activeSubpanel: CUSTOMIZE_SUBPANELS.WIDGETS,
          customizePanelSource: CUSTOMIZE_PANEL_SOURCES.MANAGE_WIDGETS,
        }
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({
          event: "SHOW_PERSONALIZE",
          source: "manage_widgets",
          subpanel: "widgets_management",
        })
      );
    });

    it("leaves subpanel out when the panel opens on its root", () => {
      const dispatch = jest.fn();
      recordCustomizePanelTransitions(
        dispatch,
        { showing: false, activeSubpanel: null },
        {
          showing: true,
          activeSubpanel: null,
          customizePanelSource: CUSTOMIZE_PANEL_SOURCES.CUSTOMIZE_BUTTON,
        }
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({ event: "SHOW_PERSONALIZE", source: "customize_button" })
      );
    });

    it("records wallpaper_categories for a requested wallpaper category", () => {
      const dispatch = jest.fn();
      recordCustomizePanelTransitions(
        dispatch,
        { showing: false, activeSubpanel: null },
        {
          showing: true,
          activeSubpanel: null,
          customizePanelSource:
            CUSTOMIZE_PANEL_SOURCES.WALLPAPER_FEATURE_HIGHLIGHT,
          customizePanelWallpaperCategory: "celestial",
        }
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({
          event: "SHOW_PERSONALIZE",
          source: "wallpaper_feature_highlight",
          subpanel: "wallpaper_categories",
        })
      );
    });

    it("prefers the active subpanel over a requested wallpaper category", () => {
      const dispatch = jest.fn();
      recordCustomizePanelTransitions(
        dispatch,
        { showing: false, activeSubpanel: null },
        {
          showing: true,
          activeSubpanel: CUSTOMIZE_SUBPANELS.THEMES,
          customizePanelWallpaperCategory: "celestial",
        }
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({
          event: "SHOW_PERSONALIZE",
          subpanel: "themes_management",
        })
      );
    });

    it("uses the wallpaper request only on a panel open", () => {
      const dispatch = jest.fn();
      const open = { showing: true, activeSubpanel: null };
      recordCustomizePanelTransitions(dispatch, open, {
        ...open,
        customizePanelWallpaperCategory: "celestial",
      });
      expect(dispatch).not.toHaveBeenCalled();
    });

    it("records nothing for a second open with a new source while showing", () => {
      const dispatch = jest.fn();
      const open = { showing: true, activeSubpanel: null };
      recordCustomizePanelTransitions(
        dispatch,
        { ...open, customizePanelSource: "customize_button" },
        { ...open, customizePanelSource: "promo_card" }
      );
      expect(dispatch).not.toHaveBeenCalled();
    });

    it("uses the current source after a close and reopen", () => {
      const dispatch = jest.fn();
      recordCustomizePanelTransitions(
        dispatch,
        { showing: false, customizePanelSource: null },
        { showing: true, customizePanelSource: "promo_card" }
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({ event: "SHOW_PERSONALIZE", source: "promo_card" })
      );
    });

    it("dispatches nothing without a transition", () => {
      const dispatch = jest.fn();
      const props = { showing: true, activeSubpanel: null };
      recordCustomizePanelTransitions(dispatch, props, props);
      expect(dispatch).not.toHaveBeenCalled();
    });
  });
});

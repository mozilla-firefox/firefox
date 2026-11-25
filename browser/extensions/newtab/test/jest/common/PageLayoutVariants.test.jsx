/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  DEFAULT_PAGE_LAYOUT_VARIANT,
  PAGE_LAYOUT_VARIANTS,
  SIDE_BY_SIDE_PAGE_LAYOUTS,
  SPACES_PAGE_LAYOUTS,
  hasSpacesArrows,
  isSideBySideActive,
  isSideBySideAssigned,
  isSpacesActive,
  isSpacesAssigned,
  isSpacesThematicAssigned,
  isAutoMinimizeWidgetsAssigned,
  isSpacesPreviewEnabled,
  resolveFeatureSpacePreviewImages,
  resolveAutoMinimizeDelayMs,
  resolvePageLayoutVariant,
  resolvePopulatedSpaces,
  selectFirstSlotWidget,
  selectFirstSlotWidgetIds,
  resolveFirstSlotWidgets,
  resolveFirstSlotWidgetsBySection,
  resolveThematicActivitySpace,
  resolveThematicDefaultSpace,
  resolveThematicSpaceSections,
  normalizeSpacesConfig,
  resolveSpacesConfigFrom,
  resolveThematicSpacesConfig,
  resolveThematicSpaceWidgets,
  sideBySideBandClasses,
  spacesBandClasses,
  isWidgetPlacementActive,
  resolveHomeWidgetIds,
  resolveWidgetPlacement,
  WIDGET_PLACEMENTS,
} from "common/PageLayoutVariants.mjs";
import {
  PREF_HOME_WIDGETS,
  resolveWidgetSize,
  WIDGET_REGISTRY,
} from "common/WidgetsRegistry.mjs";

// The gates isSideBySideActive checks besides the variant itself.
const STORIES_ON = {
  "feeds.section.topstories": true,
  "feeds.system.topstories": true,
};
// The container being on is not enough: something has to actually be in it.
const WIDGETS_ON = {
  region: "US",
  "widgets.system.enabled": true,
  "widgets.enabled": true,
  "widgets.system.lists.enabled": true,
  "widgets.lists.enabled": true,
};

// isSpacesActive derives the section keys itself, so these need the shape the
// feed actually delivers rather than a bare list.
const feedState = (...sectionKeys) => ({
  feeds: {
    data: {
      "https://example.com/feed": {
        data: { sections: sectionKeys.map(sectionKey => ({ sectionKey })) },
      },
    },
  },
});

const sideBySide = {
  "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_CONTENT_LEAD,
  ...STORIES_ON,
  ...WIDGETS_ON,
};

describe("resolvePageLayoutVariant", () => {
  it("defaults to the full-width layout when nothing is set", () => {
    expect(resolvePageLayoutVariant({})).toBe(DEFAULT_PAGE_LAYOUT_VARIANT);
    expect(DEFAULT_PAGE_LAYOUT_VARIANT).toBe("nova-full-width");
  });

  it("returns the pref value", () => {
    expect(
      resolvePageLayoutVariant({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_WIDGETS_LEAD,
      })
    ).toBe("side-by-side-widgets-lead");
  });

  it("lets trainhopConfig override a non-default pref value", () => {
    expect(
      resolvePageLayoutVariant({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_CONTENT_LEAD,
        trainhopConfig: {
          pageLayouts: {
            variant: PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_WIDGETS_LEAD,
          },
        },
      })
    ).toBe("side-by-side-widgets-lead");
  });

  it("falls back to the pref for a missing or non-string trainhop value", () => {
    const pref = {
      "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_CONTENT_LEAD,
    };
    expect(resolvePageLayoutVariant({ ...pref, trainhopConfig: {} })).toBe(
      "side-by-side-content-lead"
    );
    expect(
      resolvePageLayoutVariant({ ...pref, trainhopConfig: { pageLayouts: {} } })
    ).toBe("side-by-side-content-lead");
    expect(
      resolvePageLayoutVariant({
        ...pref,
        trainhopConfig: { pageLayouts: { variant: true } },
      })
    ).toBe("side-by-side-content-lead");
    expect(
      resolvePageLayoutVariant({
        ...pref,
        trainhopConfig: { pageLayouts: { variant: "" } },
      })
    ).toBe("side-by-side-content-lead");
  });
});

describe("isSideBySideAssigned", () => {
  it("is true for every side-by-side variant regardless of the sections", () => {
    expect(SIDE_BY_SIDE_PAGE_LAYOUTS).toHaveLength(4);
    for (const variant of SIDE_BY_SIDE_PAGE_LAYOUTS) {
      expect(isSideBySideAssigned({ "pageLayouts.variant": variant })).toBe(
        true
      );
    }
  });

  // This is the difference from isSideBySideActive: the panels follow the
  // assignment, so a lone section is still framed.
  it("stays true where isSideBySideActive is false", () => {
    const noWidgets = { ...sideBySide, "widgets.lists.enabled": false };
    expect(isSideBySideAssigned(noWidgets)).toBe(true);
    expect(isSideBySideActive(noWidgets)).toBe(false);
  });

  it("is false for the default layout", () => {
    expect(isSideBySideAssigned({})).toBe(false);
    expect(
      isSideBySideAssigned({ "pageLayouts.variant": "nova-full-width" })
    ).toBe(false);
  });

  it("follows a variant set through trainhopConfig", () => {
    expect(
      isSideBySideAssigned({
        trainhopConfig: {
          pageLayouts: {
            variant: PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_WIDGETS_LEAD,
          },
        },
      })
    ).toBe(true);
  });
});

describe("isSideBySideActive", () => {
  it("is true for every side-by-side variant when stories and widgets are on", () => {
    for (const variant of SIDE_BY_SIDE_PAGE_LAYOUTS) {
      expect(
        isSideBySideActive({ ...sideBySide, "pageLayouts.variant": variant })
      ).toBe(true);
    }
  });

  it("is false for the default variant even with stories and widgets on", () => {
    expect(
      isSideBySideActive({ ...sideBySide, "pageLayouts.variant": undefined })
    ).toBe(false);
  });

  it("is false when either stories pref is off", () => {
    expect(
      isSideBySideActive({ ...sideBySide, "feeds.section.topstories": false })
    ).toBe(false);
    expect(
      isSideBySideActive({ ...sideBySide, "feeds.system.topstories": false })
    ).toBe(false);
  });

  it("is false when the widgets container is not visible", () => {
    expect(
      isSideBySideActive({ ...sideBySide, "widgets.system.enabled": false })
    ).toBe(false);
  });

  it("is false when the container is on but every widget is hidden", () => {
    expect(
      isSideBySideActive({ ...sideBySide, "widgets.enabled": false })
    ).toBe(false);
    expect(
      isSideBySideActive({ ...sideBySide, "widgets.lists.enabled": false })
    ).toBe(false);
  });

  // Small weather lives in the sidebar, not the band.
  it("is false when the only enabled widget is weather in the sidebar", () => {
    const weatherOnly = {
      ...sideBySide,
      "widgets.lists.enabled": false,
      "widgets.system.weather.enabled": true,
      "widgets.weather.enabled": true,
      "weather.display": "",
      "widgets.weather.size": "small",
    };
    expect(isSideBySideActive(weatherOnly)).toBe(false);

    // ...but a larger weather widget stays in the content area and does count.
    expect(
      isSideBySideActive({ ...weatherOnly, "widgets.weather.size": "medium" })
    ).toBe(true);
  });

  it("is true when the variant comes from trainhopConfig", () => {
    expect(
      isSideBySideActive({
        ...STORIES_ON,
        ...WIDGETS_ON,
        trainhopConfig: {
          pageLayouts: {
            variant: PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_CONTENT_LEAD,
          },
        },
      })
    ).toBe(true);
  });

  // Goes through isWidgetsContainerVisible, so the keys the hand-rolled copies in
  // DiscoveryStreamBase.jsx and selectLayoutRender.mjs miss still count.
  it("is true when widgets are enabled only via trainhopConfig.widgetsSettings", () => {
    expect(
      isSideBySideActive({
        ...sideBySide,
        "widgets.system.enabled": false,
        trainhopConfig: { widgetsSettings: { enabled: true } },
      })
    ).toBe(true);
  });

  it("is true when widgets are enabled only via trainhopConfig.widgets", () => {
    expect(
      isSideBySideActive({
        ...sideBySide,
        "widgets.system.enabled": false,
        trainhopConfig: { widgets: { enabled: true } },
      })
    ).toBe(true);
  });
});

describe("sideBySideBandClasses", () => {
  it("is empty outside the experiment", () => {
    expect(sideBySideBandClasses({})).toEqual([]);
    expect(
      sideBySideBandClasses({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.NOVA_FULL_WIDTH,
      })
    ).toEqual([]);
    expect(
      sideBySideBandClasses({ "pageLayouts.variant": "not-a-variant" })
    ).toEqual([]);
  });

  // The lead class alone, so nothing unlocks the fourth content card.
  it("gives the four-column variants just their lead class", () => {
    expect(
      sideBySideBandClasses({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_CONTENT_LEAD,
      })
    ).toEqual(["side-by-side-content-lead"]);
    expect(
      sideBySideBandClasses({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_WIDGETS_LEAD,
      })
    ).toEqual(["side-by-side-widgets-lead"]);
  });

  // The lead class is shared with the four-column variant, so every existing
  // side-by-side rule keeps matching; side-by-side-five is the only difference.
  it("adds side-by-side-five for the five-column variants", () => {
    expect(
      sideBySideBandClasses({
        "pageLayouts.variant":
          PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_CONTENT_LEAD_FIVE,
      })
    ).toEqual(["side-by-side-content-lead", "side-by-side-five"]);
    expect(
      sideBySideBandClasses({
        "pageLayouts.variant":
          PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_WIDGETS_LEAD_FIVE,
      })
    ).toEqual(["side-by-side-widgets-lead", "side-by-side-five"]);
  });

  // CSS matches classes per token, so a "-five" variant name would not be
  // matched by any .side-by-side-*-lead rule. It must never reach the class list.
  it("never emits a -five variant name as a class", () => {
    for (const variant of SIDE_BY_SIDE_PAGE_LAYOUTS.filter(v =>
      v.endsWith("-five")
    )) {
      expect(
        sideBySideBandClasses({ "pageLayouts.variant": variant })
      ).not.toContain(variant);
    }
  });

  it("follows a variant set through trainhopConfig", () => {
    expect(
      sideBySideBandClasses({
        trainhopConfig: {
          pageLayouts: {
            variant: PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_WIDGETS_LEAD_FIVE,
          },
        },
      })
    ).toEqual(["side-by-side-widgets-lead", "side-by-side-five"]);
  });
});

const spaces = {
  "nova.enabled": true,
  "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_BUTTONS_BOTTOM,
  ...STORIES_ON,
  ...WIDGETS_ON,
};

describe("spacesBandClasses", () => {
  it("decomposes each variant into the family class plus a placement", () => {
    expect(
      spacesBandClasses({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_BUTTONS_TOP,
      })
    ).toEqual(["spaces", "spaces-buttons-top"]);
    expect(
      spacesBandClasses({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_BUTTONS_BOTTOM,
      })
    ).toEqual(["spaces", "spaces-buttons-bottom"]);
  });

  it("adds the arrows class to the floating arrows variant", () => {
    expect(
      spacesBandClasses({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_FLOATING_ARROWS,
      })
    ).toEqual(["spaces", "spaces-floating-arrows", "spaces-arrows"]);
  });

  it("returns nothing for the other layouts", () => {
    expect(spacesBandClasses({})).toEqual([]);
    expect(
      spacesBandClasses({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_CONTENT_LEAD,
      })
    ).toEqual([]);
  });
});

describe("isSpacesPreviewEnabled", () => {
  it("is on in the preview variants and off in the other spaces variants", () => {
    for (const variant of [
      PAGE_LAYOUT_VARIANTS.SPACES_PREVIEW_KIT,
      PAGE_LAYOUT_VARIANTS.SPACES_PREVIEW_ILLUSTRATED,
      PAGE_LAYOUT_VARIANTS.SPACES_PREVIEW_CARDS,
    ]) {
      expect(isSpacesPreviewEnabled({ "pageLayouts.variant": variant })).toBe(
        true
      );
    }
    for (const variant of [
      PAGE_LAYOUT_VARIANTS.SPACES_BUTTONS_BOTTOM,
      PAGE_LAYOUT_VARIANTS.SPACES_FLOATING_ARROWS,
    ]) {
      expect(isSpacesPreviewEnabled({ "pageLayouts.variant": variant })).toBe(
        false
      );
    }
    expect(isSpacesPreviewEnabled({})).toBe(false);
  });

  it("follows the trainhop variant", () => {
    expect(
      isSpacesPreviewEnabled({
        trainhopConfig: { pageLayouts: { variant: "spaces-preview-kit" } },
      })
    ).toBe(true);
  });

  it("uses the thematic config's flag", () => {
    const config = {
      order: ["a", "b"],
      spaces: { a: { label: "A" }, b: { label: "B" } },
    };
    const thematic = preview => ({
      "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_THEMATIC_V1,
      "pageLayouts.spacesConfig": JSON.stringify({ ...config, preview }),
    });
    expect(isSpacesPreviewEnabled(thematic(false))).toBe(false);
    expect(isSpacesPreviewEnabled(thematic(true))).toBe(true);
  });
});

describe("resolveFeatureSpacePreviewImages", () => {
  const ASSETS = "chrome://newtab/content/data/content/assets/";

  it("maps each space to its variant's image", () => {
    expect(
      resolveFeatureSpacePreviewImages({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_PREVIEW_KIT,
      })
    ).toEqual({
      stories: `${ASSETS}spaces-preview-kit-stories.png`,
      widgets: `${ASSETS}spaces-preview-kit-widgets.png`,
      activity: `${ASSETS}spaces-preview-kit-activity.png`,
    });
    expect(
      resolveFeatureSpacePreviewImages({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_PREVIEW_ILLUSTRATED,
      }).stories
    ).toBe(`${ASSETS}spaces-preview-stories.png`);
  });

  it("gives no images outside the image preview variants", () => {
    expect(resolveFeatureSpacePreviewImages({})).toEqual({});
    expect(
      resolveFeatureSpacePreviewImages({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_PREVIEW_CARDS,
      })
    ).toEqual({});
    expect(
      resolveFeatureSpacePreviewImages({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_BUTTONS_BOTTOM,
      })
    ).toEqual({});
  });
});

describe("isSpacesAssigned", () => {
  it("is true for every spaces variant, whatever the content prefs", () => {
    for (const variant of SPACES_PAGE_LAYOUTS) {
      expect(isSpacesAssigned({ "pageLayouts.variant": variant })).toBe(true);
    }
  });

  it("is false outside the spaces variants", () => {
    expect(isSpacesAssigned({})).toBe(false);
    expect(
      isSpacesAssigned({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SIDE_BY_SIDE_WIDGETS_LEAD,
      })
    ).toBe(false);
  });
});

describe("resolvePopulatedSpaces", () => {
  it("lists only the spaces whose content is enabled, in tablist order", () => {
    expect(resolvePopulatedSpaces(spaces)).toEqual(["stories", "widgets"]);
    expect(
      resolvePopulatedSpaces({
        ...spaces,
        "feeds.section.highlights": true,
      })
    ).toEqual(["stories", "widgets", "activity"]);
  });

  it("does not turn anything on", () => {
    expect(resolvePopulatedSpaces({ ...spaces, ...{} })).not.toContain(
      "activity"
    );
    expect(resolvePopulatedSpaces({})).toEqual([]);
  });

  describe("pageLayouts.spacesOrder", () => {
    const all = { ...spaces, "feeds.section.highlights": true };

    it("follows the pref", () => {
      expect(
        resolvePopulatedSpaces({
          ...all,
          "pageLayouts.spacesOrder": "activity,stories,widgets",
        })
      ).toEqual(["activity", "stories", "widgets"]);
    });

    it("prefers the trainhop array over the pref", () => {
      expect(
        resolvePopulatedSpaces({
          ...all,
          "pageLayouts.spacesOrder": "widgets,stories,activity",
          trainhopConfig: {
            pageLayouts: { spacesOrder: ["activity", "stories", "widgets"] },
          },
        })
      ).toEqual(["activity", "stories", "widgets"]);
    });
  });
});

describe("the experiment override", () => {
  // One shape for all three spaces: the experiment turns a space on unless its
  // opt-out pref says the user turned it off while enrolled. The user's own
  // pref is never written, so unenrolling restores their choice.
  const enrolled = {
    ...spaces,
    trainhopConfig: {
      stories: { enabled: true },
      widgets: { enabled: true },
      highlights: { enabled: true },
    },
  };

  it("overrides a space the profile enrolled with turned off", () => {
    expect(
      resolvePopulatedSpaces({
        ...enrolled,
        "feeds.section.topstories": false,
        "feeds.section.highlights": false,
      })
    ).toEqual(["stories", "widgets", "activity"]);
  });

  it("respects a space turned off while enrolled, for every space", () => {
    expect(
      resolvePopulatedSpaces({
        ...enrolled,
        "feeds.section.topstories": false,
        "spaces.storiesOptOut": true,
        "widgets.enabled": false,
        "spaces.widgetsOptOut": true,
        "feeds.section.highlights": false,
        "spaces.activityOptOut": true,
      })
    ).toEqual([]);
  });

  it("does not override outside the experiment", () => {
    expect(
      resolvePopulatedSpaces({
        ...enrolled,
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.NOVA_FULL_WIDTH,
        "feeds.section.topstories": false,
      })
    ).toEqual(["widgets"]);
  });

  it("does not conjure content that is not there", () => {
    // Region and locale decide whether stories exist at all, and no override
    // should produce an empty panel.
    expect(
      resolvePopulatedSpaces({
        ...enrolled,
        "feeds.system.topstories": false,
      })
    ).not.toContain("stories");
  });
});

describe("the widgets space gate", () => {
  it("is not a space when the user has switched widgets off", () => {
    expect(
      resolvePopulatedSpaces({ ...spaces, "widgets.enabled": false })
    ).not.toContain("widgets");
  });

  it("is not a space when the container is on but no widget is enabled", () => {
    expect(
      resolvePopulatedSpaces({
        ...spaces,
        "widgets.lists.enabled": false,
      })
    ).not.toContain("widgets");
  });

  it("is not a space when weather is the only widget", () => {
    // Weather moves to the sidebar at its small size, so the row is empty.
    expect(
      resolvePopulatedSpaces({
        ...spaces,
        "widgets.lists.enabled": false,
        "widgets.system.weather.enabled": true,
        "widgets.weather.enabled": true,
        "widgets.weather.size": "small",
      })
    ).not.toContain("widgets");
  });

  it("is a space when a widget is actually showing", () => {
    expect(resolvePopulatedSpaces(spaces)).toContain("widgets");
  });
});

describe("isSpacesActive", () => {
  it("is true with a spaces variant and somewhere to navigate to", () => {
    expect(isSpacesActive(spaces)).toBe(true);
  });

  it("is false with only one populated space", () => {
    expect(
      isSpacesActive({
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_BUTTONS_BOTTOM,
        ...STORIES_ON,
      })
    ).toBe(false);
  });

  it("is false outside the spaces variants", () => {
    expect(
      isSpacesActive({
        ...spaces,
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.NOVA_FULL_WIDTH,
      })
    ).toBe(false);
  });

  it("is false without Nova, whose stylesheet it depends on", () => {
    expect(isSpacesActive({ ...spaces, "nova.enabled": false })).toBe(false);
  });
});

describe("the thematic spaces variant", () => {
  // There is no built-in config, so every thematic case has to supply one. This
  // mirrors the shape devtools seeds and a recipe ships.
  const RAW_CONFIG = {
    order: ["games", "home", "living"],
    default: "home",
    spaces: {
      home: {
        label: "Home",
        icon: "chrome://browser/skin/home.svg",
        sections: ["top_stories_section", "politics"],
        widgets: ["pictureOfTheDay"],
      },
      games: {
        label: "Games & Entertainment",
        icon: "chrome://browser/skin/home.svg",
        sections: ["entertainment", "sports"],
        widgets: ["crossword"],
      },
      living: {
        label: "Living",
        icon: "chrome://browser/skin/home.svg",
        sections: ["food", "health"],
        widgets: ["lists", "focusTimer"],
      },
    },
  };
  const CONFIG = normalizeSpacesConfig(RAW_CONFIG);
  const thematic = {
    ...spaces,
    "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_THEMATIC_V1,
    trainhopConfig: { spaces: RAW_CONFIG },
  };

  it("is assigned by pref and by trainhop", () => {
    expect(isSpacesThematicAssigned(thematic)).toBe(true);
    expect(
      isSpacesThematicAssigned({
        trainhopConfig: {
          pageLayouts: {
            variant: PAGE_LAYOUT_VARIANTS.SPACES_THEMATIC_V1,
          },
        },
      })
    ).toBe(true);
    expect(isSpacesThematicAssigned(spaces)).toBe(false);
  });

  it("is one of the spaces variants, and reuses the buttons-bottom control", () => {
    expect(isSpacesAssigned(thematic)).toBe(true);
    expect(spacesBandClasses(thematic)).toEqual([
      "spaces",
      "spaces-buttons-bottom",
      "spaces-thematic",
    ]);
  });

  it("populates all three thematic spaces, not the feature-type ones", () => {
    expect(resolvePopulatedSpaces(thematic)).toEqual([
      "games",
      "home",
      "living",
    ]);
    expect(
      isSpacesActive(thematic, feedState("top_stories_section", "arts", "food"))
    ).toBe(true);
  });

  // Turning widgets off empties the spaces that relied on them, and no more.
  it("keeps the spaces that have sections with every widget off", () => {
    const noWidgets = {
      ...thematic,
      "widgets.enabled": false,
      "widgets.system.enabled": false,
    };
    expect(resolvePopulatedSpaces(noWidgets)).toEqual([
      "games",
      "home",
      "living",
    ]);
    expect(
      isSpacesActive(noWidgets, feedState("top_stories_section", "food"))
    ).toBe(true);
  });

  it("populates none of them without stories", () => {
    const noStories = { ...thematic, "feeds.section.topstories": false };
    expect(resolvePopulatedSpaces(noStories)).toEqual([]);
    expect(
      isSpacesActive(noStories, feedState("top_stories_section", "food"))
    ).toBe(false);
  });

  describe("arrows", () => {
    const withArrows = {
      ...thematic,
      trainhopConfig: { spaces: { ...RAW_CONFIG, arrows: true } },
    };

    it("shows them when the config asks", () => {
      expect(hasSpacesArrows(withArrows)).toBe(true);
      expect(spacesBandClasses(withArrows)).toEqual([
        "spaces",
        "spaces-buttons-bottom",
        "spaces-thematic",
        "spaces-arrows",
      ]);
    });

    it("leaves them off otherwise", () => {
      expect(hasSpacesArrows(thematic)).toBe(false);
      expect(
        hasSpacesArrows({
          ...thematic,
          trainhopConfig: { spaces: { ...RAW_CONFIG, arrows: "yes" } },
        })
      ).toBe(false);
    });

    it("are always on for the floating arrows variant", () => {
      expect(
        hasSpacesArrows({
          "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_FLOATING_ARROWS,
        })
      ).toBe(true);
    });
  });

  it("leaves the side-by-side variants alone", () => {
    expect(isSideBySideActive(thematic)).toBe(false);
    expect(isSideBySideActive(sideBySide)).toBe(true);
  });

  // The V1 enrollment control, which the thematic gate has to honour too: the
  // feeds already revive stories for an enrolled profile that had them off, so a
  // gate reading the raw pref would refuse to render what they prepared.
  describe("the V1 space overrides", () => {
    const storiesOff = {
      ...thematic,
      "feeds.section.topstories": false,
      trainhopConfig: {
        ...thematic.trainhopConfig,
        stories: { enabled: true },
      },
    };

    it("renders for a profile that had stories off", () => {
      expect(resolvePopulatedSpaces(storiesOff)).toEqual([
        "games",
        "home",
        "living",
      ]);
      expect(
        isSpacesActive(
          storiesOff,
          feedState("top_stories_section", "arts", "food")
        )
      ).toBe(true);
    });

    it("stops once that profile opts out again", () => {
      expect(
        isSpacesActive(
          { ...storiesOff, "spaces.storiesOptOut": true },
          feedState("top_stories_section", "arts", "food")
        )
      ).toBe(false);
    });

    it("gives widgets back to a profile that had the widgets container off", () => {
      const widgetsOff = {
        ...thematic,
        "widgets.enabled": false,
        trainhopConfig: {
          ...thematic.trainhopConfig,
          widgets: { enabled: true },
        },
      };
      // Only living has a widget that is on, and none of its sections arrived.
      const sectionKeys = ["top_stories_section"];
      expect(resolvePopulatedSpaces(widgetsOff, sectionKeys)).toEqual([
        "home",
        "living",
      ]);
      expect(
        resolvePopulatedSpaces(
          { ...widgetsOff, "spaces.widgetsOptOut": true },
          sectionKeys
        )
      ).toEqual(["home"]);
    });

    // The overrides are scoped to a spaces variant, so these layouts are
    // unaffected by them.
    // Widgets.jsx asks without the feed, and a false here would show the size
    // toggle in the header where the add button belongs.
    it("is true without the feed state, which asks only about the layout", () => {
      expect(isSpacesActive(thematic)).toBe(true);
    });

    it("leaves the side-by-side layouts reading the plain prefs", () => {
      expect(
        isSideBySideActive({ ...sideBySide, "feeds.section.topstories": false })
      ).toBe(false);
    });
  });

  describe("normalizeSpacesConfig", () => {
    const VALID = {
      order: ["a", "b"],
      default: "b",
      spaces: {
        a: {
          label: "A",
          icon: "chrome://browser/skin/home.svg",
          sections: ["food"],
          widgets: ["lists"],
        },
        b: { label: "B" },
      },
    };

    it("keeps the order and the named default", () => {
      const config = normalizeSpacesConfig(VALID);
      expect(config.order).toEqual(["a", "b"]);
      expect(config.default).toBe("b");
      expect(config.spaces.a.sections).toEqual(["food"]);
    });

    it("marks a space for Recent activity only for a literal true", () => {
      const config = normalizeSpacesConfig({
        order: ["a", "b", "c"],
        spaces: {
          a: { label: "A", activity: true },
          b: { label: "B", activity: "true" },
          c: { label: "C" },
        },
      });
      expect(config.spaces.a.activity).toBe(true);
      expect(config.spaces.b.activity).toBe(false);
      expect(config.spaces.c.activity).toBe(false);
    });

    it("takes a named widget catch-all, else the default space", () => {
      expect(
        normalizeSpacesConfig({ ...VALID, widgetCatchAll: "a" }).widgetCatchAll
      ).toBe("a");
      expect(normalizeSpacesConfig(VALID).widgetCatchAll).toBe("b");
      expect(
        normalizeSpacesConfig({ ...VALID, widgetCatchAll: "nope" })
          .widgetCatchAll
      ).toBe("b");
    });

    it("turns the arrows on only for a literal true", () => {
      expect(normalizeSpacesConfig(VALID).arrows).toBe(false);
      expect(normalizeSpacesConfig({ ...VALID, arrows: true }).arrows).toBe(
        true
      );
      expect(normalizeSpacesConfig({ ...VALID, arrows: "true" }).arrows).toBe(
        false
      );
    });

    // about:newtab makes no third-party requests, so an https icon is dropped.
    it("takes a chrome icon URL and drops anything else", () => {
      const config = normalizeSpacesConfig({
        order: ["a", "b"],
        spaces: {
          a: {
            label: "A",
            icon: "chrome://browser/content/profiles/assets/16_soccer.svg",
          },
          b: { label: "B", icon: "https://example.com/icon.svg" },
        },
      });
      expect(config.spaces.a.icon).toBe(
        "chrome://browser/content/profiles/assets/16_soccer.svg"
      );
      expect(config.spaces.b.icon).toBeUndefined();
    });

    it("turns the preview on only for preview: true", () => {
      expect(normalizeSpacesConfig(VALID).preview).toBe(false);
      expect(normalizeSpacesConfig({ ...VALID, preview: "yes" }).preview).toBe(
        false
      );
      expect(normalizeSpacesConfig({ ...VALID, preview: true }).preview).toBe(
        true
      );
    });

    it("takes a chrome preview image URL and drops anything else", () => {
      const config = normalizeSpacesConfig({
        order: ["a", "b"],
        spaces: {
          a: {
            label: "A",
            previewImage: "chrome://newtab/content/data/content/assets/a.png",
          },
          b: { label: "B", previewImage: "https://example.com/b.png" },
        },
      });
      expect(config.spaces.a.previewImage).toBe(
        "chrome://newtab/content/data/content/assets/a.png"
      );
      expect(config.spaces.b.previewImage).toBeUndefined();
    });

    // The two families need opposite fill and stroke, so the path decides.
    it("tags the icon family from its path", () => {
      const config = normalizeSpacesConfig({
        order: ["a", "b", "c"],
        spaces: {
          a: {
            label: "A",
            icon: "chrome://browser/content/profiles/assets/16_soccer.svg",
          },
          b: { label: "B", icon: "chrome://browser/skin/home.svg" },
          c: { label: "C" },
        },
      });
      expect(config.spaces.a.iconFamily).toBe("avatar");
      expect(config.spaces.b.iconFamily).toBe("glyph");
      expect(config.spaces.c.iconFamily).toBeUndefined();
    });

    // Silent degrade: the rest of the config still renders.
    it("drops a space with no label, keeping the others", () => {
      const config = normalizeSpacesConfig({
        order: ["a", "b"],
        spaces: {
          a: { label: "A" },
          b: { icon: "chrome://browser/skin/home.svg" },
        },
      });
      expect(config.order).toEqual(["a"]);
    });

    it("gives the leftmost space both roles when the default did not survive", () => {
      const config = normalizeSpacesConfig({
        order: ["a"],
        default: "b",
        spaces: { a: { label: "A" } },
      });
      expect(config.default).toBe("a");
    });

    // Nothing is merged and nothing falls back, so a broken config kills the
    // layout rather than half-applying.
    it("is null for a config missing order, spaces, or every space", () => {
      // Rejecting logs, so whoever wrote the config can see why.
      const error = jest.spyOn(console, "error").mockImplementation(() => {});
      for (const raw of [
        undefined,
        null,
        { spaces: { a: { label: "A" } } },
        { order: ["a"] },
        { order: [], spaces: {} },
        { order: ["a"], spaces: { a: {} } },
      ]) {
        expect(normalizeSpacesConfig(raw)).toBeNull();
      }
      expect(error).toHaveBeenCalledTimes(6);
      error.mockRestore();
    });
  });

  describe("resolveSpacesConfigFrom", () => {
    const VALID = { order: ["a"], spaces: { a: { label: "A" } } };

    it("takes a trainhop payload over the pref", () => {
      const config = resolveSpacesConfigFrom(
        { spaces: normalizeSpacesConfig(VALID) },
        JSON.stringify({ order: ["z"], spaces: { z: { label: "Z" } } })
      );
      expect(config.order).toEqual(["a"]);
    });

    it("parses the pref when there is no trainhop payload", () => {
      expect(resolveSpacesConfigFrom({}, JSON.stringify(VALID)).order).toEqual([
        "a",
      ]);
    });

    it("is null with neither, and does not complain about it", () => {
      const error = jest.spyOn(console, "error").mockImplementation(() => {});
      expect(resolveSpacesConfigFrom(undefined, "")).toBeNull();
      expect(error).not.toHaveBeenCalled();
      error.mockRestore();
    });

    it("is null for an unparseable pref, and says so", () => {
      const error = jest.spyOn(console, "error").mockImplementation(() => {});
      expect(resolveSpacesConfigFrom({}, "not json")).toBeNull();
      expect(error).toHaveBeenCalledTimes(1);
      error.mockRestore();
    });
  });

  describe("resolveThematicSpacesConfig", () => {
    it("resolves from the recipe, then the pref, then nothing", () => {
      expect(resolveThematicSpacesConfig(thematic)).toEqual(CONFIG);
      expect(
        resolveThematicSpacesConfig({
          "pageLayouts.spacesConfig": JSON.stringify(RAW_CONFIG),
        })
      ).toEqual(CONFIG);
      expect(resolveThematicSpacesConfig({})).toBeNull();
    });
  });

  describe("tablist order and the default space", () => {
    it("puts Home in the middle", () => {
      expect(resolvePopulatedSpaces(thematic)).toEqual([
        "games",
        "home",
        "living",
      ]);
    });

    // The whole point of HNT-3317: the page opens on Home even though Home is
    // not the leftmost space.
    it("opens on Home rather than the leftmost space", () => {
      expect(
        resolveThematicDefaultSpace(CONFIG, resolvePopulatedSpaces(thematic))
      ).toBe("home");
    });

    it("falls back to the first surviving space when Home is dropped", () => {
      const populated = resolvePopulatedSpaces(thematic, ["food", "sports"]);
      expect(populated).toEqual(["games", "living"]);
      expect(resolveThematicDefaultSpace(CONFIG, populated)).toBe("games");
    });

    it("has no default when no space survives", () => {
      expect(resolveThematicDefaultSpace(CONFIG, [])).toBeUndefined();
    });
  });

  describe("resolveThematicSpaceSections", () => {
    const KEYS = [
      "top_stories_section",
      "entertainment",
      "food",
      "sports",
      "health",
      "an_unlisted_key",
    ];

    it("gives each space only its own sections, in delivery order", () => {
      expect(resolveThematicSpaceSections(CONFIG, "games", KEYS)).toEqual([
        "entertainment",
        "sports",
      ]);
      expect(resolveThematicSpaceSections(CONFIG, "living", KEYS)).toEqual([
        "food",
        "health",
      ]);
    });

    it("gives Home its own sections plus whatever is left over", () => {
      expect(resolveThematicSpaceSections(CONFIG, "home", KEYS)).toEqual([
        "top_stories_section",
        "an_unlisted_key",
      ]);
    });

    it("is empty for a space with none of its sections delivered", () => {
      expect(resolveThematicSpaceSections(CONFIG, "games", ["food"])).toEqual(
        []
      );
    });

    it("is empty for every space before the feed arrives", () => {
      for (const id of ["home", "games", "living"]) {
        expect(resolveThematicSpaceSections(CONFIG, id, [])).toEqual([]);
      }
    });
  });

  describe("resolvePopulatedSpaces with the feed's sections", () => {
    // games names only crossword, which is off.
    it("drops a space that has no sections and no widgets", () => {
      expect(
        resolvePopulatedSpaces(thematic, ["top_stories_section", "food"])
      ).toEqual(["home", "living"]);
    });

    it("keeps a space that has widgets but no sections", () => {
      expect(resolvePopulatedSpaces(thematic, ["top_stories_section"])).toEqual(
        ["home", "living"]
      );
      expect(isSpacesActive(thematic, feedState("top_stories_section"))).toBe(
        true
      );
    });

    // Below two spaces there is nowhere to navigate, so the band falls back.
    it("turns spaces off when only Home has something to show", () => {
      const noWidgets = { ...thematic, "widgets.enabled": false };
      expect(
        resolvePopulatedSpaces(noWidgets, ["top_stories_section"])
      ).toEqual(["home"]);
      expect(isSpacesActive(noWidgets, feedState("top_stories_section"))).toBe(
        false
      );
    });

    it("turns spaces off before the feed arrives, and back on after", () => {
      expect(resolvePopulatedSpaces(thematic, [])).toEqual([]);
      expect(isSpacesActive(thematic, feedState())).toBe(false);
      expect(
        isSpacesActive(thematic, feedState("top_stories_section", "food"))
      ).toBe(true);
    });

    it("keeps every space when no section keys are given", () => {
      expect(resolvePopulatedSpaces(thematic)).toEqual([
        "games",
        "home",
        "living",
      ]);
    });
  });

  describe("Recent activity", () => {
    const RAW_WITH_ACTIVITY = {
      ...RAW_CONFIG,
      order: [...RAW_CONFIG.order, "mystuff"],
      spaces: {
        ...RAW_CONFIG.spaces,
        mystuff: { label: "My Stuff", activity: true },
      },
    };
    const withActivity = {
      ...thematic,
      "feeds.section.highlights": true,
      trainhopConfig: { spaces: RAW_WITH_ACTIVITY },
    };

    it("goes to the space the config marks for it", () => {
      expect(
        resolveThematicActivitySpace(normalizeSpacesConfig(RAW_WITH_ACTIVITY))
      ).toBe("mystuff");
    });

    it("goes to the default space when none is marked", () => {
      expect(resolveThematicActivitySpace(CONFIG)).toBe("home");
    });

    it("keeps a space that has only Recent activity while it is on", () => {
      expect(
        resolvePopulatedSpaces(withActivity, ["top_stories_section"])
      ).toEqual(["home", "living", "mystuff"]);
      expect(
        resolvePopulatedSpaces(
          { ...withActivity, "feeds.section.highlights": false },
          ["top_stories_section"]
        )
      ).toEqual(["home", "living"]);
    });

    // The experiment revives Recent activity for a profile that had it off.
    it("keeps it for a profile the experiment turns Recent activity back on for", () => {
      const overridden = {
        ...withActivity,
        "feeds.section.highlights": false,
        trainhopConfig: {
          ...withActivity.trainhopConfig,
          highlights: { enabled: true },
        },
      };
      expect(
        resolvePopulatedSpaces(overridden, ["top_stories_section"])
      ).toContain("mystuff");
    });
  });

  describe("resolveThematicSpaceWidgets", () => {
    // Every widget the assignment names, plus two it does not.
    const ALL_ON = {
      "widgets.system.enabled": true,
      "widgets.enabled": true,
      "widgets.system.pictureOfTheDay.enabled": true,
      "widgets.pictureOfTheDay.enabled": true,
      "widgets.system.crossword.enabled": true,
      "widgets.crossword.enabled": true,
      "widgets.system.lists.enabled": true,
      "widgets.lists.enabled": true,
      "widgets.system.focusTimer.enabled": true,
      "widgets.focusTimer.enabled": true,
      "widgets.system.privacy.enabled": true,
      "widgets.privacy.enabled": true,
      "widgets.system.clocks.enabled": true,
      "widgets.clocks.enabled": true,
    };

    it("gives each space the widgets assigned to it", () => {
      expect(resolveThematicSpaceWidgets(CONFIG, "games", ALL_ON)).toEqual([
        "crossword",
      ]);
      expect(resolveThematicSpaceWidgets(CONFIG, "living", ALL_ON)).toEqual([
        "lists",
        "focusTimer",
      ]);
    });

    // So turning a widget on from the customize menu is never invisible.
    it("adds the unassigned widgets to Home", () => {
      expect(resolveThematicSpaceWidgets(CONFIG, "home", ALL_ON)).toEqual([
        "pictureOfTheDay",
        "clocks",
        "privacy",
      ]);
    });

    it("drops widgets that are turned off", () => {
      expect(
        resolveThematicSpaceWidgets(CONFIG, "games", {
          ...ALL_ON,
          "widgets.crossword.enabled": false,
        })
      ).toEqual([]);
    });

    it("is empty for every space when the master toggle is off", () => {
      const off = { ...ALL_ON, "widgets.enabled": false };
      for (const id of ["home", "games", "living"]) {
        expect(resolveThematicSpaceWidgets(CONFIG, id, off)).toEqual([]);
      }
    });

    it("adds the unassigned widgets to the named catch-all instead", () => {
      const config = normalizeSpacesConfig({
        ...RAW_CONFIG,
        widgetCatchAll: "living",
      });
      expect(resolveThematicSpaceWidgets(config, "home", ALL_ON)).toEqual([
        "pictureOfTheDay",
      ]);
      expect(resolveThematicSpaceWidgets(config, "living", ALL_ON)).toEqual([
        "clocks",
        "lists",
        "focusTimer",
        "privacy",
      ]);
    });

    // It is in the first card of Popular Today, so nowhere else.
    it("leaves out the first-slot widget, even where it is named", () => {
      const firstSlot = {
        ...ALL_ON,
        "nova.enabled": true,
        "feeds.section.topstories": true,
        "feeds.system.topstories": true,
        "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_THEMATIC_V1,
        "pageLayouts.widgetFirstContentSlot.widget": "pictureOfTheDay",
      };
      for (const id of ["home", "games", "living"]) {
        expect(
          resolveThematicSpaceWidgets(CONFIG, id, firstSlot)
        ).not.toContain("pictureOfTheDay");
      }
    });

    // At its small size it renders in the sidebar, outside every space.
    it("leaves out Weather while it is in the sidebar", () => {
      const weather = {
        ...ALL_ON,
        "widgets.system.weather.enabled": true,
        "widgets.weather.enabled": true,
        "system.showWeather": true,
        showWeather: true,
      };
      const ids = size =>
        resolveThematicSpaceWidgets(CONFIG, "home", {
          ...weather,
          "widgets.weather.size": size,
        });
      expect(ids("small")).not.toContain("weather");
      expect(ids("medium")).toContain("weather");
    });

    it("leaves out Weather in the sidebar when the experiment overrides the master toggle", () => {
      const ids = resolveThematicSpaceWidgets(CONFIG, "home", {
        ...thematic,
        ...ALL_ON,
        "widgets.enabled": false,
        "widgets.system.weather.enabled": true,
        "widgets.weather.enabled": true,
        "widgets.weather.size": "small",
        "system.showWeather": true,
        showWeather: true,
        trainhopConfig: {
          ...thematic.trainhopConfig,
          widgets: { enabled: true },
        },
      });
      expect(ids).toContain("clocks");
      expect(ids).not.toContain("weather");
    });

    it("is empty for every space when the widgets container is hidden", () => {
      const hidden = { ...ALL_ON, "widgets.system.enabled": false };
      for (const id of ["home", "games", "living"]) {
        expect(resolveThematicSpaceWidgets(CONFIG, id, hidden)).toEqual([]);
      }
    });

    describe("with a widget library", () => {
      const LIBRARY = normalizeSpacesConfig({
        ...RAW_CONFIG,
        widgetLibrary: "living",
      });
      const home = homePref =>
        resolveThematicSpaceWidgets(LIBRARY, "home", {
          ...ALL_ON,
          [PREF_HOME_WIDGETS]: homePref,
        });

      it("shows the config's Home widgets until the user changes them", () => {
        expect(home("")).toEqual(["pictureOfTheDay"]);
        expect(home("not json")).toEqual(["pictureOfTheDay"]);
      });

      it("shows the user's Home list on Home", () => {
        expect(home(JSON.stringify(["crossword", "clocks"])).sort()).toEqual([
          "clocks",
          "crossword",
        ]);
      });

      // An empty list is the user hiding everything, not an unset pref.
      it("shows nothing on Home once the user empties the list", () => {
        expect(home("[]")).toEqual([]);
      });

      it("drops a Home widget that is turned off", () => {
        expect(
          resolveThematicSpaceWidgets(LIBRARY, "home", {
            ...ALL_ON,
            "widgets.pictureOfTheDay.enabled": false,
          })
        ).toEqual([]);
      });

      // A widget is pinned to Home or in the library, never both.
      it("shows every enabled widget not on Home in the library", () => {
        expect(
          resolveThematicSpaceWidgets(LIBRARY, "living", ALL_ON).sort()
        ).toEqual(["clocks", "crossword", "focusTimer", "lists", "privacy"]);
      });

      // How many start on Home is the config's call, not a limit in code.
      it("shows every pinned widget on Home", () => {
        const pinned = ["lists", "clocks", "privacy", "focusTimer"];
        const prefs = {
          ...ALL_ON,
          [PREF_HOME_WIDGETS]: JSON.stringify(pinned),
        };
        expect(
          resolveThematicSpaceWidgets(LIBRARY, "home", prefs).sort()
        ).toEqual([...pinned].sort());
        expect(
          resolveThematicSpaceWidgets(LIBRARY, "living", prefs).sort()
        ).toEqual(["crossword", "pictureOfTheDay"]);
      });

      it("leaves a turned-off pinned widget out of Home", () => {
        expect(
          resolveThematicSpaceWidgets(LIBRARY, "home", {
            ...ALL_ON,
            "widgets.lists.enabled": false,
            [PREF_HOME_WIDGETS]: JSON.stringify(["lists", "clocks", "privacy"]),
          }).sort()
        ).toEqual(["clocks", "privacy"]);
      });

      it("leaves the other spaces to the config", () => {
        expect(resolveThematicSpaceWidgets(LIBRARY, "games", ALL_ON)).toEqual([
          "crossword",
        ]);
      });
    });
  });

  describe("widget placement", () => {
    it("names a widget library only when it is another kept space", () => {
      expect(
        normalizeSpacesConfig({ ...RAW_CONFIG, widgetLibrary: "living" })
          .widgetLibrary
      ).toBe("living");
      expect(CONFIG.widgetLibrary).toBe(null);
      expect(
        normalizeSpacesConfig({ ...RAW_CONFIG, widgetLibrary: "home" })
          .widgetLibrary
      ).toBe(null);
      expect(
        normalizeSpacesConfig({ ...RAW_CONFIG, widgetLibrary: "nope" })
          .widgetLibrary
      ).toBe(null);
    });

    // So a widget no space names lands in the library rather than on Home.
    it("makes the library the widget catch-all unless one is named", () => {
      expect(
        normalizeSpacesConfig({ ...RAW_CONFIG, widgetLibrary: "living" })
          .widgetCatchAll
      ).toBe("living");
      expect(
        normalizeSpacesConfig({
          ...RAW_CONFIG,
          widgetLibrary: "living",
          widgetCatchAll: "games",
        }).widgetCatchAll
      ).toBe("games");
    });

    it("gives Home and the library their roles", () => {
      const config = normalizeSpacesConfig({
        ...RAW_CONFIG,
        widgetLibrary: "living",
      });
      expect(resolveWidgetPlacement(config, "home")).toBe(
        WIDGET_PLACEMENTS.HOME
      );
      expect(resolveWidgetPlacement(config, "living")).toBe(
        WIDGET_PLACEMENTS.LIBRARY
      );
      expect(resolveWidgetPlacement(config, "games")).toBe(null);
      expect(resolveWidgetPlacement(CONFIG, "home")).toBe(null);
    });

    it("is active only for a thematic config with a library", () => {
      expect(isWidgetPlacementActive(thematic)).toBe(false);
      expect(
        isWidgetPlacementActive({
          ...thematic,
          "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.NOVA_FULL_WIDTH,
          trainhopConfig: {
            spaces: { ...RAW_CONFIG, widgetLibrary: "living" },
          },
        })
      ).toBe(false);
      expect(
        isWidgetPlacementActive({
          ...thematic,
          trainhopConfig: {
            spaces: { ...RAW_CONFIG, widgetLibrary: "living" },
          },
        })
      ).toBe(true);
    });

    it("reads the Home list from the pref, else the config", () => {
      const config = normalizeSpacesConfig({
        ...RAW_CONFIG,
        widgetLibrary: "living",
      });
      expect(resolveHomeWidgetIds({}, config)).toEqual(["pictureOfTheDay"]);
      expect(
        resolveHomeWidgetIds({ [PREF_HOME_WIDGETS]: '["lists", 3]' }, config)
      ).toEqual(["lists"]);
    });
  });
});

// @experiment(remove) { bug 2066527 }
describe("auto-minimize widgets variant", () => {
  const assigned = {
    "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.AUTO_MINIMIZE_WIDGETS,
  };

  it("is assigned by pref and by trainhop", () => {
    expect(isAutoMinimizeWidgetsAssigned(assigned)).toBe(true);
    expect(
      isAutoMinimizeWidgetsAssigned({
        trainhopConfig: {
          pageLayouts: {
            variant: PAGE_LAYOUT_VARIANTS.AUTO_MINIMIZE_WIDGETS,
          },
        },
      })
    ).toBe(true);
    expect(isAutoMinimizeWidgetsAssigned({})).toBe(false);
  });

  it("defaults the delay to 3s", () => {
    expect(resolveAutoMinimizeDelayMs({})).toBe(3000);
  });

  it("prefers trainhop over the pref, and the pref over the default", () => {
    expect(
      resolveAutoMinimizeDelayMs({ "pageLayouts.autoMinimizeDelayMs": 750 })
    ).toBe(750);
    expect(
      resolveAutoMinimizeDelayMs({
        "pageLayouts.autoMinimizeDelayMs": 750,
        trainhopConfig: { pageLayouts: { autoMinimizeDelayMs: 200 } },
      })
    ).toBe(200);
  });

  it("ignores non-numeric and negative values", () => {
    expect(
      resolveAutoMinimizeDelayMs({
        "pageLayouts.autoMinimizeDelayMs": 750,
        trainhopConfig: { pageLayouts: { autoMinimizeDelayMs: "200" } },
      })
    ).toBe(750);
    expect(
      resolveAutoMinimizeDelayMs({ "pageLayouts.autoMinimizeDelayMs": -1 })
    ).toBe(3000);
  });
});

// @experiment(remove) { bug 2069496 }
describe("widgets row ad variant", () => {
  const assigned = {
    "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.WIDGETS_AD_LARGE,
  };

  it("resolves by pref and by trainhop", () => {
    expect(resolvePageLayoutVariant(assigned)).toBe("widgets-ad-large");
    expect(
      resolvePageLayoutVariant({
        trainhopConfig: {
          pageLayouts: { variant: PAGE_LAYOUT_VARIANTS.WIDGETS_AD_LARGE },
        },
      })
    ).toBe("widgets-ad-large");
  });

  // It lays out as plain Nova, so it must not pick up another variant's band
  // classes or count as one of them.
  it("takes no band classes and is neither side-by-side nor spaces", () => {
    expect(sideBySideBandClasses(assigned)).toEqual([]);
    expect(spacesBandClasses(assigned)).toEqual([]);
    expect(isSideBySideAssigned(assigned)).toBe(false);
    expect(isSpacesAssigned(assigned)).toBe(false);
    expect(isAutoMinimizeWidgetsAssigned(assigned)).toBe(false);
  });
});

describe("selectFirstSlotWidget", () => {
  const assigned = {
    "nova.enabled": true,
    "feeds.section.topstories": true,
    "feeds.system.topstories": true,
    "widgets.enabled": true,
    "widgets.system.crossword.enabled": true,
    "widgets.crossword.enabled": true,
    "widgets.system.pictureOfTheDay.enabled": true,
    "widgets.pictureOfTheDay.enabled": true,
    "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.WIDGET_FIRST_CONTENT_SLOT,
    "pageLayouts.widgetFirstContentSlot.widget": "crossword",
  };

  it("returns the widget from the pref", () => {
    expect(selectFirstSlotWidget(assigned)).toBe("crossword");
  });

  it("prefers the widget from trainhop", () => {
    expect(
      selectFirstSlotWidget({
        ...assigned,
        trainhopConfig: {
          pageLayouts: {
            widgetFirstContentSlot: { widget: "pictureOfTheDay" },
          },
        },
      })
    ).toBe("pictureOfTheDay");
  });

  it.each([
    ["outside the variant", { "pageLayouts.variant": "nova-full-width" }],
    ["with no widget set", { "pageLayouts.widgetFirstContentSlot.widget": "" }],
    [
      "for an unknown widget",
      { "pageLayouts.widgetFirstContentSlot.widget": "nope" },
    ],
    ["without Nova", { "nova.enabled": false }],
    ["without stories", { "feeds.section.topstories": false }],
    ["with widgets off", { "widgets.enabled": false }],
    ["with the widget off", { "widgets.crossword.enabled": false }],
    [
      "when the widget isn't available",
      { "widgets.system.crossword.enabled": false },
    ],
  ])("returns null %s", (_, overrides) => {
    expect(selectFirstSlotWidget({ ...assigned, ...overrides })).toBe(null);
  });

  describe("under the thematic spaces variant", () => {
    const thematic = {
      ...assigned,
      "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_THEMATIC_V1,
    };

    it("honours the same setting", () => {
      expect(selectFirstSlotWidget(thematic)).toBe("crossword");
      expect(
        selectFirstSlotWidget({
          ...thematic,
          "pageLayouts.widgetFirstContentSlot.widget": "",
        })
      ).toBe(null);
    });

    // The experiment turns stories and widgets back on for a profile that had
    // them off, so the first card has to follow.
    it("follows the space overrides", () => {
      expect(
        selectFirstSlotWidget({
          ...thematic,
          "feeds.section.topstories": false,
          "widgets.enabled": false,
          trainhopConfig: {
            stories: { enabled: true },
            widgets: { enabled: true },
          },
        })
      ).toBe("crossword");
    });
  });
});

describe("per-space first-slot widgets", () => {
  const RAW = {
    order: ["home", "games", "living"],
    default: "home",
    spaces: {
      home: {
        label: "Home",
        sections: ["top_stories_section"],
        widgets: ["clocks"],
        firstSlotWidget: "pictureOfTheDay",
      },
      games: {
        label: "Games",
        sections: ["arts", "sports"],
        firstSlotWidget: "crossword",
      },
      living: { label: "Living", sections: ["food"] },
    },
  };
  const ON = {
    "nova.enabled": true,
    ...STORIES_ON,
    "widgets.system.enabled": true,
    "widgets.enabled": true,
    "widgets.system.pictureOfTheDay.enabled": true,
    "widgets.pictureOfTheDay.enabled": true,
    "widgets.system.crossword.enabled": true,
    "widgets.crossword.enabled": true,
    "widgets.system.clocks.enabled": true,
    "widgets.clocks.enabled": true,
    "pageLayouts.variant": PAGE_LAYOUT_VARIANTS.SPACES_THEMATIC_V1,
  };
  const withConfig = (raw, overrides = {}) => ({
    ...ON,
    ...overrides,
    trainhopConfig: { spaces: raw },
  });
  const prefs = withConfig(RAW);
  // No space names a widget, so the global setting applies.
  const RAW_NONE = {
    order: ["home", "living"],
    default: "home",
    spaces: {
      home: { label: "Home", sections: ["top_stories_section"] },
      living: { label: "Living", sections: ["food"] },
    },
  };

  it("takes a known widget, once", () => {
    const config = normalizeSpacesConfig({
      order: ["a", "b", "c", "d"],
      spaces: {
        a: { label: "A", firstSlotWidget: "crossword" },
        b: { label: "B", firstSlotWidget: "crossword" },
        c: { label: "C", firstSlotWidget: "nope" },
        d: { label: "D" },
      },
    });
    expect(config.order.map(id => config.spaces[id].firstSlotWidget)).toEqual([
      "crossword",
      null,
      null,
      null,
    ]);
  });

  it("gives each space that names one its own", () => {
    expect(resolveFirstSlotWidgets(prefs)).toEqual([
      { spaceId: "home", widgetId: "pictureOfTheDay" },
      { spaceId: "games", widgetId: "crossword" },
    ]);
  });

  it("puts each in the first section of its space", () => {
    expect(
      resolveFirstSlotWidgetsBySection(prefs, [
        "top_stories_section",
        "sports",
        "food",
        "arts",
      ])
    ).toEqual({
      top_stories_section: "pictureOfTheDay",
      sports: "crossword",
    });
  });

  it("puts a widget named on another space there, not on Home", () => {
    const living = withConfig({
      ...RAW_NONE,
      spaces: {
        ...RAW_NONE.spaces,
        living: { ...RAW_NONE.spaces.living, firstSlotWidget: "crossword" },
      },
    });
    expect(
      resolveFirstSlotWidgetsBySection(living, ["top_stories_section", "food"])
    ).toEqual({ food: "crossword" });
  });

  it("skips a space with no section in the feed", () => {
    expect(
      resolveFirstSlotWidgetsBySection(prefs, ["top_stories_section", "food"])
    ).toEqual({ top_stories_section: "pictureOfTheDay" });
  });

  it("leaves out a widget that is off", () => {
    expect(
      resolveFirstSlotWidgets({ ...prefs, "widgets.crossword.enabled": false })
    ).toEqual([{ spaceId: "home", widgetId: "pictureOfTheDay" }]);
  });

  it("shows none without stories", () => {
    expect(
      resolveFirstSlotWidgets({ ...prefs, "feeds.section.topstories": false })
    ).toEqual([]);
  });

  describe("the global setting", () => {
    const GLOBAL = { "pageLayouts.widgetFirstContentSlot.widget": "clocks" };

    it("is ignored once a space names one", () => {
      expect(
        selectFirstSlotWidgetIds(withConfig(RAW, GLOBAL)).has("clocks")
      ).toBe(false);
    });

    it("goes to the top section when no space names one", () => {
      const global = withConfig(RAW_NONE, GLOBAL);
      expect(resolveFirstSlotWidgets(global)).toEqual([
        { spaceId: null, widgetId: "clocks" },
      ]);
      expect(
        resolveFirstSlotWidgetsBySection(global, [
          "food",
          "top_stories_section",
        ])
      ).toEqual({ food: "clocks" });
    });

    it("is the only one outside the thematic variant", () => {
      expect(
        resolveFirstSlotWidgets(
          withConfig(RAW, {
            ...GLOBAL,
            "pageLayouts.variant":
              PAGE_LAYOUT_VARIANTS.WIDGET_FIRST_CONTENT_SLOT,
          })
        )
      ).toEqual([{ spaceId: null, widgetId: "clocks" }]);
    });
  });

  it("keeps them out of every widgets column", () => {
    const config = normalizeSpacesConfig(RAW);
    for (const id of config.order) {
      const ids = resolveThematicSpaceWidgets(config, id, prefs);
      expect(ids).not.toContain("pictureOfTheDay");
      expect(ids).not.toContain("crossword");
    }
    expect(resolveThematicSpaceWidgets(config, "home", prefs)).toEqual([
      "clocks",
    ]);
  });

  it("shows each at the large size", () => {
    const size = id =>
      resolveWidgetSize(
        WIDGET_REGISTRY.find(w => w.id === id),
        prefs
      );
    expect(size("pictureOfTheDay")).toBe("large");
    expect(size("crossword")).toBe("large");
    expect(size("clocks")).not.toBe("large");
  });
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { render, fireEvent, act } from "@testing-library/react";
import { actionTypes as at } from "common/Actions.mjs";
import { _WallpaperCategories as WallpaperCategories } from "content-src/components/WallpaperCategories/WallpaperCategories";

const UUID_ONE = "550e8400-e29b-41d4-a716-446655440000";
const UUID_TWO = "7f2a1c93-4d1e-4a8c-9f3b-2e6d5a1b0c74";
const UUID_THREE = "9b4e7d21-3f8a-4c2e-b1d6-8a5f0e7c9b23";

const DEFAULT_PROPS = {
  Prefs: {
    values: {
      "newtabWallpapers.wallpaper": "celestial",
      "newtabWallpapers.initialWallpaper": "celestial",
      "newtabWallpapers.customWallpaper.uploadedPreviously": false,
      "newtabWallpapers.customWallpaper.fileSize": 2,
      "newtabWallpapers.customWallpaper.fileSize.enabled": false,
    },
  },
  Wallpapers: {
    wallpaperList: [{ title: "moon", category: "celestial", theme: "light" }],
    categories: ["celestial", "solid-colors"],
    uploadedWallpaper: null,
  },
  activeWallpaper: "celestial",
  setPref: jest.fn(),
  dispatch: jest.fn(),
  showPanel: false,
  activeCategory: null,
  openPanel: jest.fn(),
  closePanel: jest.fn(),
};

// Base.jsx owns the subpanel state in production. This harness stands in for
// it: openPanel records the category and shows the panel, closePanel hides
// the panel but keeps the category, exactly as Base does.
const Harness = React.forwardRef(function Harness(
  { onOpenPanel, onClosePanel, ...props },
  ref
) {
  const [showPanel, setShowPanel] = React.useState(false);
  const [activeCategory, setActiveCategory] = React.useState(null);
  const openPanel = categoryId => {
    onOpenPanel?.(categoryId);
    setActiveCategory(categoryId);
    setShowPanel(true);
  };
  const closePanel = () => {
    onClosePanel?.();
    setShowPanel(false);
  };
  return (
    <WallpaperCategories
      ref={ref}
      {...props}
      showPanel={showPanel}
      activeCategory={activeCategory}
      openPanel={openPanel}
      closePanel={closePanel}
    />
  );
});

describe("<WallpaperCategories>", () => {
  beforeEach(() => {
    jest.clearAllMocks();
  });

  it("removes the saved image on reset when the library is off", () => {
    // Pre-library behavior: with no folder to see or delete an image in,
    // resetting has to take it, or it is stranded on disk for good.
    const props = {
      ...DEFAULT_PROPS,
      Prefs: {
        values: {
          ...DEFAULT_PROPS.Prefs.values,
          "newtabWallpapers.wallpaper": "custom",
          "newtabWallpapers.customWallpaper.library.enabled": false,
        },
      },
    };
    const { container } = render(<WallpaperCategories {...props} />);
    fireEvent.click(container.querySelector(".wallpapers-reset"));

    expect(
      props.dispatch.mock.calls.some(
        ([action]) => action.type === at.WALLPAPER_REMOVE_UPLOAD
      )
    ).toBe(true);
  });

  it("keeps the saved image on reset when the library is on", () => {
    const props = {
      ...DEFAULT_PROPS,
      Prefs: {
        values: {
          ...DEFAULT_PROPS.Prefs.values,
          "newtabWallpapers.wallpaper": "custom",
          "newtabWallpapers.customWallpaper.library.enabled": true,
        },
      },
    };
    const { container } = render(<WallpaperCategories {...props} />);
    fireEvent.click(container.querySelector(".wallpapers-reset"));

    expect(
      props.dispatch.mock.calls.some(
        ([action]) => action.type === at.WALLPAPER_REMOVE_UPLOAD
      )
    ).toBe(false);
  });

  it("should clear initialWallpaper when the wallpaper is removed", () => {
    const { container } = render(<WallpaperCategories {...DEFAULT_PROPS} />);
    fireEvent.click(container.querySelector(".wallpapers-reset"));
    expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
      "newtabWallpapers.initialWallpaper",
      ""
    );
  });

  it("should clear initialWallpaper when a wallpaper is set", () => {
    const { container } = render(<Harness {...DEFAULT_PROPS} />);
    fireEvent.click(container.querySelector("#celestial"));
    fireEvent.click(container.querySelector("#moon"));
    expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
      "newtabWallpapers.wallpaper",
      "moon"
    );
    expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
      "newtabWallpapers.initialWallpaper",
      ""
    );
    expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
      "newtabWallpapers.user.enabled",
      true
    );
  });

  it("should not render the reset button when Nova is enabled", () => {
    const novaProps = {
      ...DEFAULT_PROPS,
      Prefs: {
        values: {
          ...DEFAULT_PROPS.Prefs.values,
          "nova.enabled": true,
        },
      },
    };
    const { container } = render(<WallpaperCategories {...novaProps} />);
    expect(
      container.querySelector(".wallpapers-reset")
    ).not.toBeInTheDocument();
  });

  it("should render the reset button when Nova is disabled", () => {
    const { container } = render(<WallpaperCategories {...DEFAULT_PROPS} />);
    expect(container.querySelector(".wallpapers-reset")).toBeInTheDocument();
  });

  it("does not offer wallpapers with visible set to false", () => {
    const props = {
      ...DEFAULT_PROPS,
      Wallpapers: {
        ...DEFAULT_PROPS.Wallpapers,
        wallpaperList: [
          { title: "moon", category: "celestial", theme: "light" },
          {
            title: "stars",
            category: "celestial",
            theme: "dark",
            visible: true,
          },
          {
            title: "retired",
            category: "celestial",
            theme: "light",
            visible: false,
          },
        ],
      },
    };
    const { container } = render(<Harness {...props} />);
    fireEvent.click(container.querySelector("#celestial"));

    expect(container.querySelector("#moon")).toBeInTheDocument();
    expect(container.querySelector("#stars")).toBeInTheDocument();
    expect(container.querySelector("#retired")).not.toBeInTheDocument();
    expect(
      container.querySelectorAll('input[type="radio"].wallpaper-input')
    ).toHaveLength(2);
  });

  describe("visibility_group", () => {
    const GATED_LIST = [
      { title: "moon", category: "celestial", theme: "light" },
      {
        title: "soccer-ball",
        category: "celestial",
        theme: "dark",
        visibility_group: "soccer",
      },
    ];

    const gatedProps = (groupValues = {}) => ({
      ...DEFAULT_PROPS,
      Prefs: { values: { ...DEFAULT_PROPS.Prefs.values, ...groupValues } },
      Wallpapers: { ...DEFAULT_PROPS.Wallpapers, wallpaperList: GATED_LIST },
    });

    const openCelestial = props => {
      const { container } = render(<Harness {...props} />);
      fireEvent.click(container.querySelector("#celestial"));
      return container;
    };

    it("hides a gated wallpaper when no group is active", () => {
      const container = openCelestial(gatedProps());
      expect(container.querySelector("#moon")).toBeInTheDocument();
      expect(container.querySelector("#soccer-ball")).not.toBeInTheDocument();
    });

    it("offers a gated wallpaper when the pref names its group", () => {
      const container = openCelestial(
        gatedProps({ "newtabWallpapers.visibilityGroups": "soccer" })
      );
      expect(container.querySelector("#soccer-ball")).toBeInTheDocument();
    });

    it("parses a multi-group pref and trims whitespace", () => {
      const container = openCelestial(
        gatedProps({ "newtabWallpapers.visibilityGroups": "worldcup, soccer" })
      );
      expect(container.querySelector("#soccer-ball")).toBeInTheDocument();
    });

    it("lets trainhopConfig activate a group with the pref unset", () => {
      const container = openCelestial(
        gatedProps({
          "newtabWallpapers.visibilityGroups": "",
          trainhopConfig: { wallpapers: { visibilityGroups: "soccer" } },
        })
      );
      expect(container.querySelector("#soccer-ball")).toBeInTheDocument();
    });

    it("ignores a malformed trainhop value and falls back to the pref", () => {
      const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
      const container = openCelestial(
        gatedProps({
          "newtabWallpapers.visibilityGroups": "soccer",
          trainhopConfig: { wallpapers: { visibilityGroups: ["soccer"] } },
        })
      );

      // Renders rather than throwing, and the pref still activates the group.
      expect(container.querySelector("#soccer-ball")).toBeInTheDocument();
      expect(warn).toHaveBeenCalled();
      warn.mockRestore();
    });

    it("does not throw when the trainhop value is a non-string and no pref is set", () => {
      const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
      const container = openCelestial(
        gatedProps({
          trainhopConfig: { wallpapers: { visibilityGroups: 5 } },
        })
      );

      expect(container.querySelector("#moon")).toBeInTheDocument();
      expect(container.querySelector("#soccer-ball")).not.toBeInTheDocument();
      warn.mockRestore();
    });

    it("keeps hiding a gated wallpaper that is also visible: false", () => {
      const props = {
        ...DEFAULT_PROPS,
        Prefs: {
          values: {
            ...DEFAULT_PROPS.Prefs.values,
            "newtabWallpapers.visibilityGroups": "soccer",
          },
        },
        Wallpapers: {
          ...DEFAULT_PROPS.Wallpapers,
          wallpaperList: [
            { title: "moon", category: "celestial", theme: "light" },
            {
              title: "soccer-ball",
              category: "celestial",
              theme: "dark",
              visibility_group: "soccer",
              visible: false,
            },
          ],
        },
      };
      const container = openCelestial(props);
      expect(container.querySelector("#soccer-ball")).not.toBeInTheDocument();
    });

    it("leaves ungated wallpapers alone whatever the groups say", () => {
      const container = openCelestial(
        gatedProps({ "newtabWallpapers.visibilityGroups": "unrelated" })
      );
      expect(container.querySelector("#moon")).toBeInTheDocument();
    });

    it("hides a gated wallpaper even while it is the selected one", () => {
      const props = {
        ...gatedProps(),
        activeWallpaper: "soccer-ball",
      };
      props.Prefs.values["newtabWallpapers.wallpaper"] = "soccer-ball";
      const container = openCelestial(props);
      expect(container.querySelector("#soccer-ball")).not.toBeInTheDocument();
    });

    describe("category with only gated wallpapers", () => {
      const onlyGatedProps = (groupValues = {}) => ({
        ...gatedProps(groupValues),
        Wallpapers: {
          ...DEFAULT_PROPS.Wallpapers,
          wallpaperList: [
            ...GATED_LIST,
            {
              title: "pitch",
              category: "photographs",
              theme: "dark",
              visibility_group: "soccer",
            },
          ],
          categories: ["custom-wallpaper", "celestial", "photographs"],
        },
      });

      it("hides the category when no group is active", () => {
        const { container } = render(<Harness {...onlyGatedProps()} />);
        expect(container.querySelector("#photographs")).not.toBeInTheDocument();
        expect(container.querySelector("#celestial")).toBeInTheDocument();
        expect(
          container.querySelector("#custom-wallpaper")
        ).toBeInTheDocument();
      });

      it("shows the category when its group is active", () => {
        const { container } = render(
          <Harness
            {...onlyGatedProps({
              "newtabWallpapers.visibilityGroups": "soccer",
            })}
          />
        );
        expect(container.querySelector("#photographs")).toBeInTheDocument();
      });

      it("hides a category whose wallpapers are all visible: false", () => {
        const props = onlyGatedProps();
        props.Wallpapers.wallpaperList = [
          ...GATED_LIST,
          { title: "pitch", category: "photographs", visible: false },
        ];
        const { container } = render(<Harness {...props} />);
        expect(container.querySelector("#photographs")).not.toBeInTheDocument();
      });

      it("keeps solid colors while the color picker is on", () => {
        const props = onlyGatedProps({
          "newtabWallpapers.customColor.enabled": true,
        });
        props.Wallpapers.categories = ["celestial", "solid-colors"];
        const { container } = render(<Harness {...props} />);
        expect(container.querySelector("#solid-colors")).toBeInTheDocument();
      });

      it("keeps solid colors while a custom color is applied", () => {
        const props = onlyGatedProps({
          "newtabWallpapers.customColor.enabled": false,
          "newtabWallpapers.wallpaper": "solid-color-picker-#123456",
        });
        props.Wallpapers.categories = ["celestial", "solid-colors"];
        const { container } = render(<Harness {...props} />);
        expect(container.querySelector("#solid-colors")).toBeInTheDocument();
      });

      it("hides solid colors when it has no colors and no picker", () => {
        const props = onlyGatedProps({
          "newtabWallpapers.customColor.enabled": false,
        });
        props.Wallpapers.categories = ["celestial", "solid-colors"];
        const { container } = render(<Harness {...props} />);
        expect(
          container.querySelector("#solid-colors")
        ).not.toBeInTheDocument();
      });

      it("ignores a deep link to the hidden category", () => {
        const onOpenPanel = jest.fn();
        const props = { ...onlyGatedProps(), onOpenPanel };
        const { rerender } = render(<Harness {...props} />);
        rerender(
          <Harness {...props} customizePanelWallpaperCategory="photographs" />
        );
        expect(onOpenPanel).not.toHaveBeenCalled();
      });
    });
  });

  describe("category order and cap", () => {
    const ALL = [
      "custom-wallpaper",
      "firefox",
      "abstracts",
      "celestial",
      "photographs",
      "solid-colors",
      "partner",
    ];

    const orderProps = (prefs = {}) => ({
      ...DEFAULT_PROPS,
      Prefs: { values: { ...DEFAULT_PROPS.Prefs.values, ...prefs } },
      Wallpapers: {
        ...DEFAULT_PROPS.Wallpapers,
        wallpaperList: ALL.filter(c => c !== "custom-wallpaper").map(c => ({
          title: `${c}-1`,
          category: c,
          theme: "light",
        })),
        categories: ALL,
      },
    });

    const shownCategories = props => {
      const { container } = render(<Harness {...props} />);
      return [
        ...container.querySelectorAll(".category-list button.wallpaper-input"),
      ].map(button => button.id);
    };

    it("keeps the feed order without a pref", () => {
      expect(shownCategories(orderProps())).toEqual(ALL);
    });

    it("puts listed categories first and the rest after, in feed order", () => {
      expect(
        shownCategories(
          orderProps({
            "newtabWallpapers.categoryOrder": "partner, celestial",
          })
        )
      ).toEqual([
        "partner",
        "celestial",
        "custom-wallpaper",
        "firefox",
        "abstracts",
        "photographs",
        "solid-colors",
      ]);
    });

    it("lets trainhopConfig set the order", () => {
      expect(
        shownCategories(
          orderProps({
            "newtabWallpapers.categoryOrder": "celestial",
            trainhopConfig: { wallpapers: { categoryOrder: "partner" } },
          })
        )[0]
      ).toBe("partner");
    });

    it("does not cap the classic layout", () => {
      expect(
        shownCategories(orderProps({ "newtabWallpapers.maxCategories": 2 }))
      ).toHaveLength(ALL.length);
    });

    it("caps Nova at 6 by default, dropping the last category", () => {
      expect(shownCategories(orderProps({ "nova.enabled": true }))).toEqual(
        ALL.slice(0, 6)
      );
    });

    it("lets a listed category replace one at the end", () => {
      expect(
        shownCategories(
          orderProps({
            "nova.enabled": true,
            "newtabWallpapers.categoryOrder":
              "custom-wallpaper,firefox,abstracts,celestial,photographs,partner",
          })
        )
      ).toEqual([
        "custom-wallpaper",
        "firefox",
        "abstracts",
        "celestial",
        "photographs",
        "partner",
      ]);
    });

    it("always keeps Your images, even when it sorts past the cap", () => {
      expect(
        shownCategories(
          orderProps({
            "nova.enabled": true,
            "newtabWallpapers.maxCategories": 2,
            "newtabWallpapers.categoryOrder": "firefox,abstracts,celestial",
          })
        )
      ).toEqual(["firefox", "custom-wallpaper"]);
    });

    it("lets trainhopConfig set the cap", () => {
      expect(
        shownCategories(
          orderProps({
            "nova.enabled": true,
            "newtabWallpapers.maxCategories": 6,
            trainhopConfig: { wallpapers: { maxCategories: 3 } },
          })
        )
      ).toHaveLength(3);
    });

    it("falls back to the pref, then 6, for an invalid cap", () => {
      for (const maxCategories of [0, -1, 2.5, "4"]) {
        expect(
          shownCategories(
            orderProps({
              "nova.enabled": true,
              "newtabWallpapers.maxCategories": 4,
              trainhopConfig: { wallpapers: { maxCategories } },
            })
          )
        ).toHaveLength(4);
        expect(
          shownCategories(
            orderProps({
              "nova.enabled": true,
              "newtabWallpapers.maxCategories": maxCategories,
            })
          )
        ).toHaveLength(6);
      }
    });
  });

  it("opens the requested category when deep-linked via App state", () => {
    const onOpenPanel = jest.fn();
    const props = {
      ...DEFAULT_PROPS,
      Wallpapers: {
        ...DEFAULT_PROPS.Wallpapers,
        wallpaperList: [
          ...DEFAULT_PROPS.Wallpapers.wallpaperList,
          { title: "black", category: "solid-colors", theme: "dark" },
          { title: "fox", category: "firefox", theme: "light" },
        ],
        categories: ["celestial", "solid-colors", "firefox"],
      },
      customizePanelWallpaperCategory: null,
    };
    const { container, rerender } = render(
      <Harness {...props} onOpenPanel={onOpenPanel} />
    );
    expect(onOpenPanel).not.toHaveBeenCalled();

    act(() => {
      rerender(
        <Harness
          {...props}
          onOpenPanel={onOpenPanel}
          customizePanelWallpaperCategory="firefox"
        />
      );
    });

    expect(onOpenPanel).toHaveBeenCalledWith("firefox");
    expect(
      container.querySelector(".wallpaper-list .arrow-button")
    ).toHaveAttribute(
      "data-l10n-id",
      "newtab-wallpaper-category-title-firefox"
    );
    expect(DEFAULT_PROPS.dispatch).not.toHaveBeenCalledWith(
      expect.objectContaining({ type: at.WALLPAPER_CATEGORY_CLICK })
    );
  });

  it("ignores a deep-linked category that is unavailable", () => {
    const onOpenPanel = jest.fn();
    const props = {
      ...DEFAULT_PROPS,
      customizePanelWallpaperCategory: null,
    };
    const { rerender } = render(
      <Harness {...props} onOpenPanel={onOpenPanel} />
    );

    act(() => {
      rerender(
        <Harness
          {...props}
          onOpenPanel={onOpenPanel}
          customizePanelWallpaperCategory="firefox"
        />
      );
    });

    // "firefox" is not in DEFAULT_PROPS categories, so nothing should open.
    expect(onOpenPanel).not.toHaveBeenCalled();
  });

  it("opens the panel and records a category click when a tile is clicked", () => {
    const onOpenPanel = jest.fn();
    const { container } = render(
      <Harness {...DEFAULT_PROPS} onOpenPanel={onOpenPanel} />
    );
    fireEvent.click(container.querySelector("#celestial"));
    expect(onOpenPanel).toHaveBeenCalledWith("celestial");
    expect(DEFAULT_PROPS.dispatch).toHaveBeenCalledWith(
      expect.objectContaining({
        type: at.WALLPAPER_CATEGORY_CLICK,
        data: "celestial",
      })
    );
  });

  it("closes the panel and refocuses the tile on back", () => {
    const onClosePanel = jest.fn();
    const originalRAF = globalThis.requestAnimationFrame;
    globalThis.requestAnimationFrame = cb => {
      cb();
      return 0;
    };
    try {
      const { container } = render(
        <Harness {...DEFAULT_PROPS} onClosePanel={onClosePanel} />
      );
      fireEvent.click(container.querySelector("#celestial"));

      fireEvent.click(container.querySelector(".wallpaper-list .arrow-button"));
      expect(onClosePanel).toHaveBeenCalledTimes(1);
      // The synchronous requestAnimationFrame stub makes the refocus observable straight away.
      expect(document.activeElement).toBe(
        container.querySelector("#celestial")
      );
    } finally {
      globalThis.requestAnimationFrame = originalRAF;
    }
  });

  it("opens the new category when switching while open", () => {
    const onOpenPanel = jest.fn();
    const props = {
      ...DEFAULT_PROPS,
      Wallpapers: {
        ...DEFAULT_PROPS.Wallpapers,
        wallpaperList: [
          ...DEFAULT_PROPS.Wallpapers.wallpaperList,
          { title: "black", category: "solid-colors", theme: "dark" },
        ],
      },
    };
    const { container } = render(
      <Harness {...props} onOpenPanel={onOpenPanel} />
    );
    fireEvent.click(container.querySelector("#celestial"));
    fireEvent.click(container.querySelector("#solid-colors"));
    expect(onOpenPanel).toHaveBeenLastCalledWith("solid-colors");
  });

  it("keeps the category title on the heading after back, for the exit animation", () => {
    const originalRAF = globalThis.requestAnimationFrame;
    globalThis.requestAnimationFrame = cb => {
      cb();
      return 0;
    };
    try {
      const { container } = render(<Harness {...DEFAULT_PROPS} />);
      fireEvent.click(container.querySelector("#celestial"));
      fireEvent.click(container.querySelector(".wallpaper-list .arrow-button"));
      // The harness keeps activeCategory as Base does, so the heading keeps
      // its title while the CSSTransition plays the exit.
      expect(
        container.querySelector(".wallpaper-list .arrow-button")
      ).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-category-title-celestial"
      );
    } finally {
      globalThis.requestAnimationFrame = originalRAF;
    }
  });

  it("names the back button and keeps the category title as a heading", () => {
    const novaProps = {
      ...DEFAULT_PROPS,
      Prefs: {
        values: {
          ...DEFAULT_PROPS.Prefs.values,
          "nova.enabled": true,
        },
      },
    };
    const { container } = render(<Harness {...novaProps} />);
    fireEvent.click(container.querySelector("#celestial"));

    const wrapper = container.querySelector(
      ".wallpaper-list.category .arrow-wrapper"
    );
    expect(wrapper).toBeInTheDocument();

    const backButton = wrapper.querySelector("moz-button.arrow-button");
    expect(backButton).toHaveAttribute(
      "data-l10n-id",
      "newtab-customize-panel-back-button"
    );
    expect(wrapper.querySelector("h2")).toHaveAttribute(
      "data-l10n-id",
      "newtab-wallpaper-category-title-celestial"
    );
  });

  it("should not delete the saved image when the wallpaper is reset", () => {
    const { container } = render(<WallpaperCategories {...DEFAULT_PROPS} />);
    fireEvent.click(container.querySelector(".wallpapers-reset"));
    expect(DEFAULT_PROPS.dispatch).not.toHaveBeenCalledWith(
      expect.objectContaining({
        type: at.WALLPAPER_REMOVE_UPLOAD,
      })
    );
  });

  describe("Your images", () => {
    const SAVED = [
      {
        filename: `v1-custom-light-center-1-${UUID_ONE}`,
        theme: "light",
        position: "center",
        type: "custom",
        number: 1,
      },
      {
        filename: `v1-builtin-dark-topright-1-${UUID_TWO}`,
        theme: "dark",
        position: "top right",
        type: "builtin",
        number: 2,
        fallbackName: "dark-mountain",
      },
    ];

    const tileUrl = filename => `moz-newtab-wallpaper://library/${filename}`;

    // Both the folder tile and the tiles inside it draw their picture with an
    // img, so a CSS background does not hold a full resolution decode.
    const previewOf = element =>
      element.querySelector(".your-images-preview") ??
      element.parentElement.querySelector(".your-images-preview");

    const withSavedWallpapers = (customWallpapers = SAVED, values = {}) => ({
      ...DEFAULT_PROPS,
      // The picker only does library work while the panel is open, and it
      // fails closed, so every test here has to say the panel is showing.
      panelShowing: true,
      Prefs: {
        values: {
          ...DEFAULT_PROPS.Prefs.values,
          "newtabWallpapers.user.enabled": true,
          "newtabWallpapers.customWallpaper.library.enabled": true,
          ...values,
        },
      },
      Wallpapers: {
        ...DEFAULT_PROPS.Wallpapers,
        categories: ["custom-wallpaper", "celestial"],
        customWallpapers,
      },
    });

    it("keeps the add an image tile until something is saved", () => {
      const { container } = render(<Harness {...withSavedWallpapers([])} />);
      const tile = container.querySelector("#custom-wallpaper");
      expect(tile).toHaveClass("theme-custom-wallpaper");
      expect(
        container.querySelector('label[for="custom-wallpaper"]')
      ).toHaveAttribute("data-l10n-id", "newtab-wallpaper-upload-image");
    });

    it("keeps the single tile when the library is off", () => {
      const props = withSavedWallpapers(SAVED, {
        "newtabWallpapers.customWallpaper.library.enabled": false,
      });

      const { container } = render(<Harness {...props} />);
      const tile = container.querySelector("#custom-wallpaper");
      expect(tile).toHaveClass("theme-custom-wallpaper");
      expect(tile).not.toHaveClass("your-images-folder");
      expect(
        container.querySelector('label[for="custom-wallpaper"]')
      ).not.toHaveAttribute("data-l10n-id", "newtab-wallpaper-your-images");
    });

    it("shows the applied image even before the library catches up", () => {
      // The pref and the wallpaper URL reach the page ahead of the library. If
      // the tile waited for the library list it would show the previous image,
      // which is worse than showing nothing.
      const props = withSavedWallpapers();
      const { container } = render(
        <Harness
          {...props}
          Prefs={{
            values: {
              ...props.Prefs.values,
              "newtabWallpapers.wallpaper": "custom",
              // Not in the library the page holds yet.
              "newtabWallpapers.customWallpaper.uuid": `v1-custom-light-center-1-${UUID_THREE}`,
              "newtabWallpapers.customWallpaper.position": "top right",
            },
          }}
          Wallpapers={{
            ...props.Wallpapers,
            uploadedWallpaper: "moz-newtab-wallpaper://the-new-one",
          }}
        />
      );

      const tile = container.querySelector("#custom-wallpaper");
      const preview = previewOf(tile);
      expect(preview).toHaveAttribute(
        "src",
        "moz-newtab-wallpaper://the-new-one"
      );
      expect(preview.style.objectPosition).toBe("top right");
      expect(tile).not.toHaveClass("theme-custom-wallpaper");
    });

    it("turns the tile into a folder once an image is saved", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      const tile = container.querySelector("#custom-wallpaper");
      expect(tile).not.toHaveClass("theme-custom-wallpaper");
      expect(tile).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-your-images-folder"
      );
      expect(previewOf(tile)).toHaveAttribute(
        "src",
        tileUrl(SAVED[0].filename)
      );
      expect(
        container.querySelector('label[for="custom-wallpaper"]')
      ).toHaveAttribute("data-l10n-id", "newtab-wallpaper-your-images");
    });

    it("shows the applied image on the folder", () => {
      const props = withSavedWallpapers(SAVED, {
        "newtabWallpapers.wallpaper": "custom",
        "newtabWallpapers.customWallpaper.uuid": SAVED[1].filename,
      });
      const { container } = render(<Harness {...props} />);
      const tile = container.querySelector("#custom-wallpaper");
      expect(previewOf(tile)).toHaveAttribute(
        "src",
        tileUrl(SAVED[1].filename)
      );
      expect(tile).toHaveClass("selected");
    });

    it("lists every saved image and a tile to add another", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const images = container.querySelectorAll(
        '.your-images input[type="radio"]'
      );
      expect(images).toHaveLength(SAVED.length);
      expect(previewOf(images[0])).toHaveAttribute(
        "src",
        tileUrl(SAVED[0].filename)
      );
      expect(previewOf(images[1]).style.objectPosition).toBe("top right");
      expect(container.querySelector("#your-images-add")).toHaveClass(
        "theme-custom-wallpaper"
      );
    });

    it("keeps the pictures out of the accessibility tree", () => {
      // The radio and the folder button carry the name, so a second one off
      // the picture would be read out twice.
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      const folder = previewOf(container.querySelector("#custom-wallpaper"));
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const previews = [
        folder,
        ...container.querySelectorAll(".your-images-item .your-images-preview"),
      ];
      expect(previews).toHaveLength(SAVED.length + 1);
      for (const preview of previews) {
        expect(preview.tagName).toBe("IMG");
        expect(preview).toHaveAttribute("alt", "");
        expect(preview).toHaveAttribute("aria-hidden", "true");
        expect(preview).toHaveAttribute("draggable", "false");
      }
    });

    it("loads the folder preview eagerly and saved previews lazily", () => {
      // A library can run to a hundred photos. Fetching the ones below the
      // fold on open doubled what the picker cost. The folder tile is always
      // on screen, so it stays eager.
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      const folder = previewOf(container.querySelector("#custom-wallpaper"));
      expect(folder).not.toHaveAttribute("loading");

      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const inFolder = container.querySelectorAll(
        ".your-images-item .your-images-preview"
      );
      expect(inFolder).toHaveLength(SAVED.length);
      for (const preview of inFolder) {
        expect(preview).toHaveAttribute("loading", "lazy");
      }
    });

    it("names each saved image and the add tile once", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      for (const { filename } of SAVED) {
        const id = `your-images-${filename}`;
        const radio = container.querySelector(`[id="${id}"]`);
        const label = container.querySelector(`[id="${id}-label"]`);
        expect(radio).toHaveAttribute("aria-labelledby", `${id}-label`);
        expect(label).toHaveAttribute("for", id);
        expect(label).toHaveAttribute("aria-hidden", "true");
      }

      const add = container.querySelector("#your-images-add");
      expect(add).toHaveAttribute("aria-labelledby", "your-images-add-label");
      expect(container.querySelector("#your-images-add-label")).toHaveAttribute(
        "aria-hidden",
        "true"
      );
    });

    it("keeps the folder tile's Fluent name instead of the label", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      const tile = container.querySelector("#custom-wallpaper");
      expect(tile).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-your-images-folder"
      );
      expect(tile).not.toHaveAttribute("aria-labelledby");
      expect(container.querySelector("#celestial")).toHaveAttribute(
        "aria-labelledby",
        "celestial-label"
      );
    });

    it("asks before removing a saved image", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const removeButtons = container.querySelectorAll(".your-images-remove");
      expect(removeButtons).toHaveLength(SAVED.length);
      // An image someone added has no name of its own, so its button is named
      // by the number the image is known by.
      expect(removeButtons[0]).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-remove-image-numbered"
      );

      expect(removeButtons[0]).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ number: SAVED[0].number })
      );
      // A rescued Firefox wallpaper has no file name, so its button is named
      // by the title stored when it was rescued.
      expect(removeButtons[1]).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-remove-image"
      );
      expect(removeButtons[1]).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ name: SAVED[1].fallbackName })
      );

      fireEvent.click(removeButtons[1]);

      const [dialog] = DEFAULT_PROPS.dispatch.mock.calls.find(
        ([action]) => action.type === at.DIALOG_OPEN
      );
      expect(dialog.data.onConfirm[0]).toEqual(
        expect.objectContaining({
          type: at.WALLPAPER_REMOVE_UPLOAD,
          // Just the filename. The parent records the telemetry once the
          // removal has actually happened, with the counts it really has.
          data: { filename: SAVED[1].filename },
        })
      );
      expect(dialog.data.body_string_id).toEqual([
        "newtab-wallpaper-remove-image-title",
        "newtab-wallpaper-remove-image-body",
      ]);
    });

    it("restores focus when another image is added during removal", () => {
      const props = withSavedWallpapers();
      const ref = React.createRef();
      const { container, rerender } = render(<Harness {...props} ref={ref} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const remove = container.querySelector(".your-images-remove");
      act(() => {
        remove.focus();
        fireEvent.click(remove);
      });

      const replacement = {
        filename: `v1-custom-light-center-4-${UUID_THREE}`,
        theme: "light",
        position: "center",
        type: "custom",
        number: 4,
      };
      act(() => {
        rerender(
          <Harness
            {...props}
            Wallpapers={{
              ...props.Wallpapers,
              customWallpapers: [replacement, SAVED[1]],
            }}
            ref={ref}
          />
        );
      });

      expect(document.activeElement).toBe(
        container.querySelector('.your-images input[type="radio"]')
      );
      expect(ref.current.state.pendingRemoveFilename).toBeNull();
    });

    it("names each remove button after the tile it belongs to", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const items = [...container.querySelectorAll(".your-images-item")];
      // The last item adds an image and has no remove button. Named by the
      // number it was given, so removing another cannot rename the ones left.
      const [numbered, unnamed] = items;

      expect(numbered.querySelector("label.sr-only")).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-your-images-item-numbered"
      );
      expect(numbered.querySelector("label.sr-only")).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ number: SAVED[0].number })
      );
      expect(numbered.querySelector(".your-images-remove")).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ number: SAVED[0].number })
      );

      // A rescued Firefox wallpaper keeps the title it was rescued with.
      expect(unnamed.querySelector("label.sr-only")).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-your-images-item"
      );
      expect(unnamed.querySelector("label.sr-only")).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ name: SAVED[1].fallbackName })
      );
    });

    it("moves real focus to a new upload when focus was in the folder", () => {
      const props = withSavedWallpapers();
      const ref = React.createRef();
      const { container, rerender } = render(<Harness {...props} ref={ref} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      // Uploading starts from the add tile, so that is what holds focus when
      // the parent replies with the saved filename.
      const add = container.querySelector("#your-images-add");
      act(() => {
        add.click();
        add.focus();
        // A validated file normally creates this request before dispatch.
        ref.current.pendingUploadId = "request-1";
      });
      expect(document.activeElement).toBe(add);

      const uploaded = {
        filename: `v1-custom-light-center-1-${UUID_THREE}`,
        theme: "light",
        position: "center",
        type: "custom",
        number: 3,
      };

      act(() => {
        rerender(
          <Harness
            {...props}
            Wallpapers={{
              ...props.Wallpapers,
              customWallpapers: [uploaded, ...SAVED],
              uploadResult: {
                requestId: "request-1",
                filename: uploaded.filename,
              },
            }}
            ref={ref}
          />
        );
      });

      const images = container.querySelectorAll(
        '.your-images input[type="radio"]'
      );
      expect(document.activeElement).toBe(images[0]);

      const stops = [
        ...container.querySelectorAll(".your-images .wallpaper-input"),
      ].filter(element => element.tabIndex === 0);
      expect(stops).toEqual([images[0]]);
    });

    it("does not arm upload focus while the file picker is open", () => {
      const ref = React.createRef();
      const { container } = render(
        <Harness {...withSavedWallpapers()} ref={ref} />
      );
      fireEvent.click(container.querySelector("#custom-wallpaper"));
      fireEvent.click(container.querySelector("#your-images-add"));

      expect(ref.current.pendingUploadId).toBeNull();
    });

    it("clears upload focus when the parent reports a failure", () => {
      const props = withSavedWallpapers();
      const ref = React.createRef();
      const { rerender } = render(<Harness {...props} ref={ref} />);
      act(() => {
        ref.current.pendingUploadId = "request-1";
        rerender(
          <Harness
            {...props}
            Wallpapers={{
              ...props.Wallpapers,
              uploadResult: { requestId: "request-1", filename: null },
            }}
            ref={ref}
          />
        );
      });

      expect(ref.current.pendingUploadId).toBeNull();
    });

    it("drops the upload result once it has been acted on", () => {
      const props = withSavedWallpapers();
      const ref = React.createRef();
      const { rerender } = render(<Harness {...props} ref={ref} />);
      act(() => {
        ref.current.pendingUploadId = "request-1";
        rerender(
          <Harness
            {...props}
            Wallpapers={{
              ...props.Wallpapers,
              uploadResult: { requestId: "request-1", filename: null },
            }}
            ref={ref}
          />
        );
      });

      // Stale afterwards, so it leaves this tab's state rather than sitting
      // there naming a file forever.
      expect(DEFAULT_PROPS.dispatch).toHaveBeenCalledWith({
        type: at.WALLPAPER_UPLOAD_RESULT,
        data: null,
      });
    });

    it("leaves focus alone when the library grows without this tab asking", () => {
      const props = withSavedWallpapers();
      const { container, rerender } = render(<Harness {...props} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const images = () =>
        container.querySelectorAll('.your-images input[type="radio"]');
      act(() => {
        images()[0].focus();
      });
      const [focused] = images();

      // Another tab uploading, or a retired wallpaper being rescued, both
      // arrive as a bigger library with no click here.
      act(() => {
        rerender(
          <Harness
            {...props}
            Wallpapers={{
              ...props.Wallpapers,
              customWallpapers: [
                {
                  filename: `v1-builtin-light-center-1-${UUID_THREE}`,
                  theme: "light",
                  position: "center",
                  type: "builtin",
                },
                ...SAVED,
              ],
            }}
          />
        );
      });

      expect(document.activeElement).toBe(focused);
    });

    it("leaves focus alone when an upload lands from outside the folder", () => {
      const props = withSavedWallpapers();
      const { container, rerender } = render(<Harness {...props} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const outside = document.createElement("button");
      document.body.appendChild(outside);
      outside.focus();

      act(() => {
        rerender(
          <Harness
            {...props}
            Wallpapers={{
              ...props.Wallpapers,
              customWallpapers: [
                {
                  filename: `v1-custom-light-center-1-${UUID_THREE}`,
                  theme: "light",
                  position: "center",
                  type: "custom",
                  number: 3,
                },
                ...SAVED,
              ],
            }}
          />
        );
      });

      expect(document.activeElement).toBe(outside);
      outside.remove();
    });

    it("names the remove button after a rescued wallpaper's own title", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const [, remove] = container.querySelectorAll(".your-images-remove");
      expect(remove).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-remove-image"
      );
      expect(remove).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ name: SAVED[1].fallbackName })
      );
    });

    it("names an image someone added by its number", async () => {
      const { container } = render(
        <Harness {...withSavedWallpapers([SAVED[0]])} />
      );
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const [label] = container.querySelectorAll(
        ".your-images-item label.sr-only"
      );
      expect(label).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-your-images-item-numbered"
      );
      expect(label).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ number: 1 })
      );
    });

    it("keeps its own title rather than a number when it has one", async () => {
      const withTitle = [{ ...SAVED[0], number: 4, fallbackName: "A heron" }];
      const { container } = render(
        <Harness {...withSavedWallpapers(withTitle)} />
      );
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const [label] = container.querySelectorAll(
        ".your-images-item label.sr-only"
      );
      expect(label).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-your-images-item"
      );
      expect(label).toHaveAttribute(
        "data-l10n-args",
        JSON.stringify({ name: "A heron" })
      );
    });

    it("asks for the library when a startup cache page opens the panel", () => {
      // A page from the startup cache never sees the library broadcast, so it
      // opens with an empty list and has to ask for one itself.
      const props = withSavedWallpapers([]);
      const { rerender } = render(
        <Harness {...{ ...props, panelShowing: false }} />
      );
      props.dispatch.mockClear();

      const requests = () =>
        props.dispatch.mock.calls.filter(
          ([action]) => action.type === at.WALLPAPERS_CUSTOM_LIBRARY_REQUEST
        ).length;

      act(() => {
        rerender(<Harness {...props} />);
      });
      expect(requests()).toBe(1);

      // Re-rendering with nothing changed must not ask again.
      act(() => {
        rerender(<Harness {...props} />);
      });
      expect(requests()).toBe(1);
    });

    it("asks again even when the page already holds a list", () => {
      // What the startup cache holds can be out of date: another window may
      // have added or removed an image since the page was written.
      const props = withSavedWallpapers();
      const { rerender } = render(
        <Harness {...{ ...props, panelShowing: false }} />
      );
      props.dispatch.mockClear();

      act(() => {
        rerender(<Harness {...props} />);
      });

      expect(
        props.dispatch.mock.calls.some(
          ([action]) => action.type === at.WALLPAPERS_CUSTOM_LIBRARY_REQUEST
        )
      ).toBe(true);
    });

    it("keeps the arrow-key tab stop across a re-render", () => {
      const props = withSavedWallpapers();
      const { container, rerender } = render(<Harness {...props} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const images = () =>
        container.querySelectorAll('.your-images input[type="radio"]');
      expect(images()[0].tabIndex).toBe(0);

      fireEvent.keyDown(images()[0], { key: "ArrowRight" });
      expect(images()[1].tabIndex).toBe(0);
      expect(images()[0].tabIndex).toBe(-1);

      // A library update re-renders the grid; the tab stop must not snap back.
      act(() => {
        rerender(
          <Harness
            {...props}
            Wallpapers={{ ...props.Wallpapers, customWallpapers: [...SAVED] }}
          />
        );
      });

      expect(images()[1].tabIndex).toBe(0);
      expect(images()[0].tabIndex).toBe(-1);
    });

    it("walks the grid a row at a time with the arrow keys", () => {
      // Four saved images plus the add tile fills one row of three and leaves
      // two on the second, which is what makes the short-row case reachable.
      const four = [0, 1, 2, 3].map(index => ({
        filename: `v1-custom-light-center-1-${UUID_ONE.slice(0, -1)}${index}`,
        theme: "light",
        position: "center",
        type: "custom",
        number: index + 1,
      }));
      const { container } = render(<Harness {...withSavedWallpapers(four)} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const tiles = () =>
        container.querySelectorAll(
          '.your-images input[type="radio"], .your-images button'
        );
      expect(tiles()).toHaveLength(5);

      fireEvent.keyDown(tiles()[0], { key: "ArrowDown" });
      expect(tiles()[3].tabIndex).toBe(0);

      fireEvent.keyDown(tiles()[3], { key: "ArrowUp" });
      expect(tiles()[0].tabIndex).toBe(0);

      fireEvent.keyDown(tiles()[0], { key: "ArrowRight" });
      fireEvent.keyDown(tiles()[1], { key: "ArrowLeft" });
      expect(tiles()[0].tabIndex).toBe(0);
    });

    it("stays put rather than moving sideways off a short bottom row", () => {
      const four = [0, 1, 2, 3].map(index => ({
        filename: `v1-custom-light-center-1-${UUID_ONE.slice(0, -1)}${index}`,
        theme: "light",
        position: "center",
        type: "custom",
        number: index + 1,
      }));
      const { container } = render(<Harness {...withSavedWallpapers(four)} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const tiles = () =>
        container.querySelectorAll(
          '.your-images input[type="radio"], .your-images button'
        );

      // Index 2 ends the top row and sits above nothing, since the second row
      // holds only two tiles. Clamping to the last tile would drag focus left
      // across the row instead of leaving it where it is.
      fireEvent.focus(tiles()[2]);
      expect(tiles()[2].tabIndex).toBe(0);

      fireEvent.keyDown(tiles()[2], { key: "ArrowDown" });
      expect(tiles()[2].tabIndex).toBe(0);
      expect(tiles()[4].tabIndex).toBe(-1);
    });

    it("moves the tab stop when a tile is activated without being focused", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      const images = () =>
        container.querySelectorAll('.your-images input[type="radio"]');
      expect(images()[0].tabIndex).toBe(0);

      // Assistive technology can check a radio without focusing it first, so a
      // bare click has to move the tab stop the same way focus does.
      fireEvent.click(images()[1]);

      expect(images()[1].tabIndex).toBe(0);
      expect(images()[0].tabIndex).toBe(-1);
    });

    it("opens no folder at all when the library pref is off", () => {
      const props = withSavedWallpapers(SAVED, {
        "newtabWallpapers.customWallpaper.library.enabled": false,
        "newtabWallpapers.customWallpaper.uuid": SAVED[1].filename,
        "newtabWallpapers.wallpaper": "custom",
      });
      const { container } = render(<Harness {...props} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));

      // The tile goes straight to the file picker, the way it did before the
      // library existed, so there is nothing to browse.
      expect(container.querySelector(".your-images")).toBeNull();
      expect(container.querySelector("#custom-wallpaper")).toHaveClass(
        "theme-custom-wallpaper"
      );
    });

    it("asks for the library when the feature is turned back on", () => {
      const off = withSavedWallpapers(SAVED, {
        "newtabWallpapers.customWallpaper.library.enabled": false,
      });
      const { rerender } = render(<Harness {...off} />);
      off.dispatch.mockClear();

      rerender(<Harness {...withSavedWallpapers()} />);

      const asked = off.dispatch.mock.calls.some(
        ([action]) => action.type === at.WALLPAPERS_CUSTOM_LIBRARY_REQUEST
      );
      expect(asked).toBe(true);
    });

    // Bug 2072951: applying leaves the library alone, so asking for it again
    // only churns the grid.
    it("does not ask for the library again when a picture is applied", () => {
      const props = withSavedWallpapers();
      const { rerender } = render(<Harness {...props} />);
      props.dispatch.mockClear();

      rerender(
        <Harness
          {...{
            ...props,
            Prefs: {
              values: {
                ...props.Prefs.values,
                "newtabWallpapers.customWallpaper.uuid": SAVED[1].filename,
              },
            },
          }}
        />
      );

      const asked = props.dispatch.mock.calls.some(
        ([action]) => action.type === at.WALLPAPERS_CUSTOM_LIBRARY_REQUEST
      );
      expect(asked).toBe(false);
    });

    // Nova is not shipped yet, so the folder has to work in the classic layout
    // too. Everything above runs with Nova on, which is the default fixture.
    it("puts focus back on the tile after the very first save", () => {
      // The tile the person clicked says "Add an image", and the folder that
      // replaces it uses a different key, so React throws the focused node
      // away and focus falls to the body.
      const ref = React.createRef();
      const before = withSavedWallpapers([]);
      const { container, rerender } = render(<Harness {...before} ref={ref} />);

      const addTile = container.querySelector("#custom-wallpaper");
      addTile.focus();
      expect(document.activeElement).toBe(addTile);

      // What handleUpload records on its way out, without opening a picker.
      ref.current.pendingUploadId = "request-1";
      ref.current.uploadStartedFromTile = true;

      const after = withSavedWallpapers(SAVED);
      after.Wallpapers.uploadResult = {
        requestId: "request-1",
        filename: SAVED[0].filename,
      };
      rerender(<Harness {...after} ref={ref} />);

      const folderTile = container.querySelector("#custom-wallpaper");
      expect(folderTile).not.toBe(addTile);
      expect(document.activeElement).toBe(folderTile);
    });

    describe("classic layout", () => {
      const classic = (customWallpapers = SAVED) =>
        withSavedWallpapers(customWallpapers, { "nova.enabled": false });

      it("still opens the folder and lists every saved image", () => {
        const { container } = render(<Harness {...classic()} />);
        expect(container.querySelector(".nova-enabled")).toBeNull();

        fireEvent.click(container.querySelector("#custom-wallpaper"));

        expect(
          container.querySelectorAll('.your-images input[type="radio"]')
        ).toHaveLength(SAVED.length);
        expect(container.querySelector("#your-images-add")).toBeTruthy();
      });

      it("uses the classic label on the tile before anything is saved", () => {
        const { container } = render(<Harness {...classic([])} />);
        // Nova renamed this copy; classic keeps the older string.
        expect(
          container.querySelector(
            'label[data-l10n-id="newtab-wallpaper-upload-image"]'
          )
        ).toBeTruthy();
      });

      it("keeps the folder name on the tile once something is saved", () => {
        const { container } = render(<Harness {...classic()} />);
        expect(
          container.querySelector(
            'label[data-l10n-id="newtab-wallpaper-your-images"]'
          )
        ).toBeTruthy();
      });

      it("removes a saved image the same way", () => {
        const props = classic();
        const { container } = render(<Harness {...props} />);
        fireEvent.click(container.querySelector("#custom-wallpaper"));

        fireEvent.click(container.querySelectorAll(".your-images-remove")[1]);

        const dialog = DEFAULT_PROPS.dispatch.mock.calls.find(
          ([action]) => action.type === at.DIALOG_OPEN
        );
        expect(dialog[0].data.onConfirm[0].data.filename).toBe(
          SAVED[1].filename
        );
      });

      it("walks the grid with the arrow keys", () => {
        const { container } = render(<Harness {...classic()} />);
        fireEvent.click(container.querySelector("#custom-wallpaper"));

        const images = () =>
          container.querySelectorAll('.your-images input[type="radio"]');
        expect(images()[0].tabIndex).toBe(0);
        fireEvent.keyDown(images()[0], { key: "ArrowRight" });
        expect(images()[1].tabIndex).toBe(0);
        expect(images()[0].tabIndex).toBe(-1);
      });
    });

    it("applies a saved image when it is picked", () => {
      const { container } = render(<Harness {...withSavedWallpapers()} />);
      fireEvent.click(container.querySelector("#custom-wallpaper"));
      fireEvent.click(
        container.querySelector(`#your-images-${SAVED[1].filename}`)
      );

      // Content only asks. The parent can refuse, for an image another tab
      // deleted, so it owns every pref that names the applied wallpaper.
      expect(DEFAULT_PROPS.dispatch).toHaveBeenCalledWith(
        expect.objectContaining({
          type: at.WALLPAPERS_CUSTOM_APPLY,
          data: { filename: SAVED[1].filename },
        })
      );
      const named = DEFAULT_PROPS.setPref.mock.calls.map(([name]) => name);
      expect(named).not.toContain("newtabWallpapers.wallpaper");
      expect(named).not.toContain("newtabWallpapers.initialWallpaper");
      expect(named).not.toContain("newtabWallpapers.user.enabled");
      expect(named).not.toContain("widgets.pictureOfTheDay.wallpaperActive");
    });
  });

  describe("layout grids", () => {
    const CELESTIAL = ["moon", "stars", "comet", "nebula", "aurora"].map(
      title => ({ title, category: "celestial", theme: "light" })
    );
    const COLORS = ["red", "green", "blue", "cyan", "pink"].map(title => ({
      title,
      category: "solid-colors",
      theme: "light",
      solid_color: "#000000",
      fluent_id: `newtab-wallpaper-${title}`,
    }));
    // Five categories so the category grid wraps into rows of three and two.
    const CATEGORIES = [
      "celestial",
      "solid-colors",
      "firefox",
      "abstracts",
      "photographs",
    ];

    const gridProps = (prefs = {}) => ({
      ...DEFAULT_PROPS,
      Prefs: {
        values: {
          ...DEFAULT_PROPS.Prefs.values,
          "nova.enabled": true,
          ...prefs,
        },
      },
      Wallpapers: {
        ...DEFAULT_PROPS.Wallpapers,
        wallpaperList: [
          ...CELESTIAL,
          ...COLORS,
          { title: "fox", category: "firefox", theme: "light" },
          { title: "shapes", category: "abstracts", theme: "light" },
          { title: "beach", category: "photographs", theme: "light" },
        ],
        categories: CATEGORIES,
      },
    });

    const cellsOf = grid => [...grid.querySelectorAll('[role="gridcell"]')];
    const rowsOf = grid => [...grid.querySelectorAll('[role="row"]')];
    const tabStopsOf = grid => grid.querySelectorAll('[tabindex="0"]');
    const openCelestial = container => {
      fireEvent.click(container.querySelector("#celestial"));
      return container.querySelector(".wallpaper-list.category [role='grid']");
    };
    // fireEvent returns false when the handler called preventDefault.
    const press = (el, key, init = {}) =>
      fireEvent.keyDown(el, { key, ...init });

    it("renders the category picker as a grid of rows with three cells", () => {
      const { container } = render(<Harness {...gridProps()} />);
      const grid = container.querySelector('[role="grid"].category-list');
      expect(grid).toHaveAttribute(
        "data-l10n-id",
        "newtab-wallpaper-category-list"
      );
      expect(grid.querySelector("fieldset")).not.toBeInTheDocument();

      const rows = rowsOf(grid);
      expect(rows).toHaveLength(2);
      expect(cellsOf(rows[0])).toHaveLength(3);
      expect(cellsOf(rows[1])).toHaveLength(2);
      for (const cell of cellsOf(grid)) {
        expect(cell.parentElement).toHaveAttribute("role", "row");
        expect(
          cell.querySelector("button.wallpaper-input")
        ).toBeInTheDocument();
      }
      expect(tabStopsOf(grid)).toHaveLength(1);
    });

    it("keeps a tab stop when the category list shrinks", () => {
      const props = gridProps();
      const { container, rerender } = render(<Harness {...props} />);
      const grid = () => container.querySelector('[role="grid"].category-list');

      // Ctrl+End parks the roving tab stop on the last category.
      press(container.querySelector("#celestial"), "End", { ctrlKey: true });
      expect(container.querySelector("#photographs").tabIndex).toBe(0);

      // Dropping a category, the way turning a pref off does, leaves the
      // stored index pointing past the end of the list.
      act(() => {
        rerender(
          <Harness
            {...props}
            Wallpapers={{
              ...props.Wallpapers,
              categories: CATEGORIES.slice(0, -1),
            }}
          />
        );
      });

      expect(tabStopsOf(grid())).toHaveLength(1);
      expect(container.querySelector("#abstracts").tabIndex).toBe(0);
    });

    it("groups a category's radios under one name", () => {
      const { container } = render(<Harness {...gridProps()} />);
      const grid = openCelestial(container);
      const radios = [...grid.querySelectorAll('input[type="radio"]')];

      expect(radios.length).toBeGreaterThan(1);
      expect(new Set(radios.map(radio => radio.name)).size).toBe(1);
    });

    it("opens a category on the applied wallpaper", () => {
      const { container } = render(
        <Harness {...gridProps()} activeWallpaper="comet" />
      );
      const grid = openCelestial(container);

      expect(container.querySelector("#comet").tabIndex).toBe(0);
      expect(tabStopsOf(grid)).toHaveLength(1);
    });

    it("opens on the first cell when the applied wallpaper is elsewhere", () => {
      // "red" is a solid color, so Celestial has nothing applied to open on.
      const { container } = render(
        <Harness {...gridProps()} activeWallpaper="red" />
      );
      const grid = openCelestial(container);

      expect(container.querySelector("#moon").tabIndex).toBe(0);
      expect(tabStopsOf(grid)).toHaveLength(1);
    });

    it("opens Colors on the color input when a custom color is applied", () => {
      const props = gridProps({ "newtabWallpapers.customColor.enabled": true });
      const { container } = render(
        <Harness {...props} activeWallpaper="solid-color-picker-#112233" />
      );
      fireEvent.click(container.querySelector("#solid-colors"));

      // The color input is the last cell of the grid, not one of the swatches.
      expect(container.querySelector("#solid-color-picker").tabIndex).toBe(0);
    });

    it("renders the wallpaper picker as a grid labelled by its heading", () => {
      const { container } = render(<Harness {...gridProps()} />);
      const grid = openCelestial(container);
      expect(grid).toHaveAttribute("aria-labelledby", "wallpaper-list-title");
      expect(
        container.querySelector(".wallpaper-list.category h2")
      ).toHaveAttribute("id", "wallpaper-list-title");

      const rows = rowsOf(grid);
      expect(rows).toHaveLength(2);
      expect(cellsOf(rows[0])).toHaveLength(3);
      expect(cellsOf(rows[1])).toHaveLength(2);
      for (const cell of cellsOf(grid)) {
        expect(cell.querySelector('input[type="radio"]')).toBeInTheDocument();
      }
      expect(tabStopsOf(grid)).toHaveLength(1);
      expect(container.querySelector("#moon")).toHaveAttribute("tabindex", "0");
    });

    it("puts the custom colour input in the last cell and in the roving tabindex", () => {
      const { container } = render(
        <Harness
          {...gridProps({ "newtabWallpapers.customColor.enabled": true })}
        />
      );
      fireEvent.click(container.querySelector("#solid-colors"));
      const grid = container.querySelector(
        ".wallpaper-list.category [role='grid']"
      );
      const cells = cellsOf(grid);
      expect(cells).toHaveLength(6);
      expect(rowsOf(grid)).toHaveLength(2);
      const lastCell = cells[cells.length - 1];
      expect(lastCell).toHaveClass("theme-custom-color-picker");
      const colorInput = lastCell.querySelector("#solid-color-picker");
      expect(colorInput).toHaveAttribute("tabindex", "-1");
      expect(tabStopsOf(grid)).toHaveLength(1);

      const pink = container.querySelector("#pink");
      pink.focus();
      expect(press(pink, "ArrowRight")).toBe(false);
      expect(document.activeElement).toBe(colorInput);
      expect(colorInput).toHaveAttribute("tabindex", "0");
      expect(pink).toHaveAttribute("tabindex", "-1");
      expect(tabStopsOf(grid)).toHaveLength(1);

      expect(press(colorInput, "ArrowLeft")).toBe(false);
      expect(document.activeElement).toBe(pink);
    });

    it("names each control from its label and hides the label from assistive tech", () => {
      const { container } = render(
        <Harness
          {...gridProps({ "newtabWallpapers.customColor.enabled": true })}
        />
      );
      const celestial = container.querySelector("#celestial");
      const celestialLabel = container.querySelector("#celestial-label");
      expect(celestial).toHaveAttribute("aria-labelledby", "celestial-label");
      expect(celestialLabel).toHaveAttribute("for", "celestial");
      expect(celestialLabel).toHaveAttribute("aria-hidden", "true");
      expect(celestial).toHaveAccessibleName(
        "newtab-wallpaper-category-title-celestial"
      );

      fireEvent.click(container.querySelector("#solid-colors"));
      const red = container.querySelector("#red");
      expect(red).toHaveAttribute("aria-labelledby", "red-label");
      expect(container.querySelector("#red-label")).toHaveAttribute(
        "aria-hidden",
        "true"
      );
      expect(red).toHaveAccessibleName("newtab-wallpaper-red");

      const colorInput = container.querySelector("#solid-color-picker");
      expect(colorInput).toHaveAttribute(
        "aria-labelledby",
        "solid-color-picker-label"
      );
      expect(
        container.querySelector("#solid-color-picker-label")
      ).toHaveAttribute("aria-hidden", "true");
    });

    it("wraps Right and Left between category rows and stops at the grid ends", () => {
      const { container } = render(<Harness {...gridProps()} />);
      const [celestial, , firefox, abstracts, photographs] = CATEGORIES.map(
        id => container.querySelector(`#${id}`)
      );
      firefox.focus();

      expect(press(firefox, "ArrowRight")).toBe(false);
      expect(document.activeElement).toBe(abstracts);
      expect(abstracts).toHaveAttribute("tabindex", "0");
      expect(firefox).toHaveAttribute("tabindex", "-1");

      expect(press(abstracts, "ArrowLeft")).toBe(false);
      expect(document.activeElement).toBe(firefox);

      photographs.focus();
      expect(press(photographs, "ArrowRight")).toBe(false);
      expect(document.activeElement).toBe(photographs);

      celestial.focus();
      expect(press(celestial, "ArrowLeft")).toBe(false);
      expect(document.activeElement).toBe(celestial);
    });

    it("moves Up and Down by a row without wrapping", () => {
      const { container } = render(<Harness {...gridProps()} />);
      const celestial = container.querySelector("#celestial");
      const abstracts = container.querySelector("#abstracts");
      celestial.focus();

      expect(press(celestial, "ArrowDown")).toBe(false);
      expect(document.activeElement).toBe(abstracts);
      expect(press(abstracts, "ArrowDown")).toBe(false);
      expect(document.activeElement).toBe(abstracts);

      expect(press(abstracts, "ArrowUp")).toBe(false);
      expect(document.activeElement).toBe(celestial);
      expect(press(celestial, "ArrowUp")).toBe(false);
      expect(document.activeElement).toBe(celestial);
    });

    it("moves to the row ends with Home and End and to the grid corners with Ctrl", () => {
      const { container } = render(<Harness {...gridProps()} />);
      const [celestial, solidColors, firefox, abstracts, photographs] =
        CATEGORIES.map(id => container.querySelector(`#${id}`));
      solidColors.focus();

      expect(press(solidColors, "End")).toBe(false);
      expect(document.activeElement).toBe(firefox);
      expect(press(firefox, "Home")).toBe(false);
      expect(document.activeElement).toBe(celestial);

      expect(press(celestial, "End", { ctrlKey: true })).toBe(false);
      expect(document.activeElement).toBe(photographs);
      expect(press(photographs, "Home")).toBe(false);
      expect(document.activeElement).toBe(abstracts);
      expect(press(abstracts, "Home", { ctrlKey: true })).toBe(false);
      expect(document.activeElement).toBe(celestial);
    });

    it("mirrors Right and Left in right-to-left documents", () => {
      const { container } = render(<Harness {...gridProps()} />);
      const celestial = container.querySelector("#celestial");
      const solidColors = container.querySelector("#solid-colors");
      document.dir = "rtl";
      try {
        solidColors.focus();
        expect(press(solidColors, "ArrowRight")).toBe(false);
        expect(document.activeElement).toBe(celestial);
        expect(press(celestial, "ArrowLeft")).toBe(false);
        expect(document.activeElement).toBe(solidColors);
      } finally {
        document.dir = "";
      }
    });

    it("moves focus and selection between wallpapers and leaves Tab alone", () => {
      const { container } = render(<Harness {...gridProps()} />);
      const grid = openCelestial(container);
      const moon = container.querySelector("#moon");
      const stars = container.querySelector("#stars");
      const nebula = container.querySelector("#nebula");
      const aurora = container.querySelector("#aurora");
      moon.focus();

      expect(press(moon, "ArrowRight")).toBe(false);
      expect(document.activeElement).toBe(stars);
      expect(stars).toHaveAttribute("tabindex", "0");
      expect(moon).toHaveAttribute("tabindex", "-1");
      expect(tabStopsOf(grid)).toHaveLength(1);
      expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
        "newtabWallpapers.wallpaper",
        "stars"
      );

      expect(press(stars, "ArrowDown")).toBe(false);
      expect(document.activeElement).toBe(aurora);
      expect(press(aurora, "Home")).toBe(false);
      expect(document.activeElement).toBe(nebula);

      expect(press(nebula, "Tab")).toBe(true);
      expect(press(nebula, "a")).toBe(true);
      expect(document.activeElement).toBe(nebula);
    });

    it("clamps the tab stop when the open category loses wallpapers", () => {
      const props = gridProps();
      const { container, rerender } = render(<Harness {...props} />);
      const grid = openCelestial(container);
      const moon = container.querySelector("#moon");
      moon.focus();
      press(moon, "End", { ctrlKey: true });
      expect(document.activeElement).toBe(container.querySelector("#aurora"));

      act(() => {
        rerender(
          <Harness
            {...props}
            Wallpapers={{
              ...props.Wallpapers,
              wallpaperList: CELESTIAL.slice(0, 2),
            }}
          />
        );
      });

      expect(cellsOf(grid)).toHaveLength(2);
      expect(tabStopsOf(grid)).toHaveLength(1);
      expect(container.querySelector("#stars")).toHaveAttribute(
        "tabindex",
        "0"
      );
    });

    it("labels the wallpaper grid from the back button when Nova is off", () => {
      const props = {
        ...gridProps(),
        Prefs: { values: { ...DEFAULT_PROPS.Prefs.values } },
      };
      const { container } = render(<Harness {...props} />);
      const grid = openCelestial(container);
      const backButton = container.querySelector(
        ".wallpaper-list.category button.arrow-button"
      );
      expect(backButton).toHaveAttribute("id", "wallpaper-list-title");
      expect(grid).toHaveAttribute("aria-labelledby", "wallpaper-list-title");
    });

    it("resets the focused wallpaper when a category opens again", () => {
      const { container } = render(<Harness {...gridProps()} />);
      openCelestial(container);
      const moon = container.querySelector("#moon");
      moon.focus();
      press(moon, "ArrowRight");
      expect(container.querySelector("#stars")).toHaveAttribute(
        "tabindex",
        "0"
      );

      fireEvent.click(container.querySelector(".wallpaper-list .arrow-button"));
      act(() => {
        fireEvent.click(container.querySelector("#celestial"));
      });

      expect(container.querySelector("#moon")).toHaveAttribute("tabindex", "0");
      expect(container.querySelector("#stars")).toHaveAttribute(
        "tabindex",
        "-1"
      );
    });
  });

  it("should clear initialWallpaper when a custom colour is set", () => {
    const ref = React.createRef();
    render(<WallpaperCategories {...DEFAULT_PROPS} ref={ref} />);
    act(() => {
      ref.current.handleColorInput({
        target: { id: "solid-color-picker", value: "#112233", style: {} },
      });
    });
    expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
      "newtabWallpapers.wallpaper",
      "solid-color-picker-#112233"
    );
    expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
      "newtabWallpapers.initialWallpaper",
      ""
    );
    expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
      "newtabWallpapers.user.enabled",
      true
    );
  });
});

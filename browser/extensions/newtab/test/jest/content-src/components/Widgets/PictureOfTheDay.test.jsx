/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { act, render, fireEvent } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { PictureOfTheDay } from "content-src/components/Widgets/PictureOfTheDay/PictureOfTheDay";

const mockState = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "widgets.system.enabled": true,
      "widgets.enabled": true,
      "widgets.system.pictureOfTheDay.enabled": true,
      "widgets.pictureOfTheDay.enabled": true,
      "widgets.pictureOfTheDay.size": "medium",
      "widgets.pictureOfTheDay.setAsWallpaper.enabled": true,
      "newtabWallpapers.enabled": true,
      "newtabWallpapers.user.enabled": true,
      "newtabWallpapers.customWallpaper.enabled": true,
    },
  },
};

function WrapWithProvider({ children, state = INITIAL_STATE }) {
  const store = createStore(combineReducers(reducers), state);
  return <Provider store={store}>{children}</Provider>;
}

function renderWidget(dispatch = jest.fn(), props = {}, state = mockState) {
  const handleUserInteraction = props.handleUserInteraction || jest.fn();
  const { container, unmount } = render(
    <WrapWithProvider state={state}>
      <PictureOfTheDay
        dispatch={dispatch}
        handleUserInteraction={handleUserInteraction}
        widgetsMayBeMaximized={true}
        widgetEnabledMap={{}}
        {...props}
      />
    </WrapWithProvider>
  );
  return { container, unmount, dispatch, handleUserInteraction };
}

describe("PictureOfTheDay widget", () => {
  it("renders the widget at the resolved size", () => {
    const { container } = renderWidget();
    const root = container.querySelector("article.picture-of-the-day");
    expect(root).toBeTruthy();
    expect(root.className).toContain("medium-widget");
  });

  it("renders the empty-state message and the show (eye) button", () => {
    const { container } = renderWidget();
    expect(
      container.querySelector('[data-l10n-id="newtab-picture-check-back"]')
    ).toBeTruthy();
    expect(
      container.querySelector('[data-l10n-id="newtab-picture-show-button2"]')
    ).toBeTruthy();
  });

  describe("impression telemetry", () => {
    let originalIntersectionObserver;
    let observerInstances;

    beforeEach(() => {
      observerInstances = [];
      originalIntersectionObserver = global.IntersectionObserver;
      global.IntersectionObserver = class MockIntersectionObserver {
        constructor(callback) {
          this.callback = callback;
          this.observed = [];
          observerInstances.push(this);
        }
        observe(el) {
          this.observed.push(el);
        }
        unobserve() {}
        disconnect() {}
      };
    });

    afterEach(() => {
      global.IntersectionObserver = originalIntersectionObserver;
    });

    it("dispatches WIDGETS_IMPRESSION only once even when intersection fires repeatedly", () => {
      const dispatch = jest.fn();
      renderWidget(dispatch);
      const [observer] = observerInstances;
      const [target] = observer.observed;

      observer.callback([{ isIntersecting: true, target }], observer);
      observer.callback([{ isIntersecting: true, target }], observer);

      const impressions = dispatch.mock.calls.filter(
        ([action]) => action?.type === at.WIDGETS_IMPRESSION
      );
      expect(impressions).toHaveLength(1);
      expect(impressions[0][0].data).toMatchObject({
        widget_name: "picture_of_the_day",
        widget_size: "medium",
      });
    });
  });

  it("offers only medium and large in the resize submenu", () => {
    const { container } = renderWidget();
    const sizes = [
      ...container.querySelectorAll(
        "#picture-of-the-day-size-submenu [data-size]"
      ),
    ].map(el => el.getAttribute("data-size"));
    expect(sizes).toEqual(["medium", "large"]);
  });

  it("hides the widget by setting its enabled pref to false", () => {
    const dispatch = jest.fn();
    const { container } = renderWidget(dispatch);
    fireEvent.click(
      container.querySelector('[data-l10n-id="newtab-widget-menu-hide"]')
    );
    const setPref = dispatch.mock.calls.find(
      ([action]) =>
        action.type === at.SET_PREF &&
        action.data?.name === "widgets.pictureOfTheDay.enabled"
    );
    expect(setPref).toBeTruthy();
    expect(setPref[0].data.value).toBe(false);
  });

  it("emits a user event when the show (eye) button is clicked", () => {
    const dispatch = jest.fn();
    const { container } = renderWidget(dispatch);
    fireEvent.click(container.querySelector(".picture-of-the-day-show-button"));
    const evt = dispatch.mock.calls.find(
      ([action]) =>
        action.type === at.WIDGETS_USER_EVENT &&
        action.data?.user_action === "show_picture"
    );
    expect(evt).toBeTruthy();
    expect(evt[0].data.widget_name).toBe("picture_of_the_day");
  });

  it("emits telemetry for both the manage-wallpaper and hide-photo menu items", () => {
    const dispatch = jest.fn();
    const { container } = renderWidget(dispatch);

    const manageWallpaper = container.querySelector(
      '[data-l10n-id="newtab-picture-menu-manage-wallpaper"]'
    );
    expect(manageWallpaper).toBeTruthy();
    fireEvent.click(manageWallpaper);
    expect(
      dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "manage_wallpaper"
      )
    ).toBeTruthy();

    const hidePhoto = container.querySelector(
      '[data-l10n-id="newtab-picture-menu-hide-photo2"]'
    );
    expect(hidePhoto).toBeTruthy();
    fireEvent.click(hidePhoto);
    expect(
      dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "hide_photo"
      )
    ).toBeTruthy();
  });

  it("opens the Customize panel when Manage wallpaper is clicked", () => {
    const dispatch = jest.fn();
    const { container } = renderWidget(dispatch);
    fireEvent.click(
      container.querySelector(
        '[data-l10n-id="newtab-picture-menu-manage-wallpaper"]'
      )
    );
    expect(
      dispatch.mock.calls.find(
        ([action]) => action.type === at.SHOW_PERSONALIZE
      )?.[0]
    ).toEqual({
      type: at.SHOW_PERSONALIZE,
      data: { source: "picture_of_the_day" },
    });
  });

  describe("populated state", () => {
    const populatedState = {
      ...mockState,
      PictureOfTheDay: {
        ...INITIAL_STATE.PictureOfTheDay,
        imageUrl: "https://example.com/potd.jpg",
        thumbnailUrl: "https://example.com/potd-thumb.jpg",
        title: "Test picture",
        description: "A test picture description.",
        publishedDate: "2026-07-01",
      },
    };

    const withPrefs = extraPrefs => ({
      ...populatedState,
      Prefs: {
        ...populatedState.Prefs,
        values: { ...populatedState.Prefs.values, ...extraPrefs },
      },
    });

    it("renders the picture, source line, description, and Set wallpaper button", () => {
      const { container } = renderWidget(jest.fn(), {}, populatedState);
      const root = container.querySelector("article.picture-of-the-day");
      expect(root.className).toContain("has-picture");
      const img = container.querySelector("img.picture-of-the-day-image");
      expect(img).toBeTruthy();
      // The widget renders the thumbnail, never the high-res imageUrl.
      expect(img.getAttribute("src")).toBe(
        "https://example.com/potd-thumb.jpg"
      );
      expect(img.getAttribute("alt")).toBe("A test picture description.");
      expect(
        container.querySelector('[data-l10n-id="newtab-picture-header-date"]')
      ).toBeTruthy();
      expect(
        container.querySelector(".picture-of-the-day-description").textContent
      ).toBe("A test picture description.");
      expect(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      ).toBeTruthy();
      // No empty-state eye/message while a picture is shown.
      expect(
        container.querySelector(".picture-of-the-day-show-button")
      ).toBeFalsy();
    });

    it("shows the empty state (never the high-res URL) when the thumbnail is absent", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        {
          ...populatedState,
          PictureOfTheDay: {
            ...populatedState.PictureOfTheDay,
            thumbnailUrl: "",
          },
        }
      );
      expect(
        container.querySelector("img.picture-of-the-day-image")
      ).toBeFalsy();
      expect(
        container.querySelector('[data-l10n-id="newtab-picture-check-back"]')
      ).toBeTruthy();
      expect(
        container.querySelector("article.picture-of-the-day").className
      ).not.toContain("has-picture");
    });

    it("renders the New badge until the widget has been interacted with", () => {
      const { container } = renderWidget(jest.fn(), {}, populatedState);
      const badge = container.querySelector(".picture-of-the-day-new-badge");
      expect(badge).toBeTruthy();
      // type="new" logs a missing-string error on newtab, so it must stay off.
      expect(badge.hasAttribute("type")).toBe(false);
      expect(badge.getAttribute("data-l10n-id")).toBe(
        "newtab-widget-lists-label-new"
      );
    });

    it("hides the New badge once the interaction pref is set", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({ "widgets.pictureOfTheDay.interaction": true })
      );
      expect(
        container.querySelector(".picture-of-the-day-new-badge")
      ).toBeFalsy();
    });

    it("marks the picture loaded only after it loads (bug 2071112)", () => {
      const { container } = renderWidget(jest.fn(), {}, populatedState);
      const img = container.querySelector("img.picture-of-the-day-image");
      expect(img.classList.contains("loaded")).toBe(false);

      fireEvent.load(img);

      expect(img.classList.contains("loaded")).toBe(true);
    });

    const widgetWithState = state => (
      <WrapWithProvider state={state}>
        <PictureOfTheDay
          dispatch={jest.fn()}
          handleUserInteraction={jest.fn()}
          widgetsMayBeMaximized={true}
          widgetEnabledMap={{}}
        />
      </WrapWithProvider>
    );

    it("starts the picture hidden again after it is hidden and shown", () => {
      const { container, rerender } = render(widgetWithState(populatedState));
      fireEvent.load(container.querySelector("img.picture-of-the-day-image"));

      rerender(
        widgetWithState(
          withPrefs({ "widgets.pictureOfTheDay.dismissedDate": "2026-07-01" })
        )
      );
      expect(
        container.querySelector("img.picture-of-the-day-image")
      ).toBeFalsy();

      rerender(widgetWithState(populatedState));
      expect(
        container
          .querySelector("img.picture-of-the-day-image")
          .classList.contains("loaded")
      ).toBe(false);
    });

    it("keeps the current picture showing while a new picture loads", () => {
      const { container, rerender } = render(widgetWithState(populatedState));
      fireEvent.load(container.querySelector("img.picture-of-the-day-image"));

      rerender(
        widgetWithState({
          ...populatedState,
          PictureOfTheDay: {
            ...populatedState.PictureOfTheDay,
            thumbnailUrl: "https://example.com/potd-thumb-2.jpg",
          },
        })
      );
      const img = container.querySelector("img.picture-of-the-day-image");
      expect(img.getAttribute("src")).toBe(
        "https://example.com/potd-thumb-2.jpg"
      );
      expect(img.classList.contains("loaded")).toBe(true);
    });

    it("falls back to the empty state when the picture fails to load", () => {
      const { container } = renderWidget(jest.fn(), {}, populatedState);
      const img = container.querySelector("img.picture-of-the-day-image");
      expect(img).toBeTruthy();

      fireEvent.error(img);

      expect(
        container.querySelector("img.picture-of-the-day-image")
      ).toBeFalsy();
      expect(
        container.querySelector('[data-l10n-id="newtab-picture-check-back"]')
      ).toBeTruthy();
      expect(
        container.querySelector("article.picture-of-the-day").className
      ).not.toContain("has-picture");
    });

    it("dispatches WIDGETS_PICTURE_SET_WALLPAPER when Set wallpaper is clicked", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(dispatch, {}, populatedState);
      fireEvent.click(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      );
      expect(
        dispatch.mock.calls.find(
          ([action]) => action.type === at.WIDGETS_PICTURE_SET_WALLPAPER
        )
      ).toBeTruthy();
    });

    it("keeps the Set wallpaper button collapsed with a checkmark while the current picture is the active wallpaper", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({ "widgets.pictureOfTheDay.wallpaperActive": "2026-07-01" })
      );
      const button = container.querySelector(
        ".picture-of-the-day-set-wallpaper"
      );
      expect(button.classList.contains("is-collapsed")).toBe(true);
      expect(button.classList.contains("no-expand")).toBe(true);
      expect(button.getAttribute("iconsrc")).toBe(
        "chrome://global/skin/icons/check.svg"
      );
    });

    it("does nothing when the checkmark (already-set) button is clicked", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(
        dispatch,
        {},
        withPrefs({ "widgets.pictureOfTheDay.wallpaperActive": "2026-07-01" })
      );
      fireEvent.click(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      );
      expect(
        dispatch.mock.calls.find(
          ([action]) => action.type === at.WIDGETS_PICTURE_SET_WALLPAPER
        )
      ).toBeFalsy();
    });

    it("hides the checkmark while wallpapers are toggled off, keeping the stored date", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({
          "widgets.pictureOfTheDay.wallpaperActive": "2026-07-01",
          "newtabWallpapers.user.enabled": false,
        })
      );
      const button = container.querySelector(
        ".picture-of-the-day-set-wallpaper"
      );
      expect(button.classList.contains("is-collapsed")).toBe(false);
      expect(button.getAttribute("iconsrc")).toBe(
        "chrome://browser/skin/canvas.svg"
      );
    });

    it("re-offers the Set wallpaper CTA when the active-wallpaper date is for a different picture", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({ "widgets.pictureOfTheDay.wallpaperActive": "2026-06-30" })
      );
      const button = container.querySelector(
        ".picture-of-the-day-set-wallpaper"
      );
      expect(button.classList.contains("is-collapsed")).toBe(false);
      expect(button.getAttribute("iconsrc")).toBe(
        "chrome://browser/skin/canvas.svg"
      );
    });

    describe("when today's picture is the wallpaper (bug 2065871)", () => {
      const previous = {
        imageUrl: "https://example.com/previous.jpg",
        thumbnailUrl: "https://example.com/previous-thumb.jpg",
        title: "Previous picture",
        description: "The previous picture.",
        publishedDate: "2026-06-30",
      };
      const withPrevious = extraPrefs => {
        const state = withPrefs(extraPrefs);
        return {
          ...state,
          PictureOfTheDay: { ...state.PictureOfTheDay, previous },
        };
      };
      const shownSrc = container =>
        container
          .querySelector("img.picture-of-the-day-image")
          .getAttribute("src");

      it("shows the previous picture, dated, with Set wallpaper for it", () => {
        const dispatch = jest.fn();
        const { container } = renderWidget(
          dispatch,
          {},
          withPrevious({
            "widgets.pictureOfTheDay.wallpaperActive": "2026-07-01",
          })
        );
        expect(shownSrc(container)).toBe(previous.thumbnailUrl);
        const header = container.querySelector(
          '[data-l10n-id="newtab-picture-header-date"]'
        );
        expect(JSON.parse(header.getAttribute("data-l10n-args"))).toEqual({
          date: new Date(2026, 5, 30).getTime(),
        });
        const button = container.querySelector(
          ".picture-of-the-day-set-wallpaper"
        );
        expect(button.classList.contains("is-collapsed")).toBe(false);
        fireEvent.click(button);
        expect(
          dispatch.mock.calls.find(
            ([action]) => action.type === at.WIDGETS_PICTURE_SET_WALLPAPER
          )[0].data
        ).toEqual({ publishedDate: "2026-06-30" });
      });

      it("shows today's picture once the previous one is the wallpaper", () => {
        const { container } = renderWidget(
          jest.fn(),
          {},
          withPrevious({
            "widgets.pictureOfTheDay.wallpaperActive": "2026-06-30",
          })
        );
        expect(shownSrc(container)).toBe("https://example.com/potd-thumb.jpg");
      });

      it("shows today's picture while wallpapers are toggled off", () => {
        const { container } = renderWidget(
          jest.fn(),
          {},
          withPrevious({
            "widgets.pictureOfTheDay.wallpaperActive": "2026-07-01",
            "newtabWallpapers.user.enabled": false,
          })
        );
        expect(shownSrc(container)).toBe("https://example.com/potd-thumb.jpg");
      });

      it("swaps to the previous picture in the same widget once today's is set", () => {
        const dispatch = jest.fn();
        const store = createStore(combineReducers(reducers), withPrevious({}));
        const { container } = render(
          <Provider store={store}>
            <PictureOfTheDay
              dispatch={dispatch}
              handleUserInteraction={jest.fn()}
              widgetsMayBeMaximized={true}
              widgetEnabledMap={{}}
            />
          </Provider>
        );
        const button = () =>
          container.querySelector(".picture-of-the-day-set-wallpaper");
        fireEvent.click(button());
        expect(button().getAttribute("iconsrc")).toBe(
          "chrome://global/skin/icons/check.svg"
        );

        act(() => {
          store.dispatch({
            type: at.PREF_CHANGED,
            data: {
              name: "widgets.pictureOfTheDay.wallpaperActive",
              value: "2026-07-01",
            },
          });
        });

        expect(shownSrc(container)).toBe(previous.thumbnailUrl);
        expect(button().classList.contains("is-collapsed")).toBe(false);
        expect(button().getAttribute("iconsrc")).toBe(
          "chrome://browser/skin/canvas.svg"
        );
        dispatch.mockClear();
        fireEvent.click(button());
        expect(
          dispatch.mock.calls.find(
            ([action]) => action.type === at.WIDGETS_PICTURE_SET_WALLPAPER
          )[0].data
        ).toEqual({ publishedDate: "2026-06-30" });
      });

      it("keeps today's picture when there is no previous one", () => {
        const { container } = renderWidget(
          jest.fn(),
          {},
          withPrefs({ "widgets.pictureOfTheDay.wallpaperActive": "2026-07-01" })
        );
        expect(shownSrc(container)).toBe("https://example.com/potd-thumb.jpg");
      });
    });

    it("hides the Set wallpaper button when custom wallpapers are disabled", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({ "newtabWallpapers.customWallpaper.enabled": false })
      );
      expect(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      ).toBeFalsy();
    });

    it("hides the Set wallpaper button when wallpapers are disabled", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({ "newtabWallpapers.enabled": false })
      );
      expect(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      ).toBeFalsy();
    });

    it("hides the Set wallpaper button when the feature is disabled", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({ "widgets.pictureOfTheDay.setAsWallpaper.enabled": false })
      );
      expect(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      ).toBeFalsy();
    });

    it("shows the Set wallpaper button when a trainhopConfig override enables the feature despite the pref being off", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({
          "widgets.pictureOfTheDay.setAsWallpaper.enabled": false,
          trainhopConfig: {
            widgets: { pictureOfTheDaySetAsWallpaperEnabled: true },
          },
        })
      );
      expect(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      ).toBeTruthy();
    });

    it("shows the Set wallpaper button when the dedicated widgetPictureOfTheDay namespace enables the feature", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({
          "widgets.pictureOfTheDay.setAsWallpaper.enabled": false,
          trainhopConfig: {
            widgetPictureOfTheDay: { setAsWallpaperEnabled: true },
          },
        })
      );
      expect(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      ).toBeTruthy();
    });

    it("lets the dedicated widgetPictureOfTheDay namespace win over the shared widgets key", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({
          "widgets.pictureOfTheDay.setAsWallpaper.enabled": true,
          trainhopConfig: {
            widgetPictureOfTheDay: { setAsWallpaperEnabled: false },
            widgets: { pictureOfTheDaySetAsWallpaperEnabled: true },
          },
        })
      );
      expect(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      ).toBeFalsy();
    });

    it("dismisses by setting dismissedDate to the picture's date", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(dispatch, {}, populatedState);
      fireEvent.click(
        container.querySelector(
          '[data-l10n-id="newtab-picture-menu-hide-photo2"]'
        )
      );
      const setPref = dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.SET_PREF &&
          action.data?.name === "widgets.pictureOfTheDay.dismissedDate"
      );
      expect(setPref).toBeTruthy();
      expect(setPref[0].data.value).toBe("2026-07-01");
    });

    it("shows the empty state while today's picture is dismissed; the eye clears it", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(
        dispatch,
        {},
        withPrefs({ "widgets.pictureOfTheDay.dismissedDate": "2026-07-01" })
      );
      expect(
        container.querySelector("img.picture-of-the-day-image")
      ).toBeFalsy();
      expect(
        container.querySelector('[data-l10n-id="newtab-picture-check-back"]')
      ).toBeTruthy();

      fireEvent.click(
        container.querySelector(".picture-of-the-day-show-button")
      );
      const cleared = dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.SET_PREF &&
          action.data?.name === "widgets.pictureOfTheDay.dismissedDate"
      );
      expect(cleared).toBeTruthy();
      expect(cleared[0].data.value).toBe("");
    });

    it("shows a new day's picture even if a previous day was dismissed", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        withPrefs({ "widgets.pictureOfTheDay.dismissedDate": "2026-06-30" })
      );
      expect(
        container.querySelector("img.picture-of-the-day-image")
      ).toBeTruthy();
    });

    it("falls back to the local day when the picture has no published date", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(
        dispatch,
        {},
        {
          ...populatedState,
          PictureOfTheDay: {
            ...populatedState.PictureOfTheDay,
            publishedDate: "",
          },
        }
      );
      fireEvent.click(
        container.querySelector(
          '[data-l10n-id="newtab-picture-menu-hide-photo2"]'
        )
      );
      const setPref = dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.SET_PREF &&
          action.data?.name === "widgets.pictureOfTheDay.dismissedDate"
      );
      expect(setPref).toBeTruthy();
      expect(setPref[0].data.value).toBe(new Date().toDateString());
    });
  });

  describe("attribution and source links", () => {
    const SOURCE_URL = "https://commons.wikimedia.org/wiki/File:Example.jpg";
    const LICENSE_URL = "https://creativecommons.org/licenses/by-sa/4.0/";
    const attributedState = {
      ...mockState,
      PictureOfTheDay: {
        ...INITIAL_STATE.PictureOfTheDay,
        imageUrl: "https://example.com/potd.jpg",
        thumbnailUrl: "https://example.com/potd-thumb.jpg",
        description: "A test picture description.",
        publishedDate: "2026-07-01",
        author: "Jane Doe",
        sourceUrl: SOURCE_URL,
        licenseLabel: "CC BY-SA 4.0",
        licenseUrl: LICENSE_URL,
      },
    };

    it("renders the author credit, source link, and license link", () => {
      const { container } = renderWidget(jest.fn(), {}, attributedState);
      const author = container.querySelector(
        ".picture-of-the-day-attribution-author"
      );
      expect(author.getAttribute("data-l10n-id")).toBe(
        "newtab-picture-attribution-author"
      );
      expect(author.getAttribute("data-l10n-args")).toContain("Jane Doe");
      expect(
        container.querySelector(
          '[data-l10n-id="newtab-picture-attribution-source-link"]'
        )
      ).toBeTruthy();
      const license = container.querySelector(
        '[data-l10n-id="newtab-picture-attribution-license"]'
      );
      expect(license).toBeTruthy();
      expect(license.textContent).toBe("CC BY-SA 4.0");
    });

    it("keeps the title and description as plain (non-link) text", () => {
      const { container } = renderWidget(jest.fn(), {}, attributedState);
      expect(
        container.querySelector('p[data-l10n-id="newtab-picture-header-date"]')
      ).toBeTruthy();
      expect(
        container.querySelector("p.picture-of-the-day-description")
      ).toBeTruthy();
    });

    it("links the source page via the anchor href for same-tab navigation", () => {
      const { container } = renderWidget(jest.fn(), {}, attributedState);
      const sourceLink = container.querySelector(
        '[data-l10n-id="newtab-picture-attribution-source-link"]'
      );
      expect(sourceLink.tagName).toBe("A");
      expect(sourceLink.getAttribute("href")).toBe(SOURCE_URL);
    });

    it("links the license page via the anchor href for same-tab navigation", () => {
      const { container } = renderWidget(jest.fn(), {}, attributedState);
      const licenseLink = container.querySelector(
        '[data-l10n-id="newtab-picture-attribution-license"]'
      );
      expect(licenseLink.tagName).toBe("A");
      expect(licenseLink.getAttribute("href")).toBe(LICENSE_URL);
    });

    it("records an open_license user event when the license link is clicked", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(dispatch, {}, attributedState);
      fireEvent.click(
        container.querySelector(
          '[data-l10n-id="newtab-picture-attribution-license"]'
        )
      );
      const evt = dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "open_license"
      );
      expect(evt).toBeTruthy();
      expect(evt[0].data.widget_name).toBe("picture_of_the_day");
    });

    it("links the image to the source page via the anchor href", () => {
      const { container } = renderWidget(jest.fn(), {}, attributedState);
      const imageLink = container.querySelector(
        ".picture-of-the-day-image-link"
      );
      expect(imageLink.tagName).toBe("A");
      expect(imageLink.getAttribute("href")).toBe(SOURCE_URL);
    });

    it("omits attribution parts and links whose fields are absent", () => {
      const { container } = renderWidget(
        jest.fn(),
        {},
        {
          ...attributedState,
          PictureOfTheDay: {
            ...attributedState.PictureOfTheDay,
            author: "",
            sourceUrl: "",
            licenseLabel: "",
            licenseUrl: "",
          },
        }
      );
      expect(
        container.querySelector(".picture-of-the-day-attribution")
      ).toBeFalsy();
      expect(
        container.querySelector(".picture-of-the-day-image-link")
      ).toBeFalsy();
      expect(
        container.querySelector('p[data-l10n-id="newtab-picture-header-date"]')
      ).toBeTruthy();
    });
  });

  describe("telemetry (Bug 2050977)", () => {
    const populated = {
      ...mockState,
      PictureOfTheDay: {
        ...INITIAL_STATE.PictureOfTheDay,
        imageUrl: "https://example.com/potd.jpg",
        thumbnailUrl: "https://example.com/potd-thumb.jpg",
        title: "Test picture",
        description: "A test picture description.",
        publishedDate: "2026-07-01",
      },
    };

    it("records a widgets_enabled=false event when hidden from the menu", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(dispatch);
      fireEvent.click(
        container.querySelector('[data-l10n-id="newtab-widget-menu-hide"]')
      );
      const enabled = dispatch.mock.calls.find(
        ([action]) => action.type === at.WIDGETS_ENABLED
      );
      expect(enabled).toBeTruthy();
      expect(enabled[0].data).toMatchObject({
        widget_name: "picture_of_the_day",
        enabled: false,
      });
    });

    it("records a set_wallpaper user event when Set wallpaper is clicked", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(dispatch, {}, populated);
      fireEvent.click(
        container.querySelector(".picture-of-the-day-set-wallpaper")
      );
      const evt = dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "set_wallpaper"
      );
      expect(evt).toBeTruthy();
      expect(evt[0].data.widget_name).toBe("picture_of_the_day");
    });

    it("records an open_source user event when the source line is clicked", () => {
      const dispatch = jest.fn();
      const { container } = renderWidget(
        dispatch,
        {},
        {
          ...populated,
          PictureOfTheDay: {
            ...populated.PictureOfTheDay,
            sourceUrl: "https://commons.wikimedia.org/wiki/File:Example.jpg",
          },
        }
      );
      fireEvent.click(
        container.querySelector(".picture-of-the-day-source-link")
      );
      const evt = dispatch.mock.calls.find(
        ([action]) =>
          action.type === at.WIDGETS_USER_EVENT &&
          action.data?.user_action === "open_source"
      );
      expect(evt).toBeTruthy();
      expect(evt[0].data.widget_name).toBe("picture_of_the_day");
    });
  });

  describe("records interaction on every action (cross-widget ask)", () => {
    const populated = {
      ...mockState,
      PictureOfTheDay: {
        ...INITIAL_STATE.PictureOfTheDay,
        imageUrl: "https://example.com/potd.jpg",
        thumbnailUrl: "https://example.com/potd-thumb.jpg",
        title: "Test picture",
        description: "A test picture description.",
        publishedDate: "2026-07-01",
      },
    };

    const sourced = {
      ...populated,
      PictureOfTheDay: {
        ...populated.PictureOfTheDay,
        sourceUrl: "https://commons.wikimedia.org/wiki/File:Example.jpg",
        licenseLabel: "CC BY-SA 4.0",
        licenseUrl: "https://creativecommons.org/licenses/by-sa/4.0/",
      },
    };

    it.each([
      [
        "learn more",
        mockState,
        '[data-l10n-id="newtab-picture-menu-learn-more"]',
      ],
      [
        "manage wallpaper",
        mockState,
        '[data-l10n-id="newtab-picture-menu-manage-wallpaper"]',
      ],
      ["show (eye)", mockState, ".picture-of-the-day-show-button"],
      [
        "change size",
        mockState,
        '#picture-of-the-day-size-submenu [data-size="large"]',
      ],
      ["set wallpaper", populated, ".picture-of-the-day-set-wallpaper"],
      [
        "hide today's picture",
        populated,
        '[data-l10n-id="newtab-picture-menu-hide-photo2"]',
      ],
      ["open source", sourced, ".picture-of-the-day-source-link"],
      [
        "open license",
        sourced,
        '[data-l10n-id="newtab-picture-attribution-license"]',
      ],
    ])(
      "flips widgets.pictureOfTheDay.interaction when %s is used",
      (_label, state, selector) => {
        const { container, handleUserInteraction } = renderWidget(
          jest.fn(),
          {},
          state
        );
        fireEvent.click(container.querySelector(selector));
        expect(handleUserInteraction).toHaveBeenCalledWith("pictureOfTheDay");
      }
    );
  });
});

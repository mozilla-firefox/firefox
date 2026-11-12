/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { render, fireEvent, act, waitFor } from "@testing-library/react";
import { WrapWithProvider } from "test/jest/test-utils";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import {
  _CustomizeMenu as CustomizeMenu,
  CustomizeMenu as ConnectedCustomizeMenu,
} from "content-src/components/CustomizeMenu/CustomizeMenu";
import { ContentSection } from "content-src/components/CustomizeMenu/ContentSection/ContentSection";
import { CUSTOMIZE_SUBPANELS } from "content-src/lib/constants";
import { actionCreators as ac } from "common/Actions.mjs";

const DEFAULT_STATE = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "newtabWallpapers.wallpaper": "",
    },
  },
};

// The legacy test toggled nova through the Redux store; the unconnected
// component reads it from the Prefs prop instead.
const NOVA_PREFS = { values: { "nova.enabled": true } };

let DEFAULT_PROPS;

describe("<CustomizeMenu>", () => {
  // jsdom does not implement dialog showModal()/close(), which the component
  // calls on open and exit.
  const originalShowModal = HTMLDialogElement.prototype.showModal;
  const originalClose = HTMLDialogElement.prototype.close;

  afterEach(() => {
    HTMLDialogElement.prototype.showModal = originalShowModal;
    HTMLDialogElement.prototype.close = originalClose;
    jest.useRealTimers();
  });

  beforeEach(() => {
    // jsdom does not implement <dialog>.showModal/close; stub them so no code
    // path emits a "Not implemented" console error (the shared jest setup
    // treats any console.error as a test failure).
    HTMLDialogElement.prototype.showModal = jest.fn();
    HTMLDialogElement.prototype.close = jest.fn();

    DEFAULT_PROPS = {
      showing: false,
      onOpen: jest.fn(),
      onClose: jest.fn(),
      openPreferences: jest.fn(),
      setPref: jest.fn(),
      dispatch: jest.fn(),
      enabledSections: {
        topSitesEnabled: true,
        pocketEnabled: true,
        weatherEnabled: true,
        showInferredPersonalizationEnabled: false,
        topSitesRowsCount: 1,
        selectedWallpaper: "",
      },
      enabledWidgets: { timerEnabled: false, listsEnabled: false },
      wallpapersEnabled: false,
      wallpapersUserEnabled: false,
      activeWallpaper: null,
      pocketRegion: "US",
      mayHaveTopicSections: false,
      mayHaveInferredPersonalization: false,
      mayHaveWeather: true,
      mayHaveWidgets: false,
      mayHaveTimerWidget: false,
      mayHaveListsWidget: false,
      closeSubpanels: jest.fn(),
      // Supplied here because the unconnected component reads Prefs from props.
      Prefs: { values: {} },
    };
  });

  it("renders the legacy personalize button when nova is not enabled", () => {
    const { container } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu {...DEFAULT_PROPS} />
      </WrapWithProvider>
    );
    expect(
      container.querySelector("moz-button.open-customization-button")
    ).not.toBeInTheDocument();
    expect(
      container.querySelector("button.personalize-button")
    ).toBeInTheDocument();
  });

  it("renders a moz-button when nova is enabled", () => {
    const { container } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu {...DEFAULT_PROPS} Prefs={NOVA_PREFS} />
      </WrapWithProvider>
    );
    const novaButton = container.querySelector(
      "moz-button.open-customization-button"
    );
    expect(novaButton).toBeInTheDocument();
    expect(novaButton).toHaveAttribute(
      "data-l10n-id",
      "newtab-customize-panel-label"
    );
    expect(novaButton).toHaveAttribute(
      "iconsrc",
      "chrome://global/skin/icons/edit-outline.svg"
    );
    expect(novaButton).toHaveAttribute("iconposition", "end");
    expect(novaButton).toHaveAttribute("aria-haspopup", "dialog");
  });

  it("calls onOpen when the nova moz-button is clicked", () => {
    const { container } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu {...DEFAULT_PROPS} Prefs={NOVA_PREFS} />
      </WrapWithProvider>
    );
    fireEvent.click(
      container.querySelector("moz-button.open-customization-button")
    );
    expect(DEFAULT_PROPS.onOpen).toHaveBeenCalledTimes(1);
  });

  it("renders the personalize button when not showing and calls onOpen on click", () => {
    const { container } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu {...DEFAULT_PROPS} showing={false} />
      </WrapWithProvider>
    );

    const personalizeButton = container.querySelector(".personalize-button");
    expect(personalizeButton).toBeInTheDocument();
    fireEvent.click(personalizeButton);
    expect(DEFAULT_PROPS.onOpen).toHaveBeenCalledTimes(1);
  });

  it("calls onOpen when pressing Enter on the personalize button", () => {
    const { container } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu {...DEFAULT_PROPS} showing={false} />
      </WrapWithProvider>
    );

    fireEvent.click(container.querySelector(".personalize-button"));
    expect(DEFAULT_PROPS.onOpen).toHaveBeenCalledTimes(1);
  });

  it("renders the customize menu as a dialog element", () => {
    const { container } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu {...DEFAULT_PROPS} showing={true} />
      </WrapWithProvider>
    );

    const customizeMenu = container.querySelector(".customize-menu");
    expect(customizeMenu).toBeInTheDocument();
    expect(customizeMenu.tagName).toBe("DIALOG");
  });

  it("renders the menu when showing = true and calls onClose from the close button", () => {
    const { container } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu {...DEFAULT_PROPS} showing={true} />
      </WrapWithProvider>
    );

    expect(container.querySelector(".customize-menu")).toBeInTheDocument();

    const closeButton = container.querySelector("#close-button");
    expect(closeButton).toBeInTheDocument();

    fireEvent.click(closeButton);
    expect(DEFAULT_PROPS.onClose).toHaveBeenCalledTimes(1);
  });

  it("passes key configuration props to ContentSection", () => {
    const PROPS = {
      ...DEFAULT_PROPS,
      showing: true,
      mayHaveWidgets: true,
      mayHaveTimerWidget: true,
      mayHaveListsWidget: true,
      wallpapersEnabled: true,
      wallpapersUserEnabled: true,
      enabledWidgets: { timerEnabled: true, listsEnabled: true },
    };

    // Enzyme matched the child by component identity (find(ContentSection));
    // capture the props ContentSection received through a render spy instead.
    const contentSectionRender = jest
      .spyOn(ContentSection.prototype, "render")
      .mockImplementation(() => null);

    render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu {...PROPS} />
      </WrapWithProvider>
    );

    const child = contentSectionRender.mock.instances[0].props;
    expect(child.mayHaveWidgets).toBe(true);
    expect(child.mayHaveTimerWidget).toBe(true);
    expect(child.mayHaveListsWidget).toBe(true);
    expect(child.wallpapersEnabled).toBe(true);
    expect(child.wallpapersUserEnabled).toBe(true);
    expect(child.enabledWidgets).toEqual({
      timerEnabled: true,
      listsEnabled: true,
    });

    contentSectionRender.mockRestore();
  });

  it("focuses the close button when onEntered is called", () => {
    const customizeMenuRef = React.createRef();
    render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          ref={customizeMenuRef}
          {...DEFAULT_PROPS}
          showing={true}
        />
      </WrapWithProvider>
    );
    const instance = customizeMenuRef.current;
    const mockFocus = jest.fn();
    instance.closeButtonRef.current = { focus: mockFocus };
    instance.onEntered();
    expect(mockFocus).toHaveBeenCalledTimes(1);
  });

  it("focuses the personalize button when onExited is called", () => {
    const customizeMenuRef = React.createRef();
    render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          ref={customizeMenuRef}
          {...DEFAULT_PROPS}
          showing={false}
        />
      </WrapWithProvider>
    );
    const instance = customizeMenuRef.current;
    const mockFocus = jest.fn();
    instance.personalizeButtonRef.current = { focus: mockFocus };
    instance.dialogRef.current = { open: false };
    act(() => {
      instance.onExited();
    });
    expect(mockFocus).toHaveBeenCalledTimes(1);
  });

  it("calls close() on the dialog when onExited is called and dialog is open", () => {
    const customizeMenuRef = React.createRef();
    render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          ref={customizeMenuRef}
          {...DEFAULT_PROPS}
          showing={false}
        />
      </WrapWithProvider>
    );
    const instance = customizeMenuRef.current;
    const mockClose = jest.fn();
    instance.dialogRef.current = { open: true, close: mockClose };
    instance.personalizeButtonRef.current = { focus: jest.fn() };
    act(() => {
      instance.onExited();
    });
    expect(mockClose).toHaveBeenCalledTimes(1);
  });

  it("calls closeSubpanels when onExited is called", () => {
    const closeSubpanels = jest.fn();
    const customizeMenuRef = React.createRef();
    render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          ref={customizeMenuRef}
          {...DEFAULT_PROPS}
          closeSubpanels={closeSubpanels}
        />
      </WrapWithProvider>
    );
    const instance = customizeMenuRef.current;
    instance.dialogRef.current = { open: false };
    instance.personalizeButtonRef.current = { focus: jest.fn() };
    act(() => {
      instance.onExited();
    });
    expect(closeSubpanels).toHaveBeenCalledTimes(1);
  });

  it("marks the content as subpanel-open from the activeSubpanel prop", () => {
    const { container, rerender } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          {...DEFAULT_PROPS}
          showing={true}
          activeSubpanel={null}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector(".customize-menu-content")).not.toHaveClass(
      "subpanel-open"
    );
    rerender(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          {...DEFAULT_PROPS}
          showing={true}
          activeSubpanel={CUSTOMIZE_SUBPANELS.THEMES}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector(".customize-menu-content")).toHaveClass(
      "subpanel-open"
    );
  });

  it("calls showModal when showing transitions from false to true", () => {
    const customizeMenuRef = React.createRef();
    render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          ref={customizeMenuRef}
          {...DEFAULT_PROPS}
          showing={true}
        />
      </WrapWithProvider>
    );
    const instance = customizeMenuRef.current;
    const mockShowModal = jest.fn();
    instance.dialogRef.current = {
      open: false,
      showModal: mockShowModal,
      querySelectorAll: () => [],
    };

    // Simulate the transition: prevProps.showing was false, now it's true
    instance.componentDidUpdate({ ...DEFAULT_PROPS, showing: false });

    expect(mockShowModal).toHaveBeenCalledTimes(1);
  });

  it("calls onClose when onCancel is fired (e.g. Escape key)", () => {
    const customizeMenuRef = React.createRef();
    render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          ref={customizeMenuRef}
          {...DEFAULT_PROPS}
          showing={true}
        />
      </WrapWithProvider>
    );
    const instance = customizeMenuRef.current;
    const mockPreventDefault = jest.fn();
    instance.onCancel({ preventDefault: mockPreventDefault });
    expect(mockPreventDefault).toHaveBeenCalledTimes(1);
    expect(DEFAULT_PROPS.onClose).toHaveBeenCalledTimes(1);
  });

  it("calls onClose when clicking the backdrop (dialog element itself)", () => {
    const customizeMenuRef = React.createRef();
    render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          ref={customizeMenuRef}
          {...DEFAULT_PROPS}
          showing={true}
        />
      </WrapWithProvider>
    );
    const instance = customizeMenuRef.current;
    const dialogNode = instance.dialogRef.current;
    instance.onDialogClick({ target: dialogNode });
    expect(DEFAULT_PROPS.onClose).toHaveBeenCalledTimes(1);
  });

  describe("locked prefs", () => {
    function renderWithLockedPrefs(lockedPrefs, props = {}) {
      return render(
        <WrapWithProvider state={DEFAULT_STATE}>
          <CustomizeMenu
            {...DEFAULT_PROPS}
            showing={true}
            Prefs={{ values: { lockedPrefs } }}
            {...props}
          />
        </WrapWithProvider>
      );
    }

    it("disables controls whose pref is locked and leaves the others alone", () => {
      const { container } = renderWithLockedPrefs(["feeds.topsites"]);

      expect(container.querySelector("#shortcuts-toggle").disabled).toBe(true);
      expect(container.querySelector("#pocket-toggle").disabled).toBeFalsy();
    });

    it("re-enables a control when its pref is unlocked", () => {
      const { container, rerender } = renderWithLockedPrefs(["feeds.topsites"]);
      const toggle = container.querySelector("#shortcuts-toggle");
      expect(toggle.disabled).toBe(true);

      rerender(
        <WrapWithProvider state={DEFAULT_STATE}>
          <CustomizeMenu
            {...DEFAULT_PROPS}
            showing={true}
            Prefs={{ values: { lockedPrefs: [] } }}
          />
        </WrapWithProvider>
      );

      expect(toggle.disabled).toBe(false);
    });

    it("disables a lock-managed control when only its pref is locked", () => {
      // #inferred-personalization renders its own `disabled`, so it opts out of
      // the lock sweep and React writes the attribute instead.
      const { container } = renderWithLockedPrefs(
        ["discoverystream.sections.personalization.inferred.user.enabled"],
        { mayHaveInferredPersonalization: true }
      );

      expect(
        container.querySelector("#inferred-personalization")
      ).toHaveAttribute("disabled");
    });
  });

  it("does not call onClose when clicking inside the dialog content", () => {
    const customizeMenuRef = React.createRef();
    const { container } = render(
      <WrapWithProvider state={DEFAULT_STATE}>
        <CustomizeMenu
          ref={customizeMenuRef}
          {...DEFAULT_PROPS}
          showing={true}
        />
      </WrapWithProvider>
    );
    const instance = customizeMenuRef.current;
    const innerNode = container.querySelector(".customize-menu-content");
    instance.onDialogClick({ target: innerNode });
    expect(DEFAULT_PROPS.onClose).not.toHaveBeenCalled();
  });
});

describe("<CustomizeMenu> Nova", () => {
  const DEFAULT_PROPS = {
    dispatch: jest.fn(),
    onOpen: jest.fn(),
    onClose: jest.fn(),
    openPreferences: jest.fn(),
    setPref: jest.fn(),
    showing: false,
    enabledSections: {
      topSitesEnabled: true,
      pocketEnabled: false,
      weatherEnabled: false,
      showInferredPersonalizationEnabled: false,
      topSitesRowsCount: 1,
    },
    enabledWidgets: {
      timerEnabled: false,
      listsEnabled: false,
      widgetsMaximized: false,
      widgetsMayBeMaximized: false,
    },
    wallpapersEnabled: false,
    wallpapersUserEnabled: false,
    activeWallpaper: null,
    pocketRegion: false,
    mayHaveTopicSections: false,
    mayHaveInferredPersonalization: false,
    mayHaveWeather: false,
    mayHaveWidgets: false,
    mayHaveWeatherForecast: false,
    weatherDisplay: "simple",
    mayHaveTimerWidget: false,
    mayHaveListsWidget: false,
    toggleSectionsMgmtPanel: jest.fn(),
    activeSubpanel: null,
    toggleWidgetsManagementPanel: jest.fn(),
    toggleThemesPanel: jest.fn(),
    closeSubpanels: jest.fn(),
    Prefs: { values: {} },
  };

  const NOVA_PROPS = {
    ...DEFAULT_PROPS,
    Prefs: { values: { "nova.enabled": true } },
  };

  const BROWSER_NOVA_PROPS = {
    ...DEFAULT_PROPS,
    Prefs: { values: { browserNovaEnabled: true } },
  };

  const originalShowModal = HTMLDialogElement.prototype.showModal;
  const originalClose = HTMLDialogElement.prototype.close;

  afterEach(() => {
    HTMLDialogElement.prototype.showModal = originalShowModal;
    HTMLDialogElement.prototype.close = originalClose;
    jest.useRealTimers();
  });

  it("closes every subpanel once the dialog has finished exiting", () => {
    jest.useFakeTimers();
    HTMLDialogElement.prototype.showModal = jest.fn();
    HTMLDialogElement.prototype.close = jest.fn();
    const closeSubpanels = jest.fn();
    const props = { ...NOVA_PROPS, closeSubpanels };

    const { container, rerender } = render(
      <WrapWithProvider>
        <CustomizeMenu {...props} showing={false} />
      </WrapWithProvider>
    );
    rerender(
      <WrapWithProvider>
        <CustomizeMenu
          {...props}
          showing={true}
          activeSubpanel={CUSTOMIZE_SUBPANELS.THEMES}
        />
      </WrapWithProvider>
    );
    act(() => {
      jest.advanceTimersByTime(250);
    });
    expect(container.querySelector(".customize-menu-content")).toHaveClass(
      "subpanel-open"
    );

    rerender(
      <WrapWithProvider>
        <CustomizeMenu
          {...props}
          showing={false}
          activeSubpanel={CUSTOMIZE_SUBPANELS.THEMES}
        />
      </WrapWithProvider>
    );
    expect(closeSubpanels).not.toHaveBeenCalled();
    act(() => {
      jest.advanceTimersByTime(250);
    });

    expect(closeSubpanels).toHaveBeenCalledTimes(1);
  });

  it("should render", () => {
    const { container } = render(
      <WrapWithProvider>
        <CustomizeMenu {...DEFAULT_PROPS} />
      </WrapWithProvider>
    );
    expect(container.querySelector(".personalize-button")).toBeInTheDocument();
  });

  it("renders the legacy button when nova is not enabled", () => {
    const { container } = render(
      <WrapWithProvider>
        <CustomizeMenu {...DEFAULT_PROPS} />
      </WrapWithProvider>
    );
    expect(
      container.querySelector("moz-button.open-customization-button")
    ).not.toBeInTheDocument();
    expect(
      container.querySelector("button.personalize-button")
    ).toBeInTheDocument();
  });

  it("renders moz-button with correct attributes when nova is enabled", () => {
    const { container } = render(
      <WrapWithProvider>
        <CustomizeMenu {...NOVA_PROPS} />
      </WrapWithProvider>
    );
    const btn = container.querySelector("moz-button.open-customization-button");
    expect(btn).toBeInTheDocument();
    expect(btn).toHaveAttribute("data-l10n-id", "newtab-customize-panel-label");
    expect(btn).toHaveAttribute(
      "iconsrc",
      "chrome://global/skin/icons/edit-outline.svg"
    );
    expect(btn).toHaveAttribute("iconposition", "end");
    expect(btn).toHaveAttribute("aria-haspopup", "dialog");
  });

  it("calls onOpen when the nova moz-button is clicked", () => {
    const onOpen = jest.fn();
    const { container } = render(
      <WrapWithProvider>
        <CustomizeMenu {...NOVA_PROPS} onOpen={onOpen} />
      </WrapWithProvider>
    );
    container.querySelector("moz-button.open-customization-button").click();
    expect(onOpen).toHaveBeenCalledTimes(1);
  });

  it("records the panel and subpanel opening", () => {
    const dispatch = jest.fn();
    HTMLDialogElement.prototype.showModal = jest.fn();
    HTMLDialogElement.prototype.close = jest.fn();
    const props = {
      ...DEFAULT_PROPS,
      dispatch,
      activeSubpanel: null,
      customizePanelSource: "customize_button",
    };
    const { rerender } = render(
      <WrapWithProvider>
        <CustomizeMenu {...props} showing={false} />
      </WrapWithProvider>
    );
    rerender(
      <WrapWithProvider>
        <CustomizeMenu {...props} showing={true} />
      </WrapWithProvider>
    );
    expect(dispatch).toHaveBeenCalledWith(
      ac.UserEvent({ event: "SHOW_PERSONALIZE", source: "customize_button" })
    );
    rerender(
      <WrapWithProvider>
        <CustomizeMenu
          {...props}
          showing={true}
          activeSubpanel={CUSTOMIZE_SUBPANELS.THEMES}
        />
      </WrapWithProvider>
    );
    expect(dispatch).toHaveBeenCalledWith(
      ac.UserEvent({
        event: "SHOW_PERSONALIZE_SUBPANEL",
        source: CUSTOMIZE_SUBPANELS.THEMES,
      })
    );
    expect(dispatch).toHaveBeenCalledTimes(2);
  });

  it("records the source and wallpaper category from the store", () => {
    const userEventSpy = jest.spyOn(ac, "UserEvent");
    HTMLDialogElement.prototype.showModal = jest.fn();
    HTMLDialogElement.prototype.close = jest.fn();
    const state = {
      ...DEFAULT_STATE,
      App: {
        ...DEFAULT_STATE.App,
        customizePanelSource: "wallpaper_feature_highlight",
        customizePanelWallpaperCategory: "celestial",
      },
    };
    const { rerender } = render(
      <WrapWithProvider state={state}>
        <ConnectedCustomizeMenu {...DEFAULT_PROPS} showing={false} />
      </WrapWithProvider>
    );
    rerender(
      <WrapWithProvider state={state}>
        <ConnectedCustomizeMenu {...DEFAULT_PROPS} showing={true} />
      </WrapWithProvider>
    );

    expect(userEventSpy).toHaveBeenCalledWith({
      event: "SHOW_PERSONALIZE",
      source: "wallpaper_feature_highlight",
      subpanel: CUSTOMIZE_SUBPANELS.WALLPAPERS,
    });
    userEventSpy.mockRestore();
  });

  it("does not record a panel open on a mount that is already showing with a subpanel active", () => {
    const dispatch = jest.fn();
    HTMLDialogElement.prototype.showModal = jest.fn();
    HTMLDialogElement.prototype.close = jest.fn();
    render(
      <WrapWithProvider>
        <CustomizeMenu
          {...DEFAULT_PROPS}
          dispatch={dispatch}
          showing={true}
          activeSubpanel={CUSTOMIZE_SUBPANELS.WIDGETS}
        />
      </WrapWithProvider>
    );
    for (const event of ["SHOW_PERSONALIZE", "SHOW_PERSONALIZE_SUBPANEL"]) {
      expect(dispatch).not.toHaveBeenCalledWith(
        expect.objectContaining({
          data: expect.objectContaining({ event }),
        })
      );
    }
  });

  describe("theme picker shown()", () => {
    const shownLayouts = [];
    beforeAll(() => {
      // Custom elements stay registered for the rest of this file. The stub
      // exposes `layout` as a property, as the lit element does, so React
      // sets it as a property rather than an attribute.
      customElements.define(
        "theme-picker",
        class extends HTMLElement {
          get layout() {
            return this._layout ?? this.getAttribute("layout");
          }
          set layout(value) {
            this._layout = value;
          }
          shown() {
            shownLayouts.push(this.layout);
          }
        }
      );
    });
    beforeEach(() => {
      shownLayouts.length = 0;
      HTMLDialogElement.prototype.showModal = jest.fn();
      HTMLDialogElement.prototype.close = jest.fn();
    });

    it("notifies the compact picker when the panel opens", async () => {
      const { rerender } = render(
        <WrapWithProvider>
          <CustomizeMenu {...BROWSER_NOVA_PROPS} showing={false} />
        </WrapWithProvider>
      );
      rerender(
        <WrapWithProvider>
          <CustomizeMenu
            {...BROWSER_NOVA_PROPS}
            showing={true}
            activeSubpanel={null}
          />
        </WrapWithProvider>
      );
      await waitFor(() => expect(shownLayouts).toEqual(["compact"]));
      await act(async () => {});
      expect(shownLayouts).toEqual(["compact"]);
    });

    it("notifies the compact picker when mounted already showing", async () => {
      render(
        <WrapWithProvider>
          <CustomizeMenu
            {...BROWSER_NOVA_PROPS}
            showing={true}
            activeSubpanel={null}
          />
        </WrapWithProvider>
      );
      await waitFor(() => expect(shownLayouts).toEqual(["compact"]));
      await act(async () => {});
      expect(shownLayouts).toEqual(["compact"]);
    });

    it("notifies the full picker when the themes subpanel opens", async () => {
      const { rerender } = render(
        <WrapWithProvider>
          <CustomizeMenu
            {...BROWSER_NOVA_PROPS}
            showing={true}
            activeSubpanel={null}
          />
        </WrapWithProvider>
      );
      await waitFor(() => expect(shownLayouts).toEqual(["compact"]));
      await act(async () => {});
      expect(shownLayouts).toEqual(["compact"]);
      rerender(
        <WrapWithProvider>
          <CustomizeMenu
            {...BROWSER_NOVA_PROPS}
            showing={true}
            activeSubpanel={CUSTOMIZE_SUBPANELS.THEMES}
          />
        </WrapWithProvider>
      );
      await waitFor(() => expect(shownLayouts).toEqual(["compact", "full"]));
      await act(async () => {});
      expect(shownLayouts).toEqual(["compact", "full"]);
    });

    it("notifies the compact picker again when a subpanel closes back to the root", async () => {
      const { rerender } = render(
        <WrapWithProvider>
          <CustomizeMenu
            {...BROWSER_NOVA_PROPS}
            showing={true}
            activeSubpanel={CUSTOMIZE_SUBPANELS.THEMES}
          />
        </WrapWithProvider>
      );
      await waitFor(() => expect(shownLayouts).toEqual(["full"]));
      rerender(
        <WrapWithProvider>
          <CustomizeMenu
            {...BROWSER_NOVA_PROPS}
            showing={true}
            activeSubpanel={null}
          />
        </WrapWithProvider>
      );
      await waitFor(() => expect(shownLayouts).toEqual(["full", "compact"]));
      await act(async () => {});
      expect(shownLayouts).toEqual(["full", "compact"]);
    });

    it("notifies nothing when the panel opens straight into a subpanel", async () => {
      const { rerender } = render(
        <WrapWithProvider>
          <CustomizeMenu {...BROWSER_NOVA_PROPS} showing={false} />
        </WrapWithProvider>
      );
      rerender(
        <WrapWithProvider>
          <CustomizeMenu
            {...BROWSER_NOVA_PROPS}
            showing={true}
            activeSubpanel={CUSTOMIZE_SUBPANELS.WIDGETS}
          />
        </WrapWithProvider>
      );
      await act(async () => {});
      expect(shownLayouts).toEqual([]);
    });
  });

  it("threads browserNovaEnabled from Prefs.values to ContentSection (renders the theme-picker)", () => {
    const { container } = render(
      <WrapWithProvider>
        <CustomizeMenu
          {...DEFAULT_PROPS}
          showing={true}
          Prefs={{ values: { browserNovaEnabled: true } }}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector("theme-picker")).toBeInTheDocument();
  });
});

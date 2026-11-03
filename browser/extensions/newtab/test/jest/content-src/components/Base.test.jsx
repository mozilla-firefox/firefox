/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import React from "react";
import { render, act } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { WrapWithProvider } from "test/jest/test-utils";
import { CUSTOMIZE_SUBPANELS } from "content-src/lib/constants";
import {
  Base as ConnectedBase,
  _Base as Base,
  BaseContent,
  WithDsAdmin,
} from "content-src/components/Base/Base";

// The legacy Enzyme suite used shallow(), which never mounted BaseContent's
// descendants. RTL renders the full tree, so every render is wrapped in a Redux
// Provider whose store carries safe string defaults (sectionOrder / wallpaper)
// that keep connected descendants (Sections, WallpaperCategories, ...) from
// throwing on undefined. This store is only there to keep the subtree alive;
// the assertions below observe the props-driven output of the unconnected
// components under test, mirroring the legacy expectations.
const SAFE_STORE_VALUES = {
  sectionOrder: "",
  "newtabWallpapers.wallpaper": "",
  "newtabWallpapers.initialWallpaper": "",
};

function makeStore(prefValues = {}, stateOverride = {}) {
  return createStore(combineReducers(reducers), {
    ...INITIAL_STATE,
    App: { ...INITIAL_STATE.App, initialized: true },
    Prefs: {
      ...INITIAL_STATE.Prefs,
      values: {
        ...INITIAL_STATE.Prefs.values,
        ...SAFE_STORE_VALUES,
        ...prefValues,
      },
    },
    ...stateOverride,
  });
}

// Full RTL mounting exercises BaseContent's lifecycle (componentDidMount reads
// DiscoveryStream.spocs and updateWallpaper reads Wallpapers); shallow() never
// did. These defaults keep the mount from throwing without affecting the
// legacy assertions.
const MOUNT_WALLPAPERS = { wallpaperList: [], uploadedWallpaper: null };

afterEach(() => {
  globalThis.location.hash = "";
});

describe("<Base>", () => {
  let DEFAULT_PROPS = {
    store: { getState: () => {} },
    App: { initialized: true },
    Prefs: { values: {} },
    Sections: [],
    DiscoveryStream: { config: { enabled: false } },
    dispatch: () => {},
    adminContent: {
      message: {},
    },
    document: {
      visibilityState: "visible",
      addEventListener: jest.fn(),
      removeEventListener: jest.fn(),
    },
  };

  function renderBase(props) {
    return render(
      <Provider store={makeStore(props.Prefs.values)}>
        <Base
          {...props}
          DiscoveryStream={{ ...props.DiscoveryStream, spocs: {} }}
          Wallpapers={MOUNT_WALLPAPERS}
        />
      </Provider>
    );
  }

  it("should render Base component", () => {
    const { container } = renderBase(DEFAULT_PROPS);
    // exists(): the Base subtree rendered (BaseContent's classic wrapper).
    expect(container.querySelector(".outer-wrapper")).toBeInTheDocument();
  });

  it("should render the BaseContent component, passing through all props", () => {
    // Enzyme read wrapper.find(BaseContent).props() and deep-equaled it against
    // DEFAULT_PROPS. RTL cannot read a child component's props object, so we
    // assert the closest observable proxy: Base delegated rendering to
    // BaseContent (its classic output is present).
    const { container } = renderBase(DEFAULT_PROPS);
    expect(container.querySelector(".outer-wrapper")).toBeInTheDocument();
  });

  it("should render an ErrorBoundary with class base-content-fallback", () => {
    // ErrorBoundary only applies its className in the fallback (on error), so
    // the class is not observable on the happy path. Force the top-level
    // boundary into its fallback (BaseContent throws on an undefined
    // DiscoveryStream) so "base-content-fallback" becomes observable in the DOM.
    const errorSpy = jest.spyOn(console, "error").mockImplementation(() => {});
    const { container } = render(
      <Provider store={makeStore(DEFAULT_PROPS.Prefs.values)}>
        <Base {...DEFAULT_PROPS} DiscoveryStream={undefined} />
      </Provider>
    );
    expect(
      container.querySelector(".base-content-fallback")
    ).toBeInTheDocument();
    errorSpy.mockRestore();
  });

  it("should render an WithDsAdmin if the devtools pref is true", () => {
    // find(WithDsAdmin) has no DOM identity; WithDsAdmin renders
    // DiscoveryStreamAdmin's ".discoverystream-admin-toggle", which only appears
    // via the WithDsAdmin path.
    const { container } = renderBase({
      ...DEFAULT_PROPS,
      Prefs: { values: { "asrouter.devtoolsEnabled": true } },
    });
    expect(
      container.querySelectorAll(".discoverystream-admin-toggle")
    ).toHaveLength(1);
  });

  it("should not render an WithDsAdmin if the devtools pref is false", () => {
    const { container } = renderBase({
      ...DEFAULT_PROPS,
      Prefs: { values: { "asrouter.devtoolsEnabled": false } },
    });
    expect(
      container.querySelectorAll(".discoverystream-admin-toggle")
    ).toHaveLength(0);
  });

  it("should not render without App.initialized", () => {
    const props = {
      App: { initialized: false },
      Prefs: { values: {} },
      dispatch: jest.fn(),
    };
    const { container } = render(
      <WrapWithProvider>
        <Base {...props} />
      </WrapWithProvider>
    );
    expect(
      container.querySelector(".base-content-fallback")
    ).not.toBeInTheDocument();
  });
});

describe("<BaseContent>", () => {
  let DEFAULT_PROPS = {
    store: { getState: () => {} },
    App: { initialized: true },
    Prefs: { values: {} },
    Sections: [],
    DiscoveryStream: { config: { enabled: false }, spocs: {} },
    dispatch: () => {},
    document: {
      visibilityState: "visible",
      addEventListener: jest.fn(),
      removeEventListener: jest.fn(),
    },
  };

  function renderBaseContent(props, ref) {
    return render(
      <Provider store={makeStore(props.Prefs.values)}>
        <BaseContent Wallpapers={MOUNT_WALLPAPERS} {...props} ref={ref} />
      </Provider>
    );
  }

  it("should render an ErrorBoundary with a Search child", () => {
    const searchEnabledProps = Object.assign({}, DEFAULT_PROPS, {
      Prefs: { values: { showSearch: true } },
    });

    const { container } = renderBaseContent(searchEnabledProps);

    // parent().is(ErrorBoundary) has no DOM equivalent (ErrorBoundary renders
    // its children transparently). Assert the closest observable proxy: Search
    // rendered inside the error-boundary-wrapped ".non-collapsible-section".
    expect(
      container.querySelector(".non-collapsible-section .search-wrapper")
    ).toBeInTheDocument();
  });

  it("dispatches SHOW_PERSONALIZE on open and HIDE_PERSONALIZE with its user event on close", () => {
    const dispatch = jest.fn();
    const ref = React.createRef();
    renderBaseContent(
      {
        ...DEFAULT_PROPS,
        dispatch,
        App: { customizeMenuVisible: true },
      },
      ref
    );
    ref.current.openCustomizationMenu();
    expect(dispatch).toHaveBeenCalledWith({ type: at.SHOW_PERSONALIZE });
    ref.current.closeCustomizationMenu();
    expect(dispatch).toHaveBeenCalledWith({ type: at.HIDE_PERSONALIZE });
    expect(dispatch).toHaveBeenCalledWith(
      ac.UserEvent({ event: "HIDE_PERSONALIZE" })
    );
  });

  it("opens and closes the wallpapers subpanel through Base state", () => {
    const ref = React.createRef();
    renderBaseContent(DEFAULT_PROPS, ref);

    act(() => {
      ref.current.openWallpapersPanel("celestial");
    });
    expect(ref.current.state).toMatchObject({
      activeSubpanel: CUSTOMIZE_SUBPANELS.WALLPAPERS,
      wallpapersPanelCategory: "celestial",
    });

    act(() => {
      ref.current.closeWallpapersPanel();
    });
    expect(ref.current.state.activeSubpanel).toBeNull();
    // Retained so the heading and list keep their content while the
    // subpanel slides out.
    expect(ref.current.state.wallpapersPanelCategory).toBe("celestial");
  });

  it("toggles a subpanel open and closed", () => {
    const ref = React.createRef();
    renderBaseContent(DEFAULT_PROPS, ref);

    act(() => {
      ref.current.toggleThemesPanel();
    });
    expect(ref.current.state.activeSubpanel).toBe(CUSTOMIZE_SUBPANELS.THEMES);
    act(() => {
      ref.current.toggleThemesPanel();
    });
    expect(ref.current.state.activeSubpanel).toBeNull();
  });

  it("keeps at most one subpanel open", () => {
    const ref = React.createRef();
    renderBaseContent(DEFAULT_PROPS, ref);

    act(() => {
      ref.current.toggleSectionsMgmtPanel();
    });
    act(() => {
      ref.current.toggleWidgetsManagementPanel();
    });
    expect(ref.current.state.activeSubpanel).toBe(CUSTOMIZE_SUBPANELS.WIDGETS);
    act(() => {
      ref.current.openWallpapersPanel("celestial");
    });
    expect(ref.current.state.activeSubpanel).toBe(
      CUSTOMIZE_SUBPANELS.WALLPAPERS
    );
    act(() => {
      ref.current.openWidgetsPanel();
    });
    expect(ref.current.state).toMatchObject({
      activeSubpanel: CUSTOMIZE_SUBPANELS.WIDGETS,
      wallpapersPanelCategory: "celestial",
    });
  });

  it("opens the widgets subpanel for #customize-widgets with Nova on", () => {
    globalThis.location.hash = "#customize-widgets";
    const dispatch = jest.fn();
    const ref = React.createRef();
    renderBaseContent(
      {
        ...DEFAULT_PROPS,
        dispatch,
        Prefs: {
          values: { "nova.enabled": true, "widgets.system.enabled": true },
        },
      },
      ref
    );

    expect(dispatch).toHaveBeenCalledWith({ type: at.SHOW_PERSONALIZE });
    expect(ref.current.state.activeSubpanel).toBe(CUSTOMIZE_SUBPANELS.WIDGETS);
  });

  it("opens only the customize panel for #customize-widgets with Nova on and the widgets container hidden", () => {
    globalThis.location.hash = "#customize-widgets";
    const dispatch = jest.fn();
    const ref = React.createRef();
    renderBaseContent(
      {
        ...DEFAULT_PROPS,
        dispatch,
        Prefs: {
          values: { "nova.enabled": true, "widgets.system.enabled": false },
        },
      },
      ref
    );

    expect(dispatch).toHaveBeenCalledWith({ type: at.SHOW_PERSONALIZE });
    expect(ref.current.state.activeSubpanel).toBeNull();
  });

  it("opens only the customize panel for #customize-widgets with Nova off", () => {
    globalThis.location.hash = "#customize-widgets";
    const dispatch = jest.fn();
    const ref = React.createRef();
    renderBaseContent(
      {
        ...DEFAULT_PROPS,
        dispatch,
        Prefs: { values: { "nova.enabled": false } },
      },
      ref
    );

    expect(dispatch).toHaveBeenCalledWith({ type: at.SHOW_PERSONALIZE });
    expect(ref.current.state.activeSubpanel).toBeNull();
  });

  it("opens the widgets subpanel when the hash changes to #customize-widgets on an open page", () => {
    const dispatch = jest.fn();
    const ref = React.createRef();
    renderBaseContent(
      {
        ...DEFAULT_PROPS,
        dispatch,
        Prefs: {
          values: { "nova.enabled": true, "widgets.system.enabled": true },
        },
      },
      ref
    );
    expect(ref.current.state.activeSubpanel).toBeNull();

    globalThis.location.hash = "#customize-widgets";
    act(() => {
      globalThis.dispatchEvent(new HashChangeEvent("hashchange"));
    });

    expect(dispatch).toHaveBeenCalledWith({ type: at.SHOW_PERSONALIZE });
    expect(ref.current.state.activeSubpanel).toBe(CUSTOMIZE_SUBPANELS.WIDGETS);
  });

  it("closeSubpanels closes the open subpanel and clears the wallpaper category", () => {
    const ref = React.createRef();
    renderBaseContent(DEFAULT_PROPS, ref);

    act(() => {
      ref.current.openWallpapersPanel("celestial");
    });
    act(() => {
      ref.current.closeSubpanels();
    });
    expect(ref.current.state).toMatchObject({
      activeSubpanel: null,
      wallpapersPanelCategory: null,
    });
  });

  it("closeWallpapersPanel leaves another open subpanel alone", () => {
    const ref = React.createRef();
    renderBaseContent(DEFAULT_PROPS, ref);

    act(() => {
      ref.current.toggleThemesPanel();
    });
    act(() => {
      ref.current.closeWallpapersPanel();
    });
    expect(ref.current.state.activeSubpanel).toBe(CUSTOMIZE_SUBPANELS.THEMES);
  });

  describe("subpanel wiring through the DOM", () => {
    let originalShowModal;
    let originalClose;
    beforeEach(() => {
      originalShowModal = HTMLDialogElement.prototype.showModal;
      originalClose = HTMLDialogElement.prototype.close;
      HTMLDialogElement.prototype.showModal = jest.fn();
      HTMLDialogElement.prototype.close = jest.fn();
      jest.useFakeTimers({ doNotFake: ["performance"] });
    });
    afterEach(() => {
      HTMLDialogElement.prototype.showModal = originalShowModal;
      HTMLDialogElement.prototype.close = originalClose;
      jest.useRealTimers();
    });

    describe.each([
      ["classic", {}],
      ["Nova", { "nova.enabled": true }],
    ])("wallpaper subpanel wiring, %s layout", (_layout, layoutPrefs) => {
      it("opens a category through Base state and resets it when the panel closes", () => {
        const prefValues = {
          "newtabWallpapers.enabled": true,
          ...layoutPrefs,
        };
        const store = makeStore(prefValues, {
          Wallpapers: {
            ...INITIAL_STATE.Wallpapers,
            wallpaperList: [
              { title: "moon", category: "celestial", theme: "light" },
            ],
            categories: ["celestial"],
          },
        });
        const ref = React.createRef();
        const props = {
          ...DEFAULT_PROPS,
          Prefs: { values: prefValues },
          App: {
            initialized: true,
            customizeMenuVisible: true,
            isForStartupCache: {},
          },
        };
        const { container, rerender } = render(
          <Provider store={store}>
            <BaseContent Wallpapers={MOUNT_WALLPAPERS} {...props} ref={ref} />
          </Provider>
        );

        act(() => {
          ref.current.openWallpapersPanel("celestial");
        });
        expect(
          container.querySelector(
            '.wallpaper-list [data-l10n-id="newtab-wallpaper-category-title-celestial"]'
          )
        ).toBeInTheDocument();
        expect(container.querySelector(".customize-menu-content")).toHaveClass(
          "subpanel-open"
        );

        rerender(
          <Provider store={store}>
            <BaseContent
              Wallpapers={MOUNT_WALLPAPERS}
              {...props}
              App={{
                initialized: true,
                customizeMenuVisible: false,
                isForStartupCache: {},
              }}
              ref={ref}
            />
          </Provider>
        );
        act(() => {
          jest.advanceTimersByTime(250);
        });
        expect(ref.current.state).toMatchObject({
          activeSubpanel: null,
          wallpapersPanelCategory: null,
        });
        expect(
          container.querySelector(".customize-menu-content")
        ).not.toHaveClass("subpanel-open");
      });
    });

    it("opening the themes subpanel replaces the wallpaper subpanel", () => {
      const prefValues = {
        "newtabWallpapers.enabled": true,
        "nova.enabled": true,
        browserNovaEnabled: true,
      };
      const store = makeStore(prefValues, {
        Wallpapers: {
          ...INITIAL_STATE.Wallpapers,
          wallpaperList: [
            { title: "moon", category: "celestial", theme: "light" },
          ],
          categories: ["celestial"],
        },
      });
      const ref = React.createRef();
      const props = {
        ...DEFAULT_PROPS,
        Prefs: { values: prefValues },
        App: {
          initialized: true,
          customizeMenuVisible: true,
          isForStartupCache: {},
        },
      };
      const { container } = render(
        <Provider store={store}>
          <BaseContent Wallpapers={MOUNT_WALLPAPERS} {...props} ref={ref} />
        </Provider>
      );

      act(() => {
        ref.current.openWallpapersPanel("celestial");
      });
      expect(container.querySelector(".wallpaper-list")).toBeInTheDocument();

      act(() => {
        ref.current.toggleThemesPanel();
      });
      expect(container.querySelector(".themes-mgmt-panel")).toBeInTheDocument();
      act(() => {
        jest.advanceTimersByTime(300);
      });
      expect(
        container.querySelector(".wallpaper-list")
      ).not.toBeInTheDocument();
    });
  });

  it("should render only search if no Sections are enabled", () => {
    const onlySearchProps = Object.assign({}, DEFAULT_PROPS, {
      Sections: [{ id: "highlights", enabled: false }],
      Prefs: { values: { showSearch: true } },
    });

    const { container } = renderBaseContent(onlySearchProps);
    expect(container.querySelectorAll(".only-search")).toHaveLength(1);
  });

  it("should not attach an event listener for visibility change if it is visible immediately", () => {
    const props = Object.assign({}, DEFAULT_PROPS, {
      document: {
        visibilityState: "visible",
        addEventListener: jest.fn(),
        removeEventListener: jest.fn(),
      },
    });

    renderBaseContent(props);
    expect(props.document.addEventListener).not.toHaveBeenCalled();
  });
  it("should attach an event listener for visibility change if it is not visible", () => {
    const props = Object.assign({}, DEFAULT_PROPS, {
      document: {
        visibilityState: "hidden",
        addEventListener: jest.fn(),
        removeEventListener: jest.fn(),
      },
    });

    renderBaseContent(props);
    expect(props.document.addEventListener).toHaveBeenCalledWith(
      "visibilitychange",
      expect.any(Function)
    );
  });
  it("should remove the event listener for visibility change when unmounted", () => {
    const props = Object.assign({}, DEFAULT_PROPS, {
      document: {
        visibilityState: "hidden",
        addEventListener: jest.fn(),
        removeEventListener: jest.fn(),
      },
    });

    const { unmount } = renderBaseContent(props);
    const [firstCall] = props.document.addEventListener.mock.calls;
    const [, listener] = firstCall;

    unmount();
    expect(props.document.removeEventListener).toHaveBeenCalledWith(
      "visibilitychange",
      listener
    );
  });
  it("should remove the event listener for visibility change after becoming visible", () => {
    const listeners = new Set();
    const props = Object.assign({}, DEFAULT_PROPS, {
      document: {
        visibilityState: "hidden",
        addEventListener: (ev, cb) => listeners.add(cb),
        removeEventListener: (ev, cb) => listeners.delete(cb),
      },
    });

    renderBaseContent(props);
    expect(listeners.size).toBe(1);

    // Simulate listeners getting called
    props.document.visibilityState = "visible";
    act(() => {
      listeners.forEach(l => l());
    });

    expect(listeners.size).toBe(0);
  });
});

describe("<BaseContent> wallpaper update logic", () => {
  const DOCUMENT_STUB = {
    visibilityState: "visible",
    addEventListener: jest.fn(),
    removeEventListener: jest.fn(),
  };

  const makeWallpaperProps = (prefsOverride = {}) => ({
    store: { getState: () => {} },
    App: { initialized: true, isForStartupCache: { Wallpaper: false } },
    Prefs: {
      values: {
        "newtabWallpapers.enabled": true,
        "newtabWallpapers.user.enabled": true,
        "newtabWallpapers.wallpaper": "beach",
        "newtabWallpapers.initialWallpaper": "",
        "newtabWallpapers.customWallpaper.theme": "",
        "nova.enabled": false,
        ...prefsOverride,
      },
    },
    Sections: [],
    DiscoveryStream: { config: { enabled: false }, spocs: {} },
    Wallpapers: { wallpaperList: [], uploadedWallpaper: null },
    dispatch: () => {},
    document: DOCUMENT_STUB,
  });

  // Reach the instance via a ref (white-box) to mirror wrapper.instance(), then
  // spy updateWallpaper as sinon.spy(instance, "updateWallpaper") did. The spy
  // is installed after mount so mount-time wallpaper work is not counted.
  function mountWallpaper(props) {
    const ref = React.createRef();
    render(
      <Provider store={makeStore()}>
        <BaseContent {...props} ref={ref} />
      </Provider>
    );
    const instance = ref.current;
    const updateSpy = jest.spyOn(instance, "updateWallpaper");
    return { instance, updateSpy };
  }

  it("should call updateWallpaper when wallpaper is just enabled (wasWallpaperActive false, isWallpaperActive true)", () => {
    const props = makeWallpaperProps();
    const { instance, updateSpy } = mountWallpaper(props);

    const prevProps = {
      ...props,
      Prefs: {
        values: {
          ...props.Prefs.values,
          "newtabWallpapers.enabled": false,
        },
      },
    };

    instance.componentDidUpdate(prevProps);
    expect(updateSpy).toHaveBeenCalledTimes(1);
  });

  it("should call updateWallpaper when wallpaper is just disabled (wasWallpaperActive true, isWallpaperActive false)", () => {
    const props = makeWallpaperProps({
      "newtabWallpapers.enabled": false,
    });
    const { instance, updateSpy } = mountWallpaper(props);

    const prevProps = {
      ...props,
      Prefs: {
        values: {
          ...props.Prefs.values,
          "newtabWallpapers.enabled": true,
        },
      },
    };

    instance.componentDidUpdate(prevProps);
    expect(updateSpy).toHaveBeenCalledTimes(1);
  });

  it("should call updateWallpaper when the selected wallpaper changes", () => {
    const props = makeWallpaperProps();
    const { instance, updateSpy } = mountWallpaper(props);

    const prevProps = {
      ...props,
      Prefs: {
        values: {
          ...props.Prefs.values,
          "newtabWallpapers.wallpaper": "mountains",
        },
      },
    };

    instance.componentDidUpdate(prevProps);
    expect(updateSpy).toHaveBeenCalledTimes(1);
  });

  it("should not call updateWallpaper when wallpaper is active but nothing changed", () => {
    const props = makeWallpaperProps();
    const { instance, updateSpy } = mountWallpaper(props);

    instance.componentDidUpdate(props);
    expect(updateSpy).not.toHaveBeenCalled();
  });

  it("should call updateWallpaper when uploadedWallpaper changes", () => {
    const props = makeWallpaperProps();
    const { instance, updateSpy } = mountWallpaper(props);

    const prevProps = {
      ...props,
      Wallpapers: { wallpaperList: [], uploadedWallpaper: "old-url" },
    };

    instance.componentDidUpdate(prevProps);
    expect(updateSpy).toHaveBeenCalledTimes(1);
  });

  it("should call updateWallpaper with Nova when both system and user prefs are enabled", () => {
    const props = makeWallpaperProps({ "nova.enabled": true });
    const { instance, updateSpy } = mountWallpaper(props);

    const prevProps = {
      ...props,
      Prefs: {
        values: {
          ...props.Prefs.values,
          "nova.enabled": true,
          "newtabWallpapers.wallpaper": "old-wallpaper",
        },
      },
    };

    instance.componentDidUpdate(prevProps);
    expect(updateSpy).toHaveBeenCalledTimes(1);
  });

  it("should not call updateWallpaper with Nova when user pref is disabled even if system pref is on", () => {
    const props = makeWallpaperProps({
      "nova.enabled": true,
      "newtabWallpapers.user.enabled": false,
    });
    const { instance, updateSpy } = mountWallpaper(props);

    const prevProps = {
      ...props,
      Prefs: {
        values: {
          ...props.Prefs.values,
          "nova.enabled": true,
          "newtabWallpapers.user.enabled": false,
          "newtabWallpapers.wallpaper": "old-wallpaper",
        },
      },
    };

    instance.componentDidUpdate(prevProps);
    expect(updateSpy).not.toHaveBeenCalled();
  });
});

function makeASRouterMessages({ position, isVisible = true } = {}) {
  return {
    isVisible,
    messageData: {
      content: {
        messageType: "ASRouterNewTabMessage",
        ...(position !== undefined ? { position } : {}),
      },
    },
  };
}

// The legacy helper used wrapper.find(Component) which matched React elements
// regardless of whether they produced DOM. In RTL we compare the document-order
// positions of the rendered marker nodes instead (message wrapper, TopSites,
// content feed). To give the content feed a real DOM node, the surrounding
// tests mount DiscoveryStreamBase against a store with a topstories section and
// a minimal layout so it renders ".ds-layout".
// Takes one or more container selectors, in document order, so callers can span
// .content and the .content-full-width band the feed now lives in.
function findASRouterMessagePositionIndices(container, ...containerSelectors) {
  const indices = { messageIdx: -1, topSitesIdx: -1, contentFeedIdx: -1 };
  const roots = containerSelectors
    .map(selector => container.querySelector(selector))
    .filter(Boolean);
  if (!roots.length) {
    return indices;
  }
  const ordered = roots.flatMap(root => [root, ...root.querySelectorAll("*")]);
  const positionOf = selector => {
    const node = ordered.find(candidate => candidate.matches(selector));
    return node ? ordered.indexOf(node) : -1;
  };
  indices.messageIdx = positionOf(".asrouter-newtab-message-wrapper");
  indices.topSitesIdx = positionOf(".top-sites");
  indices.contentFeedIdx = positionOf(".ds-layout");
  return indices;
}

// Store shape that lets the connected DiscoveryStreamBase render a real
// ".ds-layout" node (needed as the "content feed" marker), plus the topstories
// section its render requires.
const RENDERABLE_DISCOVERY_STREAM = {
  config: { enabled: true },
  layout: [{ components: [{ type: "HorizontalRule" }] }],
  feeds: { loaded: true, data: {} },
  spocs: { loaded: true, data: {}, blocked: [] },
  showTopicSelection: false,
};
const TOPSTORIES_SECTION = {
  id: "topstories",
  enabled: true,
  learnMore: { link: { href: "", message: "" } },
  pref: {},
  title: "",
  rows: [],
};

const HIGHLIGHTS_SECTION = {
  ...TOPSTORIES_SECTION,
  id: "highlights",
};

// storePrefs goes into the Redux store rather than BaseContent's props, for
// values the connected DiscoveryStreamBase reads itself (e.g. widgets gating).
// sections goes into the store too: DiscoveryStreamBase reads it to decide whether
// the Highlights wrapper renders at all.
function renderBaseContentWithFeed(
  props,
  storePrefs = {},
  sections = [TOPSTORIES_SECTION]
) {
  return render(
    <Provider
      store={makeStore(
        {
          "feeds.section.topstories": true,
          "feeds.system.topstories": true,
          ...storePrefs,
        },
        {
          DiscoveryStream: {
            ...INITIAL_STATE.DiscoveryStream,
            ...RENDERABLE_DISCOVERY_STREAM,
          },
          Sections: sections,
        }
      )}
    >
      <BaseContent Wallpapers={MOUNT_WALLPAPERS} {...props} />
    </Provider>
  );
}

describe("<BaseContent> Nova layout ASRouterNewTabMessage positions", () => {
  const DOCUMENT_STUB = {
    visibilityState: "visible",
    addEventListener: jest.fn(),
    removeEventListener: jest.fn(),
  };

  const NOVA_BASE_PROPS = {
    store: { getState: () => {} },
    App: { initialized: true },
    Prefs: {
      values: {
        "nova.enabled": true,
        "feeds.topsites": true,
      },
    },
    Sections: [],
    DiscoveryStream: {
      config: { enabled: true },
      spocs: {},
      feeds: { loaded: true },
      showTopicSelection: false,
    },
    dispatch: () => {},
    document: DOCUMENT_STUB,
  };

  it("does not render ASRouterNewTabMessage when there is no message", () => {
    const { container } = renderBaseContentWithFeed(NOVA_BASE_PROPS);
    expect(
      container.querySelectorAll(".asrouter-newtab-message-wrapper")
    ).toHaveLength(0);
  });

  it("does not render ASRouterNewTabMessage when isVisible is false", () => {
    const { container } = renderBaseContentWithFeed({
      ...NOVA_BASE_PROPS,
      Messages: makeASRouterMessages({ isVisible: false }),
    });
    expect(
      container.querySelectorAll(".asrouter-newtab-message-wrapper")
    ).toHaveLength(0);
  });

  it("renders exactly one ASRouterNewTabMessage for any configured position", () => {
    for (const position of [
      "ABOVE_TOPSITES",
      "ABOVE_WIDGETS",
      "ABOVE_CONTENT_FEED",
    ]) {
      const { container, unmount } = renderBaseContentWithFeed({
        ...NOVA_BASE_PROPS,
        Messages: makeASRouterMessages({ position }),
      });
      expect(
        container.querySelectorAll(".asrouter-newtab-message-wrapper")
      ).toHaveLength(1);
      unmount();
    }
  });

  it("renders ASRouterNewTabMessage before TopSites for ABOVE_TOPSITES", () => {
    const { container } = renderBaseContentWithFeed({
      ...NOVA_BASE_PROPS,
      Messages: makeASRouterMessages({ position: "ABOVE_TOPSITES" }),
    });
    const { messageIdx, topSitesIdx } = findASRouterMessagePositionIndices(
      container,
      ".content",
      ".content-full-width"
    );

    expect(topSitesIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeLessThan(topSitesIdx);
  });

  // The two positions below both land before the content feed, so ordering
  // alone cannot tell them apart. Each also asserts which container it rendered
  // in: ABOVE_WIDGETS sits at the end of .content, above the whole band, while
  // ABOVE_CONTENT_FEED is slotted inside the band between the widgets and feed.
  it("renders ASRouterNewTabMessage in .content after TopSites and before the content feed for ABOVE_WIDGETS", () => {
    const { container } = renderBaseContentWithFeed({
      ...NOVA_BASE_PROPS,
      Messages: makeASRouterMessages({ position: "ABOVE_WIDGETS" }),
    });
    const { messageIdx, topSitesIdx, contentFeedIdx } =
      findASRouterMessagePositionIndices(
        container,
        ".content",
        ".content-full-width"
      );

    expect(topSitesIdx).toBeGreaterThan(-1);
    expect(contentFeedIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(topSitesIdx);
    expect(messageIdx).toBeLessThan(contentFeedIdx);
    expect(
      container.querySelector(".content .asrouter-newtab-message-wrapper")
    ).not.toBeNull();
  });

  it("renders ASRouterNewTabMessage inside the content band before the content feed for ABOVE_CONTENT_FEED", () => {
    const { container } = renderBaseContentWithFeed({
      ...NOVA_BASE_PROPS,
      Messages: makeASRouterMessages({ position: "ABOVE_CONTENT_FEED" }),
    });
    const { messageIdx, topSitesIdx, contentFeedIdx } =
      findASRouterMessagePositionIndices(
        container,
        ".content",
        ".content-full-width"
      );

    expect(topSitesIdx).toBeGreaterThan(-1);
    expect(contentFeedIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(topSitesIdx);
    expect(messageIdx).toBeLessThan(contentFeedIdx);
    expect(
      container.querySelector(
        ".content-full-width .asrouter-newtab-message-wrapper"
      )
    ).not.toBeNull();
  });

  // The cases above leave widgets off, so their "before the content feed" check
  // matches the first .ds-layout in the band and cannot see the widgets/feed
  // boundary. ABOVE_CONTENT_FEED has to land on the far side of the widgets,
  // so pin that ordering against the two markers directly.
  it("renders ASRouterNewTabMessage after the widgets for ABOVE_CONTENT_FEED", () => {
    const { container } = renderBaseContentWithFeed(
      {
        ...NOVA_BASE_PROPS,
        Messages: makeASRouterMessages({ position: "ABOVE_CONTENT_FEED" }),
      },
      { "widgets.system.enabled": true }
    );
    const band = container.querySelector(".content-full-width");
    const ordered = [band, ...band.querySelectorAll("*")];
    const indexOfNode = selector =>
      ordered.indexOf(band.querySelector(selector));

    const widgetsIdx = indexOfNode(".ds-layout-widgets");
    const feedIdx = indexOfNode(".ds-layout:not(.discovery-stream)");
    const messageIdx = indexOfNode(".asrouter-newtab-message-wrapper");

    expect(widgetsIdx).toBeGreaterThan(-1);
    expect(feedIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeGreaterThan(widgetsIdx);
    expect(messageIdx).toBeLessThan(feedIdx);
  });
});

describe("<BaseContent> Nova layout variant class", () => {
  const DOCUMENT_STUB = {
    visibilityState: "visible",
    addEventListener: jest.fn(),
    removeEventListener: jest.fn(),
  };

  // Base.jsx reads props.Prefs.values, not the store.
  const propsWithPrefs = prefs => ({
    store: { getState: () => {} },
    App: { initialized: true },
    Prefs: {
      values: {
        "nova.enabled": true,
        "feeds.topsites": true,
        "feeds.section.topstories": true,
        "feeds.system.topstories": true,
        // Needs a widget in the content area, not just the container on.
        "widgets.system.enabled": true,
        "widgets.enabled": true,
        "widgets.system.lists.enabled": true,
        "widgets.lists.enabled": true,
        ...prefs,
      },
    },
    Sections: [],
    DiscoveryStream: {
      config: { enabled: true },
      spocs: {},
      feeds: { loaded: true },
      showTopicSelection: false,
    },
    dispatch: () => {},
    document: DOCUMENT_STUB,
  });

  const bandClassList = prefs => {
    const { container } = renderBaseContentWithFeed(propsWithPrefs(prefs), {
      "widgets.system.enabled": true,
    });
    return [...container.querySelector(".content-full-width").classList];
  };

  it("puts the lead class on the band for each side-by-side variant", () => {
    expect(
      bandClassList({ "pageLayouts.variant": "side-by-side-content-lead" })
    ).toContain("side-by-side-content-lead");
    expect(
      bandClassList({ "pageLayouts.variant": "side-by-side-widgets-lead" })
    ).toContain("side-by-side-widgets-lead");
  });

  // The four-column variants must not pick up the modifier that unlocks the
  // fourth content card.
  it("adds no width modifier for the four-column variants", () => {
    expect(
      bandClassList({ "pageLayouts.variant": "side-by-side-content-lead" })
    ).not.toContain("side-by-side-five");
    expect(
      bandClassList({ "pageLayouts.variant": "side-by-side-widgets-lead" })
    ).not.toContain("side-by-side-five");
  });

  // The shared lead class is what every side-by-side rule keys off, and CSS
  // matches per token, so the "-five" variant name must not be the class.
  it.each([
    ["side-by-side-content-lead-five", "side-by-side-content-lead"],
    ["side-by-side-widgets-lead-five", "side-by-side-widgets-lead"],
  ])("gives %s the lead class plus side-by-side-five", (variant, lead) => {
    const classes = bandClassList({ "pageLayouts.variant": variant });
    expect(classes).toContain(lead);
    expect(classes).toContain("side-by-side-five");
    expect(classes).not.toContain(variant);
  });

  // Drops the section panel in _Grid.scss; same condition that un-sidebars the logo.
  it("marks the band highlights-only when the feed and widgets are both off", () => {
    expect(
      bandClassList({
        "feeds.section.topstories": false,
        "widgets.enabled": false,
      })
    ).toContain("highlights-only");
  });

  it.each([
    ["the feed is on", { "widgets.enabled": false }],
    ["a content-area widget is on", { "feeds.section.topstories": false }],
  ])("is not highlights-only while %s", (_label, prefs) => {
    expect(bandClassList(prefs)).not.toContain("highlights-only");
  });

  it("marks the layout active when both sections are there", () => {
    expect(
      bandClassList({ "pageLayouts.variant": "side-by-side-content-lead" })
    ).toContain("side-by-side-active");
  });

  it("adds no classes at all for the default layout", () => {
    expect(bandClassList({ "pageLayouts.variant": "nova-full-width" })).toEqual(
      ["content-full-width"]
    );
    expect(bandClassList({})).toEqual(["content-full-width"]);
  });

  // The variant class stays so the lone section keeps its panel; only the
  // two-column layout drops out.
  it.each([
    ["stories are off", { "feeds.section.topstories": false }],
    [
      "the widgets container is not visible",
      { "widgets.system.enabled": false },
    ],
    ["the widgets toggle is off", { "widgets.enabled": false }],
    ["every widget is hidden", { "widgets.lists.enabled": false }],
  ])("keeps the variant class but is not active when %s", (_label, prefs) => {
    const classes = bandClassList({
      "pageLayouts.variant": "side-by-side-content-lead",
      ...prefs,
    });
    expect(classes).toContain("side-by-side-content-lead");
    expect(classes).not.toContain("side-by-side-active");
  });

  it("honours a variant set only through trainhopConfig", () => {
    expect(
      bandClassList({
        trainhopConfig: {
          pageLayouts: { variant: "side-by-side-widgets-lead" },
        },
      })
    ).toContain("side-by-side-widgets-lead");
    expect(
      bandClassList({
        trainhopConfig: {
          pageLayouts: { variant: "side-by-side-content-lead-five" },
        },
      })
    ).toEqual(
      expect.arrayContaining([
        "side-by-side-content-lead",
        "side-by-side-five",
        "side-by-side-active",
      ])
    );
  });
});

describe("<BaseContent> non-Nova classic layout ASRouterNewTabMessage positions", () => {
  const DOCUMENT_STUB = {
    visibilityState: "visible",
    addEventListener: jest.fn(),
    removeEventListener: jest.fn(),
  };

  const NON_NOVA_BASE_PROPS = {
    store: { getState: () => {} },
    App: { initialized: true },
    Prefs: {
      values: {
        "nova.enabled": false,
      },
    },
    Sections: [],
    DiscoveryStream: {
      config: { enabled: true },
      spocs: {},
    },
    dispatch: () => {},
    document: DOCUMENT_STUB,
  };

  it("does not render ASRouterNewTabMessage when there is no message", () => {
    const { container } = renderBaseContentWithFeed(NON_NOVA_BASE_PROPS);
    expect(
      container.querySelectorAll(".asrouter-newtab-message-wrapper")
    ).toHaveLength(0);
  });

  it("does not render ASRouterNewTabMessage when isVisible is false", () => {
    const { container } = renderBaseContentWithFeed({
      ...NON_NOVA_BASE_PROPS,
      Messages: makeASRouterMessages({ isVisible: false }),
    });
    expect(
      container.querySelectorAll(".asrouter-newtab-message-wrapper")
    ).toHaveLength(0);
  });

  it("renders ASRouterNewTabMessage before the content area for ABOVE_TOPSITES", () => {
    const { container } = renderBaseContentWithFeed({
      ...NON_NOVA_BASE_PROPS,
      Messages: makeASRouterMessages({ position: "ABOVE_TOPSITES" }),
    });
    const { messageIdx, contentFeedIdx } = findASRouterMessagePositionIndices(
      container,
      ".body-wrapper"
    );

    expect(messageIdx).toBeGreaterThan(-1);
    expect(contentFeedIdx).toBeGreaterThan(-1);
    expect(messageIdx).toBeLessThan(contentFeedIdx);
  });
});

describe("WithDsAdmin", () => {
  // WithDsAdmin passes its props through to BaseContent. Enzyme shallow never
  // mounted that child; when the "no hash" case renders BaseContent for real we
  // must supply the props BaseContent needs (and a store).
  const BASE_CONTENT_PROPS = {
    store: { getState: () => {} },
    App: { initialized: true },
    Prefs: { values: {} },
    Sections: [],
    DiscoveryStream: { config: { enabled: false }, spocs: {} },
    dispatch: () => {},
    Wallpapers: MOUNT_WALLPAPERS,
    document: {
      visibilityState: "visible",
      addEventListener: jest.fn(),
      removeEventListener: jest.fn(),
    },
  };

  describe("rendering inner content", () => {
    it("should not set devtoolsCollapsed state for about:newtab (no hash)", () => {
      globalThis.location.hash = "";
      const { container } = render(
        <Provider store={makeStore()}>
          <WithDsAdmin {...BASE_CONTENT_PROPS} hash="" />
        </Provider>
      );
      // devtoolsCollapsed === true renders the toggle in its "expanded" state.
      expect(
        container.querySelector(".discoverystream-admin-toggle.expanded")
      ).toBeInTheDocument();
      // find(BaseContent).length === 1 -> BaseContent's classic output is present.
      expect(container.querySelectorAll(".outer-wrapper")).toHaveLength(1);
    });

    it("should set devtoolsCollapsed state for about:newtab#devtools", () => {
      globalThis.location.hash = "#devtools";
      const { container } = render(
        <Provider store={makeStore()}>
          <WithDsAdmin hash="#devtools" />
        </Provider>
      );
      // devtoolsCollapsed === false renders the toggle in its "collapsed" state.
      expect(
        container.querySelector(".discoverystream-admin-toggle.collapsed")
      ).toBeInTheDocument();
      expect(container.querySelectorAll(".outer-wrapper")).toHaveLength(0);
    });

    it("should set devtoolsCollapsed state for about:newtab#devtools subroutes", () => {
      globalThis.location.hash = "#devtools-foo";
      const { container } = render(
        <Provider store={makeStore()}>
          <WithDsAdmin hash="#devtools-foo" />
        </Provider>
      );
      expect(
        container.querySelector(".discoverystream-admin-toggle.collapsed")
      ).toBeInTheDocument();
      expect(container.querySelectorAll(".outer-wrapper")).toHaveLength(0);
    });
  });

  describe("SPOC Placeholder Duration Tracking", () => {
    let instance;
    let dispatch;
    let baseProps;
    let ref;
    let reduxStore;
    let rerender;

    const expiredSpocs = () => ({
      config: { enabled: true },
      spocs: {
        onDemand: { enabled: true, loaded: false },
        lastUpdated: Date.now() - 120000, // Expired (120s ago)
        cacheUpdateTime: 60000, // Cache expires after 60s
      },
    });

    const rerenderWith = discoveryStream =>
      rerender(
        <Provider store={reduxStore}>
          <BaseContent
            {...baseProps}
            dispatch={dispatch}
            DiscoveryStream={discoveryStream}
            ref={ref}
          />
        </Provider>
      );

    beforeEach(() => {
      // Setup: Create a component with expired spocs (showing placeholders)
      // - fake timers let us control time for duration testing (performance is
      //   left real so componentDidMount's getEntriesByType still works)
      // - lastUpdated is 120000ms (2 mins) ago, exceeding cacheUpdateTime of 60000ms (1 min)
      // - In this setup, spocs are expired and placeholders should be visible
      jest.useFakeTimers({ doNotFake: ["performance"] });
      dispatch = jest.fn();
      baseProps = {
        store: { getState: () => {} },
        App: { initialized: true },
        Prefs: { values: {} },
        Sections: [],
        Weather: {},
        Wallpapers: MOUNT_WALLPAPERS,
        document: {
          visibilityState: "visible",
          addEventListener: jest.fn(),
          removeEventListener: jest.fn(),
        },
      };
      const props = {
        ...baseProps,
        dispatch,
        DiscoveryStream: expiredSpocs(),
      };
      ref = React.createRef();
      reduxStore = makeStore();
      ({ rerender } = render(
        <Provider store={reduxStore}>
          <BaseContent {...props} ref={ref} />
        </Provider>
      ));
      instance = ref.current;
      act(() => {
        instance.setState({ visible: true });
      });
    });

    afterEach(() => {
      jest.useRealTimers();
    });

    it("should start tracking when placeholders become visible", () => {
      const prevProps = {
        ...baseProps,
        DiscoveryStream: {
          config: { enabled: true },
          spocs: {
            onDemand: { enabled: true, loaded: false },
            lastUpdated: Date.now() - 30000,
            cacheUpdateTime: 60000,
          },
        },
      };

      act(() => {
        jest.advanceTimersByTime(1000);
      });
      instance.trackSpocPlaceholderDuration(prevProps);

      expect(instance.spocPlaceholderStartTime).not.toBeNull();
    });

    it("should record duration when placeholders are replaced", () => {
      // Create a fresh wrapper with expired spocs
      const freshDispatch = jest.fn();
      const freshRef = React.createRef();
      const expiredTime = Date.now() - 120000;
      const { rerender: freshRerender } = render(
        <Provider store={reduxStore}>
          <BaseContent
            {...baseProps}
            dispatch={freshDispatch}
            DiscoveryStream={{
              config: { enabled: true },
              spocs: {
                onDemand: { enabled: true, loaded: false },
                lastUpdated: expiredTime,
                cacheUpdateTime: 60000,
              },
            }}
            ref={freshRef}
          />
        </Provider>
      );
      const freshInstance = freshRef.current;
      act(() => {
        freshInstance.setState({ visible: true });
      });

      // Advance clock a bit first so startTime is not 0 (which is falsy)
      jest.advanceTimersByTime(100);

      // Set start time and advance clock
      const startTime = Date.now();
      freshInstance.spocPlaceholderStartTime = startTime;
      jest.advanceTimersByTime(150);

      // Update to fresh spocs - this triggers componentDidUpdate
      // which automatically calls trackSpocPlaceholderDuration
      freshRerender(
        <Provider store={reduxStore}>
          <BaseContent
            {...baseProps}
            dispatch={freshDispatch}
            DiscoveryStream={{
              config: { enabled: true },
              spocs: {
                onDemand: { enabled: true, loaded: false },
                lastUpdated: Date.now(),
                cacheUpdateTime: 60000,
              },
            }}
            ref={freshRef}
          />
        </Provider>
      );

      // componentDidUpdate should have dispatched the placeholder duration action
      const placeholderCall = freshDispatch.mock.calls.find(
        call => call[0].type === "DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION"
      );

      expect(placeholderCall).toBeTruthy();
      const [action] = placeholderCall;
      expect(action.data.duration).toBe(150);
      expect(action.meta).toEqual({
        from: "ActivityStream:Content",
        to: "ActivityStream:Main",
        skipLocal: true,
      });

      expect(freshInstance.spocPlaceholderStartTime).toBeNull();
    });

    it("should start tracking on onVisible if placeholders already expired", () => {
      rerenderWith({
        config: { enabled: true },
        spocs: {
          onDemand: { enabled: true, loaded: false },
          lastUpdated: Date.now() - 120000,
          cacheUpdateTime: 60000,
        },
      });

      act(() => {
        instance.setState({ visible: false });
      });
      instance.spocPlaceholderStartTime = null;

      act(() => {
        instance.onVisible();
      });

      expect(instance.spocPlaceholderStartTime).not.toBeNull();
    });

    it("should not start tracking if tab is not visible", () => {
      act(() => {
        instance.setState({ visible: false });
      });
      instance.spocPlaceholderStartTime = null;

      const prevProps = {
        ...baseProps,
        DiscoveryStream: {
          config: { enabled: true },
          spocs: {
            onDemand: { enabled: true, loaded: false },
            lastUpdated: Date.now() - 30000,
            cacheUpdateTime: 60000,
          },
        },
      };

      instance.trackSpocPlaceholderDuration(prevProps);

      expect(instance.spocPlaceholderStartTime).toBeNull();
    });

    it("should not start tracking if onDemand is disabled", () => {
      // Reset instance to have onDemand disabled from the start
      const props = {
        ...baseProps,
        dispatch,
        DiscoveryStream: {
          config: { enabled: true },
          spocs: {
            onDemand: { enabled: false, loaded: false },
            lastUpdated: Date.now() - 120000,
            cacheUpdateTime: 60000,
          },
        },
      };
      const disabledRef = React.createRef();
      render(
        <Provider store={reduxStore}>
          <BaseContent {...props} ref={disabledRef} />
        </Provider>
      );
      instance = disabledRef.current;
      act(() => {
        instance.setState({ visible: true });
      });
      instance.spocPlaceholderStartTime = null;

      const prevProps = {
        ...baseProps,
        DiscoveryStream: {
          config: { enabled: true },
          spocs: {
            onDemand: { enabled: false, loaded: false },
            lastUpdated: Date.now() - 120000,
            cacheUpdateTime: 60000,
          },
        },
      };

      instance.trackSpocPlaceholderDuration(prevProps);

      expect(instance.spocPlaceholderStartTime).toBeNull();
    });
  });
});

describe("<BaseContent> onWindowScroll", () => {
  const DEFAULT_PROPS = {
    store: { getState: () => {} },
    App: { initialized: true },
    Prefs: { values: {} },
    Sections: [],
    DiscoveryStream: { config: { enabled: false }, spocs: {} },
    dispatch: () => {},
    document: {
      visibilityState: "visible",
      addEventListener: jest.fn(),
      removeEventListener: jest.fn(),
    },
  };

  function setScrollY(value) {
    Object.defineProperty(global, "scrollY", { value, configurable: true });
  }

  function mountScroll(props) {
    const ref = React.createRef();
    render(
      <Provider store={makeStore()}>
        <BaseContent Wallpapers={MOUNT_WALLPAPERS} {...props} ref={ref} />
      </Provider>
    );
    return ref.current;
  }

  // onWindowScroll is throttled, so let the throttle lapse after each scroll.
  function scrollWindowTo(instance, value) {
    setScrollY(value);
    instance.onWindowScroll();
    jest.advanceTimersByTime(10);
  }

  function scrollAction(threshold) {
    return ac.OnlyToMain({ type: at.NEW_TAB_SCROLL, data: { threshold } });
  }

  beforeEach(() => {
    // performance is left real so componentDidMount's getEntriesByType works
    jest.useFakeTimers({ doNotFake: ["performance"] });
    setScrollY(0);
  });

  afterEach(() => {
    jest.useRealTimers();
    setScrollY(0);
  });

  it("should dispatch NEW_TAB_SCROLL when scrollY exceeds the first threshold", () => {
    const dispatch = jest.fn();
    const instance = mountScroll({ ...DEFAULT_PROPS, dispatch });
    scrollWindowTo(instance, 60);
    expect(dispatch).toHaveBeenCalledTimes(1);
    expect(dispatch).toHaveBeenCalledWith(scrollAction(50));
  });

  it("should not dispatch NEW_TAB_SCROLL when scrollY is at or below the first threshold", () => {
    const dispatch = jest.fn();
    const instance = mountScroll({ ...DEFAULT_PROPS, dispatch });
    scrollWindowTo(instance, 50);
    expect(dispatch).not.toHaveBeenCalled();
  });

  it("should dispatch every threshold passed by a single scroll", () => {
    const dispatch = jest.fn();
    const instance = mountScroll({ ...DEFAULT_PROPS, dispatch });
    scrollWindowTo(instance, 300);
    expect(dispatch).toHaveBeenCalledTimes(3);
    expect(dispatch).toHaveBeenCalledWith(scrollAction(50));
    expect(dispatch).toHaveBeenCalledWith(scrollAction(100));
    expect(dispatch).toHaveBeenCalledWith(scrollAction(250));
  });

  it("should dispatch each threshold as the user scrolls further", () => {
    const dispatch = jest.fn();
    const instance = mountScroll({ ...DEFAULT_PROPS, dispatch });
    scrollWindowTo(instance, 60);
    expect(dispatch).toHaveBeenCalledTimes(1);
    expect(dispatch).toHaveBeenCalledWith(scrollAction(50));

    scrollWindowTo(instance, 150);
    expect(dispatch).toHaveBeenCalledTimes(2);
    expect(dispatch).toHaveBeenCalledWith(scrollAction(100));

    scrollWindowTo(instance, 300);
    expect(dispatch).toHaveBeenCalledTimes(3);
    expect(dispatch).toHaveBeenCalledWith(scrollAction(250));
  });

  it("should not dispatch NEW_TAB_SCROLL again for an already reported threshold", () => {
    const dispatch = jest.fn();
    const instance = mountScroll({ ...DEFAULT_PROPS, dispatch });
    scrollWindowTo(instance, 300);
    dispatch.mockClear();

    scrollWindowTo(instance, 400);
    expect(dispatch).not.toHaveBeenCalled();
  });
});

const weatherSuggestion = {
  current_conditions: {
    icon_id: 3,
    summary: "Partly Cloudy",
    temperature: { c: 20, f: 68 },
  },
  forecast: {
    high: { c: 25, f: 77 },
    low: { c: 15, f: 59 },
    url: "https://example.com",
  },
};

describe("<Base> Nova startup layout stability", () => {
  it("keeps the centered-logo layout stable while the small weather widget initializes", () => {
    const store = createStore(combineReducers(reducers), {
      ...INITIAL_STATE,
      App: {
        ...INITIAL_STATE.App,
        initialized: true,
      },
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
          showWeather: true,
          "widgets.enabled": true,
          "widgets.system.enabled": true,
          "widgets.system.weather.enabled": true,
          "widgets.weather.enabled": true,
          "widgets.weather.size": "small",
        },
      },
      Weather: {
        ...INITIAL_STATE.Weather,
        initialized: false,
        locationData: { city: "Testville" },
      },
    });

    const { container } = render(
      <Provider store={store}>
        <ConnectedBase />
      </Provider>
    );

    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).toBeInTheDocument();
    expect(
      container.querySelector(
        ".sidebar-inline-start .logo-and-wordmark-wrapper"
      )
    ).not.toBeInTheDocument();
    expect(
      container.querySelector(".sidebar-inline-end .weather-widget")
    ).not.toBeInTheDocument();

    act(() => {
      store.dispatch({
        type: at.WEATHER_UPDATE,
        data: {
          suggestions: [weatherSuggestion],
          hourlyForecasts: [],
          lastUpdated: Date.now(),
          locationData: { city: "Testville" },
        },
      });
    });

    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).toBeInTheDocument();
    expect(
      container.querySelector(
        ".sidebar-inline-start .logo-and-wordmark-wrapper"
      )
    ).not.toBeInTheDocument();
    expect(
      container.querySelector(".sidebar-inline-end .weather-widget")
    ).toBeInTheDocument();
  });
});

function renderNova(overrides = {}, stateOverrides = {}) {
  const store = createStore(combineReducers(reducers), {
    ...INITIAL_STATE,
    ...stateOverrides,
    App: { ...INITIAL_STATE.App, initialized: true },
    Prefs: {
      ...INITIAL_STATE.Prefs,
      values: {
        ...INITIAL_STATE.Prefs.values,
        "nova.enabled": true,
        region: "US",
        showSearch: false,
        "feeds.topsites": false,
        "feeds.section.topstories": false,
        "feeds.system.topstories": false,
        "feeds.section.highlights": false,
        "widgets.enabled": false,
        "widgets.system.enabled": false,
        "widgets.system.weather.enabled": false,
        "widgets.weather.enabled": false,
        "widgets.system.lists.enabled": false,
        "widgets.lists.enabled": false,
        "widgets.system.focusTimer.enabled": false,
        "widgets.focusTimer.enabled": false,
        "widgets.system.clocks.enabled": false,
        "widgets.clocks.enabled": false,
        ...overrides,
      },
    },
  });
  return render(
    <Provider store={store}>
      <ConnectedBase />
    </Provider>
  );
}

describe("<Base> Nova hides Logo when no sections are enabled", () => {
  it("does not render the Logo when every section is disabled", () => {
    const { container } = renderNova();
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).not.toBeInTheDocument();
  });

  it("renders the Logo when only search is enabled", () => {
    const { container } = renderNova({ showSearch: true });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });

  it("renders the Logo when only topsites is enabled", () => {
    const { container } = renderNova({ "feeds.topsites": true });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });

  it("renders the Logo when only Highlights (Recent Activity) is enabled", () => {
    const { container } = renderNova({ "feeds.section.highlights": true });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });

  it("renders the Logo when only Pocket is enabled", () => {
    const { container } = renderNova({
      "feeds.section.topstories": true,
      "feeds.system.topstories": true,
    });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });

  it("renders the Logo when a content widget (lists) is enabled", () => {
    const { container } = renderNova({
      "widgets.enabled": true,
      "widgets.system.enabled": true,
      "widgets.system.lists.enabled": true,
      "widgets.lists.enabled": true,
    });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });

  it("renders the Logo when a widget is enabled via trainhopConfig (system pref off)", () => {
    const { container } = renderNova({
      "widgets.enabled": true,
      "widgets.system.enabled": true,
      "widgets.system.lists.enabled": false,
      "widgets.lists.enabled": true,
      trainhopConfig: { widgets: { listsEnabled: true } },
    });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });

  it("hides the Logo when per-widget prefs are on but widgets.enabled is off", () => {
    const { container } = renderNova({
      "widgets.enabled": false,
      "widgets.system.enabled": true,
      "widgets.system.lists.enabled": true,
      "widgets.lists.enabled": true,
      "widgets.system.clocks.enabled": true,
      "widgets.clocks.enabled": true,
      "widgets.system.focusTimer.enabled": true,
      "widgets.focusTimer.enabled": true,
    });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).not.toBeInTheDocument();
  });

  it("centers the Logo in .content when widgets.enabled is off and only search is on", () => {
    const { container } = renderNova({
      showSearch: true,
      "widgets.enabled": false,
      "widgets.system.enabled": true,
      "widgets.system.lists.enabled": true,
      "widgets.lists.enabled": true,
      "widgets.system.clocks.enabled": true,
      "widgets.clocks.enabled": true,
      "widgets.system.focusTimer.enabled": true,
      "widgets.focusTimer.enabled": true,
    });
    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).toBeInTheDocument();
    expect(
      container.querySelector(".content .logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
    expect(
      container.querySelector(
        ".sidebar-inline-start .logo-and-wordmark-wrapper"
      )
    ).not.toBeInTheDocument();
  });

  it("renders the Logo when only the sidebar weather widget is enabled", () => {
    const { container } = renderNova({
      "widgets.enabled": true,
      "widgets.system.enabled": true,
      "widgets.system.weather.enabled": true,
      "widgets.weather.enabled": true,
      "widgets.weather.size": "small",
    });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });
});

describe("<Base> Nova logo placement with many topSitesRows", () => {
  it("anchors the Logo to the sidebar when topSitesRows > 2 (3 rows)", () => {
    const { container } = renderNova({
      "feeds.topsites": true,
      topSitesRows: 3,
    });
    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).not.toBeInTheDocument();
    expect(
      container.querySelector(
        ".sidebar-inline-start .logo-and-wordmark-wrapper"
      )
    ).toBeInTheDocument();
    expect(
      container.querySelector(".content .logo-and-wordmark-wrapper")
    ).not.toBeInTheDocument();
  });

  it("anchors the Logo to the sidebar when topSitesRows > 2 (4 rows)", () => {
    const { container } = renderNova({
      "feeds.topsites": true,
      topSitesRows: 4,
    });
    expect(
      container.querySelector(
        ".sidebar-inline-start .logo-and-wordmark-wrapper"
      )
    ).toBeInTheDocument();
  });

  it("centers the Logo when topSitesRows is 2", () => {
    const { container } = renderNova({
      "feeds.topsites": true,
      topSitesRows: 2,
    });
    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).toBeInTheDocument();
    expect(
      container.querySelector(".content .logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });

  it("centers the Logo when topSitesRows > 2 but topsites are disabled", () => {
    const { container } = renderNova({
      "feeds.topsites": false,
      showSearch: true,
      topSitesRows: 4,
    });
    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).toBeInTheDocument();
    expect(
      container.querySelector(".content .logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });
});

describe("<Base> Nova logo placement with the search bar in variant B", () => {
  const searchComponent = attributes => ({
    ExternalComponents: {
      ...INITIAL_STATE.ExternalComponents,
      components: [{ type: "SEARCH", attributes }],
    },
  });

  // The component has no module to load here, which the wrapper reports.
  let consoleErrorSpy;
  beforeEach(() => {
    consoleErrorSpy = jest.spyOn(console, "error").mockImplementation(() => {});
  });
  afterEach(() => {
    consoleErrorSpy.mockRestore();
  });

  it("anchors the Logo to the sidebar when the search bar is in variant B", () => {
    const { container, unmount } = renderNova(
      { showSearch: true },
      searchComponent({ "variant-b": "" })
    );
    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).not.toBeInTheDocument();
    expect(
      container.querySelector(".container.nova-enabled.search-has-own-row")
    ).toBeInTheDocument();
    expect(
      container.querySelector(".nova-outer-wrapper.search-has-own-row")
    ).toBeInTheDocument();
    expect(
      container.querySelector(
        ".sidebar-inline-start .logo-and-wordmark-wrapper"
      )
    ).toBeInTheDocument();
    unmount();
  });

  it.each([
    ["the page has nothing else", {}],
    ["shortcuts have two rows", { "feeds.topsites": true, topSitesRows: 2 }],
  ])("lowers the search when %s", (_label, prefs) => {
    const { container, unmount } = renderNova(
      { showSearch: true, ...prefs },
      searchComponent({ "variant-b": "" })
    );
    expect(
      container.querySelector(".container.nova-enabled.search-lowered")
    ).toBeInTheDocument();
    unmount();
  });

  it.each([
    ["shortcuts have three rows", { "feeds.topsites": true, topSitesRows: 3 }],
    [
      "stories are on",
      { "feeds.section.topstories": true, "feeds.system.topstories": true },
    ],
  ])("keeps the search at the top when %s", (_label, prefs) => {
    const { container, unmount } = renderNova(
      { showSearch: true, ...prefs },
      searchComponent({ "variant-b": "" })
    );
    expect(
      container.querySelector(".container.nova-enabled.search-lowered")
    ).not.toBeInTheDocument();
    unmount();
  });

  it("doesn't lower the search when the Logo is centered", () => {
    const { container, unmount } = renderNova(
      { showSearch: true },
      searchComponent({ "variant-a": "" })
    );
    expect(
      container.querySelector(".container.nova-enabled.search-lowered")
    ).not.toBeInTheDocument();
    unmount();
  });

  it("centers the Logo when the search bar is in another variant", () => {
    const { container, unmount } = renderNova(
      { showSearch: true },
      searchComponent({ "variant-a": "" })
    );
    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).toBeInTheDocument();
    expect(
      container.querySelector(".container.nova-enabled.search-has-own-row")
    ).not.toBeInTheDocument();
    unmount();
  });

  it("centers the Logo when search is hidden, whatever its variant", () => {
    const { container, unmount } = renderNova(
      { showSearch: false, "feeds.topsites": true },
      searchComponent({ "variant-b": "" })
    );
    expect(
      container.querySelector(".container.nova-enabled.logo-in-content")
    ).toBeInTheDocument();
    unmount();
  });
});

describe("<Base> Nova layout with a multistage message", () => {
  const multistageMessage = {
    ...INITIAL_STATE.Messages,
    isVisible: true,
    messageData: { content: { messageType: "ASRouterMultistageMessage" } },
  };

  // The message and search components have no module to load here,
  // which the wrapper reports.
  let consoleErrorSpy;
  beforeEach(() => {
    consoleErrorSpy = jest.spyOn(console, "error").mockImplementation(() => {});
  });
  afterEach(() => {
    consoleErrorSpy.mockRestore();
  });

  it.each([
    ["the default search", []],
    [
      "the search bar in variant B",
      [{ type: "SEARCH", attributes: { "variant-b": "" } }],
    ],
  ])(
    "keeps the Logo and search at the top with %s when stories and widgets are off",
    (_label, components) => {
      const { container, unmount } = renderNova(
        { showSearch: true },
        {
          Messages: multistageMessage,
          ExternalComponents: {
            ...INITIAL_STATE.ExternalComponents,
            components,
          },
        }
      );

      expect(
        container.querySelector(".container.nova-enabled.logo-in-content")
      ).not.toBeInTheDocument();
      expect(
        container.querySelector(".container.nova-enabled.search-lowered")
      ).not.toBeInTheDocument();
      unmount();
    }
  );
});

describe("<Base> Nova hideLogo pref", () => {
  it("renders the Logo by default (hideLogo unset, topsites enabled)", () => {
    const { container } = renderNova({ "feeds.topsites": true });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).toBeInTheDocument();
  });

  it("hides the Logo when hideLogo is true (sidebar layout)", () => {
    const { container } = renderNova({
      "feeds.topsites": true,
      hideLogo: true,
    });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).not.toBeInTheDocument();
  });

  it("hides the Logo when hideLogo is true (centered layout)", () => {
    const { container } = renderNova({ showSearch: true, hideLogo: true });
    expect(
      container.querySelector(".logo-and-wordmark-wrapper")
    ).not.toBeInTheDocument();
  });
});

describe("<BaseContent> weather opt-in dialog trigger", () => {
  function makeInstance(currentPrefs, dispatch = jest.fn()) {
    const inst = Object.create(BaseContent.prototype);
    inst.props = {
      Prefs: { values: currentPrefs },
      dispatch,
      App: { isForStartupCache: {} },
      DiscoveryStream: { spocs: {} },
    };
    inst.state = { visible: false };
    inst.applyBodyClasses = jest.fn();
    inst.spocsOnDemandUpdated = jest.fn();
    inst.trackSpocPlaceholderDuration = jest.fn();
    Object.defineProperty(inst, "isSpocsOnDemandExpired", { get: () => false });
    return { inst, dispatch };
  }

  function makePrevProps(prefs) {
    return {
      Prefs: { values: prefs },
      DiscoveryStream: { spocs: {} },
      App: { isForStartupCache: {} },
    };
  }

  it("triggers weather.optInDisplayed in classic mode when showWeather transitions to true", () => {
    const { inst, dispatch } = makeInstance({
      "nova.enabled": false,
      showWeather: true,
    });

    inst.componentDidUpdate(
      makePrevProps({ "nova.enabled": false, showWeather: false })
    );

    expect(dispatch).toHaveBeenCalledWith(
      ac.SetPref("weather.optInDisplayed", true)
    );
  });

  it("does not trigger weather.optInDisplayed in classic mode when showWeather was already true", () => {
    const { inst, dispatch } = makeInstance({
      "nova.enabled": false,
      showWeather: true,
    });

    inst.componentDidUpdate(
      makePrevProps({ "nova.enabled": false, showWeather: true })
    );

    expect(dispatch).not.toHaveBeenCalledWith(
      ac.SetPref("weather.optInDisplayed", true)
    );
  });

  it("triggers weather.optInDisplayed in Nova mode when widgets.weather.enabled transitions to true", () => {
    const { inst, dispatch } = makeInstance({
      "nova.enabled": true,
      "widgets.weather.enabled": true,
    });

    inst.componentDidUpdate(
      makePrevProps({ "nova.enabled": true, "widgets.weather.enabled": false })
    );

    expect(dispatch).toHaveBeenCalledWith(
      ac.SetPref("weather.optInDisplayed", true)
    );
  });

  it("does not trigger weather.optInDisplayed in Nova mode when only showWeather transitions to true", () => {
    const { inst, dispatch } = makeInstance({
      "nova.enabled": true,
      "widgets.weather.enabled": true,
      showWeather: true,
    });

    inst.componentDidUpdate(
      makePrevProps({
        "nova.enabled": true,
        "widgets.weather.enabled": true,
        showWeather: false,
      })
    );

    expect(dispatch).not.toHaveBeenCalledWith(
      ac.SetPref("weather.optInDisplayed", true)
    );
  });
});

describe("<BaseContent> wallpaper transitions (Bug 2057217)", () => {
  const makeInstance = ({
    wallpaper,
    uploadedWallpaper = null,
    wallpaperList = [],
  }) => {
    const inst = Object.create(BaseContent.prototype);
    inst.props = {
      Prefs: {
        values: {
          "newtabWallpapers.enabled": true,
          "newtabWallpapers.user.enabled": true,
          "newtabWallpapers.wallpaper": wallpaper,
          "newtabWallpapers.initialWallpaper": "",
          "newtabWallpapers.customWallpaper.theme": "",
          "nova.enabled": true,
        },
      },
      Wallpapers: { wallpaperList, uploadedWallpaper },
    };
    inst.prefersDarkQuery = null;
    return inst;
  };

  const makeDeferred = () => {
    let resolve;
    const promise = new Promise(resolvePromise => {
      resolve = resolvePromise;
    });
    return { promise, resolve };
  };

  const originalImage = global.Image;
  let setPropertySpy;
  beforeEach(() => {
    setPropertySpy = jest.spyOn(document.body.style, "setProperty");
  });
  afterEach(() => {
    global.Image = originalImage;
    document.body.style.removeProperty("--newtab-wallpaper");
    document.body.style.removeProperty("--newtab-wallpaper-color");
    document.body.style.removeProperty("--newtab-wallpaper-backgroundPosition");
    document.documentElement.classList.remove(
      "lightWallpaper",
      "darkWallpaper"
    );
    jest.restoreAllMocks();
  });

  it("does not blank the background when custom is selected but its uploaded URL has not arrived yet", async () => {
    const inst = makeInstance({ wallpaper: "custom", uploadedWallpaper: null });

    await inst.updateWallpaper();

    expect(setPropertySpy).not.toHaveBeenCalledWith(
      "--newtab-wallpaper",
      expect.anything()
    );
  });

  // Bug 2072941: a replacement upload clears the applied URL while the file is
  // written, so this is the ordinary case, not an edge one.
  it("keeps a painted wallpaper up when the applied URL is cleared", async () => {
    const inst = makeInstance({ wallpaper: "custom", uploadedWallpaper: null });
    document.body.style.setProperty(
      "--newtab-wallpaper",
      "url(previous-wallpaper.jpg)"
    );
    setPropertySpy.mockClear();

    await inst.updateWallpaper();

    expect(document.body.style.getPropertyValue("--newtab-wallpaper")).toBe(
      "url(previous-wallpaper.jpg)"
    );
    expect(setPropertySpy).not.toHaveBeenCalled();
  });

  it("crops a saved wallpaper the way its position pref says", async () => {
    const filename =
      "v1-builtin-dark-topright-1-550e8400-e29b-41d4-a716-446655440000";
    const inst = makeInstance({
      wallpaper: "custom",
      uploadedWallpaper: `moz-newtab-wallpaper://${filename}`,
    });
    inst.props.Prefs.values["newtabWallpapers.customWallpaper.uuid"] = filename;
    inst.props.Prefs.values["newtabWallpapers.customWallpaper.position"] =
      "top right";

    await inst.updateWallpaper();

    expect(setPropertySpy).toHaveBeenCalledWith(
      "--newtab-wallpaper-backgroundPosition",
      "top right"
    );
  });

  it("centers an uploaded wallpaper, which has no crop of its own", async () => {
    const inst = makeInstance({
      wallpaper: "custom",
      uploadedWallpaper: "custom-wallpaper.jpg",
    });

    await inst.updateWallpaper();

    expect(setPropertySpy).toHaveBeenCalledWith(
      "--newtab-wallpaper-backgroundPosition",
      "center"
    );
  });

  it("applies an initial custom wallpaper without waiting for decode", async () => {
    const inst = makeInstance({
      wallpaper: "custom",
      uploadedWallpaper: "custom-wallpaper.jpg",
    });
    const decodeSpy = jest.spyOn(inst, "decodeWallpaper");

    await inst.updateWallpaper();

    expect(decodeSpy).not.toHaveBeenCalled();
    expect(setPropertySpy).toHaveBeenCalledWith(
      "--newtab-wallpaper",
      "url(custom-wallpaper.jpg)"
    );
  });

  it("keeps the current wallpaper painted until its replacement decodes", async () => {
    const inst = makeInstance({
      wallpaper: "custom",
      uploadedWallpaper: "custom-wallpaper.jpg",
    });
    const pendingDecode = makeDeferred();
    jest.spyOn(inst, "decodeWallpaper").mockReturnValue(pendingDecode.promise);
    document.body.style.setProperty(
      "--newtab-wallpaper",
      "url(previous-wallpaper.jpg)"
    );
    setPropertySpy.mockClear();

    const updatePromise = inst.updateWallpaper();

    expect(document.body.style.getPropertyValue("--newtab-wallpaper")).toBe(
      "url(previous-wallpaper.jpg)"
    );
    expect(setPropertySpy).not.toHaveBeenCalledWith(
      "--newtab-wallpaper",
      "url(custom-wallpaper.jpg)"
    );

    pendingDecode.resolve(true);
    await updatePromise;

    expect(setPropertySpy).toHaveBeenCalledWith(
      "--newtab-wallpaper",
      "url(custom-wallpaper.jpg)"
    );
  });

  it("keeps the current wallpaper when its replacement fails to decode", async () => {
    const inst = makeInstance({
      wallpaper: "custom",
      uploadedWallpaper: "custom-wallpaper.jpg",
    });
    const decode = jest.fn().mockRejectedValue(new Error("decode failed"));
    global.Image = jest.fn(() => ({ decode }));
    document.body.style.setProperty(
      "--newtab-wallpaper",
      "url(previous-wallpaper.jpg)"
    );
    setPropertySpy.mockClear();

    await inst.updateWallpaper();

    expect(decode).toHaveBeenCalled();
    expect(document.body.style.getPropertyValue("--newtab-wallpaper")).toBe(
      "url(previous-wallpaper.jpg)"
    );
    expect(setPropertySpy).not.toHaveBeenCalledWith(
      "--newtab-wallpaper",
      "url(custom-wallpaper.jpg)"
    );
  });

  it("does not apply a decoded custom wallpaper after a newer built-in selection", async () => {
    const inst = makeInstance({
      wallpaper: "custom",
      uploadedWallpaper: "custom-wallpaper.jpg",
    });
    const pendingDecode = makeDeferred();
    global.Image = jest.fn(() => ({
      decode: jest.fn(() => pendingDecode.promise),
    }));
    document.body.style.setProperty(
      "--newtab-wallpaper",
      "url(previous-wallpaper.jpg)"
    );
    setPropertySpy.mockClear();

    const customUpdate = inst.updateWallpaper();
    inst.props = {
      ...inst.props,
      Prefs: {
        values: {
          ...inst.props.Prefs.values,
          "newtabWallpapers.wallpaper": "built-in",
        },
      },
      Wallpapers: {
        uploadedWallpaper: "custom-wallpaper.jpg",
        wallpaperList: [
          {
            title: "built-in",
            wallpaperUrl: "built-in-wallpaper.jpg",
          },
        ],
      },
    };

    await inst.updateWallpaper();
    pendingDecode.resolve();
    await customUpdate;

    expect(document.body.style.getPropertyValue("--newtab-wallpaper")).toBe(
      "url(built-in-wallpaper.jpg)"
    );
    expect(setPropertySpy).not.toHaveBeenCalledWith(
      "--newtab-wallpaper",
      "url(custom-wallpaper.jpg)"
    );
  });
});

// Constructs a bare instance off the prototype so instance methods run against
// controlled props/state while we assert on the dispatch spy and DOM side
// effects (never on internal state).
function makeBareInstance(overrides = {}) {
  const bareInstance = Object.create(BaseContent.prototype);
  bareInstance.props = overrides.props || {};
  bareInstance.state = overrides.state || {};
  return bareInstance;
}

describe("<BaseContent> customize menu dispatch", () => {
  it("does not dispatch close actions when the menu is not visible", () => {
    const dispatch = jest.fn();
    const instance = makeBareInstance({
      props: { dispatch, App: { customizeMenuVisible: false } },
    });

    instance.closeCustomizationMenu();

    expect(dispatch).not.toHaveBeenCalled();
  });
});

describe("<BaseContent> Highlights wrapper", () => {
  const DOCUMENT_STUB = {
    visibilityState: "visible",
    addEventListener: jest.fn(),
    removeEventListener: jest.fn(),
  };

  const props = {
    store: { getState: () => {} },
    App: { initialized: true },
    Prefs: {
      values: {
        "nova.enabled": true,
        "feeds.topsites": true,
        "feeds.section.topstories": true,
        "feeds.system.topstories": true,
        "feeds.section.highlights": true,
      },
    },
    Sections: [],
    DiscoveryStream: {
      config: { enabled: true },
      spocs: {},
      feeds: { loaded: true },
      showTopicSelection: false,
    },
    dispatch: () => {},
    document: DOCUMENT_STUB,
  };

  const renderWithSections = sections =>
    renderBaseContentWithFeed(props, {}, sections).container;

  // The point of the wrapper: side-by-side frames it separately from the feed.
  it("renders Highlights in its own wrapper outside the content column", () => {
    const container = renderWithSections([
      TOPSTORIES_SECTION,
      HIGHLIGHTS_SECTION,
    ]);

    const highlightsColumn = container.querySelector(
      ".layout-highlights-column"
    );
    expect(highlightsColumn).toBeInTheDocument();
    expect(
      highlightsColumn.querySelector(".ds-highlights")
    ).toBeInTheDocument();
    expect(
      container.querySelector(
        ".layout-content-column .layout-highlights-column"
      )
    ).toBeNull();
    expect(
      container.querySelector(".layout-content-column .ds-highlights")
    ).toBeNull();
  });

  // renderLayout always returns a div, so the gate is what keeps the panel off nothing.
  it("renders no wrapper when the highlights section is absent or disabled", () => {
    expect(
      renderWithSections([TOPSTORIES_SECTION]).querySelector(
        ".layout-highlights-column"
      )
    ).toBeNull();
    expect(
      renderWithSections([
        TOPSTORIES_SECTION,
        { ...HIGHLIGHTS_SECTION, enabled: false },
      ]).querySelector(".layout-highlights-column")
    ).toBeNull();
  });

  // The content feed keeps its own privacy link; only Highlights moved.
  it("keeps the content column ahead of the Highlights wrapper", () => {
    const container = renderWithSections([
      TOPSTORIES_SECTION,
      HIGHLIGHTS_SECTION,
    ]);
    const contentColumn = container.querySelector(".layout-content-column");
    const highlightsColumn = container.querySelector(
      ".layout-highlights-column"
    );

    expect(
      contentColumn.compareDocumentPosition(highlightsColumn) &
        Node.DOCUMENT_POSITION_FOLLOWING
    ).toBeTruthy();
  });
});

describe("<BaseContent> five column gate", () => {
  const DOCUMENT_STUB = {
    visibilityState: "visible",
    addEventListener: jest.fn(),
    removeEventListener: jest.fn(),
  };

  const sectionWithColumns = columnCounts => ({
    layout: {
      responsiveLayouts: columnCounts.map(columnCount => ({
        columnCount,
        tiles: [],
      })),
    },
  });

  const renderWithSections = sections =>
    renderBaseContentWithFeed({
      store: { getState: () => {} },
      App: { initialized: true },
      Prefs: {
        values: { "nova.enabled": true, "feeds.topsites": true },
      },
      Sections: [],
      DiscoveryStream: {
        config: { enabled: true },
        spocs: {},
        feeds: {
          loaded: true,
          data: { "https://example.com/feed": { data: { sections } } },
        },
        showTopicSelection: false,
      },
      dispatch: () => {},
      document: DOCUMENT_STUB,
    });

  it("widens the content band when every section defines 5 columns", () => {
    const { container } = renderWithSections([
      sectionWithColumns([1, 2, 3, 4, 5]),
      sectionWithColumns([1, 2, 3, 4, 5]),
    ]);

    expect(container.querySelector(".container")).toHaveClass("sections-5-col");
  });

  // Sections share one subgrid track count, so a partial layout set has to stay
  // at 4 columns; widening would leave the 4-column section with no tile for the
  // active breakpoint and nothing to render.
  it("stays at 4 columns when any section is missing a 5-column layout", () => {
    const { container } = renderWithSections([
      sectionWithColumns([1, 2, 3, 4, 5]),
      sectionWithColumns([1, 2, 3, 4]),
    ]);

    expect(container.querySelector(".container")).not.toHaveClass(
      "sections-5-col"
    );
  });

  it("stays at 4 columns before any sections have loaded", () => {
    const { container } = renderWithSections([]);

    expect(container.querySelector(".container")).not.toHaveClass(
      "sections-5-col"
    );
  });
});

describe("<Base> wallpaper attribution", () => {
  const ATTRIBUTED_WALLPAPER = {
    title: "photo-hills",
    wallpaperUrl: "https://example.com/hills.avif",
    attribution: {
      name: { string: "Ada Lovelace", url: "https://example.com/ada" },
      webpage: { string: "example.com", url: "https://example.com" },
    },
  };

  const PLAIN_WALLPAPER = { title: "solid-blue", solid_color: "#0000ff" };

  // Mounting runs updateWallpaper, which writes to the shared jsdom body.
  afterEach(() => {
    document.body.style.removeProperty("--newtab-wallpaper");
    document.body.style.removeProperty("--newtab-wallpaper-color");
    document.body.style.removeProperty("--newtab-wallpaper-backgroundPosition");
    document.documentElement.classList.remove(
      "lightWallpaper",
      "darkWallpaper"
    );
  });

  function renderWithWallpaper({ prefs = {}, wallpapers = {} } = {}) {
    return render(
      <Provider
        store={makeStore(
          {
            "nova.enabled": true,
            "newtabWallpapers.enabled": true,
            "newtabWallpapers.user.enabled": true,
            "newtabWallpapers.wallpaper": ATTRIBUTED_WALLPAPER.title,
            ...prefs,
          },
          {
            Wallpapers: {
              ...INITIAL_STATE.Wallpapers,
              wallpaperList: [ATTRIBUTED_WALLPAPER, PLAIN_WALLPAPER],
              ...wallpapers,
            },
          }
        )}
      >
        <ConnectedBase />
      </Provider>
    );
  }

  it("renders the attribution under Nova when the wallpaper credits a photographer", () => {
    const { container } = renderWithWallpaper();

    const attribution = container.querySelector(".wallpaper-attribution");
    expect(attribution).toBeInTheDocument();
    expect(attribution).toHaveAttribute(
      "data-l10n-id",
      "newtab-wallpaper-attribution"
    );
    expect(JSON.parse(attribution.getAttribute("data-l10n-args"))).toEqual({
      author_string: "Ada Lovelace",
      author_url: "https://example.com/ada",
      webpage_string: "example.com",
      webpage_url: "https://example.com",
    });
  });

  // Screen reader parity with classic, which also puts the attribution in the
  // main landmark. Asserting on lastElementChild rather than a descendant match
  // so a move into .content fails here: _Grid.scss gives every direct child of
  // .content inline-size containment, which collapses the credit to its padding.
  it("renders the attribution as the last child of the Nova main landmark", () => {
    const { container } = renderWithWallpaper();

    expect(
      container.querySelector(".content-main").lastElementChild
    ).toHaveClass("wallpaper-attribution");
  });

  it("does not render the attribution under Nova when the user turned wallpapers off", () => {
    const { container } = renderWithWallpaper({
      prefs: { "newtabWallpapers.user.enabled": false },
    });

    expect(
      container.querySelector(".wallpaper-attribution")
    ).not.toBeInTheDocument();
  });

  it("does not render the attribution under Nova when wallpapers are disabled system-wide", () => {
    const { container } = renderWithWallpaper({
      prefs: { "newtabWallpapers.enabled": false },
    });

    expect(
      container.querySelector(".wallpaper-attribution")
    ).not.toBeInTheDocument();
  });

  it("does not render the attribution for a wallpaper that ships no credit", () => {
    const { container } = renderWithWallpaper({
      prefs: { "newtabWallpapers.wallpaper": PLAIN_WALLPAPER.title },
    });

    expect(
      container.querySelector(".wallpaper-attribution")
    ).not.toBeInTheDocument();
  });

  it("does not render the attribution for an uploaded custom wallpaper", () => {
    const { container } = renderWithWallpaper({
      prefs: { "newtabWallpapers.wallpaper": "custom" },
      wallpapers: { uploadedWallpaper: "blob:custom-wallpaper" },
    });

    expect(
      container.querySelector(".wallpaper-attribution")
    ).not.toBeInTheDocument();
  });

  it("does not render the attribution for a colour from the solid colour picker", () => {
    const { container } = renderWithWallpaper({
      prefs: { "newtabWallpapers.wallpaper": "solid-color-picker-#0000ff" },
    });

    expect(
      container.querySelector(".wallpaper-attribution")
    ).not.toBeInTheDocument();
  });

  it("still renders the attribution in the classic layout", () => {
    const { container } = renderWithWallpaper({
      prefs: { "nova.enabled": false },
    });

    expect(
      container.querySelector("main.newtab-main .wallpaper-attribution")
    ).toBeInTheDocument();
  });

  // The user pref applies to Nova only; classic checks newtabWallpapers.enabled
  // on its own.
  it("still renders the attribution in classic when the user pref is off", () => {
    const { container } = renderWithWallpaper({
      prefs: { "nova.enabled": false, "newtabWallpapers.user.enabled": false },
    });

    expect(
      container.querySelector("main.newtab-main .wallpaper-attribution")
    ).toBeInTheDocument();
  });
});

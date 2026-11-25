import { render, fireEvent } from "@testing-library/react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { ContentSection } from "content-src/components/CustomizeMenu/ContentSection/ContentSection";
import { WrapWithProvider } from "test/jest/test-utils";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";

const DEFAULT_PROPS = {
  mayHaveWidgets: false,
  mayHaveWeather: true,
  mayHaveTimerWidget: false,
  mayHaveListsWidget: false,
  wallpapersEnabled: false,
  wallpapersUserEnabled: false,
  activeWallpaper: null,
  showWallpapersPanel: false,
  wallpapersPanelCategory: null,
  openWallpapersPanel: jest.fn(),
  closeWallpapersPanel: jest.fn(),
  enabledSections: {
    topSitesEnabled: true,
    pocketEnabled: true,
    weatherEnabled: true,
    showInferredPersonalizationEnabled: false,
    topSitesRowsCount: 1,
  },
  enabledWidgets: {
    timerEnabled: false,
    listsEnabled: false,
  },
  dispatch: jest.fn(),
  setPref: jest.fn(),
  openPreferences: jest.fn(),
  toggleSectionsMgmtPanel: jest.fn(),
  toggleWidgetsManagementPanel: jest.fn(),
  pocketRegion: "US",
  mayHaveInferredPersonalization: false,
  mayHaveTopicSections: false,
  mayHaveWeatherForecast: false,
  weatherDisplay: "simple",
};

// WallpaperCategories reads newtabWallpapers.wallpaper from the store;
// provide it so the component doesn't crash on .includes().
const WALLPAPER_STATE = {
  ...INITIAL_STATE,
  Prefs: {
    ...INITIAL_STATE.Prefs,
    values: {
      ...INITIAL_STATE.Prefs.values,
      "newtabWallpapers.wallpaper": "",
    },
  },
};

// ContentSection is a class component whose event handler is wired to custom
// elements via lowercase `ontoggle`/`onChange` props that jsdom cannot fire,
// so obtain the instance via ref to exercise onPreferenceSelect directly and
// assert on the observable dispatch/setPref spies.
function renderWithInstance(props) {
  const instanceRef = { current: null };
  const result = render(
    <ContentSection
      {...props}
      ref={instance => {
        instanceRef.current = instance;
      }}
    />
  );
  return { ...result, instance: instanceRef.current };
}

describe("ContentSection", () => {
  afterEach(() => {
    jest.clearAllMocks();
  });

  it("should render the component", () => {
    const { container } = render(<ContentSection {...DEFAULT_PROPS} />);
    expect(container.querySelector(".home-section")).toBeInTheDocument();
  });

  it("should look for a data-event-source attribute and dispatch an event for INPUT", () => {
    const dispatch = jest.fn();
    const setPref = jest.fn();
    const { instance } = renderWithInstance({
      ...DEFAULT_PROPS,
      dispatch,
      setPref,
    });

    instance.onPreferenceSelect({
      target: {
        nodeName: "INPUT",
        checked: true,
        dataset: { preference: "foo", eventSource: "bar" },
      },
    });

    expect(dispatch).toHaveBeenCalledWith(
      ac.UserEvent({
        event: "PREF_CHANGED",
        source: "bar",
        value: { status: true, menu_source: "CUSTOMIZE_MENU" },
      })
    );
    expect(setPref).toHaveBeenCalledWith("foo", true);
  });

  it("should call setPref with parsed integer value for MOZ-SELECT", () => {
    const setPref = jest.fn();
    const { instance } = renderWithInstance({ ...DEFAULT_PROPS, setPref });

    instance.onPreferenceSelect({
      target: {
        nodeName: "MOZ-SELECT",
        value: "3",
        dataset: { preference: "topSitesRows" },
      },
    });

    expect(setPref).toHaveBeenCalledWith("topSitesRows", 3);
  });

  it("should have data-event-source attributes on relevant pref changing inputs", () => {
    const { container } = render(<ContentSection {...DEFAULT_PROPS} />);

    expect(container.querySelector("#weather-toggle")).toHaveAttribute(
      "data-event-source",
      "WEATHER"
    );
    expect(container.querySelector("#shortcuts-toggle")).toHaveAttribute(
      "data-event-source",
      "TOP_SITES"
    );
    expect(container.querySelector("#pocket-toggle")).toHaveAttribute(
      "data-event-source",
      "TOP_STORIES"
    );
  });

  // Bug 1985305 - "Widgets Toggle Section" Layout

  it("renders the Widgets section when mayHaveWidgets = true", () => {
    const { container } = render(
      <ContentSection {...DEFAULT_PROPS} mayHaveWidgets={true} />
    );
    expect(container.querySelector(".widgets-section")).toBeInTheDocument();
  });

  it("does not render the Widgets section when mayHaveWidgets = false", () => {
    const { container } = render(
      <ContentSection {...DEFAULT_PROPS} mayHaveWidgets={false} />
    );
    expect(container.querySelector(".widgets-section")).not.toBeInTheDocument();
  });

  it("places Weather under Widgets when widgets are enabled and doesn't render default Weather placement", () => {
    const { container } = render(
      <ContentSection
        {...DEFAULT_PROPS}
        mayHaveWidgets={true}
        mayHaveWeather={true}
        enabledSections={{
          ...DEFAULT_PROPS.enabledSections,
          weatherEnabled: true,
        }}
      />
    );

    expect(
      container.querySelector(
        ".widgets-section #weather-section #weather-toggle"
      )
    ).toBeInTheDocument();
    expect(
      container.querySelector(".settings-toggles #weather-section")
    ).not.toBeInTheDocument();
  });

  it("places Weather in the default area when widgets are disabled", () => {
    const { container } = render(
      <ContentSection
        {...DEFAULT_PROPS}
        mayHaveWidgets={false}
        mayHaveWeather={true}
      />
    );

    expect(
      container.querySelector(
        ".settings-toggles #weather-section #weather-toggle"
      )
    ).toBeInTheDocument();
    expect(
      container.querySelector(".widgets-section #weather-section")
    ).not.toBeInTheDocument();
  });

  it("renders Lists toggle only when mayHaveListsWidget = true in Widgets section", () => {
    const { container, rerender } = render(
      <ContentSection
        {...DEFAULT_PROPS}
        mayHaveWidgets={true}
        mayHaveListsWidget={true}
        enabledWidgets={{
          ...DEFAULT_PROPS.enabledWidgets,
          listsEnabled: true,
        }}
      />
    );

    expect(
      container.querySelector(
        ".widgets-section #lists-widget-section #lists-toggle"
      )
    ).toBeInTheDocument();

    rerender(
      <ContentSection
        {...DEFAULT_PROPS}
        mayHaveWidgets={true}
        mayHaveListsWidget={false}
      />
    );
    expect(
      container.querySelector("#lists-widget-section")
    ).not.toBeInTheDocument();
  });

  it("renders Timer toggle only when mayHaveTimerWidget = true in Widgets section", () => {
    const { container, rerender } = render(
      <ContentSection
        {...DEFAULT_PROPS}
        mayHaveWidgets={true}
        mayHaveTimerWidget={true}
        enabledWidgets={{
          ...DEFAULT_PROPS.enabledWidgets,
          timerEnabled: true,
        }}
      />
    );

    expect(
      container.querySelector(
        ".widgets-section #timer-widget-section #timer-toggle"
      )
    ).toBeInTheDocument();

    rerender(
      <ContentSection
        {...DEFAULT_PROPS}
        mayHaveWidgets={true}
        mayHaveTimerWidget={false}
      />
    );
    expect(
      container.querySelector("#timer-widget-section")
    ).not.toBeInTheDocument();
  });

  it("labels the classic Crossword toggle with its Fluent string", () => {
    const { container } = render(
      <ContentSection
        {...DEFAULT_PROPS}
        mayHaveWidgets={true}
        mayHaveCrosswordWidget={true}
      />
    );

    const toggle = container.querySelector("#crossword-toggle");
    expect(toggle).toHaveAttribute(
      "data-l10n-id",
      "newtab-custom-widget-crossword-toggle"
    );
    expect(toggle).not.toHaveAttribute("label");
  });

  it("should dispatch WIDGETS_ENABLED with widget_size=large when widgetsMayBeMaximized is false", () => {
    const dispatch = jest.fn();
    const { instance } = renderWithInstance({
      ...DEFAULT_PROPS,
      dispatch,
      enabledWidgets: {
        listsEnabled: false,
        timerEnabled: false,
        widgetsMaximized: false,
        widgetsMayBeMaximized: false,
      },
    });

    instance.onPreferenceSelect({
      target: {
        nodeName: "INPUT",
        checked: true,
        dataset: {
          preference: "widgets.lists.enabled",
          eventSource: "WIDGET_LISTS",
        },
      },
    });

    const enabledCall = dispatch.mock.calls.find(
      ([action]) => action?.type === at.WIDGETS_ENABLED
    );
    expect(enabledCall).toBeTruthy();
    expect(enabledCall?.[0].data.widget_size).toBe("large");
  });

  it("should dispatch WIDGETS_ENABLED with widget_name=recent_searches when the recent searches toggle fires", () => {
    const dispatch = jest.fn();
    const { container, instance } = renderWithInstance({
      ...DEFAULT_PROPS,
      dispatch,
      // The classic-path widget toggles render only when the widgets section
      // is available and Nova is off.
      mayHaveWidgets: true,
      novaEnabled: false,
      mayHaveRecentSearchesWidget: true,
      enabledWidgets: {
        recentSearchesEnabled: false,
        widgetsMaximized: false,
        widgetsMayBeMaximized: false,
      },
    });

    expect(
      container.querySelector("#recent-searches-widget-section")
    ).toBeInTheDocument();

    instance.onPreferenceSelect({
      target: {
        nodeName: "INPUT",
        checked: true,
        dataset: {
          preference: "widgets.recentSearches.enabled",
          eventSource: "WIDGET_RECENT_SEARCHES",
        },
      },
    });

    const enabledCall = dispatch.mock.calls.find(
      ([action]) => action?.type === at.WIDGETS_ENABLED
    );
    expect(enabledCall).toBeTruthy();
    expect(enabledCall?.[0].data.widget_name).toBe("recent_searches");
  });

  it("should dispatch WIDGETS_ENABLED with widget_size=small for Weather widget", () => {
    const dispatch = jest.fn();
    const { instance } = renderWithInstance({
      ...DEFAULT_PROPS,
      dispatch,
      enabledWidgets: {
        widgetsMaximized: false,
        widgetsMayBeMaximized: false,
      },
    });

    instance.onPreferenceSelect({
      target: {
        nodeName: "INPUT",
        checked: true,
        dataset: {
          preference: "showWeather",
          eventSource: "WEATHER",
        },
      },
    });

    const enabledCall = dispatch.mock.calls.find(
      ([action]) => action?.type === at.WIDGETS_ENABLED
    );
    expect(enabledCall).toBeTruthy();
    expect(enabledCall?.[0].data.widget_name).toBe("weather");
    expect(enabledCall?.[0].data.widget_size).toBe("small");
  });

  describe("SectionsMgmtPanel", () => {
    const STATE_WITH_SECTIONS = {
      ...INITIAL_STATE,
      DiscoveryStream: {
        ...INITIAL_STATE.DiscoveryStream,
        layout: [
          {
            components: [
              {
                type: "CardGrid",
                feed: {
                  url: "https://example.com/feed",
                },
              },
            ],
          },
        ],
        feeds: {
          data: {
            "https://example.com/feed": {
              data: {
                sections: [
                  {
                    sectionKey: "technology",
                    title: "Technology",
                    receivedRank: 0,
                  },
                ],
              },
            },
          },
        },
        sectionPersonalization: {},
      },
    };

    // SectionsMgmtPanel renders a moz-box-button with this l10n id as its
    // visible entry point, so use it as the DOM presence signal.
    const SECTIONS_PANEL_SELECTOR =
      "moz-box-button[data-l10n-id='newtab-section-manage-topics-button-v2']";

    it("should not render SectionsMgmtPanel when mayHaveTopicSections is false", () => {
      const { container } = render(
        <WrapWithProvider state={STATE_WITH_SECTIONS}>
          <ContentSection
            {...DEFAULT_PROPS}
            mayHaveTopicSections={false}
            enabledSections={{
              ...DEFAULT_PROPS.enabledSections,
              pocketEnabled: true,
            }}
          />
        </WrapWithProvider>
      );
      expect(
        container.querySelector(SECTIONS_PANEL_SELECTOR)
      ).not.toBeInTheDocument();
    });

    it("should render SectionsMgmtPanel when mayHaveTopicSections is true", () => {
      const { container } = render(
        <WrapWithProvider state={STATE_WITH_SECTIONS}>
          <ContentSection
            {...DEFAULT_PROPS}
            mayHaveTopicSections={true}
            enabledSections={{
              ...DEFAULT_PROPS.enabledSections,
              pocketEnabled: true,
            }}
          />
        </WrapWithProvider>
      );
      expect(
        container.querySelector(SECTIONS_PANEL_SELECTOR)
      ).toBeInTheDocument();
    });

    it("should render SectionsMgmtPanel only when pocket is enabled and mayHaveTopicSections is true", () => {
      const { container } = render(
        <WrapWithProvider state={STATE_WITH_SECTIONS}>
          <ContentSection
            {...DEFAULT_PROPS}
            mayHaveTopicSections={true}
            enabledSections={{
              ...DEFAULT_PROPS.enabledSections,
              pocketEnabled: false,
            }}
          />
        </WrapWithProvider>
      );
      expect(
        container.querySelector(SECTIONS_PANEL_SELECTOR)
      ).toBeInTheDocument();
    });
  });

  // @nova-cleanup(remove-conditional): Remove novaEnabled condition from wallpapers section
  it("drives WallpaperCategories from the wallpaper subpanel props", () => {
    const openWallpapersPanel = jest.fn();
    const closeWallpapersPanel = jest.fn();
    const state = {
      ...WALLPAPER_STATE,
      Wallpapers: {
        ...WALLPAPER_STATE.Wallpapers,
        wallpaperList: [
          { title: "moon", category: "celestial", theme: "light" },
        ],
        categories: ["celestial"],
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <ContentSection
          {...DEFAULT_PROPS}
          wallpapersEnabled={true}
          showWallpapersPanel={true}
          wallpapersPanelCategory="celestial"
          openWallpapersPanel={openWallpapersPanel}
          closeWallpapersPanel={closeWallpapersPanel}
        />
      </WrapWithProvider>
    );

    expect(
      container.querySelector(".wallpaper-list .arrow-button")
    ).toHaveAttribute(
      "data-l10n-id",
      "newtab-wallpaper-category-title-celestial"
    );

    fireEvent.click(container.querySelector("#celestial"));
    expect(openWallpapersPanel).toHaveBeenCalledWith("celestial");

    fireEvent.click(container.querySelector(".wallpaper-list .arrow-button"));
    expect(closeWallpapersPanel).toHaveBeenCalledTimes(1);
  });

  describe("wallpapers section (novaEnabled)", () => {
    const NOVA_PROPS = {
      novaEnabled: true,
      wallpapersEnabled: true,
      toggleWidgetsManagementPanel: jest.fn(),
      showWidgetsManagementPanel: false,
    };

    it("renders the wallpaper toggle", () => {
      const { container } = render(
        <WrapWithProvider state={WALLPAPER_STATE}>
          <ContentSection
            {...DEFAULT_PROPS}
            {...NOVA_PROPS}
            wallpapersUserEnabled={false}
          />
        </WrapWithProvider>
      );
      expect(container.querySelector("#wallpapers-toggle")).toBeInTheDocument();
    });

    it("shows WallpaperCategories regardless of toggle state", () => {
      const { container } = render(
        <WrapWithProvider state={WALLPAPER_STATE}>
          <ContentSection
            {...DEFAULT_PROPS}
            {...NOVA_PROPS}
            wallpapersUserEnabled={false}
          />
        </WrapWithProvider>
      );
      expect(container.querySelector(".category-list")).toBeInTheDocument();
    });

    it("sets newtabWallpapers.user.enabled to false when the wallpaper toggle is turned off", () => {
      const setPref = jest.fn();
      const instanceRef = { current: null };
      render(
        <WrapWithProvider state={WALLPAPER_STATE}>
          <ContentSection
            {...DEFAULT_PROPS}
            {...NOVA_PROPS}
            setPref={setPref}
            wallpapersUserEnabled={true}
            ref={instance => {
              instanceRef.current = instance;
            }}
          />
        </WrapWithProvider>
      );

      instanceRef.current.onPreferenceSelect({
        target: {
          nodeName: "MOZ-TOGGLE",
          pressed: false,
          dataset: {
            preference: "newtabWallpapers.user.enabled",
            eventSource: "WALLPAPERS",
          },
        },
      });

      expect(setPref).toHaveBeenCalledWith(
        "newtabWallpapers.user.enabled",
        false
      );
    });

    it("sets newtabWallpapers.user.enabled to true when the wallpaper toggle is turned on", () => {
      const setPref = jest.fn();
      const instanceRef = { current: null };
      render(
        <WrapWithProvider state={WALLPAPER_STATE}>
          <ContentSection
            {...DEFAULT_PROPS}
            {...NOVA_PROPS}
            setPref={setPref}
            wallpapersUserEnabled={false}
            ref={instance => {
              instanceRef.current = instance;
            }}
          />
        </WrapWithProvider>
      );

      instanceRef.current.onPreferenceSelect({
        target: {
          nodeName: "MOZ-TOGGLE",
          pressed: true,
          dataset: {
            preference: "newtabWallpapers.user.enabled",
            eventSource: "WALLPAPERS",
          },
        },
      });

      expect(setPref).toHaveBeenCalledWith(
        "newtabWallpapers.user.enabled",
        true
      );
    });

    // @nova-cleanup(remove-conditional): Remove the `.widgets-section` assertion once the novaEnabled condition is removed
    it("renders WidgetsManagementPanel instead of .widgets-section when novaEnabled and mayHaveWidgets are true", () => {
      const { container } = render(
        <WrapWithProvider state={WALLPAPER_STATE}>
          <ContentSection
            {...DEFAULT_PROPS}
            {...NOVA_PROPS}
            mayHaveWidgets={true}
          />
        </WrapWithProvider>
      );
      expect(
        container.querySelector("#widgets-management-panel")
      ).toBeInTheDocument();
      expect(
        container.querySelector(".widgets-section")
      ).not.toBeInTheDocument();
    });

    describe("web notifications toggle", () => {
      function renderWith({ mayHave = true, enabled = false } = {}) {
        return render(
          <ContentSection
            {...DEFAULT_PROPS}
            mayHaveWebNotifications={mayHave}
            enabledSections={{
              ...DEFAULT_PROPS.enabledSections,
              webNotificationsEnabled: enabled,
            }}
          />
        );
      }

      it("renders the web notifications toggle under the shortcuts section", () => {
        const { container } = renderWith();
        expect(
          container.querySelector("#web-notifications-toggle")
        ).toBeInTheDocument();
      });

      it("does not offer the toggle when the feature gate is off", () => {
        const { container } = renderWith({ mayHave: false });
        expect(
          container.querySelector("#web-notifications-toggle")
        ).not.toBeInTheDocument();
      });

      it("reflects webNotificationsEnabled in the toggle pressed state", () => {
        const { container } = renderWith({ enabled: true });
        expect(
          container.querySelector("#web-notifications-toggle")
        ).toHaveAttribute("pressed");
      });

      it("writes the user pref when toggled", () => {
        const { instance } = renderWithInstance({
          ...DEFAULT_PROPS,
          mayHaveWebNotifications: true,
        });
        instance.onPreferenceSelect({
          target: {
            nodeName: "MOZ-TOGGLE",
            pressed: true,
            dataset: { preference: "showWebNotifications" },
          },
        });
        expect(DEFAULT_PROPS.setPref).toHaveBeenCalledWith(
          "showWebNotifications",
          true
        );
      });
    });

    describe("widgets system toggle", () => {
      it("renders the widgets system toggle in ContentSection", () => {
        const { container } = render(
          <WrapWithProvider state={WALLPAPER_STATE}>
            <ContentSection
              {...DEFAULT_PROPS}
              {...NOVA_PROPS}
              mayHaveWidgets={true}
              widgetsEnabled={false}
            />
          </WrapWithProvider>
        );
        expect(
          container.querySelector("#widgets-system-toggle")
        ).toBeInTheDocument();
      });

      it("sets widgets-system-toggle pressed when widgetsEnabled is true", () => {
        const { container } = render(
          <WrapWithProvider state={WALLPAPER_STATE}>
            <ContentSection
              {...DEFAULT_PROPS}
              {...NOVA_PROPS}
              mayHaveWidgets={true}
              widgetsEnabled={true}
            />
          </WrapWithProvider>
        );
        expect(
          container.querySelector("#widgets-system-toggle")
        ).toHaveAttribute("pressed");
      });

      it("sets widgets-system-toggle unpressed when widgetsEnabled is false", () => {
        const { container } = render(
          <WrapWithProvider state={WALLPAPER_STATE}>
            <ContentSection
              {...DEFAULT_PROPS}
              {...NOVA_PROPS}
              mayHaveWidgets={true}
              widgetsEnabled={false}
            />
          </WrapWithProvider>
        );
        expect(
          container.querySelector("#widgets-system-toggle")
        ).not.toHaveAttribute("pressed");
      });
    });
  });
});

describe("<ContentSection>", () => {
  const DEFAULT_PROPS = {
    dispatch: jest.fn(),
    openPreferences: jest.fn(),
    setPref: jest.fn(),
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
    showWallpapersPanel: false,
    wallpapersPanelCategory: null,
    openWallpapersPanel: jest.fn(),
    closeWallpapersPanel: jest.fn(),
    toggleSectionsMgmtPanel: jest.fn(),
    showSectionsMgmtPanel: false,
    novaEnabled: false,
    browserNovaEnabled: false,
    toggleWidgetsManagementPanel: jest.fn(),
    showWidgetsManagementPanel: false,
  };

  it("should render", () => {
    const { container } = render(<ContentSection {...DEFAULT_PROPS} />);
    expect(container.querySelector(".home-section")).toBeInTheDocument();
  });

  it("does not render the theme-picker when browserNovaEnabled is false (novaEnabled does not gate it)", () => {
    const { container } = render(
      <ContentSection
        {...DEFAULT_PROPS}
        novaEnabled={true}
        browserNovaEnabled={false}
      />
    );
    expect(container.querySelector("theme-picker")).not.toBeInTheDocument();
  });

  it("renders the compact theme-picker and themes panel when browser Nova is enabled", () => {
    const { container } = render(
      <WrapWithProvider>
        <ContentSection {...DEFAULT_PROPS} browserNovaEnabled={true} />
      </WrapWithProvider>
    );
    const picker = container.querySelector("theme-picker");
    expect(picker).toBeInTheDocument();
    expect(picker).toHaveAttribute("layout", "compact");
    expect(picker).toHaveAttribute("installsource", "about:newtab");
    expect(
      container.querySelector(".themes-mgmt-panel-container")
    ).toBeInTheDocument();
  });

  describe("inputUserEvent telemetry", () => {
    function getInstance(extraProps = {}) {
      const ref = { current: null };
      render(
        <ContentSection
          {...DEFAULT_PROPS}
          ref={instance => {
            ref.current = instance;
          }}
          {...extraProps}
        />
      );
      return ref.current;
    }

    it("dispatches WIDGETS_ENABLED with widget_name='crossword' when the crossword toggle fires", () => {
      const dispatch = jest.fn();
      const instance = getInstance({ dispatch });
      instance.inputUserEvent("WIDGET_CROSSWORD", true);

      const enabledCall = dispatch.mock.calls.find(
        ([action]) => action?.type === at.WIDGETS_ENABLED
      );
      expect(enabledCall?.[0].data).toMatchObject({
        widget_name: "crossword",
        widget_source: "customize_panel",
        enabled: true,
        widget_size: "large",
      });
    });

    it("dispatches WIDGETS_ENABLED with widget_name='picture_of_the_day' when the picture toggle fires", () => {
      const dispatch = jest.fn();
      const instance = getInstance({ dispatch });
      instance.inputUserEvent("WIDGET_PICTURE_OF_THE_DAY", true);

      const enabledCall = dispatch.mock.calls.find(
        ([action]) => action?.type === at.WIDGETS_ENABLED
      );
      expect(enabledCall?.[0].data).toMatchObject({
        widget_name: "picture_of_the_day",
        widget_source: "customize_panel",
        enabled: true,
        widget_size: "large",
      });
    });
  });
});

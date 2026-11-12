import { render, fireEvent } from "@testing-library/react";
import { WrapWithProvider } from "test/jest/test-utils";
import { WallpaperFeatureHighlight } from "content-src/components/DiscoveryStreamComponents/FeatureHighlight/WallpaperFeatureHighlight";
import { INITIAL_STATE } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";

describe("<WallpaperFeatureHighlight>", () => {
  it("should render when messageData has content", () => {
    const state = {
      ...INITIAL_STATE,
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: { content: { feature: "WALLPAPER" } },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={jest.fn()}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    expect(
      container.querySelector(".wallpaper-feature-highlight")
    ).toBeInTheDocument();
  });

  it("should render the Nova image class when nova.enabled is true", () => {
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: { content: { feature: "WALLPAPER" } },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={jest.fn()}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    expect(
      container.querySelector(".wallpaper-feature-highlight-image")
    ).toBeInTheDocument();
    expect(
      container.querySelector(".follow-section-button-highlight-image")
    ).not.toBeInTheDocument();
  });

  it("uses the message's l10n ids over the built-in Nova copy", () => {
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: {
          content: {
            feature: "WALLPAPER",
            messageType: "CustomWallpaperHighlight",
            title: "newtab-nova-customization-callout-header",
            subtitle: "newtab-nova-customization-callout-message",
            cta: "newtab-nova-customization-callout-primary-button",
          },
        },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={jest.fn()}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector(".title").getAttribute("data-l10n-id")).toBe(
      "newtab-nova-customization-callout-header"
    );
    expect(
      container.querySelector(".subtitle").getAttribute("data-l10n-id")
    ).toBe("newtab-nova-customization-callout-message");
    expect(
      container.querySelector("moz-button").getAttribute("data-l10n-id")
    ).toBe("newtab-nova-customization-callout-primary-button");
  });

  it("uses the message's raw card copy over the built-in Nova copy", () => {
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: {
          content: {
            feature: "WALLPAPER",
            messageType: "CustomWallpaperHighlight",
            cardTitle: "Card title",
            cardMessage: "Card message",
            cardCta: "Card CTA",
          },
        },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={jest.fn()}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    expect(container.querySelector(".title").textContent).toBe("Card title");
    expect(container.querySelector(".subtitle").textContent).toBe(
      "Card message"
    );
    expect(container.querySelector("moz-button").getAttribute("label")).toBe(
      "Card CTA"
    );
  });

  it("deep-links the CTA into the Firefox wallpaper category", () => {
    const dispatch = jest.fn();
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: {
          content: {
            feature: "WALLPAPER",
            messageType: "CustomWallpaperHighlight",
          },
        },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={dispatch}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    fireEvent.click(container.querySelector(".button-wrapper moz-button"));
    expect(dispatch).toHaveBeenCalledWith({
      type: at.SHOW_PERSONALIZE,
      data: {
        source: "wallpaper_feature_highlight",
        wallpaperCategory: "firefox",
      },
    });
  });

  it("deep-links into the wallpaper category provided by the message", () => {
    const dispatch = jest.fn();
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: {
          content: {
            feature: "WALLPAPER",
            messageType: "CustomWallpaperHighlight",
            wallpaperCategory: "celestial",
          },
        },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={dispatch}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    fireEvent.click(container.querySelector(".button-wrapper moz-button"));
    expect(dispatch).toHaveBeenCalledWith({
      type: at.SHOW_PERSONALIZE,
      data: {
        source: "wallpaper_feature_highlight",
        wallpaperCategory: "celestial",
      },
    });
  });

  it("opens the top-level panel when the message sets an empty category", () => {
    const dispatch = jest.fn();
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: {
          content: {
            feature: "WALLPAPER",
            messageType: "CustomWallpaperHighlight",
            wallpaperCategory: null,
          },
        },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={dispatch}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    fireEvent.click(container.querySelector(".button-wrapper moz-button"));
    expect(dispatch).toHaveBeenCalledWith({
      type: at.SHOW_PERSONALIZE,
      data: { source: "wallpaper_feature_highlight", wallpaperCategory: null },
    });
  });

  it("opens the top-level panel for an unknown category", () => {
    const dispatch = jest.fn();
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: {
          content: {
            feature: "WALLPAPER",
            messageType: "CustomWallpaperHighlight",
            wallpaperCategory: "not-a-real-category",
          },
        },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={dispatch}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    fireEvent.click(container.querySelector(".button-wrapper moz-button"));
    expect(dispatch).toHaveBeenCalledWith({
      type: at.SHOW_PERSONALIZE,
      data: { source: "wallpaper_feature_highlight", wallpaperCategory: null },
    });
  });

  it("opens the top-level panel for the custom-wallpaper category", () => {
    const dispatch = jest.fn();
    const state = {
      ...INITIAL_STATE,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: {
          ...INITIAL_STATE.Prefs.values,
          "nova.enabled": true,
        },
      },
      Messages: {
        ...INITIAL_STATE.Messages,
        messageData: {
          content: {
            feature: "WALLPAPER",
            messageType: "CustomWallpaperHighlight",
            wallpaperCategory: "custom-wallpaper",
          },
        },
      },
    };
    const { container } = render(
      <WrapWithProvider state={state}>
        <WallpaperFeatureHighlight
          dispatch={dispatch}
          handleDismiss={jest.fn()}
          handleClick={jest.fn()}
          handleBlock={jest.fn()}
        />
      </WrapWithProvider>
    );
    fireEvent.click(container.querySelector(".button-wrapper moz-button"));
    expect(dispatch).toHaveBeenCalledWith({
      type: at.SHOW_PERSONALIZE,
      data: { source: "wallpaper_feature_highlight", wallpaperCategory: null },
    });
  });
});

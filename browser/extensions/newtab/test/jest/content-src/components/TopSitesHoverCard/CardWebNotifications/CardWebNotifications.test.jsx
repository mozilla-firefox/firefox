import { render, fireEvent } from "@testing-library/react";
import { Provider } from "react-redux";
import { combineReducers, createStore } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { CardWebNotifications } from "content-src/components/TopSitesHoverCard/CardWebNotifications/CardWebNotifications";

const ORIGIN = "https://example.com";

const NOTIFICATIONS = {
  a: {
    id: "a",
    title: "First",
    body: "A",
    origin: ORIGIN,
    timestamp: 100,
  },
  b: {
    id: "b",
    title: "Second",
    origin: ORIGIN,
    timestamp: 300,
  },
  c: {
    id: "c",
    title: "Third",
    origin: ORIGIN,
    timestamp: 200,
  },
  d: {
    id: "d",
    title: "Fourth",
    origin: ORIGIN,
    timestamp: 400,
  },
};
const BY_ORIGIN = { [ORIGIN]: ["a", "b", "c", "d"] };

function mockState({ notifications = {}, byOrigin = {} } = {}) {
  return {
    ...INITIAL_STATE,
    WebNotifications: {
      ...INITIAL_STATE.WebNotifications,
      notifications,
      byOrigin,
    },
  };
}

function renderCard(link, stateOpts) {
  const store = createStore(combineReducers(reducers), mockState(stateOpts));
  const dispatch = jest.spyOn(store, "dispatch");
  const { container } = render(
    <Provider store={store}>
      <CardWebNotifications link={link} />
    </Provider>
  );
  return { container, dispatch };
}

function dispatchedActions(dispatch) {
  return dispatch.mock.calls.map(([action]) => action);
}

describe("<CardWebNotifications>", () => {
  const link = { url: `${ORIGIN}/path`, label: "Example" };

  it("renders nothing when the site has no notifications", () => {
    const { container } = renderCard(link, {});
    expect(container.querySelectorAll(".top-sites-hover-card")).toHaveLength(0);
  });

  it("renders nothing for a non-http(s) link", () => {
    const { container } = renderCard(
      { url: "about:blank" },
      { notifications: NOTIFICATIONS, byOrigin: BY_ORIGIN }
    );
    expect(container.querySelectorAll(".top-sites-hover-card")).toHaveLength(0);
  });

  it("lists every notification, most recent first", () => {
    const { container } = renderCard(link, {
      notifications: NOTIFICATIONS,
      byOrigin: BY_ORIGIN,
    });
    const titles = Array.from(
      container.querySelectorAll(".top-sites-hover-card-notification-title")
    ).map(node => node.textContent);
    expect(titles).toEqual(["Fourth", "Second", "Third", "First"]);
  });

  it("renders every notification as an activatable button", () => {
    const { container } = renderCard(link, {
      notifications: {
        a: {
          id: "a",
          title: "SW",
          origin: ORIGIN,
          timestamp: 2,
        },
        b: {
          id: "b",
          title: "Page",
          origin: ORIGIN,
          timestamp: 1,
        },
      },
      byOrigin: { [ORIGIN]: ["a", "b"] },
    });
    const activators = container.querySelectorAll(
      ".top-sites-hover-card-notification-activate"
    );
    expect(activators).toHaveLength(2);
    expect(activators[0].tagName).toBe("BUTTON");
    expect(activators[1].tagName).toBe("BUTTON");
  });

  it("dispatches WEB_NOTIFICATIONS_CLICK when a notification is activated", () => {
    const { container, dispatch } = renderCard(link, {
      notifications: {
        a: {
          id: "a",
          title: "Page",
          origin: ORIGIN,
          timestamp: 1,
        },
      },
      byOrigin: { [ORIGIN]: ["a"] },
    });
    fireEvent.click(
      container.querySelector(
        "button.top-sites-hover-card-notification-activate"
      )
    );
    expect(
      dispatchedActions(dispatch).some(
        action => action.type === at.WEB_NOTIFICATIONS_CLICK
      )
    ).toBe(true);
  });

  it("dispatches SHOW_PERSONALIZE with its source from the settings button", () => {
    const { container, dispatch } = renderCard(link, {
      notifications: NOTIFICATIONS,
      byOrigin: BY_ORIGIN,
    });
    fireEvent.click(
      container.querySelector("button.top-sites-hover-card-settings")
    );
    expect(dispatchedActions(dispatch)).toContainEqual({
      type: at.SHOW_PERSONALIZE,
      data: { source: "top_sites_web_notifications" },
    });
  });

  it("dispatches WEB_NOTIFICATIONS_DISMISS from the per-row dismiss", () => {
    const { container, dispatch } = renderCard(link, {
      notifications: {
        a: {
          id: "a",
          title: "A",
          origin: ORIGIN,
          timestamp: 1,
        },
      },
      byOrigin: { [ORIGIN]: ["a"] },
    });
    fireEvent.click(
      container.querySelector(
        "button.top-sites-hover-card-notification-dismiss"
      )
    );
    const action = dispatchedActions(dispatch).find(
      a => a.type === at.WEB_NOTIFICATIONS_DISMISS
    );
    expect(action).toBeTruthy();
    expect(action.data).toEqual({ origin: ORIGIN, id: "a" });
  });

  it("dispatches WEB_NOTIFICATIONS_DISMISS_ALL from mark-all-read", () => {
    const { container, dispatch } = renderCard(link, {
      notifications: NOTIFICATIONS,
      byOrigin: BY_ORIGIN,
    });
    fireEvent.click(
      container.querySelector("button.top-sites-hover-card-mark-read")
    );
    expect(
      dispatchedActions(dispatch).some(
        action => action.type === at.WEB_NOTIFICATIONS_DISMISS_ALL
      )
    ).toBe(true);
  });

  describe("notification icons", () => {
    function renderWithIcon(icon, site = ORIGIN) {
      const id = "icon-1";
      return renderCard(
        { url: `${site}/path`, label: "Example" },
        {
          notifications: { [id]: { id, title: "T", origin: site, icon } },
          byOrigin: { [site]: [id] },
        }
      );
    }

    it("loads an icon through the image proxy, never from the origin", () => {
      const { container } = renderWithIcon("https://example.com/icon.png");
      const src = container
        .querySelector("img.top-sites-hover-card-notification-icon")
        .getAttribute("src");
      expect(src).toContain("https://img-getpocket.cdn.mozilla.net/");
      expect(src).not.toBe("https://example.com/icon.png");
    });

    it("renders no icon when the icon cannot be proxied", () => {
      // eslint-disable-next-line sdl/no-insecure-url
      const { container } = renderWithIcon("http://example.com/icon.png");
      expect(
        container.querySelectorAll("img.top-sites-hover-card-notification-icon")
      ).toHaveLength(0);
    });

    it("renders no icon for a suppressed origin", () => {
      const { container } = renderWithIcon(
        "https://apnews.com/icon.png",
        "https://apnews.com"
      );
      expect(
        container.querySelectorAll("img.top-sites-hover-card-notification-icon")
      ).toHaveLength(0);
    });

    it("drops the icon on a proxy error rather than retrying the origin", () => {
      const { container } = renderWithIcon("https://example.com/icon.png");
      fireEvent.error(
        container.querySelector("img.top-sites-hover-card-notification-icon")
      );
      expect(
        container.querySelectorAll("img.top-sites-hover-card-notification-icon")
      ).toHaveLength(0);
    });
  });
});

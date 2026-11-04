/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { act, fireEvent, render } from "@testing-library/react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import {
  _Card as Card,
  PlaceholderCard,
} from "content-src/components/Card/Card";
import { cardContextTypes } from "content-src/components/Card/types";
import { LinkMenu } from "content-src/components/LinkMenu/LinkMenu";
import { WrapWithProvider } from "test/jest/test-utils";
import React from "react";

// LinkMenu is rendered by Card via ContextMenuButton. The legacy test located
// it by Enzyme component identity (find(LinkMenu)) and read its props(); there
// is no DOM equivalent for props like dispatch/onUpdate/site, so it is mocked
// here to record the props Card passes and to render a stand-in ".context-menu"
// element (the class the real LinkMenu renders) that find(LinkMenu) observed.
jest.mock("content-src/components/LinkMenu/LinkMenu", () => {
  const MockReact = require("react");
  return {
    LinkMenu: jest.fn(() =>
      MockReact.createElement("div", { className: "context-menu" })
    ),
  };
});

let DEFAULT_PROPS = {
  dispatch: jest.fn(),
  index: 0,
  link: {
    hostname: "foo",
    title: "A title for foo",
    url: "https://www.foo.com",
    type: "history",
    description: "A description for foo",
    image: "https://www.foo.com/img.png",
    guid: 1,
  },
  eventSource: "TOP_STORIES",
  shouldSendImpressionStats: true,
  contextMenuOptions: ["Separator"],
};

let DEFAULT_BLOB_IMAGE = {
  path: "/testpath",
  data: new Blob([0]),
};

function mountCardWithProps(props) {
  return render(
    <WrapWithProvider>
      <Card {...props} />
    </WrapWithProvider>
  );
}

describe("<Card>", () => {
  let wrapper;
  beforeEach(() => {
    wrapper = mountCardWithProps(DEFAULT_PROPS);
  });
  afterEach(() => {
    DEFAULT_PROPS.dispatch.mockClear();
    LinkMenu.mockClear();
  });
  it("should render a Card component", () =>
    expect(wrapper.container.querySelector(".card-outer")).toBeInTheDocument());
  it("should add the right url", () => {
    expect(wrapper.container.querySelector("a")).toHaveAttribute(
      "href",
      DEFAULT_PROPS.link.url
    );

    // test that pocket cards get a special open_url href
    const pocketLink = Object.assign({}, DEFAULT_PROPS.link, {
      open_url: "getpocket.com/foo",
      type: "pocket",
    });
    wrapper = render(
      <Card {...Object.assign({}, DEFAULT_PROPS, { link: pocketLink })} />
    );
    expect(wrapper.container.querySelector("a")).toHaveAttribute(
      "href",
      pocketLink.open_url
    );
  });
  it("should display a title", () =>
    expect(wrapper.container.querySelector(".card-title")).toHaveTextContent(
      DEFAULT_PROPS.link.title
    ));
  it("should display a description", () =>
    expect(
      wrapper.container.querySelector(".card-description")
    ).toHaveTextContent(DEFAULT_PROPS.link.description));
  it("should display a host name", () =>
    expect(
      wrapper.container.querySelector(".card-host-name")
    ).toHaveTextContent("foo"));
  it("should have a link menu button", () =>
    expect(
      wrapper.container.querySelector(".context-menu-button")
    ).toBeInTheDocument());
  it("should render a link menu when button is clicked", () => {
    const button = wrapper.container.querySelector(".context-menu-button");
    expect(
      wrapper.container.querySelector(".context-menu")
    ).not.toBeInTheDocument();
    fireEvent.click(button);
    expect(
      wrapper.container.querySelector(".context-menu")
    ).toBeInTheDocument();
  });
  it("should pass dispatch, source, onUpdate, site, options, and index to LinkMenu", () => {
    fireEvent.click(wrapper.container.querySelector(".context-menu-button"));
    // eslint-disable-next-line no-shadow
    const [linkMenuProps] = LinkMenu.mock.calls.at(-1);
    const { dispatch, source, onUpdate, site, options, index } = linkMenuProps;
    expect(dispatch).toBe(DEFAULT_PROPS.dispatch);
    expect(source).toBe(DEFAULT_PROPS.eventSource);
    expect(onUpdate).toBeTruthy();
    expect(site).toBe(DEFAULT_PROPS.link);
    expect(options).toBe(DEFAULT_PROPS.contextMenuOptions);
    expect(index).toBe(DEFAULT_PROPS.index);
  });
  it("should pass through the correct menu options to LinkMenu if overridden by individual card", () => {
    const link = Object.assign({}, DEFAULT_PROPS.link);
    link.contextMenuOptions = ["CheckBookmark"];

    wrapper = mountCardWithProps(Object.assign({}, DEFAULT_PROPS, { link }));
    fireEvent.click(wrapper.container.querySelector(".context-menu-button"));
    // eslint-disable-next-line no-shadow
    const [linkMenuProps] = LinkMenu.mock.calls.at(-1);
    const { options } = linkMenuProps;
    expect(options).toBe(link.contextMenuOptions);
  });
  it("should have a context based on type", () => {
    wrapper = render(<Card {...DEFAULT_PROPS} />);
    const cardContext = wrapper.container.querySelector(".card-context");
    const { icon, fluentID } = cardContextTypes[DEFAULT_PROPS.link.type];
    expect(cardContext.children[0]).toHaveClass(`icon-${icon}`);
    expect(cardContext.children[1]).toHaveClass("card-context-label");
    expect(cardContext.children[1]).toHaveAttribute("data-l10n-id", fluentID);
  });
  it("should support setting custom context", () => {
    const linkWithCustomContext = {
      type: "history",
      context: "Custom",
      icon: "icon-url",
    };

    wrapper = render(
      <Card
        {...Object.assign({}, DEFAULT_PROPS, { link: linkWithCustomContext })}
      />
    );
    const cardContext = wrapper.container.querySelector(".card-context");
    const { icon } = cardContextTypes[DEFAULT_PROPS.link.type];
    expect(cardContext.children[0]).not.toHaveClass(`icon-${icon}`);
    expect(cardContext.children[0].getAttribute("style")).toContain("icon-url");

    expect(cardContext.children[1]).toHaveClass("card-context-label");
    expect(cardContext.children[1]).toHaveTextContent(
      linkWithCustomContext.context
    );
  });
  it("should parse args for fluent correctly", () => {
    const title = '"fluent"';
    const link = { ...DEFAULT_PROPS.link, title };

    wrapper = mountCardWithProps({ ...DEFAULT_PROPS, link });
    const button = wrapper.container.querySelector(".context-menu-button");

    expect(button).toHaveAttribute("data-l10n-args", JSON.stringify({ title }));
  });
  it("should have .active class, on card-outer if context menu is open", () => {
    const button = wrapper.container.querySelector(".context-menu-button");
    expect(wrapper.container.querySelector(".card-outer")).not.toHaveClass(
      "active"
    );
    fireEvent.click(button);
    expect(wrapper.container.querySelector(".card-outer")).toHaveClass(
      "active"
    );
  });
  it("should send OPEN_DOWNLOAD_FILE if we clicked on a download", () => {
    const downloadLink = {
      type: "download",
      url: "download.mov",
    };
    wrapper = mountCardWithProps(
      Object.assign({}, DEFAULT_PROPS, { link: downloadLink })
    );
    const card = wrapper.container.querySelector(".card");
    fireEvent.click(card);
    expect(DEFAULT_PROPS.dispatch).toHaveBeenCalledTimes(3);

    const [[firstAction]] = DEFAULT_PROPS.dispatch.mock.calls;
    expect(firstAction.type).toEqual(at.OPEN_DOWNLOAD_FILE);
    expect(firstAction.data).toEqual(downloadLink);
  });
  it("should send OPEN_LINK if we clicked on anything other than a download", () => {
    const nonDownloadLink = {
      type: "history",
      url: "download.mov",
    };
    wrapper = mountCardWithProps(
      Object.assign({}, DEFAULT_PROPS, { link: nonDownloadLink })
    );
    const card = wrapper.container.querySelector(".card");
    const event = {
      altKey: "1",
      button: 0,
      ctrlKey: "3",
      metaKey: "4",
      shiftKey: "5",
    };
    fireEvent.click(card, event);
    expect(DEFAULT_PROPS.dispatch).toHaveBeenCalledTimes(3);

    const [[firstAction]] = DEFAULT_PROPS.dispatch.mock.calls;
    expect(firstAction.type).toEqual(at.OPEN_LINK);
  });
  describe("card image display", () => {
    const DEFAULT_BLOB_URL = "blob://test";
    let url;
    let originalURL;
    beforeEach(() => {
      originalURL = {
        createObjectURL: globalThis.URL.createObjectURL,
        revokeObjectURL: globalThis.URL.revokeObjectURL,
      };
      url = {
        createObjectURL: jest.fn(() => DEFAULT_BLOB_URL),
        revokeObjectURL: jest.fn(),
      };
      globalThis.URL.createObjectURL = url.createObjectURL;
      globalThis.URL.revokeObjectURL = url.revokeObjectURL;
    });
    afterEach(() => {
      globalThis.URL.createObjectURL =
        originalURL.createObjectURL || (() => {});
      globalThis.URL.revokeObjectURL =
        originalURL.revokeObjectURL || (() => {});
    });
    it("should display a regular image correctly and not call revokeObjectURL when unmounted", () => {
      const ref = React.createRef();
      wrapper = render(<Card {...DEFAULT_PROPS} ref={ref} />);

      expect(ref.current.state.cardImage.path).toBeUndefined();
      expect(ref.current.state.cardImage.url).toEqual(DEFAULT_PROPS.link.image);
      expect(
        wrapper.container
          .querySelector(".card-preview-image")
          .getAttribute("style")
      ).toContain(ref.current.state.cardImage.url);

      wrapper.unmount();
      expect(url.revokeObjectURL).not.toHaveBeenCalled();
    });
    it("should display a blob image correctly and revoke blob url when unmounted", () => {
      const link = Object.assign({}, DEFAULT_PROPS.link, {
        image: DEFAULT_BLOB_IMAGE,
      });
      const ref = React.createRef();
      wrapper = render(<Card {...DEFAULT_PROPS} link={link} ref={ref} />);

      expect(ref.current.state.cardImage.path).toEqual(DEFAULT_BLOB_IMAGE.path);
      expect(ref.current.state.cardImage.url).toEqual(DEFAULT_BLOB_URL);
      expect(
        wrapper.container
          .querySelector(".card-preview-image")
          .getAttribute("style")
      ).toContain(ref.current.state.cardImage.url);

      wrapper.unmount();
      expect(url.revokeObjectURL).toHaveBeenCalledTimes(1);
    });
    it("should not show an image if there isn't one and not call revokeObjectURL when unmounted", () => {
      const link = Object.assign({}, DEFAULT_PROPS.link);
      delete link.image;

      const ref = React.createRef();
      wrapper = render(<Card {...DEFAULT_PROPS} link={link} ref={ref} />);

      expect(ref.current.state.cardImage).toBeNull();
      expect(
        wrapper.container.querySelector(".card-preview-image")
      ).not.toBeInTheDocument();

      wrapper.unmount();
      expect(url.revokeObjectURL).not.toHaveBeenCalled();
    });
    it("should remove current card image if new image is not present", () => {
      const ref = React.createRef();
      wrapper = render(<Card {...DEFAULT_PROPS} ref={ref} />);

      const otherLink = Object.assign({}, DEFAULT_PROPS.link);
      delete otherLink.image;
      wrapper.rerender(
        <Card
          {...Object.assign({}, DEFAULT_PROPS, { link: otherLink })}
          ref={ref}
        />
      );

      expect(ref.current.state.cardImage).toBeNull();
    });
    it("should not create or revoke urls if normal image is already in state", () => {
      wrapper = render(<Card {...DEFAULT_PROPS} />);

      wrapper.rerender(<Card {...DEFAULT_PROPS} />);

      expect(url.createObjectURL).not.toHaveBeenCalled();
      expect(url.revokeObjectURL).not.toHaveBeenCalled();
    });
    it("should not create or revoke more urls if blob image is already in state", () => {
      const link = Object.assign({}, DEFAULT_PROPS.link, {
        image: DEFAULT_BLOB_IMAGE,
      });
      wrapper = render(<Card {...DEFAULT_PROPS} link={link} />);

      expect(url.createObjectURL).toHaveBeenCalledTimes(1);
      expect(url.revokeObjectURL).not.toHaveBeenCalled();

      wrapper.rerender(
        <Card {...Object.assign({}, DEFAULT_PROPS, { link })} />
      );

      expect(url.createObjectURL).toHaveBeenCalledTimes(1);
      expect(url.revokeObjectURL).not.toHaveBeenCalled();
    });
    it("should create blob urls for new blobs and revoke existing ones", () => {
      const link = Object.assign({}, DEFAULT_PROPS.link, {
        image: DEFAULT_BLOB_IMAGE,
      });
      wrapper = render(<Card {...DEFAULT_PROPS} link={link} />);

      expect(url.createObjectURL).toHaveBeenCalledTimes(1);
      expect(url.revokeObjectURL).not.toHaveBeenCalled();

      const otherLink = Object.assign({}, DEFAULT_PROPS.link, {
        image: { path: "/newpath", data: new Blob([0]) },
      });
      wrapper.rerender(
        <Card {...Object.assign({}, DEFAULT_PROPS, { link: otherLink })} />
      );

      expect(url.createObjectURL).toHaveBeenCalledTimes(2);
      expect(url.revokeObjectURL).toHaveBeenCalledTimes(1);
    });
    it("should not call createObjectURL and revokeObjectURL for normal images", () => {
      wrapper = render(<Card {...DEFAULT_PROPS} />);

      expect(url.createObjectURL).not.toHaveBeenCalled();
      expect(url.revokeObjectURL).not.toHaveBeenCalled();

      const otherLink = Object.assign({}, DEFAULT_PROPS.link, {
        image: "https://other/image",
      });
      wrapper.rerender(
        <Card {...Object.assign({}, DEFAULT_PROPS, { link: otherLink })} />
      );

      expect(url.createObjectURL).not.toHaveBeenCalled();
      expect(url.revokeObjectURL).not.toHaveBeenCalled();
    });
  });
  describe("image loading", () => {
    let link;
    let triggerImage = {};
    let uniqueLink = 0;
    let ref;
    let originalImage;
    beforeEach(() => {
      originalImage = global.Image;
      global.Image = class {
        addEventListener(event, callback) {
          triggerImage[event] = () => Promise.resolve(callback());
        }
      };

      link = Object.assign({}, DEFAULT_PROPS.link);
      link.image += uniqueLink++;
      ref = React.createRef();
      wrapper = render(<Card {...DEFAULT_PROPS} link={link} ref={ref} />);
    });
    afterEach(() => {
      global.Image = originalImage;
    });
    it("should have a loaded preview image when the image is loaded", () => {
      expect(
        wrapper.container.querySelector(".card-preview-image")
      ).not.toHaveClass("loaded");

      act(() => {
        ref.current.setState({ imageLoaded: true });
      });

      expect(
        wrapper.container.querySelector(".card-preview-image")
      ).toHaveClass("loaded");
    });
    it("should start not loaded", () => {
      expect(ref.current.state.imageLoaded).toBe(false);
    });
    it("should be loaded after load", async () => {
      await act(async () => {
        await triggerImage.load();
      });

      expect(ref.current.state.imageLoaded).toBe(true);
    });
    it("should be not be loaded after error ", async () => {
      await act(async () => {
        await triggerImage.error();
      });

      expect(ref.current.state.imageLoaded).toBe(false);
    });
    it("should be not be loaded if image changes", async () => {
      await act(async () => {
        await triggerImage.load();
      });
      const otherLink = Object.assign({}, link, {
        image: "https://other/image",
      });

      wrapper.rerender(
        <Card
          {...Object.assign({}, DEFAULT_PROPS, { link: otherLink })}
          ref={ref}
        />
      );

      expect(ref.current.state.imageLoaded).toBe(false);
    });
  });
  describe("placeholder=true", () => {
    let ref;
    beforeEach(() => {
      ref = React.createRef();
      wrapper = render(<Card placeholder={true} ref={ref} />);
    });
    it("should render when placeholder=true", () => {
      expect(
        wrapper.container.querySelector(".card-outer")
      ).toBeInTheDocument();
    });
    it("should add a placeholder class to the outer element", () => {
      expect(wrapper.container.querySelector(".card-outer")).toHaveClass(
        "placeholder"
      );
    });
    it("should not have a context menu button or LinkMenu", () => {
      expect(
        wrapper.container.querySelector(".context-menu-button")
      ).not.toBeInTheDocument();
      expect(
        wrapper.container.querySelector(".context-menu")
      ).not.toBeInTheDocument();
    });
    it("should not call onLinkClick when the link is clicked", () => {
      const spy = jest.spyOn(ref.current, "onLinkClick");
      const card = wrapper.container.querySelector(".card");
      fireEvent.click(card);
      expect(spy).not.toHaveBeenCalled();
    });
  });
  describe("#trackClick", () => {
    it("should call dispatch when the link is clicked with the right data", () => {
      const card = wrapper.container.querySelector(".card");
      const event = {
        altKey: true,
        button: 0,
        ctrlKey: true,
        metaKey: true,
        shiftKey: true,
      };
      fireEvent.click(card, event);
      expect(DEFAULT_PROPS.dispatch).toHaveBeenCalledTimes(3);

      const [[firstAction], [secondAction], [thirdAction]] =
        DEFAULT_PROPS.dispatch.mock.calls;

      // first dispatch call is the AlsoToMain message which will open a link in a window, and send some event data
      expect(firstAction.type).toEqual(at.OPEN_LINK);
      expect(firstAction.data.event).toEqual(event);

      // second dispatch call is a UserEvent action for telemetry
      expect(secondAction.type).toEqual(at.TELEMETRY_USER_EVENT);
      expect(secondAction).toEqual(
        ac.UserEvent({
          event: "CLICK",
          source: DEFAULT_PROPS.eventSource,
          action_position: DEFAULT_PROPS.index,
        })
      );

      // third dispatch call is to send impression stats
      expect(thirdAction).toEqual(
        ac.ImpressionStats({
          source: DEFAULT_PROPS.eventSource,
          click: 0,
          tiles: [{ id: DEFAULT_PROPS.link.guid, pos: DEFAULT_PROPS.index }],
        })
      );
    });
    it("should provide card_type to telemetry info if type is not history", () => {
      const link = Object.assign({}, DEFAULT_PROPS.link);
      link.type = "bookmark";
      wrapper = render(
        <Card {...Object.assign({}, DEFAULT_PROPS, { link })} />
      );
      const card = wrapper.container.querySelector(".card");
      const event = {
        altKey: "1",
        button: 0,
        ctrlKey: "3",
        metaKey: "4",
        shiftKey: "5",
      };

      fireEvent.click(card, event);

      const [, [secondAction]] = DEFAULT_PROPS.dispatch.mock.calls;
      expect(secondAction.type).toEqual(at.TELEMETRY_USER_EVENT);
      expect(secondAction).toEqual(
        ac.UserEvent({
          event: "CLICK",
          source: DEFAULT_PROPS.eventSource,
          action_position: DEFAULT_PROPS.index,
          value: { card_type: link.type },
        })
      );
    });
    it("should notify Web Extensions with WEBEXT_CLICK if props.isWebExtension is true", () => {
      wrapper = mountCardWithProps(
        Object.assign({}, DEFAULT_PROPS, {
          isWebExtension: true,
          eventSource: "MyExtension",
          index: 3,
        })
      );
      const card = wrapper.container.querySelector(".card");
      fireEvent.click(card);
      expect(DEFAULT_PROPS.dispatch).toHaveBeenCalledWith(
        ac.WebExtEvent(at.WEBEXT_CLICK, {
          source: "MyExtension",
          url: DEFAULT_PROPS.link.url,
          action_position: 3,
        })
      );
    });
  });
});

describe("<PlaceholderCard />", () => {
  it("should render a Card with placeholder=true", () => {
    const { container } = render(
      <WrapWithProvider>
        <PlaceholderCard />
      </WrapWithProvider>
    );
    expect(container.querySelector(".card-outer")).toHaveClass("placeholder");
  });
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { render, fireEvent, act } from "@testing-library/react";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import React from "react";
import { TopSiteForm } from "content-src/components/TopSites/TopSiteForm";

describe("<TopSiteForm>", () => {
  let container;
  let rerender;
  let currentProps;
  let ref;
  let dispatch;
  let onClose;

  // Mirrors the legacy mount() helper. Renders the unconnected TopSiteForm with
  // spy dispatch/onClose props and keeps a ref to the class instance so the
  // white-box state/method assertions from the Enzyme test can be preserved.
  function testSetup(props = {}) {
    dispatch = jest.fn();
    onClose = jest.fn();
    ref = React.createRef();
    currentProps = Object.assign({}, { onClose, dispatch }, props);
    ({ container, rerender } = render(
      <TopSiteForm {...currentProps} ref={ref} />
    ));
  }

  // Enzyme wrapper.setState -> drive the instance's state directly (white-box).
  function setState(newState) {
    act(() => {
      ref.current.setState(newState);
    });
  }

  // Enzyme wrapper.setProps -> re-render with merged props (Enzyme merges too).
  function setProps(newProps) {
    currentProps = { ...currentProps, ...newProps };
    rerender(<TopSiteForm {...currentProps} ref={ref} />);
  }

  // Enzyme wrapper.instance().validateForm() may setState, so run it in act().
  function callValidateForm() {
    let result;
    act(() => {
      result = ref.current.validateForm();
    });
    return result;
  }

  // The custom screenshot url input is the third text input, rendered after the
  // label and url inputs once the custom screenshot form is open.
  function getScreenshotInput(formContainer) {
    return formContainer.querySelectorAll("input")[2];
  }

  function changeValue(input, value) {
    fireEvent.change(input, { target: { value } });
  }

  describe("validateForm", () => {
    beforeEach(() => testSetup({ site: { url: "https://foo" } }));

    it("should return true for a correct URL", () => {
      setState({ url: "foo" });

      expect(callValidateForm()).toBe(true);
    });

    it("should return false for a incorrect URL", () => {
      setState({ url: " " });

      expect(callValidateForm()).toBeNull();
      expect(ref.current.state.validationError).toBe(true);
    });

    it("should return true for a correct custom screenshot URL", () => {
      setState({ customScreenshotUrl: "foo" });

      expect(callValidateForm()).toBe(true);
    });

    it("should return false for a incorrect custom screenshot URL", () => {
      setState({ customScreenshotUrl: " " });

      expect(callValidateForm()).toBeNull();
    });

    it("should return true for an empty custom screenshot URL", () => {
      setState({ customScreenshotUrl: "" });

      expect(callValidateForm()).toBe(true);
    });

    it("should return false for file: protocol", () => {
      setState({ customScreenshotUrl: "file:///C:/Users/foo" });

      expect(callValidateForm()).toBe(false);
    });
  });

  describe("#previewButton", () => {
    beforeEach(() =>
      testSetup({
        site: { customScreenshotURL: "https://foo.com" },
        previewResponse: null,
      })
    );

    it("should render the preview button on invalid urls", () => {
      expect(
        container.querySelectorAll("#topsites-form-preview-button")
      ).toHaveLength(0);

      setState({ customScreenshotUrl: " " });

      expect(
        container.querySelectorAll("#topsites-form-preview-button")
      ).toHaveLength(1);
    });

    it("should render the preview button when input value updated", () => {
      expect(
        container.querySelectorAll("#topsites-form-preview-button")
      ).toHaveLength(0);

      setState({
        customScreenshotUrl: "https://baz.com",
        screenshotPreview: null,
      });

      expect(
        container.querySelectorAll("#topsites-form-preview-button")
      ).toHaveLength(1);
    });
  });

  describe("preview request", () => {
    beforeEach(() => {
      testSetup({
        site: {
          customScreenshotURL: "https://foo.com",
          url: "https://foo.com",
        },
        previewResponse: null,
      });
    });

    it("shouldn't dispatch a request for invalid urls", () => {
      setState({ customScreenshotUrl: " ", url: "foo" });

      fireEvent.click(container.querySelector("#topsites-form-preview-button"));

      expect(dispatch).not.toHaveBeenCalled();
    });

    it("should dispatch a PREVIEW_REQUEST", () => {
      setState({ customScreenshotUrl: "https://screenshot" });
      fireEvent.click(container.querySelector("#topsites-form-preview-button"));

      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch).toHaveBeenCalledWith(
        ac.AlsoToMain({
          type: at.PREVIEW_REQUEST,
          data: { url: "https://screenshot" },
        })
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({
          event: "PREVIEW_REQUEST",
          source: "TOP_SITES",
        })
      );
    });
  });

  describe("#TopSiteLink", () => {
    beforeEach(() => {
      testSetup();
    });

    it("should display a TopSiteLink preview", () => {
      expect(container.querySelectorAll(".top-site-outer")).toHaveLength(1);
    });

    it("should display an icon for tippyTop sites", () => {
      setProps({ site: { tippyTopIcon: "bar" } });

      expect(
        container.querySelector(".top-site-icon").style.backgroundImage
      ).toBe("url(bar)");
    });

    it("should not display a preview screenshot", () => {
      setProps({ previewResponse: "foo", previewUrl: "foo" });

      expect(container.querySelectorAll(".screenshot")).toHaveLength(0);
    });

    it("should not render any icon on error", () => {
      setProps({ previewResponse: "" });

      expect(container.querySelectorAll(".top-site-icon")).toHaveLength(0);
    });

    it("should render the search icon when searchTopSite is true", () => {
      setProps({ site: { tippyTopIcon: "bar", searchTopSite: true } });

      expect(container.querySelector(".rich-icon").style.backgroundImage).toBe(
        "url(bar)"
      );
      expect(container.querySelector(".search-topsite")).toBeInTheDocument();
    });
  });

  describe("#addMode", () => {
    beforeEach(() => testSetup());

    it("should render the component", () => {
      expect(container.querySelector(".topsite-form")).toBeInTheDocument();
    });
    it("should have the correct header", () => {
      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-add-shortcut-header"]'
        )
      ).toHaveLength(1);
    });
    it("should have the correct button text", () => {
      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-save-button"]'
        )
      ).toHaveLength(0);
      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-add-button"]'
        )
      ).toHaveLength(1);
    });
    it("should not render a preview button", () => {
      expect(
        container.querySelectorAll(".custom-image-input-container")
      ).toHaveLength(0);
    });
    it("should call onClose if Cancel button is clicked", () => {
      fireEvent.click(container.querySelector("#topsites-form-cancel-button"));
      expect(onClose).toHaveBeenCalledTimes(1);
    });
    it("should set validationError if url is empty", () => {
      expect(ref.current.state.validationError).toBe(false);
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(ref.current.state.validationError).toBe(true);
    });
    it("should set validationError if url is invalid", () => {
      setState({ url: "not valid" });
      expect(ref.current.state.validationError).toBe(false);
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(ref.current.state.validationError).toBe(true);
    });
    it("should call onClose and dispatch with right args if URL is valid", () => {
      setState({ url: "https://valid.com", label: "a label" });
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(onClose).toHaveBeenCalledTimes(1);
      expect(dispatch).toHaveBeenCalledWith(
        ac.AlsoToMain({
          type: at.TOP_SITES_PIN,
          data: {
            site: { label: "a label", url: "https://valid.com" },
            index: -1,
          },
        })
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({
          source: "TOP_SITES",
          event: "TOP_SITES_ADD",
          action_position: -1,
        })
      );
    });
    it("should not pass empty string label in dispatch data", () => {
      setState({ url: "https://valid.com", label: "" });
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(dispatch).toHaveBeenCalledWith(
        ac.AlsoToMain({
          type: at.TOP_SITES_PIN,
          data: { site: { url: "https://valid.com" }, index: -1 },
        })
      );
    });
    it("should open the custom screenshot input", () => {
      expect(ref.current.state.showCustomScreenshotForm).toBe(false);

      fireEvent.click(container.querySelector(".enable-custom-image-input"));

      expect(ref.current.state.showCustomScreenshotForm).toBe(true);
    });
    it("should swap the custom image link for the input when opened", () => {
      expect(
        container.querySelectorAll(".custom-image-input-container")
      ).toHaveLength(0);
      expect(
        container.querySelectorAll(".enable-custom-image-input")
      ).toHaveLength(1);

      fireEvent.click(container.querySelector(".enable-custom-image-input"));

      expect(
        container.querySelectorAll(".custom-image-input-container")
      ).toHaveLength(1);
      expect(
        container.querySelectorAll(".enable-custom-image-input")
      ).toHaveLength(0);
    });
  });

  describe("edit existing Topsite", () => {
    beforeEach(() =>
      testSetup({
        site: {
          url: "https://foo.bar",
          label: "baz",
          customScreenshotURL: "https://foo",
        },
        index: 7,
      })
    );

    it("should render the component", () => {
      expect(container.querySelector(".topsite-form")).toBeInTheDocument();
    });
    it("should have the correct header", () => {
      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-edit-shortcut-header"]'
        )
      ).toHaveLength(1);
    });
    it("should have the correct button text", () => {
      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-add-button"]'
        )
      ).toHaveLength(0);
      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-save-button"]'
        )
      ).toHaveLength(1);
    });
    it("should call onClose if Cancel button is clicked", () => {
      fireEvent.click(container.querySelector("#topsites-form-cancel-button"));
      expect(onClose).toHaveBeenCalledTimes(1);
    });
    it("should show error and not call onClose or dispatch if URL is empty", () => {
      setState({ url: "" });
      expect(ref.current.state.validationError).toBe(false);
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(ref.current.state.validationError).toBe(true);
      expect(onClose).not.toHaveBeenCalled();
      expect(dispatch).not.toHaveBeenCalled();
    });
    it("should show error and not call onClose or dispatch if URL is invalid", () => {
      setState({ url: "not valid" });
      expect(ref.current.state.validationError).toBe(false);
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(ref.current.state.validationError).toBe(true);
      expect(onClose).not.toHaveBeenCalled();
      expect(dispatch).not.toHaveBeenCalled();
    });
    it("should call onClose and dispatch with right args if URL is valid", () => {
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(onClose).toHaveBeenCalledTimes(1);
      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch).toHaveBeenCalledWith(
        ac.AlsoToMain({
          type: at.TOP_SITES_PIN,
          data: {
            site: {
              label: "baz",
              url: "https://foo.bar",
              customScreenshotURL: "https://foo",
            },
            index: 7,
          },
        })
      );
      expect(dispatch).toHaveBeenCalledWith(
        ac.UserEvent({
          source: "TOP_SITES",
          event: "TOP_SITES_EDIT",
          action_position: 7,
          has_title_changed: false,
          has_url_changed: false,
        })
      );
    });
    it("should set customScreenshotURL to null if it was removed", () => {
      setState({ customScreenshotUrl: "" });

      fireEvent.click(container.querySelector("#topsites-form-save-button"));

      expect(dispatch).toHaveBeenCalledWith(
        ac.AlsoToMain({
          type: at.TOP_SITES_PIN,
          data: {
            site: {
              label: "baz",
              url: "https://foo.bar",
              customScreenshotURL: null,
            },
            index: 7,
          },
        })
      );
    });
    it("should call onClose and dispatch with right args if URL is valid (negative index)", () => {
      setProps({ index: -1 });
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(onClose).toHaveBeenCalledTimes(1);
      expect(dispatch).toHaveBeenCalledTimes(2);
      expect(dispatch).toHaveBeenCalledWith(
        ac.AlsoToMain({
          type: at.TOP_SITES_PIN,
          data: {
            site: {
              label: "baz",
              url: "https://foo.bar",
              customScreenshotURL: "https://foo",
            },
            index: -1,
          },
        })
      );
    });
    it("should not pass empty string label in dispatch data", () => {
      setState({ label: "" });
      fireEvent.click(container.querySelector("#topsites-form-save-button"));
      expect(dispatch).toHaveBeenCalledWith(
        ac.AlsoToMain({
          type: at.TOP_SITES_PIN,
          data: {
            site: {
              url: "https://foo.bar",
              customScreenshotURL: "https://foo",
            },
            index: 7,
          },
        })
      );
    });
    it("should render the save button if custom screenshot request finished", () => {
      setState({
        customScreenshotUrl: "https://foo",
        screenshotPreview: "custom",
      });
      expect(
        container.querySelectorAll("#topsites-form-preview-button")
      ).toHaveLength(0);
      expect(
        container.querySelectorAll(
          'moz-button[data-l10n-id="newtab-topsites-save-button"]'
        )
      ).toHaveLength(1);
    });
    it("should render the save button if custom screenshot url was cleared", () => {
      setState({ customScreenshotUrl: "" });
      setProps({ site: { customScreenshotURL: "foo" } });
      expect(
        container.querySelectorAll("#topsites-form-preview-button")
      ).toHaveLength(0);
      expect(
        container.querySelectorAll(
          'moz-button[data-l10n-id="newtab-topsites-save-button"]'
        )
      ).toHaveLength(1);
    });
    it("should render the save button (not preview) when the screenshot url resolves to the existing one", () => {
      // Re-entering the existing custom screenshot URL leaves it unchanged, so
      // no preview is required and the save button stays.
      changeValue(getScreenshotInput(container), "https://foo");

      expect(
        container.querySelectorAll("#topsites-form-preview-button")
      ).toHaveLength(0);
      expect(
        container.querySelector("#topsites-form-save-button")
      ).toHaveAttribute("data-l10n-id", "newtab-topsites-save-button");
    });
  });

  describe("#previewMode", () => {
    beforeEach(() => testSetup({ previewResponse: null }));

    it("should transition from save to preview", () => {
      setProps({
        site: { url: "https://foo.bar", customScreenshotURL: "baz" },
        index: 7,
      });

      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-save-button"]'
        )
      ).toHaveLength(1);

      setState({ customScreenshotUrl: "foo" });

      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-preview-button"]'
        )
      ).toHaveLength(1);
    });

    it("should transition from add to preview", () => {
      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-add-button"]'
        )
      ).toHaveLength(1);

      setState({ customScreenshotUrl: "foo" });

      expect(
        container.querySelectorAll(
          '[data-l10n-id="newtab-topsites-preview-button"]'
        )
      ).toHaveLength(1);
    });
  });

  describe("#validateUrl", () => {
    it("should properly validate URLs", () => {
      testSetup();
      expect(ref.current.validateUrl("mozilla.org")).toBeTruthy();
      expect(ref.current.validateUrl("https://mozilla.org")).toBeTruthy();
      // eslint-disable-next-line sdl/no-insecure-url
      expect(ref.current.validateUrl("http://mozilla.org")).toBeTruthy();
      expect(
        ref.current.validateUrl(
          "https://mozilla.invisionapp.com/d/main/#/projects/prototypes"
        )
      ).toBeTruthy();
      expect(ref.current.validateUrl("httpfoobar")).toBeTruthy();
      expect(ref.current.validateUrl("httpsfoo.bar")).toBeTruthy();
      expect(ref.current.validateUrl("mozilla org")).toBeNull();
      expect(ref.current.validateUrl("")).toBeNull();
    });
  });

  describe("#cleanUrl", () => {
    it("should properly prepend https:// to URLs when required", () => {
      testSetup();
      expect(ref.current.cleanUrl("mozilla.org")).toBe("https://mozilla.org");
      expect(ref.current.cleanUrl("https.org")).toBe("https://https.org");
      expect(ref.current.cleanUrl("httpcom")).toBe("https://httpcom");
      expect(
        // eslint-disable-next-line sdl/no-insecure-url
        ref.current.cleanUrl("http://mozilla.org")
        // eslint-disable-next-line sdl/no-insecure-url
      ).toBe("http://mozilla.org");
      expect(ref.current.cleanUrl("https://firefox.com")).toBe(
        "https://firefox.com"
      );
    });
  });
});

import React from "react";
import { mount } from "enzyme";
import { SingleSelect } from "content-src/components/SingleSelect";

describe("SingleSelect component", () => {
  let sandbox;
  let wrapper;
  let handleAction;
  let setActiveSingleSelectSelection;

  const SINGLE_SELECT_ID = "single-select-0";
  const NOTICE_URL = "https://example.com/details";
  const NOTICE_TEXT = [
    { raw: "Notice text." },
    " ",
    { raw: "Get more details", href: NOTICE_URL },
  ];

  const makeContent = (tilesOverrides = {}) => ({
    tiles: {
      type: "single-select",
      selected: "plain",
      data: [
        { id: "plain", label: { raw: "Plain" }, action: {} },
        {
          id: "with-notice",
          label: { raw: "With notice" },
          action: {},
          selectedNotice: { text: NOTICE_TEXT },
        },
      ],
      ...tilesOverrides,
    },
  });

  const mountSingleSelect = (content, selectedId) =>
    mount(
      <SingleSelect
        content={content}
        handleAction={handleAction}
        setActiveSingleSelectSelection={setActiveSingleSelectSelection}
        activeSingleSelectSelections={
          selectedId ? { [SINGLE_SELECT_ID]: selectedId } : {}
        }
        singleSelectId={SINGLE_SELECT_ID}
        messageId="TEST_MESSAGE"
      />
    );

  beforeEach(() => {
    sandbox = sinon.createSandbox();
    handleAction = sandbox.stub();
    setActiveSingleSelectSelection = sandbox.stub();
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = null;
    sandbox.restore();
  });

  it("should give every single-select radio the same group name", () => {
    wrapper = mountSingleSelect(makeContent(), "plain");

    const names = wrapper
      .find("input[type='radio']")
      .map(input => input.prop("name"));
    assert.deepEqual(names, [
      `select-item-${SINGLE_SELECT_ID}`,
      `select-item-${SINGLE_SELECT_ID}`,
    ]);
  });

  describe("selectedNotice", () => {
    it("should not render a notice region when no tile has a selectedNotice", () => {
      const content = makeContent({
        data: [
          { id: "a", label: { raw: "A" }, action: {} },
          { id: "b", label: { raw: "B" }, action: {} },
        ],
      });
      wrapper = mountSingleSelect(content, "a");

      assert.isFalse(wrapper.find(".single-select-notice-region").exists());
    });

    it("should render an empty live region while a tile without a notice is selected", () => {
      wrapper = mountSingleSelect(makeContent(), "plain");

      const region = wrapper.find(".single-select-notice-region");
      assert.isTrue(region.exists());
      assert.strictEqual(region.prop("role"), "status");
      assert.isFalse(wrapper.find(".single-select-notice").exists());
    });

    it("should render the notice while its tile is selected", () => {
      wrapper = mountSingleSelect(makeContent(), "with-notice");

      assert.isTrue(wrapper.find(".single-select-notice").exists());
      assert.deepEqual(wrapper.find("LinkParagraph").prop("text_content"), {
        text: NOTICE_TEXT,
      });
    });

    it("should show and hide the notice as the selection changes", () => {
      wrapper = mountSingleSelect(makeContent(), "plain");
      assert.isFalse(wrapper.find(".single-select-notice").exists());

      wrapper.setProps({
        activeSingleSelectSelections: { [SINGLE_SELECT_ID]: "with-notice" },
      });
      assert.isTrue(wrapper.find(".single-select-notice").exists());

      wrapper.setProps({
        activeSingleSelectSelections: { [SINGLE_SELECT_ID]: "plain" },
      });
      assert.isFalse(wrapper.find(".single-select-notice").exists());
      assert.isTrue(wrapper.find(".single-select-notice-region").exists());
    });

    it("should use the default info icon when no iconURL is given", () => {
      wrapper = mountSingleSelect(makeContent(), "with-notice");

      assert.strictEqual(
        wrapper.find(".single-select-notice-icon").prop("src"),
        "chrome://global/skin/icons/info-filled.svg"
      );
    });

    it("should use iconURL when provided", () => {
      const content = makeContent();
      content.tiles.data[1].selectedNotice.iconURL = "chrome://test/icon.svg";
      wrapper = mountSingleSelect(content, "with-notice");

      assert.strictEqual(
        wrapper.find(".single-select-notice-icon").prop("src"),
        "chrome://test/icon.svg"
      );
    });

    it("should open the link's href through handleAction when clicked", () => {
      wrapper = mountSingleSelect(makeContent(), "with-notice");

      const link = wrapper.find(".single-select-notice a.text-link");
      assert.strictEqual(link.prop("href"), NOTICE_URL);

      link.simulate("click");

      assert.calledOnce(handleAction);
      assert.deepEqual(handleAction.firstCall.args[1], {
        type: "OPEN_URL",
        data: { args: NOTICE_URL, where: "tab" },
      });
    });

    it("should not render a notice for non single-select tiles", () => {
      const content = makeContent({ type: "theme" });
      wrapper = mountSingleSelect(content, "with-notice");

      assert.isFalse(wrapper.find(".single-select-notice-region").exists());
    });
  });
});

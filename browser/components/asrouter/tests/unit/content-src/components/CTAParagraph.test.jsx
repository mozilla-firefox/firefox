import React from "react";
import { shallow } from "enzyme";
import { CTAParagraph } from "content-src/components/CTAParagraph";

describe("CTAParagraph component", () => {
  let sandbox;
  let wrapper;
  let handleAction;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
    handleAction = sandbox.stub();
    wrapper = shallow(
      <CTAParagraph
        content={{
          text: {
            raw: "Link Text",
            string_name: "Test Name",
          },
        }}
        handleAction={handleAction}
      />
    );
  });

  afterEach(() => {
    sandbox.restore();
  });

  it("should render CTAParagraph component", () => {
    assert.ok(wrapper.exists());
  });

  it("should render CTAParagraph component if only CTA text is passed", () => {
    wrapper.setProps({ content: { text: "CTA Text" } });
    assert.ok(wrapper.exists());
  });

  it("should render CTAParagraph icon if one is passed", () => {
    wrapper.setProps({ content: { text: "CTA Text", icon: {} } });
    assert.ok(wrapper.exists());
    const infoIcon = wrapper.find(".cta-paragraph-icon");
    assert.ok(infoIcon.exists());
  });

  it("should call handleAction method when button is link is clicked", () => {
    const btnLink = wrapper.find(".cta-paragraph span");
    btnLink.simulate("click", { preventDefault() {} });
    assert.calledOnce(handleAction);
  });

  it("should call handleAction method when Enter or Space is pressed and released on the link", () => {
    const btnLink = wrapper.find(".cta-paragraph span");
    for (const key of ["Enter", " "]) {
      btnLink.simulate("keydown", { key });
      btnLink.simulate("keyup", { key, preventDefault() {} });
    }
    assert.calledTwice(handleAction);
  });

  it("should not call handleAction method when a key pressed elsewhere is released on the link", () => {
    const btnLink = wrapper.find(".cta-paragraph span");
    btnLink.simulate("keyup", { key: "Enter", preventDefault() {} });
    assert.notCalled(handleAction);
  });

  it("should not render CTAParagraph component if CTA text is not passed", () => {
    wrapper.setProps({ content: { text: null } });
    assert.ok(wrapper.isEmptyRender());
  });
});

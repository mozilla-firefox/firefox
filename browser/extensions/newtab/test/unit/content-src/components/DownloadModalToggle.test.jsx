import { DownloadModalToggle } from "content-src/components/DownloadModalToggle/DownloadModalToggle";
import React from "react";
import { mount } from "enzyme";

describe("<DownloadModalToggle>", () => {
  let wrapper;

  beforeEach(() => {
    wrapper = mount(<DownloadModalToggle />);
  });

  it("should render", () => {
    assert.ok(wrapper.exists());
    assert.ok(wrapper.find(".mobile-download-promo").exists());
  });
});

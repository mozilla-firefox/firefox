import React from "react";
import { mount } from "enzyme";
import { EmbeddedBackupRestore } from "content-src/components/EmbeddedBackupRestore";
import { GlobalOverrider } from "tests/unit/utils";

describe("EmbeddedBackupRestore component", () => {
  let wrapper;
  let globals;
  let sandbox;

  beforeEach(() => {
    sandbox = sinon.createSandbox();
    globals = new GlobalOverrider();
    globals.set({ AWSendToParent: sandbox.stub() });
    globals.set({
      AWSendToParent: sandbox.stub(),
      AWFindBackupsInWellKnownLocations: sandbox.stub().resolves({
        found: false,
        multipleBackupsFound: false,
        backupFileToRestore: null,
      }),
    });
  });

  afterEach(() => {
    sandbox.restore();
    globals.restore();
    if (wrapper) {
      wrapper.unmount();
    }
  });

  it("should render the restore-from-backup element", () => {
    wrapper = mount(<EmbeddedBackupRestore />);

    const restoreFromBackupElement = wrapper.find("restore-from-backup");
    assert.ok(
      restoreFromBackupElement.exists(),
      "restore-from-backup element should be rendered"
    );

    assert.equal(
      restoreFromBackupElement.prop("aboutWelcomeEmbedded"),
      "true",
      "aboutWelcomeEmbedded should be set to 'true'"
    );

    assert.equal(
      restoreFromBackupElement.prop("labelFontWeight"),
      "600",
      "labelFontWeight should be set to '600'"
    );
  });

  it("sets the layout attribute on restore-from-backup", () => {
    wrapper = mount(<EmbeddedBackupRestore layout="center-large" />);

    assert.equal(
      wrapper.find("restore-from-backup").getDOMNode().getAttribute("layout"),
      "center-large"
    );
  });

  it("calls AWFindBackupsInWellKnownLocations on mount", async () => {
    wrapper = mount(<EmbeddedBackupRestore />);

    // Ensure the effect runs before we assert.
    await new Promise(resolve => setTimeout(resolve, 0));

    assert.isTrue(
      window.AWFindBackupsInWellKnownLocations.calledOnce,
      "should call AWFindBackupsInWellKnownLocations exactly once on mount"
    );
  });

  it("renders skip button when provided and applies arrow icon class, and calls handleAction", async () => {
    const handleAction = sandbox.stub();
    const skipButton = {
      label: { raw: "Skip" },
      has_arrow_icon: true,
    };

    wrapper = mount(
      <EmbeddedBackupRestore
        handleAction={handleAction}
        skipButton={skipButton}
      />
    );

    const button = wrapper.find("#secondary_button");
    assert.ok(button.exists(), "skip button rendered");
    assert.isTrue(
      button.hasClass("secondary") && button.hasClass("arrow-icon"),
      "arrow icon class is applied when has_arrow_icon is true"
    );

    button.simulate("click");
    sinon.assert.calledOnce(handleAction);
  });

  it("updates skip button state based on BackupUI:RecoveryProgress events", async () => {
    const skipButton = { label: { string_id: "skip" } };
    wrapper = mount(<EmbeddedBackupRestore skipButton={skipButton} />);
    const node = wrapper.find("restore-from-backup").getDOMNode();

    node.dispatchEvent(
      new CustomEvent("BackupUI:RecoveryProgress", {
        detail: { recoveryInProgress: true },
      })
    );

    wrapper.update();
    let button = wrapper.find("#secondary_button");
    assert.isTrue(button.prop("disabled"));
    assert.equal(button.prop("aria-busy"), true);

    node.dispatchEvent(
      new CustomEvent("BackupUI:RecoveryProgress", {
        detail: { recoveryInProgress: false },
      })
    );

    wrapper.update();
    button = wrapper.find("#secondary_button");
    assert.isFalse(button.prop("disabled"));
    assert.isUndefined(button.prop("aria-busy"));
  });

  it("does not render skip button section when skipButton prop is missing", () => {
    wrapper = mount(<EmbeddedBackupRestore />);
    assert.isFalse(wrapper.find("#secondary_button").exists());
    assert.isFalse(wrapper.find(".secondary-cta").exists());
  });
});

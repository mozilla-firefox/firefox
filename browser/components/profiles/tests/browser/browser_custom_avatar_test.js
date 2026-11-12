/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const setup = async () => {
  await initGroupDatabase();
  let profile = SelectableProfileService.currentProfile;
  Assert.ok(profile, "Should have a profile now");

  return profile;
};

async function createAvatarFile(width, height) {
  const mockAvatarFilePath = await IOUtils.createUniqueFile(
    PathUtils.tempDir,
    "avatar.png"
  );

  // We need to create actual image data here because our circular avatar
  // processing code in profile-avatar-selector.mjs expects valid image data.
  // If we pass an empty file, the img.decode() call will fail with an EncodingError
  const canvas = new OffscreenCanvas(width, height);
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "red";
  ctx.fillRect(0, 0, width, height);

  const blob = await canvas.convertToBlob({ type: "image/png" });
  const buffer = await blob.arrayBuffer();
  await IOUtils.write(mockAvatarFilePath, new Uint8Array(buffer));

  const mockAvatarFile = Cc["@mozilla.org/file/local;1"].createInstance(
    Ci.nsIFile
  );
  mockAvatarFile.initWithPath(mockAvatarFilePath);

  return mockAvatarFile;
}

async function verifyCustomAvatarImage(
  avatarPath,
  expectedWidth,
  expectedHeight
) {
  // Verify the saved avatar is circular and corners are transparent
  const avatarData = await IOUtils.read(avatarPath);
  const avatarBlob = new Blob([avatarData], { type: "image/png" });

  const img = new Image();
  const loaded = new Promise(resolve => {
    img.addEventListener("load", resolve, { once: true });
  });
  img.src = URL.createObjectURL(avatarBlob);
  await loaded;

  const verifyCanvas = new OffscreenCanvas(img.width, img.height);
  const verifyCtx = verifyCanvas.getContext("2d");
  verifyCtx.drawImage(img, 0, 0);

  const topLeft = verifyCtx.getImageData(0, 0, 1, 1).data;
  const topRight = verifyCtx.getImageData(img.width - 1, 0, 1, 1).data;
  const bottomLeft = verifyCtx.getImageData(0, img.height - 1, 1, 1).data;
  const bottomRight = verifyCtx.getImageData(
    img.width - 1,
    img.height - 1,
    1,
    1
  ).data;
  const center = verifyCtx.getImageData(
    Math.floor(img.width / 2),
    Math.floor(img.height / 2),
    1,
    1
  ).data;

  Assert.equal(img.width, expectedWidth, "The image is the expected width.");
  Assert.equal(img.height, expectedHeight, "The image is the expected height.");

  Assert.equal(topLeft[3], 0, "Top left corner should be transparent");
  Assert.equal(topRight[3], 0, "Top right corner should be transparent");
  Assert.equal(bottomLeft[3], 0, "Bottom left corner should be transparent");
  Assert.equal(bottomRight[3], 0, "Bottom right corner should be transparent");

  Assert.equal(center[0], 255, "Center pixel should have red channel = 255");
  Assert.equal(center[1], 0, "Center pixel should have green channel = 0");
  Assert.equal(center[2], 0, "Center pixel should have blue channel = 0");
  Assert.equal(center[3], 255, "Center pixel should be opaque");

  URL.revokeObjectURL(img.src);
}

add_task(async function test_edit_profile_custom_avatar() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }
  await setup();

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => editProfileCard.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        await editProfileCard.updateComplete;
        await new Promise(resolve => content.requestAnimationFrame(resolve));

        EventUtils.synthesizeMouseAtCenter(
          editProfileCard.avatarSelectorButton,
          {},
          content
        );

        const avatarSelector = editProfileCard.avatarSelector;

        Assert.ok(
          ContentTaskUtils.isVisible(avatarSelector.dialog),
          "Should be showing the profile avatar selector"
        );

        avatarSelector.state = "custom";
        await avatarSelector.updateComplete;
      });
    }
  );
});

add_task(async function test_edit_profile_custom_avatar_upload() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }
  const profile = await setup();

  const avatarWidth = 100;
  const avatarHeight = 100;
  const mockAvatarFile = await createAvatarFile(avatarWidth, avatarHeight);

  const MockFilePicker = SpecialPowers.MockFilePicker;
  MockFilePicker.init();
  MockFilePicker.setFiles([mockAvatarFile]);
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  let curProfile = await SelectableProfileService.getProfile(profile.id);
  await curProfile.setAvatar("star");
  Assert.ok(
    !curProfile.hasCustomAvatar,
    "Current profile does not have a custom avatar"
  );

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      const customAvatarData = await SpecialPowers.spawn(
        browser,
        [],
        async () => {
          let editProfileCard =
            content.document.querySelector("edit-profile-card").wrappedJSObject;

          await ContentTaskUtils.waitForCondition(
            () => editProfileCard.initialized,
            "Waiting for edit-profile-card to be initialized"
          );

          await editProfileCard.updateComplete;
          await new Promise(resolve => content.requestAnimationFrame(resolve));

          EventUtils.synthesizeMouseAtCenter(
            editProfileCard.avatarSelectorButton,
            {},
            content
          );

          const avatarSelector = editProfileCard.avatarSelector;

          Assert.ok(
            ContentTaskUtils.isVisible(avatarSelector.dialog),
            "Should be showing the profile avatar selector"
          );

          EventUtils.synthesizeMouseAtCenter(
            avatarSelector.customTabButton,
            {},
            content
          );
          await avatarSelector.updateComplete;

          await ContentTaskUtils.waitForCondition(
            () => ContentTaskUtils.isVisible(avatarSelector.input),
            "Waiting for avatar selector input to be visible"
          );

          const inputReceived = new Promise(resolve =>
            avatarSelector.input.addEventListener(
              "input",
              event => {
                resolve(event.target.files[0].name);
              },
              { once: true }
            )
          );

          EventUtils.synthesizeMouseAtCenter(avatarSelector.input, {}, content);

          await inputReceived;

          await ContentTaskUtils.waitForCondition(
            () => ContentTaskUtils.isVisible(avatarSelector.saveButton),
            "Waiting for avatar selector save button to be visible"
          );

          await ContentTaskUtils.waitForCondition(
            () => avatarSelector.customAvatarImage?.complete,
            "Waiting for avatar selector image to load"
          );

          let region = avatarSelector.avatarRegion.dimensions;
          let cropClientHeight =
            avatarSelector.customAvatarCropArea.clientHeight;
          let cropClientWidth = avatarSelector.customAvatarCropArea.clientWidth;

          EventUtils.synthesizeMouseAtCenter(
            avatarSelector.saveButton,
            {},
            content
          );

          // Sometimes the async message takes a bit longer to arrive.
          await new Promise(resolve => content.setTimeout(resolve, 500));

          return { region, cropClientHeight, cropClientWidth };
        }
      );

      curProfile = await SelectableProfileService.getProfile(profile.id);
      Assert.ok(
        curProfile.hasCustomAvatar,
        "Current profile has a custom avatar image"
      );

      info(`customAvatarData: ${JSON.stringify(customAvatarData, null, 2)}`);
      let { region, cropClientHeight, cropClientWidth } = customAvatarData;
      const scale =
        avatarWidth <= avatarHeight
          ? avatarWidth / cropClientWidth
          : avatarHeight / cropClientHeight;

      const expectedWidth =
        2 * Math.round(region.radius * scale * window.devicePixelRatio);

      await verifyCustomAvatarImage(
        curProfile.getAvatarPath(),
        expectedWidth,
        expectedWidth
      );
    }
  );

  MockFilePicker.cleanup();
});

add_task(async function test_avatar_selector_tabs() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }
  await setup();

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => editProfileCard.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        await editProfileCard.updateComplete;
        await new Promise(resolve => content.requestAnimationFrame(resolve));

        EventUtils.synthesizeMouseAtCenter(
          editProfileCard.avatarSelectorButton,
          {},
          content
        );

        const avatarSelector = editProfileCard.avatarSelector;

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isVisible(avatarSelector.dialog),
          "Waiting for avatar selector to become visible"
        );
        await avatarSelector.updateComplete;

        // Wait for all Lit microtask queue updates to complete so we don't run into race condition issues
        await new Promise(resolve => content.requestAnimationFrame(resolve));

        const buttonGroup =
          avatarSelector.shadowRoot.querySelector(".button-group");
        Assert.ok(buttonGroup, "Tab section should exist");

        const iconTab = buttonGroup.querySelector(
          '[data-l10n-id="avatar-selector-icon-tab"]'
        );
        const customTab = buttonGroup.querySelector(
          '[data-l10n-id="avatar-selector-custom-tab"]'
        );
        Assert.ok(iconTab, "Icon tab should exist");
        Assert.ok(customTab, "Custom tab should exist");

        // Verify Icon is default
        Assert.equal(
          avatarSelector.view,
          "icon",
          "Icon should be the default tab"
        );
        Assert.equal(
          avatarSelector.iconTabButton.type,
          "primary",
          "Icon tab should be active by default"
        );
        Assert.equal(
          avatarSelector.customTabButton.type,
          "ghost",
          "Custom tab should be inactive by default"
        );

        info("Clicking custom tab");
        EventUtils.synthesizeMouseAtCenter(customTab, {}, content);
        await avatarSelector.updateComplete;

        Assert.equal(
          avatarSelector.view,
          "custom",
          "Should switch to custom tab"
        );
        Assert.equal(
          avatarSelector.customTabButton.type,
          "primary",
          "Custom tab should be active"
        );
        Assert.equal(
          avatarSelector.iconTabButton.type,
          "ghost",
          "Icon tab should be inactive"
        );

        info("Clicking icon tab");
        EventUtils.synthesizeMouseAtCenter(iconTab, {}, content);
        await avatarSelector.updateComplete;

        Assert.equal(
          avatarSelector.view,
          "icon",
          "Should switch back to icon tab"
        );
        Assert.equal(
          avatarSelector.iconTabButton.type,
          "primary",
          "Icon tab should be active"
        );
        Assert.equal(
          avatarSelector.customTabButton.type,
          "ghost",
          "Custom tab should be inactive"
        );
      });
    }
  );
});

add_task(async function test_edit_profile_custom_avatar_crop() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }
  const profile = await setup();

  const avatarWidth = 1000;
  const avatarHeight = 2000;

  const mockAvatarFile = await createAvatarFile(avatarWidth, avatarHeight);

  const MockFilePicker = SpecialPowers.MockFilePicker;
  MockFilePicker.init();
  MockFilePicker.setFiles([mockAvatarFile]);
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  let curProfile = await SelectableProfileService.getProfile(profile.id);
  await curProfile.setAvatar("star");
  Assert.ok(
    !curProfile.hasCustomAvatar,
    "Current profile does not have a custom avatar"
  );

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      const customAvatarData = await SpecialPowers.spawn(
        browser,
        [],
        async () => {
          let editProfileCard =
            content.document.querySelector("edit-profile-card").wrappedJSObject;

          await ContentTaskUtils.waitForCondition(
            () => editProfileCard.initialized,
            "Waiting for edit-profile-card to be initialized"
          );

          await editProfileCard.updateComplete;

          const avatarSelector = editProfileCard.avatarSelector;

          EventUtils.synthesizeMouseAtCenter(
            editProfileCard.avatarSelectorButton,
            {},
            content
          );

          Assert.ok(
            ContentTaskUtils.isVisible(avatarSelector.dialog),
            "Should be showing the profile avatar selector"
          );

          EventUtils.synthesizeMouseAtCenter(
            avatarSelector.customTabButton,
            {},
            content
          );
          await avatarSelector.updateComplete;

          await ContentTaskUtils.waitForCondition(
            () => ContentTaskUtils.isVisible(avatarSelector.input),
            "Waiting for avatar selector input to be visible"
          );

          const inputReceived = new Promise(resolve =>
            avatarSelector.input.addEventListener(
              "input",
              event => {
                resolve(event.target.files[0].name);
              },
              { once: true }
            )
          );

          EventUtils.synthesizeMouseAtCenter(avatarSelector.input, {}, content);

          await inputReceived;

          await ContentTaskUtils.waitForCondition(
            () => ContentTaskUtils.isVisible(avatarSelector.saveButton),
            "Waiting for avatar selector save button to be visible"
          );

          await ContentTaskUtils.waitForCondition(
            () => avatarSelector.customAvatarImage?.complete,
            "Waiting for avatar selector image to load"
          );

          // do cropping here
          for (let [target, diffX, diffY] of [
            [avatarSelector.topLeftMover, 20, 20],
            [avatarSelector.topRightMover, -20, 20],
            [avatarSelector.bottomLeftMover, 20, -20],
            [avatarSelector.bottomRightMover, -20, -20],
          ]) {
            let start = target.getBoundingClientRect();

            let startCenterX = start.x + (start.width + diffX) / 2;
            let startCenterY = start.y + (start.height + diffY) / 2;
            // Start dragging region
            EventUtils.synthesizeMouseAtPoint(
              startCenterX,
              startCenterY,
              { type: "mousedown" },
              content
            );

            await ContentTaskUtils.waitForCondition(
              () => avatarSelector.state === "resizing",
              "Waiting for avatar selector state to be resizing"
            );

            // Drag the mover element
            EventUtils.synthesizeMouseAtPoint(
              startCenterX + diffX,
              startCenterY + diffY,
              { type: "mousemove" },
              content
            );

            // Stop dragging
            EventUtils.synthesizeMouseAtPoint(
              startCenterX + diffX,
              startCenterY + diffY,
              { type: "mouseup" },
              content
            );

            await ContentTaskUtils.waitForCondition(
              () => avatarSelector.state === "selected",
              "Waiting for avatar selector state to be selected"
            );
          }

          const tl = avatarSelector.topLeftMover.getBoundingClientRect();
          const br = avatarSelector.bottomRightMover.getBoundingClientRect();
          const startX = tl.x + tl.width / 2;
          const startY = tl.y + tl.height / 2;
          const endX = br.x - 4;
          const endY = br.y - 4;

          EventUtils.synthesizeMouseAtPoint(
            startX,
            startY,
            { type: "mousedown" },
            content
          );

          await ContentTaskUtils.waitForCondition(
            () => avatarSelector.state === "resizing",
            "Waiting for avatar selector state to be resizing"
          );

          EventUtils.synthesizeMouseAtPoint(
            endX,
            endY,
            { type: "mousemove" },
            content
          );

          EventUtils.synthesizeMouseAtPoint(
            endX,
            endY,
            { type: "mouseup" },
            content
          );

          await ContentTaskUtils.waitForCondition(
            () => avatarSelector.state === "selected",
            "Waiting for avatar selector state to be selected"
          );

          Assert.greaterOrEqual(
            avatarSelector.avatarRegion.dimensions.width,
            48,
            "drag path clamps selection to minimum size"
          );

          let region = avatarSelector.avatarRegion.dimensions;
          let cropClientHeight =
            avatarSelector.customAvatarCropArea.clientHeight;
          let cropClientWidth = avatarSelector.customAvatarCropArea.clientWidth;

          EventUtils.synthesizeMouseAtCenter(
            avatarSelector.saveButton,
            {},
            content
          );

          // Sometimes the async message takes a bit longer to arrive.
          await new Promise(resolve => content.setTimeout(resolve, 500));

          return { region, cropClientHeight, cropClientWidth };
        }
      );

      curProfile = await SelectableProfileService.getProfile(profile.id);
      Assert.ok(
        curProfile.hasCustomAvatar,
        "Current profile has a custom avatar image"
      );

      let { region, cropClientHeight, cropClientWidth } = customAvatarData;
      const scale =
        avatarWidth <= avatarHeight
          ? avatarWidth / cropClientWidth
          : avatarHeight / cropClientHeight;

      const expectedWidth =
        2 * Math.round(region.radius * scale * window.devicePixelRatio);

      await verifyCustomAvatarImage(
        curProfile.getAvatarPath(),
        expectedWidth,
        expectedWidth
      );
    }
  );

  MockFilePicker.cleanup();
});

add_task(async function test_edit_profile_custom_avatar_keyboard_crop() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }
  const profile = await setup();

  const avatarWidth = 100;
  const avatarHeight = 100;

  const mockAvatarFile = await createAvatarFile(avatarWidth, avatarHeight);

  const MockFilePicker = SpecialPowers.MockFilePicker;
  MockFilePicker.init();
  MockFilePicker.setFiles([mockAvatarFile]);
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  let curProfile = await SelectableProfileService.getProfile(profile.id);
  await curProfile.setAvatar("star");
  Assert.ok(
    !curProfile.hasCustomAvatar,
    "Current profile does not have a custom avatar"
  );

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      const customAvatarData = await SpecialPowers.spawn(
        browser,
        [],
        async () => {
          function assertAvatarRegionDimensions(
            actualDimensions,
            expectedDimensions
          ) {
            is(
              actualDimensions.top,
              expectedDimensions.top,
              "Top dimension is correct"
            );
            is(
              actualDimensions.left,
              expectedDimensions.left,
              "Left dimension is correct"
            );
            is(
              actualDimensions.bottom,
              expectedDimensions.bottom,
              "Bottom dimension is correct"
            );
            is(
              actualDimensions.right,
              expectedDimensions.right,
              "Right dimension is correct"
            );
          }

          let editProfileCard =
            content.document.querySelector("edit-profile-card").wrappedJSObject;

          await ContentTaskUtils.waitForCondition(
            () => editProfileCard.initialized,
            "Waiting for edit-profile-card to be initialized"
          );

          await editProfileCard.updateComplete;

          const avatarSelector = editProfileCard.avatarSelector;

          EventUtils.synthesizeMouseAtCenter(
            editProfileCard.avatarSelectorButton,
            {},
            content
          );

          Assert.ok(
            ContentTaskUtils.isVisible(avatarSelector.dialog),
            "Should be showing the profile avatar selector"
          );

          EventUtils.synthesizeMouseAtCenter(
            avatarSelector.customTabButton,
            {},
            content
          );
          await avatarSelector.updateComplete;

          await ContentTaskUtils.waitForCondition(
            () => ContentTaskUtils.isVisible(avatarSelector.input),
            "Waiting for avatar selector input to be visible"
          );

          const inputReceived = new Promise(resolve =>
            avatarSelector.input.addEventListener(
              "input",
              event => {
                resolve(event.target.files[0].name);
              },
              { once: true }
            )
          );

          EventUtils.synthesizeMouseAtCenter(avatarSelector.input, {}, content);

          await inputReceived;

          await ContentTaskUtils.waitForCondition(
            () => ContentTaskUtils.isVisible(avatarSelector.saveButton),
            "Waiting for avatar selector save button to be visible"
          );

          await ContentTaskUtils.waitForCondition(
            () => avatarSelector.customAvatarImage?.complete,
            "Waiting for avatar selector image to load"
          );

          await avatarSelector.updateComplete;

          const initialDimensions = avatarSelector.avatarRegion.dimensions;

          // Move top left mover
          avatarSelector.topLeftMover.focus();
          EventUtils.synthesizeKey("ArrowRight", { repeat: 20 }, content);
          EventUtils.synthesizeKey("ArrowDown", { repeat: 20 }, content);
          EventUtils.synthesizeKey("ArrowLeft", { repeat: 10 }, content);
          EventUtils.synthesizeKey("ArrowUp", { repeat: 10 }, content);

          // Move top right mover
          avatarSelector.topRightMover.focus();
          EventUtils.synthesizeKey("ArrowLeft", { repeat: 20 }, content);
          EventUtils.synthesizeKey("ArrowDown", { repeat: 20 }, content);
          EventUtils.synthesizeKey("ArrowRight", { repeat: 10 }, content);
          EventUtils.synthesizeKey("ArrowUp", { repeat: 10 }, content);

          // Move bottom right mover
          avatarSelector.bottomRightMover.focus();
          EventUtils.synthesizeKey("ArrowLeft", { repeat: 20 }, content);
          EventUtils.synthesizeKey("ArrowUp", { repeat: 20 }, content);
          EventUtils.synthesizeKey("ArrowRight", { repeat: 10 }, content);
          EventUtils.synthesizeKey("ArrowDown", { repeat: 10 }, content);

          // Move bottom left mover
          avatarSelector.bottomLeftMover.focus();
          EventUtils.synthesizeKey("ArrowRight", { repeat: 20 }, content);
          EventUtils.synthesizeKey("ArrowUp", { repeat: 20 }, content);
          EventUtils.synthesizeKey("ArrowLeft", { repeat: 10 }, content);
          EventUtils.synthesizeKey("ArrowDown", { repeat: 10 }, content);

          const expected = {
            left: initialDimensions.left + 40,
            top: initialDimensions.top + 40,
            right: initialDimensions.right - 40,
            bottom: initialDimensions.bottom - 40,
          };
          // Check that initial position is correct
          assertAvatarRegionDimensions(
            avatarSelector.avatarRegion.dimensions,
            expected
          );

          avatarSelector.highlight.focus();

          EventUtils.synthesizeKey("ArrowRight", { repeat: 20 }, content);
          expected.left += 20;
          expected.right += 20;
          assertAvatarRegionDimensions(
            avatarSelector.avatarRegion.dimensions,
            expected
          );

          EventUtils.synthesizeKey("ArrowUp", { repeat: 20 }, content);
          expected.top -= 20;
          expected.bottom -= 20;
          assertAvatarRegionDimensions(
            avatarSelector.avatarRegion.dimensions,
            expected
          );

          EventUtils.synthesizeKey("ArrowLeft", { repeat: 20 }, content);
          expected.left -= 20;
          expected.right -= 20;
          assertAvatarRegionDimensions(
            avatarSelector.avatarRegion.dimensions,
            expected
          );

          EventUtils.synthesizeKey("ArrowDown", { repeat: 20 }, content);
          expected.top += 20;
          expected.bottom += 20;
          assertAvatarRegionDimensions(
            avatarSelector.avatarRegion.dimensions,
            expected
          );

          // Check that the avatar region is clamped to the minimum size
          avatarSelector.topLeftMover.focus();
          EventUtils.synthesizeKey("ArrowRight", { repeat: 400 }, content);
          EventUtils.synthesizeKey("ArrowDown", { repeat: 400 }, content);
          Assert.greaterOrEqual(
            avatarSelector.avatarRegion.dimensions.width,
            48,
            "keyboard path clamps selection to minimum size"
          );

          let region = avatarSelector.avatarRegion.dimensions;
          let cropClientHeight =
            avatarSelector.customAvatarCropArea.clientHeight;
          let cropClientWidth = avatarSelector.customAvatarCropArea.clientWidth;

          EventUtils.synthesizeMouseAtCenter(
            avatarSelector.saveButton,
            {},
            content
          );

          // Sometimes the async message takes a bit longer to arrive.
          await new Promise(resolve => content.setTimeout(resolve, 500));

          return { region, cropClientHeight, cropClientWidth };
        }
      );

      curProfile = await SelectableProfileService.getProfile(profile.id);
      Assert.ok(
        curProfile.hasCustomAvatar,
        "Current profile has a custom avatar image"
      );

      let { region, cropClientHeight, cropClientWidth } = customAvatarData;
      const scale =
        avatarWidth <= avatarHeight
          ? avatarWidth / cropClientWidth
          : avatarHeight / cropClientHeight;

      const expectedWidth =
        2 * Math.round(region.radius * scale * window.devicePixelRatio);

      await verifyCustomAvatarImage(
        curProfile.getAvatarPath(),
        expectedWidth,
        expectedWidth
      );
    }
  );

  MockFilePicker.cleanup();
});

add_task(async function test_edit_profile_custom_avatar_keyboard_crop() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }
  const profile = await setup();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.profiles.updated-avatar-selector", true]],
  });

  const avatarWidth = 100;
  const avatarHeight = 100;

  const mockAvatarFile = await createAvatarFile(avatarWidth, avatarHeight);

  const MockFilePicker = SpecialPowers.MockFilePicker;
  MockFilePicker.init();
  MockFilePicker.setFiles([mockAvatarFile]);
  MockFilePicker.returnValue = MockFilePicker.returnOK;

  let curProfile = await SelectableProfileService.getProfile(profile.id);
  await curProfile.setAvatar("star");
  Assert.ok(
    !curProfile.hasCustomAvatar,
    "Current profile does not have a custom avatar"
  );

  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: "about:editprofile",
    },
    async browser => {
      await SpecialPowers.spawn(browser, [], async () => {
        let editProfileCard =
          content.document.querySelector("edit-profile-card").wrappedJSObject;

        await ContentTaskUtils.waitForCondition(
          () => editProfileCard.initialized,
          "Waiting for edit-profile-card to be initialized"
        );

        await editProfileCard.updateComplete;

        const avatarSelector = editProfileCard.avatarSelector;

        EventUtils.synthesizeMouseAtCenter(
          editProfileCard.avatarSelectorButton,
          {},
          content
        );

        Assert.ok(
          ContentTaskUtils.isVisible(avatarSelector.dialog),
          "Should be showing the profile avatar selector"
        );

        EventUtils.synthesizeKey("KEY_Escape", {}, content);

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isHidden(avatarSelector.dialog),
          "Waiting for avatar selector to be hidden"
        );
        Assert.ok(
          ContentTaskUtils.isHidden(avatarSelector.dialog),
          "Should be hiding the profile avatar selector"
        );

        EventUtils.synthesizeMouseAtCenter(
          editProfileCard.avatarSelectorButton,
          {},
          content
        );

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isVisible(avatarSelector.dialog),
          "Waiting for avatar selector to be showing"
        );
        Assert.ok(
          ContentTaskUtils.isVisible(avatarSelector.dialog),
          "Should be showing the profile avatar selector"
        );

        EventUtils.synthesizeMouseAtCenter(
          avatarSelector.customTabButton,
          {},
          content
        );
        await avatarSelector.updateComplete;

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isVisible(avatarSelector.input),
          "Waiting for avatar selector input to be visible"
        );

        EventUtils.synthesizeKey("KEY_Escape", {}, content);

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isHidden(avatarSelector.dialog),
          "Waiting for avatar selector to be hidden"
        );
        Assert.ok(
          ContentTaskUtils.isHidden(avatarSelector.dialog),
          "Should be hiding the profile avatar selector"
        );

        EventUtils.synthesizeMouseAtCenter(
          editProfileCard.avatarSelectorButton,
          {},
          content
        );

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isVisible(avatarSelector.dialog),
          "Waiting for avatar selector to be showing"
        );
        Assert.ok(
          ContentTaskUtils.isVisible(avatarSelector.dialog),
          "Should be showing the profile avatar selector"
        );

        EventUtils.synthesizeMouseAtCenter(
          avatarSelector.customTabButton,
          {},
          content
        );
        await avatarSelector.updateComplete;

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isVisible(avatarSelector.input),
          "Waiting for avatar selector input to be visible"
        );

        const inputReceived = new Promise(resolve =>
          avatarSelector.input.addEventListener(
            "input",
            event => {
              resolve(event.target.files[0].name);
            },
            { once: true }
          )
        );

        EventUtils.synthesizeMouseAtCenter(avatarSelector.input, {}, content);

        await inputReceived;

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isVisible(avatarSelector.saveButton),
          "Waiting for avatar selector save button to be visible"
        );

        await ContentTaskUtils.waitForCondition(
          () => avatarSelector.customAvatarImage?.complete,
          "Waiting for avatar selector image to load"
        );

        await avatarSelector.updateComplete;

        EventUtils.synthesizeKey("KEY_Escape", {}, content);

        await ContentTaskUtils.waitForCondition(
          () => ContentTaskUtils.isVisible(avatarSelector.input),
          "Waiting for avatar selector input to be visible"
        );

        Assert.ok(
          ContentTaskUtils.isVisible(avatarSelector.input),
          "Should still be showing the profile avatar selector after escape"
        );
      });
    }
  );

  MockFilePicker.cleanup();
});

add_task(async function test_missing_custom_avatar_falls_back_to_default() {
  if (!AppConstants.MOZ_SELECTABLE_PROFILES) {
    // `mochitest-browser` suite `add_task` does not yet support
    // `properties.skip_if`.
    ok(true, "Skipping because !AppConstants.MOZ_SELECTABLE_PROFILES");
    return;
  }
  const profile = await setup();
  Services.fog.testResetFOG();

  const mockAvatarFile = await createAvatarFile(100, 100);
  const avatarFile = await File.createFromFileName(mockAvatarFile.path);

  let curProfile = await SelectableProfileService.getProfile(profile.id);
  await curProfile.setAvatar(avatarFile);
  Assert.ok(
    curProfile.hasCustomAvatar,
    "Current profile has a custom avatar image"
  );

  const avatarPath = curProfile.getAvatarPath();
  Assert.ok(await IOUtils.exists(avatarPath), "Custom avatar file exists");

  // Simulate the avatar file being moved or deleted out from under us.
  await IOUtils.remove(avatarPath);
  Assert.ok(
    !(await IOUtils.exists(avatarPath)),
    "Custom avatar file has been removed"
  );

  const url = await curProfile.getAvatarURL();
  Assert.equal(
    url,
    "chrome://browser/content/profiles/assets/80_star.svg",
    "Missing custom avatar falls back to the default star avatar URL"
  );
  Assert.ok(
    !curProfile.hasCustomAvatar,
    "Profile no longer reports a custom avatar after the fallback"
  );
  Assert.equal(
    curProfile.avatar,
    "star",
    "Profile avatar was reset to the default star avatar"
  );
  await assertGlean("profiles", "error", "avatar");

  // The fallback should be persisted so other instances see it too.
  const reloadedProfile = await SelectableProfileService.getProfile(profile.id);
  Assert.equal(
    reloadedProfile.avatar,
    "star",
    "Fallback avatar is persisted to the database"
  );
});

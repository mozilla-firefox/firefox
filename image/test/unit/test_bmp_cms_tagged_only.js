// In tagged-only color management mode, BMPs without usable color space
// information must not be color managed.

setColorManagementPrefs(2);

add_task(function test_tagged_only() {
  Assert.notDeepEqual(
    decodeSRGBTaggedBmp(),
    PIXELS,
    "sRGB-tagged BMP is converted to the output profile"
  );
  for (const [description, bmp] of untaggedBmps()) {
    Assert.deepEqual(
      decodePixels(bmp),
      PIXELS,
      `${description} is not color managed`
    );
  }
});

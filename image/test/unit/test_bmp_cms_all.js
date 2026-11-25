// In full color management mode, BMPs without usable color space information
// are treated as sRGB.

setColorManagementPrefs(1);

add_task(function test_all() {
  const srgbPixels = decodeSRGBTaggedBmp();
  Assert.notDeepEqual(
    srgbPixels,
    PIXELS,
    "sRGB-tagged BMP is converted to the output profile"
  );
  for (const [description, bmp] of untaggedBmps()) {
    Assert.deepEqual(
      decodePixels(bmp),
      srgbPixels,
      `${description} is treated as sRGB`
    );
  }
});

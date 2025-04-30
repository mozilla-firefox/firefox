/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that getFontPreviewData of the style utils generates font previews.

const TEST_URI = "data:text/html,<title>Test getFontPreviewData</title>";

add_task(async function () {
  await addTab(TEST_URI);

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    const { require } = ChromeUtils.importESModule(
      "resource://devtools/shared/loader/Loader.sys.mjs"
    );
    const {
      getFontPreviewData,
    } = require("resource://devtools/server/actors/utils/style-utils.js");

    const font = Services.appinfo.OS === "WINNT" ? "Arial" : "Liberation Sans";
    let fontPreviewData = getFontPreviewData(font, content.document);
    ok(
      fontPreviewData?.dataURL,
      "Returned a font preview with a valid dataURL"
    );
    is(
      fontPreviewData.ctx.font,
      `40px ${Services.appinfo.OS === "WINNT" ? "Arial" : `"Liberation Sans"`}, serif`,
      "Expected font style was used in the canvas"
    );

    // Create <img> element and load the generated preview into it
    // to check whether the image is valid and get its dimensions
    const image = content.document.createElement("img");
    let imageLoaded = new Promise(loaded =>
      image.addEventListener("load", loaded, { once: true })
    );
    image.src = fontPreviewData.dataURL;
    await imageLoaded;

    const { naturalWidth: widthImage1, naturalHeight: heightImage1 } = image;

    Assert.greater(widthImage1, 0, "Preview width is greater than 0");
    Assert.greater(heightImage1, 0, "Preview height is greater than 0");

    // Create a preview with different text and compare
    // its dimensions with the first one
    fontPreviewData = getFontPreviewData(font, content.document, {
      previewText: "Abcdef",
    });

    ok(
      fontPreviewData?.dataURL,
      "Returned a font preview with a valid dataURL"
    );

    imageLoaded = new Promise(loaded =>
      image.addEventListener("load", loaded, { once: true })
    );
    image.src = fontPreviewData.dataURL;
    await imageLoaded;

    const { naturalWidth: widthImage2, naturalHeight: heightImage2 } = image;

    // Check whether the width is greater than with the default parameters
    // and that the height is the same
    Assert.greater(
      widthImage2,
      widthImage1,
      "Preview width is greater than with default parameters"
    );
    Assert.strictEqual(
      heightImage2,
      heightImage1,
      "Preview height is the same as with default parameters"
    );

    // Create a preview with smaller font size and compare
    // its dimensions with the first one
    fontPreviewData = getFontPreviewData(font, content.document, {
      previewFontSize: 20,
    });

    ok(
      fontPreviewData?.dataURL,
      "Returned a font preview with a valid dataURL"
    );

    imageLoaded = new Promise(loaded =>
      image.addEventListener("load", loaded, { once: true })
    );
    image.src = fontPreviewData.dataURL;
    await imageLoaded;

    const { naturalWidth: widthImage3, naturalHeight: heightImage3 } = image;

    // Check whether the width and height are smaller than with the default parameters
    Assert.less(
      widthImage3,
      widthImage1,
      "Preview width is smaller than with default parameters"
    );
    Assert.less(
      heightImage3,
      heightImage1,
      "Preview height is smaller than with default parameters"
    );

    // Create a preview with multiple lines and compare
    // its dimensions with the first one
    fontPreviewData = getFontPreviewData(font, content.document, {
      previewText: "Abc\ndef",
    });

    ok(
      fontPreviewData?.dataURL,
      "Returned a font preview with a valid dataURL"
    );

    imageLoaded = new Promise(loaded =>
      image.addEventListener("load", loaded, { once: true })
    );
    image.src = fontPreviewData.dataURL;
    await imageLoaded;

    const { naturalWidth: widthImage4, naturalHeight: heightImage4 } = image;

    // Check whether the width is the same as with the default parameters
    // and that the height is greater
    Assert.strictEqual(
      widthImage4,
      widthImage1,
      "Preview width is the same as with default parameters"
    );
    Assert.greater(
      heightImage4,
      heightImage1,
      "Preview height is greater than with default parameters"
    );

    // Check generic family name
    is(
      getFontPreviewData("monospace", content.document).ctx.font,
      `40px monospace, serif`,
      "Expected font style was used in the canvas"
    );

    // Check font wrapped in double quotes
    is(
      getFontPreviewData(`"Zilla Bold"`, content.document).ctx.font,
      `40px "Zilla Bold", serif`,
      "Expected font style was used in the canvas"
    );

    // Check font wrapped in simple quotes
    is(
      getFontPreviewData(`'Font Awesome 5 Brands'`, content.document).ctx.font,
      `40px "Font Awesome 5 Brands", serif`,
      "Expected font style was used in the canvas"
    );

    // Check multiple font
    is(
      getFontPreviewData(`Menlo, monospace`, content.document).ctx.font,
      `40px Menlo, monospace, serif`,
      "Expected font style was used in the canvas"
    );

    // Check multiple font some with quotes, some not
    is(
      getFontPreviewData(
        `Menlo Bold, "Fira Code", 'Mono Lisa', monospace`,
        content.document
      ).ctx.font,
      `40px "Menlo Bold", "Fira Code", "Mono Lisa", monospace, serif`,
      "Expected font style was used in the canvas"
    );

    // Check font-weight value
    is(
      getFontPreviewData(`monospace`, content.document, { fontWeight: "200" })
        .ctx.font,
      `200 40px monospace, serif`,
      "Expected font style was used in the canvas"
    );

    // Check font-style value
    is(
      getFontPreviewData(`monospace`, content.document, {
        fontWeight: "200",
        fontStyle: "italic",
      }).ctx.font,
      `italic 200 40px monospace, serif`,
      "Expected font style was used in the canvas"
    );
  });
});

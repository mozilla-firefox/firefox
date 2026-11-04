/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.security.html_serialization_escape_lt_gt", true],
      ["layout.css.alpha-color-function.enabled", true],
      ["layout.css.backdrop-filter.enabled", true],
      ["layout.css.color-mix-multi-color.enabled", true],
      ["layout.css.random.enabled", true],
      ["security.allow_unsafe_parent_loads", true],
    ],
  });
  await addTab("about:blank");
  await performTest();
  gBrowser.removeCurrentTab();
});

async function performTest() {
  const OutputParser = require("resource://devtools/client/shared/output-parser.js");

  const { host, doc } = await createHost(
    "bottom",
    "data:text/html," + "<h1>browser_outputParser.js</h1><div></div>"
  );

  const cssProperties = getClientCssProperties();

  const parser = new OutputParser(doc, cssProperties);
  testParseCssProperty(doc, parser);
  testParseCssVar(doc, parser);
  testParseURL(doc, parser);
  testParseFilter(doc, parser);
  testParseBackdropFilter(doc, parser);
  testParseAngle(doc, parser);
  testParseShape(doc, parser);
  testParseVariable(doc, parser);
  testParseColorVariable(doc, parser);
  testParseFontFamily(doc, parser);
  testParseLightDark(doc, parser);
  testParseAttr(doc, parser);
  testParseForCssExplainers(doc, parser);

  host.destroy();
}

// Class name used in color swatch.
var COLOR_TEST_CLASS = "test-class";

// Create a new CSS color-parsing test.  |name| is the name of the CSS
// property.  |value| is the CSS text to use.  |segments| is an array
// describing the expected result.  If an element of |segments| is a
// string, it is simply appended to the expected string.  Otherwise,
// it must be an object with a |name| property, which is the color
// name as it appears in the input.
//
// This approach is taken to reduce boilerplate and to make it simpler
// to modify the test when the parseCssProperty output changes.
function makeColorTest(name, value, segments) {
  const result = {
    name,
    value,
    expected: "",
  };

  for (const segment of segments) {
    if (typeof segment === "string") {
      result.expected += segment;
    } else {
      result.expected += getColorMarkup({
        color: segment.name,
        colorFunction: segment.colorFunction,
      });
    }
  }

  return result;
}

function getColorMarkup({ color, colorFunction, content }) {
  const buttonAttributes = {
    class: COLOR_TEST_CLASS,
    style: `background-color:${color}`,
    tabindex: 0,
    role: "button",
  };
  if (colorFunction) {
    buttonAttributes["data-color-function"] = colorFunction;
  }
  const buttonAttrString = Object.entries(buttonAttributes)
    .map(([attr, v]) => `${attr}="${v}"`)
    .join(" ");

  // prettier-ignore
  return (
    `<span data-color="${color}" class="color-swatch-container">` +
      `<span ${buttonAttrString}></span>` +
      `<span>${content ?? color}</span>` +
    `</span>`
  );
}

function testParseCssProperty(doc, parser) {
  const tests = [
    makeColorTest("border", "1px solid red", ["1px solid ", { name: "red" }]),

    makeColorTest(
      "background-image",
      "linear-gradient(to right, #F60 10%, rgba(0,0,0,1))",
      [
        "linear-gradient(to right, ",
        { name: "#F60", colorFunction: "linear-gradient" },
        " 10%, ",
        { name: "rgba(0,0,0,1)", colorFunction: "linear-gradient" },
        ")",
      ]
    ),

    // In "arial black", "black" is a font, not a color.
    // (The font-family parser creates a span)
    makeColorTest("font-family", "arial black", ["<span>arial black</span>"]),

    makeColorTest("box-shadow", "0 0 1em red", ["0 0 1em ", { name: "red" }]),

    makeColorTest("box-shadow", "0 0 1em red, 2px 2px 0 0 rgba(0,0,0,.5)", [
      "0 0 1em ",
      { name: "red" },
      ", 2px 2px 0 0 ",
      { name: "rgba(0,0,0,.5)" },
    ]),

    makeColorTest("content", '"red"', ['"red"']),

    // Invalid property names should not cause exceptions.
    makeColorTest("hellothere", "'red'", ["'red'"]),

    makeColorTest(
      "filter",
      "blur(1px) drop-shadow(0 0 0 blue) url(red.svg#blue)",
      [
        '<span data-filters="blur(1px) drop-shadow(0 0 0 blue) ',
        'url(red.svg#blue)"><span>',
        "blur(1px) drop-shadow(0 0 0 ",
        { name: "blue", colorFunction: "drop-shadow" },
        ") url(red.svg#blue)</span></span>",
      ]
    ),

    makeColorTest("color", "currentColor", ["currentColor"]),

    // Test a very long property.
    makeColorTest(
      "background-image",
      "linear-gradient(to left, transparent 0, transparent 5%,#F00 0, #F00 10%,#FF0 0, #FF0 15%,#0F0 0, #0F0 20%,#0FF 0, #0FF 25%,#00F 0, #00F 30%,#800 0, #800 35%,#880 0, #880 40%,#080 0, #080 45%,#088 0, #088 50%,#008 0, #008 55%,#FFF 0, #FFF 60%,#EEE 0, #EEE 65%,#CCC 0, #CCC 70%,#999 0, #999 75%,#666 0, #666 80%,#333 0, #333 85%,#111 0, #111 90%,#000 0, #000 95%,transparent 0, transparent 100%)",
      [
        "linear-gradient(to left, ",
        { name: "transparent", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "transparent", colorFunction: "linear-gradient" },
        " 5%,",
        { name: "#F00", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#F00", colorFunction: "linear-gradient" },
        " 10%,",
        { name: "#FF0", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#FF0", colorFunction: "linear-gradient" },
        " 15%,",
        { name: "#0F0", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#0F0", colorFunction: "linear-gradient" },
        " 20%,",
        { name: "#0FF", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#0FF", colorFunction: "linear-gradient" },
        " 25%,",
        { name: "#00F", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#00F", colorFunction: "linear-gradient" },
        " 30%,",
        { name: "#800", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#800", colorFunction: "linear-gradient" },
        " 35%,",
        { name: "#880", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#880", colorFunction: "linear-gradient" },
        " 40%,",
        { name: "#080", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#080", colorFunction: "linear-gradient" },
        " 45%,",
        { name: "#088", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#088", colorFunction: "linear-gradient" },
        " 50%,",
        { name: "#008", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#008", colorFunction: "linear-gradient" },
        " 55%,",
        { name: "#FFF", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#FFF", colorFunction: "linear-gradient" },
        " 60%,",
        { name: "#EEE", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#EEE", colorFunction: "linear-gradient" },
        " 65%,",
        { name: "#CCC", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#CCC", colorFunction: "linear-gradient" },
        " 70%,",
        { name: "#999", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#999", colorFunction: "linear-gradient" },
        " 75%,",
        { name: "#666", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#666", colorFunction: "linear-gradient" },
        " 80%,",
        { name: "#333", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#333", colorFunction: "linear-gradient" },
        " 85%,",
        { name: "#111", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#111", colorFunction: "linear-gradient" },
        " 90%,",
        { name: "#000", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "#000", colorFunction: "linear-gradient" },
        " 95%,",
        { name: "transparent", colorFunction: "linear-gradient" },
        " 0, ",
        { name: "transparent", colorFunction: "linear-gradient" },
        " 100%)",
      ]
    ),

    // Note the lack of a space before the color here.
    makeColorTest("border", "1px dotted#f06", [
      "1px dotted ",
      { name: "#f06" },
    ]),

    {
      name: "color",
      value: "color-mix(in srgb, red, blue)",
      expected: getColorMarkup({
        color: "color-mix(in srgb, red, blue)",
        content:
          `color-mix(in srgb, ` +
          // we have a nested color spans for the color-mix() params, `red` and `blue`
          getColorMarkup({ color: "red", colorFunction: "color-mix" }) +
          `, ` +
          getColorMarkup({ color: "blue", colorFunction: "color-mix" }) +
          `)`,
      }),
    },

    {
      name: "background-image",
      value:
        "linear-gradient(to top, color-mix(in srgb, #008000, rgba(255, 255, 0, 0.9)), blue, contrast-color(#abc))",
      expected:
        `linear-gradient(to top, ` +
        // first we have a nested color span for resulting color of color-mix()
        getColorMarkup({
          color: "color-mix(in srgb, #008000, rgba(255, 255, 0, 0.9))",
          colorFunction: "linear-gradient",
          content:
            `color-mix(in srgb, ` +
            // we have a nested color spans for the color-mix() params, `#008000` and `rgba(255, 255, 0, 0.9)`
            getColorMarkup({ color: "#008000", colorFunction: "color-mix" }) +
            `, ` +
            getColorMarkup({
              color: "rgba(255, 255, 0, 0.9)",
              colorFunction: "color-mix",
            }) +
            // closing the `color-mix()` function
            `)`,
        }) +
        ", " +
        // second param for the gradient, `blue`
        getColorMarkup({
          color: "blue",
          colorFunction: "linear-gradient",
        }) +
        ", " +
        // third param for the gradient, a `contrast-color()`
        getColorMarkup({
          color: "contrast-color(#abc)",
          colorFunction: "linear-gradient",
          content:
            `contrast-color(` +
            getColorMarkup({
              // we have a nested color spans for the contrast-color() param, `#abc`
              color: "#abc",
              colorFunction: "contrast-color",
            }) +
            `)`,
        }) +
        // closing the `linear-gradient()` function
        ")",
    },

    makeColorTest("color", "light-dark(red, blue)", [
      "light-dark(",
      { name: "red", colorFunction: "light-dark" },
      ", ",
      { name: "blue", colorFunction: "light-dark" },
      ")",
    ]),

    makeColorTest(
      "background-image",
      "linear-gradient(to top, light-dark(#008000, rgba(255, 255, 0, 0.9)), blue)",
      [
        "linear-gradient(to top, ",
        "light-dark(",
        { name: "#008000", colorFunction: "light-dark" },
        ", ",
        { name: "rgba(255, 255, 0, 0.9)", colorFunction: "light-dark" },
        "), ",
        { name: "blue", colorFunction: "linear-gradient" },
        ")",
      ]
    ),

    {
      name: "color",
      value: "rgb(from gold r g b)",
      expected: getColorMarkup({
        color: "rgb(from gold r g b)",
        // we have a nested color span for the `gold` after `from`
        content: `rgb(from ${getColorMarkup({ color: "gold", colorFunction: "rgb" })} r g b)`,
      }),
    },

    {
      name: "color",
      value: "color(from hsl(0 100% 50%) xyz x y 0.5)",
      expected: getColorMarkup({
        color: "color(from hsl(0 100% 50%) xyz x y 0.5)",
        // we have a nested color span for the inner `hsl()` after `from`
        content:
          `color(from ` +
          getColorMarkup({ color: "hsl(0 100% 50%)", colorFunction: "color" }) +
          ` xyz x y 0.5)`,
      }),
    },

    {
      name: "color",
      value: "oklab(from red calc(l - 1) calc(a * 2) calc(b + 3) / alpha)",
      expected: getColorMarkup({
        color: "oklab(from red calc(l - 1) calc(a * 2) calc(b + 3) / alpha)",
        // we have a nested color span for the inner `red` after `from`
        content:
          `oklab(from ` +
          getColorMarkup({ color: "red", colorFunction: "oklab" }) +
          ` calc(l - 1) calc(a * 2) calc(b + 3) / alpha)`,
      }),
    },

    {
      name: "color",
      value: "rgb(from color-mix(in lch, plum 40%, pink) r g b)",
      expected: getColorMarkup({
        color: "rgb(from color-mix(in lch, plum 40%, pink) r g b)",
        content:
          `rgb(from ` +
          // we have a nested color span for the inner `color-mix()` after `from`
          getColorMarkup({
            color: "color-mix(in lch, plum 40%, pink)",
            colorFunction: "rgb",
            content:
              `color-mix(in lch, ` +
              // and we have nested colors representing the color-mix color params (plum and pink)
              getColorMarkup({ color: "plum", colorFunction: "color-mix" }) +
              ` 40%, ` +
              getColorMarkup({ color: "pink", colorFunction: "color-mix" }) +
              `)`,
          }) +
          ` r g b)`,
      }),
    },

    {
      name: "color",
      value: "rgb(from rgb(from gold r g b) r g b)",
      expected: getColorMarkup({
        color: "rgb(from rgb(from gold r g b) r g b)",
        content:
          `rgb(from ` +
          // we have a nested color span for the inner `rgb()` after `from`
          getColorMarkup({
            color: "rgb(from gold r g b)",
            colorFunction: "rgb",
            content:
              `rgb(from ` +
              // we have a nested color span for `gold` after `from`
              getColorMarkup({ color: "gold", colorFunction: "rgb" }) +
              ` r g b)`,
          }) +
          ` r g b)`,
      }),
    },

    {
      name: "color",
      value: "alpha(from red / calc(alpha * 0.5))",
      expected: getColorMarkup({
        color: "alpha(from red / calc(alpha * 0.5))",
        // we have a nested color span for the inner `red` after `from`
        content:
          `alpha(from ` +
          getColorMarkup({ color: "red", colorFunction: "alpha" }) +
          ` / calc(alpha * 0.5))`,
      }),
    },

    {
      name: "background-image",
      value: `image(rgb(255 0 0 / 0.5)), url("bg-image.png")`,
      expected:
        `image(` +
        getColorMarkup({
          color: "rgb(255 0 0 / 0.5)",
          colorFunction: "image",
        }) +
        `), url("bg-image.png")`,
    },

    {
      name: "background-image",
      value: "linear-gradient(to right, #F60 10%, rgb(from gold r g b))",
      expected:
        `linear-gradient(to right, ` +
        getColorMarkup({ color: "#F60", colorFunction: "linear-gradient" }) +
        " 10%, " +
        getColorMarkup({
          color: "rgb(from gold r g b)",
          colorFunction: "linear-gradient",
          content:
            `rgb(from ` +
            // nested color span for `gold` after `from`
            getColorMarkup({
              color: "gold",
              colorFunction: "rgb",
            }) +
            " r g b)",
        }) +
        // closing linear-gradient()
        ")",
    },

    {
      desc: "--a: (min-width:680px)",
      name: "--a",
      value: "(min-width:680px)",
      expected: "(min-width:680px)",
    },

    {
      desc: "Interactive color swatch",
      name: "color",
      value: "gold",
      expected:
        // prettier-ignore
        `<span data-color="gold" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:gold" tabindex="0" role="button"></span>` +
          `<span>gold</span>` +
        `</span>`,
      parserExtraOptions: {
        colorSwatchReadOnly: false,
      },
    },

    {
      desc: "Read-only color swatch",
      name: "color",
      value: "gold",
      expected:
        // prettier-ignore
        `<span data-color="gold" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:gold"></span>` +
          `<span>gold</span>` +
        `</span>`,
      parserExtraOptions: {
        colorSwatchReadOnly: true,
      },
    },

    {
      name: "color",
      value: "contrast-color(red)",
      expected: getColorMarkup({
        color: "contrast-color(red)",
        content:
          "contrast-color(" +
          // color span for the `color()` param, `red`
          getColorMarkup({ color: "red", colorFunction: "contrast-color" }) +
          ")",
      }),
    },

    {
      name: "color",
      value: "color-mix(in srgb, red, contrast-color(hsl(0 100 200)))",
      expected: getColorMarkup({
        color: "color-mix(in srgb, red, contrast-color(hsl(0 100 200)))",
        content:
          "color-mix(in srgb, " +
          // color span for the `color-mix()` param, `red`
          getColorMarkup({ color: "red", colorFunction: "color-mix" }) +
          ", " +
          // color span for the resulting color of `contrast-color()`
          getColorMarkup({
            color: "contrast-color(hsl(0 100 200))",
            colorFunction: "color-mix",
            content:
              "contrast-color(" +
              // color span for the `color()` param, `hsl(0 100 200)`
              getColorMarkup({
                color: "hsl(0 100 200)",
                colorFunction: "contrast-color",
              }) +
              ")",
          }) +
          // closing `color-mix()`
          ")",
      }),
    },

    {
      name: "color",
      value: "color-mix(in srgb, red, blue, green)",
      expected: getColorMarkup({
        color: "color-mix(in srgb, red, blue, green)",
        content:
          "color-mix(in srgb, " +
          // color span for the first `color-mix()` param, `red`
          getColorMarkup({ color: "red", colorFunction: "color-mix" }) +
          ", " +
          // color span for the second `color-mix()` param, `blue`
          getColorMarkup({ color: "blue", colorFunction: "color-mix" }) +
          ", " +
          // color span for the third `color-mix()` param, `green`
          getColorMarkup({ color: "green", colorFunction: "color-mix" }) +
          // closing `color-mix()
          ")",
      }),
    },

    {
      name: "color",
      value: "color-mix(in srgb, red)",
      expected: getColorMarkup({
        color: "color-mix(in srgb, red)",
        content:
          "color-mix(in srgb, " +
          // color span for the first `color-mix()` param, `red`
          getColorMarkup({ color: "red", colorFunction: "color-mix" }) +
          // closing `color-mix()
          ")",
      }),
    },

    {
      name: "color",
      value: "hsl(100 sibling-index() sibling-count())",
      // Check what happens without provided siblingCount/siblingIndex
      parserExtraOptions: {
        siblingCount: null,
        siblingIndex: null,
      },
      expected: getColorMarkup({
        color: "hsl(100 sibling-index() sibling-count())",
      }),
    },

    {
      name: "color",
      value: "hsl(100 sibling-index() sibling-count())",
      parserExtraOptions: {
        siblingCount: 20,
        siblingIndex: 10,
      },
      expected: getColorMarkup({
        content: "hsl(100 sibling-index() sibling-count())",
        color: "hsl(100 10 20)",
      }),
    },

    {
      name: "color",
      value:
        "oklch(from hsl(100 sibling-count() 50) l c calc(h * sibling-index()))",
      parserExtraOptions: {
        siblingCount: 11,
        siblingIndex: 7,
      },
      expected: getColorMarkup({
        color: "oklch(from hsl(100 11 50) l c calc(h * 7))",
        content:
          "oklch(from " +
          getColorMarkup({
            color: "hsl(100 11 50)",
            colorFunction: "oklch",
            content: "hsl(100 sibling-count() 50)",
          }) +
          " l c calc(h * sibling-index()))",
      }),
    },
  ];

  const target = doc.querySelector("div");
  ok(target, "captain, we have the div");

  for (const test of tests) {
    info(`Testing "${test.name}: ${test.value}"`);

    const frag = parser.parseCssProperty(test.name, test.value, {
      colorSwatchClass: COLOR_TEST_CLASS,
      ...(test.parserExtraOptions || {}),
    });

    target.appendChild(frag);

    is(
      target.innerHTML,
      test.expected,
      `CSS property correctly parsed for "${test.name}: ${test.value}"`
    );

    target.innerHTML = "";
  }
}

function testParseCssVar(doc, parser) {
  const frag = parser.parseCssProperty("color", "var(--some-kind-of-green)", {
    colorSwatchClass: "test-colorswatch",
  });

  const target = doc.querySelector("div");
  ok(target, "captain, we have the div");
  target.appendChild(frag);

  is(
    target.innerHTML,
    "var(--some-kind-of-green)",
    "CSS property correctly parsed"
  );

  target.innerHTML = "";
}

function testParseURL(doc, parser) {
  info("Test that URL parsing preserves quoting style");

  const tests = [
    {
      desc: "simple test without quotes",
      leader: "url(",
      trailer: ")",
    },
    {
      desc: "simple test with single quotes",
      leader: "url('",
      trailer: "')",
    },
    {
      desc: "simple test with double quotes",
      leader: 'url("',
      trailer: '")',
    },
    {
      desc: "test with single quotes and whitespace",
      leader: "url( \t'",
      trailer: "'\r\n\f)",
    },
    {
      desc: "simple test with uppercase",
      leader: "URL(",
      trailer: ")",
    },
    {
      desc: "bad url, missing paren",
      leader: "url(",
      trailer: "",
      expectedTrailer: ")",
    },
    {
      desc: "bad url, missing paren, with baseURI",
      baseURI: "data:text/html,<style></style>",
      leader: "url(",
      trailer: "",
      expectedTrailer: ")",
    },
    {
      desc: "bad url, double quote, missing paren",
      leader: 'url("',
      trailer: '"',
      expectedTrailer: '")',
    },
    {
      desc: "bad url, single quote, missing paren and quote",
      leader: "url('",
      trailer: "",
      expectedTrailer: "')",
    },
  ];

  const target = doc.querySelector("div");
  for (const test of tests) {
    const url = test.leader + "something.jpg" + test.trailer;
    const frag = parser.parseCssProperty("background", url, {
      urlClass: "test-urlclass",
      baseURI: test.baseURI,
    });

    target.replaceChildren(frag);

    const expectedTrailer = test.expectedTrailer || test.trailer;

    const expected =
      test.leader +
      '<a target="_blank" class="test-urlclass" ' +
      'href="something.jpg">something.jpg</a>' +
      expectedTrailer;

    is(target.innerHTML, expected, test.desc);
  }

  info("Check that long URLs get the class for visual truncation");
  const LONG_URL = `something.jpg?${"a".repeat(5000)}`;
  target.replaceChildren(
    parser.parseCssProperty("background", `url(${LONG_URL})`, {
      urlClass: "test-urlclass",
    })
  );
  is(
    target.innerHTML,
    // prettier-ignore
    `url(` +
    `<a target="_blank" class="test-urlclass propertyvalue-long-text" href="${LONG_URL}">` +
    LONG_URL +
    `</a>` +
    `)`,
    "long url is wrapped in an element with a specific class"
  );

  target.replaceChildren(
    parser.parseCssProperty("background", `url(${LONG_URL})`, {})
  );
  is(
    target.innerHTML,
    `<span class="propertyvalue-long-text">url(${LONG_URL})</span>`,
    "long url is wrapped in an element with a specific class, even when urlClass option is not set"
  );

  target.innerHTML = "";
}

function testParseFilter(doc, parser) {
  const frag = parser.parseCssProperty("filter", "something invalid", {
    filterSwatchClass: "test-filterswatch",
  });

  const swatchCount = frag.querySelectorAll(".test-filterswatch").length;
  is(swatchCount, 1, "filter swatch was created");
}

function testParseBackdropFilter(doc, parser) {
  const frag = parser.parseCssProperty("backdrop-filter", "something invalid", {
    filterSwatchClass: "test-filterswatch",
  });

  const swatchCount = frag.querySelectorAll(".test-filterswatch").length;
  is(swatchCount, 1, "filter swatch was created for backdrop-filter");
}

function testParseAngle(doc, parser) {
  let frag = parser.parseCssProperty("rotate", "90deg", {
    angleSwatchClass: "test-angleswatch",
  });

  let swatchCount = frag.querySelectorAll(".test-angleswatch").length;
  is(swatchCount, 1, "angle swatch was created");

  frag = parser.parseCssProperty(
    "background-image",
    "linear-gradient(90deg, red, blue",
    {
      angleSwatchClass: "test-angleswatch",
    }
  );

  swatchCount = frag.querySelectorAll(".test-angleswatch").length;
  is(swatchCount, 1, "angle swatch was created");
}

function testParseShape(doc, parser) {
  info("Test shape parsing");

  const tests = [
    {
      desc: "simple polygon()",
      definition: "polygon(0px 0px, 10px 10px, 10px 20px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `polygon(` +
            `<span class="inspector-shape-point" data-point="0">` +
              `<span class="inspector-shape-point" data-point="0" data-pair="x">0px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="0" data-pair="y">0px</span>` +
            `</span>` +
            `, ` +
            `<span class="inspector-shape-point" data-point="1">` +
              `<span class="inspector-shape-point" data-point="1" data-pair="x">10px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="1" data-pair="y">10px</span>` +
            `</span>` +
            `, ` +
            `<span class="inspector-shape-point" data-point="2">` +
              `<span class="inspector-shape-point" data-point="2" data-pair="x">10px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="2" data-pair="y">20px</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "simple polygon() with extra spaces",
      definition: "polygon( 0px 0px , 10px 10px , 10px 20px )",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `polygon( ` +
            `<span class="inspector-shape-point" data-point="0">` +
              `<span class="inspector-shape-point" data-point="0" data-pair="x">0px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="0" data-pair="y">0px</span>` +
            `</span>` +
            ` , ` +
            `<span class="inspector-shape-point" data-point="1">` +
              `<span class="inspector-shape-point" data-point="1" data-pair="x">10px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="1" data-pair="y">10px</span>` +
            `</span>` +
            ` , ` +
            `<span class="inspector-shape-point" data-point="2">` +
              `<span class="inspector-shape-point" data-point="2" data-pair="x">10px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="2" data-pair="y">20px</span>` +
            `</span>` +
            ` )` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "polygon() with fill rule",
      definition: "polygon(nonzero, 0px 0px, 10px 10px, 10px 20px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `polygon(nonzero, ` +
            `<span class="inspector-shape-point" data-point="0">` +
              `<span class="inspector-shape-point" data-point="0" data-pair="x">0px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="0" data-pair="y">0px</span>` +
            `</span>` +
            `, ` +
            `<span class="inspector-shape-point" data-point="1">` +
              `<span class="inspector-shape-point" data-point="1" data-pair="x">10px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="1" data-pair="y">10px</span>` +
            `</span>` +
            `, ` +
            `<span class="inspector-shape-point" data-point="2">` +
              `<span class="inspector-shape-point" data-point="2" data-pair="x">10px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="2" data-pair="y">20px</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "complex polygon()",
      definition:
        "polygon(evenodd, 0px 0px, 10%200px,30%30% , calc(250px - 10px) 0 ,\n " +
        "12em var(--variable), 100% 100%) margin-box",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `polygon(` +
            `evenodd, ` +
            `<span class="inspector-shape-point" data-point="0">` +
              `<span class="inspector-shape-point" data-point="0" data-pair="x">0px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="0" data-pair="y">0px</span>` +
            `</span>` +
            `, ` +
            `<span class="inspector-shape-point" data-point="1">` +
              `<span class="inspector-shape-point" data-point="1" data-pair="x">10%</span>` +
              `<span class="inspector-shape-point" data-point="1" data-pair="y">200px</span>` +
            `</span>` +
            `,` +
            `<span class="inspector-shape-point" data-point="2">` +
              `<span class="inspector-shape-point" data-point="2" data-pair="x">30%</span>` +
              `<span class="inspector-shape-point" data-point="2" data-pair="y">30%</span>` +
            `</span>` +
            ` , ` +
            `<span class="inspector-shape-point" data-point="3">` +
              `<span class="inspector-shape-point" data-point="3" data-pair="x">calc(250px - 10px)</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="3" data-pair="y">0</span>` +
            `</span>` +
            ` ,\n ` +
            `<span class="inspector-shape-point" data-point="4">` +
              `<span class="inspector-shape-point" data-point="4" data-pair="x">12em</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="4" data-pair="y">var(--variable)</span>` +
            `</span>` +
            `, ` +
            `<span class="inspector-shape-point" data-point="5">` +
              `<span class="inspector-shape-point" data-point="5" data-pair="x">100%</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="5" data-pair="y">100%</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>` +
        ` margin-box`,
    },
    {
      desc: "complex POLYGON()",
      definition:
        "POLYGON(evenodd, 0px 0px, 10%200px,30%30% , calc(250px - 10px) 0 ,\n " +
        "12em var(--variable), 100% 100%) margin-box",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `POLYGON(` +
            `evenodd, ` +
            `<span class="inspector-shape-point" data-point="0">` +
              `<span class="inspector-shape-point" data-point="0" data-pair="x">0px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="0" data-pair="y">0px</span>` +
            `</span>` +
            `, ` +
            `<span class="inspector-shape-point" data-point="1">` +
              `<span class="inspector-shape-point" data-point="1" data-pair="x">10%</span>` +
              `<span class="inspector-shape-point" data-point="1" data-pair="y">200px</span>` +
            `</span>` +
            `,` +
            `<span class="inspector-shape-point" data-point="2">` +
              `<span class="inspector-shape-point" data-point="2" data-pair="x">30%</span>` +
              `<span class="inspector-shape-point" data-point="2" data-pair="y">30%</span>` +
            `</span>` +
            ` , ` +
            `<span class="inspector-shape-point" data-point="3">` +
              `<span class="inspector-shape-point" data-point="3" data-pair="x">calc(250px - 10px)</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="3" data-pair="y">0</span>` +
            `</span>` +
            ` ,\n ` +
            `<span class="inspector-shape-point" data-point="4">` +
              `<span class="inspector-shape-point" data-point="4" data-pair="x">12em</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="4" data-pair="y">var(--variable)</span>` +
            `</span>` +
            `, ` +
            `<span class="inspector-shape-point" data-point="5">` +
              `<span class="inspector-shape-point" data-point="5" data-pair="x">100%</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="5" data-pair="y">100%</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>` +
        ` margin-box`,
    },
    {
      desc: "Invalid polygon shape",
      definition: "polygon(0px 0px 100px 20px, 20% 20%)",
      markup: "polygon(0px 0px 100px 20px, 20% 20%)",
    },
    {
      desc: "Circle shape with all arguments",
      definition: "circle(25% at\n 30% 200px) border-box",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `<span class="inspector-shape-point" data-point="radius">25%</span>` +
            ` at\n ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">30%</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">200px</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>` +
        ` border-box`,
    },
    {
      desc: "Circle shape with only one center",
      definition: "circle(25em at 40%)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `<span class="inspector-shape-point" data-point="radius">25em</span>` +
            ` at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">40%</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with no radius",
      definition: "circle(at 30% 40%)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">30%</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">40%</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with no radius and keyword position",
      definition: "circle(at right center)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">right</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">center</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with no radius and 4 positions",
      definition: "circle(at left 10px top 15px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">left</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center">10px</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">top</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center">15px</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with no center",
      definition: "circle(12em)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `<span class="inspector-shape-point" data-point="radius">12em</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with no center and keyword radius size",
      definition: "circle(farthest-side)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `<span class="inspector-shape-point" data-point="radius">farthest-side</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with no center and computed radius size",
      definition: "circle(calc(10% + 12em))",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `<span class="inspector-shape-point" data-point="radius">calc(10% + 12em)</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with computed position",
      definition: "circle(25% at calc(30% + 1px) calc(200px - 10%))",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `<span class="inspector-shape-point" data-point="radius">25%</span>` +
            ` at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">calc(30% + 1px)</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">calc(200px - 10%)</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with no arguments",
      definition: "circle()",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle()` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Circle shape with no space before at",
      definition: "circle(25%at 30% 30%)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `circle(` +
            `<span class="inspector-shape-point" data-point="radius">25%</span>` +
            `at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">30%</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">30%</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "CIRCLE",
      definition: "CIRCLE(12em)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `CIRCLE(` +
            `<span class="inspector-shape-point" data-point="radius">12em</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Invalid circle shape",
      definition: "circle(25%at30%30%)",
      markup: "circle(25%at30%30%)",
    },
    {
      desc: "Ellipse shape with all arguments",
      definition: "ellipse(200px 10em at 25% 120px) content-box",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `ellipse(` +
            `<span class="inspector-shape-point" data-point="rx">200px</span>` +
            ` ` +
            `<span class="inspector-shape-point" data-point="ry">10em</span>` +
            ` at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">25%</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">120px</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>` +
        ` content-box`,
    },
    {
      desc: "Ellipse shape with only one center",
      definition: "ellipse(200px 10% at 120px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `ellipse(` +
            `<span class="inspector-shape-point" data-point="rx">200px</span>` +
            ` ` +
            `<span class="inspector-shape-point" data-point="ry">10%</span>` +
            ` at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">120px</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Ellipse shape with no radius",
      definition: "ellipse(at 25% 120px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `ellipse(` +
            `at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">25%</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">120px</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Ellipse shape with no center",
      definition: "ellipse(200px\n10em)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `ellipse(` +
            `<span class="inspector-shape-point" data-point="rx">200px</span>` +
            `\n` +
            `<span class="inspector-shape-point" data-point="ry">10em</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Ellipse shape with no center and keyword radii",
      definition: "ellipse(farthest-side closest-side)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `ellipse(` +
            `<span class="inspector-shape-point" data-point="rx">farthest-side</span>` +
            ` ` +
            `<span class="inspector-shape-point" data-point="ry">closest-side</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Ellipse shape with computed position and radiis",
      definition:
        "ellipse(calc(25% + 1px) calc(50% - 2px) at calc(30% + 1px) calc(200px - 10%))",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `ellipse(` +
            `<span class="inspector-shape-point" data-point="rx">calc(25% + 1px)</span>` +
            ` ` +
            `<span class="inspector-shape-point" data-point="ry">calc(50% - 2px)</span>` +
            ` at ` +
            `<span class="inspector-shape-point" data-point="center">` +
              `<span class="inspector-shape-point" data-point="center" data-pair="x">calc(30% + 1px)</span>` +
              ` ` +
              `<span class="inspector-shape-point" data-point="center" data-pair="y">calc(200px - 10%)</span>` +
            `</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Ellipse shape with no arguments",
      definition: "ellipse()",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `ellipse()` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "ELLIPSE()",
      definition: "ELLIPSE(200px 10em)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `ELLIPSE(` +
            `<span class="inspector-shape-point" data-point="rx">200px</span>` +
            ` ` +
            `<span class="inspector-shape-point" data-point="ry">10em</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Invalid ellipse shape",
      definition: "ellipse(200px100px at 30$ 20%)",
      markup: "ellipse(200px100px at 30$ 20%)",
    },
    {
      desc: "Inset shape with 4 arguments",
      definition: "inset(200px 100px\n 30%15%)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `inset(` +
            `<span class="inspector-shape-point top">200px</span>` +
            ` ` +
            `<span class="inspector-shape-point right">100px</span>` +
            `\n ` +
            `<span class="inspector-shape-point bottom">30%</span>` +
            `<span class="inspector-shape-point left">15%</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Inset shape with 3 arguments",
      definition: "inset(200px 100px 15%)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `inset(` +
            `<span class="inspector-shape-point top">200px</span>` +
            ` ` +
            `<span class="inspector-shape-point right left">100px</span>` +
            ` ` +
            `<span class="inspector-shape-point bottom">15%</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Inset shape with 2 arguments",
      definition: "inset(200px 100px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `inset(` +
            `<span class="inspector-shape-point top bottom">200px</span>` +
            ` ` +
            `<span class="inspector-shape-point right left">100px</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Inset shape with 1 argument",
      definition: "inset(200px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `inset(` +
            `<span class="inspector-shape-point top right bottom left">200px</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Inset shape with 0 arguments",
      definition: "inset()",
      markup: "inset()",
    },
    {
      desc: "INSET()",
      definition: "INSET(200px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `INSET(` +
            `<span class="inspector-shape-point top right bottom left">200px</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "offset-path property with inset shape value",
      property: "offset-path",
      definition: "inset(200px)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `inset(` +
            `<span class="inspector-shape-point top right bottom left">200px</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Inset shape with nested function",
      definition: "inset(200px calc(100px + 10%) 15%)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `inset(` +
            `<span class="inspector-shape-point top">200px</span>` +
            ` ` +
            `<span class="inspector-shape-point right left">calc(100px + 10%)</span>` +
            ` ` +
            `<span class="inspector-shape-point bottom">15%</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      desc: "Inset shape with round keyword",
      definition: "inset(4rem round 1rem 2rem 3rem 4rem)",
      markup:
        // prettier-ignore
        `<span>` +
          `<button class="inspector-shape-swatch"></button>` +
          `<span class="inspector-shape">` +
            `inset(` +
            `<span class="inspector-shape-point top right bottom left">4rem</span>` +
            ` round 1rem 2rem 3rem 4rem` +
            `)` +
          `</span>` +
        `</span>`,
    },
  ];

  for (const { desc, definition, property = "clip-path", markup } of tests) {
    info(desc);
    const frag = parser.parseCssProperty(property, definition, {
      shapeClass: "inspector-shape",
      shapeSwatchClass: "inspector-shape-swatch",
    });
    const el = frag.ownerDocument.createElement("span");
    el.append(frag);
    is(el.innerHTML, markup, desc + " markup");
    is(el.textContent, definition, desc + " text content");

    const swatchlessFrag = parser.parseCssProperty(property, definition, {
      shapeClass: "inspector-shape",
    });
    is(
      swatchlessFrag.querySelector("button"),
      null,
      `${desc} does not have a swatch button when shapeSwatchClass option is not passed`
    );
  }
}

function getJumpToVariableButton(varName) {
  return `<button class="ruleview-variable-link jump-definition" data-variable-name="${varName}" title="Jump to variable definition"></button>`;
}

function testParseVariable(doc, parser) {
  const TESTS = [
    {
      text: "var(--seen)",
      variables: { "--seen": "chartreuse" },
      expected:
        // prettier-ignore
        '<span data-color="chartreuse">' +
          "<span>var(" +
            `<span data-variable="chartreuse">--seen${getJumpToVariableButton("--seen")}</span>)` +
          "</span>" +
        "</span>",
    },
    {
      text: "var(--seen)",
      variables: {
        "--seen": { value: "var(--base)", computedValue: "1em" },
      },
      expected:
        // prettier-ignore
        "<span>var(" +
          `<span data-variable="var(--base)" data-variable-computed="1em">--seen${getJumpToVariableButton("--seen")}</span>)` +
        "</span>",
    },
    {
      text: "var(--not-seen)",
      variables: {},
      expected:
        // prettier-ignore
        "<span>var(" +
          '<span class="unmatched-class" data-variable="--not-seen is not set">--not-seen</span>' +
        ")</span>",
    },
    {
      text: "var(--seen, seagreen)",
      variables: { "--seen": "chartreuse" },
      expected:
        // prettier-ignore
        '<span data-color="chartreuse">' +
          "<span>var(" +
            `<span data-variable="chartreuse">--seen${getJumpToVariableButton("--seen")}</span>` +
            `,` +
            '<span class="unmatched-class"> ' +
              '<span data-color="seagreen">' +
                "<span>seagreen</span>" +
              "</span>" +
            "</span>)" +
          "</span>" +
        "</span>",
    },
    {
      text: "var(--not-seen, var(--seen))",
      variables: { "--seen": "chartreuse" },
      expected:
        // prettier-ignore
        `<span data-color=" chartreuse">` +
          "<span>var(" +
            '<span class="unmatched-class" data-variable="--not-seen is not set">--not-seen</span>'+
            ',' +
            "<span> " +
              '<span data-color="chartreuse">' +
                "<span>var(" +
                  `<span data-variable="chartreuse">--seen${getJumpToVariableButton("--seen")}</span>)` +
                "</span>" +
              "</span>" +
            "</span>)" +
          "</span>" +
        "</span>",
    },
    {
      text: "color-mix(in srgb, var(--x), purple)",
      variables: { "--x": "yellow" },
      expected:
        // prettier-ignore
        `<span data-color=\"color-mix(in srgb, yellow, purple)\" class=\"color-swatch-container\">` +
          `<span class=\"test-class\" style=\"background-color:color-mix(in srgb, yellow, purple)\" tabindex=\"0\" role=\"button\"></span>` +
          `<span>` +
            `color-mix(in srgb, ` +
            `<span data-color="yellow" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:yellow" tabindex="0" role="button" data-color-function="color-mix">` +
              `</span>` +
              `<span>var(<span data-variable="yellow">--x${getJumpToVariableButton("--x")}</span>)</span>` +
            `</span>` +
            `, ` +
            `<span data-color="purple" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:purple" tabindex="0" role="button" data-color-function="color-mix">` +
              `</span>` +
              `<span>purple</span>` +
            `</span>` +
            // closing `color-mix()`
            `)` +
          `</span>` +
        `</span>`,
      parserExtraOptions: {
        colorSwatchClass: COLOR_TEST_CLASS,
      },
    },
    {
      text: "light-dark(var(--light), var(--dark))",
      variables: { "--light": "yellow", "--dark": "gold" },
      expected:
        // prettier-ignore
        `light-dark(` +
        `<span data-color="yellow" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:yellow" tabindex="0" role="button" data-color-function="light-dark">` +
          `</span>` +
          `<span>var(<span data-variable="yellow">--light${getJumpToVariableButton("--light")}</span>)</span>` +
        `</span>` +
        `, ` +
        `<span data-color="gold" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:gold" tabindex="0" role="button" data-color-function="light-dark">` +
          `</span>` +
          `<span>var(<span data-variable="gold">--dark${getJumpToVariableButton("--dark")}</span>)</span>` +
        `</span>` +
        `)`,
      parserExtraOptions: {
        colorSwatchClass: COLOR_TEST_CLASS,
      },
    },
    {
      text: "1px solid var(--seen, seagreen)",
      // See Bug 1911974
      skipVariableDeclarationTest: true,
      variables: { "--seen": "chartreuse" },
      expected:
        // prettier-ignore
        '1px solid ' +
        '<span data-color="chartreuse">' +
          "<span>var(" +
            `<span data-variable="chartreuse">--seen${getJumpToVariableButton("--seen")}</span>` +
            `,` +
            '<span class="unmatched-class"> ' +
              '<span data-color="seagreen">' +
                "<span>seagreen</span>" +
              "</span>" +
            "</span>)" +
          "</span>" +
        "</span>",
    },
    {
      text: "1px solid var(--not-seen, seagreen)",
      // See Bug 1911975
      skipVariableDeclarationTest: true,
      variables: {},
      expected:
        // prettier-ignore
        `1px solid ` +
        `<span data-color=" seagreen">` +
          `<span>var(` +
            `<span class="unmatched-class" data-variable="--not-seen is not set">--not-seen</span>` +
            `,` +
            `<span> ` +
              `<span data-color="seagreen">` +
                `<span>seagreen</span>` +
              `</span>` +
            `</span>)` +
          `</span>` +
        `</span>`,
    },
    {
      text: "rgba(var(--r), 0, 0, var(--a))",
      variables: { "--r": "255", "--a": "0.5" },
      expected:
        // prettier-ignore
        '<span data-color="rgba(255, 0, 0, 0.5)">' +
          "<span>rgba("+
            "<span>" +
              `var(<span data-variable="255">--r${getJumpToVariableButton("--r")}</span>)` +
            "</span>, 0, 0, " +
            "<span>" +
              `var(<span data-variable="0.5">--a${getJumpToVariableButton("--a")}</span>)` +
            "</span>" +
          ")</span>" +
        "</span>",
    },
    {
      text: "rgba(from var(--base) r g 0 / calc(var(--a) * 0.5))",
      variables: { "--base": "red", "--a": "0.8" },
      expected:
        // prettier-ignore
        '<span data-color="rgba(from red r g 0 / calc(0.8 * 0.5))">' +
          "<span>rgba("+
            "from " +
            `<span data-color="red">` +
              "<span>" +
                `var(<span data-variable="red">--base${getJumpToVariableButton("--base")}</span>)` +
              "</span>" +
            "</span>" +
            " r g 0 / " +
            "calc(" +
            "<span>" +
              `var(<span data-variable="0.8">--a${getJumpToVariableButton("--a")}</span>)` +
            "</span>" +
            " * 0.5)" +
          ")</span>" +
        "</span>",
    },
    {
      text: "rgb(var(--not-seen, 255), 0, 0)",
      variables: {},
      expected:
        // prettier-ignore
        '<span data-color="rgb( 255, 0, 0)">' +
          "<span>rgb("+
            "<span>var(" +
              `<span class="unmatched-class" data-variable="--not-seen is not set">--not-seen</span>` +
              `,` +
              `<span> 255</span>` +
            ")</span>, 0, 0" +
          ")</span>" +
        "</span>",
    },
    {
      text: "rgb(var(--not-seen), 0, 0)",
      variables: {},
      expected:
        // prettier-ignore
        `rgb(` +
          `<span>` +
            `var(` +
              `<span class="unmatched-class" data-variable="--not-seen is not set">` +
                `--not-seen` +
              `</span>` +
            `)` +
          `</span>` +
          `, 0, 0` +
        `)`,
    },
    {
      text: "var(--registered)",
      variables: {
        "--registered": {
          value: "chartreuse",
          registeredProperty: {
            syntax: "<color>",
            inherits: true,
            initialValue: "hotpink",
          },
        },
      },
      expected:
        // prettier-ignore
        '<span data-color="chartreuse">' +
          "<span>var(" +
            '<span ' +
              'data-variable="chartreuse" ' +
              'data-registered-property-initial-value="hotpink" ' +
              'data-registered-property-syntax="&lt;color&gt;" ' +
              'data-registered-property-inherits="true"' +
            `>--registered${getJumpToVariableButton("--registered")}</span>)` +
          "</span>" +
        "</span>",
    },
    {
      text: "var(--registered-universal)",
      variables: {
        "--registered-universal": {
          value: "chartreuse",
          registeredProperty: {
            syntax: "*",
            inherits: false,
          },
        },
      },
      expected:
        // prettier-ignore
        '<span data-color="chartreuse">' +
          "<span>var(" +
            '<span ' +
              'data-variable="chartreuse" ' +
              'data-registered-property-syntax="*" ' +
              'data-registered-property-inherits="false"' +
            `>--registered-universal${getJumpToVariableButton("--registered-universal")}</span>)` +
          "</span>" +
        "</span>",
    },
    {
      text: "var(--x)",
      variables: {
        "--x": "light-dark(red, blue)",
      },
      parserExtraOptions: {
        isDarkColorScheme: false,
      },
      expected: `<span>var(<span data-variable="light-dark(red, blue)">--x${getJumpToVariableButton("--x")}</span>)</span>`,
    },
    {
      text: "var(--x)",
      variables: {
        "--x": "color-mix(in srgb, red 50%, blue)",
      },
      parserExtraOptions: {
        isDarkColorScheme: false,
      },
      expected:
        // prettier-ignore
        '<span data-color="color-mix(in srgb, red 50%, blue)">' +
          '<span>var(' +
            `<span data-variable="color-mix(in srgb, red 50%, blue)">--x${getJumpToVariableButton("--x")}</span>` +
          ')</span>' +
        '</span>',
    },
    {
      text: "var(--x)",
      variables: {
        "--x": "contrast-color(blue)",
      },
      parserExtraOptions: {
        isDarkColorScheme: false,
      },
      expected:
        // prettier-ignore
        '<span data-color="contrast-color(blue)">' +
          '<span>var(' +
            `<span data-variable="contrast-color(blue)">--x${getJumpToVariableButton("--x")}</span>` +
          ')</span>' +
        '</span>',
    },
    {
      text: "var(--refers-empty)",
      variables: {
        "--refers-empty": { value: "var(--empty)", computedValue: "" },
      },
      expected:
        // prettier-ignore
        "<span>var(" +
          `<span data-variable="var(--empty)" data-variable-computed="">--refers-empty${getJumpToVariableButton("--refers-empty")}</span>)` +
        "</span>",
    },
    {
      text: "hsl(50, 70%, var(--foo))",
      variables: {
        "--foo": "40%",
      },
      expected:
        // prettier-ignore
        `<span data-color="hsl(50, 70%, 40%)">` +
          `<span>`+
            `hsl(50, 70%, ` +
            `<span>` +
              `var(` +
                `<span data-variable="40%">--foo${getJumpToVariableButton("--foo")}</span>` +
              `)` +
            `</span>)` +
          `</span>` +
        `</span>`,
    },
    {
      text: "var(--bar)",
      variables: {
        "--foo": "40%",
        "--bar": "hsl(50, 70%, var(--foo))",
      },
      expected:
        // prettier-ignore
        `<span data-color="hsl(50, 70%, 40%)">` +
          `<span>` +
            `var(` +
              `<span data-variable="hsl(50, 70%, var(--foo))" data-variable-computed="hsl(50, 70%, 40%)">--bar${getJumpToVariableButton("--bar")}</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      text: "var(--primary)",
      variables: {
        "--primary": "hsl(10, 100%, var(--fur))",
        "--fur": "var(--bar)",
        "--bar": "var(--foo)",
        "--foo": "50%",
      },
      expected:
        // prettier-ignore
        `<span data-color="hsl(10, 100%, 50%)">` +
          `<span>` +
            `var(` +
              `<span data-variable="hsl(10, 100%, var(--fur))" data-variable-computed="hsl(10, 100%, 50%)">--primary${getJumpToVariableButton("--primary")}</span>` +
            `)` +
          `</span>` +
        `</span>`,
    },
    {
      text: "oklch(var(--fur) 20 var(--boo))",
      variables: {
        "--fur": "var(--baz)",
        "--baz": "var(--foo)",
        "--foo": "10",
        "--boo": "30",
      },
      expected:
        // prettier-ignore
        `<span data-color="oklch(10 20 30)">` +
          `<span>oklch(` +
            `<span>` +
              `var(` +
                `<span data-variable="var(--baz)" data-variable-computed="10">--fur${getJumpToVariableButton("--fur")}</span>` +
              `)` +
            `</span>` +
            ` 20 ` +
            `<span>` +
              `var(` +
                `<span data-variable="30">--boo${getJumpToVariableButton("--boo")}</span>` +
              `)` +
            `</span>` +
          `)</span>` +
        `</span>`,
    },
    {
      text: "var(--x)",
      variables: {
        "--x": "10px",
      },
      parserExtraOptions: {
        showJumpToVariableButton: false,
      },
      // This shouldn't have a Jump to variable button
      expected: `<span>var(<span data-variable="10px">--x</span>)</span>`,
    },
    {
      // var() with spaces between params/parenthesis
      text: "var(  --foo  ,  500px  )",
      variables: { "--foo": "1px" },
      expected:
        // prettier-ignore
        `<span>` +
          `var(  ` +
            `<span data-variable="1px">--foo${getJumpToVariableButton("--foo")}</span>` +
            `  ,` +
            `<span class="unmatched-class">  500px</span>` +
          `  )` +
        `</span>`,
    },
    {
      // multiline var()
      text: "var(\n--foo, 500px\n)",
      variables: { "--foo": "1px" },
      expected:
        // prettier-ignore
        `<span>` +
          `var(\n` +
            `<span data-variable="1px">--foo${getJumpToVariableButton("--foo")}</span>` +
            `,` +
            `<span class="unmatched-class"> 500px</span>` +
          `\n)` +
        `</span>`,
    },
  ];

  const target = doc.querySelector("div");

  const VAR_NAME_TO_DEFINE = "--test-parse-variable";
  for (const test of TESTS) {
    // VAR_NAME_TO_DEFINE is used to test parsing the test.text if it's set for a
    // variable declaration, so it shouldn't be set in test.variables to avoid
    // messing with the test results.
    if (VAR_NAME_TO_DEFINE in test.variables) {
      throw new Error(`${VAR_NAME_TO_DEFINE} shouldn't be set in variables`);
    }

    // Also set the variable we're going to define, so its value can be computed as well
    const variables = {
      ...(test.variables || {}),
      [VAR_NAME_TO_DEFINE]: test.text,
    };
    // Set the variables to an element so we can get their computed values
    for (const [varName, varData] of Object.entries(variables)) {
      doc.body.style.setProperty(
        varName,
        typeof varData === "string" ? varData : varData.value
      );
    }

    const getVariableData = function (varName) {
      if (typeof variables[varName] === "string") {
        const value = variables[varName];
        const computedValue = getComputedStyle(doc.body).getPropertyValue(
          varName
        );
        return { value, computedValue };
      }

      return variables[varName] || {};
    };

    const frag = parser.parseCssProperty("color", test.text, {
      getVariableData,
      unmatchedClass: "unmatched-class",
      ...(test.parserExtraOptions || {}),
    });

    target.appendChild(frag);

    is(
      target.innerHTML,
      test.expected,
      `"color: ${test.text}" is parsed as expected`
    );

    target.innerHTML = "";

    if (test.skipVariableDeclarationTest) {
      continue;
    }

    const varFrag = parser.parseCssProperty(
      "--test-parse-variable",
      test.text,
      {
        getVariableData,
        unmatchedClass: "unmatched-class",
        ...(test.parserExtraOptions || {}),
      }
    );

    target.appendChild(varFrag);

    is(
      target.innerHTML,
      test.expected,
      `"--test-parse-variable: ${test.text}" is parsed as expected`
    );

    target.innerHTML = "";

    // Remove the variables to an element so we can get their computed values
    for (const varName in variables || {}) {
      doc.body.style.removeProperty(varName);
    }
  }
}

function testParseColorVariable(doc, parser) {
  const testCategories = [
    {
      desc: "Test for CSS variable defining color",
      tests: [
        makeColorTest("--test-var", "lime", [{ name: "lime" }]),
        makeColorTest("--test-var", "#000", [{ name: "#000" }]),
      ],
    },
    {
      desc: "Test for CSS variable not defining color",
      tests: [
        makeColorTest("--foo", "something", ["something"]),
        makeColorTest("--bar", "Arial Black", ["Arial Black"]),
        makeColorTest("--baz", "10vmin", ["10vmin"]),
      ],
    },
    {
      desc: "Test for non CSS variable defining color",
      tests: [
        makeColorTest("non-css-variable", "lime", ["lime"]),
        makeColorTest("-non-css-variable", "#000", ["#000"]),
      ],
    },
  ];

  for (const category of testCategories) {
    info(category.desc);

    for (const test of category.tests) {
      info(test.desc);
      const target = doc.querySelector("div");

      const frag = parser.parseCssProperty(test.name, test.value, {
        colorSwatchClass: COLOR_TEST_CLASS,
      });

      target.appendChild(frag);

      is(
        target.innerHTML,
        test.expected,
        `The parsed result for '${test.name}: ${test.value}' is correct`
      );

      target.innerHTML = "";
    }
  }
}

function testParseFontFamily(doc, parser) {
  info("Test font-family parsing");
  const tests = [
    {
      desc: "No fonts",
      definition: "",
      families: [],
    },
    {
      desc: "List of fonts",
      definition: "Arial,Helvetica,sans-serif",
      families: ["Arial", "Helvetica", "sans-serif"],
    },
    {
      desc: "Fonts with spaces",
      definition: "Open Sans",
      families: ["Open Sans"],
    },
    {
      desc: "Quoted fonts",
      definition: "\"Arial\",'Open Sans'",
      families: ["Arial", "Open Sans"],
    },
    {
      desc: "Fonts with extra whitespace",
      definition: " Open  Sans  ",
      families: ["Open  Sans"],
    },
  ];

  const textContentTests = [
    {
      desc: "No whitespace between fonts",
      definition: "Arial,Helvetica,sans-serif",
      output: "Arial,Helvetica,sans-serif",
    },
    {
      desc: "Whitespace between fonts",
      definition: "Arial ,  Helvetica,   sans-serif",
      output: "Arial ,  Helvetica,   sans-serif",
    },
    {
      desc: "Whitespace before first font trimmed",
      definition: "  Arial,Helvetica,sans-serif",
      output: "Arial,Helvetica,sans-serif",
    },
    {
      desc: "Whitespace after last font trimmed",
      definition: "Arial,Helvetica,sans-serif  ",
      output: "Arial,Helvetica,sans-serif",
    },
    {
      desc: "Whitespace between quoted fonts",
      definition: "'Arial' ,  \"Helvetica\" ",
      output: "'Arial' ,  \"Helvetica\"",
    },
    {
      desc: "Whitespace within font preserved",
      definition: "'  Ari al '",
      output: "'  Ari al '",
    },
  ];

  for (const { desc, definition, families } of tests) {
    info(desc);
    const frag = parser.parseCssProperty("font-family", definition, {
      fontFamilyClass: "ruleview-font-family",
    });
    const spans = frag.querySelectorAll(".ruleview-font-family");

    is(spans.length, families.length, desc + " span count");
    for (let i = 0; i < spans.length; i++) {
      is(spans[i].textContent, families[i], desc + " span contents");
    }
  }

  info("Test font-family text content");
  for (const { desc, definition, output } of textContentTests) {
    info(desc);
    const frag = parser.parseCssProperty("font-family", definition, {});
    is(frag.textContent, output, desc + " text content matches");
  }

  info("Test font-family with custom properties");
  const frag = parser.parseCssProperty(
    "font-family",
    "MonoLisa, var(--family, Georgia black, 'Helvetica Neue', serif), monospace !important",
    {
      getVariableData: () => ({}),
      unmatchedClass: "unmatched-class",
      fontFamilyClass: "ruleview-font-family",
    }
  );
  const target = doc.createElement("div");
  target.appendChild(frag);
  is(
    target.innerHTML,
    // prettier-ignore
    `<span class="ruleview-font-family">MonoLisa</span>` +
    `, ` +
    `<span>var(` +
      `<span class="unmatched-class" data-variable="--family is not set">` +
        `--family` +
      `</span>` +
      `,` +
      `<span> ` +
        `<span class="ruleview-font-family">Georgia black</span>` +
        `, ` +
        `'<span class="ruleview-font-family">Helvetica Neue</span>'` +
        `, ` +
        `<span class="ruleview-font-family">serif</span>` +
      `</span>)` +
    `</span>` +
    `, ` +
    `<span class="ruleview-font-family">monospace</span>` +
    ` !important`,
    "Got expected output for font-family with custom properties"
  );
}

function testParseLightDark(doc, parser) {
  const TESTS = [
    {
      message:
        "Not passing isDarkColorScheme doesn't add unmatched classes to parameters",
      propertyName: "color",
      propertyValue: "light-dark(red, blue)",
      expected:
        // prettier-ignore
        `light-dark(` +
        `<span data-color="red" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>red</span>` +
        `</span>, ` +
        `<span data-color="blue" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>blue</span>` +
        `</span>` +
      `)`,
    },
    {
      message: "in light mode, the second parameter gets the unmatched class",
      propertyName: "color",
      propertyValue: "light-dark(red, blue)",
      isDarkColorScheme: false,
      expected:
        // prettier-ignore
        `light-dark(` +
        `<span data-color="red" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>red</span>` +
        `</span>, ` +
        `<span data-color="blue" class="color-swatch-container unmatched-class">` +
          `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>blue</span>` +
        `</span>` +
      `)`,
    },
    {
      message: "in dark mode, the first parameter gets the unmatched class",
      propertyName: "color",
      propertyValue: "light-dark(red, blue)",
      isDarkColorScheme: true,
      expected:
        // prettier-ignore
        `light-dark(` +
        `<span data-color="red" class="color-swatch-container unmatched-class">` +
          `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>red</span>` +
        `</span>, ` +
        `<span data-color="blue" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>blue</span>` +
        `</span>` +
      `)`,
    },
    {
      message: "light-dark gets parsed as expected in shorthands in light mode",
      propertyName: "border",
      propertyValue: "1px solid light-dark(red, blue)",
      isDarkColorScheme: false,
      expected:
        // prettier-ignore
        `1px solid light-dark(` +
        `<span data-color="red" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>red</span>` +
        `</span>, ` +
        `<span data-color="blue" class="color-swatch-container unmatched-class">` +
          `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>blue</span>` +
        `</span>` +
      `)`,
    },
    {
      message: "light-dark gets parsed as expected in shorthands in dark mode",
      propertyName: "border",
      propertyValue: "1px solid light-dark(red, blue)",
      isDarkColorScheme: true,
      expected:
        // prettier-ignore
        `1px solid light-dark(` +
        `<span data-color="red" class="color-swatch-container unmatched-class">` +
          `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>red</span>` +
        `</span>, ` +
        `<span data-color="blue" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
          `<span>blue</span>` +
        `</span>` +
      `)`,
    },
    {
      message: "Nested light-dark gets parsed as expected in light mode",
      propertyName: "background",
      propertyValue:
        "linear-gradient(45deg, light-dark(red, blue), light-dark(pink, cyan))",
      isDarkColorScheme: false,
      expected:
        // prettier-ignore
        `linear-gradient(` +
          `<span data-angle="45deg"><span>45deg</span></span>, ` +
          `light-dark(` +
            `<span data-color="red" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>`+
              `<span>red</span>`+
            `</span>, `+
            `<span data-color="blue" class="color-swatch-container unmatched-class">` +
              `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
              `<span>blue</span>` +
            `</span>` +
          `), ` +
          `light-dark(` +
            `<span data-color="pink" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:pink" tabindex="0" role="button" data-color-function="light-dark"></span>` +
              `<span>pink</span>` +
            `</span>, ` +
            `<span data-color="cyan" class="color-swatch-container unmatched-class">` +
              `<span class="test-class" style="background-color:cyan" tabindex="0" role="button" data-color-function="light-dark"></span>` +
              `<span>cyan</span>` +
            `</span>` +
          `)` +
        `)`,
    },
    {
      message: "Nested light-dark gets parsed as expected in dark mode",
      propertyName: "background",
      propertyValue:
        "linear-gradient(33deg, light-dark(red, blue), light-dark(pink, cyan))",
      isDarkColorScheme: true,
      expected:
        // prettier-ignore
        `linear-gradient(` +
          `<span data-angle="33deg"><span>33deg</span></span>, ` +
          `light-dark(` +
            `<span data-color="red" class="color-swatch-container unmatched-class">` +
              `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>`+
              `<span>red</span>`+
            `</span>, `+
            `<span data-color="blue" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
              `<span>blue</span>` +
            `</span>` +
          `), ` +
          `light-dark(` +
            `<span data-color="pink" class="color-swatch-container unmatched-class">` +
              `<span class="test-class" style="background-color:pink" tabindex="0" role="button" data-color-function="light-dark"></span>` +
              `<span>pink</span>` +
            `</span>, ` +
            `<span data-color="cyan" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:cyan" tabindex="0" role="button" data-color-function="light-dark"></span>` +
              `<span>cyan</span>` +
            `</span>` +
          `)` +
        `)`,
    },
    {
      message:
        "in light mode, the second parameter gets the unmatched class when it's a variable",
      propertyName: "color",
      propertyValue: "light-dark(var(--x), var(--y))",
      isDarkColorScheme: false,
      variables: { "--x": "red", "--y": "blue" },
      expected:
        // prettier-ignore
        `light-dark(` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>var(` +
              `<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>` +
            `)</span>` +
          `</span>, ` +
          `<span data-color="blue" class="color-swatch-container unmatched-class">` +
            `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>var(` +
              `<span data-variable="blue">--y${getJumpToVariableButton("--y")}</span>` +
            `)</span>` +
          `</span>` +
        `)`,
    },
    {
      message:
        "in light mode, the second parameter gets the unmatched class when some param are not parsed",
      propertyName: "color",
      // Using `notacolor` so we don't get a wrapping Node for it (contrary to colors).
      // The value is still valid at parse time since we're using a variable,
      // so the OutputParser will actually parse the different parts
      propertyValue: "light-dark(var(--x),notacolor)",
      isDarkColorScheme: false,
      variables: { "--x": "red" },
      expected:
        // prettier-ignore
        `light-dark(` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>` +
              `var(<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>)` +
            `</span>` +
          `</span>,` +
          `<span class="unmatched-class">notacolor</span>` +
        `)`,
    },
    {
      message:
        "in dark mode, the first parameter gets the unmatched class when some param are not parsed",
      propertyName: "color",
      // Using `notacolor` so we don't get a wrapping Node for it (contrary to colors).
      // The value is still valid at parse time since we're using a variable,
      // so the OutputParser will actually parse the different parts
      propertyValue: "light-dark(notacolor,var(--x))",
      isDarkColorScheme: true,
      variables: { "--x": "red" },
      expected:
        // prettier-ignore
        `light-dark(` +
          `<span class="unmatched-class">notacolor</span>,` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>` +
              `var(<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>)` +
            `</span>` +
          `</span>` +
        `)`,
    },
    {
      message:
        "in light mode, the second parameter gets the unmatched class, comments are stripped out and whitespace are preserved",
      propertyName: "color",
      propertyValue:
        "light-dark( /* 1st param */ var(--x) /* delim */ , /*  2nd param */ notacolor /* delim */ )",
      isDarkColorScheme: false,
      variables: { "--x": "red" },
      expected:
        // prettier-ignore
        `light-dark(  ` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>` +
              `var(<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>)` +
            `</span>` +
          `</span>  ,  ` +
          `<span class="unmatched-class">notacolor</span>  ` +
        `)`,
    },
    {
      message:
        "in dark mode, the first parameter gets the unmatched class, comments are stripped out and whitespace are preserved",
      propertyName: "color",
      propertyValue:
        "light-dark( /* 1st param */ notacolor /* delim */ , /*  2nd param */ var(--x) /* delim */ )",
      isDarkColorScheme: true,
      variables: { "--x": "red" },
      expected:
        // prettier-ignore
        `light-dark(  ` +
          `<span class="unmatched-class">notacolor</span>  ,  ` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>` +
              `var(<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>)` +
            `</span>` +
          `</span>  ` +
        `)`,
    },
    {
      message:
        "in light mode with a single parameter, we don't strike through any parameter (TODO wrap with IACVT - Bug 1910845)",
      propertyName: "color",
      propertyValue: "light-dark(var(--x))",
      isDarkColorScheme: false,
      variables: { "--x": "red" },
      expected:
        // prettier-ignore
        `light-dark(` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>` +
              `var(<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>)` +
            `</span>` +
          `</span>` +
        `)`,
    },
    {
      message:
        "in dark mode with a single parameter, we don't strike through any parameter (TODO wrap with IACVT - Bug 1910845)",
      propertyName: "color",
      propertyValue: "light-dark(var(--x))",
      isDarkColorScheme: true,
      variables: { "--x": "red" },
      expected:
        // prettier-ignore
        `light-dark(` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>` +
              `var(<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>)` +
            `</span>` +
          `</span>` +
        `)`,
    },
    {
      message:
        "in light mode with 3 parameters, we don't strike through any parameter (TODO wrap with IACVT - Bug 1910845)",
      propertyName: "color",
      propertyValue: "light-dark(var(--x),a,b)",
      isDarkColorScheme: false,
      variables: { "--x": "red" },
      expected:
        // prettier-ignore
        `light-dark(` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>` +
              `var(<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>)` +
            `</span>` +
          `</span>,a,b` +
        `)`,
    },
    {
      message:
        "in dark mode with 3 parameters, we don't strike through any parameter (TODO wrap with IACVT - Bug 1910845)",
      propertyName: "color",
      propertyValue: "light-dark(var(--x),a,b)",
      isDarkColorScheme: true,
      variables: { "--x": "red" },
      expected:
        // prettier-ignore
        `light-dark(` +
          `<span data-color="red" class="color-swatch-container">` +
            `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>` +
              `var(<span data-variable="red">--x${getJumpToVariableButton("--x")}</span>)` +
            `</span>` +
          `</span>,a,b` +
        `)`,
    },
    {
      message:
        "in light mode with images, the second parameter gets the unmatched class",
      propertyName: "background-image",
      propertyValue: `light-dark(url("a.png"), url("b.png"))`,
      isDarkColorScheme: false,
      expected: `light-dark(url("a.png"), <span class="unmatched-class">url("b.png")</span>)`,
    },
    {
      message:
        "in dark mode with images, the first parameter gets the unmatched class",
      propertyName: "background-image",
      propertyValue: `light-dark(url("a.png"), url("b.png"))`,
      isDarkColorScheme: true,
      expected: `light-dark(<span class="unmatched-class">url("a.png")</span>, url("b.png"))`,
    },
    {
      message:
        "in light mode with gradients, the second parameter gets the unmatched class",
      propertyName: "background-image",
      propertyValue:
        "light-dark(linear-gradient(white, blue), linear-gradient(black, red))",
      isDarkColorScheme: false,
      expected:
        // prettier-ignore
        `light-dark(` +
          `linear-gradient(` +
            `<span data-color="white" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:white" tabindex="0" role="button" data-color-function="linear-gradient"></span>` +
              `<span>white</span>` +
            `</span>` +
            `, ` +
            `<span data-color="blue" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="linear-gradient"></span>` +
              `<span>blue</span>` +
            `</span>` +
          `)` +
          `, ` +
          `<span class="unmatched-class">` +
            `linear-gradient(` +
              `<span data-color="black" class="color-swatch-container">` +
                `<span class="test-class" style="background-color:black" tabindex="0" role="button" data-color-function="linear-gradient"></span>` +
                `<span>black</span>` +
              `</span>` +
              `, ` +
              `<span data-color="red" class="color-swatch-container">` +
                `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="linear-gradient"></span>` +
                `<span>red</span>` +
              `</span>` +
            `)` +
          `</span>` +
        `)`,
    },
    {
      message:
        "in dark mode with gradients, the first parameter gets the unmatched class",
      propertyName: "background-image",
      propertyValue:
        "light-dark(linear-gradient(white, blue), linear-gradient(black, red))",
      isDarkColorScheme: true,
      expected:
        // prettier-ignore
        `light-dark(` +
          `<span class="unmatched-class">` +
            `linear-gradient(` +
              `<span data-color="white" class="color-swatch-container">` +
                `<span class="test-class" style="background-color:white" tabindex="0" role="button" data-color-function="linear-gradient"></span>` +
                `<span>white</span>` +
              `</span>` +
              `, ` +
              `<span data-color="blue" class="color-swatch-container">` +
                `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="linear-gradient"></span>` +
                `<span>blue</span>` +
              `</span>` +
            `)` +
          `</span>` +
          `, ` +
          `linear-gradient(` +
            `<span data-color="black" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:black" tabindex="0" role="button" data-color-function="linear-gradient"></span>` +
              `<span>black</span>` +
            `</span>` +
            `, ` +
            `<span data-color="red" class="color-swatch-container">` +
              `<span class="test-class" style="background-color:red" tabindex="0" role="button" data-color-function="linear-gradient"></span>` +
              `<span>red</span>` +
            `</span>` +
          `)` +
        `)`,
    },
  ];

  for (const test of TESTS) {
    const frag = parser.parseCssProperty(
      test.propertyName,
      test.propertyValue,
      {
        isDarkColorScheme: test.isDarkColorScheme,
        unmatchedClass: "unmatched-class",
        colorSwatchClass: COLOR_TEST_CLASS,
        getVariableData: varName => {
          if (typeof test.variables[varName] === "string") {
            return { value: test.variables[varName] };
          }

          return test.variables[varName] || {};
        },
      }
    );

    const target = doc.querySelector("div");
    target.appendChild(frag);

    is(target.innerHTML, test.expected, test.message);
    target.innerHTML = "";
  }
}

function testParseAttr(doc, parser) {
  const TESTS = [
    {
      message:
        "Not passing getAttributeValue doesn't add unmatched classes to attribute name",
      propertyValue: "attr(unknown)",
      attributes: {},
      getAttributeValue: null,
      expected: `attr(unknown)`,
    },
    {
      message:
        "Passing known attribute doesn't add unmatched classes to attribute name",
      propertyValue: "attr(data-x)",
      attributes: { "data-x": "" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;&quot;">data-x</span>` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Passing unknown attribute adds unmatched classes to attribute name",
      propertyValue: "attr(data-x)",
      attributes: {},
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-x is not set">` +
          `<span class="inspector-attr-name">data-x</span>` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Passing unknown attribute adds unmatched classes to attribute name, not to fallback",
      propertyValue: `attr(data-x, "fallback")`,
      attributes: {},
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-x is not set">` +
          `<span class="inspector-attr-name">data-x</span>` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">"fallback"</span>` +
        `)`,
    },
    {
      message: "Passing known attribute adds unmatched classes to fallback",
      propertyValue: `attr(data-x, "fallback")`,
      attributes: { "data-x": "" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;&quot;">data-x</span>` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">"fallback"</span>` +
        `)`,
    },
    {
      message: "Checking attr() + spaces",
      propertyValue: `attr(  data-x  ,  "fallback"  )`,
      attributes: { "data-x": "" },
      // prettier-ignore
      expected:
        `attr(` +
          `  ` +
          `<span class="inspector-attr-param">` +
            `<span class="inspector-attr-name" data-attribute="&quot;&quot;">data-x</span>` +
          `</span>` +
          `  ,  ` +
          `<span class="inspector-attr-fallback unmatched-class">"fallback"</span>` +
        `  )`,
    },
    {
      message: "Modern attr() with known attribute and simple type",
      propertyValue: "attr(data-x raw-string)",
      attributes: { "data-x": "x" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;x&quot;">data-x</span>` +
          ` raw-string` +
        `</span>` +
        `)`,
    },
    {
      message: "Modern attr() with known attribute and type()",
      propertyName: "width",
      propertyValue: "attr(data-x type(<length> | <percentage>))",
      attributes: { "data-x": "10px" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;10px&quot;">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `)`,
    },
    {
      message: "Modern attr() with unknown attribute and simple type",
      propertyValue: "attr(data-x raw-string)",
      attributes: {},
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-x is not set">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` raw-string` +
        `</span>` +
        `)`,
    },
    {
      message: "Modern attr() with unknown attribute and type()",
      propertyValue: "attr(data-x type(<length> | <percentage>))",
      attributes: {},
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-x is not set">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with known attribute, simple type and simple fallback",
      propertyValue: `attr(data-x raw-string, "fallback")`,
      attributes: { "data-x": "12" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;12&quot;">data-x</span>` +
          ` raw-string` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">"fallback"</span>` +
        `)`,
    },
    {
      message: "Modern attr() with known attribute, type() and simple fallback",
      propertyName: "width",
      propertyValue: `attr(data-x type(<length> | <percentage>), "fallback")`,
      attributes: { "data-x": "10%" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;10%&quot;">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">"fallback"</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with unknown attribute, simple type and simple fallback",
      propertyValue: `attr(data-x raw-string, "fallback")`,
      attributes: {},
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-x is not set">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` raw-string` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">"fallback"</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with unknown attribute, type() and simple fallback",
      propertyValue: `attr(data-x type(<length> | <percentage>), "fallback")`,
      attributes: {},
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-x is not set">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">"fallback"</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with known attribute, simple type and nested attr() fallback",
      propertyValue: `attr(data-x raw-string, attr(data-y, "fallback"))`,
      attributes: { "data-x": "x", "data-y": "y" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;x&quot;">data-x</span>` +
          ` raw-string` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">` +
          `attr(` +
          `<span class="inspector-attr-param">` +
            `<span class="inspector-attr-name" data-attribute="&quot;y&quot;">data-y</span>` +
          `</span>` +
          `, ` +
          `<span class="inspector-attr-fallback unmatched-class">"fallback"</span>` +
          `)` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with known attribute, type() and nested attr() fallback",
      propertyValue: `attr(data-x type(<length> | <percentage>), attr(data-y, 300px))`,
      attributes: { "data-x": "20em" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;20em&quot;">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">` +
          `attr(` +
          `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-y is not set">` +
            `<span class="inspector-attr-name">data-y</span>` +
          `</span>` +
          `, ` +
          `<span class="inspector-attr-fallback">300px</span>` +
          `)` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with unknown attribute, simple type and nested attr() fallback",
      propertyValue: `attr(data-x raw-string, attr(data-y, "fallback"))`,
      attributes: { "data-y": "y" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-x is not set">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` raw-string` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">` +
          `attr(` +
          `<span class="inspector-attr-param">` +
            `<span class="inspector-attr-name" data-attribute="&quot;y&quot;">data-y</span>` +
          `</span>` +
          `, ` +
          `<span class="inspector-attr-fallback unmatched-class">"fallback"</span>` +
          `)` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with unknown attribute, type() and nested attr() fallback",
      propertyValue: `attr(data-x type(<length> | <percentage>), attr(data-y, 99%))`,
      attributes: {},
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-x is not set">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">` +
          `attr(` +
          `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute data-y is not set">` +
            `<span class="inspector-attr-name">data-y</span>` +
          `</span>` +
          `, ` +
          `<span class="inspector-attr-fallback">99%</span>` +
          `)` +
        `</span>` +
        `)`,
    },
    {
      message: "Modern attr() with `number` attr type and value matching type",
      propertyName: "line-height",
      propertyValue: `attr(data-x number, 2)`,
      attributes: { "data-x": "3" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;3&quot;">data-x</span>` +
          ` number` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">2</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with `number` attr type and value not matching type",
      propertyName: "line-height",
      propertyValue: `attr(data-x number, 2)`,
      attributes: { "data-x": "x" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute value (&quot;x&quot;) is not a valid numeric value">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` number` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">2</span>` +
        `)`,
    },
    {
      message: "Modern attr() with `deg` attr type and value matching type",
      propertyName: "rotate",
      propertyValue: `attr(data-x deg, 90deg)`,
      attributes: { "data-x": "1.5" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;1.5&quot;">data-x</span>` +
          ` deg` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">` +
          `<span data-angle="90deg"><span>90deg</span></span>` +
        `</span>` +
        `)`,
    },
    {
      message: "Modern attr() with `deg` attr type and value not matching type",
      propertyName: "rotate",
      propertyValue: `attr(data-x deg, 90deg)`,
      attributes: { "data-x": "x" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute value (&quot;x&quot;) is not a valid numeric value">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` deg` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">` +
          `<span data-angle="90deg"><span>90deg</span></span>` +
        `</span>` +
        `)`,
    },
    {
      message: "Modern attr() with `%` attr type and value matching type",
      propertyName: "height",
      propertyValue: `attr(data-x %, 50%)`,
      attributes: { "data-x": "99" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;99&quot;">data-x</span>` +
          ` %` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">50%</span>` +
        `)`,
    },
    {
      message: "Modern attr() with `%` attr type and value not matching type",
      propertyName: "height",
      propertyValue: `attr(data-x %, 50%)`,
      attributes: { "data-x": "20%" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute value (&quot;20%&quot;) is not a valid numeric value">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` %` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">50%</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with `type(<color>)` attr type and value matching type",
      propertyName: "color",
      propertyValue: `attr(data-x type(<color>), hotpink)`,
      attributes: { "data-x": "tomato" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;tomato&quot;">data-x</span>` +
          ` type(&lt;color&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">` +
          `<span data-color="hotpink"><span>hotpink</span></span>` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with `type(<color>)` attr type and value not matching type",
      propertyName: "color",
      propertyValue: `attr(data-x type(<color>), hotpink)`,
      attributes: { "data-x": "uwu" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute value (&quot;uwu&quot;) does not match expected &quot;&lt;color&gt;&quot; syntax">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` type(&lt;color&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">` +
          `<span data-color="hotpink"><span>hotpink</span></span>` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with `type(<length> | <percentage>)` attr type and a length value",
      propertyName: "width",
      propertyValue: `attr(data-x type(<length> | <percentage>), 33px)`,
      attributes: { "data-x": "42em" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;42em&quot;">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">33px</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with `type(<length> | <percentage>)` attr type and a percentage value",
      propertyName: "width",
      propertyValue: `attr(data-x type(<length> | <percentage>), 33px)`,
      attributes: { "data-x": "42%" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;42%&quot;">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">33px</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with `type(<length> | <percentage>)` attr type and value not matching type",
      propertyName: "width",
      propertyValue: `attr(data-x type(<length> | <percentage>), 33px)`,
      attributes: { "data-x": "42" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute value (&quot;42&quot;) does not match expected &quot;&lt;length&gt; | &lt;percentage&gt;&quot; syntax">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` type(&lt;length&gt; | &lt;percentage&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback">33px</span>` +
        `)`,
    },
    {
      message: "Modern attr() with `type(<custom-ident>)` attr type",
      propertyName: "position-anchor",
      propertyValue: `attr(data-x type(<custom-ident>), match-parent)`,
      attributes: { "data-x": "--my-anchor" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;--my-anchor&quot;">data-x</span>` +
          ` type(&lt;custom-ident&gt;)` +
        `</span>` +
        `, ` +
        `<span class="inspector-attr-fallback unmatched-class">match-parent</span>` +
        `)`,
    },
    {
      message: "Modern attr() with known attribute using random() and type()",
      propertyName: "rotate",
      propertyValue: "attr(data-x type(<angle>))",
      attributes: { "data-x": "random(90deg, 0.75turn)" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param">` +
          `<span class="inspector-attr-name" data-attribute="&quot;random(90deg, 0.75turn)&quot;">data-x</span>` +
          ` type(&lt;angle&gt;)` +
        `</span>` +
        `)`,
    },
    {
      message:
        "Modern attr() with known attribute using random() not matching specified type()",
      propertyName: "rotate",
      propertyValue: "attr(data-x type(<angle>))",
      attributes: { "data-x": "random(10px, 20px)" },
      // prettier-ignore
      expected:
        `attr(` +
        `<span class="inspector-attr-param unmatched-class" data-attribute="Attribute value (&quot;random(10px, 20px)&quot;) does not match expected &quot;&lt;angle&gt;&quot; syntax">` +
          `<span class="inspector-attr-name">data-x</span>` +
          ` type(&lt;angle&gt;)` +
        `</span>` +
        `)`,
    },
  ];

  for (const test of TESTS) {
    const frag = parser.parseCssProperty(
      test.propertyName || "content",
      test.propertyValue,
      {
        unmatchedClass: "unmatched-class",
        getAttributeValue:
          "getAttributeValue" in test
            ? test.getAttributeValue
            : attrName => test.attributes[attrName] ?? null,
      }
    );

    const target = doc.querySelector("div");
    target.appendChild(frag);

    is(target.innerHTML, test.expected, test.message);
    target.innerHTML = "";
  }
}

function testParseForCssExplainers(doc, parser) {
  const TESTS = [
    {
      message:
        "No custom elements and attributes when cssExplainersEnabled is false",
      propertyName: "width",
      propertyValue: "calc(10px + 1em)",
      cssExplainersEnabled: false,
      expected: `calc(10px + 1em)`,
    },
    {
      message:
        "No custom elements and attributes when parsing a function that isn't supported",
      propertyName: "content",
      propertyValue: "counter(count, decimal)",
      cssExplainersEnabled: true,
      expected: `counter(count, decimal)`,
    },
    {
      message: "Custom elements and attributes when parsing calc()",
      propertyName: "width",
      propertyValue: "calc(10px + 1em)",
      cssExplainersEnabled: true,
      // prettier-ignore
      expected:
        `<span data-function-expression="calc(10px + 1em)">` +
          `<span class="css-explainers-function-name">calc</span>` +
          `(10px + <span data-length-expression="1em">1em</span>)` +
        `</span>`,
    },
    {
      message: "Custom elements and attributes when parsing nested functions",
      propertyName: "transform",
      propertyValue: "translateY(calc(10px + round(up, sin(40deg)) * 1px))",
      cssExplainersEnabled: true,
      // prettier-ignore
      expected:
        `translateY(` +
        `<span data-function-expression="calc(10px + round(up, sin(40deg)) * 1px)">` +
          `<span class="css-explainers-function-name">calc</span>` +
          `(` +
            `10px + ` +
            `<span data-function-expression="round(up, sin(40deg))">` +
              `<span class="css-explainers-function-name">round</span>` +
              `(` +
                `up, ` +
                `<span data-function-expression="sin(40deg)">` +
                  `<span class="css-explainers-function-name">sin</span>` +
                  `(` +
                    `<span data-angle="40deg">` +
                      `<span>40deg</span>` +
                    `</span>` +
                  `)` +
                `</span>` +
              `)` +
            `</span>` +
            ` * 1px` +
          `)` +
        `</span>` +
        `)`,
    },
    {
      message: "Custom elements and attributes when using var() and attr()",
      propertyName: "width",
      propertyValue: "calc(var(--x, attr(data-x px, 16px)))",
      cssExplainersEnabled: true,
      attributes: { "data-x": "20" },
      // prettier-ignore
      expected:
        `<span data-function-expression="calc(var(--x, attr(data-x px, 16px)))">` +
          `<span class="css-explainers-function-name">calc</span>` +
          `(` +
            `<span data-function-expression="var(--x, attr(data-x px, 16px))">` +
              `<span>` +
                `<span class="css-explainers-function-name">var</span>` +
                `(` +
                `<span data-variable="--x is not set">--x</span>` +
                `,` +
                `<span>` +
                  ` ` +
                  `<span data-function-expression="attr(data-x px, 16px)">` +
                    `<span class="css-explainers-function-name">attr</span>` +
                    `(` +
                      `<span class="inspector-attr-param">` +
                        `<span class="inspector-attr-name" data-attribute="&quot;20&quot;">data-x</span>` +
                        ` px` +
                      `</span>` +
                      `, ` +
                      `<span class="inspector-attr-fallback null">16px</span>` +
                    `)` +
                  `</span>` +
                `</span>` +
                `)` +
              `</span>` +
            `</span>` +
          `)` +
        `</span>`,
    },
    {
      message: "Custom elements and attributes when using parenthesis block",
      propertyName: "opacity",
      propertyValue: "calc(1 - (var(--x) - 0.8rem))",
      cssExplainersEnabled: true,
      variables: { "--x": "2em" },
      expected:
        // prettier-ignore
        `<span data-function-expression="calc(1 - (var(--x) - 0.8rem))">` +
          `<span class="css-explainers-function-name">calc</span>` +
          `(` +
          `1 - (` +
          `<span data-function-expression="var(--x)">` +
            `<span>` +
              `<span class="css-explainers-function-name">var</span>` +
              `(` +
              `<span data-variable="2em">` +
                `--x` +
                `<button class="ruleview-variable-link jump-definition" data-variable-name="--x" title="Jump to variable definition"></button>` +
              `</span>` +
              `)` +
            `</span>` +
          `</span>` +
          ` - <span data-length-expression="0.8rem">0.8rem</span>` +
          `)` +
          `)` +
        `</span>`,
    },
    {
      message:
        "No data-function-expression attribute when a nested function isn't supported",
      propertyName: "width",
      propertyValue:
        "calc(10px + anchor-size(--my-anchor width, calc(50% + 10vw)))",
      cssExplainersEnabled: true,
      // prettier-ignore
      expected:
        `<span class="css-explainers-function-name">calc</span>(` +
          `10px + ` +
          `anchor-size(` +
            `--my-anchor width, ` +
            `<span data-function-expression="calc(50% + 10vw)">` +
              `<span class="css-explainers-function-name">calc</span>` +
              `(<span data-length-expression="50%">50%</span> + <span data-length-expression="10vw">10vw</span>)` +
            `</span>` +
          `)` +
        `)`,
    },
    {
      message:
        "data-length-expression attribute for non-px length and percentage",
      propertyName: "margin",
      propertyValue: "10% 20px 30em 40lh",
      cssExplainersEnabled: true,
      // prettier-ignore
      expected:
        `<span data-length-expression="10%">10%</span> ` +
        // no attribute for px
        `20px ` +
        `<span data-length-expression="30em">30em</span> ` +
        `<span data-length-expression="40lh">40lh</span>`,
    },
    {
      message: "attr() with type()",
      propertyName: "width",
      propertyValue: "attr(data-x type(<length>))",
      cssExplainersEnabled: true,
      attributes: { "data-x": "42em" },
      // prettier-ignore
      expected:
        `<span data-function-expression="attr(data-x type(&lt;length&gt;))">` +
          `<span class="css-explainers-function-name">attr</span>` +
          `(` +
          `<span class="inspector-attr-param">` +
            `<span class="inspector-attr-name" data-attribute="&quot;42em&quot;">data-x</span>` +
            ` type(&lt;length&gt;)` +
          `</span>` +
          `)` +
        `</span>`,
    },
  ];

  const target = doc.querySelector("div");
  for (const test of TESTS) {
    const frag = parser.parseCssProperty(
      test.propertyName,
      test.propertyValue,
      {
        cssExplainersEnabled: test.cssExplainersEnabled,
        getAttributeValue:
          "getAttributeValue" in test
            ? test.getAttributeValue
            : attrName => test.attributes[attrName] ?? null,
        getVariableData: varName => {
          if (typeof test.variables?.[varName] === "string") {
            return { value: test.variables[varName] };
          }

          return test.variables?.[varName] || {};
        },
      }
    );

    target.replaceChildren(frag);

    is(target.innerHTML, test.expected, test.message);
  }
  target.innerHTML = "";
}

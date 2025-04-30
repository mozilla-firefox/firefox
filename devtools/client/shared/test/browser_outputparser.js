/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["security.allow_unsafe_parent_loads", true],
      ["layout.css.backdrop-filter.enabled", true],
      ["layout.css.relative-color-syntax.enabled", true],
      ["dom.security.html_serialization_escape_lt_gt", true],
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
      const buttonAttributes = {
        class: COLOR_TEST_CLASS,
        style: `background-color:${segment.name}`,
        tabindex: 0,
        role: "button",
      };
      if (segment.colorFunction) {
        buttonAttributes["data-color-function"] = segment.colorFunction;
      }
      const buttonAttrString = Object.entries(buttonAttributes)
        .map(([attr, v]) => `${attr}="${v}"`)
        .join(" ");

      // prettier-ignore
      result.expected +=
        `<span data-color="${segment.name}" class="color-swatch-container">` +
          `<span ${buttonAttrString}></span>`+
          `<span>${segment.name}</span>` +
        `</span>`;
    }
  }

  result.desc = "Testing " + name + ": " + value;

  return result;
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

    makeColorTest("color", "color-mix(in srgb, red, blue)", [
      "color-mix(in srgb, ",
      { name: "red", colorFunction: "color-mix" },
      ", ",
      { name: "blue", colorFunction: "color-mix" },
      ")",
    ]),

    makeColorTest(
      "background-image",
      "linear-gradient(to top, color-mix(in srgb, #008000, rgba(255, 255, 0, 0.9)), blue)",
      [
        "linear-gradient(to top, ",
        "color-mix(in srgb, ",
        { name: "#008000", colorFunction: "color-mix" },
        ", ",
        { name: "rgba(255, 255, 0, 0.9)", colorFunction: "color-mix" },
        "), ",
        { name: "blue", colorFunction: "linear-gradient" },
        ")",
      ]
    ),

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

    makeColorTest("color", "rgb(from gold r g b)", [
      { name: "rgb(from gold r g b)" },
    ]),

    makeColorTest("color", "color(from hsl(0 100% 50%) xyz x y 0.5)", [
      { name: "color(from hsl(0 100% 50%) xyz x y 0.5)" },
    ]),

    makeColorTest(
      "color",
      "oklab(from red calc(l - 1) calc(a * 2) calc(b + 3) / alpha)",
      [{ name: "oklab(from red calc(l - 1) calc(a * 2) calc(b + 3) / alpha)" }]
    ),

    makeColorTest(
      "color",
      "rgb(from color-mix(in lch, plum 40%, pink) r g b)",
      [{ name: "rgb(from color-mix(in lch, plum 40%, pink) r g b)" }]
    ),

    makeColorTest("color", "rgb(from rgb(from gold r g b) r g b)", [
      { name: "rgb(from rgb(from gold r g b) r g b)" },
    ]),

    makeColorTest(
      "background-image",
      "linear-gradient(to right, #F60 10%, rgb(from gold r g b))",
      [
        "linear-gradient(to right, ",
        { name: "#F60", colorFunction: "linear-gradient" },
        " 10%, ",
        { name: "rgb(from gold r g b)", colorFunction: "linear-gradient" },
        ")",
      ]
    ),

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
  ];

  const target = doc.querySelector("div");
  ok(target, "captain, we have the div");

  for (const test of tests) {
    info(test.desc);

    const frag = parser.parseCssProperty(test.name, test.value, {
      colorSwatchClass: COLOR_TEST_CLASS,
      ...(test.parserExtraOptions || {}),
    });

    target.appendChild(frag);

    is(
      target.innerHTML,
      test.expected,
      "CSS property correctly parsed for " + test.name + ": " + test.value
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

  for (const test of tests) {
    const url = test.leader + "something.jpg" + test.trailer;
    const frag = parser.parseCssProperty("background", url, {
      urlClass: "test-urlclass",
      baseURI: test.baseURI,
    });

    const target = doc.querySelector("div");
    target.appendChild(frag);

    const expectedTrailer = test.expectedTrailer || test.trailer;

    const expected =
      test.leader +
      '<a target="_blank" class="test-urlclass" ' +
      'href="something.jpg">something.jpg</a>' +
      expectedTrailer;

    is(target.innerHTML, expected, test.desc);

    target.innerHTML = "";
  }
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
      desc: "Polygon shape",
      definition:
        "polygon(evenodd, 0px 0px, 10%200px,30%30% , calc(250px - 10px) 0 ,\n " +
        "12em var(--variable), 100% 100%) margin-box",
      spanCount: 18,
    },
    {
      desc: "POLYGON()",
      definition:
        "POLYGON(evenodd, 0px 0px, 10%200px,30%30% , calc(250px - 10px) 0 ,\n " +
        "12em var(--variable), 100% 100%) margin-box",
      spanCount: 18,
    },
    {
      desc: "Invalid polygon shape",
      definition: "polygon(0px 0px 100px 20px, 20% 20%)",
      spanCount: 0,
    },
    {
      desc: "Circle shape with all arguments",
      definition: "circle(25% at\n 30% 200px) border-box",
      spanCount: 4,
    },
    {
      desc: "Circle shape with only one center",
      definition: "circle(25em at 40%)",
      spanCount: 3,
    },
    {
      desc: "Circle shape with no radius",
      definition: "circle(at 30% 40%)",
      spanCount: 3,
    },
    {
      desc: "Circle shape with no center",
      definition: "circle(12em)",
      spanCount: 1,
    },
    {
      desc: "Circle shape with no arguments",
      definition: "circle()",
      spanCount: 0,
    },
    {
      desc: "Circle shape with no space before at",
      definition: "circle(25%at 30% 30%)",
      spanCount: 4,
    },
    {
      desc: "CIRCLE",
      definition: "CIRCLE(12em)",
      spanCount: 1,
    },
    {
      desc: "Invalid circle shape",
      definition: "circle(25%at30%30%)",
      spanCount: 0,
    },
    {
      desc: "Ellipse shape with all arguments",
      definition: "ellipse(200px 10em at 25% 120px) content-box",
      spanCount: 5,
    },
    {
      desc: "Ellipse shape with only one center",
      definition: "ellipse(200px 10% at 120px)",
      spanCount: 4,
    },
    {
      desc: "Ellipse shape with no radius",
      definition: "ellipse(at 25% 120px)",
      spanCount: 3,
    },
    {
      desc: "Ellipse shape with no center",
      definition: "ellipse(200px\n10em)",
      spanCount: 2,
    },
    {
      desc: "Ellipse shape with no arguments",
      definition: "ellipse()",
      spanCount: 0,
    },
    {
      desc: "ELLIPSE()",
      definition: "ELLIPSE(200px 10em)",
      spanCount: 2,
    },
    {
      desc: "Invalid ellipse shape",
      definition: "ellipse(200px100px at 30$ 20%)",
      spanCount: 0,
    },
    {
      desc: "Inset shape with 4 arguments",
      definition: "inset(200px 100px\n 30%15%)",
      spanCount: 4,
    },
    {
      desc: "Inset shape with 3 arguments",
      definition: "inset(200px 100px 15%)",
      spanCount: 3,
    },
    {
      desc: "Inset shape with 2 arguments",
      definition: "inset(200px 100px)",
      spanCount: 2,
    },
    {
      desc: "Inset shape with 1 argument",
      definition: "inset(200px)",
      spanCount: 1,
    },
    {
      desc: "Inset shape with 0 arguments",
      definition: "inset()",
      spanCount: 0,
    },
    {
      desc: "INSET()",
      definition: "INSET(200px)",
      spanCount: 1,
    },
    {
      desc: "offset-path property with inset shape value",
      property: "offset-path",
      definition: "inset(200px)",
      spanCount: 1,
    },
  ];

  for (const { desc, definition, property = "clip-path", spanCount } of tests) {
    info(desc);
    const frag = parser.parseCssProperty(property, definition, {
      shapeClass: "inspector-shape",
    });
    const spans = frag.querySelectorAll(".inspector-shape-point");
    is(spans.length, spanCount, desc + " span count");
    is(frag.textContent, definition, desc + " text content");
  }
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
            '<span data-variable="chartreuse">--seen</span>)' +
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
          '<span data-variable="var(--base)" data-variable-computed="1em">--seen</span>)' +
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
            '<span data-variable="chartreuse">--seen</span>,' +
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
        "<span>var(" +
          '<span class="unmatched-class" data-variable="--not-seen is not set">--not-seen</span>,' +
          "<span> " +
            '<span data-color="chartreuse">' +
              "<span>var(" +
                '<span data-variable="chartreuse">--seen</span>)' +
              "</span>" +
            "</span>" +
          "</span>)" +
        "</span>",
    },
    {
      text: "color-mix(in sgrb, var(--x), purple)",
      variables: { "--x": "yellow" },
      expected:
        // prettier-ignore
        `color-mix(in sgrb, ` +
        `<span data-color="yellow" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:yellow" tabindex="0" role="button" data-color-function="color-mix">` +
          `</span>` +
          `<span>var(<span data-variable="yellow">--x</span>)</span>` +
        `</span>` +
        `, ` +
        `<span data-color="purple" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:purple" tabindex="0" role="button" data-color-function="color-mix">` +
          `</span>` +
          `<span>purple</span>` +
        `</span>` +
        `)`,
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
          `<span>var(<span data-variable="yellow">--light</span>)</span>` +
        `</span>` +
        `, ` +
        `<span data-color="gold" class="color-swatch-container">` +
          `<span class="test-class" style="background-color:gold" tabindex="0" role="button" data-color-function="light-dark">` +
          `</span>` +
          `<span>var(<span data-variable="gold">--dark</span>)</span>` +
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
            '<span data-variable="chartreuse">--seen</span>,' +
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
        `<span>var(` +
          `<span class="unmatched-class" data-variable="--not-seen is not set">--not-seen</span>,` +
          `<span> ` +
            `<span data-color="seagreen">` +
              `<span>seagreen</span>` +
            `</span>` +
          `</span>)` +
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
              'var(<span data-variable="255">--r</span>)' +
            "</span>, 0, 0, " +
            "<span>" +
              'var(<span data-variable="0.5">--a</span>)' +
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
            "<span>" +
              'var(<span data-variable="red">--base</span>)' +
            "</span> r g 0 / " +
            "calc(" +
            "<span>" +
              'var(<span data-variable="0.8">--a</span>)' +
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
              `<span class="unmatched-class" data-variable="--not-seen is not set">--not-seen</span>,` +
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
            '>--registered</span>)' +
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
            '>--registered-universal</span>)' +
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
      expected:
        '<span>var(<span data-variable="light-dark(red, blue)">--x</span>)</span>',
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
            '<span data-variable="color-mix(in srgb, red 50%, blue)">--x</span>' +
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
          '<span data-variable="var(--empty)" data-variable-computed="">--refers-empty</span>)' +
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
                `<span data-variable="40%">--foo</span>` +
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
              `<span data-variable="hsl(50, 70%, var(--foo))" data-variable-computed="hsl(50, 70%, 40%)">--bar</span>` +
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
              `<span data-variable="hsl(10, 100%, var(--fur))" data-variable-computed="hsl(10, 100%, 50%)">--primary</span>` +
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
                `<span data-variable="var(--baz)" data-variable-computed="10">--fur</span>` +
              `)` +
            `</span>` +
            ` 20 ` +
            `<span>` +
              `var(` +
                `<span data-variable="30">--boo</span>` +
              `)` +
            `</span>` +
          `)</span>` +
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
      families: ["Open Sans"],
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
      output: "Arial , Helvetica, sans-serif",
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
      output: "'Arial' , \"Helvetica\"",
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
    "var(--family, Georgia, serif)",
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
    `<span>var(` +
      `<span class="unmatched-class" data-variable="--family is not set">` +
        `--family` +
      `</span>` +
      `,` +
      `<span> ` +
        `<span class="ruleview-font-family">Georgia</span>` +
        `, ` +
        `<span class="ruleview-font-family">serif</span>` +
      `</span>)` +
    `</span>`,
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
              `<span data-variable="red">--x</span>` +
            `)</span>` +
          `</span>, ` +
          `<span data-color="blue" class="color-swatch-container unmatched-class">` +
            `<span class="test-class" style="background-color:blue" tabindex="0" role="button" data-color-function="light-dark"></span>` +
            `<span>var(` +
              `<span data-variable="blue">--y</span>` +
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
              `var(<span data-variable="red">--x</span>)` +
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
              `var(<span data-variable="red">--x</span>)` +
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
              `var(<span data-variable="red">--x</span>)` +
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
              `var(<span data-variable="red">--x</span>)` +
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
              `var(<span data-variable="red">--x</span>)` +
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
              `var(<span data-variable="red">--x</span>)` +
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
              `var(<span data-variable="red">--x</span>)` +
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
              `var(<span data-variable="red">--x</span>)` +
            `</span>` +
          `</span>,a,b` +
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

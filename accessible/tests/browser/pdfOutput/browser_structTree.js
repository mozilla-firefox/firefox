/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

addPdfStructTreeTest(
  "testBasic",
  `
<h1>h1</h1>
<p>p1</p>
<div role="heading" aria-level="2">h2</div>
<ul>
  <li>uli1</li>
  <li>uli2</li>
</ul>
<ol>
  <li>oli</li>
</ol>
<table>
  <tr><th>tc1</th><th>tc2</th></tr>
  <tr><th>tc3</th><td>tc4</td></tr>
</table>
  `,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "H1",
              children: [
                { role: "NonStruct", children: [{ content: ["h1"] }] },
              ],
            },
            {
              role: "P",
              children: [
                { role: "NonStruct", children: [{ content: ["p1"] }] },
              ],
            },
            {
              role: "H2",
              children: [
                { role: "NonStruct", children: [{ content: ["h2"] }] },
              ],
            },
            {
              role: "L",
              children: [
                {
                  role: "LI",
                  children: [
                    { role: "Lbl", children: [{ content: ["•"] }] },
                    {
                      role: "NonStruct",
                      children: [{ content: [" ", "uli1"] }],
                    },
                  ],
                },
                {
                  role: "LI",
                  children: [
                    { role: "Lbl", children: [{ content: ["•"] }] },
                    {
                      role: "NonStruct",
                      children: [{ content: [" ", "uli2"] }],
                    },
                  ],
                },
              ],
            },
            {
              role: "L",
              children: [
                {
                  role: "LI",
                  children: [
                    { role: "Lbl", children: [{ content: ["1."] }] },
                    {
                      role: "NonStruct",
                      children: [{ content: [" ", "oli"] }],
                    },
                  ],
                },
              ],
            },
            {
              role: "Table",
              children: [
                {
                  role: "TR",
                  children: [
                    {
                      role: "TH",
                      children: [
                        { role: "NonStruct", children: [{ content: ["tc1"] }] },
                      ],
                      scope: "Column",
                    },
                    {
                      role: "TH",
                      children: [
                        {
                          role: "NonStruct",
                          children: [{ content: [" ", "tc2"] }],
                        },
                      ],
                      structId: "id1",
                      scope: "Column",
                    },
                  ],
                },
                {
                  role: "TR",
                  children: [
                    {
                      role: "TH",
                      children: [
                        { role: "NonStruct", children: [{ content: ["tc3"] }] },
                      ],
                      structId: "id2",
                      scope: "Row",
                    },
                    {
                      role: "TD",
                      children: [
                        {
                          role: "NonStruct",
                          children: [{ content: [" ", "tc4"] }],
                        },
                      ],
                      headers: ["id1", "id2"],
                    },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
  ],
  { chrome: true, topLevel: true }
);

// A headers attribute naming only a column header retains implicit row headers.
addPdfStructTreeTest(
  "testTableExplicitHeaders",
  `
<table>
  <tr><th>c1</th><th id="ch">c2</th></tr>
  <tr><th>r1</th><td headers="ch">d</td></tr>
</table>
  `,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "Table",
              children: [
                {
                  role: "TR",
                  children: [
                    {
                      role: "TH",
                      children: [
                        { role: "NonStruct", children: [{ content: ["c1"] }] },
                      ],
                      scope: "Column",
                    },
                    {
                      role: "TH",
                      children: [
                        {
                          role: "NonStruct",
                          children: [{ content: [" ", "c2"] }],
                        },
                      ],
                      structId: "id1",
                      scope: "Column",
                    },
                  ],
                },
                {
                  role: "TR",
                  children: [
                    {
                      role: "TH",
                      children: [
                        { role: "NonStruct", children: [{ content: ["r1"] }] },
                      ],
                      structId: "id2",
                      scope: "Row",
                    },
                    {
                      role: "TD",
                      children: [
                        {
                          role: "NonStruct",
                          children: [{ content: [" ", "d"] }],
                        },
                      ],
                      headers: ["id1", "id2"],
                    },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
  ],
  { chrome: true, topLevel: true }
);

addPdfStructTreeTest(
  "testIframe",
  `<h1>inside</h1>`,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "NonStruct", // iframe
              children: [
                // XXX We render an empty marked content sequence before an
                // iframe document for some unknown reason. This is inconsequential.
                { content: [] },
                {
                  role: "Document",
                  children: [
                    {
                      role: "H1",
                      children: [
                        {
                          role: "NonStruct",
                          children: [{ content: ["inside"] }],
                        },
                      ],
                    },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
  ],
  { topLevel: false, iframe: true, remoteIframe: true }
);

addPdfStructTreeTest(
  "testIframeWithSurroundingContent",
  `
<h1>before</h1>
<iframe src="data:text/html,<h2>inside</h2>"></iframe>
<h3>after</h3>
  `,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "H1",
              children: [
                { role: "NonStruct", children: [{ content: ["before"] }] },
              ],
            },
            {
              role: "NonStruct", // iframe
              children: [
                { content: [] },
                {
                  role: "Document",
                  children: [
                    {
                      role: "H2",
                      children: [
                        {
                          role: "NonStruct",
                          children: [{ content: ["inside"] }],
                        },
                      ],
                    },
                  ],
                },
              ],
            },
            {
              role: "H3",
              children: [
                { role: "NonStruct", children: [{ content: ["after"] }] },
              ],
            },
          ],
        },
      ],
    },
  ]
);

addPdfStructTreeTest(
  "testAriaOwns",
  `
<div aria-owns="h3 h2 h1">
  <h1 id="h1">h1</h1>
  <h2 id="h2">h2</h2>
</div>
<h3 id="h3">h3</h3>
  `,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "NonStruct", // div
              children: [
                {
                  role: "H3",
                  children: [
                    { role: "NonStruct", children: [{ content: ["h3"] }] },
                  ],
                },
                {
                  role: "H2",
                  children: [
                    { role: "NonStruct", children: [{ content: ["h2"] }] },
                  ],
                },
                {
                  role: "H1",
                  children: [
                    { role: "NonStruct", children: [{ content: ["h1"] }] },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
  ],
  { chrome: true, topLevel: true }
);

// In math, text at a raised script level (e.g. superscripts and fraction
// numerators) is often drawn using alternate glyphs which don't have a
// character mapped in the font. Use a serif font so this test doesn't depend on
// which math font is installed.
addPdfStructTreeTest(
  "testMath",
  `
<p>a <math style="font-family: serif;">
  <mrow><mi>sin</mi><mo>+</mo><mfrac><mn>1</mn><mn>3</mn></mfrac></mrow>
  <msqrt><mn>4</mn></msqrt>
  <mo>-</mo>
  <msup><mn>5</mn><mn>6</mn></msup>
</math> b</p>
  `,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "P",
              children: [
                { role: "NonStruct", children: [{ content: ["a"] }] },
                {
                  role: "Formula",
                  children: [
                    {
                      role: "math",
                      children: [
                        {
                          role: "mrow",
                          children: [
                            {
                              role: "mi",
                              children: [{ content: [" ", "sin"] }],
                            },
                            { role: "mo", children: [{ content: [" ", "+"] }] },
                            {
                              role: "mfrac",
                              children: [
                                {
                                  role: "mn",
                                  children: [{ content: [" ", "1"] }],
                                },
                                { role: "mn", children: [{ content: ["3"] }] },
                              ],
                            },
                          ],
                        },
                        {
                          role: "msqrt",
                          children: [
                            { role: "mn", children: [{ content: [" ", "4"] }] },
                          ],
                        },
                        { role: "mo", children: [{ content: [" ", "−"] }] },
                        {
                          role: "msup",
                          children: [
                            { role: "mn", children: [{ content: [" ", "5"] }] },
                            { role: "mn", children: [{ content: ["6"] }] },
                          ],
                        },
                      ],
                    },
                  ],
                },
                { role: "NonStruct", children: [{ content: [" ", "b"] }] },
              ],
            },
          ],
        },
      ],
    },
  ],
  { chrome: true, topLevel: true }
);

addPdfStructTreeTest(
  "testLink",
  `<p>a <a href="https://example.com/">l</a></p>`,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "P",
              children: [
                { role: "NonStruct", children: [{ content: ["a"] }] },
                {
                  role: "Link",
                  children: [
                    { role: "NonStruct", children: [{ content: [" ", "l"] }] },
                    { annotation: "Link", url: "https://example.com/" },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
  ]
);

addPdfStructTreeTest(
  "testLinkOverflow",
  `
<p>a <a href="https://example.com/">l<span style="display: inline-block; height: 100px;">m</span></a></p>
  `,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "P",
              children: [
                { role: "NonStruct", children: [{ content: ["a"] }] },
                {
                  role: "Link",
                  children: [
                    { role: "NonStruct", children: [{ content: [" ", "l"] }] },
                    {
                      role: "NonStruct",
                      children: [
                        { role: "NonStruct", children: [{ content: ["m"] }] },
                        // This extends outside the link element's frame, so it
                        // needs its own link annotation.
                        { annotation: "Link", url: "https://example.com/" },
                      ],
                    },
                    { annotation: "Link", url: "https://example.com/" },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
  ]
);

// mtext is a MathML text integration point, so this (invalid) nested mtext is
// actually parsed as an unknown HTML element, not a MathML one. It therefore
// gets no Accessible of its own; the text ends up as a direct child Accessible
// of the outer (real) mtext.
addPdfStructTreeTest(
  "testMathNestedMtext",
  `<math><mtext><mtext>1</mtext></mtext></math>`,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "Formula",
              children: [
                {
                  role: "math",
                  children: [
                    {
                      role: "mtext",
                      children: [{ content: ["1"] }],
                    },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
  ],
  { chrome: true, topLevel: true }
);

// HTML content is valid directly inside mtext. Unlike testMathNestedMtext,
// the link here gets its own Accessible, so it needs its own struct element
// rather than being folded into mtext.
addPdfStructTreeTest(
  "testMathLinkInMtext",
  `<math><mtext><a href="https://example.com/">1</a></mtext></math>`,
  [
    {
      role: "Root",
      children: [
        {
          role: "Document",
          children: [
            {
              role: "Formula",
              children: [
                {
                  role: "math",
                  children: [
                    {
                      role: "mtext",
                      children: [
                        {
                          role: "Link",
                          children: [
                            {
                              role: "NonStruct",
                              children: [{ content: ["1"] }],
                            },
                            {
                              annotation: "Link",
                              url: "https://example.com/",
                            },
                          ],
                        },
                      ],
                    },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
  ],
  { chrome: true, topLevel: true }
);

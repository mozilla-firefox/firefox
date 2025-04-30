/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

window._snapshots = {
  "ColorContrastAccessibility error render.": {
    type: "div",
    props: {
      role: "presentation",
      tabIndex: "-1",
      className: "accessibility-check",
    },
    children: [
      {
        type: "h3",
        props: {
          className: "accessibility-check-header",
        },
        children: ["Color and Contrast"],
      },
      {
        type: "div",
        props: {
          role: "presentation",
          tabIndex: "-1",
          className: "accessibility-color-contrast",
        },
        children: [
          {
            type: "span",
            props: {
              className: "accessibility-color-contrast-error",
              role: "presentation",
            },
            children: ["Unable to calculate"],
          },
        ],
      },
    ],
  },
  "ColorContrastAccessibility basic render.": {
    type: "div",
    props: {
      role: "presentation",
      tabIndex: "-1",
      className: "accessibility-check",
    },
    children: [
      {
        type: "h3",
        props: {
          className: "accessibility-check-header",
        },
        children: ["Color and Contrast"],
      },
      {
        type: "div",
        props: {
          role: "presentation",
          tabIndex: "-1",
          className: "accessibility-color-contrast",
        },
        children: [
          {
            type: "span",
            props: {
              className: "accessibility-contrast-value FAIL",
              role: "presentation",
              style: {
                "--accessibility-contrast-color": "rgba(255,0,0,1)",
                "--accessibility-contrast-bg": "rgba(255,255,255,1)",
              },
            },
            children: ["4.00"],
          },
        ],
      },
      {
        type: "p",
        props: {
          className: "accessibility-check-annotation",
        },
        children: [
          "Does not meet WCAG standards for accessible text. ",
          {
            type: "a",
            props: {
              className: "link",
              href:
                "https://developer.mozilla.org/docs/Web/Accessibility/Guides/" +
                "Understanding_WCAG/Perceivable/Color_contrast?utm_source=" +
                "devtools&utm_medium=a11y-panel-checks-color-contrast",
              onClick:
                "openDocOnClick(event) {\n    event.preventDefault();\n    " +
                "openDocLink(event.target.href);\n  }",
            },
            children: ["Learn more"],
          },
          "",
        ],
      },
    ],
  },
  "ColorContrastAccessibility range render.": {
    type: "div",
    props: {
      role: "presentation",
      tabIndex: "-1",
      className: "accessibility-check",
    },
    children: [
      {
        type: "h3",
        props: {
          className: "accessibility-check-header",
        },
        children: ["Color and Contrast"],
      },
      {
        type: "div",
        props: {
          role: "presentation",
          tabIndex: "-1",
          className: "accessibility-color-contrast",
        },
        children: [
          {
            type: "span",
            props: {
              className: "accessibility-contrast-value FAIL",
              role: "presentation",
              style: {
                "--accessibility-contrast-color": "rgba(128,128,128,1)",
                "--accessibility-contrast-bg": "rgba(219,106,116,1)",
              },
            },
            children: ["1.19"],
          },
          {
            type: "div",
            props: {
              role: "presentation",
              tabIndex: "-1",
              className: "accessibility-color-contrast-separator",
            },
            children: null,
          },
          {
            type: "span",
            props: {
              className: "accessibility-contrast-value FAIL",
              role: "presentation",
              style: {
                "--accessibility-contrast-color": "rgba(128,128,128,1)",
                "--accessibility-contrast-bg": "rgba(156,145,211,1)",
              },
            },
            children: ["1.39"],
          },
        ],
      },
      {
        type: "p",
        props: {
          className: "accessibility-check-annotation",
        },
        children: [
          "Does not meet WCAG standards for accessible text. ",
          {
            type: "a",
            props: {
              className: "link",
              href:
                "https://developer.mozilla.org/docs/Web/Accessibility/Guides/" +
                "Understanding_WCAG/Perceivable/Color_contrast?utm_source=" +
                "devtools&utm_medium=a11y-panel-checks-color-contrast",
              onClick:
                "openDocOnClick(event) {\n    event.preventDefault();\n    " +
                "openDocLink(event.target.href);\n  }",
            },
            children: ["Learn more"],
          },
          "",
        ],
      },
    ],
  },
  "ColorContrastAccessibility large text render.": {
    type: "div",
    props: {
      role: "presentation",
      tabIndex: "-1",
      className: "accessibility-check",
    },
    children: [
      {
        type: "h3",
        props: {
          className: "accessibility-check-header",
        },
        children: ["Color and Contrast"],
      },
      {
        type: "div",
        props: {
          role: "presentation",
          tabIndex: "-1",
          className: "accessibility-color-contrast",
        },
        children: [
          {
            type: "span",
            props: {
              className: "accessibility-contrast-value AA",
              role: "presentation",
              style: {
                "--accessibility-contrast-color": "rgba(255,0,0,1)",
                "--accessibility-contrast-bg": "rgba(255,255,255,1)",
              },
            },
            children: ["4.00"],
          },
          {
            type: "span",
            props: {
              className: "accessibility-color-contrast-large-text",
              role: "presentation",
              title:
                "Text is 14 point and bold or larger, or 18 point or larger.",
            },
            children: ["large text"],
          },
        ],
      },
      {
        type: "p",
        props: {
          className: "accessibility-check-annotation",
        },
        children: [
          "Meets WCAG AA standards for accessible text. ",
          {
            type: "a",
            props: {
              className: "link",
              href:
                "https://developer.mozilla.org/docs/Web/Accessibility/Guides/" +
                "Understanding_WCAG/Perceivable/Color_contrast?utm_source=" +
                "devtools&utm_medium=a11y-panel-checks-color-contrast",
              onClick:
                "openDocOnClick(event) {\n    event.preventDefault();\n    " +
                "openDocLink(event.target.href);\n  }",
            },
            children: ["Learn more"],
          },
          "",
        ],
      },
    ],
  },
};

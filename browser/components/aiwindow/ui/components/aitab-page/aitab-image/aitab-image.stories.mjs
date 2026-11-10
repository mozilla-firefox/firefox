/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-image.mjs";

export default {
  title: "Domain-specific UI Widgets/AI Window/AI Tab Image",
  component: "aitab-image",
  argTypes: {
    src: { control: { type: "text" } },
    alt: { control: { type: "text" } },
    width: { control: { type: "number" } },
  },
};

const Template = ({ src, alt, width }) => html`
  <aitab-image
    style=${`inline-size: ${width}px`}
    src=${src}
    alt=${alt}
  ></aitab-image>
`;

const SRC = "chrome://browser/content/aiwindow/assets/aitab-error-kit.svg";

export const Large = Template.bind({});
Large.args = { src: SRC, alt: "Smart Page illustration", width: 640 };

export const Medium = Template.bind({});
Medium.args = { src: SRC, alt: "Smart Page illustration", width: 374 };

export const Thumbnail = Template.bind({});
Thumbnail.args = { src: SRC, alt: "Smart Page illustration", width: 144 };

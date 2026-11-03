/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "chrome://browser/content/aiwindow/components/aitab-highlights.mjs";
import { withHeadingFont } from "../aitab-story-font.mjs";

export default {
  title: "Domain-specific UI Widgets/AI Window/AI Tab Highlights",
  component: "aitab-highlights",
  decorators: [withHeadingFont],
  argTypes: {
    title: { control: "text" },
    items: { control: "object" },
  },
};

const Template = ({ title, items }) => html`
  <aitab-highlights .title=${title} .items=${items}></aitab-highlights>
`;

// page-icon: is not reachable from Storybook, so the sources carry favicons.
const EXPEDIA = {
  title: "Expedia",
  href: "https://www.expedia.com/Niijima-Island.d6055565.Destination-Travel-Guides",
  favicon: "chrome://branding/content/about-logo.svg",
};
const TRIPADVISOR = {
  title: "Trip Advisor",
  href: "https://www.tripadvisor.com/Attraction_Review-Habushiura-Beach",
  favicon: "chrome://branding/content/icon16.png",
};
const YUNOHAMA = {
  title: "Yunohama Onsen",
  href: "https://www.yunohamaonsen.com/en/",
  favicon: "chrome://global/skin/icons/defaultFavicon.svg",
};

const ITINERARY = [
  {
    eyebrow: "Morning",
    title: "Habushiura beach",
    body: "6.5 to 7-kilometer-long white sand and turquoise water coastline famous for surfing, dramatic cliffs, and clear emerald-green waters.",
    sources: { items: [EXPEDIA] },
  },
  {
    eyebrow: "Afternoon",
    title: "Yunohama Onsen",
    body: "A historic coastal hot spring resort in Tsuruoka City, Yamagata Prefecture, featuring mineral-rich chloride waters, panoramic views of the Sea of Japan, and status as one of Japan's top sunset locations.",
    sources: { items: [YUNOHAMA, EXPEDIA] },
  },
  {
    eyebrow: "Evening",
    title: "Overnight ferry",
    body: "Departs from Takeshiba Pier in Tokyo, takes about 8.5 to 9 hours, and is operated by Tokai Kisen.",
    sources: { items: [TRIPADVISOR] },
  },
];

export const Itinerary = Template.bind({});
Itinerary.args = {
  title: "A day on Niijima",
  items: ITINERARY,
};

export const StatementsOnly = Template.bind({});
StatementsOnly.args = {
  title: "",
  items: [
    {
      title: "The deadline is the whole ballgame",
      body: "The contract makes time of the essence, so missing the completion date is a breach on its own, whatever the reason for the delay.",
    },
    {
      title: "Returning the deposit does not undo the breach",
      body: "Refunding the deposit settles the money already paid but leaves the buyer's claim for losses caused by the late completion untouched.",
    },
    {
      title: "Notice has to be in writing",
      body: "Both parties agreed that a notice to complete is only effective once it has been served in writing on the other side's solicitor.",
    },
  ],
};

export const SingleStatement = Template.bind({});
SingleStatement.args = {
  title: "",
  items: [
    {
      title: "General watering rule",
      body: "Most houseplants prefer the top 2 to 3 cm of soil to dry out between waterings. Overwatering is the top killer; yellow leaves are a common sign of too much water.",
    },
  ],
};

export const MixedOptionalParts = Template.bind({});
MixedOptionalParts.args = {
  title: "What the sources agree on",
  items: [
    {
      eyebrow: "Sizing",
      title: "A 1940s house usually needs a 3 to 4 ton unit",
      body: "Every calculator the sources link to lands in this range once the attic is insulated.",
      sources: { items: [EXPEDIA, TRIPADVISOR, YUNOHAMA] },
    },
    {
      title: "Cold-climate models hold their output to around -25 C",
      body: "Manufacturer spec sheets list the rated capacity at -25 C, and the two owner forums report the same in practice.",
    },
    {
      eyebrow: "Rebates",
      body: "Federal and utility rebates stack, but only one of the three installers quoted them up front.",
      sources: { items: [YUNOHAMA] },
    },
    {
      title: "Ductwork is the hidden cost",
    },
  ],
};

export const LongText = Template.bind({});
LongText.args = {
  title:
    "A title long enough to wrap onto a second line inside the rounded card",
  items: [
    {
      eyebrow: "An eyebrow that is far longer than an eyebrow ought to be",
      title:
        "Supercalifragilisticexpialidocious-unbrokenstatementthatmustwrapsomewhere",
      body: "The evidence column keeps its share of the width however long the statement beside it grows, and this sentence keeps going so that it wraps onto several lines of its own to prove the point.",
      sources: {
        items: [
          {
            title: "A source whose title runs well past the chip's width",
            href: "https://www.example.com/a/very/long/path",
            favicon: "chrome://global/skin/icons/defaultFavicon.svg",
          },
          EXPEDIA,
        ],
      },
    },
    {
      title: "Short",
      body: "Short.",
    },
  ],
};

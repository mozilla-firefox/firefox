/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html } from "chrome://global/content/vendor/lit.all.mjs";
import "chrome://browser/content/aiwindow/components/ai-clarifying-question.mjs";

const QUESTIONS = [
  {
    id: "coverage",
    question: "What should the report cover?",
    options: [
      {
        id: "kit",
        label: "The whole cold-night kit",
        description: "The cold nights",
        recommended: true,
      },
      {
        id: "jacket",
        label: "Just the jacket",
        description: "The page you are on, on its own",
      },
      {
        id: "sleep",
        label: "Sleeping bag and pad",
        description: "The sleep system on its own",
      },
    ],
  },
  {
    id: "owned",
    question:
      "Is there anything you already own that you'd like to keep using?",
    options: [
      {
        id: "none",
        label: "Nothing yet, start from scratch",
        description: "I'll price a full kit",
        recommended: true,
      },
      {
        id: "bag",
        label: "I have a sleeping bag",
        description: "I'll work around it",
      },
      {
        id: "stove",
        label: "I have a stove",
        description: "Hot food is already covered",
      },
    ],
  },
];

export default {
  title: "Domain-specific UI Widgets/AI Window/Clarifying question",
  component: "ai-clarifying-question",
  argTypes: {
    questions: { control: "object" },
    currentIndex: { control: { type: "number", min: 0 } },
    maxCustomAnswerLength: { control: "number" },
  },
  parameters: {
    actions: {
      handles: [
        "ai-clarifying-question:answer",
        "ai-clarifying-question:close",
      ],
    },
    fluent: `
ai-clarifying-question-progress = Question { $current } of { $total }
ai-clarifying-question-recommended = Recommended
ai-clarifying-question-custom-answer =
    .placeholder = Something else
    .aria-label = Something else
ai-clarifying-question-skip =
    .label = Skip
ai-clarifying-question-send =
    .label = Send
ai-clarifying-question-close =
    .title = Close questions
    `,
  },
};

const Template = ({ questions, currentIndex, maxCustomAnswerLength }) => html`
  <link
    rel="stylesheet"
    href="chrome://browser/content/aiwindow/ai-window-shared.css"
  />
  <ai-clarifying-question
    .questions=${questions}
    .currentIndex=${currentIndex}
    .maxCustomAnswerLength=${maxCustomAnswerLength}
  ></ai-clarifying-question>
`;

export const Default = Template.bind({});
Default.args = {
  questions: QUESTIONS,
  currentIndex: 0,
  maxCustomAnswerLength: 500,
};

export const SecondQuestion = Template.bind({});
SecondQuestion.args = {
  ...Default.args,
  currentIndex: 1,
};

export const SingleQuestion = Template.bind({});
SingleQuestion.args = {
  ...Default.args,
  questions: [QUESTIONS[0]],
};

export const NoRecommendation = Template.bind({});
NoRecommendation.args = {
  ...Default.args,
  questions: [
    {
      ...QUESTIONS[0],
      options: QUESTIONS[0].options.map(option => ({
        ...option,
        recommended: false,
      })),
    },
  ],
};

export const LongText = Template.bind({});
LongText.args = {
  ...Default.args,
  questions: [
    {
      id: "long",
      question:
        "Which parts of the trip should the report focus on, given the forecast lows and how far you plan to hike in and out each day?",
      options: [
        {
          id: "a",
          label:
            "Everything you need to stay warm overnight, from the sleeping system to hot food without a campfire",
          description:
            "Covers the bag, pad, layers and a stove, priced as a full kit",
          recommended: true,
        },
        {
          id: "b",
          label: "Only the clothing layers",
          description: "Jacket, base layers and accessories",
        },
      ],
    },
  ],
};

export const ShortCharacterLimit = Template.bind({});
ShortCharacterLimit.args = {
  ...Default.args,
  maxCustomAnswerLength: 20,
};

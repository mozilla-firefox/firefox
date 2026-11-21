/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

export const MEMORIES_ENGINE_ID = "about-inference-memories";

export const MEMORIES_MODEL = {
  engineId: MEMORIES_ENGINE_ID,
  featureId: "memories-initial-generation-system",
  backend: "llama.cpp",
  taskName: "text-generation",
  modelId: "mozilla/gemma4",
  modelFile: "gemma-4-E2B-it-Q4_0.gguf",
  modelRevision: "v1.0.0",
  modelHubRootUrl: "https://model-hub.mozilla.org",
  modelHubUrlTemplate: "{model}/{revision}",
  device: { kinds: ["gpu", "cpu"] },
  numBatch: 512,
  kvCacheDtype: "q8_0",
  flashAttn: true,
};

const SESSIONS = `Session 1 (3 hours ago)
Search queries: trail running shoes for beginners; coastal day hikes
Page titles: Beginner Trail Running Guide; Choosing Trail Running Shoes; Best Day Hikes Along the Coast
Chat: What should I pack for a half day hike along the coast?
Chat: Are trail running shoes fine for a rocky coastal path?

Session 2 (2 hours ago)
Search queries: sourdough starter feeding schedule; overnight focaccia recipe
Page titles: Sourdough Starter From Scratch; Understanding Dough Hydration; Dutch Oven Baking Tips
Chat: My sourdough loaf came out dense, how do I get a more open crumb?
Chat: How often should I feed a starter I keep in the fridge?

Session 3 (1 hour ago)
Search queries: lisbon walking tour alfama; lisbon to sintra day trip train
Page titles: Three Days in Lisbon Itinerary; Riding the Historic Trams of Lisbon; A Day Trip to Sintra
Chat: Plan a relaxed three day walking itinerary for Lisbon.
Chat: Is Sintra doable as a half day from Lisbon by train?

Session 4 (30 minutes ago)
Search queries: trail running training plan beginners; running vest buying guide
Page titles: Trail Running Training Plan for Beginners; Scenic Mountain Loop Trails; Choosing Trail Running Shoes
Chat: How many runs a week should a beginner trail plan have?
Chat: Do I need a running vest for runs under two hours?`;

const MEMORIES = [
  ["Enjoys hiking coastal trails", "Read several guides to coastal day hikes"],
  [
    "Plans coastal hikes with trail running shoes",
    "Compared shoes for rocky coastal paths",
  ],
  [
    "Bakes sourdough bread at home",
    "Looked up starter feeding and dough hydration",
  ],
  [
    "Makes focaccia with sourdough starter",
    "Compared overnight sourdough focaccia recipes",
  ],
  [
    "Plans a walking trip to Lisbon",
    "Compared Lisbon itineraries and neighbourhoods",
  ],
  [
    "Plans a day trip from Lisbon to Sintra",
    "Looked up trains and day trip itineraries",
  ],
];

export async function loadMemoriesWorkload() {
  if (AppConstants.MOZ_BUILD_APP !== "browser") {
    throw new Error("The memories workload requires Firefox Desktop");
  }
  // Bundled prompts isolate runs from Remote Settings and profile overrides.
  const response = await fetch(
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit -- Guarded by MOZ_BUILD_APP.
    "resource://app/defaults/settings/main/ai-window-prompts.json"
  );
  const { data } = await response.json();
  const prompt = (id, fields = {}) => {
    const record = data.find(item => item.id === id);
    if (!record?.prompts) {
      throw new Error(`Missing bundled prompt: ${id}`);
    }
    return record.prompts.replace(
      /\{(\w+)\}/g,
      (match, key) => fields[key] ?? match
    );
  };
  const message = (role, content) => ({ role, content });
  return [
    {
      id: "generate",
      featureId: MEMORIES_MODEL.featureId,
      prompt: [
        message(
          "system",
          prompt(
            "memories-initial-generation-system--gemini-3-1-flash-lite--v3"
          )
        ),
        message(
          "user",
          prompt(
            "memories-initial-generation-user--gemini-3-1-flash-lite--v4",
            {
              categoriesList:
                "Sports & Fitness\nFood & Drink\nTravel\nHobbies & Leisure",
              intentsList: "Research / Learn\nPlan / Organize\nBuy / Sell",
              profileRecordsRenderedStr: SESSIONS,
            }
          )
        ),
      ],
    },
    {
      id: "merge",
      featureId: "memories-merge",
      prompt: [
        message(
          "system",
          prompt(
            "memories-merge--system-instructions--gemini-3-1-flash-lite--v1"
          )
        ),
        message(
          "user",
          MEMORIES.map(
            ([summary, reasoning]) =>
              `- Statement: ${summary}\n  Reasoning: ${reasoning}`
          ).join("\n")
        ),
      ],
    },
    {
      id: "filter",
      featureId: "memories-quality-and-sensitivity-filter-system",
      prompt: [
        message(
          "system",
          prompt(
            "memories-quality-and-sensitivity-filter-system--gemini-3-1-flash-lite--v1"
          )
        ),
        message(
          "user",
          prompt(
            "memories-quality-and-sensitivity-filter-user--gemini-3-1-flash-lite--v1",
            {
              memoriesList: [
                ...MEMORIES.map(([summary]) => summary),
                "Checks the weather",
                "Tracks package deliveries",
              ]
                .map(summary => `- ${summary}`)
                .join("\n"),
            }
          )
        ),
      ],
    },
  ];
}

/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Rendering tests for the individual AI Tab block components. Each component
// gets its own add_task here rather than its own file: they are props in,
// markup out, and a file each would mean a fresh browser session each in CI.
// Behaviour that crosses a process or database boundary belongs in
// browser_aitab_actions.js instead.
//
// The components are mounted bare in the about:smartpage document rather than
// through a stored page, so these stay independent of the page config shape.
// Note the mounting has to be repeated inside each content task: the task runs
// in the content process and cannot call helpers defined in this file.

const AITAB_TEST_PREF = "browser.smartwindow.aitab.enabled";

/**
 * Opens about:smartpage, where every AI Tab custom element is registered.
 *
 * @param {Function} task - Content task, receives the spawn args.
 * @param {Array} args - Structured-cloneable arguments for the task.
 */
async function withAITabDocument(task, args = []) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.enabled", true],
      [AITAB_TEST_PREF, true],
    ],
  });
  await BrowserTestUtils.withNewTab("about:smartpage", async browser => {
    await SpecialPowers.spawn(browser, args, task);
  });
  await SpecialPowers.popPrefEnv();
}

// The date itself is formatted in AITabParent and covered by
// test_AITabDate.js. The header only renders the label it is handed.
add_task(async function test_header_renders_the_created_label() {
  await withAITabDocument(
    async label => {
      await content.customElements.whenDefined("aitab-header");
      const element = content.document.createElement("aitab-header");
      content.document.body.append(element);
      const header = element.wrappedJSObject;

      header.createdAt = label;
      await header.updateComplete;

      Assert.equal(
        header.shadowRoot.querySelector(".aitab-eyebrow").textContent,
        label,
        "The eyebrow shows the label the parent formatted"
      );

      header.createdAt = "";
      await header.updateComplete;
      Assert.ok(
        !header.shadowRoot.querySelector(".aitab-eyebrow"),
        "No eyebrow is rendered without a label"
      );
    },
    ["Created Sep 1"]
  );
});

add_task(async function test_header_references() {
  await withAITabDocument(
    async tags => {
      await content.customElements.whenDefined("aitab-header");
      const element = content.document.createElement("aitab-header");
      content.document.body.append(element);
      const header = element.wrappedJSObject;
      await header.updateComplete;

      Assert.ok(
        !header.shadowRoot.querySelector("ai-grouped-chip-container"),
        "No chip container is rendered when there are no references"
      );

      header.references = Cu.cloneInto(tags, content);
      await header.updateComplete;

      // header is already the unwrapped object, so its shadow DOM query
      // returns raw content elements; no second unwrap is needed.
      const chips = header.shadowRoot.querySelector(
        "ai-grouped-chip-container"
      );
      Assert.ok(chips, "The chip container renders once there are references");
      Assert.deepEqual(
        chips.chips.map(chip => [chip.url, chip.label]),
        tags.map(tag => [tag.href, tag.title]),
        "Each tag becomes a chip keyed by href with its title as the label"
      );
      // ai-grouped-chip-container is shared with chat and defaults to chat's
      // event name, so without this the chip would open tags through
      // AIChatContent and the AITab actor would never hear about it.
      Assert.equal(
        chips.getAttribute("openLinkEvent"),
        "AITab:OpenLink",
        "The chip is wired to the AI Tab open-link event, not chat's"
      );
    },
    [
      [
        { title: "energy.gov", href: "https://energy.gov" },
        { title: "NEEP", href: "https://neep.org" },
      ],
    ]
  );
});

add_task(async function test_header_reference_count_label() {
  // Resolved text, not the l10n id: the string is in aiWindowContent.ftl,
  // which aitab.html has to load.
  await withAITabDocument(async () => {
    await content.customElements.whenDefined("aitab-header");
    const element = content.document.createElement("aitab-header");
    content.document.body.append(element);
    const header = element.wrappedJSObject;

    for (const [count, expected] of [
      [1, "1 source"],
      [3, "3 sources"],
    ]) {
      header.references = Cu.cloneInto(
        Array.from({ length: count }, (_, i) => ({
          title: `example${i}.com`,
          href: `https://example${i}.com`,
        })),
        content
      );
      await header.updateComplete;

      const label = header.shadowRoot
        .querySelector("ai-grouped-chip-container")
        .shadowRoot.querySelector(".grouped-chips__label");

      await ContentTaskUtils.waitForCondition(
        () => label.textContent.trim() == expected,
        `${count} reference(s) reads as "${expected}"`
      );
      Assert.equal(label.textContent.trim(), expected, `Reads "${expected}"`);
    }
  });
});

add_task(async function test_list_groups() {
  await withAITabDocument(
    async groups => {
      await content.customElements.whenDefined("aitab-list");
      const element = content.document.createElement("aitab-list");
      content.document.body.append(element);
      const list = element.wrappedJSObject;
      await list.updateComplete;

      Assert.ok(
        !list.shadowRoot.querySelector(".aitab-list"),
        "Nothing is rendered before there are any groups"
      );

      list.title = "Kanazawa shortlist";
      list.groups = Cu.cloneInto(groups, content);
      await list.updateComplete;

      Assert.equal(
        list.shadowRoot.querySelector(".aitab-list-title").textContent.trim(),
        "Kanazawa shortlist",
        "The block title renders above the groups"
      );
      Assert.deepEqual(
        [...list.shadowRoot.querySelectorAll(".aitab-list-group")].map(group =>
          [
            group.querySelector(".aitab-list-group-heading")?.textContent ?? "",
            ...[...group.querySelectorAll(".aitab-list-item")].map(
              item => item.textContent
            ),
          ].map(text => text.trim())
        ),
        [
          ["Food", "Omicho market for lunch", "Kanazawa-style curry"],
          ["", "Kenroku-en at opening"],
        ],
        "Each group with items renders its heading, if any, over its items"
      );
    },
    [
      [
        {
          heading: "Food",
          items: [
            { text: "Omicho market for lunch" },
            { text: "Kanazawa-style curry" },
          ],
        },
        { items: [{ text: "Kenroku-en at opening" }] },
        // A group the model left empty would otherwise render as a heading
        // with nothing under it.
        { heading: "Practical", items: [] },
      ],
    ]
  );
});

add_task(async function test_list_layout() {
  await withAITabDocument(async () => {
    await content.customElements.whenDefined("aitab-list");
    const element = content.document.createElement("aitab-list");
    content.document.body.append(element);
    const list = element.wrappedJSObject;
    list.groups = Cu.cloneInto(
      [{ items: [{ text: "Sear the thighs" }] }],
      content
    );
    await list.updateComplete;

    Assert.equal(
      list.getAttribute("layout"),
      "column",
      "The intro sits above the groups unless the props say otherwise"
    );

    list.layout = "row";
    await list.updateComplete;

    Assert.equal(
      list.getAttribute("layout"),
      "row",
      "The layout reflects to the attribute the stylesheet selects on"
    );
  });
});

add_task(async function test_list_styles() {
  await withAITabDocument(async () => {
    await content.customElements.whenDefined("aitab-list");
    const element = content.document.createElement("aitab-list");
    content.document.body.append(element);
    const list = element.wrappedJSObject;
    list.groups = Cu.cloneInto(
      [
        { heading: "Food", items: [{ text: "Omicho" }, { text: "Curry" }] },
        { heading: "Sights", items: [{ text: "Kenroku-en" }] },
      ],
      content
    );
    await list.updateComplete;

    const root = list.shadowRoot;
    const item = content.getComputedStyle(
      root.querySelector(".aitab-list-item")
    );
    Assert.equal(item.paddingBlockStart, "12px", "Items have 12px above");
    Assert.equal(item.paddingBlockEnd, "14px", "Items have 14px below");

    const groups = content.getComputedStyle(
      root.querySelector(".aitab-list-groups")
    );
    Assert.equal(groups.rowGap, "36px", "Stacked groups are 36px apart");
    Assert.equal(
      groups.columnGap,
      "36px",
      "Side by side groups are 36px apart"
    );
  });
});

add_task(async function test_timeline_items() {
  await withAITabDocument(
    async items => {
      await content.customElements.whenDefined("aitab-timeline");
      const element = content.document.createElement("aitab-timeline");
      content.document.body.append(element);
      const timeline = element.wrappedJSObject;
      await timeline.updateComplete;

      Assert.ok(
        !timeline.shadowRoot.querySelector(".aitab-timeline"),
        "Nothing is rendered before there are any entries"
      );

      timeline.title = "Three days";
      timeline.items = Cu.cloneInto(items, content);
      await timeline.updateComplete;

      Assert.equal(
        timeline.shadowRoot
          .querySelector(".aitab-timeline-title")
          .textContent.trim(),
        "Three days",
        "The block title renders beside the entries"
      );
      Assert.deepEqual(
        [...timeline.shadowRoot.querySelectorAll(".aitab-timeline-item")].map(
          item =>
            [
              ".aitab-timeline-date-label",
              ".aitab-timeline-date-eyebrow",
              ".aitab-timeline-item-title",
              ".aitab-timeline-item-description",
            ].map(selector =>
              (item.querySelector(selector)?.textContent ?? "").trim()
            )
        ),
        [
          ["Thu, Oct 9", "Arrival", "Korinbo & the 21st Century Museum", ""],
          ["Fri, Oct 10", "", "Kenroku-en, castle, Nagamachi", "Garden first."],
        ],
        "Entries keep their order, and each field is left out when unset"
      );
      Assert.equal(
        timeline.shadowRoot.querySelector(".aitab-timeline-item-title")
          .localName,
        "h3",
        "Entry titles sit under the block title"
      );

      timeline.title = "";
      await timeline.updateComplete;

      Assert.equal(
        timeline.shadowRoot.querySelector(".aitab-timeline-item-title")
          .localName,
        "h2",
        "Entry titles step up a level when the block has no title of its own"
      );
    },
    [
      [
        {
          date_label: "Thu, Oct 9",
          date_eyebrow: "Arrival",
          title: "Korinbo & the 21st Century Museum",
        },
        {
          date_label: "Fri, Oct 10",
          title: "Kenroku-en, castle, Nagamachi",
          description: "Garden first.",
        },
        // An entry the model left without a date or a title has nothing to
        // show, and would otherwise render as a blank row.
        { description: "Somewhere, at some point." },
      ],
    ]
  );
});

add_task(async function test_highlights_renders_statements() {
  await withAITabDocument(
    async items => {
      await content.customElements.whenDefined("aitab-highlights");
      const element = content.document.createElement("aitab-highlights");
      content.document.body.append(element);
      const highlights = element.wrappedJSObject;

      highlights.title = "A day on Niijima";
      highlights.items = Cu.cloneInto(items, content);
      await highlights.updateComplete;

      const shadow = highlights.shadowRoot;
      const title = shadow.querySelector("h2");
      Assert.equal(title.textContent.trim(), "A day on Niijima", "Title");
      const list = shadow.querySelector("ul");
      Assert.equal(
        list.getAttribute("aria-labelledby"),
        title.id,
        "The list is named by the title"
      );

      const rows = [...shadow.querySelectorAll("li")];
      Assert.equal(rows.length, 3, "One row per statement");
      Assert.deepEqual(
        rows.map(row => [
          row.querySelector(".aitab-eyebrow")?.textContent ?? null,
          row.querySelector(".aitab-highlight-title")?.textContent.trim() ??
            null,
          row.querySelector(".aitab-highlight-body")?.textContent ?? null,
        ]),
        [
          ["Morning", "Habushiura beach", "White sand and turquoise water."],
          [null, "Overnight ferry", "Departs from Takeshiba Pier in Tokyo."],
          [null, null, "A statement can be a bare sentence of evidence."],
        ],
        "Eyebrow, statement and evidence render only when the item has them"
      );

      Assert.ok(!shadow.querySelector("h3"), "Statements are not headings");

      highlights.title = "";
      await highlights.updateComplete;
      Assert.ok(!shadow.querySelector("h2"), "No title element");
      Assert.ok(
        !shadow.querySelector("ul").hasAttribute("aria-labelledby"),
        "No dangling aria-labelledby without a title"
      );

      highlights.items = null;
      await highlights.updateComplete;
      Assert.ok(
        !shadow.querySelector("ul"),
        "A null items list renders nothing"
      );
    },
    [
      [
        {
          eyebrow: "Morning",
          title: "Habushiura beach",
          body: "White sand and turquoise water.",
        },
        {
          title: "Overnight ferry",
          body: "Departs from Takeshiba Pier in Tokyo.",
        },
        { body: "A statement can be a bare sentence of evidence." },
      ],
    ]
  );
});

add_task(async function test_highlights_source_chips() {
  await withAITabDocument(
    async items => {
      await content.customElements.whenDefined("aitab-highlights");
      const element = content.document.createElement("aitab-highlights");
      content.document.body.append(element);
      const highlights = element.wrappedJSObject;

      highlights.items = Cu.cloneInto(items, content);
      await highlights.updateComplete;

      const chips = [...highlights.shadowRoot.querySelectorAll("li")].map(row =>
        [...row.querySelectorAll("smartwindow-chip")].map(chip => ({
          label: chip.label,
          href: chip.href,
          icon: chip.iconSrc,
          event: chip.getAttribute("openLinkEvent"),
        }))
      );
      Assert.deepEqual(
        chips,
        [
          [
            {
              label: "Expedia",
              href: "https://www.expedia.com/niijima",
              icon: "chrome://branding/content/icon16.png",
              event: "AITab:OpenLink",
            },
            {
              label: "www.tripadvisor.com",
              href: "https://www.tripadvisor.com/habushiura",
              icon: "page-icon:https://www.tripadvisor.com/habushiura",
              event: "AITab:OpenLink",
            },
          ],
          [],
        ],
        "Each http(s) source is a chip wired to the AI Tab open-link event; " +
          "the label falls back to the host and the icon to page-icon"
      );
      Assert.ok(
        !highlights.shadowRoot.querySelector(
          "li:nth-child(2) .aitab-highlight-sources"
        ),
        "No sources container when nothing is linkable"
      );
    },
    [
      [
        {
          title: "Habushiura beach",
          body: "White sand and turquoise water.",
          sources: {
            items: [
              {
                title: "Expedia",
                href: "https://www.expedia.com/niijima",
                favicon: "chrome://branding/content/icon16.png",
              },
              { href: "https://www.tripadvisor.com/habushiura" },
            ],
          },
        },
        {
          title: "Overnight ferry",
          sources: {
            items: [{ title: "Route", href: "app://views/relayout" }],
          },
        },
      ],
    ]
  );
});

add_task(async function test_text_block_content() {
  await withAITabDocument(
    async (heading, paragraphs) => {
      await content.customElements.whenDefined("aitab-text-block");
      const element = content.document.createElement("aitab-text-block");
      content.document.body.append(element);
      const block = element.wrappedJSObject;
      await block.updateComplete;

      Assert.ok(
        !block.shadowRoot.querySelector(".aitab-text-block-p"),
        "No paragraph is rendered before there are any"
      );
      Assert.ok(
        !block.shadowRoot.querySelector("h2"),
        "No empty heading is rendered without a heading"
      );

      block.heading = heading;
      block.paragraphs = Cu.cloneInto(paragraphs, content);
      await block.updateComplete;

      Assert.equal(
        block.shadowRoot
          .querySelector(".aitab-text-block-heading")
          .textContent.trim(),
        heading,
        "The heading renders"
      );
      Assert.deepEqual(
        [...block.shadowRoot.querySelectorAll(".aitab-text-block-p")].map(p =>
          p.textContent.trim()
        ),
        paragraphs,
        "Each paragraph renders, in order"
      );
    },
    [
      "Three nights is enough for Kanazawa",
      [
        "Everything sits inside a 25-minute walk.",
        "The ryokan is the trade-off.",
      ],
    ]
  );
});

add_task(async function test_text_block_references() {
  await withAITabDocument(
    async references => {
      await content.customElements.whenDefined("aitab-text-block");
      const element = content.document.createElement("aitab-text-block");
      content.document.body.append(element);
      const block = element.wrappedJSObject;
      await block.updateComplete;

      Assert.ok(
        !block.shadowRoot.querySelector(".aitab-text-block-references"),
        "No references row is rendered when there are no references"
      );

      block.references = Cu.cloneInto(references, content);
      await block.updateComplete;

      const chips = [
        ...block.shadowRoot.querySelectorAll(".aitab-text-block-chip"),
      ];
      Assert.deepEqual(
        chips.map(chip => [chip.href, chip.label]),
        [
          ["https://energy.gov", "energy.gov"],
          ["https://neep.org", "https://neep.org"],
        ],
        "Each reference becomes a chip, labelled by its href when untitled"
      );

      // smartwindow-chip is shared with chat and defaults to chat's event
      // name, so without this the AITab actor would never hear about a click.
      Assert.ok(
        chips.every(chip => chip.openLinkEvent == "AITab:OpenLink"),
        "Each chip is wired to the AI Tab open-link event, not chat's"
      );
    },
    [
      [
        { title: "energy.gov", href: "https://energy.gov" },
        { href: "https://neep.org" },
      ],
    ]
  );
});

const IMAGE_SRC =
  "chrome://browser/content/aiwindow/assets/aitab-error-kit.svg";

add_task(async function test_image_renders() {
  await withAITabDocument(
    async src => {
      await content.customElements.whenDefined("aitab-image");
      const element = content.document.createElement("aitab-image");
      // Lazy images only load once on screen, so keep it above the page.
      content.document.body.prepend(element);
      const image = element.wrappedJSObject;
      await image.updateComplete;

      Assert.ok(
        !image.shadowRoot.querySelector(".aitab-image"),
        "Nothing is rendered without a src"
      );

      image.src = src;
      image.alt = "Smart Page illustration";
      await image.updateComplete;
      const img = image.shadowRoot.querySelector("img");
      Assert.equal(img.alt, "Smart Page illustration", "The alt text renders");
      await ContentTaskUtils.waitForCondition(
        () => img.complete && img.naturalWidth,
        "The image loads"
      );
    },
    [IMAGE_SRC]
  );
});

add_task(async function test_image_fails_quietly() {
  await withAITabDocument(
    async (src, blockedSrc) => {
      await content.customElements.whenDefined("aitab-image");
      const element = content.document.createElement("aitab-image");
      // Lazy images only load once on screen, so keep it above the page.
      content.document.body.prepend(element);
      const image = element.wrappedJSObject;

      image.src = blockedSrc;
      await image.updateComplete;
      Assert.ok(
        image.shadowRoot.querySelector(".aitab-image"),
        "The frame holds the space while the image loads"
      );
      await ContentTaskUtils.waitForCondition(
        () => !image.shadowRoot.querySelector(".aitab-image"),
        "Nothing is rendered once the page CSP blocks the image"
      );

      image.src = src;
      await image.updateComplete;
      Assert.ok(
        image.shadowRoot.querySelector("img"),
        "A new src is tried again after a failure"
      );
    },
    [
      IMAGE_SRC,
      // Remote images are blocked until they have a vetted path to the page.
      "https://example.com/image.png",
    ]
  );
});

add_task(async function test_image_radius_follows_width() {
  await withAITabDocument(
    async src => {
      await content.customElements.whenDefined("aitab-image");
      const element = content.document.createElement("aitab-image");
      content.document.body.append(element);
      const image = element.wrappedJSObject;
      image.src = src;
      await image.updateComplete;
      const frame = image.shadowRoot.querySelector(".aitab-image");

      const radii = [640, 374, 144].map(width => {
        element.style.inlineSize = `${width}px`;
        return content.getComputedStyle(frame).borderTopLeftRadius;
      });
      Assert.deepEqual(
        radii,
        ["50px", "24px", "10px"],
        "Lead images, cards and thumbnails each get their own corner radius"
      );
      Assert.equal(
        frame.getBoundingClientRect().height,
        81,
        "The frame keeps a 16:9 aspect ratio"
      );
    },
    [IMAGE_SRC]
  );
});

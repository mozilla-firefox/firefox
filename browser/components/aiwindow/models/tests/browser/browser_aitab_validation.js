/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Exercises the real packaged A2UI catalog (fetched via loadAssets from
// chrome://): buildSurface() validates a surface (a flat list of component
// instances + a data model) against the catalog and returns it unchanged.

const { AITab } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs"
);
const { loadAssets, buildSurface } = AITab;
const { UrlTokenizer } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/UrlTokenizer.sys.mjs"
);
const { A2UI } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/A2UI.sys.mjs"
);

let gEnv;

add_setup(async function () {
  // Raise AITab's log verbosity for the duration of this file so a failing run
  // surfaces the module's debug/error output. Reverted automatically at teardown.
  await SpecialPowers.pushPrefEnv({
    set: [["browser.smartwindow.conversation.logLevel", "Debug"]],
  });
  const { env } = await loadAssets();
  gEnv = env;
});

// A minimal valid surface: a root Page referencing a Header, a TextBlock, and a
// RankedTable whose rows are bound into the data model.
const VALID_SURFACE = Object.freeze({
  catalogId: "https://aitab.mozilla.org/catalogs/aitab/catalog.json",
  components: [
    {
      id: "root",
      component: "Page",
      header: "hdr",
      children: ["intro", "tbl"],
    },
    { id: "hdr", component: "Header", title: "Installer Quotes" },
    { id: "intro", component: "TextBlock", lead: "Three quotes, compared." },
    {
      id: "tbl",
      component: "RankedTable",
      columns: [
        { key: "name", type: "text", role: "title" },
        { key: "price", type: "currency", role: "detail", goal: "min" },
      ],
      rows: { path: "/quotes" },
    },
  ],
  dataModel: { quotes: [{ name: "Bright Build", price: 31200 }] },
});

function createRepeatedTextBlockSurface(entries) {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[0].children = {
    componentId: "intro",
    path: "/entries",
  };
  surface.components[2].lead = { path: "lead" };
  surface.components[2].image = { path: "hero" };
  surface.dataModel.entries = entries;
  return surface;
}

// Page requires a `header`, so the fixtures below that are about something
// else pair their root with this minimal one.
const HEADER = Object.freeze({ id: "hdr", component: "Header", title: "T" });

add_task(function test_valid_surface_validates() {
  const result = buildSurface(structuredClone(VALID_SURFACE), gEnv);
  Assert.ok(
    result.ok,
    `the surface validates; errors: ${JSON.stringify(result.errors)}`
  );
  Assert.deepEqual(
    result.surface,
    VALID_SURFACE,
    "the surface is returned unchanged"
  );
});

add_task(function test_literal_table_row_image_validates() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[3].rows = [
    {
      name: "Bright Build",
      price: 31200,
      image: {
        src: "https://example.com/image.png",
        alt_text: "Example image",
      },
    },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_bound_table_row_image_validates() {
  const surface = structuredClone(VALID_SURFACE);
  surface.dataModel.quotes[0].image = {
    src: "https://example.com/image.png",
    alt_text: "Example image",
  };

  const result = buildSurface(surface, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_literal_table_row_string_image_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[3].rows = [
    {
      name: "Bright Build",
      price: 31200,
      image: "https://example.com/image.png",
    },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a literal table row image must be an object");
});

add_task(function test_bound_table_row_string_image_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.dataModel.quotes[0].image = "https://example.com/image.png";

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a bound table row image must be an object");
});

add_task(function test_literal_table_row_image_without_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[3].rows = [
    {
      name: "Bright Build",
      price: 31200,
      image: { alt_text: "Missing source" },
    },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a literal table row image requires src");
});

add_task(function test_bound_table_row_image_without_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.dataModel.quotes[0].image = { alt_text: "Missing source" };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a bound table row image requires src");
});

add_task(function test_relative_template_image_validates() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[0].children = {
    componentId: "intro",
    path: "/entries",
  };
  surface.components[2].image = { path: "hero" };
  surface.dataModel.entries = [
    { hero: { src: "https://example.com/image.png" } },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_relative_template_string_image_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[0].children = {
    componentId: "intro",
    path: "/entries",
  };
  surface.components[2].image = { path: "hero" };
  surface.dataModel.entries = [
    { hero: { src: "https://example.com/image.png" } },
    { hero: "https://example.com/unvetted.png" },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "each repeated item's image must be an object");
});

add_task(function test_relative_template_image_without_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[0].children = {
    componentId: "intro",
    path: "/entries",
  };
  surface.components[2].image = { path: "hero" };
  surface.dataModel.entries = [{ hero: { alt_text: "Missing source" } }];

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a relative image binding requires src");
});

add_task(function test_relative_template_missing_image_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[0].children = {
    componentId: "intro",
    path: "/entries",
  };
  surface.components[2].image = { path: "hero" };
  surface.dataModel.entries = [{}];

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a relative image binding must resolve");
});

add_task(function test_relative_table_row_string_image_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[0].children = {
    componentId: "tbl",
    path: "/entries",
  };
  surface.components[3].rows = { path: "quotes" };
  surface.dataModel.entries = [
    {
      quotes: [
        {
          name: "Bright Build",
          price: 31200,
          image: "https://example.com/unvetted.png",
        },
      ],
    },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "relative table rows enforce the image schema");
});

add_task(function test_inline_text_block_image_validates() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = {
    src: "https://example.com/image.png",
    alt_text: "Example image",
  };

  const result = buildSurface(surface, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
  Assert.deepEqual(result.surface, surface, "the image is preserved");
});

add_task(function test_bound_text_block_image_validates() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };
  surface.dataModel.hero = {
    src: "https://example.com/image.png",
    alt_text: "Example image",
  };

  const result = buildSurface(surface, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
  Assert.deepEqual(result.surface, surface, "the image binding is preserved");
});

add_task(function test_unregistered_bound_image_is_removed() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };
  surface.dataModel.hero = {
    src: "https://example.com/unregistered.png",
    alt_text: "Example image",
  };

  const expanded = AITab.expandSurfaceUrlTokens(surface, new UrlTokenizer());
  Assert.ok(
    !Object.hasOwn(expanded.dataModel, "hero"),
    "an unregistered image source removes the bound image object"
  );

  Assert.ok(
    !Object.hasOwn(expanded.components[2], "image"),
    "the binding to the filtered image is removed"
  );

  const result = buildSurface(expanded, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_unknown_bound_image_token_is_removed() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };
  surface.dataModel.hero = {
    src: "§url_token: UNKNOWN_IMAGE_1§",
    alt_text: "Example image",
  };

  const expanded = AITab.expandSurfaceUrlTokens(surface, new UrlTokenizer());
  Assert.ok(
    !Object.hasOwn(expanded.dataModel, "hero"),
    "the image with an unknown token is removed"
  );
  Assert.ok(
    !Object.hasOwn(expanded.components[2], "image"),
    "the binding to the filtered image is removed"
  );

  const result = buildSurface(expanded, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_filtered_parent_removes_header_subhead_binding() {
  const tokenizer = new UrlTokenizer();
  const surface = structuredClone(VALID_SURFACE);
  surface.components[1].subhead = { path: "/source/description" };
  surface.dataModel.source = {
    href: tokenizer.tokenizeText("https://example.com/disallowed"),
    description: "Additional context",
  };

  const expanded = AITab.expandSurfaceUrlTokens(surface, tokenizer, new Set());

  Assert.ok(
    !Object.hasOwn(expanded.dataModel, "source"),
    "a registered but disallowed link removes its containing object"
  );
  Assert.ok(
    !Object.hasOwn(expanded.components[1], "subhead"),
    "the Header binding to a descendant of the removed object is removed"
  );

  const result = buildSurface(expanded, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_filtered_textblock_images_hydrate_independently() {
  const tokenizer = new UrlTokenizer();
  const imageUrl = "https://example.com/image.png";
  const surface = createRepeatedTextBlockSurface([
    { lead: "Paris", hero: { src: tokenizer.tokenizeText(imageUrl) } },
    { lead: "Rome", hero: { src: "§url_token: UNKNOWN_IMAGE_1§" } },
  ]);
  const original = structuredClone(surface);

  const expanded = AITab.expandSurfaceUrlTokens(surface, tokenizer);
  const [firstId, secondId] = expanded.components[0].children;
  const first = expanded.components.find(component => component.id === firstId);
  const second = expanded.components.find(
    component => component.id === secondId
  );

  Assert.deepEqual(first.image, { path: "/entries/0/hero" });
  Assert.equal(expanded.dataModel.entries[0].hero.src, imageUrl);
  Assert.ok(
    !Object.hasOwn(second, "image"),
    "the filtered image binding is removed"
  );
  Assert.deepEqual(surface, original, "the input surface is unchanged");

  const result = buildSurface(expanded, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));

  const page = new A2UI(result.surface).toUI();
  const [firstBlock, secondBlock] = page.children.slice(1);

  Assert.deepEqual(
    firstBlock.image,
    { src: imageUrl },
    "the surviving image reaches the hydrated TextBlock"
  );
  Assert.ok(
    !Object.hasOwn(secondBlock, "image"),
    "the other TextBlock hydrates without the filtered image"
  );
});

add_task(function test_table_hydration_hides_images_after_one_is_filtered() {
  const tokenizer = new UrlTokenizer();
  const imageUrl = "https://example.com/image.png";
  const surface = structuredClone(VALID_SURFACE);
  surface.dataModel.quotes = [
    {
      name: "Bright Build",
      price: 31200,
      image: { src: tokenizer.tokenizeText(imageUrl) },
    },
    {
      name: "Sterling Mechanical",
      price: 34000,
      image: { src: "§url_token: UNKNOWN_IMAGE_1§" },
    },
  ];

  const expanded = AITab.expandSurfaceUrlTokens(surface, tokenizer);
  const result = buildSurface(expanded, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));

  const page = new A2UI(result.surface).toUI();
  const table = page.children.find(
    component => component.component === "RankedTable"
  );

  Assert.equal(table.rows.length, 2, "both rows survive filtering");
  Assert.ok(
    table.rows.every(row => !Object.hasOwn(row, "image")),
    "hydration hides every row image when one image was filtered"
  );
  Assert.equal(
    result.surface.dataModel.quotes[0].image.src,
    imageUrl,
    "the valid image remains in the surface data model"
  );
  Assert.ok(
    !Object.hasOwn(result.surface.dataModel.quotes[1], "image"),
    "URL filtering removed the unknown image from the data model"
  );
});

add_task(
  function test_originally_missing_textblock_image_still_fails_validation() {
    const surface = createRepeatedTextBlockSurface([
      { lead: "Paris" },
      { lead: "Rome", hero: { src: "§url_token: UNKNOWN_IMAGE_1§" } },
    ]);

    const expanded = AITab.expandSurfaceUrlTokens(surface, new UrlTokenizer());
    const first = expanded.components.find(
      component => component.id === "intro"
    );

    Assert.deepEqual(
      first.image,
      { path: "/entries/0/hero" },
      "the originally missing target keeps its binding"
    );

    const result = buildSurface(expanded, gEnv);
    Assert.ok(
      !result.ok,
      "the originally missing target still fails validation"
    );
    Assert.ok(
      result.errors.some(error => error.message?.includes("/entries/0/hero")),
      "validation identifies the originally missing target"
    );
  }
);

add_task(function test_textblock_bindings_follow_surviving_repeater_items() {
  const tokenizer = new UrlTokenizer();
  const surface = createRepeatedTextBlockSurface([
    { href: "§url_token: UNKNOWN_PAGE_1§" },
    {
      lead: "Paris",
      hero: { src: tokenizer.tokenizeText("https://example.com/image.png") },
    },
    { lead: "Rome", hero: { src: "§url_token: UNKNOWN_IMAGE_1§" } },
  ]);

  const expanded = AITab.expandSurfaceUrlTokens(surface, tokenizer);
  const first = expanded.components.find(component => component.id === "intro");

  Assert.equal(
    expanded.dataModel.entries.length,
    2,
    "the first repeater item was filtered"
  );
  Assert.deepEqual(first.image, { path: "/entries/0/hero" });
  Assert.deepEqual(first.lead, { path: "/entries/0/lead" });

  const result = buildSurface(expanded, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_repeated_textblock_ids_avoid_existing_component_ids() {
  const surface = createRepeatedTextBlockSurface([
    { lead: "Paris", hero: { src: "§url_token: UNKNOWN_IMAGE_1§" } },
    { lead: "Rome", hero: { src: "§url_token: UNKNOWN_IMAGE_1§" } },
  ]);
  surface.components.push({
    id: "intro-1",
    component: "TextBlock",
    lead: "Reserved id",
  });

  const expanded = AITab.expandSurfaceUrlTokens(surface, new UrlTokenizer());

  Assert.deepEqual(
    expanded.components[0].children,
    ["intro", "intro-1-"],
    "generated ids avoid existing component ids"
  );

  const result = buildSurface(expanded, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_originally_missing_bound_image_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };

  const expanded = AITab.expandSurfaceUrlTokens(surface, new UrlTokenizer());
  Assert.deepEqual(
    expanded.components[2].image,
    { path: "/hero" },
    "a binding whose target was originally missing is preserved"
  );

  const result = buildSurface(expanded, gEnv);
  Assert.ok(!result.ok, "the originally missing image target is rejected");
  Assert.ok(
    result.errors.some(error => error.message?.includes("/hero")),
    "validation reports the missing image path"
  );
});

add_task(function test_bound_image_without_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };
  surface.dataModel.hero = { alt_text: "Example image" };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a bound image requires src");
});

add_task(function test_bound_image_with_non_string_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };
  surface.dataModel.hero = { src: 42 };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a bound image source must be a string");
});

add_task(function test_bound_image_with_non_string_alt_text_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };
  surface.dataModel.hero = {
    src: "https://example.com/image.png",
    alt_text: 42,
  };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "bound image alt text must be a string");
});

add_task(function test_bound_image_with_unknown_property_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };
  surface.dataModel.hero = {
    src: "https://example.com/image.png",
    unexpected: true,
  };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "unknown bound image properties are rejected");
});

add_task(function test_bound_image_with_non_object_value_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { path: "/hero" };
  surface.dataModel.hero = "https://example.com/image.png";

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "a bound image must be an object");
});

add_task(function test_image_without_alt_text_validates() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { src: "https://example.com/image.png" };

  const result = buildSurface(surface, gEnv);
  Assert.ok(result.ok, "alt_text is optional");
});

add_task(function test_image_without_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { alt_text: "Example image" };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "an image requires src");
});

add_task(function test_image_with_non_string_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = { src: 42 };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "an image source must be a string");
});

add_task(function test_image_with_non_string_alt_text_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = {
    src: "https://example.com/image.png",
    alt_text: 42,
  };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "image alt text must be a string");
});

add_task(function test_image_with_unknown_property_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2].image = {
    src: "https://example.com/image.png",
    unexpected: true,
  };

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "unknown image properties are rejected");
});

add_task(function test_bound_timeline_image_validates() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2] = {
    id: "intro",
    component: "Timeline",
    items: { path: "/events" },
  };
  surface.dataModel.events = [
    {
      date_label: "Today",
      title: "Example event",
      image: {
        src: "https://example.com/event.png",
        alt_text: "Event image",
      },
    },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_bound_timeline_image_without_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2] = {
    id: "intro",
    component: "Timeline",
    items: { path: "/events" },
  };
  surface.dataModel.events = [
    {
      date_label: "Today",
      title: "Example event",
      image: { alt_text: "Event image" },
    },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "bound timeline images require src");
});

add_task(function test_bound_highlight_image_validates() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2] = {
    id: "intro",
    component: "Highlights",
    items: { path: "/highlights" },
  };
  surface.dataModel.highlights = [
    {
      title: "Example highlight",
      image: {
        src: "https://example.com/highlight.png",
        alt_text: "Highlight image",
      },
    },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(result.ok, JSON.stringify(result.errors));
});

add_task(function test_bound_highlight_image_without_src_is_rejected() {
  const surface = structuredClone(VALID_SURFACE);
  surface.components[2] = {
    id: "intro",
    component: "Highlights",
    items: { path: "/highlights" },
  };
  surface.dataModel.highlights = [
    {
      title: "Example highlight",
      image: { alt_text: "Highlight image" },
    },
  ];

  const result = buildSurface(surface, gEnv);
  Assert.ok(!result.ok, "bound highlight images require src");
});

add_task(function test_components_must_be_an_array() {
  const result = buildSurface({ dataModel: {} }, gEnv);
  Assert.ok(!result.ok, "a surface without a components array is rejected");
});

add_task(function test_requires_exactly_one_root() {
  const result = buildSurface(
    { components: [{ id: "a", component: "Header", title: "x" }] },
    gEnv
  );
  Assert.ok(!result.ok, "a surface with no `root` component is rejected");
});

add_task(function test_root_must_be_a_page() {
  const result = buildSurface(
    { components: [{ id: "root", component: "Header", title: "x" }] },
    gEnv
  );
  Assert.ok(!result.ok, "a root that is not a Page is rejected");
});

add_task(function test_unknown_component_type() {
  const result = buildSurface(
    {
      components: [
        { id: "root", component: "Page", header: "hdr", children: ["b"] },
        HEADER,
        { id: "b", component: "Banner" },
      ],
    },
    gEnv
  );
  Assert.ok(!result.ok, "an unknown component type is rejected");
});

add_task(function test_component_missing_required_prop() {
  const result = buildSurface(
    {
      components: [
        { id: "root", component: "Page", header: "h", children: [] },
        { id: "h", component: "Header" },
      ],
    },
    gEnv
  );
  Assert.ok(!result.ok, "a Header without its required `title` is rejected");
});

add_task(function test_additional_property_rejected() {
  const result = buildSurface(
    {
      components: [
        {
          id: "root",
          component: "Page",
          header: "hdr",
          children: [],
          bogus: 1,
        },
        HEADER,
      ],
    },
    gEnv
  );
  Assert.ok(!result.ok, "an unexpected property on a component is rejected");
});

add_task(function test_dangling_id_reference() {
  const result = buildSurface(
    {
      components: [
        { id: "root", component: "Page", header: "hdr", children: ["ghost"] },
        HEADER,
      ],
    },
    gEnv
  );
  Assert.ok(!result.ok, "a child id that no component defines is rejected");
});

add_task(function test_unresolved_data_binding() {
  const result = buildSurface(
    {
      components: [
        { id: "root", component: "Page", header: "hdr", children: ["s"] },
        HEADER,
        { id: "s", component: "Cards", items: { path: "/missing" } },
      ],
      dataModel: {},
    },
    gEnv
  );
  Assert.ok(
    !result.ok,
    "an absolute binding path absent from dataModel is rejected"
  );
});

// A List whose single group binds its items into the data model, as models
// emit it: ListGroup.items only accepts a literal array.
function listGroupBindingSurface(items) {
  return {
    components: [
      { id: "root", component: "Page", header: "hdr", children: ["list"] },
      HEADER,
      {
        id: "list",
        component: "List",
        layout: "row",
        groups: [{ items: { path: "/attractions" } }],
      },
    ],
    dataModel: { attractions: items },
  };
}

const ATTRACTIONS = Object.freeze([
  { text: "CN Tower: Panoramic views, glass floor, and the EdgeWalk." },
  { text: "Casa Loma: North America's only full-sized Gothic Revival castle." },
  { text: "Toronto Islands: Car-free islands with beaches and parks." },
]);

add_task(function test_list_group_items_binding_is_inlined() {
  const surface = listGroupBindingSurface(structuredClone(ATTRACTIONS));
  Assert.ok(
    !buildSurface(structuredClone(surface), gEnv).ok,
    "a binding inside a List group is rejected as written"
  );

  const result = buildSurface(AITab.inlineListGroupItems(surface), gEnv);
  Assert.ok(
    result.ok,
    `the inlined surface validates; errors: ${JSON.stringify(result.errors)}`
  );
  const [group] = result.surface.components.find(c => c.id == "list").groups;
  Assert.deepEqual(group.items, ATTRACTIONS, "the bound items are inlined");
  Assert.notStrictEqual(
    group.items,
    result.surface.dataModel.attractions,
    "the inlined items are a copy of the data model array"
  );
});

add_task(function test_items_binding_inside_bound_groups_is_inlined() {
  const surface = listGroupBindingSurface(structuredClone(ATTRACTIONS));
  surface.components[2].groups = { path: "/groups" };
  surface.dataModel.groups = [
    { heading: "Landmarks", items: { path: "/attractions" } },
  ];
  Assert.ok(
    !buildSurface(structuredClone(surface), gEnv).ok,
    "a binding inside a bound List group is rejected as written"
  );

  const result = buildSurface(AITab.inlineListGroupItems(surface), gEnv);
  Assert.ok(
    result.ok,
    `the inlined surface validates; errors: ${JSON.stringify(result.errors)}`
  );
  Assert.deepEqual(
    result.surface.components.find(c => c.id == "list").groups,
    { path: "/groups" },
    "the groups binding is kept"
  );
  const [group] = result.surface.dataModel.groups;
  Assert.equal(group.heading, "Landmarks", "the group heading is kept");
  Assert.deepEqual(group.items, ATTRACTIONS, "the bound items are inlined");
  Assert.notStrictEqual(
    group.items,
    result.surface.dataModel.attractions,
    "the inlined items are a copy of the data model array"
  );
});

add_task(function test_unresolved_list_group_binding_is_left_unchanged() {
  const surface = listGroupBindingSurface(structuredClone(ATTRACTIONS));
  surface.components[2].groups[0].items = { path: "/missing" };
  const before = structuredClone(surface);

  AITab.inlineListGroupItems(surface);
  Assert.deepEqual(surface, before, "an unresolved binding is not touched");
  Assert.ok(
    !buildSurface(surface, gEnv).ok,
    "an unresolved binding inside a List group is still rejected"
  );
});

add_task(function test_inlined_list_group_items_are_still_validated() {
  const surface = listGroupBindingSurface([
    { name: "CN Tower", desc: "Panoramic views." },
  ]);
  const result = buildSurface(AITab.inlineListGroupItems(surface), gEnv);
  Assert.ok(!result.ok, "inlined items that are not ListItems are rejected");
});

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

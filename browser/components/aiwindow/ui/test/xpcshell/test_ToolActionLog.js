/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

do_get_profile();

const {
  getActionLogConfigForTool,
  getActionLogChipsForTool,
  buildActionLogRow,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ToolActionLog.sys.mjs"
);

const {
  GET_OPEN_TABS,
  SEARCH_BROWSING_HISTORY,
  GET_USER_MEMORIES,
  GET_NAVIGATION_INFO,
  SEARCH_THE_WEB,
  GENERATE_AITAB,
} = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

add_task(function test_aitab_pending_creation_without_modify_slug() {
  const cfg = getActionLogConfigForTool(GENERATE_AITAB, { pending: true }, {});

  Assert.equal(cfg.label.l10nId, "smartwindow-aitab-creating");
  Assert.equal(cfg.pendingLabel.l10nId, "smartwindow-aitab-creating");
});

add_task(function test_aitab_pending_creation_with_empty_modify_slug() {
  const cfg = getActionLogConfigForTool(
    GENERATE_AITAB,
    { pending: true },
    { modify_slug: "" }
  );

  Assert.equal(cfg.label.l10nId, "smartwindow-aitab-creating");
  Assert.equal(cfg.pendingLabel.l10nId, "smartwindow-aitab-creating");
});

add_task(function test_aitab_pending_creation_with_blank_modify_slug() {
  const cfg = getActionLogConfigForTool(
    GENERATE_AITAB,
    { pending: true },
    { modify_slug: "   " }
  );

  Assert.equal(cfg.label.l10nId, "smartwindow-aitab-creating");
  Assert.equal(cfg.pendingLabel.l10nId, "smartwindow-aitab-creating");
});

add_task(function test_aitab_pending_creation_with_non_string_modify_slug() {
  const cfg = getActionLogConfigForTool(
    GENERATE_AITAB,
    { pending: true },
    { modify_slug: 42 }
  );

  Assert.equal(cfg.label.l10nId, "smartwindow-aitab-creating");
  Assert.equal(cfg.pendingLabel.l10nId, "smartwindow-aitab-creating");
});

add_task(function test_aitab_pending_update_with_existing_page() {
  const cfg = getActionLogConfigForTool(
    GENERATE_AITAB,
    { pending: true },
    { modify_slug: "existing-page" }
  );

  Assert.equal(cfg.label.l10nId, "smartwindow-aitab-updating");
  Assert.equal(cfg.pendingLabel.l10nId, "smartwindow-aitab-updating");
});

add_task(function test_aitab_completed_creation_labels() {
  const cfg = getActionLogConfigForTool(
    GENERATE_AITAB,
    { page: { slug: "new-page" } },
    {}
  );

  Assert.equal(cfg.label.l10nId, "smartwindow-aitab-created-an-aitab");
  Assert.equal(cfg.pendingLabel.l10nId, "smartwindow-aitab-creating");
});

add_task(function test_aitab_completed_update_labels() {
  const cfg = getActionLogConfigForTool(
    GENERATE_AITAB,
    { page: { slug: "existing-page" } },
    { modify_slug: "existing-page" }
  );

  Assert.equal(cfg.label.l10nId, "smartwindow-aitab-updated-an-aitab");
  Assert.equal(cfg.pendingLabel.l10nId, "smartwindow-aitab-updating");
});

add_task(function test_getActionLogConfigForTool_visible_tools() {
  Assert.withSoftAssertions(function (soft) {
    for (const toolName of [
      GET_OPEN_TABS,
      SEARCH_BROWSING_HISTORY,
      GET_USER_MEMORIES,
      GET_NAVIGATION_INFO,
      SEARCH_THE_WEB,
    ]) {
      const cfg = getActionLogConfigForTool(toolName, []);

      soft.strictEqual(cfg.show, true, `${toolName} is visible`);
      soft.equal(typeof cfg.label, "object", `${toolName} label is descriptor`);
      soft.equal(
        typeof cfg.label?.l10nId,
        "string",
        `${toolName} label has an l10nId`
      );
      soft.greater(
        cfg.label?.l10nId?.length ?? 0,
        0,
        `${toolName} label l10nId is not empty`
      );
    }
  });
});

add_task(function test_getActionLogConfigForTool_unknown_tool_suppressed() {
  const cfg = getActionLogConfigForTool("not_a_real_tool");

  Assert.strictEqual(cfg.show, false, "Unknown tool is suppressed");
  Assert.strictEqual(cfg.label, null, "Unknown tool has no label");
  Assert.strictEqual(
    cfg.pendingLabel,
    null,
    "Unknown tool has no pending label"
  );
  Assert.strictEqual(cfg.link, null, "Unknown tool has no link");
});

add_task(function test_getActionLogConfigForTool_search_the_web_link() {
  const cfg = getActionLogConfigForTool(SEARCH_THE_WEB, {});

  Assert.equal(
    cfg.link?.l10nName,
    "exa-link",
    "Exa link uses the expected l10n name"
  );
  Assert.ok(
    cfg.link?.href?.startsWith("http"),
    `Exa link href is resolved from a support pref (got: ${cfg.link?.href})`
  );
  Assert.equal(
    cfg.pendingLabel?.l10nId,
    "action-log-searching-web-with-exa",
    "Pending label uses the Exa-branded string"
  );
  Assert.equal(
    cfg.pendingLabel?.link?.href,
    cfg.link.href,
    "Pending label carries the same resolved link as the row"
  );
});

add_task(
  function test_getActionLogConfigForTool_pending_body_uses_pending_label() {
    const pending = getActionLogConfigForTool(SEARCH_THE_WEB, {
      pending: true,
    });
    Assert.equal(
      pending.label?.l10nId,
      "action-log-searching-web-with-exa",
      "A pending body surfaces the in-progress label on the row"
    );

    const completed = getActionLogConfigForTool(SEARCH_THE_WEB, {
      could_answer: true,
    });
    Assert.equal(
      completed.label?.l10nId,
      "action-log-searched-web-with-exa",
      "A completed body surfaces the completed label on the row"
    );
  }
);

add_task(function test_search_the_web_direct_answer_label() {
  const searchOnly = getActionLogConfigForTool(SEARCH_THE_WEB, {});
  const answered = getActionLogConfigForTool(SEARCH_THE_WEB, {
    directAnswer: true,
    read_urls: ["https://example.com/"],
    requiresSearchHandoff: false,
  });

  Assert.notEqual(
    answered.label?.l10nId,
    searchOnly.label?.l10nId,
    "A reply written by Exa gets its own label"
  );
  Assert.deepEqual(
    answered.link,
    searchOnly.link,
    "The answered label keeps the Exa support link"
  );
  Assert.deepEqual(
    answered.pendingLabel,
    searchOnly.pendingLabel,
    "The in-progress label is unchanged"
  );

  Assert.withSoftAssertions(function (soft) {
    for (const [description, body] of [
      ["fast path results", { results: [], requiresSearchHandoff: false }],
      [
        "answers path failure",
        { read_urls: [], error: "failed", requiresSearchHandoff: false },
      ],
      ["search handoff", { requiresSearchHandoff: true }],
    ]) {
      soft.equal(
        getActionLogConfigForTool(SEARCH_THE_WEB, body).label?.l10nId,
        searchOnly.label?.l10nId,
        `${description} keeps the search-only label`
      );
    }
  });
});

add_task(function test_getActionLogConfigForTool_no_link_by_default() {
  const cfg = getActionLogConfigForTool(GET_OPEN_TABS, []);
  Assert.strictEqual(cfg.link, null, "Tools without a link config return null");
});

add_task(function test_getActionLogConfigForTool_returns_default() {
  const cfg = getActionLogConfigForTool("not_a_real_tool");

  Assert.equal(typeof cfg, "object", "Returns an object");
  Assert.equal(typeof cfg.show, "boolean", "show is a boolean");
  Assert.strictEqual(cfg.label, null, "label is null by default");
});

add_task(function test_getActionLogChipsForTool_get_open_tabs() {
  const body = [
    { url: "https://example.com/", title: "Example", lastAccessed: 1 },
    { url: "https://firefox.com/", title: "Firefox", lastAccessed: 2 },
  ];

  const chips = getActionLogChipsForTool(GET_OPEN_TABS, body);

  Assert.withSoftAssertions(function (soft) {
    soft.equal(chips.length, 2, "One chip per tab");
    soft.deepEqual(chips[0], {
      url: "https://example.com/",
      label: "Example",
    });
    soft.deepEqual(chips[1], {
      url: "https://firefox.com/",
      label: "Firefox",
    });
  });
});

add_task(function test_getActionLogChipsForTool_get_open_tabs_empty() {
  Assert.deepEqual(
    getActionLogChipsForTool(GET_OPEN_TABS, []),
    [],
    "Empty body yields no chips"
  );
  Assert.deepEqual(
    getActionLogChipsForTool(GET_OPEN_TABS, null),
    [],
    "Null body yields no chips"
  );
});

add_task(function test_getActionLogChipsForTool_search_browsing_history() {
  const body = {
    results: [
      { url: "https://news.example.com/", title: "Today's News" },
      { url: "https://docs.example.com/api", title: "API Docs" },
    ],
  };

  const chips = getActionLogChipsForTool(SEARCH_BROWSING_HISTORY, body);

  Assert.withSoftAssertions(function (soft) {
    soft.equal(chips.length, 2, "One chip per history result");
    soft.deepEqual(chips[0], {
      url: "https://news.example.com/",
      label: "Today's News",
    });
  });
});

add_task(function test_getActionLogChipsForTool_unknown_tool() {
  Assert.deepEqual(
    getActionLogChipsForTool("not_a_real_tool", [{ url: "x", title: "y" }]),
    [],
    "Unknown tool returns no chips"
  );
});

add_task(function test_buildActionLogRow_label_no_args() {
  const row = buildActionLogRow(
    GET_OPEN_TABS,
    { l10nId: "action-log-searched-open-tabs" },
    [{ url: "https://example.com/", title: "Example" }]
  );

  Assert.withSoftAssertions(function (soft) {
    soft.equal(
      row.labelL10nId,
      "action-log-searched-open-tabs",
      "labelL10nId is flattened onto the row"
    );
    soft.strictEqual(
      row.labelL10nArgs,
      undefined,
      "labelL10nArgs is undefined when the actionLogLabel has no args"
    );
    soft.equal(row.items.length, 1, "Items contain the tool's chips");
    soft.deepEqual(row.items[0], {
      url: "https://example.com/",
      label: "Example",
    });
  });
});

add_task(function test_buildActionLogRow_memories_l10n_args_no_chips() {
  const row = buildActionLogRow(GET_USER_MEMORIES, {
    l10nId: "action-log-checked-memories",
  });

  Assert.equal(row.labelL10nId, "action-log-checked-memories");
  Assert.equal(row.items.length, 0, "Memory rows carry no chips");
});

add_task(function test_buildActionLogRow_string_label_fallback() {
  const row = buildActionLogRow(GET_OPEN_TABS, "Raw label", []);
  Assert.equal(row.label, "Raw label");
  Assert.strictEqual(row.labelL10nId, undefined);
  Assert.deepEqual(row.items, []);
});

add_task(function test_buildActionLogRow_unknown_tool_no_chip() {
  const row = buildActionLogRow("not_a_real_tool", { l10nId: "x" }, [
    { url: "https://example.com/", title: "Example" },
  ]);
  Assert.deepEqual(row.items, [], "Unknown tool has no chip adapter");
  Assert.equal(row.labelL10nId, "x");
});

add_task(function test_buildActionLogRow_carries_link() {
  const link = { l10nName: "exa-link", href: "https://example.com/exa" };
  const row = buildActionLogRow(
    SEARCH_THE_WEB,
    { l10nId: "action-log-searched-web-with-exa" },
    {},
    undefined,
    link
  );
  Assert.deepEqual(row.link, link, "Link is carried onto the row");
});

add_task(function test_buildActionLogRow_no_link_without_href() {
  const row = buildActionLogRow(
    SEARCH_THE_WEB,
    { l10nId: "action-log-searched-web-with-exa" },
    {},
    undefined,
    { l10nName: "exa-link", href: "" }
  );
  Assert.ok(!("link" in row), "Row omits link when href is empty");
});

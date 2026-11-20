/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_handleInlineSelectionUserEvent_topic_position() {
  info(
    "TelemetryFeed.handleInlineSelectionUserEvent should record the " +
      "topic_position that InterestPicker sends"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();
  sandbox.stub(instance.sessions, "get").returns({ session_id: "decafc0ffee" });

  instance.handleInlineSelectionUserEvent({
    type: "INLINE_SELECTION_CLICK",
    data: {
      topic: "sports",
      section_position: 2,
      topic_position: 3,
      is_followed: true,
    },
  });

  const [event] = Glean.newtab.inlineSelectionClick.testGetValue();
  Assert.equal(event.extra.topic_position, "3", "topic_position is recorded");

  sandbox.restore();
});

add_task(
  async function test_handleTopSitesOrganicImpressionStats_no_recording() {
    info(
      "TelemetryFeed.handleTopSitesOrganicImpressionStats should not " +
        "record events on an unknown session"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    sandbox.stub(instance.sessions, "get").returns(false);

    await instance.handleTopSitesOrganicImpressionStats({});
    Assert.ok(!Glean.topsites.click.testGetValue(), "Click was not recorded");
    Assert.ok(
      !Glean.topsites.impression.testGetValue(),
      "Impression was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesOrganicImpressionStats_no_recording_with_session() {
    info(
      "TelemetryFeed.handleTopSitesOrganicImpressionStats should not record " +
        "events on an unknown impressionStats action"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    await instance.handleTopSitesOrganicImpressionStats({ type: "unknown" });
    Assert.ok(!Glean.topsites.click.testGetValue(), "Click was not recorded");
    Assert.ok(
      !Glean.topsites.impression.testGetValue(),
      "Impression was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_no_recording_with_session() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent correctly handles " +
        "action with no `data`"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let action = actionCreators.DiscoveryStreamUserEvent();
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);
    Assert.ok(
      !Glean.pocket.topicClick.testGetValue(),
      "Pocket topicClick was not recorded"
    );
    Assert.ok(
      !Glean.pocket.click.testGetValue(),
      "Pocket click was not recorded"
    );
    Assert.ok(
      !Glean.pocket.save.testGetValue(),
      "Pocket save was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_click_with_no_value() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent correctly handles " +
        "CLICK data with no value"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: "POPULAR_TOPICS",
    });
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);
    let topicClicks = Glean.pocket.topicClick.testGetValue();
    Assert.equal(topicClicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(topicClicks[0].extra, {
      newtab_visit_id: SESSION_ID,
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_non_popular_click_with_no_value() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent correctly handles " +
        "non-POPULAR_TOPICS CLICK data with no value"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: "not-POPULAR_TOPICS",
    });
    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);
    Assert.ok(
      !Glean.pocket.topicClick.testGetValue(),
      "Pocket topicClick was not recorded"
    );
    Assert.ok(
      !Glean.pocket.click.testGetValue(),
      "Pocket click was not recorded"
    );
    Assert.ok(
      !Glean.pocket.save.testGetValue(),
      "Pocket save was not recorded"
    );

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_non_popular_click() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent correctly handles " +
        "CLICK data with non-POPULAR_TOPICS source"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const TOPIC = "atopic";
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: "not-POPULAR_TOPICS",
      value: {
        card_type: "topics_widget",
        topic: TOPIC,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);
    let topicClicks = Glean.pocket.topicClick.testGetValue();
    Assert.equal(topicClicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(topicClicks[0].extra, {
      newtab_visit_id: SESSION_ID,
      topic: TOPIC,
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_without_card_type() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent doesn't instrument " +
        "a CLICK without a card_type"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      source: "not-POPULAR_TOPICS",
      value: {
        card_type: "not spoc, organic, or topics_widget",
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    Assert.ok(
      !Glean.pocket.topicClick.testGetValue(),
      "Pocket topicClick was not recorded"
    );
    Assert.ok(
      !Glean.pocket.click.testGetValue(),
      "Pocket click was not recorded"
    );
    Assert.ok(
      !Glean.pocket.save.testGetValue(),
      "Pocket save was not recorded"
    );

    sandbox.restore();
  }
);

add_task(async function test_handleDiscoveryStreamUserEvent_popular_click() {
  info(
    "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a popular " +
      "topic click"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();
  const TOPIC = "entertainment";
  let action = actionCreators.DiscoveryStreamUserEvent({
    event: "CLICK",
    source: "POPULAR_TOPICS",
    value: {
      card_type: "topics_widget",
      topic: TOPIC,
    },
  });

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  instance.handleDiscoveryStreamUserEvent(action);
  let topicClicks = Glean.pocket.topicClick.testGetValue();
  Assert.equal(topicClicks.length, 1, "Recorded 1 click");
  Assert.deepEqual(topicClicks[0].extra, {
    newtab_visit_id: SESSION_ID,
    topic: TOPIC,
  });

  sandbox.restore();
});

add_task(async function test_handleDiscoveryStreamUserEvent_tooltip_click() {
  info(
    "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a " +
      "tooltip click"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();
  const feature = "FEATURE_HIGHLIGHT_DEFAULT";
  let action = actionCreators.DiscoveryStreamUserEvent({
    event: "CLICK",
    source: "FEATURE_HIGHLIGHT",
    value: {
      feature,
    },
  });

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  instance.handleDiscoveryStreamUserEvent(action);
  let tooltipClicks = Glean.newtab.tooltipClick.testGetValue();
  Assert.equal(tooltipClicks.length, 1, "Recorded 1 click");
  Assert.deepEqual(tooltipClicks[0].extra, {
    newtab_visit_id: SESSION_ID,
    feature,
  });

  sandbox.restore();
});

add_task(
  async function test_handleDiscoveryStreamUserEvent_organic_top_stories_click() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments an organic " +
        "top stories click"
    );
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, false);

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "organic",
        corpus_item_id: "decaf-beef",
        scheduled_corpus_item_id: "dead-beef",
        tile_id: 314623757745896,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(ACTION_POSITION),
      content_redacted: String(true),
    });

    sandbox.restore();
    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_private_ping_with_redactions_organic_top_stories_click() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments an organic " +
        "top stories click with private ping fully enabled"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "organic",
        corpus_item_id: "decaf-beef",
        scheduled_corpus_item_id: "dead-beef",
        tile_id: 314623757745896,
        variant_id: 5050,
        source_section_id: "sourced-section",
        content_redacted: true,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });
    sandbox.spy(instance.newtabContentPing, "recordEvent");

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();

    Assert.equal(clicks.length, 1, "Recorded 1 content click");
    Assert.equal(clicks.length, 1, "Recorded 1 private click");
    Assert.deepEqual(clicks[0].extra, {
      content_redacted: String(true),
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(ACTION_POSITION),
    });

    Assert.ok(
      instance.newtabContentPing.recordEvent.calledWith(
        "click",
        sinon.match({
          newtab_visit_id: SESSION_ID,
          is_sponsored: false,
          position: ACTION_POSITION,
          corpus_item_id: "decaf-beef",
          scheduled_corpus_item_id: "dead-beef",
          variant_id: 5050,
          source_section_id: "sourced-section",
        })
      ),
      "NewTabContentPing passed the expected arguments."
    );

    sandbox.restore();
    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_sponsored_top_stories_click() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent instruments a sponsored " +
        "top stories click"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    const SHIM = "Y29uc2lkZXIgeW91ciBjdXJpb3NpdHkgcmV3YXJkZWQ=";
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "spoc",
        tile_id: 448685088,
        shim: SHIM,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(true),
      position: String(ACTION_POSITION),
      content_redacted: String(true),
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_record_adsClient_click_if_enabled() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent records an adsClient click " +
        "if enabled"
    );

    let sandbox = sinon.createSandbox();
    Services.prefs.setStringPref(PREF_ENDPOINTS, "https://test.reporting.net/");
    Services.prefs.setBoolPref(PREF_UNIFIED_ADS_SPOCS_ENABLED, true);

    sandbox.stub(AdsClient, "isEnabled").returns(true);
    const ADS_CLIENT = { recordClick: sinon.fake.resolves() };
    sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);
    sandbox.stub(AdsClient, "callbackOptions").returns({ ohttp: true });

    let instance = new TelemetryFeed();
    sandbox.replaceGetter(
      instance,
      "SHOW_SPONSORED_STORIES_ENABLED",
      sinon.fake.returns(true)
    );

    await instance.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
    });
    Assert.ok(AdsClient.isEnabled.calledOnce);
    Assert.ok(AdsClient.getClient.calledOnce);

    sandbox
      .stub(instance.sessions, "get")
      .returns({ session_id: "decafc0ffee" });

    instance.handleDiscoveryStreamUserEvent(
      actionCreators.DiscoveryStreamUserEvent({
        event: "CLICK",
        action_position: 42,
        value: {
          card_type: "spoc",
          shim: "https://test.reporting.net/",
        },
      })
    );

    Assert.ok(AdsClient.callbackOptions.calledOnce);
    Assert.ok(ADS_CLIENT.recordClick.calledOnce);

    Services.prefs.clearUserPref(PREF_UNIFIED_ADS_SPOCS_ENABLED);
    Services.prefs.clearUserPref(PREF_ENDPOINTS);
    sandbox.restore();
  }
);

add_task(
  async function test_handleReportAdUserEvent_reports_adsClient_ad_if_enabled() {
    info(
      "TelemetryFeed.handleDiscoveryStreamImpressionStats should report add to adsClient " +
        "when enabled"
    );

    let sandbox = sinon.createSandbox();
    Services.prefs.setStringPref(PREF_ENDPOINTS, "https://test.reporting.net/");

    sandbox.stub(AdsClient, "isEnabled").returns(true);
    const ADS_CLIENT = { reportAd: sinon.fake.resolves() };
    sandbox.stub(AdsClient, "getClient").returns(ADS_CLIENT);
    sandbox.stub(AdsClient, "callbackOptions").returns({ ohttp: true });

    let instance = new TelemetryFeed();

    await instance.onAction({
      type: actionTypes.PREFS_INITIAL_VALUES,
    });
    Assert.ok(AdsClient.isEnabled.calledOnce);
    Assert.ok(AdsClient.getClient.calledOnce);

    await instance.handleReportAdUserEvent({
      type: actionTypes.REPORT_AD_SUBMIT,
      data: {
        placement_id: "place",
        position: 42,
        report_reason: "not_interested",
        reporting_url: "https://test.reporting.net/",
      },
    });

    Assert.ok(AdsClient.callbackOptions.calledOnce);
    Assert.ok(ADS_CLIENT.reportAd.calledOnce);

    Services.prefs.clearUserPref(PREF_ENDPOINTS);
    sandbox.restore();
  }
);

add_task(
  async function test_handleDiscoveryStreamUserEvent_sponsored_top_stories_click_tile_id_redacted() {
    info(
      "TelemetryFeed.handleDiscoveryStreamUserEvent never sends a tile_id to " +
        "either the newtab ping or the newtab-content ping for a sponsored " +
        "top stories click"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    const ACTION_POSITION = 42;
    const TILE_ID = 448685088;
    let action = actionCreators.DiscoveryStreamUserEvent({
      event: "CLICK",
      action_position: ACTION_POSITION,
      value: {
        card_type: "spoc",
        tile_id: TILE_ID,
      },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });
    sandbox.spy(instance.newtabContentPing, "recordEvent");

    instance.handleDiscoveryStreamUserEvent(action);

    let clicks = Glean.pocket.click.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(
      clicks[0].extra,
      {
        newtab_visit_id: SESSION_ID,
        is_sponsored: String(true),
        position: String(ACTION_POSITION),
        content_redacted: String(true),
      },
      "A tile_id should not have been included with the newtab ping."
    );

    Assert.ok(
      instance.newtabContentPing.recordEvent.calledOnce,
      "recordEvent was called"
    );
    const newtabContentClickArgs =
      instance.newtabContentPing.recordEvent.getCall(0).args;
    Assert.equal(
      newtabContentClickArgs[0],
      "click",
      "Called with the click event type"
    );
    Assert.equal(
      newtabContentClickArgs[1].tile_id,
      undefined,
      "tile_id was not included."
    );

    sandbox.restore();
    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

/**
 * Every sponsored top sites code path that could put a tile_id on the newtab
 * ping. These topsites metrics no longer declare a tile_id extra key, so none
 * of these paths should ever record one.
 */
const SPONSORED_TOPSITES_TILE_ID_PATHS = [
  {
    name: "topsites.impression",
    metric: () => Glean.topsites.impression,
    drive: instance =>
      instance.handleTopSitesSponsoredImpressionStats({
        data: {
          type: "impression",
          tile_id: 4567,
          source: "newtab",
          position: 1,
          advertiser_name: "adnoid ads",
        },
      }),
  },
  {
    name: "topsites.click",
    metric: () => Glean.topsites.click,
    drive: instance =>
      instance.handleTopSitesSponsoredImpressionStats({
        data: {
          type: "click",
          tile_id: 4567,
          source: "newtab",
          position: 0,
          advertiser_name: "adnoid ads",
        },
      }),
  },
  {
    name: "topsites.dismiss",
    metric: () => Glean.topsites.dismiss,
    drive: instance =>
      instance.handleBlockUrl({
        source: "TOP_SITES",
        data: [
          {
            is_pocket_card: false,
            position: 42,
            advertiser_name: "mozilla",
            tile_id: 4567,
            isSponsoredTopSite: 1,
          },
        ],
      }),
  },
  {
    name: "topsites.showPrivacyClick",
    metric: () => Glean.topsites.showPrivacyClick,
    drive: instance =>
      instance.handleAboutSponsoredTopSites({
        data: {
          position: 42,
          advertiser_name: "mozilla",
          tile_id: 4567,
        },
      }),
  },
];

add_task(async function test_sponsored_topsites_tile_id_never_recorded() {
  info(
    "No sponsored top sites event on the newtab ping should ever carry a " +
      "tile_id, since the topsites metrics no longer declare it"
  );

  for (const { name, metric, drive } of SPONSORED_TOPSITES_TILE_ID_PATHS) {
    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    sandbox
      .stub(instance.sessions, "get")
      .returns({ session_id: "decafc0ffee" });

    await drive(instance);

    let events = metric().testGetValue();
    Assert.equal(events.length, 1, `Recorded 1 ${name} event`);
    Assert.ok(
      !("tile_id" in events[0].extra),
      `${name} should not carry a tile_id`
    );

    sandbox.restore();
  }
});

add_task(
  async function test_handleAboutSponsoredTopSites_record_showPrivacyClick() {
    info(
      "TelemetryFeed.handleAboutSponsoredTopSites should record a Glean " +
        "topsites.showPrivacyClick event on action"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = {
      position: 42,
      advertiser_name: "mozilla",
      tile_id: 4567,
    };

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    instance.handleAboutSponsoredTopSites({ data });

    let clicks = Glean.topsites.showPrivacyClick.testGetValue();
    Assert.equal(clicks.length, 1, "Recorded 1 click");
    Assert.deepEqual(clicks[0].extra, {
      advertiser_name: data.advertiser_name,
      newtab_visit_id: SESSION_ID,
      position: String(data.position),
    });

    sandbox.restore();
  }
);

add_task(
  async function test_handleAboutSponsoredTopSites_no_record_showPrivacyClick() {
    info(
      "TelemetryFeed.handleAboutSponsoredTopSites should not record a Glean " +
        "topsites.showPrivacyClick event if there's no session"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    let data = {
      position: 42,
      advertiser_name: "mozilla",
      tile_id: 4567,
    };

    sandbox.stub(instance.sessions, "get").returns(null);

    instance.handleAboutSponsoredTopSites({ data });

    let clicks = Glean.topsites.showPrivacyClick.testGetValue();
    Assert.ok(!clicks, "Did not record any clicks");

    sandbox.restore();
  }
);

add_task(async function test_handleBlockUrl_no_record_dismisses() {
  info(
    "TelemetryFeed.handleBlockUrl shouldn't record events for pocket " +
      "cards' dismisses"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  let data = [
    {
      // Shouldn't record anything for this one
      is_pocket_card: true,
      position: 43,
      tile_id: undefined,
    },
  ];

  await instance.handleBlockUrl({ data });

  Assert.ok(
    !Glean.topsites.dismiss.testGetValue(),
    "Should not record a dismiss for Pocket cards"
  );

  sandbox.restore();
});

add_task(async function test_handleBlockUrl_record_dismiss_on_action() {
  info(
    "TelemetryFeed.handleBlockUrl should record a topsites.dismiss event " +
      "on action"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  const SESSION_ID = "decafc0ffee";
  sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

  let data = [
    {
      is_pocket_card: false,
      position: 42,
      advertiser_name: "mozilla",
      tile_id: 4567,
      isSponsoredTopSite: 1, // for some reason this is an int.
    },
  ];

  await instance.handleBlockUrl({ data, source: "TOP_SITES" });

  let dismisses = Glean.topsites.dismiss.testGetValue();
  Assert.equal(dismisses.length, 1, "Should have recorded 1 dismiss");
  Assert.deepEqual(dismisses[0].extra, {
    advertiser_name: data[0].advertiser_name,
    newtab_visit_id: SESSION_ID,
    is_sponsored: String(!!data[0].isSponsoredTopSite),
    position: String(data[0].position),
  });

  sandbox.restore();
});

add_task(
  async function test_handleBlockUrl_record_dismiss_on_nonsponsored_action() {
    info(
      "TelemetryFeed.handleBlockUrl should record a Glean topsites.dismiss " +
        "event on action on non-sponsored topsite"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    let data = [
      {
        is_pocket_card: false,
        position: 42,
        tile_id: undefined,
      },
    ];

    await instance.handleBlockUrl({ data, source: "TOP_SITES" });

    let dismisses = Glean.topsites.dismiss.testGetValue();
    Assert.equal(dismisses.length, 1, "Should have recorded 1 dismiss");
    Assert.deepEqual(dismisses[0].extra, {
      newtab_visit_id: SESSION_ID,
      is_sponsored: String(false),
      position: String(data[0].position),
    });

    sandbox.restore();
  }
);

add_task(async function test_handleBlockUrl_no_record_dismiss_on_no_session() {
  info(
    "TelemetryFeed.handleBlockUrl should not record a Glean " +
      "topsites.dismiss event if there's no session"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  sandbox.stub(instance.sessions, "get").returns(null);

  let data = {};

  await instance.handleBlockUrl({ data });

  Assert.ok(
    !Glean.topsites.dismiss.testGetValue(),
    "Should not have recorded a dismiss"
  );

  sandbox.restore();
});

add_task(function test_randomizeOrganicContentEvent() {
  info(
    "TelemetryFeed._randomizeOrganicContentEvent should return true or false" +
      " based on the given probability"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  const computeRec = id => ({
    corpus_item_id: `item-${id}`,
    topic: "a",
    is_sponsored: false,
    variant_id: 0,
    source_section_id: "src-section",
  });
  const allRecs = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map(computeRec);
  sandbox.stub(instance, "getRecommendationCount").returns(allRecs.length);
  sandbox.stub(instance, "getAllRecommendations").returns(allRecs);
  instance._privateRandomContentTelemetryProbablityValues = { epsilon: 30 };
  let decideStub = sandbox.stub(NewTabContentPing, "decideWithProbability");
  decideStub.returns(true);
  let result = instance.randomizeOrganicContentEvent(allRecs[0]);
  Assert.equal(result, allRecs[0]);
  Assert.ok(decideStub.calledOnce, "decideWithProbability was called once");
  const [probUsed] = decideStub.firstCall.args;
  Assert.greater(probUsed, 0.9); // Epsilon 30 is very high probability
  Assert.less(probUsed, 1.01);

  // Run again - randomization kicks in
  decideStub.returns(false);
  sandbox.stub(NewTabContentPing, "secureRandIntInRange").returns(3);
  result = instance.randomizeOrganicContentEvent(allRecs[0]);
  Assert.equal(probUsed, decideStub.lastCall.args[0]);
  Assert.deepEqual(result, allRecs[3]);

  sandbox.restore();
});

add_task(function test_randomizeOrganicContentEvent_tracks_layout_name() {
  info(
    "randomizeOrganicContentEvent should set layout_name to the swapped " +
      "section's layout"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  const item = {
    corpus_item_id: "orig",
    topic: "a",
    is_sponsored: false,
    section: "orig-section",
    section_position: 0,
    layout_name: "orig-layout",
    variant_id: 0,
    source_section_id: "orig-source",
  };
  const randomItem = {
    corpus_item_id: "swapped",
    topic: "b",
    is_sponsored: false,
    section: "swapped-section",
    variant_id: 5050,
    source_section_id: "swapped-source",
  };
  sandbox.stub(instance, "getRecommendationCount").returns(10);
  sandbox.stub(instance, "getAllRecommendations").returns([randomItem]);
  sandbox
    .stub(instance, "getAllSections")
    .returns([
      { sectionKey: "swapped-section", layout: { name: "swapped-layout" } },
    ]);
  instance._privateRandomContentTelemetryProbablityValues = { epsilon: 30 };
  sandbox.stub(NewTabContentPing, "decideWithProbability").returns(false);
  sandbox.stub(NewTabContentPing, "secureRandIntInRange").returns(0);

  const result = instance.randomizeOrganicContentEvent(item);

  Assert.equal(result.section, "swapped-section", "section is swapped");
  Assert.equal(
    result.layout_name,
    "swapped-layout",
    "layout_name tracks the swapped section"
  );
  Assert.equal(
    result.variant_id,
    5050,
    "variant_id tracks the swapped section"
  );
  Assert.equal(
    result.source_section_id,
    "swapped-source",
    "source_section_id tracks the swapped section"
  );

  sandbox.restore();
});

add_task(async function test_randomizeOrganicContentEvent_section_position() {
  info(
    "randomizeOrganicContentEvent should only swap in items from rendered " +
      "sections and report the rendered position of the swapped section"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  const PORT_ID = "port123";
  const session = instance.addSession(PORT_ID);

  await instance.onAction({
    type: actionTypes.CARD_SECTIONS_ORDER,
    meta: { fromTarget: PORT_ID },
    data: { sections: ["orig-section", "swapped-section"] },
  });

  const item = {
    corpus_item_id: "orig",
    section: "orig-section",
    section_position: 0,
  };
  sandbox.stub(instance, "getRecommendationCount").returns(10);
  sandbox
    .stub(instance, "getAllRecommendations")
    .returns([
      { corpus_item_id: "hidden", section: "hidden" },
      { corpus_item_id: "unsectioned" },
      { corpus_item_id: "swapped", section: "swapped-section" },
    ]);
  instance._privateRandomContentTelemetryProbablityValues = { epsilon: 30 };
  sandbox.stub(NewTabContentPing, "decideWithProbability").returns(false);
  sandbox.stub(NewTabContentPing, "secureRandIntInRange").returns(0);

  const result = instance.randomizeOrganicContentEvent(item, session);

  Assert.equal(
    result.corpus_item_id,
    "swapped",
    "Only items from rendered sections are candidates"
  );
  Assert.equal(
    result.section_position,
    1,
    "section_position is the swapped section's rendered position"
  );

  sandbox.restore();
});

add_task(function test_randomizeOrganicContentEvent_variant_id_popular_today() {
  info(
    "randomizeOrganicContentEvent should keep the original variant_id when the " +
      "section stays Popular Today, since the swapped item's variant would be " +
      "an impossible section/variant pair"
  );
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();

  const item = {
    corpus_item_id: "orig",
    topic: "a",
    is_sponsored: false,
    section: "top_stories_section",
    variant_id: 0,
    source_section_id: "orig-origin",
  };
  const randomItem = {
    corpus_item_id: "swapped",
    topic: "b",
    is_sponsored: false,
    section: "sports",
    variant_id: 5050,
  };
  sandbox.stub(instance, "getRecommendationCount").returns(10);
  sandbox.stub(instance, "getAllRecommendations").returns([randomItem]);
  instance._privateRandomContentTelemetryProbablityValues = { epsilon: 30 };
  sandbox.stub(NewTabContentPing, "decideWithProbability").returns(false);
  sandbox.stub(NewTabContentPing, "secureRandIntInRange").returns(0);

  const result = instance.randomizeOrganicContentEvent(item);

  Assert.equal(
    result.section,
    "top_stories_section",
    "section stays Popular Today"
  );
  Assert.equal(
    result.variant_id,
    0,
    "variant_id stays the Popular Today variant, not the swapped item's"
  );
  Assert.equal(result.corpus_item_id, "swapped", "content is still swapped");

  sandbox.restore();
});

add_task(async function test_handleSpocPlaceholderDuration_records_metric() {
  info(
    "TelemetryFeed.handleSpocPlaceholderDuration should record the " +
      "spoc_placeholder_duration metric"
  );

  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  const DURATION_MS = 150;
  let action = {
    type: actionTypes.DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION,
    data: { duration: DURATION_MS },
  };

  instance.handleSpocPlaceholderDuration(action);

  let recordedDuration = Glean.pocket.spocPlaceholderDuration.testGetValue();
  Assert.ok(recordedDuration, "Metric should be recorded");
  Assert.equal(recordedDuration.count, 1, "Should have 1 sample");
  Assert.greaterOrEqual(
    recordedDuration.sum,
    DURATION_MS,
    "Duration should be at least the input value"
  );
});

add_task(async function test_handleSpocPlaceholderDuration_ignores_negative() {
  info(
    "TelemetryFeed.handleSpocPlaceholderDuration should ignore negative durations"
  );

  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  let action = {
    type: actionTypes.DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION,
    data: { duration: -1 },
  };

  instance.handleSpocPlaceholderDuration(action);

  let recordedDuration = Glean.pocket.spocPlaceholderDuration.testGetValue();
  Assert.equal(
    recordedDuration,
    null,
    "Metric should not be recorded for negative duration"
  );
});

add_task(async function test_handleSpocPlaceholderDuration_ignores_undefined() {
  info(
    "TelemetryFeed.handleSpocPlaceholderDuration should ignore undefined durations"
  );

  let instance = new TelemetryFeed();
  Services.fog.testResetFOG();

  let action = {
    type: actionTypes.DISCOVERY_STREAM_SPOC_PLACEHOLDER_DURATION,
    data: {},
  };

  instance.handleSpocPlaceholderDuration(action);

  let recordedDuration = Glean.pocket.spocPlaceholderDuration.testGetValue();
  Assert.equal(
    recordedDuration,
    null,
    "Metric should not be recorded for undefined duration"
  );
});
/**
 * Helper to create a mock store with trainhopConfig
 */
function createMockStore(
  trainhopConfig = {},
  civState = { initialized: false }
) {
  return {
    getState: () => ({
      Prefs: {
        values: {
          trainhopConfig,
        },
      },
      InferredPersonalization: civState,
    }),
  };
}

add_task(async function test_initializePrivacySession_defaults_to_private() {
  info(
    "initializeGleanSession should default to PrivateGleanSession " +
      "when privatePingEnabled is false"
  );

  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, false);

  let instance = new TelemetryFeed();
  instance.store = createMockStore({
    Prefs: {
      values: {
        trainhopConfig: { newtabPrivatePing: { clickOnly: true } },
      },
    },
  });

  instance.initializeGleanSession();

  Assert.equal(
    instance.gleanSessionType,
    "private",
    "Should be PrivateGleanSession when privatePingEnabled is false"
  );

  Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
});

add_task(
  async function test_initializePrivacySession_defaults_to_private_no_clickOnly() {
    info(
      "initializeGleanSession should default to PrivateGleanSession " +
        "when clickOnly is false"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let instance = new TelemetryFeed();
    instance.store = createMockStore({
      Prefs: {
        values: {
          trainhopConfig: { newtabPrivatePing: { clickOnly: false } },
        },
      },
    });

    instance.initializeGleanSession();

    Assert.equal(
      instance.gleanSessionType,
      "private",
      "Should be PrivateGleanSession when clickOnly is false"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(
  async function test_initializePrivacySession_defaults_to_private_sov_enabled() {
    info(
      "initializeGleanSession should default to PrivateGleanSession when SOV is enabled"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: {
          values: {
            trainhopConfig: {
              sov: { enabled: true },
              newtabPrivatePing: { clickOnly: true },
            },
          },
        },
      }),
    };

    instance.initializeGleanSession();

    Assert.equal(
      instance.gleanSessionType,
      "private",
      "Should be PrivateGleanSession when SOV is enabled"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(
  async function test_initializePrivacySession_defaults_to_private_sponsored_scoring() {
    info(
      "initializeGleanSession should default to PrivateGleanSession when sponsored top site scoring is enabled"
    );

    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: {
          values: {
            adsBackendConfig: { sponsored_top_site_scoring: true },
            trainhopConfig: { newtabPrivatePing: { clickOnly: true } },
          },
        },
      }),
    };

    instance.initializeGleanSession();

    Assert.equal(
      instance.gleanSessionType,
      "private",
      "Should be PrivateGleanSession when sponsored top site scoring is enabled"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  }
);

add_task(async function test_hasRecordedClicksInCIV_with_click_count() {
  info("hasRecordedClicksInCIV should return true when click count > 0");

  let instance = new TelemetryFeed();
  instance.store = createMockStore(
    {},
    {
      initialized: true,
      inferredInterests: { clicks: 5 },
    }
  );

  Assert.equal(
    instance.hasRecordedClicksInCIV(),
    true,
    "Should return true when clicks > 0"
  );
});

add_task(async function test_recordOrQueueEvent_queues_in_normal_session() {
  info("recordOrQueueEvent should queue events in NormalContentSession mode");

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.gleanSessionType = "normal";
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

  let callbackCalled = false;
  let callback = () => {
    callbackCalled = true;
  };

  let recordEventStub = sandbox.stub(instance.newtabContentPing, "recordEvent");

  instance.recordOrQueueEvent(
    "testEvent",
    { test: "data" },
    "session1",
    callback
  );

  Assert.ok(
    !recordEventStub.called,
    "newtabContentPing.recordEvent should NOT be called"
  );
  Assert.ok(!callbackCalled, "callback should NOT be called");

  Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  sandbox.restore();
});

add_task(async function test_recordOrQueueEvent_immediate_in_private_session() {
  info(
    "recordOrQueueEvent should send events immediately in PrivateContentSession mode"
  );

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.gleanSessionType = "private";
  Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

  let callbackCalled = false;
  let callback = () => {
    callbackCalled = true;
  };

  let recordEventStub = sandbox.stub(instance.newtabContentPing, "recordEvent");

  const eventData = { test: "data" };
  instance.recordOrQueueEvent("testEvent", eventData, "session1", callback);

  Assert.ok(callbackCalled, "callback should be called immediately");
  Assert.ok(
    recordEventStub.calledOnce,
    "newtabContentPing.recordEvent should be called once"
  );
  Assert.ok(
    recordEventStub.calledWith("testEvent", eventData),
    "recordEvent should be called with correct arguments"
  );

  Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
  sandbox.restore();
});

add_task(
  async function test_recordOrQueueEvent_no_private_ping_when_disabled() {
    info(
      "recordOrQueueEvent should not send to newtab-content ping when privatePingEnabled is false"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.gleanSessionType = "private";
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, false);

    let callbackCalled = false;
    let callback = () => {
      callbackCalled = true;
    };

    let recordEventStub = sandbox.stub(
      instance.newtabContentPing,
      "recordEvent"
    );

    instance.recordOrQueueEvent(
      "testEvent",
      { test: "data" },
      "session1",
      callback
    );

    Assert.ok(callbackCalled, "callback should still be called");
    Assert.ok(
      !recordEventStub.called,
      "newtabContentPing.recordEvent should NOT be called when pref is false"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
    sandbox.restore();
  }
);

add_task(async function test_endSession_leaves_other_sessions_events_queued() {
  info(
    "endSession should drain only its own session's queued events, leaving " +
      "events belonging to other open sessions in the buffer"
  );
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.gleanSessionType = "normal";

  let endingSession = instance.addSession("ending-port");
  let otherSession = instance.addSession("other-port");
  endingSession.perf.visibility_event_rcvd_ts = 1000;
  otherSession.perf.visibility_event_rcvd_ts = 1000;

  let endingRecorded = false;
  let otherRecorded = false;
  instance.recordOrQueueEvent(
    "impression",
    {},
    endingSession.session_id,
    () => {
      endingRecorded = true;
    }
  );
  instance.recordOrQueueEvent("impression", {}, otherSession.session_id, () => {
    otherRecorded = true;
  });

  await instance.endSession("ending-port");

  Assert.ok(endingRecorded, "The ending session's event should be recorded.");
  Assert.ok(
    !otherRecorded,
    "The still-open session's event should stay queued."
  );

  // The survivor is still drainable by its own session.
  await instance.endSession("other-port");
  Assert.ok(
    otherRecorded,
    "The other session's event should be recorded when that session ends."
  );

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
  Services.fog.testResetFOG();
});

add_task(async function test_endSession_never_shown_discards_own_events() {
  info(
    "endSession should discard a never-shown session's queued events without " +
      "recording them, and leave other sessions' events alone"
  );
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);

  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.gleanSessionType = "normal";

  let neverShown = instance.addSession("preloaded-port");
  let visible = instance.addSession("visible-port");
  visible.perf.visibility_event_rcvd_ts = 1000;

  let neverShownRecorded = false;
  let visibleRecorded = false;
  instance.recordOrQueueEvent("impression", {}, neverShown.session_id, () => {
    neverShownRecorded = true;
  });
  instance.recordOrQueueEvent("impression", {}, visible.session_id, () => {
    visibleRecorded = true;
  });

  await instance.endSession("preloaded-port");

  Assert.ok(
    !neverShownRecorded,
    "A never-shown session's own events must not be recorded."
  );
  Assert.ok(
    !visibleRecorded,
    "A never-shown session must not drain the visible session's events."
  );

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
  Services.fog.testResetFOG();
});

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_frecency_boosted_queued() {
    info(
      "Frecency-boosted topsite events should be queued, not sent immediately"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    instance.gleanSessionType = "normal";
    instance.store = createMockStore({
      sov: { enabled: true },
      newtabPrivatePing: { clickOnly: true },
    });

    sandbox.stub(instance.sessions, "get").returns({ session_id: FAKE_UUID });

    let recordEventStub = sandbox.stub(
      instance.newtabContentPing,
      "recordEvent"
    );

    // Send an impression (should be queued)
    let impressionData = {
      type: "impression",
      tile_id: 42,
      source: "newtab",
      position: 1,
      advertiser_name: "test advertiser",
      visible_topsites: 8,
      frecency_boosted: true,
    };
    await instance.handleTopSitesSponsoredImpressionStats({
      data: impressionData,
    });

    Assert.equal(
      recordEventStub.callCount,
      0,
      "Impression should be queued, not sent to newtab-content"
    );

    // Send a click (should also be queued, not trigger transition)
    let clickData = {
      type: "click",
      tile_id: 42,
      source: "newtab",
      position: 1,
      advertiser_name: "test advertiser",
      visible_topsites: 8,
      frecency_boosted: true,
    };
    await instance.handleTopSitesSponsoredImpressionStats({ data: clickData });

    Assert.equal(
      instance.gleanSessionType,
      "normal",
      "Topsite clicks should NOT transition to private session"
    );
    Assert.equal(
      recordEventStub.callCount,
      0,
      "Click should also be queued, not sent immediately"
    );

    // Manually transition and verify events are flushed
    instance.transitionToPrivateSession();

    Assert.equal(
      recordEventStub.callCount,
      2,
      "Both impression and click should be sent after transition"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_non_frecency_boosted_content_ping() {
    info(
      "Non-frecency-boosted sponsored topsites in SOV mode should go to newtab-content ping"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    instance.gleanSessionType = "private";
    instance.store = createMockStore({
      sov: { enabled: true },
      newtabPrivatePing: { clickOnly: true },
    });

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    let recordEventStub = sandbox.stub(
      instance.newtabContentPing,
      "recordEvent"
    );

    let impressionData = {
      type: "impression",
      tile_id: 42,
      source: "newtab",
      position: 1,
      advertiser_name: "test advertiser",
      visible_topsites: 8,
      frecency_boosted: false,
    };
    await instance.handleTopSitesSponsoredImpressionStats({
      data: impressionData,
    });

    Assert.equal(
      recordEventStub.callCount,
      1,
      "Non-frecency-boosted impression should go to newtab-content ping in private session"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
    sandbox.restore();
  }
);

add_task(
  async function test_handleTopSitesSponsoredImpressionStats_sponsored_scoring_content_ping() {
    info(
      "Sponsored topsites events with score-based selection enabled should go to the newtab-content ping"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    Services.fog.testResetFOG();
    Services.prefs.setBoolPref(PREF_PRIVATE_PING_ENABLED, true);

    instance.gleanSessionType = "private";
    instance.store = {
      getState: () => ({
        Prefs: {
          values: {
            adsBackendConfig: { sponsored_top_site_scoring: true },
            trainhopConfig: { newtabPrivatePing: { clickOnly: true } },
          },
        },
      }),
    };

    const SESSION_ID = "decafc0ffee";
    sandbox.stub(instance.sessions, "get").returns({ session_id: SESSION_ID });

    let recordEventStub = sandbox.stub(
      instance.newtabContentPing,
      "recordEvent"
    );

    for (const type of ["impression", "click"]) {
      await instance.handleTopSitesSponsoredImpressionStats({
        data: {
          type,
          tile_id: 42,
          source: "newtab",
          position: 1,
          advertiser_name: "test advertiser",
          visible_topsites: 8,
        },
      });
    }

    Assert.deepEqual(
      recordEventStub.getCalls().map(call => call.args[0]),
      ["topSitesImpression", "topSitesClick"],
      "The impression and click should go to the newtab-content ping"
    );
    Assert.equal(
      Glean.topsites.impression.testGetValue(),
      null,
      "No topsites.impression event should be recorded in the newtab ping"
    );
    Assert.equal(
      Glean.topsites.click.testGetValue(),
      null,
      "No topsites.click event should be recorded in the newtab ping"
    );

    Services.prefs.clearUserPref(PREF_PRIVATE_PING_ENABLED);
    sandbox.restore();
  }
);

add_task(
  async function test_configureContentPing_includes_experiment_when_merino_feed_experiment() {
    info(
      "configureContentPing should include experiment name and branch " +
        "when merino-feed-experiment pref is truthy"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: { values: { trainhopConfig: {} } },
        DiscoveryStream: { sectionPersonalization: {} },
        InferredPersonalization: { initialized: false },
      }),
    };

    Services.prefs.setBoolPref(PREF_IS_MERINO_FEED_EXPERIMENT, true);

    sandbox
      .stub(NimbusFeatures.pocketNewtab, "getEnrollmentMetadata")
      .returns({ slug: "test-experiment", branch: "treatment" });
    let scheduleStub = sandbox.stub(
      instance.newtabContentPing,
      "scheduleSubmission"
    );
    sandbox.stub(instance.newtabContentPing, "setMaxEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerWeek");

    await instance.configureContentPing();

    Assert.ok(scheduleStub.calledOnce, "scheduleSubmission called once");
    let [metrics] = scheduleStub.firstCall.args;
    Assert.equal(
      metrics.experimentName,
      "test-experiment",
      "experimentName should be the Nimbus slug"
    );
    Assert.equal(
      metrics.experimentBranch,
      "treatment",
      "experimentBranch should be the Nimbus branch"
    );

    Services.prefs.clearUserPref(PREF_IS_MERINO_FEED_EXPERIMENT);
    sandbox.restore();
  }
);

add_task(async function test_configureContentPing_surfaceId_gates_utcOffset() {
  info(
    "configureContentPing should only send surfaceId and utcOffset when a " +
      "surfaceId is known"
  );

  const SURFACE_ID_PREF =
    "browser.newtabpage.activity-stream.telemetry.surfaceId";
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          trainhopConfig: {},
          inferredPersonalizationConfig: { normalized_time_zone_offset: true },
        },
      },
      DiscoveryStream: { sectionPersonalization: {} },
      InferredPersonalization: { initialized: false },
    }),
  };
  let scheduleStub = sandbox.stub(
    instance.newtabContentPing,
    "scheduleSubmission"
  );
  sandbox.stub(instance.newtabContentPing, "setMaxEventsPerDay");
  sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerDay");
  sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerWeek");

  Services.prefs.clearUserPref(SURFACE_ID_PREF);
  await instance.configureContentPing();
  let [metrics] = scheduleStub.lastCall.args;
  Assert.ok(!("surfaceId" in metrics), "No surfaceId without the pref");
  Assert.ok(!("utcOffset" in metrics), "No utcOffset without a surfaceId");

  Services.prefs.setStringPref(SURFACE_ID_PREF, "NEW_TAB_EN_US");
  await instance.configureContentPing();
  [metrics] = scheduleStub.lastCall.args;
  Assert.equal(metrics.surfaceId, "NEW_TAB_EN_US", "surfaceId is sent");
  Assert.equal(typeof metrics.utcOffset, "number", "utcOffset is sent");

  Services.prefs.clearUserPref(SURFACE_ID_PREF);
  sandbox.restore();
});

add_task(
  async function test_configureContentPing_omits_experiment_when_not_merino_feed_experiment() {
    info(
      "configureContentPing should not include experiment name and branch " +
        "when merino-feed-experiment pref is falsy"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: { values: { trainhopConfig: {} } },
        DiscoveryStream: { sectionPersonalization: {} },
        InferredPersonalization: { initialized: false },
      }),
    };

    Services.prefs.setBoolPref(PREF_IS_MERINO_FEED_EXPERIMENT, false);

    sandbox
      .stub(NimbusFeatures.pocketNewtab, "getEnrollmentMetadata")
      .returns({ slug: "test-experiment", branch: "treatment" });
    let scheduleStub = sandbox.stub(
      instance.newtabContentPing,
      "scheduleSubmission"
    );
    sandbox.stub(instance.newtabContentPing, "setMaxEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerWeek");

    await instance.configureContentPing();

    Assert.ok(scheduleStub.calledOnce, "scheduleSubmission called once");
    let [metrics] = scheduleStub.firstCall.args;
    Assert.equal(
      metrics.experimentName,
      "",
      "experimentName should not be the Nimbus slug when pref is falsy"
    );
    Assert.equal(
      metrics.experimentBranch,
      "",
      "experimentBranch should not be the Nimbus branch when pref is falsy"
    );

    Services.prefs.clearUserPref(PREF_IS_MERINO_FEED_EXPERIMENT);
    sandbox.restore();
  }
);

add_task(
  async function test_configureContentPing_empty_experiment_when_merino_but_no_metadata() {
    info(
      "configureContentPing should set empty experiment name and branch " +
        "when merino-feed-experiment is truthy but no enrollment metadata"
    );

    let sandbox = sinon.createSandbox();
    let instance = new TelemetryFeed();
    instance.store = {
      getState: () => ({
        Prefs: { values: { trainhopConfig: {} } },
        DiscoveryStream: { sectionPersonalization: {} },
        InferredPersonalization: { initialized: false },
      }),
    };

    Services.prefs.setBoolPref(PREF_IS_MERINO_FEED_EXPERIMENT, true);

    sandbox
      .stub(NimbusFeatures.pocketNewtab, "getEnrollmentMetadata")
      .returns(null);
    let scheduleStub = sandbox.stub(
      instance.newtabContentPing,
      "scheduleSubmission"
    );
    sandbox.stub(instance.newtabContentPing, "setMaxEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerDay");
    sandbox.stub(instance.newtabContentPing, "setMaxClickEventsPerWeek");

    await instance.configureContentPing();

    Assert.ok(scheduleStub.calledOnce, "scheduleSubmission called once");
    let [metrics] = scheduleStub.firstCall.args;
    Assert.equal(
      metrics.experimentName,
      "",
      "experimentName should be empty when no enrollment metadata"
    );
    Assert.equal(
      metrics.experimentBranch,
      "",
      "experimentBranch should be empty when no enrollment metadata"
    );

    Services.prefs.clearUserPref(PREF_IS_MERINO_FEED_EXPERIMENT);
    sandbox.restore();
  }
);

add_task(async function test_recordEnabledWidgets_partial() {
  info(
    "recordEnabledWidgets should set widgetsEnabledList with only enabled widget names"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.enabled": true,
          "widgets.lists.enabled": true,
          "widgets.system.lists.enabled": true,
          "widgets.focusTimer.enabled": false,
          "widgets.system.focusTimer.enabled": true,
          "widgets.weather.enabled": true,
          "widgets.system.weather.enabled": true,
        },
      },
    }),
  };

  instance.recordEnabledWidgets();

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    ["lists", "weather"],
    "widgetsEnabledList should contain only enabled widget names"
  );
});

add_task(async function test_recordEnabledWidgets_excludes_no_history() {
  info(
    "recordEnabledWidgets should drop the privacy widget when the profile records no history"
  );
  Services.fog.testResetFOG();

  const values = {
    "widgets.enabled": true,
    "widgets.privacy.enabled": true,
    "widgets.system.privacy.enabled": true,
    "widgets.lists.enabled": true,
    "widgets.system.lists.enabled": true,
  };

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({ Prefs: { values } }),
  };

  instance.recordEnabledWidgets();
  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue().sort(),
    ["lists", "privacy"],
    "privacy is listed while the profile records history"
  );

  // Turning history off flips enablement without any widget toggle, so the
  // PREF_CHANGED for the derived value has to re-record the list (Bug 2063207).
  values.recordsHistory = false;
  instance.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "recordsHistory", value: false },
  });

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    ["lists"],
    "privacy drops out once the profile records no history"
  );
});

add_task(async function test_recordEnabledWidgets_extension_widgets() {
  info(
    "recordEnabledWidgets should list an extension widget only while ExtensionWidgets has its extension"
  );
  Services.fog.testResetFOG();

  const extensionWidget = {
    id: "extensionWidget",
    telemetryName: "extension_widget",
    enabledPref: "widgets.extensionWidget.enabled",
    systemEnabledPref: "widgets.system.extensionWidget.enabled",
    extensionID: "fixture-widget@mozilla.org",
  };
  WIDGET_REGISTRY.push(extensionWidget);

  const state = {
    Prefs: {
      values: {
        "widgets.enabled": true,
        "widgets.lists.enabled": true,
        "widgets.system.lists.enabled": true,
        "widgets.extensionWidget.enabled": true,
        "widgets.system.extensionWidget.enabled": true,
      },
    },
    ExtensionWidgets: {},
  };

  let instance = new TelemetryFeed();
  instance.store = { getState: () => state };

  try {
    instance.recordEnabledWidgets();
    Assert.deepEqual(
      Glean.newtab.widgetsEnabledList.testGetValue(),
      ["lists"],
      "extension widget without its extension left out"
    );

    state.ExtensionWidgets = {
      [extensionWidget.extensionID]: { src: "moz-extension://uuid/w.html" },
    };
    await instance.onAction({
      type: actionTypes.WIDGETS_EXTENSIONS_UPDATE,
      data: state.ExtensionWidgets,
    });

    Assert.deepEqual(
      Glean.newtab.widgetsEnabledList.testGetValue(),
      ["lists", "extension_widget"],
      "extension widget listed"
    );
  } finally {
    WIDGET_REGISTRY.splice(WIDGET_REGISTRY.indexOf(extensionWidget), 1);
  }
});

add_task(async function test_recordEnabledWidgets_trainhop() {
  info(
    "recordEnabledWidgets should count a widget enabled via trainhopConfig when the system pref is off"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.enabled": true,
          "widgets.lists.enabled": true,
          "widgets.system.lists.enabled": false,
          region: "US",
          trainhopConfig: { widgets: { listsEnabled: true } },
        },
      },
    }),
  };

  instance.recordEnabledWidgets();

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    ["lists"],
    "widgetsEnabledList should include widgets gated on by trainhopConfig"
  );
});

add_task(async function test_recordEnabledWidgets_region_resolves() {
  info(
    "recordEnabledWidgets should re-record when the region resolves and reveals a trainhop widget"
  );
  Services.fog.testResetFOG();

  const values = {
    "widgets.enabled": true,
    "widgets.lists.enabled": true,
    "widgets.system.lists.enabled": false,
    trainhopConfig: { widgets: { listsEnabled: true } },
  };

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({ Prefs: { values } }),
  };

  instance.recordEnabledWidgets();
  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    [],
    "a trainhop-revealed widget is not listed while the region is unresolved"
  );

  values.region = "US";
  instance.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "region", value: "US" },
  });

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    ["lists"],
    "lists is listed once the region resolves"
  );
});

add_task(async function test_recordEnabledWidgets_container_disabled() {
  info(
    "recordEnabledWidgets should return an empty list when the widgets container is off"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.enabled": false,
          "widgets.lists.enabled": true,
          "widgets.system.lists.enabled": true,
        },
      },
    }),
  };

  instance.recordEnabledWidgets();

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    [],
    "widgetsEnabledList should be empty when widgets.enabled is false"
  );
});

add_task(async function test_recordEnabledWidgets_none_enabled() {
  info(
    "recordEnabledWidgets should set an empty list when no widgets are enabled"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "widgets.lists.enabled": false,
          "widgets.focusTimer.enabled": false,
          "widgets.weather.enabled": false,
        },
      },
    }),
  };

  instance.recordEnabledWidgets();

  Assert.deepEqual(
    Glean.newtab.widgetsEnabledList.testGetValue(),
    [],
    "widgetsEnabledList should be empty when no widgets are enabled"
  );
});

add_task(async function test_recordPageLayoutVariant_default() {
  info("recordPageLayoutVariant should report the default layout name");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = { getState: () => ({ Prefs: { values: {} } }) };

  instance.recordPageLayoutVariant();

  Assert.equal(
    Glean.newtab.pageLayoutVariant.testGetValue(),
    "nova-full-width",
    "pageLayoutVariant should be the default name rather than an empty string"
  );
});

add_task(async function test_handleSpacesUserEvent() {
  info("handleSpacesUserEvent should record a spaces_switch event");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  const session = instance.addSession("foo", "about:newtab");

  instance.handleSpacesUserEvent({
    data: { space: "widgets", previous_space: "stories", method: "tab" },
    meta: { fromTarget: "foo" },
  });

  const events = Glean.newtab.spacesSwitch.testGetValue();
  Assert.equal(events.length, 1, "one spaces_switch event should be recorded");
  Assert.deepEqual(
    events[0].extra,
    {
      newtab_visit_id: session.session_id,
      space: "widgets",
      previous_space: "stories",
      method: "tab",
    },
    "spaces_switch should carry the visit id and the switch it describes"
  );
});

add_task(async function test_recordHomeWidgets() {
  info(
    "recordHomeWidgets should list the widgets Home shows, only where users pin widgets to Home"
  );
  Services.fog.testResetFOG();

  const spaces = {
    order: ["home", "widgets"],
    default: "home",
    spaces: {
      home: { label: "Home", widgets: ["lists"] },
      widgets: { label: "Widgets" },
    },
  };
  const values = {
    "pageLayouts.variant": "spaces-thematic-v1",
    trainhopConfig: { spaces },
    "widgets.system.enabled": true,
    "widgets.enabled": true,
    "widgets.home": '["focusTimer","lists","clocks","stocks"]',
  };
  for (const id of ["lists", "focusTimer", "clocks", "stocks"]) {
    values[`widgets.system.${id}.enabled`] = true;
    values[`widgets.${id}.enabled`] = true;
  }
  values["widgets.focusTimer.enabled"] = false;

  let instance = new TelemetryFeed();
  instance.store = { getState: () => ({ Prefs: { values } }) };

  instance.recordHomeWidgets();
  Assert.equal(
    Glean.newtab.widgetsHomeList.testGetValue(),
    null,
    "widgetsHomeList is not set without a widget library"
  );

  values.trainhopConfig = { spaces: { ...spaces, widgetLibrary: "widgets" } };
  instance.recordEnabledWidgets();
  Assert.deepEqual(
    Glean.newtab.widgetsHomeList.testGetValue().sort(),
    ["clocks", "lists", "stocks"],
    "widgetsHomeList lists every pinned widget that is turned on, with no limit"
  );

  values["widgets.home"] = "[]";
  instance.onAction({
    type: actionTypes.PREF_CHANGED,
    data: { name: "widgets.home", value: "[]" },
  });
  Assert.deepEqual(
    Glean.newtab.widgetsHomeList.testGetValue(),
    [],
    "widgetsHomeList is re-recorded as empty when Home is emptied"
  );
});

add_task(async function test_widget_events_carry_widget_location() {
  info(
    "widgets_impression, widgets_user_event and widgets_ready should pass widget_location through only when it is set"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  const session = instance.addSession("foo", "about:newtab");
  const meta = { fromTarget: "foo" };

  instance.handleUnifiedWidgetImpression({
    data: {
      widget_name: "lists",
      widget_size: "medium",
      widget_location: "home_space",
    },
    meta,
  });
  instance.handleUnifiedWidgetUserEvent({
    data: {
      widget_name: "lists",
      widget_source: "widget",
      user_action: "list_create",
      widget_location: "sidebar",
    },
    meta,
  });
  instance.handleUnifiedWidgetImpression({
    data: { widget_name: "clocks", widget_size: "medium" },
    meta,
  });
  instance.handleUnifiedWidgetReady({
    data: {
      widget_name: "lists",
      widget_size: "medium",
      widget_location: "home_space",
    },
    meta,
  });

  const impressions = Glean.newtab.widgetsImpression.testGetValue();
  Assert.deepEqual(
    impressions.map(e => e.extra),
    [
      {
        newtab_visit_id: session.session_id,
        widget_name: "lists",
        widget_size: "medium",
        widget_location: "home_space",
      },
      {
        newtab_visit_id: session.session_id,
        widget_name: "clocks",
        widget_size: "medium",
      },
    ],
    "widget_location is recorded when given and left out otherwise"
  );
  Assert.equal(
    Glean.newtab.widgetsUserEvent.testGetValue()[0].extra.widget_location,
    "sidebar",
    "widgets_user_event carries widget_location"
  );
  Assert.equal(
    Glean.newtab.widgetsReady.testGetValue()[0].extra.widget_location,
    "home_space",
    "widgets_ready carries widget_location"
  );
});

add_task(async function test_handleSpacesUserEvent_no_session() {
  info("handleSpacesUserEvent should record nothing without a session");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();

  instance.handleSpacesUserEvent({
    data: { space: "widgets", previous_space: "stories", method: "tab" },
    meta: { fromTarget: "missing" },
  });

  Assert.equal(
    Glean.newtab.spacesSwitch.testGetValue(),
    null,
    "no event should be recorded for an unknown port"
  );
});

add_task(async function test_handleUnifiedWidget_widget_variant() {
  info("The unified widget handlers should pass widget_variant through");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  const session = instance.addSession("foo", "about:newtab");
  const meta = { fromTarget: "foo" };
  const base = { widget_name: "privacy", widget_size: "medium" };
  const withVariant = { ...base, widget_variant: "no_trackers_blocked" };

  instance.handleUnifiedWidgetImpression({ data: withVariant, meta });
  instance.handleUnifiedWidgetUserEvent({
    data: { ...withVariant, user_action: "learn_more" },
    meta,
  });
  instance.handleUnifiedWidgetEnabled({
    data: { ...withVariant, enabled: false },
    meta,
  });
  instance.handleUnifiedWidgetError({
    data: { ...withVariant, error_type: "load_error" },
    meta,
  });
  instance.handleUnifiedWidgetReady({ data: withVariant, meta });
  instance.handleUnifiedWidgetImpression({
    data: { widget_name: "lists", widget_size: "large" },
    meta,
  });

  for (const metric of [
    "widgetsImpression",
    "widgetsUserEvent",
    "widgetsEnabled",
    "widgetsError",
    "widgetsReady",
  ]) {
    const [event] = Glean.newtab[metric].testGetValue();
    Assert.equal(
      event.extra.widget_variant,
      "no_trackers_blocked",
      `${metric} should carry widget_variant`
    );
    Assert.equal(event.extra.newtab_visit_id, session.session_id);
  }
  Assert.deepEqual(
    Glean.newtab.widgetsImpression.testGetValue()[1].extra,
    {
      newtab_visit_id: session.session_id,
      widget_name: "lists",
      widget_size: "large",
    },
    "a widget that sends no variant should record no widget_variant"
  );
});

add_task(async function test_recordPageLayoutVariant_pref() {
  info("recordPageLayoutVariant should report the variant set by pref");
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: { values: { "pageLayouts.variant": "side-by-side-content-lead" } },
    }),
  };

  instance.recordPageLayoutVariant();

  Assert.equal(
    Glean.newtab.pageLayoutVariant.testGetValue(),
    "side-by-side-content-lead",
    "pageLayoutVariant should be the pref value"
  );
});

add_task(async function test_recordPageLayoutVariant_trainhop() {
  info(
    "recordPageLayoutVariant should report a variant that only trainhopConfig carries"
  );
  Services.fog.testResetFOG();

  let instance = new TelemetryFeed();
  instance.store = {
    getState: () => ({
      Prefs: {
        values: {
          "pageLayouts.variant": "nova-full-width",
          trainhopConfig: {
            pageLayouts: { variant: "side-by-side-widgets-lead" },
          },
        },
      },
    }),
  };

  instance.recordPageLayoutVariant();

  Assert.equal(
    Glean.newtab.pageLayoutVariant.testGetValue(),
    "side-by-side-widgets-lead",
    "pageLayoutVariant should follow trainhopConfig over the pref"
  );
});

add_task(async function test_onAction_NEW_TAB_SCROLL_tracks_max_threshold() {
  info(
    "TelemetryFeed should track the deepest scroll threshold passed in a session"
  );
  let instance = new TelemetryFeed();
  instance.addSession("foo");

  const scrollAction = threshold => ({
    type: actionTypes.NEW_TAB_SCROLL,
    data: { threshold },
    meta: { fromTarget: "foo" },
  });

  instance.onAction(scrollAction(50));
  Assert.equal(instance.sessions.get("foo").max_scroll_threshold, 50);

  instance.onAction(scrollAction(250));
  Assert.equal(instance.sessions.get("foo").max_scroll_threshold, 250);

  info("A shallower threshold should not lower the recorded one");
  instance.onAction(scrollAction(100));
  Assert.equal(instance.sessions.get("foo").max_scroll_threshold, 250);
});

add_task(async function test_onAction_NEW_TAB_SCROLL_no_throw_on_bad_session() {
  info("TelemetryFeed should ignore a scroll for an unknown session");
  let instance = new TelemetryFeed();
  try {
    instance.onAction({
      type: actionTypes.NEW_TAB_SCROLL,
      data: { threshold: 50 },
      meta: { fromTarget: "doesn't exist" },
    });
    Assert.ok(true, "Did not throw.");
  } catch (e) {
    Assert.ok(false, "Should not have thrown.");
  }
});

add_task(async function test_endSession_records_scroll_metrics() {
  info(
    "TelemetryFeed.endSession should record a scroll metric for every " +
      "threshold, true only for those the session passed"
  );
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");

  const session = instance.addSession("foo");
  session.perf.visibility_event_rcvd_ts = 1000;
  instance.onAction({
    type: actionTypes.NEW_TAB_SCROLL,
    data: { threshold: 100 },
    meta: { fromTarget: "foo" },
  });

  // Ping lifetime metrics are cleared on submission, so assert before it.
  let pingSubmitted = new Promise(resolve => {
    GleanPings.newtab.testBeforeNextSubmit(() => {
      Assert.equal(Glean.newtab.scroll.testGetValue(), true);
      Assert.equal(Glean.newtab.scroll100.testGetValue(), true);
      Assert.equal(Glean.newtab.scroll250.testGetValue(), false);
      resolve();
    });
  });

  await instance.endSession("foo");
  await pingSubmitted;

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

add_task(async function test_endSession_records_false_scroll_metrics() {
  info(
    "TelemetryFeed.endSession should record false for every scroll metric " +
      "when the session was never scrolled"
  );
  Services.fog.testResetFOG();
  Services.prefs.setBoolPref(PREF_TELEMETRY, true);
  let sandbox = sinon.createSandbox();
  let instance = new TelemetryFeed();
  sandbox.stub(instance, "configureContentPing");

  const session = instance.addSession("foo");
  session.perf.visibility_event_rcvd_ts = 1000;

  let pingSubmitted = new Promise(resolve => {
    GleanPings.newtab.testBeforeNextSubmit(() => {
      Assert.equal(Glean.newtab.scroll.testGetValue(), false);
      Assert.equal(Glean.newtab.scroll100.testGetValue(), false);
      Assert.equal(Glean.newtab.scroll250.testGetValue(), false);
      resolve();
    });
  });

  await instance.endSession("foo");
  await pingSubmitted;

  Services.prefs.clearUserPref(PREF_TELEMETRY);
  sandbox.restore();
});

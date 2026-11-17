/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests sports suggestions.

// Trying to avoid timeouts in TV mode, especially on debug Mac.
requestLongerTimeout(3);

const TEST_ICON_URL_HOME = TEST_BASE_URL + "moz.png";
const TEST_ICON_URL_AWAY = TEST_BASE_URL + "moz-flipped.png";

// Known sports, i.e., sports that have have fallback icons
const KNOWN_SPORTS = [
  {
    sportCategory: "baseball",
    fallbackIconName: "baseball",
    sport: "MLB",
  },
  {
    sportCategory: "basketball",
    fallbackIconName: "basketball",
    sport: "NBA",
  },
  {
    sportCategory: "cricket",
    fallbackIconName: "cricket",
    sport: "IPL",
  },
  {
    sportCategory: "football",
    fallbackIconName: "american-football",
    sport: "NFL",
  },
  {
    sportCategory: "golf",
    fallbackIconName: "golf",
    sport: "PGA",
  },
  {
    sportCategory: "hockey",
    fallbackIconName: "hockey",
    sport: "NHL",
  },
  {
    sportCategory: "racing",
    fallbackIconName: "racing",
    sport: "F1",
  },
  {
    sportCategory: "soccer",
    fallbackIconName: "soccer",
    sport: "FIFA",
  },
];

add_setup(async function () {
  await SearchTestUtils.installSearchExtension({}, { setAsDefault: true });
  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
  });

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    merinoSuggestions: makeMerinoSuggestions([]),
    prefs: [
      ["sports.featureGate", true],
      ["suggest.sports", true],
      ["suggest.quicksuggest.all", true],
    ],
  });

  UrlbarTestUtils.stubLocale("en-US");
  registerCleanupFunction(() => {
    UrlbarTestUtils.stubNowZonedDateTime(null);
    UrlbarTestUtils.stubLocale(null);
  });
});

//////////////////////////////////////////////////////////////////////////////
//
// Known sports tasks

// * Known sports
// * In each suggestion value, both teams have icons
// * Game statuses: past (not today)
//
// => Each item should show both teams' icons
add_task(async function knownSports_bothIcons_past_notToday() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-10-30T17:00:00Z",
        statusType: "past",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      })
    ),
  });
});

// * Known sports
// * In each suggestion value, both teams have icons
// * Game statuses: past (today)
//
// => Each item should show both teams' icons
add_task(async function knownSports_bothIcons_past_today() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-10-31T17:00:00Z",
        statusType: "past",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
          itemOverrides: {
            date: "Today",
            status: {
              l10n: {
                id: "urlbar-result-sports-status-final",
              },
            },
            "home-team-date-chiclet-day": {
              textContent: "31",
            },
            "home-team-date-chiclet-month": {
              textContent: "Oct",
            },
            "away-team-date-chiclet-day": {
              textContent: "31",
            },
            "away-team-date-chiclet-month": {
              textContent: "Oct",
            },
          },
        },
      })
    ),
  });
});

// * Known sports
// * In each suggestion value, both teams have icons
// * Game statuses: live
//
// => Each item should show both teams' icons
add_task(async function knownSports_bothIcons_live() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      })
    ),
  });
});

// * Known sports
// * In each suggestion value, both teams have icons
// * Game statuses: scheduled
//
// => Each item should show both teams' icons
add_task(async function knownSports_bothIcons_scheduled() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-11-01T17:00:00Z",
        statusType: "scheduled",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      })
    ),
  });
});

// * Known sports
// * In each suggestion value, neither team has an icon
// * Game statuses: past
//
// => Each item should show a single fallback icon
add_task(async function knownSports_noIcons_past() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-10-30T17:00:00Z",
        statusType: "past",
        homeTeam: {
          score: 1,
        },
        awayTeam: {
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: true,
        },
      })
    ),
  });
});

// * Known sports
// * In each suggestion value, neither team has an icon
// * Game statuses: live
//
// => Each item should show a single fallback icon
add_task(async function knownSports_noIcons_live() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          score: 1,
        },
        awayTeam: {
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: true,
        },
      })
    ),
  });
});

// * Known sports
// * In each suggestion value, neither team has an icon
// * Game statuses: scheduled
//
// => Each item should show a single fallback icon
add_task(async function knownSports_noIcons_scheduled() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-11-01T17:00:00Z",
        statusType: "scheduled",
        homeTeam: {},
        awayTeam: {},
        expected: {
          isAwayTeamImageHidden: true,
        },
      })
    ),
  });
});

// * Known sports
// * In each suggestion value, only the home team has an icon
// * Game statuses: doesn't matter, covered by other tasks
//
// => Each item should show the home team icon and a fallback for away
add_task(async function knownSports_homeTeamIcon() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      })
    ),
  });
});

// * Known sports
// * In each suggestion value, only the away team has an icon
// * Game statuses: doesn't matter, covered by other tasks
//
// => Each item should show the away team icon and a fallback for home
add_task(async function knownSports_awayTeamIcon() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: KNOWN_SPORTS.map(data =>
      makeValueAndExpectedItem({
        ...data,
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      })
    ),
  });
});

//////////////////////////////////////////////////////////////////////////////
//
// Unknown sports tasks

// * Unknown sport
// * In each suggestion value, both teams have icons
// * Game statuses: past
//
// => Each item should show both teams' icons
add_task(async function unknownSports_bothIcons_past() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-30T17:00:00Z",
        statusType: "past",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

// * Unknown sports
// * In each suggestion value, both teams have icons
// * Game statuses: live
//
// => Each item should show both teams' icons
add_task(async function unknownSports_bothIcons_live() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

// * Unknown sports
// * In each suggestion value, both teams have icons
// * Game statuses: scheduled
//
// => Each item should show both teams' icons
add_task(async function unknownSports_bothIcons_scheduled() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-11-01T17:00:00Z",
        statusType: "scheduled",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

// * Unknown sports
// * In each suggestion value, neither team has an icon
// * Game statuses: past
//
// => Each item should show a single date chiclet
add_task(async function unknownSports_noIcons_past() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-30T17:00:00Z",
        statusType: "past",
        homeTeam: {
          score: 1,
        },
        awayTeam: {
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: true,
        },
      }),
    ],
  });
});

// * Unknown sports
// * In each suggestion value, neither team has an icon
// * Game statuses: live
//
// => Each item should show a single date chiclet
add_task(async function unknownSports_noIcons_live() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          score: 1,
        },
        awayTeam: {
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: true,
        },
      }),
    ],
  });
});

// * Unknown sports
// * In each suggestion value, neither team has an icon
// * Game statuses: scheduled
//
// => Each item should show a single date chiclet
add_task(async function unknownSports_noIcons_scheduled() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-11-01T17:00:00Z",
        statusType: "scheduled",
        homeTeam: {},
        awayTeam: {},
        expected: {
          isAwayTeamImageHidden: true,
        },
      }),
    ],
  });
});

// * Unknown sports
// * In each suggestion value, only the home team has an icon
// * Game statuses: doesn't matter, covered by other tasks
//
// => Each item should show the home team icon and a date chiclet for away
add_task(async function unknownSports_homeTeamIcon() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

// * Unknown sports
// * In each suggestion value, only the away team has an icon
// * Game statuses: doesn't matter, covered by other tasks
//
// => Each item should show the away team icon and a date chiclet for home
add_task(async function unknownSports_awayTeamIcon() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

//////////////////////////////////////////////////////////////////////////////
//
// Unusual status and score combinations

// Past without scores
//
// => Show the no-score UI: "Home Team vs Away Team"
add_task(async function pastWithoutScores() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-30T17:00:00Z",
        statusType: "past",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

// Live without scores
//
// => Show the no-score UI: "Home Team vs Away Team"
add_task(async function liveWithoutScores() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

// Scheduled with scores
//
// => Show the score UI: "Home Team 1, Away Team 0"
add_task(async function scheduledWithScores() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-11-01T17:00:00Z",
        statusType: "scheduled",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
          score: 0,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

// Score in one team but not the other
//
// => Show the no-score UI: "Home Team vs Away Team"
add_task(async function scoreInOneTeam() {
  await doTest({
    now: "2025-10-31T14:00:00-04:00[-04:00]",
    data: [
      makeValueAndExpectedItem({
        sportCategory: "Unknown sport_category",
        sport: "Unknown sport",
        date: "2025-10-31T17:00:00Z",
        statusType: "live",
        homeTeam: {
          icon: TEST_ICON_URL_HOME,
          score: 1,
        },
        awayTeam: {
          icon: TEST_ICON_URL_AWAY,
        },
        expected: {
          isAwayTeamImageHidden: false,
        },
      }),
    ],
  });
});

// The l10n IDs of a realtime suggestion's result menu commands are derived from
// its realtime type, so the menu can be broken for one type only.
add_task(async function resultMenu() {
  MerinoTestUtils.server.response.body.suggestions = makeMerinoSuggestions([
    {
      date: "2025-10-31T17:00:00Z",
      sport: "NFL",
      sport_category: "football",
      query: "query 1",
      home_team: { name: "Home Team", score: 1 },
      away_team: { name: "Away Team", score: 0 },
      status_type: "live",
    },
  ]);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "only match the Merino suggestion",
  });
  await UrlbarTestUtils.openResultMenu(window, { resultIndex: 1 });

  let menuitems = gURLBar.view.resultMenu.querySelectorAll("panel-item");
  Assert.deepEqual(
    [...menuitems].map(m => document.l10n.getAttributes(m).id),
    [
      "urlbar-result-menu-show-less-frequently2",
      "urlbar-result-menu-dont-show-sports2",
      "urlbar-result-menu-manage-firefox-suggest2",
      "urlbar-result-menu-learn-more2",
      "urlbar-view-context-menu-skip-menu-with-tab",
    ],
    "The result menu should contain the expected commands"
  );
  await TestUtils.waitForCondition(
    () => [...menuitems].every(m => m.textContent),
    "Waiting for all commands to be labeled"
  );

  gURLBar.view.resultMenu.removeAttribute("open");
  await UrlbarTestUtils.promisePopupClose(window);
  gURLBar.handleRevert();
});

async function doTest({ now, data }) {
  let nows = Array.isArray(now) ? now : [now];

  MerinoTestUtils.server.response.body.suggestions = makeMerinoSuggestions(
    data.map(d => d.value)
  );

  let expectedItems = data.map(d => d.expectedItem);

  for (let n of nows) {
    info("Testing with `now`: " + n);
    UrlbarTestUtils.stubNowZonedDateTime(n);
    await doOneTest({ expectedItems });
  }
}

async function doOneTest({ expectedItems }) {
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "only match the Merino suggestion",
  });

  let {
    result,
    element: { row },
  } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

  // Make sure the row is a sports suggestion.
  Assert.equal(
    result.providerName,
    "UrlbarProviderQuickSuggest",
    "Row should be a Suggest result"
  );
  Assert.equal(
    result.payload.provider,
    "sports",
    "Row should be a sports result"
  );

  if (expectedItems.length > 1) {
    Assert.deepEqual(
      document.l10n.getAttributes(row._content),
      {
        id: "urlbar-result-aria-group-sports",
        args: null,
      },
      "ARIA group label should be set on the row inner"
    );
  } else {
    Assert.deepEqual(
      document.l10n.getAttributes(row._content),
      {
        id: null,
        args: null,
      },
      "ARIA group label should not be set on the row inner"
    );
  }

  // Check each realtime item in the row.
  for (let i = 0; i < expectedItems.length; i++) {
    let expectedItem = expectedItems[i];

    // Check each expected child element in the item.
    for (let [childNamePrefix, expectedValue] of Object.entries(expectedItem)) {
      let sep = childNamePrefix == "item" ? "_" : "-";
      let childName = `${childNamePrefix}${sep}${i}`;
      let child = row.querySelector(`[name=${childName}]`);

      if (expectedValue === null) {
        Assert.ok(!child, "Child element should not exist: " + childName);
        continue;
      }

      Assert.ok(child, "Expected child element should exist: " + childName);

      let backgroundImage = "none";
      let isHidden = false;
      let attributes = {};
      if (typeof expectedValue == "object") {
        backgroundImage = expectedValue.backgroundImage || backgroundImage;
        isHidden = !!expectedValue.isHidden || isHidden;
        attributes = expectedValue.attributes || attributes;
      }

      // background image
      Assert.equal(
        window.getComputedStyle(child).backgroundImage,
        backgroundImage,
        "Child element should have expected background-image: " + childName
      );

      // is hidden
      Assert.equal(
        BrowserTestUtils.isVisible(child),
        !isHidden,
        "Child element should be visible as expected: " + childName
      );

      // attributes
      for (let [attr, value] of Object.entries(attributes)) {
        if (value === null) {
          Assert.ok(
            !child.hasAttribute(attr),
            "Child element should not have attribute: " +
              JSON.stringify({ childName, attr })
          );
        } else {
          Assert.ok(
            child.hasAttribute(attr),
            "Child element should have expected attribute: " +
              JSON.stringify({ childName, attr })
          );

          if (childNamePrefix.endsWith("image")) {
            UrlbarTestUtils.checkImageUrl(
              child.getAttribute(attr),
              UrlbarTestUtils.makeMozRemoteImageUrl(value),
              "Child image attribute should have expected value: " +
                JSON.stringify({ childName, attr })
            );
          } else {
            Assert.equal(
              child.getAttribute(attr),
              value,
              "Child element attribute should have expected value: " +
                JSON.stringify({ childName, attr })
            );
          }
        }
      }

      // textContent or l10n
      if (typeof expectedValue == "string") {
        Assert.equal(
          child.textContent,
          expectedValue,
          "Child element should have expected textContent: " + childName
        );
      } else if (typeof expectedValue.textContent == "string") {
        Assert.equal(
          child.textContent,
          expectedValue.textContent,
          "Child element should have expected textContent: " + childName
        );
      } else if (expectedValue.l10n) {
        Assert.equal(
          child.dataset.l10nId,
          expectedValue.l10n.id,
          "Child element should have expected l10nId: " + childName
        );
        if (expectedValue.l10n.args) {
          Assert.deepEqual(
            JSON.parse(child.dataset.l10nArgs),
            expectedValue.l10n.args,
            "Child element should have expected l10nArgs: " + childName
          );
        } else {
          Assert.ok(
            !child.dataset.l10nArgs,
            "Child element shouldn't have any l10nArgs: " + childName
          );
        }
      }
    }
  }

  await UrlbarTestUtils.promisePopupClose(window);
  gURLBar.handleRevert();
}

/**
 * Returns an object `{ value, expectedItem }`.
 *
 * `value` is a value object that can be included in a sports suggestion.
 *
 * `expectedItem` is a description of the expected item DOM in the suggestion's
 * row that corresponds to `value`. It can be passed to `doTest()` and
 * `doOneTest()`.
 *
 * @param {object} options
 * @param {string} options.sport
 *   The `sport` in the suggestion value.
 * @param {string} options.sportCategory
 *   The `sport_category` in the suggestion value.
 * @param {string} options.fallbackIconName
 *   The basename without file extension of the expected fallback icon for the
 *   sport.
 * @param {string} options.date
 *   The `date` in the suggestion value.
 * @param {string} options.statusType
 *   The `status_type` in the suggestion value.
 * @param {{ icon: ?string, score: ?number }} options.homeTeam
 *   Partial `home_team` in the suggestion value.
 * @param {{ icon: ?string, score: ?number }} options.awayTeam
 *   Partial `away_team` in the suggestion value.
 * @param {object} options.expected
 *   {bool} isAwayTeamImageHidden
 *     Whether the away team image container is expected to be hidden (because
 *     it's the same as the home team image container).
 *   {?object} itemOverrides
 *     Values to use for `expectedItem` instead of the defaults that this
 *     function normally creates. See the code for details.
 *
 * @returns {{ value: any, expectedItem: any }}
 */
function makeValueAndExpectedItem({
  sport,
  sportCategory,
  fallbackIconName,
  date,
  statusType,
  expected,
  homeTeam = {},
  awayTeam = {},
}) {
  let expectedItem = {
    sport,

    item: {
      attributes: {
        "sport-category": sportCategory,
        status: statusType,
      },
    },

    "home-team-image-container": {
      attributes: {
        "has-team-icon": homeTeam.icon ? "" : null,
      },
      backgroundImage:
        homeTeam.icon || !fallbackIconName
          ? null
          : `url("chrome://browser/skin/urlbar/sports-${fallbackIconName}.svg")`,
    },
    "home-team-image": {
      attributes: {
        src: homeTeam.icon ?? null,
      },
    },

    "away-team-image-container": {
      attributes: {
        "has-team-icon": awayTeam.icon ? "" : null,
      },
      backgroundImage:
        awayTeam.icon || !fallbackIconName
          ? null
          : `url("chrome://browser/skin/urlbar/sports-${fallbackIconName}.svg")`,
      isHidden: expected.isAwayTeamImageHidden,
    },
    "away-team-image": {
      attributes: {
        src: awayTeam.icon ?? null,
      },
      isHidden: expected.isAwayTeamImageHidden,
    },
  };

  if (homeTeam.hasOwnProperty("score") && awayTeam.hasOwnProperty("score")) {
    expectedItem = {
      ...expectedItem,
      "home-team-name": "Home Team",
      "home-team-score": homeTeam.score,
      "away-team-name": "Away Team",
      "away-team-score": awayTeam.score,
    };
  } else {
    expectedItem = {
      ...expectedItem,
      "team-names": {
        l10n: {
          id: "urlbar-result-sports-team-names",
          args: {
            homeTeam: "Home Team",
            awayTeam: "Away Team",
          },
        },
      },
    };
  }

  let chicletDay;
  let chicletMonth;

  switch (statusType) {
    case "past":
      expectedItem = {
        ...expectedItem,
        date: "Yesterday",
        status: "",
      };
      chicletDay = "30";
      chicletMonth = "Oct";
      break;
    case "live":
      expectedItem = {
        ...expectedItem,
        date: "Today",
        status: {
          l10n: {
            id: "urlbar-result-sports-status-live",
          },
        },
      };
      chicletDay = "31";
      chicletMonth = "Oct";
      break;
    case "scheduled":
      expectedItem = {
        ...expectedItem,
        date: {
          l10n: {
            id: "urlbar-result-sports-game-date-with-time",
            args: {
              date: "Tomorrow",
              time: "1:00 PM GMT-4",
            },
          },
        },
        status: "",
      };
      chicletDay = "1";
      chicletMonth = "Nov";
      break;
  }

  expectedItem = {
    ...expectedItem,
    "home-team-date-chiclet-day": {
      textContent: chicletDay,
      isHidden: !!homeTeam.icon || !!fallbackIconName,
    },
    "home-team-date-chiclet-month": {
      textContent: chicletMonth,
      isHidden: !!homeTeam.icon || !!fallbackIconName,
    },
    "away-team-date-chiclet-day": {
      textContent: chicletDay,
      isHidden:
        !!awayTeam.icon || !!fallbackIconName || expected.isAwayTeamImageHidden,
    },
    "away-team-date-chiclet-month": {
      textContent: chicletMonth,
      isHidden:
        !!awayTeam.icon || !!fallbackIconName || expected.isAwayTeamImageHidden,
    },
  };

  // Merge `expectedItem` with the passed-in overrides.
  if (expected.itemOverrides) {
    mergeObjects(expectedItem, expected.itemOverrides);
  }

  return {
    expectedItem,
    value: {
      date,
      sport,
      sport_category: sportCategory,
      query: "query 1",
      home_team: {
        name: "Home Team",
        ...homeTeam,
      },
      away_team: {
        name: "Away Team",
        ...awayTeam,
      },
      status_type: statusType,
    },
  };
}

function makeMerinoSuggestions(values) {
  return [
    {
      provider: "sports",
      is_sponsored: false,
      score: 0.2,
      title: "",
      custom_details: {
        sports: {
          values,
        },
      },
    },
  ];
}

function mergeObjects(dest, other) {
  for (let [key, value] of Object.entries(other)) {
    if (
      value &&
      typeof value == "object" &&
      dest[key] &&
      typeof dest[key] == "object"
    ) {
      mergeObjects(dest[key], value);
    } else {
      dest[key] = value;
    }
  }
}

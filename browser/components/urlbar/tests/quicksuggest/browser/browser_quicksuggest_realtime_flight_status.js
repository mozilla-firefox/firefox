/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const TEST_MERINO_SINGLE = [
  {
    provider: "flightaware",
    is_sponsored: false,
    score: 0,
    title: "Flight Suggestion",
    custom_details: {
      flightaware: {
        values: [
          {
            flight_number: "A1",
            airline: {
              name: null,
              code: null,
              icon: null,
            },
            origin: {
              city: "Origin",
              code: "O",
            },
            destination: {
              city: "Destination",
              code: "D",
            },
            departure: {
              scheduled_time: "2025-09-17T14:05:00Z",
            },
            arrival: {
              scheduled_time: "2025-09-17T18:30:00Z",
            },
            status: "Scheduled",
            url: "https://example.com/A1",
          },
        ],
      },
    },
  },
];

const TEST_MERINO_MULTI = [
  {
    provider: "flightaware",
    is_sponsored: false,
    score: 0,
    title: "Flight Suggestion",
    custom_details: {
      flightaware: {
        values: [
          {
            flight_number: "A1",
            airline: {
              name: "A Air",
              code: "A",
              icon: "chrome://browser/skin/urlbar/market-up.svg",
            },
            origin: {
              city: "Origin 1",
              code: "O1",
            },
            destination: {
              city: "Destination 1",
              code: "D1",
            },
            departure: {
              scheduled_time: "2025-09-01T14:01:00Z",
            },
            arrival: {
              scheduled_time: "2025-09-01T18:31:00Z",
            },
            status: "Scheduled",
            url: "https://example.com/A1",
          },
          {
            flight_number: "A2",
            airline: {
              name: null,
              code: null,
              icon: null,
            },
            origin: {
              city: "Origin 2",
              code: "O2",
            },
            destination: {
              city: "Destination 2",
              code: "D2",
            },
            departure: {
              scheduled_time: "2025-09-02T14:02:00+02:00",
            },
            arrival: {
              scheduled_time: "2025-09-02T18:32:00-02:00",
            },
            status: "En Route",
            progress_percent: 0,
            time_left_minutes: 1,
            url: "https://example.com/A2",
          },
          {
            flight_number: "A3",
            airline: {
              name: null,
              code: null,
              icon: null,
            },
            origin: {
              city: "Origin 3",
              code: "O3",
            },
            destination: {
              city: "Destination 3",
              code: "D3",
            },
            departure: {
              scheduled_time: "2025-09-03T14:03:00+0300",
            },
            arrival: {
              scheduled_time: "2025-09-03T18:33:00-0300",
            },
            status: "Arrived",
            time_left_minutes: 0,
            url: "https://example.com/A3",
          },
          {
            flight_number: "A4",
            airline: {
              name: null,
              code: null,
              icon: null,
            },
            origin: {
              city: "Origin 4",
              code: "O4",
            },
            destination: {
              city: "Destination 4",
              code: "D4",
            },
            departure: {
              scheduled_time: "2025-09-04T14:04:00+10:30",
            },
            arrival: {
              scheduled_time: "2025-09-04T18:34:00-10:30",
            },
            status: "Cancelled",
            url: "https://example.com/A4",
          },
        ],
      },
    },
  },
];

add_setup(async function () {
  await SearchTestUtils.installSearchExtension({}, { setAsDefault: true });
  UrlbarTestUtils.stubLocale("en-US");
  registerCleanupFunction(async () => {
    await PlacesUtils.history.clear();
    UrlbarTestUtils.stubLocale(null);
  });

  await QuickSuggestTestUtils.ensureQuickSuggestInit({
    merinoSuggestions: TEST_MERINO_SINGLE,
    prefs: [
      ["flightStatus.featureGate", true],
      ["suggest.flightStatus", true],
      ["suggest.quicksuggest.all", true],
    ],
  });
});

add_task(async function ui_basic() {
  MerinoTestUtils.server.response.body.suggestions = TEST_MERINO_MULTI;

  const expectedList = [
    {
      flight_number: "A1, A Air",
      image: "chrome://browser/skin/urlbar/market-up.svg",
      departure_time: "2:01 PM",
      departure_date: "Mon, Sep 1",
      arrival_time: "6:31 PM",
      origin_airport: "Origin 1 (O1)",
      destination_airport: "Destination 1 (D1)",
      status: "On time",
      url: "https://example.com/A1",
    },
    {
      flight_number: "A2",
      image: "",
      departure_time: "2:02 PM",
      departure_date: "Tue, Sep 2",
      arrival_time: "6:32 PM",
      origin_airport: "Origin 2 (O2)",
      destination_airport: "Destination 2 (D2)",
      status: "In flight",
      time_left: "1 min left",
      url: "https://example.com/A2",
    },
    {
      flight_number: "A3",
      image: "chrome://browser/skin/urlbar/flight-airline.svg",
      departure_time: "2:03 PM",
      departure_date: "Wed, Sep 3",
      arrival_time: "6:33 PM",
      origin_airport: "Origin 3 (O3)",
      destination_airport: "Destination 3 (D3)",
      status: "Arrived",
      url: "https://example.com/A3",
    },
    {
      flight_number: "A4",
      image: "chrome://browser/skin/urlbar/flight-airline.svg",
      departure_time: "2:04 PM",
      departure_date: "Thu, Sep 4",
      arrival_time: "6:34 PM",
      origin_airport: "Origin 4 (O4)",
      destination_airport: "Destination 4 (D4)",
      status: "Cancelled",
      url: "https://example.com/A4",
    },
  ];

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "only match the Merino suggestion",
  });
  let { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

  await assertUI({ row: element.row, expectedList });

  await UrlbarTestUtils.promisePopupClose(window);
  gURLBar.handleRevert();
});

add_task(async function ui_delayed() {
  MerinoTestUtils.server.response.body.suggestions = [
    {
      provider: "flightaware",
      is_sponsored: false,
      score: 0,
      title: "Flight Suggestion",
      custom_details: {
        flightaware: {
          values: [
            {
              flight_number: "D1",
              airline: {
                name: "Delay Air",
                code: "D",
                icon: "chrome://browser/skin/urlbar/market-unchanged.svg",
              },
              origin: {
                city: "Origin D1",
                code: "OD1",
              },
              destination: {
                city: "Destination D1",
                code: "DD1",
              },
              departure: {
                scheduled_time: "2025-09-05T14:05:00+1130",
                estimated_time: "2025-09-05T15:05:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:35:00Z",
                estimated_time: "2025-09-05T19:35:00Z",
              },
              status: "Delayed",
              delayed: true,
              url: "https://example.com/D1",
            },
            {
              flight_number: "D2",
              airline: {
                name: null,
                code: null,
                icon: null,
              },
              origin: {
                city: "Origin D2",
                code: "OD2",
              },
              destination: {
                city: "Destination D2",
                code: "DD2",
              },
              departure: {
                scheduled_time: "2025-09-06T14:06:00Z",
                estimated_time: "2025-09-06T15:06:00Z",
              },
              arrival: {
                scheduled_time: "2025-09-06T18:36:00Z",
                estimated_time: "2025-09-06T19:36:00Z",
              },
              status: "En Route",
              progress_percent: 18,
              time_left_minutes: 583,
              delayed: true,
              url: "https://example.com/D2",
            },
            {
              flight_number: "D3",
              airline: {
                name: null,
                code: null,
                icon: null,
              },
              origin: {
                city: "Origin D3",
                code: "OD3",
              },
              destination: {
                city: "Destination D3",
                code: "DD3",
              },
              departure: {
                scheduled_time: "2025-09-07T14:07:00Z",
                estimated_time: "2025-09-07T15:07:00Z",
              },
              arrival: {
                scheduled_time: "2025-09-07T18:37:00Z",
                estimated_time: "2025-09-07T19:37:00Z",
              },
              status: "Arrived",
              delayed: true,
              time_left_minutes: 0,
              url: "https://example.com/D3",
            },
          ],
        },
      },
    },
  ];

  const expectedList = [
    {
      flight_number: "D1, Delay Air",
      image: "chrome://browser/skin/urlbar/market-unchanged.svg",
      departure_time: "2:05 PM",
      departure_date: "Fri, Sep 5",
      arrival_time: "6:35 PM",
      origin_airport: "Origin D1 (OD1)",
      destination_airport: "Destination D1 (DD1)",
      status: "Delayed until 3:05 PM",
      url: "https://example.com/D1",
    },
    {
      flight_number: "D2",
      image: "",
      departure_time: "3:06 PM",
      departure_date: "Sat, Sep 6",
      arrival_time: "7:36 PM",
      origin_airport: "Origin D2 (OD2)",
      destination_airport: "Destination D2 (DD2)",
      status: "In flight",
      time_left: "9 hr, 43 min left",
      url: "https://example.com/D2",
    },
    {
      flight_number: "D3",
      image: "chrome://browser/skin/urlbar/flight-airline.svg",
      departure_time: "3:07 PM",
      departure_date: "Sun, Sep 7",
      arrival_time: "7:37 PM",
      origin_airport: "Origin D3 (OD3)",
      destination_airport: "Destination D3 (DD3)",
      status: "Arrived",
      url: "https://example.com/D3",
    },
  ];

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "only match the Merino suggestion",
  });
  let { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

  await assertUI({ row: element.row, expectedList });

  await UrlbarTestUtils.promisePopupClose(window);
  gURLBar.handleRevert();
});

add_task(async function ui_inflight() {
  MerinoTestUtils.server.response.body.suggestions = [
    {
      provider: "flightaware",
      is_sponsored: false,
      score: 0,
      title: "Flight Suggestion",
      custom_details: {
        flightaware: {
          values: [
            {
              flight_number: "I1",
              airline: {
                name: "In flight Air",
                code: "I",
                icon: "chrome://browser/skin/urlbar/flight-airline.svg",
                color: "#f00",
              },
              origin: {
                city: "Origin I1",
                code: "OI1",
              },
              destination: {
                city: "Destination I1",
                code: "DI1",
              },
              departure: {
                scheduled_time: "2025-09-05T14:06:00+1130",
                estimated_time: "2025-09-05T15:06:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:36:00Z",
                estimated_time: "2025-09-05T19:36:00Z",
              },
              status: "En Route",
              progress_percent: 0,
              url: "https://example.com/I1",
            },
            {
              flight_number: "I2",
              airline: {
                name: "In flight Up Air",
                code: "IUP",
                icon: "chrome://browser/skin/urlbar/market-up.svg",
                color: null,
              },
              origin: {
                city: "Origin I2",
                code: "OI2",
              },
              destination: {
                city: "Destination I2",
                code: "DI2",
              },
              departure: {
                scheduled_time: "2025-09-05T14:07:00+1130",
                estimated_time: "2025-09-05T15:07:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:37:00Z",
                estimated_time: "2025-09-05T19:37:00Z",
              },
              status: "En Route",
              progress_percent: 19,
              url: "https://example.com/I2",
            },
            {
              flight_number: "I3",
              airline: {
                name: "In flight Unchanged Air",
                code: "IUN",
                icon: "chrome://browser/skin/urlbar/market-unchanged.svg",
                color: "#0f0",
              },
              origin: {
                city: "Origin I3",
                code: "OI3",
              },
              destination: {
                city: "Destination I3",
                code: "DI3",
              },
              departure: {
                scheduled_time: "2025-09-05T14:08:00+1130",
                estimated_time: "2025-09-05T15:08:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:38:00Z",
                estimated_time: "2025-09-05T19:38:00Z",
              },
              status: "En Route",
              progress_percent: 20,
              url: "https://example.com/I3",
            },
            {
              flight_number: "I4",
              airline: {
                name: "In flight No Color Air",
                code: "INC",
                icon: "chrome://browser/skin/urlbar/flight-airline.svg",
                color: null,
              },
              origin: {
                city: "Origin I4",
                code: "OI4",
              },
              destination: {
                city: "Destination I4",
                code: "DI4",
              },
              departure: {
                scheduled_time: "2025-09-05T14:09:00+1130",
                estimated_time: "2025-09-05T15:09:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:39:00Z",
                estimated_time: "2025-09-05T19:39:00Z",
              },
              status: "En Route",
              progress_percent: 39,
              url: "https://example.com/I4",
            },
            {
              flight_number: "I5",
              airline: {
                name: null,
                code: null,
                icon: null,
                color: "#00f",
              },
              origin: {
                city: "Origin I5",
                code: "OI5",
              },
              destination: {
                city: "Destination I5",
                code: "DI5",
              },
              departure: {
                scheduled_time: "2025-09-05T14:10:00+1130",
                estimated_time: "2025-09-05T15:10:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:40:00Z",
                estimated_time: "2025-09-05T19:40:00Z",
              },
              status: "En Route",
              progress_percent: 40,
              url: "https://example.com/I5",
            },
            {
              flight_number: "I6",
              airline: {
                name: null,
                code: null,
                icon: null,
                color: null,
              },
              origin: {
                city: "Origin I6",
                code: "OI6",
              },
              destination: {
                city: "Destination I6",
                code: "DI6",
              },
              departure: {
                scheduled_time: "2025-09-05T14:11:00+1130",
                estimated_time: "2025-09-05T15:11:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:41:00Z",
                estimated_time: "2025-09-05T19:41:00Z",
              },
              status: "En Route",
              progress_percent: 59,
              url: "https://example.com/I6",
            },
            {
              flight_number: "I7",
              airline: {
                name: null,
                code: null,
                icon: null,
                color: "#ff0",
              },
              origin: {
                city: "Origin I7",
                code: "OI7",
              },
              destination: {
                city: "Destination I7",
                code: "DI7",
              },
              departure: {
                scheduled_time: "2025-09-05T14:12:00+1130",
                estimated_time: "2025-09-05T15:12:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:42:00Z",
                estimated_time: "2025-09-05T19:42:00Z",
              },
              status: "En Route",
              progress_percent: 60,
              url: "https://example.com/I7",
            },
            {
              flight_number: "I8",
              airline: {
                name: null,
                code: null,
                icon: null,
                color: null,
              },
              origin: {
                city: "Origin I8",
                code: "OI8",
              },
              destination: {
                city: "Destination I8",
                code: "DI8",
              },
              departure: {
                scheduled_time: "2025-09-05T14:13:00+1130",
                estimated_time: "2025-09-05T15:13:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:43:00Z",
                estimated_time: "2025-09-05T19:43:00Z",
              },
              status: "En Route",
              progress_percent: 79,
              url: "https://example.com/I8",
            },
            {
              flight_number: "I9",
              airline: {
                name: null,
                code: null,
                icon: null,
                color: "#0ff",
              },
              origin: {
                city: "Origin I9",
                code: "OI9",
              },
              destination: {
                city: "Destination I9",
                code: "DI9",
              },
              departure: {
                scheduled_time: "2025-09-05T14:14:00+1130",
                estimated_time: "2025-09-05T15:14:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:44:00Z",
                estimated_time: "2025-09-05T19:44:00Z",
              },
              status: "En Route",
              progress_percent: 80,
              url: "https://example.com/I9",
            },
            {
              flight_number: "I10",
              airline: {
                name: null,
                code: null,
                icon: null,
                color: null,
              },
              origin: {
                city: "Origin I10",
                code: "OI10",
              },
              destination: {
                city: "Destination I10",
                code: "DI10",
              },
              departure: {
                scheduled_time: "2025-09-05T14:15:00+1130",
                estimated_time: "2025-09-05T15:15:00-1130",
              },
              arrival: {
                scheduled_time: "2025-09-05T18:45:00Z",
                estimated_time: "2025-09-05T19:45:00Z",
              },
              status: "En Route",
              progress_percent: 100,
              url: "https://example.com/I10",
            },
          ],
        },
      },
    },
  ];

  const expectedList = [
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-0.svg",
        fill: "rgb(255, 0, 0)",
        stroke: "rgb(255, 0, 0)",
      },
      foreground: {
        image: "chrome://browser/skin/urlbar/flight-airline.svg",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-0.svg",
      },
      foreground: {
        image: "chrome://browser/skin/urlbar/market-up.svg",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-1.svg",
        fill: "rgb(0, 255, 0)",
        stroke: "rgb(0, 255, 0)",
      },
      foreground: {
        image: "chrome://browser/skin/urlbar/market-unchanged.svg",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-1.svg",
      },
      foreground: {
        image: "chrome://browser/skin/urlbar/flight-airline.svg",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-2.svg",
        fill: "rgb(0, 0, 255)",
        stroke: "rgb(0, 0, 255)",
      },
      foreground: {
        image: "",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-2.svg",
      },
      foreground: {
        image: "",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-3.svg",
        fill: "rgb(255, 255, 0)",
        stroke: "rgb(255, 255, 0)",
      },
      foreground: {
        image: "",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-3.svg",
      },
      foreground: {
        image: "",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-4.svg",
        fill: "rgb(0, 255, 255)",
        stroke: "rgb(0, 255, 255)",
      },
      foreground: {
        image: "",
      },
    },
    {
      background: {
        image: "chrome://browser/skin/urlbar/flight-inflight-progress-4.svg",
      },
      foreground: {
        image: "",
      },
    },
  ];

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "only match the Merino suggestion",
  });
  let { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

  let items = element.row.querySelectorAll(".urlbarView-realtime-item");
  Assert.equal(items.length, expectedList.length);

  for (let i = 0; i < items.length; i++) {
    info(`Check the item[${i}]`);
    let item = items[i];
    let expected = expectedList[i];

    await TestUtils.waitForCondition(
      () => item.querySelector(`[name=status_${i}]`).textContent == "In flight"
    );
    let backgroundStyle = window.getComputedStyle(
      item.querySelector(".urlbarView-realtime-image-container")
    );
    Assert.equal(
      backgroundStyle.backgroundImage,
      `url("${expected.background.image}")`
    );

    // The fill and stroke uses currentColor as the default.
    let defaultColor = backgroundStyle.color;
    let expectedFillColor = expected.background.fill ?? defaultColor;
    let expectedStrokeColor = expected.background.stroke ?? defaultColor;
    Assert.equal(backgroundStyle.fill, expectedFillColor);
    Assert.equal(backgroundStyle.stroke, expectedStrokeColor);
    Assert.equal(
      item.querySelector(".urlbarView-realtime-image").src,
      expected.foreground.image
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);
  gURLBar.handleRevert();
});

add_task(async function activate_single() {
  MerinoTestUtils.server.response.body.suggestions = TEST_MERINO_SINGLE;

  for (let mouse of [true, false]) {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "only match the Merino suggestion",
    });
    let { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
    let { row } = element;

    let target = TEST_MERINO_SINGLE[0].custom_details.flightaware.values[0];
    let expectedURL = target.url;
    let newTabOpened = BrowserTestUtils.waitForNewTab(gBrowser, expectedURL);

    if (mouse) {
      info("Activate by mouse");
      let root = row.querySelector(".urlbarView-realtime-root");
      EventUtils.synthesizeMouseAtCenter(root, {});
    } else {
      info("Activate by key");
      EventUtils.synthesizeKey("KEY_Tab");
      Assert.equal(
        UrlbarTestUtils.getSelectedRow(window),
        row,
        "The row should be selected"
      );
      EventUtils.synthesizeKey("KEY_Enter");
    }

    let newTab = await newTabOpened;
    Assert.ok(true, `Expected URL is loaded [${expectedURL}]`);

    await UrlbarTestUtils.promisePopupClose(window);
    BrowserTestUtils.removeTab(newTab);
    await PlacesUtils.history.clear();
  }
});

add_task(async function activate_multi() {
  MerinoTestUtils.server.response.body.suggestions = TEST_MERINO_MULTI;

  for (let [
    index,
    value,
  ] of TEST_MERINO_MULTI[0].custom_details.flightaware.values.entries()) {
    info(`Activate item[${index}] by mouse`);
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "only match the Merino suggestion",
    });
    let { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

    let items = element.row.querySelectorAll(".urlbarView-realtime-item");
    let item = items[index];

    let newTabOpened = BrowserTestUtils.waitForNewTab(gBrowser, value.url);
    EventUtils.synthesizeMouseAtCenter(item, {}, item.documentGlobal);

    let newTab = await newTabOpened;
    Assert.ok(true, `Expected URL is loaded [${value.url}]`);
    BrowserTestUtils.removeTab(newTab);
    await PlacesUtils.history.clear();
  }

  for (let [
    index,
    value,
  ] of TEST_MERINO_MULTI[0].custom_details.flightaware.values.entries()) {
    info(`Activate item[${index}] by key`);
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "only match the Merino suggestion",
    });
    let { element } = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);

    for (let i = 0; i < index + 1; i++) {
      EventUtils.synthesizeKey("KEY_Tab");
    }

    let items = element.row.querySelectorAll(".urlbarView-realtime-item");
    let item = items[index];

    let newTabOpened = BrowserTestUtils.waitForNewTab(gBrowser, value.url);
    EventUtils.synthesizeMouseAtCenter(item, {}, item.documentGlobal);
    let newTab = await newTabOpened;
    Assert.ok(true, `Expected URL is loaded [${value.url}]`);
    BrowserTestUtils.removeTab(newTab);
    await PlacesUtils.history.clear();
  }

  await UrlbarTestUtils.promisePopupClose(window);
  gURLBar.handleRevert();
});

// The l10n IDs of a realtime suggestion's result menu commands are derived from
// its realtime type, so the menu can be broken for one type only.
add_task(async function resultMenu() {
  MerinoTestUtils.server.response.body.suggestions = TEST_MERINO_SINGLE;

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
      "urlbar-result-menu-dont-show-flight-status2",
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

async function assertUI({ row, expectedList }) {
  if (expectedList.length > 1) {
    Assert.deepEqual(
      document.l10n.getAttributes(row._content),
      {
        id: "urlbar-result-aria-group-flight-status",
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

  let items = row.querySelectorAll(".urlbarView-realtime-item");
  Assert.equal(items.length, expectedList.length);

  // Select the row, which will select the first item if there are multiple
  // items.
  UrlbarTestUtils.setSelectedRowIndex(window, 1);

  for (let i = 0; i < items.length; i++) {
    info(`Check the item[${i}]`);
    let item = items[i];
    let expected = expectedList[i];

    await TestUtils.waitForCondition(
      () =>
        item.querySelector(`[name=status_${i}]`).textContent == expected.status,
      "Wait until the status text will be applied by Fluent"
    );
    Assert.equal(
      item.querySelector(".urlbarView-realtime-image").src,
      expected.image
    );
    Assert.equal(
      item.querySelector(`[name=departure_time_${i}]`).textContent,
      expected.departure_time
    );
    Assert.equal(
      item.querySelector(`[name=departure_date_${i}]`).textContent,
      expected.departure_date
    );
    Assert.equal(
      item.querySelector(`[name=arrival_time_${i}]`).textContent,
      expected.arrival_time
    );
    Assert.equal(
      item.querySelector(`[name=origin_airport_${i}]`).textContent,
      expected.origin_airport
    );
    Assert.equal(
      item.querySelector(`[name=destination_airport_${i}]`).textContent,
      expected.destination_airport
    );
    Assert.equal(
      item.querySelector(`[name=flight_number_${i}]`).textContent,
      expected.flight_number
    );

    let timeLeftMinutes = item.querySelector(`[name=time_left_${i}]`);
    if (typeof expected.time_left != "undefined") {
      Assert.equal(timeLeftMinutes.textContent, expected.time_left);
    } else {
      Assert.equal(timeLeftMinutes.textContent, "");
      let previousSeparator = timeLeftMinutes.previousElementSibling;
      Assert.ok(BrowserTestUtils.isHidden(previousSeparator));
    }

    Assert.equal(
      gURLBar.value,
      expected.url,
      "Input value should be the expected URL"
    );

    // Select the next item.
    EventUtils.synthesizeKey("KEY_Tab");
  }

  // Clear the selection.
  UrlbarTestUtils.setSelectedRowIndex(window, -1);
}

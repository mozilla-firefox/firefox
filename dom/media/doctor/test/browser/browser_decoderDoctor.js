"use strict";

// 'data' contains the notification data object:
// - data.type must be provided.
// - data.isSolved and data.decoderDoctorReportId will be added if not provided
//   (false and "testReportId" resp.)
// - Other fields (e.g.: data.formats) may be provided as needed.
// 'notificationMessage': Expected message in the notification bar.
//   Falsy if nothing is expected after the notification is sent, in which case
//   we won't have further checks, so the following parameters are not needed.
// 'label': Expected button label. Falsy if no button is expected, in which case
//   we won't have further checks, so the following parameters are not needed.
// 'accessKey': Expected access key for the button.
// 'tabChecker': function(openedTab) called with the opened tab that resulted
//   from clicking the button.
async function test_decoder_doctor_notification(
  data,
  notificationMessage,
  label,
  accessKey,
  isLink,
  tabChecker
) {
  const TEST_URL = "https://example.org";
  // A helper closure to test notifications in same or different origins.
  // 'test_cross_origin' is used to determine if the observers used in the test
  // are notified in the same frame (when false) or in a cross origin iframe
  // (when true).
  async function create_tab_and_test(test_cross_origin) {
    await BrowserTestUtils.withNewTab(
      { gBrowser, url: TEST_URL },
      async function (browser) {
        let awaitNotificationBar;
        if (notificationMessage) {
          awaitNotificationBar = BrowserTestUtils.waitForNotificationBar(
            gBrowser,
            browser,
            "decoder-doctor-notification"
          );
        }

        await SpecialPowers.spawn(
          browser,
          [data, test_cross_origin],
          /* eslint-disable-next-line no-shadow */
          async function (data, test_cross_origin) {
            if (!test_cross_origin) {
              // Notify in the same origin.
              Services.obs.notifyObservers(
                content.window,
                "decoder-doctor-notification",
                JSON.stringify(data)
              );
              return;
              // Done notifying in the same origin.
            }

            // Notify in a different origin.
            const CROSS_ORIGIN_URL = "https://example.com";
            let frame = content.document.createElement("iframe");
            frame.src = CROSS_ORIGIN_URL;
            await new Promise(resolve => {
              frame.addEventListener("load", () => {
                resolve();
              });
              content.document.body.appendChild(frame);
            });

            await content.SpecialPowers.spawn(
              frame,
              [data],
              async function (
                /* eslint-disable-next-line no-shadow */
                data
              ) {
                Services.obs.notifyObservers(
                  content.window,
                  "decoder-doctor-notification",
                  JSON.stringify(data)
                );
              }
            );
            // Done notifying in a different origin.
          }
        );

        if (!notificationMessage) {
          ok(
            true,
            "Tested notifying observers with a nonsensical message, no effects expected"
          );
          return;
        }

        let notification;
        try {
          notification = await awaitNotificationBar;
        } catch (ex) {
          ok(false, ex);
          return;
        }
        ok(notification, "Got decoder-doctor-notification notification");
        if (label?.l10nId) {
          label = await document.l10n.formatValue(label.l10nId);
        }
        if (isLink) {
          let link = notification.supportLinkEls[0];
          if (link) {
            // Seems to be a Windows specific quirk, but without this
            // mutation observer the notification.messageText.textContent
            // will not be updated. This will cause consistent failures
            // on Windows.
            await BrowserTestUtils.waitForMutationCondition(
              link,
              { childList: true },
              () => link.textContent.trim()
            );
          }
        }
        is(
          notification.messageText.textContent.trim(),
          notificationMessage,
          "notification message should match expectation"
        );

        let button = notification.buttonContainer.querySelector("button");
        let link = notification.supportLinkEls[0];
        if (!label) {
          ok(!button, "There should not be a button");
          ok(!link, "There should not be a link");
          return;
        }

        if (isLink) {
          ok(!button, "There should not be a button");
          is(link.textContent, label, `notification link should be '${label}'`);
          ok(
            !link.hasAttribute("accesskey"),
            "notification link should not have accesskey"
          );
        } else {
          ok(!link, "There should not be a link");
          await document.l10n.translateFragment(button);
          is(
            button.getAttribute("label"),
            label,
            `notification button should be '${label}'`
          );
          is(
            button.getAttribute("accesskey"),
            accessKey,
            "notification button should have accesskey"
          );
        }

        if (!tabChecker) {
          ok(false, "Test implementation error: Missing tabChecker");
          return;
        }
        let awaitNewTab = BrowserTestUtils.waitForNewTab(gBrowser);
        if (button) {
          button.click();
        } else {
          link.click();
        }
        let openedTab = await awaitNewTab;
        tabChecker(openedTab);
        BrowserTestUtils.removeTab(openedTab);
      }
    );
  }

  if (typeof data.type === "undefined") {
    ok(false, "Test implementation error: data.type must be provided");
    return;
  }
  data.isSolved = data.isSolved || false;
  if (typeof data.decoderDoctorReportId === "undefined") {
    data.decoderDoctorReportId = "testReportId";
  }

  // Test same origin.
  await create_tab_and_test(false);
  // Test cross origin.
  await create_tab_and_test(true);
}

async function getReportIssueButtonStrings() {
  let [msg] = await document.l10n.formatMessages([
    { id: "decoder-doctor-report-issue-button" },
  ]);
  return Object.fromEntries(msg.attributes.map(a => [a.name, a.value]));
}

function tab_checker_for_sumo(expectedPath) {
  return function (openedTab) {
    let baseURL = Services.urlFormatter.formatURLPref("app.support.baseURL");
    let url = baseURL + expectedPath;
    is(
      openedTab.linkedBrowser.currentURI.spec,
      url,
      `Expected '${url}' in new tab`
    );
  };
}

function tab_checker_for_webcompat(expectedParams) {
  return function (openedTab) {
    let urlString = openedTab.linkedBrowser.currentURI.spec;
    let endpoint = Services.prefs.getStringPref(
      "media.decoder-doctor.new-issue-endpoint",
      ""
    );
    ok(
      urlString.startsWith(endpoint),
      `Expected URL starting with '${endpoint}', got '${urlString}'`
    );
    let params = new URL(urlString).searchParams;
    for (let k in expectedParams) {
      if (!params.has(k)) {
        ok(false, `Expected ${k} in webcompat URL`);
      } else {
        is(
          params.get(k),
          expectedParams[k],
          `Expected ${k}='${expectedParams[k]}' in webcompat URL`
        );
      }
    }
  };
}

add_task(async function test_platform_decoder_not_found() {
  let message = "";
  let decoderDoctorReportId = "";
  let isLinux = AppConstants.platform == "linux";
  if (isLinux) {
    message = await document.l10n.formatValue(
      "decoder-doctor-no-codecs-linux-message"
    );
    decoderDoctorReportId = "MediaPlatformDecoderNotFound";
  } else if (AppConstants.platform == "win") {
    message = await document.l10n.formatValue(
      "decoder-doctor-no-hw-acceleration-message"
    );
    decoderDoctorReportId = "MediaWMFNeeded";
  }

  await test_decoder_doctor_notification(
    {
      type: "platform-decoder-not-found",
      decoderDoctorReportId,
      formats: "testFormat",
    },
    message,
    isLinux ? "" : { l10nId: "decoder-doctor-learn-how-link" },
    "",
    true,
    tab_checker_for_sumo("fix-video-audio-problems-firefox-windows")
  );
});

add_task(async function test_cannot_initialize_pulseaudio() {
  let message = "";
  // This is only sent on Linux.
  if (AppConstants.platform == "linux") {
    message = await document.l10n.formatValue(
      "decoder-doctor-no-pulse-audio-message"
    );
  }

  await test_decoder_doctor_notification(
    { type: "cannot-initialize-pulseaudio", formats: "testFormat" },
    message,
    { l10nId: "decoder-doctor-learn-how-link" },
    "",
    true,
    tab_checker_for_sumo("fix-common-audio-and-video-issues")
  );
});

add_task(async function test_unsupported_libavcodec() {
  let message = "";
  // This is only sent on Linux.
  if (AppConstants.platform == "linux") {
    message = await document.l10n.formatValue(
      "decoder-doctor-unsupported-libavcodec-message"
    );
  }

  await test_decoder_doctor_notification(
    { type: "unsupported-libavcodec", formats: "testFormat" },
    message
  );
});

add_task(async function test_decode_error() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "media.decoder-doctor.new-issue-endpoint",
        "http://example.com/webcompat",
      ],
      ["browser.fixup.fallback-to-https", false],
    ],
  });
  let message = await document.l10n.formatValue(
    "decoder-doctor-decode-error-message"
  );
  let { label, accesskey } = await getReportIssueButtonStrings();
  await test_decoder_doctor_notification(
    {
      type: "decode-error",
      decodeIssue: "DecodeIssue",
      docURL: "DocURL",
      resourceURL: "ResURL",
    },
    message,
    label,
    accesskey,
    false,
    tab_checker_for_webcompat({
      url: "DocURL",
      label: "type-media",
      problem_type: "video_bug",
      details: JSON.stringify({
        "Technical Information:": "DecodeIssue",
        "Resource:": "ResURL",
      }),
    })
  );
});

add_task(async function test_decode_warning() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [
        "media.decoder-doctor.new-issue-endpoint",
        "http://example.com/webcompat",
      ],
    ],
  });
  let message = await document.l10n.formatValue(
    "decoder-doctor-decode-warning-message"
  );
  let { label, accesskey } = await getReportIssueButtonStrings();
  await test_decoder_doctor_notification(
    {
      type: "decode-warning",
      decodeIssue: "DecodeIssue",
      docURL: "DocURL",
      resourceURL: "ResURL",
    },
    message,
    label,
    accesskey,
    false,
    tab_checker_for_webcompat({
      url: "DocURL",
      label: "type-media",
      problem_type: "video_bug",
      details: JSON.stringify({
        "Technical Information:": "DecodeIssue",
        "Resource:": "ResURL",
      }),
    })
  );
});

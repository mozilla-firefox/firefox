const { LoginAutoCompleteResult } = ChromeUtils.importESModule(
  "resource://gre/modules/LoginAutoComplete.sys.mjs"
);
let nsLoginInfo = Components.Constructor(
  "@mozilla.org/login-manager/loginInfo;1",
  Ci.nsILoginInfo,
  "init"
);

const PREF_SCHEME_UPGRADES = "signon.schemeUpgrades";

let matchingLogins = [];
matchingLogins.push(
  new nsLoginInfo(
    "https://mochi.test:8888",
    "https://autocomplete:8888",
    null,
    "",
    "emptypass1",
    "uname",
    "pword"
  )
);

matchingLogins.push(
  new nsLoginInfo(
    "https://mochi.test:8888",
    "https://autocomplete:8888",
    null,
    "tempuser1",
    "temppass1",
    "uname",
    "pword"
  )
);

matchingLogins.push(
  new nsLoginInfo(
    "https://mochi.test:8888",
    "https://autocomplete:8888",
    null,
    "testuser2",
    "testpass2",
    "uname",
    "pword"
  )
);
// subdomain:
matchingLogins.push(
  new nsLoginInfo(
    "https://sub.mochi.test:8888",
    "https://autocomplete:8888",
    null,
    "testuser3",
    "testpass3",
    "uname",
    "pword"
  )
);

// to test signon.schemeUpgrades
matchingLogins.push(
  new nsLoginInfo(
    "http://mochi.test:8888",
    "http://autocomplete:8888",
    null,
    "zzzuser4",
    "zzzpass4",
    "uname",
    "pword"
  )
);

// HTTP auth
matchingLogins.push(
  new nsLoginInfo(
    "https://mochi.test:8888",
    null,
    "My HTTP auth realm",
    "httpuser",
    "httppass"
  )
);

add_setup(async () => {
  // Get a profile so we have storage access and insert the logins to get unique GUIDs.
  do_get_profile();
  matchingLogins = await Services.logins.addLogins(matchingLogins);
});

add_task(async function test_all_patterns() {
  let meta = matchingLogins[0].QueryInterface(Ci.nsILoginMetaInfo);
  let dateAndTimeFormatter = new Services.intl.DateTimeFormat(undefined, {
    dateStyle: "medium",
  });
  let time = dateAndTimeFormatter.format(new Date(meta.timePasswordChanged));
  const LABEL_NO_USERNAME = "No username (" + time + ")";
  const EXACT_ORIGIN_MATCH_COMMENT = "From this website";

  let expectedResults = [
    {
      isSecure: true,
      hasBeenTypePassword: false,
      matchingLogins,
      items: [
        {
          value: "",
          label: LABEL_NO_USERNAME,
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "tempuser1",
          label: "tempuser1",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "testuser2",
          label: "testuser2",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "zzzuser4",
          label: "zzzuser4",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "httpuser",
          label: "httpuser",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888 (My HTTP auth realm)" },
        },
        {
          value: "testuser3",
          label: "testuser3",
          style: "loginWithOrigin",
          comment: { secondary: "sub.mochi.test:8888" },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: {
            formHostname: "mochi.test",
            telemetryEventData: { searchStartTimeMS: 0 },
          },
        },
      ],
    },
    {
      isSecure: false,
      hasBeenTypePassword: false,
      matchingLogins: [],
      items: [
        {
          value: "",
          label:
            "This connection is not secure. Logins entered here could be compromised.",
          style: "insecureWarning",
          comment: {
            fillMessageName:
              "PasswordManager:OpenInsecureFieldWarningLearnMore",
            icon: "chrome://global/skin/icons/security-broken.svg",
          },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: {
            formHostname: "mochi.test",
            telemetryEventData: { searchStartTimeMS: 1 },
          },
        },
      ],
    },
    {
      isSecure: false,
      hasBeenTypePassword: false,
      matchingLogins,
      items: [
        {
          value: "",
          label:
            "This connection is not secure. Logins entered here could be compromised.",
          style: "insecureWarning",
          comment: {
            fillMessageName:
              "PasswordManager:OpenInsecureFieldWarningLearnMore",
            icon: "chrome://global/skin/icons/security-broken.svg",
          },
        },
        {
          value: "",
          label: LABEL_NO_USERNAME,
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "tempuser1",
          label: "tempuser1",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "testuser2",
          label: "testuser2",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "zzzuser4",
          label: "zzzuser4",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "httpuser",
          label: "httpuser",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888 (My HTTP auth realm)" },
        },
        {
          value: "testuser3",
          label: "testuser3",
          style: "loginWithOrigin",
          comment: { secondary: "sub.mochi.test:8888" },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      isSecure: true,
      hasBeenTypePassword: true,
      matchingLogins,
      items: [
        {
          value: "emptypass1",
          label: LABEL_NO_USERNAME,
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "temppass1",
          label: "tempuser1",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "testpass2",
          label: "testuser2",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "zzzpass4",
          label: "zzzuser4",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "httppass",
          label: "httpuser",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888 (My HTTP auth realm)" },
        },
        {
          value: "testpass3",
          label: "testuser3",
          style: "loginWithOrigin",
          comment: { secondary: "sub.mochi.test:8888" },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      isSecure: false,
      hasBeenTypePassword: true,
      matchingLogins,
      items: [
        {
          value: "",
          label:
            "This connection is not secure. Logins entered here could be compromised.",
          style: "insecureWarning",
          comment: {
            fillMessageName:
              "PasswordManager:OpenInsecureFieldWarningLearnMore",
            icon: "chrome://global/skin/icons/security-broken.svg",
          },
        },
        {
          value: "emptypass1",
          label: LABEL_NO_USERNAME,
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "temppass1",
          label: "tempuser1",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "testpass2",
          label: "testuser2",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "zzzpass4",
          label: "zzzuser4",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "httppass",
          label: "httpuser",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888 (My HTTP auth realm)" },
        },
        {
          value: "testpass3",
          label: "testuser3",
          style: "loginWithOrigin",
          comment: { secondary: "sub.mochi.test:8888" },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      isSecure: true,
      hasBeenTypePassword: false,
      matchingLogins,
      items: [
        {
          value: "",
          label: LABEL_NO_USERNAME,
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "tempuser1",
          label: "tempuser1",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "testuser2",
          label: "testuser2",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "zzzuser4",
          label: "zzzuser4",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "httpuser",
          label: "httpuser",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888 (My HTTP auth realm)" },
        },
        {
          value: "testuser3",
          label: "testuser3",
          style: "loginWithOrigin",
          comment: { secondary: "sub.mochi.test:8888" },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      isSecure: false,
      hasBeenTypePassword: false,
      matchingLogins: [],
      searchString: "foo",
      items: [
        {
          value: "",
          label:
            "This connection is not secure. Logins entered here could be compromised.",
          style: "insecureWarning",
          comment: {
            fillMessageName:
              "PasswordManager:OpenInsecureFieldWarningLearnMore",
            icon: "chrome://global/skin/icons/security-broken.svg",
          },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      isSecure: true,
      hasBeenTypePassword: true,
      matchingLogins: [],
      items: [
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      isSecure: true,
      hasBeenTypePassword: true,
      matchingLogins: [],
      searchString: "foo",
      items: [],
    },
    {
      generatedPassword: "9ljgfd4shyktb45",
      isSecure: true,
      hasBeenTypePassword: true,
      matchingLogins: [],
      items: [
        {
          value: "9ljgfd4shyktb45",
          label: "Use a Securely Generated Password",
          style: "generatedPassword",
          comment: {
            generatedPassword: "9ljgfd4shyktb45",
            willAutoSaveGeneratedPassword: false,
          },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      description:
        "willAutoSaveGeneratedPassword should propagate to the comment",
      generatedPassword: "9ljgfd4shyktb45",
      willAutoSaveGeneratedPassword: true,
      isSecure: true,
      hasBeenTypePassword: true,
      matchingLogins: [],
      items: [
        {
          value: "9ljgfd4shyktb45",
          label: "Use a Securely Generated Password",
          style: "generatedPassword",
          comment: {
            generatedPassword: "9ljgfd4shyktb45",
            willAutoSaveGeneratedPassword: true,
          },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      description:
        "If a generated password is passed then show it even if there is a search string. This handles when forcing the generation option from the context menu of a non-empty field",
      generatedPassword: "9ljgfd4shyktb45",
      isSecure: true,
      hasBeenTypePassword: true,
      matchingLogins: [],
      searchString: "9ljgfd4shyktb45",
      items: [
        {
          value: "9ljgfd4shyktb45",
          label: "Use a Securely Generated Password",
          style: "generatedPassword",
          comment: {
            generatedPassword: "9ljgfd4shyktb45",
            willAutoSaveGeneratedPassword: false,
          },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      description: "secure username field on sub.mochi.test",
      formOrigin: "https://sub.mochi.test:8888",
      isSecure: true,
      hasBeenTypePassword: false,
      matchingLogins,
      items: [
        {
          value: "testuser3",
          label: "testuser3",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "",
          label: LABEL_NO_USERNAME,
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "tempuser1",
          label: "tempuser1",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "testuser2",
          label: "testuser2",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "zzzuser4",
          label: "zzzuser4",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "httpuser",
          label: "httpuser",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888 (My HTTP auth realm)" },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      description: "secure password field on sub.mochi.test",
      formOrigin: "https://sub.mochi.test:8888",
      isSecure: true,
      hasBeenTypePassword: true,
      matchingLogins,
      items: [
        {
          value: "testpass3",
          label: "testuser3",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "emptypass1",
          label: LABEL_NO_USERNAME,
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "temppass1",
          label: "tempuser1",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "testpass2",
          label: "testuser2",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "zzzpass4",
          label: "zzzuser4",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "httppass",
          label: "httpuser",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888 (My HTTP auth realm)" },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
    {
      description: "schemeUpgrades: false",
      formOrigin: "https://mochi.test:8888",
      schemeUpgrades: false,
      isSecure: true,
      hasBeenTypePassword: false,
      matchingLogins,
      items: [
        {
          value: "",
          label: LABEL_NO_USERNAME,
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "tempuser1",
          label: "tempuser1",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "testuser2",
          label: "testuser2",
          style: "loginWithOrigin",
          comment: { secondary: EXACT_ORIGIN_MATCH_COMMENT },
        },
        {
          value: "zzzuser4",
          label: "zzzuser4",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888" },
        },
        {
          value: "httpuser",
          label: "httpuser",
          style: "loginWithOrigin",
          comment: { secondary: "mochi.test:8888 (My HTTP auth realm)" },
        },
        {
          value: "testuser3",
          label: "testuser3",
          style: "loginWithOrigin",
          comment: { secondary: "sub.mochi.test:8888" },
        },
        {
          value: "",
          label: "Manage Passwords",
          style: "loginsFooter",
          comment: { formHostname: "mochi.test" },
        },
      ],
    },
  ];

  Services.prefs.setBoolPref("signon.showAutoCompleteFooter", true);

  expectedResults.forEach((pattern, testIndex) => {
    info(`expectedResults[${testIndex}]`);
    info(JSON.stringify(pattern, null, 2));
    Services.prefs.setBoolPref(
      PREF_SCHEME_UPGRADES,
      "schemeUpgrades" in pattern ? pattern.schemeUpgrades : true
    );
    let actual = new LoginAutoCompleteResult(
      pattern.searchString || "",
      pattern.matchingLogins,
      [],
      pattern.formOrigin || "https://mochi.test:8888",
      {
        hostname: "mochi.test",
        generatedPassword: pattern.generatedPassword,
        willAutoSaveGeneratedPassword: !!pattern.willAutoSaveGeneratedPassword,
        isSecure: pattern.isSecure,
        hasBeenTypePassword: pattern.hasBeenTypePassword,
        telemetryEventData: { searchStartTimeMS: testIndex },
      }
    );
    equal(
      actual.matchCount,
      pattern.items.length,
      `${testIndex}: Check matching row count`
    );
    pattern.items.forEach((item, index) => {
      equal(
        actual.getValueAt(index),
        item.value,
        `${testIndex}: Value ${index}`
      );
      equal(
        actual.getLabelAt(index),
        item.label,
        `${testIndex}: Label ${index}`
      );
      equal(
        actual.getStyleAt(index),
        item.style,
        `${testIndex}: Style ${index}`
      );
      let actualComment = actual.getCommentAt(index);
      if (typeof item.comment == "object") {
        let parsedComment = JSON.parse(actualComment);
        for (let [key, val] of Object.entries(item.comment)) {
          Assert.deepEqual(
            parsedComment[key],
            val,
            `${testIndex}: Comment.${key} ${index}`
          );
        }
      } else {
        equal(actualComment, item.comment, `${testIndex}: Comment ${index}`);
      }
    });

    if (pattern.items.length) {
      Assert.throws(
        () => actual.getValueAt(pattern.items.length),
        /Index out of range\./
      );

      Assert.throws(
        () => actual.getLabelAt(pattern.items.length),
        /Index out of range\./
      );
    }
  });
});

add_task(function test_external_autocomplete_items() {
  const genericItem = {
    image: "generic.svg",
    label: "Generic entry",
    secondary: "Secondary text",
    fillMessageName: "PasswordManager:Test",
    fillMessageData: { value: "generic" },
  };
  const smartFormFillComment = {
    type: "smartFormFill",
    sources: [
      { label: "Source tab", favicon: "page-icon:https://example.com" },
    ],
    sourcesLabel: "Sources:",
    ariaLabel: "Smart Form Fill",
    loading: false,
    loadingLabel: "Loading...",
    emptySourcesLabel: null,
    fillMessageName: "SmartFormFill:Start",
    fillMessageData: {},
  };
  const smartFormFillItem = {
    style: "smartFormFill",
    value: "",
    image: "smart-form-fill.svg",
    label: "Smart Form Fill",
    comment: JSON.stringify(smartFormFillComment),
  };

  const result = new LoginAutoCompleteResult(
    "",
    [],
    [genericItem, smartFormFillItem],
    "https://mochi.test:8888",
    {
      hostname: "mochi.test",
      isSecure: true,
      hasBeenTypePassword: false,
      telemetryEventData: {},
    }
  );

  equal(result.matchCount, 3, "External entries and the footer are present");
  equal(result.getStyleAt(0), "generic", "Generic entries are adapted");
  Assert.deepEqual(JSON.parse(result.getCommentAt(0)), {
    secondary: genericItem.secondary,
    fillMessageName: genericItem.fillMessageName,
    fillMessageData: genericItem.fillMessageData,
  });
  equal(
    result.getStyleAt(1),
    "smartFormFill",
    "Smart Form Fill retains its specialized style"
  );
  Assert.deepEqual(
    JSON.parse(result.getCommentAt(1)),
    smartFormFillComment,
    "Smart Form Fill retains its specialized metadata"
  );
  equal(
    result.getStyleAt(2),
    "loginsFooter",
    "External entries appear before the LoginManager footer"
  );
});

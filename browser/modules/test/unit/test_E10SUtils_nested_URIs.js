const { E10SUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/E10SUtils.sys.mjs"
);

var TEST_PREFERRED_REMOTE_TYPES = [
  E10SUtils.WEB_REMOTE_TYPE,
  E10SUtils.NOT_REMOTE,
  "fakeRemoteType",
];

// These test cases give a nestedURL and a plainURL that should always load in
// the same remote type. By making these tests comparisons, they should work
// with any pref combination.
var TEST_CASES = [
  {
    nestedURL: "jar:file:///some.file!/",
    plainURL: "file:///some.file",
  },
  {
    nestedURL: "jar:jar:file:///some.file!/!/",
    plainURL: "file:///some.file",
  },
  {
    nestedURL: "jar:http://some.site/file!/",
    plainURL: "http://some.site/file",
  },
  {
    nestedURL: "view-source:http://some.site",
    plainURL: "http://some.site",
  },
  {
    nestedURL: "view-source:file:///some.file",
    plainURL: "file:///some.file",
  },
  {
    nestedURL: "view-source:about:home",
    plainURL: "about:home",
  },
  {
    nestedURL: "view-source:about:robots",
    plainURL: "about:robots",
  },
  {
    nestedURL: "view-source:pcast:http://some.site",
    plainURL: "http://some.site",
  },
];

function run_test() {
  for (let testCase of TEST_CASES) {
    for (let preferredRemoteType of TEST_PREFERRED_REMOTE_TYPES) {
      let plainUri = Services.io.newURI(testCase.plainURL);
      let plainRemoteType = E10SUtils.getRemoteTypeForURIObject(plainUri, {
        multiProcess: true,
        remoteSubFrames: false,
        preferredRemoteType,
      });

      let nestedUri = Services.io.newURI(testCase.nestedURL);
      let nestedRemoteType = E10SUtils.getRemoteTypeForURIObject(nestedUri, {
        multiProcess: true,
        remoteSubFrames: false,
        preferredRemoteType,
      });

      let nestedStr = nestedUri.scheme + ":";
      do {
        nestedUri = nestedUri.QueryInterface(Ci.nsINestedURI).innerURI;
        if (nestedUri.scheme == "about") {
          nestedStr += nestedUri.spec;
          break;
        }

        nestedStr += nestedUri.scheme + ":";
      } while (nestedUri instanceof Ci.nsINestedURI);

      let plainStr =
        plainUri.scheme == "about" ? plainUri.spec : plainUri.scheme + ":";
      equal(
        nestedRemoteType,
        plainRemoteType,
        `Check that ${nestedStr} loads in same remote type as ${plainStr}` +
          ` with preferred remote type: ${preferredRemoteType}`
      );
    }
  }
}

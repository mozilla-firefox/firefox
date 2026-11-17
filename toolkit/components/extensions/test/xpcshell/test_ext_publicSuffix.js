"use strict";

const server = createHttpServer({ hosts: ["example.com"] });
server.registerDirectory("/data/", do_get_file("data"));

async function testPublicSuffix({ permissions, testBackground, testContent }) {
  async function runTest(testFn, completionMessage) {
    try {
      dump(`Running test before sending ${completionMessage}\n`);
      await testFn();
    } catch (e) {
      browser.test.fail(`Unexpected error: ${e}`);
    }
    browser.test.sendMessage(completionMessage);
  }
  const extension = ExtensionTestUtils.loadExtension({
    background: `(${runTest})(${testBackground}, "background_done");`,
    manifest: {
      content_scripts: [
        {
          run_at: "document_end",
          js: ["test.js"],
          matches: ["http://example.com/data/file_sample.html"],
        },
      ],
      permissions,
    },
    files: {
      "test.js": `(${runTest})(${testContent}, "content_done");`,
    },
  });

  // Run background script.
  await extension.startup();
  await extension.awaitMessage("background_done");

  // Run content script.
  const page = await ExtensionTestUtils.loadContentPage(
    "http://example.com/data/file_sample.html"
  );
  await extension.awaitMessage("content_done");
  await page.close();

  await extension.unload();
}

// Tests publicSuffix API methods: isKnownSuffix(), getKnownSuffix(), getDomain()
// using a range of input hostnames and options.
add_task(async function test_publicSuffix() {
  function testBackground() {
    function testMethod(method, args, description, expect) {
      if (!(expect && typeof expect === "object")) {
        expect = { returns: expect };
      }

      const func = () => browser.publicSuffix[method](...args);
      const argsDescription = args.map(arg => JSON.stringify(arg)).join(", ");
      const fullDescription = `${method}(${argsDescription}) - ${description}`;
      if (expect.throws) {
        browser.test.assertThrows(func, expect.throws, fullDescription);
      } else {
        try {
          browser.test.assertEq(expect.returns, func(), fullDescription);
        } catch (e) {
          browser.test.fail(`Unexpected error: ${e} - ${fullDescription}`);
        }
      }
    }

    function testApi(test) {
      const { hostname, getDomain_options, description, expect } = test;
      testMethod(
        "isKnownSuffix",
        [hostname],
        description,
        expect.isKnownSuffix
      );
      testMethod(
        "getKnownSuffix",
        [hostname],
        description,
        expect.getKnownSuffix
      );
      testMethod(
        "getDomain",
        getDomain_options ? [hostname, getDomain_options] : [hostname],
        description,
        expect.getDomain
      );
    }

    testApi({
      hostname: "example.net",
      description: "eTLD+1",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "net",
        getDomain: "example.net",
      },
    });

    testApi({
      hostname: "www.example.net",
      description: "eTLD+2",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "net",
        getDomain: "example.net",
      },
    });

    testApi({
      hostname: "net",
      description: "is an eTLD itself, single-label",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "net",
        getDomain: null,
      },
    });

    testApi({
      hostname: "net",
      getDomain_options: { allowPlainSuffix: true },
      description: "is an eTLD itself, single-label, using options",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "net",
        getDomain: "net",
      },
    });

    testApi({
      hostname: "co.uk",
      description: "is an eTLD itself, multi-label, ICANN section",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "co.uk",
        getDomain: null,
      },
    });

    testApi({
      hostname: "co.uk",
      getDomain_options: { allowPlainSuffix: true },
      description:
        "is an eTLD itself, multi-label, ICANN section, using options",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "co.uk",
        getDomain: "co.uk",
      },
    });

    testApi({
      hostname: "github.io",
      description: "is an eTLD itself, multi-label, private section",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "github.io",
        getDomain: null,
      },
    });

    testApi({
      hostname: "github.io",
      getDomain_options: { allowPlainSuffix: true },
      description:
        "is an eTLD itself, multi-label, private section, using options",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "github.io",
        getDomain: "github.io",
      },
    });

    testApi({
      hostname: "banana",
      description: "no matching eTLD in PSL, single-label",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: null,
      },
    });

    testApi({
      hostname: "banana",
      getDomain_options: { allowUnknownSuffix: true },
      description: "no matching eTLD in PSL, single-label, using options",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: "banana",
      },
    });

    testApi({
      hostname: "banana",
      getDomain_options: { allowPlainSuffix: true },
      description: "no matching eTLD in PSL, single-label, using options",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        // allowUnknownSuffix defaults to false, so getDomain returns null.
        getDomain: null,
      },
    });

    testApi({
      hostname: "banana",
      getDomain_options: { allowUnknownSuffix: true, allowPlainSuffix: true },
      description: "no matching eTLD in PSL, single-label, using more options",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: "banana",
      },
    });

    testApi({
      hostname: "my.net.banana",
      description: "no matching eTLD in PSL, multi-label",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: null,
      },
    });

    testApi({
      hostname: "my.net.banana",
      getDomain_options: { allowUnknownSuffix: true },
      description: "no matching eTLD in PSL, multi-label, using options",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: "net.banana",
      },
    });

    testApi({
      hostname: "banana.net",
      description: "has an eTLD in the ICANN section",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "net",
        getDomain: "banana.net",
      },
    });

    testApi({
      hostname: "facebook.co.uk",
      description: "has an eTLD in the ICANN section, multi-label suffix",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "co.uk",
        getDomain: "facebook.co.uk",
      },
    });

    testApi({
      hostname: "banana.github.io",
      description: "has an eTLD in the Private section",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "github.io",
        getDomain: "banana.github.io",
      },
    });

    testApi({
      hostname: "127.0.0.1",
      description: "IP address, IPv4",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: null,
      },
    });

    testApi({
      hostname: "127.0.0.1",
      getDomain_options: { allowIPAddress: true },
      description: "IP address, IPv4, using options",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: "127.0.0.1",
      },
    });

    testApi({
      hostname: "[127.0.0.1]",
      getDomain_options: { allowIPAddress: true },
      description: "Invalid IP address, IPv4, using options",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: [127.0.0.1]" },
        getKnownSuffix: { throws: "Invalid hostname: [127.0.0.1]" },
        getDomain: { throws: "Invalid hostname: [127.0.0.1]" },
      },
    });

    testApi({
      hostname: "[::1]",
      description: "IP address, IPv6",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: null,
      },
    });

    testApi({
      hostname: "[::1]",
      getDomain_options: { allowIPAddress: true },
      description: "IP address, IPv6, using options",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: null,
        getDomain: "[::1]",
      },
    });

    testApi({
      hostname: "::1",
      description: "IP address, IPv6, no brackets",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: ::1" },
        getKnownSuffix: { throws: "Invalid hostname: ::1" },
        getDomain: { throws: "Invalid hostname: ::1" },
      },
    });

    testApi({
      hostname: "::1",
      getDomain_options: { allowIPAddress: true },
      description: "IP address, IPv6, no brackets, using options",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: ::1" },
        getKnownSuffix: { throws: "Invalid hostname: ::1" },
        getDomain: { throws: "Invalid hostname: ::1" },
      },
    });

    testApi({
      hostname: "[example.com]",
      description: "domain name in brackets",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: [example.com]" },
        getKnownSuffix: { throws: "Invalid hostname: [example.com]" },
        getDomain: { throws: "Invalid hostname: [example.com]" },
      },
    });

    testApi({
      hostname: "[example.com:80]",
      description: "domain name in brackets, with port",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: [example.com:80]" },
        getKnownSuffix: { throws: "Invalid hostname: [example.com:80]" },
        getDomain: { throws: "Invalid hostname: [example.com:80]" },
      },
    });

    testApi({
      hostname: "EXAMPLE.NET",
      description: "uppercase",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "net",
        getDomain: "example.net",
      },
    });

    testApi({
      hostname: ".example.net",
      description: "dot in front",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: .example.net" },
        getKnownSuffix: { throws: "Invalid hostname: .example.net" },
        getDomain: { throws: "Invalid hostname: .example.net" },
      },
    });

    testApi({
      hostname: "example.net.",
      description: "dot in the end, this is an FQDN",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "net.",
        getDomain: "example.net.",
      },
    });

    testApi({
      hostname: "مليسيا",
      description: "this is an IDN that is also an eTLD",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "xn--mgbx4cd0ab",
        getDomain: null,
      },
    });

    testApi({
      hostname: "xn--mgbx4cd0ab",
      description: "this is an IDN that is also an eTLD, in punycode form",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "xn--mgbx4cd0ab",
        getDomain: null,
      },
    });

    testApi({
      hostname: "foo.مليسيا",
      description: "this is an IDN",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "xn--mgbx4cd0ab",
        getDomain: "foo.xn--mgbx4cd0ab",
      },
    });

    testApi({
      hostname: "foo.مليسيا",
      getDomain_options: { encoding: "display" },
      description: "this is an IDN, using options",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "xn--mgbx4cd0ab",
        getDomain: "foo.مليسيا",
      },
    });

    testApi({
      hostname: "foo.xn--mgbx4cd0ab",
      description: "this is an IDN, but punycode",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "xn--mgbx4cd0ab",
        getDomain: "foo.xn--mgbx4cd0ab",
      },
    });

    testApi({
      hostname: "foo.xn--mgbx4cd0ab",
      getDomain_options: { encoding: "display" },
      description: "this is an IDN, but punycode, using options",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "xn--mgbx4cd0ab",
        getDomain: "foo.مليسيا",
      },
    });

    testApi({
      hostname: "xn--bs-red.com",
      getDomain_options: { encoding: "display" },
      description:
        "this is an IDN, but punycode, with a unicode confusable label",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "com",
        // not converted to unicode due to unicode confusable.
        getDomain: "xn--bs-red.com",
      },
    });

    testApi({
      hostname: "*.com",
      description: "contains invalid character '*', 1-label",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: *.com" },
        getKnownSuffix: { throws: "Invalid hostname: *.com" },
        getDomain: { throws: "Invalid hostname: *.com" },
      },
    });

    testApi({
      hostname: "^.com",
      description: "contains invalid character '^', 1-label",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: ^.com" },
        getKnownSuffix: { throws: "Invalid hostname: ^.com" },
        getDomain: { throws: "Invalid hostname: ^.com" },
      },
    });

    testApi({
      hostname: "*.mydomain.com",
      description: "contains invalid character '*', 2-label",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: *.mydomain.com" },
        getKnownSuffix: { throws: "Invalid hostname: *.mydomain.com" },
        getDomain: { throws: "Invalid hostname: *.mydomain.com" },
      },
    });

    testApi({
      hostname: "^.mydomain.com",
      description: "contains invalid character '^', 2-label",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: ^.mydomain.com" },
        getKnownSuffix: { throws: "Invalid hostname: ^.mydomain.com" },
        getDomain: { throws: "Invalid hostname: ^.mydomain.com" },
      },
    });

    testApi({
      hostname: "",
      description: "empty string",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: " },
        getKnownSuffix: { throws: "Invalid hostname: " },
        getDomain: { throws: "Invalid hostname: " },
      },
    });

    testApi({
      hostname: ".",
      description: "no domain labels",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: ." },
        getKnownSuffix: { throws: "Invalid hostname: ." },
        getDomain: { throws: "Invalid hostname: ." },
      },
    });

    testApi({
      hostname: "example..com",
      description: "contains an empty domain label",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: example..com" },
        getKnownSuffix: { throws: "Invalid hostname: example..com" },
        getDomain: { throws: "Invalid hostname: example..com" },
      },
    });

    testApi({
      hostname: "ck",
      description: "wildcard rule *.ck, is incomplete eTLD",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "ck",
        getDomain: null,
      },
    });

    testApi({
      hostname: "^.ck",
      description: "wildcard rule *.ck should not match invalid domain ^.ck",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: ^.ck" },
        getKnownSuffix: { throws: "Invalid hostname: ^.ck" },
        getDomain: { throws: "Invalid hostname: ^.ck" },
      },
    });

    testApi({
      hostname: "*.ck",
      description: "wildcard rule *.ck should not match invalid domain *.ck",
      expect: {
        isKnownSuffix: { throws: "Invalid hostname: *.ck" },
        getKnownSuffix: { throws: "Invalid hostname: *.ck" },
        getDomain: { throws: "Invalid hostname: *.ck" },
      },
    });

    testApi({
      hostname: "banana.ck",
      description: "wildcard rule *.ck, is eTLD",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "banana.ck",
        getDomain: null,
      },
    });

    testApi({
      hostname: "mydomain.banana.ck",
      description: "wildcard rule *.ck, is eTLD+1",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "banana.ck",
        getDomain: "mydomain.banana.ck",
      },
    });

    testApi({
      hostname: "www.ck",
      description: "exception rule !www.ck",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "ck",
        getDomain: "www.ck",
      },
    });

    testApi({
      hostname: "mydomain.www.ck",
      description: "exception rule !www.ck, subdomain",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "ck",
        getDomain: "www.ck",
      },
    });

    testApi({
      hostname: "compute.amazonaws.com",
      description:
        "wildcard rule *.compute.amazonaws.com, private section, incomplete eTLD",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "compute.amazonaws.com",
        getDomain: null,
      },
    });

    testApi({
      hostname: "banana.compute.amazonaws.com",
      description:
        "wildcard rule *.compute.amazonaws.com, private section, eTLD",
      expect: {
        isKnownSuffix: true,
        getKnownSuffix: "banana.compute.amazonaws.com",
        getDomain: null,
      },
    });

    testApi({
      hostname: "mydomain.banana.compute.amazonaws.com",
      description:
        "wildcard rule *.compute.amazonaws.com, private section, eTLD+1",
      expect: {
        isKnownSuffix: false,
        getKnownSuffix: "banana.compute.amazonaws.com",
        getDomain: "mydomain.banana.compute.amazonaws.com",
      },
    });
  }

  function testContent() {
    browser.test.assertEq(
      undefined,
      browser.publicSuffix,
      "publicSuffix should not be available in a content script"
    );
  }

  await testPublicSuffix({
    permissions: ["publicSuffix"],
    testBackground,
    testContent,
  });
});

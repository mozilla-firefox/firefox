/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { RemoteSettings } = ChromeUtils.importESModule(
  "resource://services-settings/remote-settings.sys.mjs"
);
const { AITabStore } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/AITabStore.sys.mjs"
);
const { HEADING_FONT_RECORD_NAME, resetHeadingFontForTesting } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/ui/modules/AITabHeadingFont.sys.mjs"
  );
const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);
const { AITab } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs"
);
const { createAITab } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs"
);

const AITAB_PREF = "browser.smartwindow.aitab.enabled";
// A minimal TrueType font mapping printable ASCII to a box glyph, so the h1
// renders with it alone. The page registers it under its heading font name.
const TEST_FONT_BASE64 =
  "AAEAAAAKAIAAAwAgT1MvMkXIRHIAAAEoAAAAYGNtYXAAEADTAAABkAAAAChnbHlmW9EkagAA" +
  "AcAAAAAaaGVhZC+UsoQAAACsAAAANmhoZWEFegHEAAAA5AAAACRobXR4AooAAAAAAYgAAAAG" +
  "bG9jYQANAAAAAAG4AAAABm1heHAABAAGAAABCAAAACBuYW1lAAYAAAAAAdwAAAAGcG9zdAAD" +
  "AAAAAAHkAAAAIAABAAAAAQAA8uBh018PPPUAAwPoAAAAAOcTN3QAAAAA5xM3dAAyAAACJgK8" +
  "AAAAAwACAAAAAAAAAAEAAAMg/zgAAAJYADIAMgImAAEAAAAAAAAAAAAAAAAAAAABAAEAAAAC" +
  "AAQAAQAAAAAAAgAAAAAAAAAAAAAAAAAAAAAAAwJYAlgABQAEAAAAAAAAAAAAAAAAAAAAAAAA" +
  "AAAAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAPz8/PwAAACAAfgMg/zgAAAMgAMgAAAAA" +
  "AAAAAAAAAAAAAAAgAAACWAAAADIAAAAAAAEAAwAKAAAADAANAAAAAAAcAAAAAAAAAAEAAAAg" +
  "AAAAfgAAAAEAAAAAAA0AAAABADIAAAImArwAAwAAMxEhETIB9AK8/UQAAAAAAAAAAAYAAAAD" +
  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

const client = RemoteSettings("ai-window-aitab");

let RECORD;
let fontBytes;
let pageSlug;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({ set: [[AITAB_PREF, true]] });
  fontBytes = Uint8Array.fromBase64(TEST_FONT_BASE64).buffer;

  // Remote Settings checks cached files against the record's hash and size.
  const digest = await crypto.subtle.digest("SHA-256", fontBytes);
  RECORD = {
    id: "heading-font-test",
    name: HEADING_FONT_RECORD_NAME,
    last_modified: 1,
    attachment: {
      hash: Array.from(new Uint8Array(digest), b =>
        b.toString(16).padStart(2, "0")
      ).join(""),
      size: fontBytes.byteLength,
      filename: HEADING_FONT_RECORD_NAME,
      location: "main-workspace/ai-window-aitab/test.ttf",
      mimetype: "font/ttf",
    },
  };

  const page = await AITabStore.create({
    convId: "heading-font-conv",
    slug: "heading_font_page",
    title: "Heading font",
    components: {
      metadata: {},
      surface: {
        components: [
          { id: "root", component: "Page", header: "hdr", children: [] },
          { id: "hdr", component: "Header", title: "Heading font check" },
        ],
        dataModel: {},
      },
    },
  });
  pageSlug = page.slug;

  registerCleanupFunction(async () => {
    await AITabStore.deleteBySlug(pageSlug);
    await client.db.clear();
    await client.attachments.deleteDownloaded(RECORD);
    resetHeadingFontForTesting();
  });
});

/**
 * Replaces the collection's records, with or without the font record.
 *
 * @param {boolean} withRecord
 */
async function seedCollection(withRecord) {
  await client.db.importChanges({}, Date.now(), withRecord ? [RECORD] : [], {
    clear: true,
  });
}

/**
 * Opens the test page and reports which font its h1 renders with. The page
 * renders without waiting for the font. This waits for the view event
 * before inspecting the heading.
 *
 * @returns {Promise<object>} faces: families registered on the document,
 *   used: families the h1 actually renders with, fontFamily: its CSS value,
 *   family: the heading font name from aitab-base.css.
 */
async function openPageAndReadHeadingFont() {
  Services.fog.testResetFOG();
  return BrowserTestUtils.withNewTab(
    `about:smartpage?page=${pageSlug}`,
    async browser => {
      await TestUtils.waitForCondition(
        () => Glean.smartWindow.aitabView.testGetValue()?.length,
        "The view is recorded"
      );
      return SpecialPowers.spawn(browser, [], async () => {
        const page = content.document.querySelector("aitab-page");
        await ContentTaskUtils.waitForCondition(
          () => page.wrappedJSObject.status == "ready",
          "The page renders"
        );
        const header = page.shadowRoot.querySelector("aitab-header");
        await ContentTaskUtils.waitForCondition(
          () => header.shadowRoot?.querySelector("h1")?.textContent,
          "The header renders its title"
        );

        const faceFamilies = () =>
          [...content.document.fonts].map(face =>
            face.family.replace(/"/g, "")
          );
        await content.document.fonts.ready;

        const h1 = header.shadowRoot.querySelector("h1");
        const range = content.document.createRange();
        range.selectNodeContents(h1);
        return {
          faces: faceFamilies(),
          family: content
            .getComputedStyle(content.document.documentElement)
            .getPropertyValue("--aitab-heading-font-family")
            .trim()
            .replace(/"/g, ""),
          used: InspectorUtils.getUsedFontFaces(range).map(
            face => face.CSSFamilyName
          ),
          fontFamily: content.getComputedStyle(h1).fontFamily,
        };
      });
    }
  );
}

describe("AI Tab heading font", () => {
  let download;

  beforeEach(async () => {
    resetHeadingFontForTesting();
    await seedCollection(true);
    await client.attachments.deleteDownloaded(RECORD);
    // Stands in for the network fetch; each test decides what it returns.
    download = sinon.stub(client.attachments, "downloadAsBytes");
    download.callsFake(async () => fontBytes);
  });

  afterEach(() => {
    download.restore();
  });

  describe("downloading", () => {
    it("downloads the font and renders the h1 with it", async () => {
      const { faces, used, family } = await openPageAndReadHeadingFont();

      Assert.equal(download.callCount, 1, "The font is downloaded once");
      Assert.ok(
        faces.includes(family),
        "The page registers the downloaded font"
      );
      Assert.deepEqual(
        used,
        [family],
        "The h1 renders with the downloaded font"
      );
    });

    it("renders the page before the font finishes downloading", async () => {
      Services.fog.testResetFOG();
      const { promise, resolve } = Promise.withResolvers();
      download.callsFake(() => promise);

      await BrowserTestUtils.withNewTab(
        `about:smartpage?page=${pageSlug}`,
        async browser => {
          await SpecialPowers.spawn(browser, [], async () => {
            const page = content.document.querySelector("aitab-page");
            await ContentTaskUtils.waitForCondition(
              () => page.wrappedJSObject.status == "ready",
              "The page renders while the font is still downloading"
            );
            const header = page.shadowRoot.querySelector("aitab-header");
            await ContentTaskUtils.waitForCondition(
              () => header.shadowRoot?.querySelector("h1")?.textContent,
              "The header renders its title"
            );
            const family = content
              .getComputedStyle(content.document.documentElement)
              .getPropertyValue("--aitab-heading-font-family")
              .trim()
              .replace(/"/g, "");
            Assert.ok(
              ![...content.document.fonts].some(
                face => face.family.replace(/"/g, "") == family
              ),
              "No heading font is registered yet"
            );
          });

          await TestUtils.waitForCondition(
            () => Glean.smartWindow.aitabView.testGetValue()?.length,
            "The view is recorded while the font is still downloading"
          );
          Assert.equal(
            Glean.smartWindow.aitabView.testGetValue()[0].extra.font,
            "unavailable",
            "The heading font was unavailable when the page rendered"
          );

          resolve(fontBytes);
          await SpecialPowers.spawn(browser, [], async () => {
            const family = content
              .getComputedStyle(content.document.documentElement)
              .getPropertyValue("--aitab-heading-font-family")
              .trim()
              .replace(/"/g, "");
            await ContentTaskUtils.waitForCondition(
              () =>
                [...content.document.fonts].some(
                  face => face.family.replace(/"/g, "") == family
                ),
              "The heading font is installed after the page rendered"
            );
          });
          Assert.equal(
            Glean.smartWindow.aitabView.testGetValue().length,
            1,
            "Installing the font does not record another view"
          );
        }
      );
    });

    it("starts the download when page generation starts", async () => {
      const generate = sinon
        .stub(AITab, "generateAITab")
        .resolves({ error: "test" });
      try {
        await createAITab({}, {});
        await TestUtils.waitForCondition(
          () => download.callCount,
          "The font downloads without a page being opened"
        );
        Assert.equal(download.callCount, 1, "The font is downloaded once");
      } finally {
        generate.restore();
      }
    });

    it("tries again on the next page after a failure", async () => {
      download.callsFake(async () => {
        throw new Error("Simulated network failure");
      });
      const first = await openPageAndReadHeadingFont();
      Assert.ok(
        !first.faces.includes(first.family),
        "The first page has no heading font"
      );

      download.callsFake(async () => fontBytes);
      const second = await openPageAndReadHeadingFont();
      Assert.deepEqual(
        second.used,
        [second.family],
        "A later page gets the font once the download works"
      );
    });
  });

  describe("fallback fonts", () => {
    it("uses fallback fonts when the font record is missing", async () => {
      await seedCollection(false);

      const { faces, used, fontFamily, family } =
        await openPageAndReadHeadingFont();

      Assert.equal(download.callCount, 0, "Nothing is downloaded");
      Assert.ok(!faces.includes(family), "No heading font is registered");
      Assert.ok(used.length, "The h1 still renders with a font");
      Assert.ok(
        fontFamily.includes("sans-serif"),
        "The fallback family is still declared"
      );
    });

    it("uses fallback fonts when the download fails", async () => {
      download.callsFake(async () => {
        throw new Error("Simulated network failure");
      });

      const { faces, family } = await openPageAndReadHeadingFont();

      Assert.greater(download.callCount, 0, "The download was attempted");
      Assert.ok(!faces.includes(family), "No heading font is registered");
    });
  });

  describe("caching", () => {
    it("reuses the download for a second page in the session", async () => {
      await openPageAndReadHeadingFont();
      const second = await openPageAndReadHeadingFont();

      Assert.equal(
        download.callCount,
        1,
        "A second page reuses the first download"
      );
      Assert.deepEqual(
        second.used,
        [second.family],
        "The second page renders with the font"
      );
    });

    it("serves the font from disk after a restart", async () => {
      await openPageAndReadHeadingFont();
      await TestUtils.waitForCondition(
        () => client.attachments.cacheImpl.get(RECORD.id),
        "The download is written to the Remote Settings cache"
      );

      // Simulates a restart: the in-memory copy is gone and the network is down.
      resetHeadingFontForTesting();
      download.resetHistory();
      download.callsFake(async () => {
        throw new Error("Simulated network failure");
      });

      const { used, family } = await openPageAndReadHeadingFont();

      Assert.equal(download.callCount, 0, "The network is not used");
      Assert.deepEqual(
        used,
        [family],
        "The font is served from the disk cache"
      );
    });
  });
});

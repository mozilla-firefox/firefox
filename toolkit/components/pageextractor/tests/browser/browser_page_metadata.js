/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * @import { PageExtractorParent } from "../../PageExtractorParent.sys.mjs"
 */

// Structured data - @type extraction

add_task(async function test_page_metadata_structured_data_single_type() {
  const { actor, cleanup } = await html`
    <script type="application/ld+json">
      { "@context": "https://schema.org", "@type": "Article" }
    </script>
  `;

  const { structuredDataTypes } = await actor.getPageMetadata();

  Assert.deepEqual(
    structuredDataTypes,
    ["Article"],
    "Single @type string is extracted."
  );

  return cleanup();
});

add_task(async function test_page_metadata_structured_data_array_type() {
  const { actor, cleanup } = await html`
    <script type="application/ld+json">
      {
        "@context": "https://schema.org",
        "@type": ["Article", "NewsArticle"]
      }
    </script>
  `;

  const { structuredDataTypes } = await actor.getPageMetadata();

  Assert.deepEqual(
    structuredDataTypes,
    ["Article", "NewsArticle"],
    "@type array is flattened into structuredDataTypes."
  );

  return cleanup();
});

add_task(async function test_page_metadata_structured_data_multiple_scripts() {
  const { actor, cleanup } = await html`
    <script type="application/ld+json">
      { "@context": "https://schema.org", "@type": "WebPage" }
    </script>
    <script type="application/ld+json">
      { "@context": "https://schema.org", "@type": "BreadcrumbList" }
    </script>
  `;

  const { structuredDataTypes } = await actor.getPageMetadata();

  Assert.deepEqual(
    structuredDataTypes,
    ["WebPage", "BreadcrumbList"],
    "Types from multiple JSON-LD scripts are all extracted."
  );

  return cleanup();
});

add_task(async function test_page_metadata_structured_data_deduplication() {
  const { actor, cleanup } = await html`
    <script type="application/ld+json">
      { "@context": "https://schema.org", "@type": "Article" }
    </script>
    <script type="application/ld+json">
      { "@context": "https://schema.org", "@type": "Article" }
    </script>
  `;

  const { structuredDataTypes } = await actor.getPageMetadata();

  Assert.deepEqual(
    structuredDataTypes,
    ["Article"],
    "Duplicate @type values are deduplicated."
  );

  return cleanup();
});

add_task(async function test_page_metadata_structured_data_invalid_json() {
  const { actor, cleanup } = await html`
    <script type="application/ld+json">
      { this is not valid json }
    </script>
    <script type="application/ld+json">
      { "@context": "https://schema.org", "@type": "Product" }
    </script>
  `;

  const { structuredDataTypes } = await actor.getPageMetadata();

  Assert.deepEqual(
    structuredDataTypes,
    ["Product"],
    "Invalid JSON-LD blocks are skipped, valid ones are still extracted."
  );

  return cleanup();
});

add_task(async function test_page_metadata_structured_data_no_json_ld() {
  const { actor, cleanup } = await html`
    <p>A page with no structured data.</p>
  `;

  const { structuredDataTypes } = await actor.getPageMetadata();

  Assert.deepEqual(
    structuredDataTypes,
    [],
    "Empty array returned when no JSON-LD is present."
  );

  return cleanup();
});

// Word count

add_task(async function test_page_metadata_word_count() {
  const { actor, cleanup } = await html` <p>one two three four five</p> `;

  const { wordCount } = await actor.getPageMetadata();

  is(wordCount, 5, "Word count matches the number of words on the page.");

  return cleanup();
});

add_task(async function test_page_metadata_word_count_empty() {
  const { actor, cleanup } = await html``;

  const { wordCount } = await actor.getPageMetadata();

  is(wordCount, 0, "Word count is zero for a page with no text.");

  return cleanup();
});

// Language detection

add_task(async function test_page_metadata_language_valid() {
  const { actor, cleanup } = await html`
    <script>
      document.documentElement.lang = "en-US";
    </script>
  `;

  const { language } = await actor.getPageMetadata();

  is(language, "en-US", "Valid lang attribute is returned as a BCP 47 tag.");

  return cleanup();
});

add_task(async function test_page_metadata_language_invalid() {
  const { actor, cleanup } = await html`
    <script>
      document.documentElement.lang = "!!!invalid!!!";
    </script>
  `;

  const { language } = await actor.getPageMetadata();

  is(language, "", "Invalid lang attribute falls back to empty string.");

  return cleanup();
});

add_task(async function test_page_metadata_language_missing() {
  const { actor, cleanup } = await html`
    <p>No lang attribute on this page.</p>
  `;

  const { language } = await actor.getPageMetadata();

  is(language, "", "Missing lang attribute returns empty string.");

  return cleanup();
});

add_task(async function test_page_metadata_no_document() {
  const { tab, cleanup } = await html`<p>Test</p>`;

  const result = await SpecialPowers.spawn(
    tab.linkedBrowser,
    [],
    async function () {
      const actor = content.windowGlobalChild.getActor("PageExtractor");
      const mockThis = Object.create(actor, {
        browsingContext: { get: () => null },
      });

      try {
        await actor.getPageMetadata.call(mockThis);
        return { rejected: false };
      } catch (e) {
        return { rejected: true, message: e.message };
      }
    }
  );

  Assert.ok(
    result.rejected,
    "getPageMetadata rejects when the document is unavailable."
  );
  Assert.ok(
    result.message.includes("No document available"),
    "The error message is for the missing document case."
  );

  return cleanup();
});

// Reader mode

const READERABLE_ARTICLE = `
  It's interesting that inside of Mozilla most people call mochitests "moh
  kee tests". I believe this is because it is adjacent to the term
  "mocha tests", which is pronounced with the hard k sound. However, the
  testing infrastructure is named after the delicious Japanese treat known
  as mochi. Mochi, pronounced like "moh chee" is a food that is made from
  pounding steamed rice into a soft elastic mass.
`;

add_task(
  async function test_page_metadata_reader_mode_structured_data_is_empty() {
    const { cleanup, getPageExtractor } = await html`
      <script type="application/ld+json">
        { "@context": "https://schema.org", "@type": "Article" }
      </script>
      <article>
        <h1>Etymology of Mochitests</h1>
        <p>${READERABLE_ARTICLE}</p>
      </article>
    `;

    const { structuredDataTypes: before } =
      await getPageExtractor().getPageMetadata();
    Assert.deepEqual(
      before,
      ["Article"],
      "Structured data is present before reader mode."
    );

    await toggleReaderMode();

    const { structuredDataTypes: after } =
      await getPageExtractor().getPageMetadata();
    Assert.deepEqual(after, [], "Structured data is empty in reader mode.");

    return cleanup();
  }
);

add_task(async function test_page_metadata_reader_mode_language() {
  const { cleanup, getPageExtractor } = await html`
    <script>
      document.documentElement.lang = "fr";
    </script>
    <article>
      <h1>Etymology of Mochitests</h1>
      <p>${READERABLE_ARTICLE}</p>
    </article>
  `;

  await toggleReaderMode();

  const { language } = await getPageExtractor().getPageMetadata();
  is(
    language,
    "fr",
    "Language is read from the article lang attribute in reader mode."
  );

  return cleanup();
});

// isReaderable

add_task(async function test_page_metadata_is_readerable_true() {
  const { actor, cleanup } = await html`
    <article>
      <h1>Etymology of Mochitests</h1>
      <p>${READERABLE_ARTICLE}</p>
    </article>
  `;

  const { isReaderable } = await actor.getPageMetadata();

  ok(isReaderable, "A page with article content is readerable.");

  return cleanup();
});

add_task(async function test_page_metadata_is_readerable_false() {
  const { actor, cleanup } = await html`<p>Short page.</p>`;

  const { isReaderable } = await actor.getPageMetadata();

  ok(
    !isReaderable,
    "A minimal page without article content is not readerable."
  );

  return cleanup();
});

add_task(async function test_page_metadata_reader_mode_is_readerable_true() {
  const { cleanup, getPageExtractor } = await html`
    <article>
      <h1>Etymology of Mochitests</h1>
      <p>${READERABLE_ARTICLE}</p>
    </article>
  `;

  await toggleReaderMode();

  const { isReaderable } = await getPageExtractor().getPageMetadata();

  ok(isReaderable, "A page in reader mode returns isReaderable true.");

  return cleanup();
});

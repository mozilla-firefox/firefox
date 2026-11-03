/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export type GetTextOptions = Partial<{
  // The length of extracted text that is sufficient for the purpose.
  // When set, extraction will stop when the text meets or exceeds this length.
  // When unset, the lenghth of the extracted text is unbounded.
  // Link URLs count as a fixed length, so the text can be longer or shorter
  // than this.
  sufficientLength: number;
  // The same as sufficientLength, but for a video transcript that replaces the
  // page content, e.g. on YouTube. Falls back to sufficientLength when unset.
  sufficientTranscriptLength: number;
  // Just include the viewport content.
  justViewport: boolean;
  // Skip canvases smaller than this dimension
  minCanvasSize: number;
  // Max canvases to collect
  maxCanvasCount: number;
  // Enable canvas capture
  includeCanvasSnapshots: boolean;
  // Max width/height for captured canvases
  maxCanvasDimension: number;
  // WebP quality 0-1
  canvasQuality: number;
  // Make two attempts to extract the text content. First prefer reader mode for the
  // content as it will remove boilerplate, but then fall back to the DOMExtractor.
  removeBoilerplate: boolean;
  // A test-only option for forcing this behavior.
  _forceRemoveBoilerplate: boolean;
  // The URL of the page being extracted. Used to apply custom extraction strategies for specific sites.
  sourceUrl: string;
  // Return plain prose instead of the default markdown-annotated text. Anchors
  // keep their text but lose their target, and whitespace within a block
  // collapses to single spaces. Paragraph breaks between blocks are preserved.
  useSimpleText: boolean;
}>;

export type CanvasSnapshot = {
  blob: Blob;
  width: number;
  height: number;
};

export type DOMExtractionResult = {
  text: string;
  links: string[];
  canvases: HTMLCanvasElement[];
  siteStrategy: string | null;
};

export type ExtractionResult = {
  text: string;
  links: string[];
  canvasSnapshots: CanvasSnapshot[];
};

export type ExtractionStrategy = Partial<{
  // Identifies the site-specific strategy ("google-search", "youtube"), or
  // null for the default one.
  name: string | null;
  // A CSS selector for elements to exclude from extraction.
  filterSelector: string;
  // Whether to format blocks that are inside anchors as markdown links.
  formatBlockAnchorsAsMarkdown: boolean;
  // A CSS selector for the blocks to format as markdown links. If not set, all blocks inside anchors will be formatted.
  formatBlockAnchorSelector: string;
}>;

export type PageMetadata = {
  // JSON-LD types as defined by https://schema.org/Thing
  // this is used to understand context about the content, where expected values could be things like:
  // ["Recipe", "NewsArticle"] or ["Book"] or ["Person", "Blog", "Article"]
  structuredDataTypes: string[];
  // word count of all the content on the page
  wordCount: number;
  // lang-tag of the page
  language: string;
  // whether the page is likely readable by reader mode
  isReaderable: boolean;
  // whether the page declares that its content is gated, e.g. behind a paywall
  // or a registration wall. Derived from the schema.org `isAccessibleForFree`
  // markup, so this is only ever true for pages that annotate themselves.
  isGated: boolean;
};

/**
 * Reader mode doesn't provide the types for the result. This a stub for making
 * the results easier to interpret.
 */
export interface ReaderModeDocument {
  title: string;
  byline: null | string;
  dir: "ltr" | "rtl";

  /**
   * The HTML content of the article
   */
  content: string;

  /**
   * The text content of the artice without whitespace collapsing.
   */
  textContent: string;

  /**
   * The text length of textContent.
   */
  length: number;

  /**
   * A shorter excerpt of the article.
   */
  excerpt: string;
  siteName: null | string;
  publishedTime: null | string;
  url: string;

  /**
   * The language tag, e.g. "en".
   */
  detectedLanguage: string;

  /**
   * Minutes at a slower reading pace.
   */
  readingTimeMinsSlow: number;

  /**
   * Minutes at a faster reading pace.
   */
  readingTimeMinsFast: number;
}

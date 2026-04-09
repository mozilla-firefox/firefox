/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export type GetTextOptions = Partial<{
  // The length of extracted text that is sufficient for the purpose.
  // When set, extraction will stop when the text meets or exceeds this length.
  // When unset, the lenghth of the extracted text is unbounded.
  sufficientLength: number;
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
};

export type ExtractionResult = {
  text: string;
  links: string[];
  canvasSnapshots: CanvasSnapshot[];
};

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

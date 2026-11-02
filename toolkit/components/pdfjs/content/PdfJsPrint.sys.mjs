/* Copyright 2026 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { NetUtil } from "resource://gre/modules/NetUtil.sys.mjs";

// Match PDF.js's LINE_FACTOR.
const LINE_HEIGHT = 1.35;
// Cap the generated PDF's storage stream at 100 MiB.
const MAX_PRINT_TO_PDF_SIZE = 100 * 1024 * 1024;
const PDF_HEADER = "%PDF-";
const PDF_EOF_MARKER = "%%EOF";
// Acrobat's EOF search window (PDF 1.7, Appendix H, note 18).
const PDF_EOF_SEARCH_SIZE = 1024;
// nsIStorageStream allocation unit.
const STORAGE_STREAM_SEGMENT_SIZE = 4096;

// Check only the PDF boundary markers.
export function hasPDFHeaderAndEOF(buffer) {
  if (!buffer || buffer.byteLength <= PDF_HEADER.length) {
    return false;
  }
  const bytes = new Uint8Array(buffer);
  const header = String.fromCharCode(...bytes.subarray(0, PDF_HEADER.length));
  if (header !== PDF_HEADER) {
    return false;
  }
  const tail = String.fromCharCode(...bytes.subarray(-PDF_EOF_SEARCH_SIZE));
  return tail.includes(PDF_EOF_MARKER);
}

/**
 * Draw text with LINE_HEIGHT spacing and the requested vertical alignment.
 *
 * @param {CanvasRenderingContext2D} ctx
 * @param {object} entry - a validated text entry.
 * @param {number} y - top of the appearance box, in PDF points.
 */
function drawText(
  ctx,
  { text, color, fontSize, fontFamily, verticalAlign, height },
  y
) {
  ctx.fillStyle = color;
  // Use px because canvas units map to PDF points. Keep a fallback for invalid
  // font-family syntax.
  ctx.font = `${fontSize}px sans-serif`;
  ctx.font = `${fontSize}px ${fontFamily}`;

  const lines = text.split("\n");
  const lineHeight = fontSize * LINE_HEIGHT;
  const blockHeight = lines.length * lineHeight;
  const { fontBoundingBoxAscent, fontBoundingBoxDescent } =
    ctx.measureText(text);
  const baseline =
    (lineHeight - (fontBoundingBoxAscent + fontBoundingBoxDescent)) / 2 +
    fontBoundingBoxAscent;

  let lineTop = verticalAlign === "top" ? y : y + (height - blockHeight) / 2;
  for (const line of lines) {
    ctx.fillText(line, 0, lineTop + baseline);
    lineTop += lineHeight;
  }
}

export const PdfJsPrint = {
  /**
   * Draw one field appearance per PDF page.
   *
   * Round page sizes up to whole points and align each appearance box with
   * the page's bottom-left corner.
   *
   * @param {Document} doc - the viewer's document.
   * @param {Array<object>} entries - validated by PdfStreamConverter:
   *   `{ width, height }` in PDF points plus
   *   `{ text, color, fontSize, fontFamily, verticalAlign }`.
   * @returns {ArrayBuffer|null} PDF bytes, or null if boundary markers are
   *   missing.
   */
  printToPDF(doc, entries) {
    const canvases = entries.map(entry => {
      const canvas = doc.createElement("canvas");
      canvas.width = Math.ceil(entry.width);
      canvas.height = Math.ceil(entry.height);
      const y = canvas.height - entry.height;
      canvas.mozPrintCallback = ({ context }) => {
        drawText(context, entry, y);
      };
      return canvas;
    });

    const stream = Cc["@mozilla.org/storagestream;1"].createInstance(
      Ci.nsIStorageStream
    );
    stream.init(STORAGE_STREAM_SEGMENT_SIZE, MAX_PRINT_TO_PDF_SIZE);
    const outputStream = stream.getOutputStream(0);

    let inputStream;
    try {
      doc.defaultView.HTMLCanvasElement.printToPDF(canvases, outputStream);

      inputStream = stream.newInputStream(0);
      const buffer = NetUtil.readInputStream(inputStream);

      if (!hasPDFHeaderAndEOF(buffer)) {
        console.warn("PdfJsPrint: printing produced no usable PDF.");
        return null;
      }
      return buffer;
    } finally {
      // Also close streams when printing or reading throws.
      outputStream.close();
      inputStream?.close();
    }
  },
};

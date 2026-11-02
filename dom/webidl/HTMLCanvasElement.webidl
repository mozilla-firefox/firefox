/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://www.whatwg.org/specs/web-apps/current-work/#the-canvas-element
 * © Copyright 2004-2011 Apple Computer, Inc., Mozilla Foundation, and
 * Opera Software ASA. You are granted a license to use, reproduce
 * and create derivative works of this document.
 */

interface nsISupports;
interface OutputStream;

typedef (HTMLCanvasElement or OffscreenCanvas) CanvasSource;

[Exposed=Window]
interface HTMLCanvasElement : HTMLElement {
  [HTMLConstructor] constructor();

  [CEReactions, Pure, SetterThrows]
           attribute unsigned long width;
  [CEReactions, Pure, SetterThrows]
           attribute unsigned long height;

  [Throws]
  nsISupports? getContext(DOMString contextId, optional any contextOptions = null);

  [Throws, NeedsSubjectPrincipal]
  DOMString toDataURL(optional DOMString type = "",
                      optional any encoderOptions);
  [Throws, NeedsSubjectPrincipal]
  undefined toBlob(BlobCallback callback,
                   optional DOMString type = "",
                   optional any encoderOptions);
};

// Mozilla specific bits
partial interface HTMLCanvasElement {
  [Pure, SetterThrows]
           attribute boolean mozOpaque;

           attribute PrintCallback? mozPrintCallback;

  [Throws, NeedsSubjectPrincipal]
  CanvasCaptureMediaStream captureStream(optional double frameRate);

  // Write one PDF page per detached canvas, using its dimensions as points.
  // Each chrome mozPrintCallback runs synchronously with the canvas' 2D
  // context targeting the PDF. The existing bitmap and drawing state are reset
  // before the callback, and the bitmap is discarded afterward.
  [ChromeOnly, Throws]
  static undefined printToPDF(sequence<HTMLCanvasElement> canvases,
                              OutputStream stream);
};

// For OffscreenCanvas
// Reference: https://wiki.whatwg.org/wiki/OffscreenCanvas
partial interface HTMLCanvasElement {
  [Throws]
  OffscreenCanvas transferControlToOffscreen();
};

[ChromeOnly,
 Exposed=Window]
interface MozCanvasPrintState
{
  // A canvas rendering context.
  readonly attribute nsISupports context;

  // To be called when rendering to the context is done.
  undefined done();
};

callback PrintCallback = undefined(MozCanvasPrintState ctx);

callback BlobCallback = undefined(Blob? blob);

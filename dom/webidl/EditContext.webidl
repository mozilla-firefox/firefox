/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/edit-context
 */

dictionary EditContextInit {
    DOMString text = "";
    unsigned long selectionStart = 0;
    unsigned long selectionEnd = 0;
};

[Pref="dom.editcontext.enabled", Exposed=Window]
interface EditContext : EventTarget {
    [Throws, UseCounter]
    constructor(optional EditContextInit options = {});

    undefined updateText(unsigned long rangeStart, unsigned long rangeEnd,
        DOMString text);
    undefined updateSelection(unsigned long start, unsigned long end);
    undefined updateControlBounds(DOMRect controlBounds);
    undefined updateSelectionBounds(DOMRect selectionBounds);
    undefined updateCharacterBounds(unsigned long rangeStart, sequence<DOMRect> characterBounds);

    [Pure]
    sequence<HTMLElement> attachedElements();

    [Pure]
    readonly attribute DOMString text;
    [Pure]
    readonly attribute unsigned long selectionStart;
    [Pure]
    readonly attribute unsigned long selectionEnd;
    [Pure]
    readonly attribute unsigned long characterBoundsRangeStart;
    [Pure]
    sequence<DOMRect> characterBounds();

    attribute EventHandler ontextupdate;
    attribute EventHandler ontextformatupdate;
    attribute EventHandler oncharacterboundsupdate;
    attribute EventHandler oncompositionstart;
    attribute EventHandler oncompositionend;
};

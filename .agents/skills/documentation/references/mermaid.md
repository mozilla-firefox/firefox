# Mermaid diagrams

Besides mermaid's own diagram types, a block that starts with `zenuml` draws a
ZenUML sequence diagram. It suits a call flow whose calls nest, such as a method
that calls into other objects before it returns: the source is written like
code, `A->B.method(arg) { B->C.other() return value }`, and a nested call is
drawn inside the activation of the call that made it, where `sequenceDiagram`
lists messages one after another and leaves the nesting to the reader.
`tools/moztreedocs/docs/mermaid-integration.md` has a rendered example.

Every failure below is invisible until the built page is open in a browser:

-   **A diagram renders at its intrinsic size.** One narrower than the column is
    centered in it, caption included, so the `:align:` option is redundant. One
    wider than the column scrolls inside its own box, where an edge fade shows
    that it continues, and its labels keep the size every other diagram's
    have, so width costs the reader a pan but not legibility.
-   **A label holding a long unbroken word renders as an empty box in Firefox**
    (mermaid#5785), which a `wrappingWidth` config block in the diagram's
    frontmatter works around.
-   **A label starting with `1. ` renders as `Unsupported markdown: list`**,
    because mermaid parses labels as markdown. Only a leading `<digit>. `
    triggers it, and a colon in place of the period avoids it: `4: startQuery`
    is fine.
-   **A diagram follows the page's color scheme.** A `classDef` or `style` that
    hardcodes a `fill` keeps that color in both schemes, so it needs an explicit
    `color:` as well, or the theme's label color lands on it and comes out grey on
    a light fill in dark mode. `mermaid-integration.md` has the color rules,
    including what the unstyled default fill means for prose that points at a
    node by color.
-   **A ZenUML diagram is inverted in the dark scheme**, because its plugin
    ignores the theme and draws black on white. Avoid colors a light-to-dark
    inversion would misrepresent.
-   **Do not distinguish two kinds of node by fill color alone**: it fails for
    readers with a color vision deficiency and on poor displays. Vary the shape as
    well -- a stadium `(["text"])` reads clearly against a plain `["text"]`,
    while a rounded rectangle `("text")` is too close to it. `classDef` accepts
    `rx` and `ry` for a radius in between, but only with a unit: `rx:14` is
    silently ignored, `rx:14px` applies.
-   **Directive options have to be contiguous**, immediately under the opening
    fence. A blank line between two of them ends the option block, and the rest
    then render as diagram source.

## Directive form

````
```{mermaid}
:caption: What the diagram shows, as a sentence.

---
config:
  flowchart:
    wrappingWidth: 400
---
%% wrappingWidth works around https://github.com/mermaid-js/mermaid/issues/5785,
%% which makes Firefox drop labels containing long words.
flowchart TD
    ...
```
````

The caption renders as a `<figure>` caption under the diagram, so write it as a
sentence, and a mermaid block replaces a `{figure}` directive one for one.
Inside a list item, indent the whole fence, closing backticks included, to the
list content. A CamelCase identifier of about 25 characters is long enough to
need the `wrappingWidth` block.

## Laying out for the rendered width

Labels render at `mermaid_init_config`'s `fontSize` in `docs/conf.py`, and the
column is about 700px wide in a typical window. Pick the direction the content
reads in, `flowchart LR` and `sequenceDiagram` included, then spend width where
it buys clarity:

-   **Keep a row short, whatever the direction.** Eight `flowchart LR` nodes or
    sequence participants with typical identifiers come out more than twice the
    column's width, so the reader scrolls sideways to follow an arrow from one
    end to the other. Hold a sequence diagram to the participants its lifecycle
    needs.
-   **Don't put siblings in a `subgraph`.** Members with no edges between them
    land on the same rank and each adds its full width. Collapse them into one
    node, a name per `<br/>` line:
    `providers["Providers<br/>ProviderPlaces<br/>ProviderTopSites<br/>..."]`.
-   **Extra lines in a node are free; extra text on the widest line is not.** A
    `<br/>` line adds height only, while shortening the longest line narrows the
    whole diagram. When a box has to say more, stack it.
-   A back edge to a node already drawn costs little width in `flowchart TD`.

## Make it readable in order

A reader follows a pipeline diagram, so the failure is a reader who arrives at a
box and cannot tell what leaves it.

-   **Label every arrow.** An unlabeled hop between two numbered ones breaks the
    chain. Where a hop relays the same message onward, repeat the previous label
    verbatim; reusing a number with a different label is what confuses.
-   **A condition on a node goes in the node, not on the arrow into it.** An edge
    label reads as a condition on the transition, so a qualifier on the arrow
    into a node says that reaching it is conditional. Put the qualifier in the
    source node, in italics under its name.
-   **One number per arrow, and a step that is not a hop gets no number.** Leave
    the gap rather than numbering work a node does internally, since every other
    number labels a line. Two calls on one arrow take two lines,
    `4: isActive<br/>5: startQuery`, not `4, 5: isActive, startQuery`.
-   **The closing edge of a cycle takes no number.** Label it with what travels,
    such as `result rows`.
-   **Step numbers tie the diagram to a numbered list in the prose; method
    names make it self-contained.** Either works, as does `3: startQuery`. Take
    method names from the code.
-   **Edge declaration order decides which side a sibling lands on, unless
    another order crosses fewer edges.** The ELK layout places a node's children
    in the order their edges are declared, and overrides that order where it
    saves a crossing; reordering node declarations does nothing. Declare the
    edges in the order the numbered steps run, so the steps read left to
    right, and check the rendered page.
-   **Dotted arrows (`-. "label" .->`) for the return path** separate requests
    from results at no cost in width.

## Grouping and kinds of node

-   **A `subgraph` around components that share a home** (a process, a layer, a
    side of an actor boundary) usually earns its width, because it answers who
    lives where. Name the group for what its members are, not for a property
    that merely correlates with it; a wrong group label reads as architecture.
-   **Give each kind of node a `classDef`** and assign it with
    `class a,b,c kind;`. Blue against amber survives every common color vision
    deficiency and keeps a lightness gap, where green against red or blue does
    not: `fill:#dbeafe,stroke:#1e40af,color:#1a1a1a` against
    `fill:#fef3c7,stroke:#92400e,color:#1a1a1a`. A dashed stroke,
    `stroke-dasharray:5 5`, is a third axis.
-   **A lone styled node reads as a mistake** beside nodes in the default fill.
    Give the terminal node a neutral gray,
    `fill:#e5e7eb,stroke:#4b5563,color:#1a1a1a`, so the accent is one of three
    kinds.
-   **One node per signal.** Once the coloring would split a box that holds
    several names, split the box; it was also hiding whether its members fire
    in sequence or together.
-   **Name the kinds in one sentence before the first diagram**, by color and
    by a shape a reader can name ("blue pill-shaped boxes are DOM events"),
    rather than spending width on a legend subgraph.

## Label traps

-   **A label is HTML.** `<tab>` renders as nothing at all; write `&lt;tab&gt;`.
    Backticks render literally, so mark an identifier with `<b>` or `<i>`,
    which survive the sanitizer.

## Choosing between layouts

Which layout reads best cannot be decided from the source. Add a temporary
section with the same content laid out three to five ways (top-down against
sequence, numbers against method names, grouped against flat), build once, and
compare them in the browser. Keep the variants otherwise identical, apply any
wording fix to all of them, and delete the section once one is picked.

## Replacing a diagram image

Delete the image in the same commit after searching the docs for its file name
and its directory. A hand-drawn diagram often predates a refactor, so draw what
the surrounding prose and the code say now, and say in the commit message what
changed.

## Verifying what rendered

Serve the page with `./mach doc <path>` rather than opening the built file: on
a `file://` page the extension's module script does not run, so its width
rules are missing and the diagram looks fine at the wrong size. Rendering needs
network access, since mermaid loads from a CDN. With the firefox-devtools MCP,
read each diagram back:

```js
() => [...document.querySelectorAll(".mermaid svg")].map(svg => ({
  viewBox: svg.getAttribute("viewBox"),
  rendered: svg.getBoundingClientRect().width,
  scrolls: svg.parentElement.scrollWidth > svg.parentElement.clientWidth,
  labels: [...svg.querySelectorAll(".nodeLabel, .edgeLabel")].map(e =>
    e.textContent.trim()
  ),
}))
```

-   `rendered` should match the viewBox width. A gap means the diagram is being
    scaled, so one of the sizing pieces described next to `mermaid_width` in
    `docs/conf.py` is missing.
-   `scrolls` should be false, or the pan should be one you chose.
-   No label should be empty or read `Unsupported markdown`.
-   Two edges between the same pair of nodes put their labels a node's width
    apart, so narrowing a box can make them overlap. Compare the labels'
    bounding rectangles; shortening one label is the fix, and the result can lay
    out wider, so measure again.

Navigate the same tab on each rebuild, and return the diagram count with the
labels, so a query that ran against the wrong page shows up as zero diagrams
rather than as a diagram that failed to render.

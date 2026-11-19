(layout-overview)=

# Layout Overview

Last update: May 2024

## Introduction

Much of the layout code deals with operations on the frame tree (or
rendering tree). In the frame tree, each node represents a rectangle
(or, for SVG, other shapes). The frame tree has a shape similar to the
content tree, since many content nodes have one corresponding frame,
though it differs in a few ways: some content nodes have more than one
frame or don't have any frames at all. When elements are `display:none`
in CSS or undisplayed for certain other reasons, they won't have any
frames. When elements are broken across lines or pages, they have
multiple frames; elements may also have multiple frames when multiple
frames nested inside each other are needed to display a single element
(for example, `<table>`, or `<video controls>`).

Each node in the frame tree is an instance of a class derived from
`nsIFrame`. As with the content tree, there is a substantial type
hierarchy, but the type hierarchy is very different: it includes types
like text frames, blocks and inlines, the various parts of tables, flex
and grid containers, and the various types of HTML form controls.

Frames are allocated from a process-wide frame arena. Each
frame is owned by its parent, created in `nsCSSFrameConstructor` and
destroyed via `nsIFrame::Destroy()`. Frames are not reference counted,
and code must not hold on to pointers to frames. To mitigate potential
security bugs when pointers to destroyed frames are accessed, we use
[frame
poisoning](http://robert.ocallahan.org/2010/10/mitigating-dangling-pointer-bugs-using_15.html),
which fills each freed slot with a repeated pointer to inaccessible memory.
The arena allocates each frame class from its own slabs. When a slab becomes
empty, the arena can return its pages to the OS while keeping the address
range reserved for frames of the same class. The memory layout diagram in
{searchfox}`nsPresArena.cpp <layout/base/nsPresArena.cpp>` shows how the
arena finds the slab for a frame pointer.

Like the content tree, frames must be accessed only from the main thread
of their processes.

The frame tree should generally not store any data that cannot be
recomputed on-the-fly. While the frame tree does usually persist while a
page is being displayed, frames are often destroyed and recreated in
response to certain style changes such as changing `display:block` to
`display:flex` on an element.

The rectangle represented by the frame is what CSS calls the element's
border box. See the illustration in [8.1 Box
dimensions](https://www.w3.org/TR/CSS22/box.html#box-dimensions) in CSS2
spec. This is the outside edge of the border (or the inside edge of the
margin). The margin lives outside the border; and the padding lives
inside the border. In addition to `nsIFrame::GetRect()`, we also have
the APIs `nsIFrame::GetPaddingRect()` to get the padding box (the
outside edge of the padding, or inside edge of the border) and
`nsIFrame::GetContentRect()` to get the content box (the outside edge of
the content, or inside edge of the padding). These APIs may produce out
of date results when reflow is needed (or has not yet occurred).

### Ink Overflow vs Scrollable Overflow

In addition to tracking a rectangle, frames also track two overflow
areas: **ink overflow** and **scrollable overflow**. These overflow
areas represent the union of the area needed by the frame and by all its
descendants. The ink overflow is used for painting-related
optimizations: it is a rectangle covering all of the area that might be
painted when the frame and all of its descendants paint. The scrollable
overflow represents the area that the user should be able to scroll to
to see the frame and all of its descendants. In some cases differences
between the frame's rect and its overflow happen because of descendants
that stick out of the frame; in other cases they occur because of some
characteristic of the frame itself. The two overflow areas are similar,
but there are differences: for example, margins are part of scrollable
overflow but not ink overflow, whereas text-shadows are part of ink
overflow but not scrollable overflow.

```{seealso}
- [CSS Overflow Module Level 3](https://drafts.csswg.org/css-overflow-3/)
```

### Brief Intro to Fragmentation (or why we need frame continuations?)

When frames are broken across lines, columns, or pages, we create
multiple frames representing the multiple rectangles of the element. The
first one is called the **primary frame**, and the rest are called its
**continuation frames**, or just **continuations** (which are more
likely to be destroyed and recreated during reflow). These frames are
linked together as continuations: they have a doubly-linked list that
can be used to traverse the continuations using
`nsIFrame::GetPrevContinuation()` and `nsIFrame::GetNextContinuation()`.
(Currently continuations always have the same style data, though we may
at some point want to break that invariant.)

Continuations are sometimes siblings of each other (i.e.
`nsIFrame::GetNextContinuation()` and `nsIFrame::GetNextSibling()` might
return the same frame), and sometimes not. For example, if a paragraph
contains a span which contains a link, and the link is split across
lines, then the continuations of the span are siblings (since they are
both children of the paragraph), but the continuations of the link are
not siblings (since each continuation of the link is descended from a
different continuation of the span). Traversing the entire frame tree
does **not** require explicit traversal of any frames'
continuations-list, since all of the continuations are descendants of
the element containing the break.

We also use continuations for cases (most importantly, bidi reordering,
where left-to-right text and right-to-left text need to be separated
into different continuations since they may not form a contiguous
rectangle) where the continuations should not be rewrapped during
reflow: we call these continuations **fixed** rather than **fluid**.
`nsIFrame::GetNextInFlow()` and `nsIFrame::GetPrevInFlow()` traverse
only the fluid continuations and do not cross fixed continuation
boundaries. We'll explan more on continuations and fragmentation in a
later section [](#layout-fragmentation).

### IB-splitting

If an inline frame has non-inline children, then we split the original
inline frame into parts. The original inline's children are distributed
into these parts like so: The children of the original inline are
grouped into runs of inline and non-inline, and runs of inline get an
inline parent, while runs of non-inline get an anonymous block parent.
We call this "ib-splitting" or "block-inside-inline splitting." This
splitting proceeds recursively up the frame tree until all non-inlines
inside inlines are ancestors of a block frame with anonymous block
wrappers in between. This splitting maintains the relative order between
these child frames, and the relationship between the parts of a split
inline is maintained using an ib-sibling chain. It is important to note
that any wrappers created during frame construction (such as for tables)
might not be included in the ib-sibling chain depending on when this
wrapper creation takes place. See details in
[`nsCSSFrameConstructor::CreateIBSiblings()`](https://searchfox.org/mozilla-central/rev/c34cf367c29601ed56ae4ea51e20b28cd8331f9c/layout/base/nsCSSFrameConstructor.h#1864-1884).

### Physical Coordinates vs Logical Coordinates

In Western scripts, the text flows from left to right, and lines and
block containers progress from top to bottom. To represent a rectangle
in this writing mode, it is natural we choose the origin at the top-left
corner in a space, i.e. `(left,right)` at `(0,0)`. The size of a frame
(rectangle) is specified by its `(width, height)`. This is called the
"physical coordinates."

However, to support various international writing modes on the web, we
need to generalize the concept. For example, in Chinese or Japanese
vertical typesetting, the text can flow from top to bottom while the
lines progress from right to left; in Mongolian script, text can flow
from top to bottom while the lines progress from left to right. We
define "abstract coordinate" or "logical coordinates" to unify the
coordinates under different writing modes. The text flow direction is
defined as "inline direction" and the direction which lines or block
containers stack is defined as "block direction". CSS defines three
properties to determine a writing mode: `writing-mode`, `direction`, and
`text-orientation`.

Nearly all the physical CSS properties have their logical counterparts.
For example, `width` and `height` correspond to `inline-size` and
`block-size`. `left`, `right`, `top`, and `bottom` correspond to
`inline-start`, `inline-end`, `block-start`, and `block-end`.

In layout, we have physical types, such as `nsPoint`, `nsSize`,
`nsRect`, and `nsMargin`; their logical counterparts are `LogicalPoint`,
`LogicalSize`, `LogicalRect`, and `LogicalMargin`. Ideally, we should
all work on logical coordinates, and convert the code that still uses
physical coordinates to logical ones, except when the physical
coordinates might make more sense.

```{seealso}
- [CSS Writing Modes Level 3](https://drafts.csswg.org/css-writing-modes-3/)
- [CSS Logical Properties and Values Level 1](https://drafts.csswg.org/css-logical-1/)
- <https://bugzilla.mozilla.org/show_bug.cgi?id=writing-mode>
```

### References

Code (note that most files in base and generic have useful one line
descriptions at the top that show up when browsing a directory in
searchfox):

- [layout/base/](http://searchfox.org/firefox-main/source/layout/base/)
  contains objects that coordinate everything and a bunch of other
  miscellaneous things
- [layout/generic/](http://searchfox.org/firefox-main/source/layout/generic/)
  contains the basic frame classes as well as support code for their
  reflow methods (`ReflowInput`, `ReflowOutput`, `nsReflowStatus`)
- [layout/forms/](http://searchfox.org/firefox-main/source/layout/forms/)
  contains frame classes for HTML form controls
- [layout/tables/](http://searchfox.org/firefox-main/source/layout/tables/)
  contains frame classes for CSS/HTML tables
- [layout/mathml/](http://searchfox.org/firefox-main/source/layout/mathml/)
  contains frame classes for MathML
- [layout/svg/](http://searchfox.org/firefox-main/source/layout/svg/)
  contains frame classes for SVG
- [layout/xul/](http://searchfox.org/firefox-main/source/layout/xul/)
  contains frame classes for the XUL box model and for various XUL
  widgets

Bugzilla: all of the components whose names begin with "Layout" in the
"Core" product.

Further documentation:

- Talk: [An Overview of Gecko Layout](https://mozilla.hosted.panopto.com/Panopto/Pages/Viewer.aspx?id=34ff8151-353a-40c4-89e3-ac3201363608) (Cameron McCormack :heycam, 2018-06-13)

## Frame Construction

Frame construction is the process of creating frames, which is handled
by
[`nsCSSFrameConstructor`](http://searchfox.org/firefox-main/source/layout/base/nsCSSFrameConstructor.h).
This is done when styles change in ways that require frames to be
created or recreated or when nodes are inserted into the document. The
content tree and the frame tree don't have quite the same shape, and the
frame construction process does some of the work of creating the right
shape for the frame tree. It handles the aspects of creating the right
shape that don't depend on layout information. So for example, frame
construction handles the work needed to implement [table anonymous
objects](https://www.w3.org/TR/CSS22/tables.html#anonymous-boxes) but
does not handle frames that need to be created when an element is broken
across lines or pages.

The basic unit of frame construction is a run of contiguous children of
a single parent element. When asked to construct frames for such a run
of children, the frame constructor first determines, based on the
siblings and parent of the nodes involved, where in the frame tree the
new frames should be inserted. Then the frame constructor walks through
the list of content nodes involved and for each one creates a temporary
data structure called a **frame construction item**, i.e.
[`FrameConstructionItem`](https://searchfox.org/mozilla-central/rev/c34cf367c29601ed56ae4ea51e20b28cd8331f9c/layout/base/nsCSSFrameConstructor.h#1125-1130).
The frame construction item encapsulates various information needed to
create the frames for the content node: its style data, some metadata
about how one would create a frame for this node based on its namespace,
tag name, and styles, and some data about what sort of frame will be
created. This list of frame construction items is then analyzed to see
whether constructing frames based on it and inserting them at the chosen
insertion point will produce a valid frame tree. If it will not, the
frame constructor either fixes up the list of frame construction items
so that the resulting frame tree would be valid or throws away the list
of frame construction items and requests the destruction and re-creation
of the frame for the parent element so that it has a chance to create a
list of frame construction items that it _can_ fix up. The re-creation
for the parent element is called "reframing", which is an expensive
operation, and we'd love to avoid it if possible.

Once the frame constructor has a list of frame construction items and an
insertion point that would lead to a valid frame tree, it goes ahead and
creates frames based on those items. Creation of a non-leaf frame
recursively attempts to create frames for the children of that frame's
element, so in effect frames are created in a depth-first traversal of
the content tree.

The vast majority of the code in the frame constructor, therefore, falls
into one of these categories:

- Code to determine the correct insertion point in the frame tree for
  new frames.
- Code to create, for a given content node, frame construction items.
  This involves some searches through static data tables for metadata
  about the frame to be created.
- Code to analyze the list of frame construction items.
- Code to fix up the list of frame construction items.
- Code to create frames from frame construction items.

## Reflow

Reflow is the process of computing the positions and sizes of frames.
(After all, frames represent rectangles, and at some point we need to
figure out exactly **what** rectangle.) Reflow is done recursively, with
each frame's `Reflow()` method calling the `Reflow()` methods on that
frame's descendants.

In many cases, the correct results are defined by CSS specifications
(particularly [CSS 2.2](https://www.w3.org/TR/CSS22/visudet.html)). In
some cases, the details are not defined by CSS, though in some (but not
all) of those cases we are constrained by Web compatibility. When the
details are defined by CSS, however, the code to compute the layout is
generally structured somewhat differently from the way it is described
in the CSS specifications, since the CSS specifications are generally
written in terms of constraints, whereas our layout code consists of
algorithms optimized for incremental recomputation.

### Where does reflow start? How do we avoid reflowing the world every time?

The reflow generally starts from the root of the frame tree, though some
other types of frame can act as "reflow roots" and start a reflow from
them (`nsTextControlFrame` is one example; see the
[`NS_FRAME_REFLOW_ROOT`](https://searchfox.org/mozilla-central/search?q=symbol:E_%3CT_nsFrameState%3E_NS_FRAME_REFLOW_ROOT&redirect=true)
frame state bit). Reflow roots must obey the invariant that a change
inside one of their descendants never changes their rect or overflow
areas (though currently scrollbars are reflow roots but don't quite obey
this invariant).

In many cases, we want to reflow a part of the frame tree, and we want
this reflow to be efficient. For example, when content is added or
removed from the document tree or when styles change, we want the amount
of work we need to redo to be proportional to the amount of content. We
also want to efficiently handle a series of changes to the same content.
To do this, we maintain two bits on frames:
[`NS_FRAME_IS_DIRTY`](https://searchfox.org/mozilla-central/rev/dbd654fa899a56a6a2e92f325c4608020e80afae/layout/generic/nsFrameStateBits.h#113-120)
indicates that a frame and all of its descendants require reflow.
[`NS_FRAME_HAS_DIRTY_CHILDREN`](https://searchfox.org/mozilla-central/rev/dbd654fa899a56a6a2e92f325c4608020e80afae/layout/generic/nsFrameStateBits.h#127-144)
indicates that a frame has a descendant that is dirty or has had a
descendant removed (see its comment for details). These bits allow
coalescing of multiple updates; this coalescing is done in `PresShell`,
which tracks the set of reflow roots that require reflow. The bits are
set during calls to
[`PresShell::FrameNeedsReflow`](https://searchfox.org/mozilla-central/rev/dbd654fa899a56a6a2e92f325c4608020e80afae/layout/base/PresShell.h#1483-1496)
and are cleared during reflow.

### Reflow Contract

The layout algorithms used by many of the frame classes are those
specified in CSS, which are based on the traditional document formatting
model, where inline sizes (widths) are input and block sizes (heights)
are output.

When an individual frame's `Reflow()` method is called, most of the
input is provided in `ReflowInput`, which is setup by the parent frame.
The output is filled in into `ReflowOutput` and `nsReflowStatus`. After
reflow, the caller (usually the parent) is responsible for setting the
frame's size based on the metrics reported in `ReflowOutput`. The caller
is also responsible to create a continuation based on the completion
status reported in `nsReflowStatus`. We will cover more on
`nsReflowStatus` in a later section in [](#reflow-status).

### Compute intrinsic sizes

In some cases, inline sizes need to be determined based on the content.
For example, an element with `width:min-content` or `width:max-content`.
This depends on two **intrinsic inline sizes**: the minimum intrinsic
inline size (see
[`nsIFrame::GetMinISize()`](https://searchfox.org/mozilla-central/search?q=nsIFrame%3A%3AGetMinISize&path=))
and the preferred intrinsic inline size (see
[`nsIFrame::GetPrefISize()`](https://searchfox.org/mozilla-central/search?q=nsIFrame%3A%3AGetPrefISize&path=)).
The concept of what these inline sizes represent is best explained by
describing what they are on a paragraph containing only text: in such a
paragraph the minimum intrinsic inline size is the inline size of the
longest word, and the preferred intrinsic inline size is the inline size
of the entire paragraph laid out on one line.

Intrinsic inline sizes are invalidated separately from the dirty bits
described above. When a caller informs the pres shell that a frame needs
reflow via `PresShell::FrameNeedsReflow()`, it passes one of the three
options:

- `None` indicates that no intrinsic inline sizes are dirty
- `FrameAndAncestors` indicates that intrinsic inline sizes on it and
  its ancestors are dirty (which happens, for example, if new children
  are added to it)
- `FrameAncestorsAndDescendants` indicates that intrinsic inline sizes
  on it, its ancestors, and its descendants are dirty (for example, if
  the font-size changes)

## Painting

See [](#rendering-overview).

(layout-fragmentation)=

## Fragmentation

Fragmentation (or pagination) is a concept used in printing,
print-preview, and multicolumn layout.

### Continuations in the Frame Tree

To render a DOM node, represented as `nsIContent` object, Gecko creates
zero or more frames (`nsIFrame` objects). Each frame represents a
rectangular area usually corresponding to the node's CSS box as
described by the CSS specs. Simple elements are often representable with
exactly one frame, but sometimes an element needs to be represented with
more than one frame. For example, text breaking across lines:

```
  xxxxxx AAAA
  AAA xxxxxxx
```

The A element is a single DOM node but obviously a single rectangular
frame isn't going to represent its layout precisely.

Similarly, consider text breaking across pages:

```
  | BBBBBBBBBB |
  | BBBBBBBBBB |
  +------------+
```

```
  +------------+
  | BBBBBBBBBB |
  | BBBBBBBBBB |
  |            |
```

Again, a single rectangular frame cannot represent the layout of the
node. A multi-column container with multiple columns is similar.

Another case where a single DOM node is represented by multiple frames
is when a text node contains bidirectional text (e.g. both Hebrew and
English text). In this case, the text node and its inline ancestors are
split so that each frame contains only unidirectional text.

The first frame for an element is called the **primary frame**. The
other frames are called **continuation frames**. Primary frames are
created by `nsCSSFrameConstructor` in response to content insertion
notifications. Continuation frames are created during bidi resolution,
and during reflow, when reflow detects that a content element cannot be
fully laid out within the constraints assigned (e.g., when inline text
will not fit within a particular width constraint, or when a block
cannot be laid out within a particular height constraint).

Continuation frames created during reflow are called "fluid"
continuations (or "in-flows"). Other continuation frames (currently,
those created during bidi resolution), are, in contrast, "non-fluid".
The `NS_FRAME_IS_FLUID_CONTINUATION` state bit indicates whether a
continuation frame is fluid or not.

The frames for an element are put in a doubly-linked list. The links are
accessible via `nsIFrame::GetNextContinuation()` and
`nsIFrame::GetPrevContinuation()`. If only fluid continuations are to be
accessed, `nsIFrame::GetNextInFlow()` and `nsIFrame::GetPrevInFlow()`
are used instead.

The following diagram shows the relationship between the original frame
tree considering just primary frames, and a possible layout with
breaking and continuations:

```
Original frame tree       Frame tree with A broken into three parts
    Root                      Root
     |                      /  |  \
     A                     A1  A2  A3
    / \                   / |  |    |
   B   C                 B  C1 C2   C3
   |  /|\                |  |  | \   |
   D E F G               D  E  F G1  G2
```

Certain kinds of frames create multiple child frames for the same
content element:

- `nsPageSequenceFrame` creates multiple page children, each one
  associated with the entire document, separated by page breaks
- `nsColumnSetFrame` creates multiple block children, each one
  associated with the column element, separated by column breaks
- `nsBlockFrame` creates multiple inline children, each one associated
  with the same inline element, separated by line breaks, or by
  changes in text direction
- `nsTableColFrame` creates non-fluid continuations for itself if it
  has span="N" and N > 1
- If a block frame is a multi-column container and has
  `column-span:all` children, it creates multiple `nsColumnSetFrame`
  children, which are linked together as non-fluid continuations.
  Similarly, if a block frame is within a multi-column formatting
  context and has `column-span:all` children, it is chopped into
  several flows, which are linked together as non-fluid continuations
  as well. See documentation and example frame trees in
  [`nsCSSFrameConstructor::ConstructBlock()`](https://searchfox.org/mozilla-central/rev/7f8450b7a32bfbafa060c184d3a3ac9c197e814f/layout/base/nsCSSFrameConstructor.cpp#10426-10491).

```{seealso}
- [CSS Fragmentation Module Level 3: Breaking the Web, one fragment at a time](https://drafts.csswg.org/css-break-3/)
```

#### Overflow Container Continuations

Sometimes the content of a frame needs to break across pages even though
the frame itself is complete. This usually happens if an element with
fixed block size has overflow that doesn't fit on one page. In this case,
the completed frame is "overflow incomplete", and special
continuations are created to hold its overflow. These continuations are
called "overflow containers". They are invisible, and are kept on a
special list in their parent. See documentation in
[nsContainerFrame.h](https://searchfox.org/mozilla-central/rev/7f8450b7a32bfbafa060c184d3a3ac9c197e814f/layout/generic/nsContainerFrame.h#294-342)
and example trees in [bug 379349 comment
3](https://bugzilla.mozilla.org/show_bug.cgi?id=379349#c3).

This infrastructure was extended in [bug
154892](https://bugzilla.mozilla.org/show_bug.cgi?id=154892) to also
manage continuations for absolutely-positioned frames.

#### Relationship of continuations to frame tree structure

It is worth emphasizing two points about the relationship of the
prev-continuation / next-continuation linkage to the existing frame tree
structure.

First, if you want to traverse the frame tree or a subtree thereof to
examine all the frames once, you do _not_ want to traverse
next-continuation links. All continuations are reachable by traversing
the `GetNextSibling()` links from the result of `GetFirstChild()` for
all child lists.

Second, the following property holds: consider two frames F1 and F2
where F1's next-continuation is F2 and their respective parent frames
are P1 and P2. Then either P1's next continuation is P2, or P1 == P2,
because P is responsible for breaking F1 and F2.

In other words, continuations are sometimes siblings of each other, and
sometimes not. If their parent content was broken at the same point,
then they are not siblings, since they are children of different
continuations of the parent. So in the frame tree for the markup

```html
<p>This is <b><i>some<br/>text</i></b>.</p>
```

the two continuations for the `<b>` element are siblings (unless the line
break is also a page break), but the two continuations for the `<i>`
element are not.

There is an exception to that property when F1 is a first-in-flow float
placeholder. In that case F2's parent will be the next-in-flow of F1's
containing block.

### Reflow Status

Reflow status is found in `aStatus` argument of `Reflow()`.
`IsComplete()` means that we reflowed all the content and no more
next-in-flows are needed. At that point there may still be next in
flows, but the parent will delete them. `IsIncomplete()` means "some
content did not fit in this frame". `IsOverflowIncomplete()` means that
the frame is itself complete, but some of its content didn't fit: this
triggers the creation of overflow containers for the frame's
continuations. `IsIncomplete()` and `NextInFlowNeedsReflow()` means
"some content did not fit in this frame AND it must be reflowed". These
values are defined and documented in
[`nsReflowStatus::Completion`](https://searchfox.org/mozilla-central/rev/7f8450b7a32bfbafa060c184d3a3ac9c197e814f/layout/generic/nsIFrame.h#228-250).

### Dynamic Reflow Considerations

When we reflow a frame F with fluid continuations, two things can
happen:

1. Some child frames do not fit in the passed-in inline size or block
   size constraint. These frames must be "pushed" to F's next-in-flow.
   If F has no next-in-flow, we must create one under F's parent's
   next-in-flow --- or if F's parent is managing the breaking of F, then
   we create F's next in flow directly under F's parent. If F is a
   block, it pushes overflowing child frames to its "overflow" child
   list and forces F's next in flow to be reflowed. When we reflow a
   block, we pull the child frames from the prev-in-flow's overflow
   list into the current frame.
2. All child frames fit in the passed-in inline size or block size
   constraint. Then child frames must be "pulled" from F's next-in-flow
   to fill in the available space. If F's next-in-flow becomes empty, we
   may be able to delete it.

In both of these situations we might end up with a frame F containing
two child frames, one of which is a continuation of the other. This is
incorrect. We might also create holes, where there are frames P1 P2 and
P3, P1 has child F1 and P3 has child F2, but P2 has no F child.

A strategy for avoiding these issues is this: When pulling a frame F2
from parent P2 to prev-in-flow P1, if F2 is a breakable container, then:

- If F2 has no prev-in-flow F1 in P1, then create a new primary frame F1
  in P1 for F2's content, with F2 as its next-in-flow.
- Pull children from F2 to F1 until F2 is empty or we run out of space.
  If F2 goes empty, pull from the next non-empty next-in-flow. Empty
  continuations with no next-in-flows can be deleted.

When pushing a frame F1 from parent P1 to P2, where F1 has a
next-in-flow F2 (which must be a child of P2):

- Merge F2 into F1 by moving all F2's children into F1, then deleting F2

For inline frames F, we have our own custom strategy that coalesces
adjacent inline frames. This need not change.

We do need to implement this strategy when F is a normal in-flow block,
a floating block, and eventually an absolutely positioned block.

# SavedFrame

A `SavedFrame` instance is a singly linked list of stack frames. It represents a
JavaScript call stack at a past moment of execution. Younger frames hold a
reference to the frames that invoked them. The older tails are shared across
many younger frames.

`SavedFrame` stacks should generally be captured, allocated, and live within the
compartment that is being observed or debugged. Usually this is a content
compartment.

## Capturing `SavedFrame` Stacks

### From C++

Use `JS::CaptureCurrentStack` declared in `jsapi.h`.

### From JS

Use `saveStack`, accessible via `Components.utils.getJSTestingFunction()`.

## Including and Excluding Chrome Frames

Consider the following `SavedFrame` stack. Arrows represent links from child to
parent frame, `content.js` is from a compartment with content principals, and
`chrome.js` is from a compartment with chrome principals.

```text
function A from content.js
            |
            V
function B from chrome.js
            |
            V
function C from content.js
```
The content compartment will ever have one view of this stack: `A -> C`.

However, a chrome compartment has a choice: it can either take the same view
that the content compartment has (`A -> C`), or it can view all stack frames,
including the frames from chrome compartments (`A -> B -> C`). To view
everything, use an `XrayWrapper`. This is the default wrapper. To see the stack
as the content compartment sees it, waive the xray wrapper with
`Components.utils.waiveXrays`:

    const contentViewOfStack = Components.utils.waiveXrays(someStack);

## Accessor Properties of the `SavedFrame.prototype` Object

### `source`
The source URL for this stack frame, as a string.

### `sourceId`
The process-unique internal integer ID of this source. Usable to match up
a SavedFrame with a [Debugger.Source][dbg-source] using its `id` property.

### `line`
The line number for this stack frame.

### `column`
The column number for this stack frame.

### `functionDisplayName`
Either SpiderMonkey's inferred name for this stack frame's function, or
    `null`.

### `asyncCause`
If this stack frame is the `asyncParent` of other stack frames, then this is
a string representing the type of asynchronous call by which this frame
invoked its children. For example, if this frame's children are calls to
handlers for a promise this frame created, this frame's `asyncCause` would
be `"Promise"`. If the asynchronous call was started in a descendant frame
to which the requester of the property does not have access, this will be
the generic string `"Async"`. If this is not an asynchronous call point,
this will be `null`.

### `asyncParent`
If this stack frame was called as a result of an asynchronous operation, for
example if the function referenced by this frame is a promise handler, this
property points to the stack frame responsible for the asynchronous call,
for example where the promise was created. If the frame responsible for the
call is not accessible to the caller, this points to the youngest accessible
ancestor of the real frame, if any. In all other cases, this is `null`.

### `parent`
This stack frame's caller, or `null` if this is the oldest frame on the
stack. In this case, there might be an `asyncParent` instead.

## Function Properties of the `SavedFrame.prototype` Object

### `toString()`
Return this frame and its parents formatted as a human readable stack trace
string.

[dbg-source]: /devtools-user/debugger-api/debugger.source/index.md

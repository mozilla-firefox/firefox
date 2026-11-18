# Avoiding intermittent tests

Intermittent oranges are test failures which happen intermittently,
in a seemingly random way. Many of such failures could be avoided by
good test writing principles. This page tries to explain some of
those principles for use by people who contribute tests, and also
those who review them for inclusion into the Firefox repository.

They are also called flaky tests in other projects.

A list of patterns which have been known to cause intermittent failures
comes next, with a description of why each one causes test failures, and
how to avoid it.

After writing a successful test case, make sure to run it locally,
preferably in a debug build. Maybe tests depend on the state of another
test or some future test or browser operation to clean up what is left
over. This is a common problem in browser-chrome, here are things to
try:

- debug mode, run test standalone `./mach <test> <path>/<to>/<test>/test.html|js`
- debug mode, run test standalone directory `./mach <test> <path>/<to>/<test>`
- debug mode, run test standalone larger directory `./mach <test> <path>/<to>`

## Accessing DOM elements too soon

`data:` URLs load asynchronously. You should wait for the load event
of an [\<iframe>](https://developer.mozilla.org/docs/Web/HTML/Element/iframe) that is
loading a `data:` URL before trying to access the DOM of the
subdocument.

For example, the following code pattern is bad:

```html
<html>
  <body>
    <iframe id="x" src="data:text/html,<div id='y'>"></iframe>
    <script>
      var elem = document.getElementById("x").
                          contentDocument.
                          getElementById("y"); // might fail
      // ...
    </script>
  </body>
</html>
```

Instead, write the code like this:

```html
<html>
  <body>
    <script>
        function onLoad() {
          var elem = this.contentDocument.
                          getElementById("y");
          // ...
        };
    </script>
   <iframe src="data:text/html,<div id='y'>"
           onload="onLoad()"></iframe>
  </body>
</html>
```

## Using script functions before they're defined

This may be relevant to event handlers, more than anything else. Let's
say that you have an `<iframe>` and you want to
do something after it's been loaded, so you might write code like this:

```html
<iframe src="..." onload="onLoad()"></iframe>
<script>
  function onLoad() { // oops, too late!
    // ...
  }
</script>
```

This is bad, because the
`<iframe>`'s load may be
completed before the script gets parsed, and therefore before the
`onLoad` function comes into existence. This will cause you to miss
the `<iframe>` load, which
may cause your test to time out, for example. The best way to fix this
is to move the function definition before where it's used in the DOM,
like this:

```html
<script>
  function onLoad() {
    // ...
  }
</script>
<iframe src="..." onload="onLoad()"></iframe>
```

## Relying on the order of asynchronous operations

In general, when you have two asynchronous operations, you cannot assume
any order between them. For example, let's say you have two
`<iframe>`'s like this:

```html
<script>
  var f1Doc;
  function f1Loaded() {
    f1Doc = document.getElementById("f1").contentDocument;
  }
  function f2Loaded() {
    var elem = f1Doc.getElementById("foo"); // oops, f1Doc might not be set yet!
  }
</script>
<iframe id="f1" src="..." onload="f1Loaded()"></iframe>
<iframe id="f2" src="..." onload="f2Loaded()"></iframe>
```

This code is implicitly assuming that `f1` will be loaded before
`f2`, but this assumption is incorrect. A simple fix is to just
detect when all of the asynchronous operations have been finished, and
then do what you need to do, like this:

```html
<script>
  var f1Doc, loadCounter = 0;
  function process() {
    var elem = f1Doc.getElementById("foo");
  }
  function f1Loaded() {
    f1Doc = document.getElementById("f1").contentDocument;
    if (++loadCounter == 2) process();
  }
  function f2Loaded() {
    if (++loadCounter == 2) process();
  }
</script>
<iframe id="f1" src="..." onload="f1Loaded()"></iframe>
<iframe id="f2" src="..." onload="f2Loaded()"></iframe>
```

## Using magical timeouts to cause delays

Sometimes when there is an asynchronous operation going on, it may be
tempting to use a timeout to wait a while, hoping that the operation has
been finished by then and that it's then safe to continue. Such code
uses patterns like this:

```js
setTimeout(handler, 500);
```

This should raise an alarm in your head. As soon as you see such code,
you should ask yourself: "Why 500, and not 100? Why not 1000? Why not
328, for that matter?" You can never answer this question, so you
should always avoid code like this!

What's wrong with this code is that you're assuming that 500ms is enough
for whatever operation you're waiting for. This may stop being true
depending on the platform, whether it's a debug or optimized build of
Firefox running this code, machine load, whether the test is run on a
VM, etc. And it will start failing, sooner or later.

Instead of code like this, you should wait for the operation to be
completed explicitly. Most of the time this can be done by listening
for an event. Some of the time there is no good event to listen for, in
which case you can add one to the code responsible for the completion of
the task at hand.

Ideally magical timeouts are never necessary, but there are a couple
cases, in particular when writing web-platform-tests, where you might
need them. In such cases consider documenting why a timer was used so it
can be removed if in the future it turns out to be no longer needed.

## Using objects without accounting for the possibility of their death

This is a very common pattern in our test suite, which was recently
discovered to be responsible for many intermittent failures:

```js
function runLater(func) {
  var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(func, 0, Ci.nsITimer.TYPE_ONE_SHOT);
}
```

The problem with this code is that it assumes that the `timer` object
will live long enough for the timer to fire. That may not be the case
if a garbage collection is performed before the timer needs to fire. If
that happens, the `timer` object will get garbage collected and will
go away before the timer has had a chance to fire. A simple way to fix
this is to make the `timer` object global, so that an outstanding
reference to the object would still exist by the time that the garbage
collection code attempts to collect it.

```js
var timer;
function runLater(func) {
  timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(func, 0, Ci.nsITimer.TYPE_ONE_SHOT);
}
```

A similar problem may happen with `nsIWebProgressListener` objects
passed to the `nsIWebProgress.addProgressListener()` method, because
the web progress object stores a weak reference to the
`nsIWebProgressListener` object, which does not prevent it from being
garbage collected.

## Tests which require focus

Some tests require the application window to be focused in order to
function properly.

For example if you're writing a crashtest or reftest which tests an
element which is focused, you need to specify it in the manifest file,
like this:

```
needs-focus load my-crashtest.html
needs-focus == my-reftest.html my-reftest-ref.html
```

Also, if you're writing a mochitest which synthesizes keyboard events
using `synthesizeKey()`, the window needs to be focused, otherwise the
test would fail intermittently on Linux. You can ensure that by using
`SimpleTest.waitForFocus()` and start what your test does from inside
the callback for that function, as below:

```js
SimpleTest.waitForFocus(function() {
  synthesizeKey("x", {});
  // ...
});
```

Tests which require mouse interaction, open context menus, etc. may also
require focus. Note that waitForFocus implicitly waits for a load event
as well, so it's safe to call it for a window which has not finished
loading yet.

## Tests which take too long

Sometimes what happens in a single unit test is just too much. This
will cause the test to time out in random places during its execution if
the running machine is under a heavy load, which is a sign that the test
needs to have more time to execute. This could potentially happen only
in debug builds, as they are slower in general. There are two ways to
solve this problem. One of them is to split the test into multiple
smaller tests (which might have other advantages as well, including
better readability in the test), or to ask the test runner framework to
give the test more time to finish correctly. The latter can be done
using the `requestLongerTimeout` function.

## Tests that do not clean up properly

Sometimes, tests register event handlers for various events, but they
don't clean up after themselves correctly. Alternatively, sometimes
tests do things which have persistent effects in the browser running the
test suite. Examples include opening a new window, adding a bookmark,
changing the value of a preference, etc.

In these situations, sometimes the problem is caught as soon as the test
is checked into the tree. But it's also possible for the thing which
was not cleaned up properly to have an intermittent effect on future
(and perhaps seemingly unrelated) tests. These types of intermittent
failures may be extremely hard to debug, and not obvious at first
because most people only look at the test in which the failure happens
instead of previous tests. How the failure would look varies on a case
by case basis, but one example is [bug
612625](https://bugzilla.mozilla.org/show_bug.cgi?id=612625).

## Not waiting on the specific event that you need

Sometimes, instead of waiting for event A, tests wait on event B,
implicitly hoping that B occurring means that A has occurred too. [Bug
626168](https://bugzilla.mozilla.org/show_bug.cgi?id=626168) was an
example of this. The test really needed to wait for a paint in the
middle of its execution, but instead it would wait for an event loop
hit, hoping that by the time that we hit the event loop, a paint has
also occurred. While these types of assumptions may hold true when
developing the test, they're not guaranteed to be true every time that
the test is run. When writing a test, if you have to wait for an event,
you need to take note of why you're waiting for the event, and what
exactly you're waiting on, and then make sure that you're really waiting
on the correct event.

## Tests that rely on external sites

Even if the external site is not actually down, variable performance of
the external site, and external networks can add enough variation to
test duration that it can easily cause a test to fail intermittently.

External sites should NOT be used for testing.

## Tests that rely on Math.random() to create unique values

Sometimes you need unique values in your test. Using `Math.random()`
to get unique values works most of the time, but this function actually
doesn't guarantee that its return values are unique, so your test might
get repeated values from this function, which means that it may fail
intermittently. You can use the following pattern instead of calling
`Math.random()` if you need values that have to be unique for your
test:

```js
var gUniqueCounter = 0;
function generateUniqueValues() {
  return Date.now() + "-" + (++gUniqueCounter);
}
```

## Tests that depend on the current time

When writing a test which depends on the current time, extra attention
should be paid to different types of behavior depending on when a test
runs. For example, how does your test handle the case where the
daylight saving (DST) settings change while it's running? If you're
testing for a time concept relative to now (like today, yesterday,
tomorrow, etc) does your test handle the case where these concepts
change their meaning at the middle of the test (for example, what if
your test starts at 23:59:36 on a given day and finishes at 00:01:13)?

## Tests that depend on time differences or comparison

When doing time differences the operating system timers resolution
should be taken into account. For example consecutive calls to
[Date()](https://developer.mozilla.org/docs/Web/JavaScript/Reference/Global_Objects/Date) don't
guarantee to get different values. Also when crossing XPCOM different
time implementations can give surprising results. For example when
comparing a timestamp got through {doc}`/nspr/reference/pr_now` with one
got though a JavaScript date, the last call could result in the past of
the first call! These differences are more pronounced on Windows, where
the skew can be up to 16ms. Globally, the timers' resolutions are
guesses that are not guaranteed (also due to bogus resolutions on
virtual machines), so it's better to use larger brackets when the
comparison is really needed.

## Tests that destroy the original tab

Tests that remove the original tab from the browser chrome test window
can cause intermittent oranges or can, and of themselves, be
intermittent oranges. Obviously, both of these outcomes are undesirable.
You should neither write tests that do this, or r+ tests that do this.
As a general rule, if you call `addTab` or other tab-opening methods
in your test cleanup code, you're probably doing something you shouldn't
be.

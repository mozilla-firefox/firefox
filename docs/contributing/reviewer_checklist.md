(reviewer-checklist)=

# Reviewer Checklist

Submitting patches to Mozilla source code needn't be complex. This
article provides a list of best practices for your patch content that
reviewers will check for or require. Following these best practices
will lead to a smoother, more rapid process of review and acceptance.

## Tooling

- Reviewers should track their queue with the MyQOnly add-on, see
  {ref}`keeping-track-of-your-review-queue`.

## Good web citizenship

- Make sure new web-exposed APIs actually make sense and are either
  standards track or preffed off by default.
- In C++, wrapper-cache as needed. If your object can be gotten from
  somewhere without creating it in the process, it needs to be
  wrapper-cached.

## Correctness

- The bug being fixed is a valid bug and should be fixed.
- The patch fixes the issue.
- The patch is not unnecessarily complicated.
- The patch does not add duplicates of existing code ('almost
  duplicates' could mean a refactor is needed). Commonly this results
  in "part 0" of a bug, which is "tidy things up to make the fix easier
  to write and review".
- If QA needs to verify the fix, you should provide steps to reproduce
  (STR).

## Quality

- If you can unit-test it, you should unit-test it.
- If it's JS, try to design and build so that xpcshell can exercise
  most functionality. It's quicker.
- Make sure the patch doesn't create any unused code (e.g., remove
  strings when removing a feature)
- All caught exceptions should be logged at the appropriate level,
  bearing in mind personally identifiable information, but also
  considering the expense of computing and recording log output.
  Fenix: prefer the android-components `Logger` over
  `android.util.Log`; its `message` parameter is eagerly evaluated, so
  don't build expensive strings inline.
- Error messages that appear in web platform environments should
  explain the reason for the error, and use web platform terminology
  (as opposed to internal Firefox terminology). More details can be
  found in the {doc}`helpful error messages guide <helpful-error-messages>`.

## Style

- Follow the [style
  guide](/code-quality/coding-style/index.md)
  for the language and module in question.
- Follow local style for the surrounding code, even if that local style
  isn't formally documented.
- New files have license declarations and modelines.
- New JS files should use strict mode.
- Trailing whitespace (git diff highlight this). You can use git rebase --whitespace=fix.

## Security issues

- There should be no writing to arbitrary files outside the profile
  folder.
- Be careful when reading user input, network input, or files on disk.
  Assume that inputs will be too big, too short, empty, malformed, or
  malicious.
- Tag for sec review if unsure.
- If you're writing code that uses JSAPI, chances are you got it wrong.
  Try hard to avoid doing that.

## Privacy issues

- There should be no logging of URLs or content from which URLs may be
  inferred.
- Fenix: there is no PII-safe logging helper; keep URLs and profile
  paths out of log messages entirely.
- Tag for privacy review if needed.

## Resource leaks

- In Java, memory leaks are largely due to singletons holding on to
  caches and collections, or observers sticking around, or runnables
  sitting in a queue.
- In C++, cycle-collect as needed. If JavaScript can see your object,
  it probably needs to be cycle-collected.
- Fenix: if your custom view does animations, clean up runnables in
  onDetachedFromWindow().
- Ensure all file handles and other closeable resources are closed
  appropriately.

## Performance impact

- Check for main-thread IO. Fenix: Android may warn about this with
  StrictMode.
- Remove debug logging that is not needed in production.

## Threading issues

- Enormous: correct use of locking and volatility; livelock and
  deadlock; ownership.
- Fenix: all view methods should be touched only on the UI thread.
- Fenix: activity lifecycle awareness -- test with "Don't keep
  activities" enabled in Android developer options.

## Compatibility

- Version files, databases, messages
- Tag messages with ids to disambiguate callers.
- IDL UUIDs are updated when the interface is updated.
- Android permissions should be 'grouped' into a common release to
  avoid breaking auto-updates.
- Android APIs newer than the minimum supported SDK level should be
  guarded by a version check.

## Preffability

- If the feature being worked on is covered by prefs, make sure they
  are hooked up.
- If working on a new feature, consider adding prefs to control the
  behavior.
- Consider adding prefs to disable the feature entirely in case bugs
  are found later in the release cycle.
- Fenix: "prefs" can be Gecko prefs, SharedPreferences values, or
  build-time flags. Which one you choose depends on how the feature is
  implemented: a pure Java service can't easily check Gecko prefs, for
  example.

## Strings

- There should be no string changes in patches that will be uplifted
  (including string removals).
- Rev entity names for string changes.
- When making UI changes, be aware of the fact that strings will be
  different lengths in different locales.

## Documentation

- The commit message should describe what the patch is changing (not be
  a copy of the bug summary). The first line should be a short
  description (since only the first line is shown in the log), and
  additional description, if needed, should be present, properly
  wrapped, in later lines.
- Adequately document any potentially confusing pieces of code.
- Flag a bug with dev-doc-needed if any addon or web APIs are affected.
- Use Javadocs extensively, especially on any new non-private methods.
- When moving files, ensure blame/annotate is preserved.

## Accessibility

- For HTML pages, images should have the alt attribute set when
  appropriate. Similarly, a button that is not a native HTML button
  should have role="button" and the aria-label attribute set.
- Fenix: make sure contentDescription is set for parts of the UI that
  should be accessible.

## Landing the patch

- If the patch is simple enough and has no linter errors, the reviewer
  should not hesitate to land it directly after approving it, rather
  than leaving it to the author. This reduces round-trips and gets the
  fix into the tree faster, which is especially helpful for new
  contributors.

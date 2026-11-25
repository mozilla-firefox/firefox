# Smart Window browser testing instructions

Before writing or changing a Smart Window browser test, read the
examples below and the shared helpers they use. Use the examples to infer test
names, scope, and the order of setup, action, assertions, and cleanup. Follow
the rules in this file where an example differs.

The descriptions highlight one particularly useful pattern per example.
Consider any other patterns that fit the test you’re writing.

## Mochia examples

- [`browser_smartformfill_form_review_editing.js`](ui/test/browser/browser_smartformfill_form_review_editing.js) for short tests with shared setup and cleanup and distinct user actions.
- [`browser_smartformfill_form_review_filling.js`](ui/test/browser/browser_smartformfill_form_review_filling.js) for the nested retry tests under `when filling the form fails`.
- [`tasks/browser_tasks_monitor_groups.js`](ui/test/browser/tasks/browser_tasks_monitor_groups.js) for nested setup and feature-specific organization.
- [`aitab/browser_aitab_sidebar.js`](ui/test/browser/aitab/browser_aitab_sidebar.js) for separate sidebar scenarios and Fluent ID assertions.

## `add_task` examples

- [`browser_smartwindow_get_page_content_timeout.js`](ui/test/browser/browser_smartwindow_get_page_content_timeout.js) for separate error scenarios and assertions that the turn still completes.
- [`browser_smartwindow_jump_to_bottom.js`](ui/test/browser/browser_smartwindow_jump_to_bottom.js) for input through the Smartbar and tab switching in `test_jump_to_bottom_hidden_on_empty_conversation`.
- [`browser_smartwindow_manage_tabs_fullpage_chat.js`](ui/test/browser/browser_smartwindow_manage_tabs_fullpage_chat.js) for capturing and checking the model request in `test_group_tabs_without_card_skips_chat_tab`.

## Rules to follow

- Use the Mochia interface for new or refactored tests.
- Files containing `add_task` tests will continue to be maintained and should
not be refactored to Mochia unless explicitly asked to refactor to Mochia.
- Prove behaviour, preferably with browser tests. Reviewers ask for no new
  xpcshell tests, and push back on tautological tests, assertions, and
  hand-made fakes.
- Mock model calls with `MockEngineManager`. Use `startMockOpenAI` only to
  test the real engine and HTTP layer.
- Don't add `*ForTesting` exports or test-only hooks to production modules.
- `head.js` installs global sinon stubs. Use `sinon.createSandbox()` per test
  and never `sinon.restore()`.
- Don't sleep, including through `content.setTimeout` or `win.setTimeout`;
  use `BrowserTestUtils.waitForMutationCondition` when a DOM mutation is the condition
  being waited on, otherwise use `TestUtils.waitForCondition`.
- Assert on Fluent IDs, not English text. Cover edge cases such as empty,
  very long and malformed input.

### Test design

Use an end to end testing approach for browser tests. Drive UI through helpers
like BrowserTestUtils.sys.mjs, TestUtils.sys.mjs, ContentTaskUtils.sys.mjs,
and any other existing click and shared helpers to automate user interactions
that exercise production code to assert a behavioral outcome.

- Separate setup, the action named by the test, and assertions with blank lines.
  Keep before-and-after checks next to the action.
- Write a separate test for each distinct input and expected outcome. Avoid
  looping over test cases.
- Define helpers after the `describe` block. Use concise JSDoc for each new
  helper's purpose and parameters.
- Trigger errors through production code when possible. If you must inject a
  failure, limit it to the failing operation and let the real error handling
  run. Remove the injection as soon as it’s no longer needed, even if the test
  fails.
- Make cleanup safe after partial setup. Failed `Assert.*` calls let browser
  mochitests continue, so throw a clear error or guard dependent steps when a
  required precondition fails.

### Suite organization

- Put new browser tests in a feature subdirectory of
  `browser/components/aiwindow/ui/test/browser`. Reuse a matching directory or
  confirm the new directory name with the user. Include the feature name
  in the test file name.
- Give each feature directory its own manifest so it can run independently.
  Keep related tests and their shared `head.js` together.
- Keep setup cheap and each file well within the default timeout. Test-verify
  reruns changed files in chaos mode, which costs about 8x a local opt run, so
  split slow files into focused suites.
- Keep each test’s setup and cleanup independent. Reuse the window when tests
  can restore its state even on failure. Use a separate window when testing
  window behavior or when state cannot be restored reliably. A fresh tab
  shares window state.

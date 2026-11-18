# Testing Policy

**Everything that lands in the Firefox repository includes automated tests by default**. Every commit has tests that cover every major piece of functionality and expected input conditions.

One of the following Project Tags must be applied in Phabricator before landing, at the discretion of the reviewer:
* `testing-approved` if it has sufficient automated test coverage.
* One of `testing-exception-*` if not. After speaking with many teams across the project we’ve identified the most common exceptions, which are detailed below.

## Exceptions

* **testing-exception-unchanged**: Commits that don’t change behavior for end users. For example:
  * Refactors, mechanical changes, and deleting dead code as long as they aren’t meaningfully changing or removing any existing tests. Authors should consider checking for and adding missing test coverage in a separate commit before a refactor.
  * Code that doesn’t ship to users (for example: documentation, build scripts and manifest files, mach commands). Effort should be made to test these when regressions are likely to cause bustage or confusion for developers, but it’s left to the discretion of the reviewer.
* **testing-exception-ui**: Commits that change UI styling, images, or localized strings. While we have end-to-end automated tests that ensure the frontend isn’t totally broken, and screenshot-based tracking of changes over time, we currently rely only on manual testing and bug reports to surface style regressions.
* **testing-exception-elsewhere**: Commits where tests exist but are somewhere else. This **requires a comment** from the reviewer explaining where the tests are. For example:
  * In another commit in the Stack.
  * In a followup bug.
  * In an external repository for third party code.
  * When following the {doc}`Security Bug Approval Process </bug-mgmt/processes/security-approval>` tests are usually landed later, but should be written and reviewed at the same time as the commit.
* **testing-exception-other**: Commits where none of the defined exceptions above apply but it should still be landed. This should be scrutinized by the reviewer before using it - consider whether an exception is actually required or if a test could be reasonably added before using it. This **requires a comment** from the reviewer explaining why it’s appropriate to land without tests. Some examples that have been identified include:
  * Interacting with external hardware or software and our code is missing abstractions to mock the interaction out.
  * Inability to reproduce a reported problem, so landing something to test a fix in Nightly.

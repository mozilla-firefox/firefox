(fixing-security-bugs)=

# Fixing Security Bugs

A bug has been reported as security-sensitive in Bugzilla and received a
security rating.

If this bug is private - which is most likely for a reported security
bug - **the process for patching is slightly different than the usual
process for fixing a bug**.

Here are security guidelines to follow if you’re involved in reviewing,
testing and landing a security patch. See
{ref}`security-bug-approval-process`
for more details about how to request sec-approval and land the patch.

## Keeping private information private

A security-sensitive bug in Bugzilla means that all information about
the bug except its ID number are hidden. This includes the title,
comments, reporter, assignee and CC’d people.

A security-sensitive bug usually remains private until a fix is shipped
in a new release, **and after a certain amount of time to ensure that a
maximum number of users updated their version of Firefox**. Bugs are
usually made public after 6 months and a couple of releases.

From the moment a security bug has been privately reported to the moment
a fix is shipped and the bug is set public, all information about that
bug needs to be handled carefully in order to avoid an unmitigated
vulnerability becoming known and exploited before we release a
fix (0-day).

During a normal process, information about the nature of bug can be
accessed through:

- Bug comments (Bugzilla, GitHub issue)
- Commit message (visible on Bugzilla, tree check-ins and test servers)
- Code comments
- Test cases
- Bug content can potentially be discussed on public IRC/Slack channels
  and mailing list emails.

When patching for a security bug, you’ll need to be mindful about what
type of information you share and where.

### In commit messages

People are watching code check-ins, so we want to avoid sharing
information which would disclose or help finding a vulnerability too
easily before we shipped the fix to our users. This includes:

- The **nature of the vulnerability** (overflow, use-after-free, XSS,
  CSP bypass...)

- **Ways to trigger and exploit that vulnerability**
  \- In commit messages, code comments and test cases.

- The fact that a bug / commit is security-related:

  - **Trigger words** in the commit message or code comments such as
    "security", "exploitable", or the nature of a security vulnerability
    (overflow, use-after-free…)
  - **Security approver’s name** in a commit message.

- The Firefox versions and components affected by the vulnerability.

- Patches with an obvious fix.

### In Bugzilla and other public channels

In addition to commits, you’ll need to be mindful of not disclosing
sensitive information about the bug in public places, such as Bugzilla:

- Mention the bugs in comment of the private bug instead.
- Do not comment sensitive information in public related bugs.
- Also be careful about who you give bug access to: **double check
  before CC’ing the wrong person or alias**.
- You may add public bugs in the “duplicate”,
  “depends on”, “blocks”, “regression”, “regressed by”, or “see also” section,
  as well as adding security bugs into those sections of public bugs.
  Bugzilla will only reveal those relationships to people with `editbugs`
  permission or access to the security bug.

On IRC, Slack channels, GitHub issues, mailing lists: If you need to
discuss about a security bug, use a private channel (protected with a
password or with proper right access management)

## During Development

### Testing security bugs

Pushing to Try servers requires Level 1 Commit access but **content
viewing is publicly accessible**.

As much as possible, **do not push to Try servers**. Testing should be
done locally before checkin in order to prevent public disclosing of the
bug.

Because of the public visibility, pushing to Try has all the same concerns
as committing the patch. Please heed the concerns in the
{ref}`landing-your-patch` section.  If necessary, you can fold your changes
into another working patch and send them to try together, not referencing
the security fixes in the commit or comments.

**Do not push the bug's own vulnerability testcase to Try.**

If you need to push to Try servers, make sure your tests don’t disclose
what the vulnerability is about or how to trigger it. Do not mention
anywhere it is security related.

### Obfuscating a security patch

If you really need to push to Try servers,
**consider integrating your security-related patch to non-security work
in the same area**. And/or pretend it is related to something else, like
some performance improvement or a correctness fix. **Definitely don't
include the bug number in the commit message.** This will help make
the security issue less easily identifiable.

### Requesting sec-approval

See {ref}`security-bug-approval-process`
for more details

(landing-your-patch)=

### Landing your patch (with or without sec-approval)

Before asking for sec-approval or landing, ensure your patch does not disclose
information about the security vulnerability unnecessarily. Specifically:

1. The patch commit message and its contents should not mention security,
   security bugs, or sec-approvers.
   Note that you can alter the commit message directly in phabricator,
   if that's the only thing you need to do - you don't need to amend your
   local commit and re-push it.
   While comprehensive commit messages are generally encouraged; they
   should be omitted for security bugs and instead be posted in the bug
   (which will eventually become public.)

2. Consider separating out tests into a separate commit.
   While previously we did not land tests with the patch ever, the advent of
   AI tools has frequently made this precaution not worth the extra effort
   for the developer.  Instead **consider with your experience if the
   vulnerability is unusually difficult to determine from the patch,
   or unusually difficult to trigger** - this is when it is appropriate to
   separate the test into a separate commit.

   - Those tests should be checked in later, after an official Firefox
     release that contains the fix has been live for at least
     four weeks. For example, if Firefox 53
     contains a security issue that affects the world and that issue is
     fixed in 54, tests for this fix should not be checked in
     until four weeks after 54 goes live.

     The exception to this is if there is a security issue that doesn't
     affect Firefox Release or ESR, only mozilla-central and/or other
     development branches. Since the security problem was never
     released to the world, once the bug is fixed in all affected
     places, tests can be checked in to the various branches.

   - There are two main techniques for remembering to check in the
     tests later:

   > 1. clone the sec bug into a separate "task" bug **that is also
   >    in a security-sensitive group to ensure it's not publicly visible**
   >    called something like "land tests for bug xxxxx" and assign to
   >    yourself. It should get a "sec-other" keyword rating.
   >
   >    Tip: In phabricator, you can change the bug linked to
   >    a commit with tests if the tests were already separate, while keeping
   >    the previously granted review, meaning you can just land the patch
   >    when ready, rather than having your reviewer and you have to remember
   >    what this was about a month or two down the line.
   >
   > 2. Or, set the "in-testsuite" flag to "?" and add a whiteboard tag of the
   >    form [reminder-test 2026-11-30]. Bugbot will needinfo the bug assignee
   >    on that date. When the tests get checked in, change the "in-testsuite"
   >    flag to "+".
   >
   >    If the tests are revealing enough to separate this way, the reminder
   >    date should be 4 weeks after the release containing the fix.

### Landing tests

Tests can be landed **once the release containing fixes has been live
at least 4 weeks**.

The exception is if a security issue has never been shipped in a release
build and has been fixed in all development branches.

### Making a security bug public

This is the responsibility of the security management team.

## Essentials

- **Do not disclose any information about the vulnerability before a
  release with a fix has gone live for enough time for users to update
  their software**.

  - This includes code comments, commit messages, tests, public
    communication channels.

- If any doubt: '''request sec-approval? '''

- If any doubt: **needinfo security folks**.

## Documentation & Contacts

- {ref}`Normal process for submitting a patch <how-to-submit-a-patch>`
- [How to file a security bug](https://wiki.mozilla.org/Security/Fileabug)
- [Handling Mozilla security bugs (policy)](https://www.mozilla.org/en-US/about/governance/policies/security-group/bugs/)
- {ref}`security-bug-approval-process`
- [Contacting the Security team(s) at Mozilla:](https://wiki.mozilla.org/Security)

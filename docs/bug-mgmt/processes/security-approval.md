(security-bug-approval-process)=

# Security Bug Approval Process

## How to fix a core-security bug in Firefox - developer guidelines

Follow these security guidelines if you’re involved in reviewing,
testing and landing a security patch:
{ref}`fixing-security-bugs`.

## Purpose: protect the worst security vulnerabilities while reducing developer friction

In the past, obfuscating our security patches was an effective
technique to frustrate the process of exploiting vulnerabilities that
were fixed in development branches but not yet released to users.
However the advent of AI tools means that most potential vulnerability
fixes can be analyzed autonomously to discover the root cause of an
issue, as well as how to trigger it and in many cases even develop
a proof of concept exhibiting exploitation control.

Therefore, our primary mechanism to protect users is to ship security
fixes as quickly as possible.

Accordingly, we have minimized the sec-approval process to reduce
developer friction when landing security patches.

## Process for Security Bugs (Developer Perspective)

One type of security bug is a straightforward vulnerability.  A
second type is a more casual analysis of a code pattern, an architecture
limitation, or a general observation about bug patterns.

It is important to recognize the difference between these two types of issues,
especially when commenting in bugs with external reporters.

### Filing / Managing Bugs

- Try whenever possible to file security bugs marked as such when
  filing, instead of filing them as open bugs and then closing later.
  This is not always possible, but attention to this, especially when
  filing from crash-stats, is helpful.
- It is \_ok\_ to link security bugs to non-security bugs with Blocks,
  Depends, Regressed By, Regressions, or See Also. Users with the editbugs
  permission will be able to see the reference, but not view a restricted bug
  or its summary. Users without the permission will not be able to see the
  link.
- Try not to discuss problematic code patterns or architecture limitations
  in bugs filed by external reporters, or in situations where fixing the
  undesirable behavior is likely to be a project that does not complete
  within half a year or more.  Instead, file a second bug tagged
  `sec-audit` (for identifying patterns and addressing them) or `sec-want`
  (for developing more comprehensive checks or fixing a design), and move
  that discussion there.  AI is very good at taking patterns and
  descriptions and finding new bugs based on them.

### Developing the Patch

- Comments in the code should not mention a security issue is being
  fixed. Don’t paint a picture or an arrow pointing to security issues
  any more than the code changes already do.
- Do not push to Try servers if possible: this exposes the security
  issues for these high rated vulnerabilities to public viewing. In an
  ideal case, testing of patches is done locally before final check-in
  to mozilla-central.
- If pushing to Try servers is necessary, **do not include the bug
  number in the patch**. Ideally, do not include tests in the push as
  the tests can illustrate the exact nature of the security problem
  frequently.
- If you must push to Try servers, with or without tests, try to
  obfuscate what this patch is for. Try to push it with other,
  non-security work, in the same area.

Request review of the patch in the same process as normal. After the
patch has been reviewed you will request sec-approval as needed. See
{ref}`fixing-security-bugs`
for more examples/details of these points.

### Preparing the patch for landing

See {ref}`fixing-security-bugs`
for more details.

### On Requesting sec-approval

Previously, sec-approval was required for all `sec-high` rated security
bugs in most circumstances.  This is now inverted.

**sec-approval is only required for bugs that represent a vulnerability
in the parent process, triggerable from a content process**.

This is typically constrained to bugs with the keywords `sec-high` and
`csectype-sandbox-escape`.

 - If a bug does not have a security rating, you are invited to give it
   one, following the [Client Severity Guidelines](https://wiki.mozilla.org/Security_Severity_Ratings/Client)
 - If a bug is a cross-process bug that does not affect the Parent
   Process, the correct keyword is `csectype-priv-escalation`.  In the
   past, these were given `csectype-sandbox-escape` but this is no longer
   correct, and you are invited to fix the keywords if you encounter them
   used incorrectly.

For the avoidance of doubt, core-security bug fixes can be landed by
a developer without any explicit approval if:

 - The bug has a sec-low, sec-moderate, sec-other, or sec-want rating.
 - The bug has a sec-high rating, but only affects the content process
 - The bug has a sec-high rating, but only affects some other, non-Parent process
 - The bug has a sec-high rating, is a cross-process bug, but that target process
   is the GPU, RDD, GMP, Utility, Socket, or other non-Parent process
 - The bug _is_ a parent process bug _but_ it is a recent regression on
   mozilla-central. Meaning:
   - A specific regressing check-in has been identified
   - The developer can (**and has**) marked the status flags for ESR and
     Beta as "unaffected"
   - We have not shipped this vulnerability in anything other than a
     nightly build

If it meets any of the above criteria, developers do not need to ask for
sec-approval.

In the sandbox escape case, developers should ask for sec-approval. When
a patch is ready to be landed, click on the "Details" link for it in the
Bugzilla attachment table (not directly on phabricator at time of writing),
then set the sec-approval flag to '?'.

If developers are unsure about a bug and it has a patch ready, just
request sec-approval anyway and move on. Don't overthink it!

An automatic nomination comment will be added to bugzilla when
sec-approval is set to '?'. The questions in this need to be filled out
as best as possible when sec-approval is requested for the patch.

It is as follows:

```
[Security approval request comment]
How easily can the security issue be deduced from the patch?
Do comments in the patch, the check-in comment, or tests included in
the patch paint a bulls-eye on the security problem?
Which older supported branches are affected by this flaw?
If not all supported branches, which bug introduced the flaw?
Do you have backports for the affected branches? If not, how
different, hard to create, and risky will they be?
How likely is this patch to cause regressions; how much testing does
it need?
```

This is similar to the ESR approval nomination form and is meant to help
us evaluate the risks around approving the patch for checkin.

When the bug is approved for landing, the sec-approval flag will be set
to '+' with a comment from the approver to land the patch. At that
point, land it according to instructions provided..

This will allow us to control when we can land security bugs without
exposing them too early and to make sure they get landed on the various
branches.

If you have any questions or are unsure about anything in this document
contact us on Slack in the #security channel or the current
sec-approvers Dan Veditz and Tom Ritter.

## Process for Security Bugs (sec-approver Perspective)

The security assurance team and release management will have their own
process for approving bugs:

1. The Security assurance team goes through sec-approval ? bugs daily
   and approves low risk fixes for central (if early in cycle).
   Developers can also ping the Security Assurance Team (specifically
   Tom Ritter & Dan Veditz) in #security on Slack when important.

   1. If a bug lacks a security-rating one should be assigned - possibly
      in coordination with the (other member of) the Security Assurance
      Team

2. Security team marks tracking flags to ? for all affected versions
   when approved for central. (This allows release management to decide
   whether to uplift to branches just like always.)

3. Weekly security/release management triage meeting goes through
   sec-approval + and ? bugs where beta and ESR is affected, ? bugs with
   higher risk (sec-high and sec-critical), or ? bugs near end of cycle.

Options for sec-approval including a logical combination of the
following:

- Separate out the test and comments in the code into a followup commit
  we will commit later.
- Remove the commit message and place it in the bug or comments in a
  followup commit.
- Please land it bundled in with another commit
- Land today
- Land today, land the tests after
- Land closer to the release date
- Land in Nightly to assess stability
- Land today and request uplift to all branches
- Request uplift to all branches and we'll land as close to shipping as
  permitted
- Chemspill time

The decision process for which of these to choose is perceived risk on
multiple axes:

- ease of exploitation
- reverse engineering risk
- stability risk

The most common choice is: not much stability risk, not an immediate
reverse engineering risk, moderate to high difficulty of exploitation:
"land whenever".

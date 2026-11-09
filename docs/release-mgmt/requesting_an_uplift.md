# Requesting an Uplift

```{warning}
See [the uplift rules page](uplift_rules.md) for more information on uplifts.
```

Please [file a bug](https://bugzilla.mozilla.org/enter_bug.cgi?product=Conduit&component=General)
if you encounter issues requesting an uplift, or tell us about the problem in
#engineering-workflow on Slack.

## Requesting an Uplift

Requesting a patch uplift is completed using the standard Mozilla code review
and landing tools and processes. Creating an uplift request can be done either:

- By finding the patch in your local VCS and submitting via `moz-phab uplift`.
- By finding the patch on Phabricator and submitting via Lando.

After submitting the patch stack via either method, you must complete the
Uplift Request form on the tip of the patch stack. This will request review from
Release Management.

The [patch uplifting rules page](uplift_rules.md) contains information on what
should be uplifted.

## Uplifting a simple change that won't require local modification or testing

If your Phabricator revision(s) are straightforward and won't require local
testing prior to requesting an uplift you can use Lando to create the uplift
revision. Follow the [Requesting Uplift using Lando](#requesting-uplift-using-lando)
process.

### Requesting Uplift using Lando

1. Go to the Lando page for the stack of revisions to uplift.
   - You can get here from the Phabricator revision by using the "View stack in
     Lando" option on the right side menu.
2. Click the Log In button in the top right of Lando, if you're not already
   logged in.
3. If you haven't added a Phabricator API token (or it has expired i.e. you
   don't see the Request Uplift button), click your username in the top right
   corner and paste the Phabricator API token there.
   - Tokens are in the form `api-XXXXX…`. This token can be found here (replace
     `<USERNAME>` with your phab username):
     `https://phabricator.services.mozilla.com/settings/user/<USERNAME>/page/apitokens/`
     ([screenshot](https://wiki.mozilla.org/File:Lando-phab-api-token.png))
4. Click the "Request Uplift" button in the bottom right corner of the page.
5. Select your uplift targets by checking all the repositories you wish to
   uplift to (e.g., firefox-beta, firefox-release, etc.).
6. Complete the uplift request assessment form and click "Create uplift
   request".
   - After submitting, Lando will verify whether your stack applies cleanly to
     the selected uplift targets in the background.
   - If your stack applies cleanly, Lando will automatically create Phabricator
     uplift requests for you.
   - If your patch has merge conflicts you will receive an email notification
     with steps on how to proceed.

## Uplifting a change that you want to apply and test locally first

If your Phabricator revision(s) require more attention, perhaps because it's
expected they won't apply cleanly to the target, you'll need to apply the commit
locally and submit using moz-phab.

**Note:** you must use `moz-phab uplift`, not `moz-phab submit` or any other
unsupported tool to submit uplift requests.

### Requesting Uplift using moz-phab

1. Ensure you are using the
   [mozilla-firefox](https://github.com/mozilla-firefox/firefox) repository.
2. Switch to the appropriate branch (e.g. `beta`, `esr140`, etc.).
3. Make sure your local repository is up to date with the target branch head.
4. Find the patch or series of patches in your local VCS.
   - You can `moz-phab patch` patches onto your local machine if need be.
5. Use `moz-phab uplift --list-trains` to determine which uplift train you want
   to request an uplift for.
   - Note: Train names generally start with `firefox-` and are not simply the
     branch name.
6. Run
   `moz-phab uplift --train <train> <revision before first of patches to uplift> <last revision of patches to uplift>`.
   - This will create new revisions in Phabricator that will be uplifted to the
     specified train.
   - Most of the arguments to `moz-phab uplift` are the same as
     `moz-phab submit`.
   - The `moz-phab uplift` command requires the commit hash of the revision
     immediately before the first one in the patch stack, and the commit hash of
     the last revision to uplift.
7. Open the URL as suggested at the bottom of the output.
8. Complete the uplift request form. See the
   [section below](#submit-the-uplift-request-form) on how to submit the form.

### Manual Cherry-Pick (Recommended if Conflicts Are Likely)

If you expect merge conflicts or want finer control over the uplift, you may
prefer manually cherry-picking commits and uploading with
`moz-phab uplift --no-rebase`. This avoids the behavior of `moz-phab uplift`,
which will **fail without recovery options if any conflicts are encountered.**

1. Ensure you are using the
   [mozilla-firefox](https://github.com/mozilla-firefox/firefox) repository.
2. Switch to the appropriate branch (e.g. `beta`, `esr140`, etc.).
3. Make sure your local repository is up to date with the target branch head.
4. Create a new branch for your uplift, e.g. `git switch -c <new branch name>`
5. Cherry-pick each commit or range, e.g. `git cherry-pick <commit-hash>`
6. Resolve any merge conflicts as needed.
7. Use `moz-phab uplift --list-trains` to determine which uplift train you want
   to request an uplift for.
   - Note: Train names generally start with `firefox-` and are not simply the
     branch name.
8. Optionally, fix up the commit message if needed, with `git commit --amend`
   (for a single commit; or `git rebase -i HEAD~<some number>` for multiple
   commits). This step is not needed for the first attempt to uplift a commit
   from the main branch. It may be needed for complex cases, such as when
   multiple uplift requests are made for the same commit.
   - If you want to update an existing uplift revision, ensure the commit
     message ends with a line containing `Differential Revision: ...` (replace
     `...` with the URL of your uplift's Phabricator URL)
   - If you want to create a new Phabricator revision, make sure that the
     commit message does not contain `Differential Revision: ...`.
   - In either case, the URL of the original revision on which the uplift is
     based is expected to be listed as `Original Revision: ...`.
9. Submit the uplifts using
   `moz-phab uplift --no-rebase --train <train name> <revision before first of patches to uplift> <last revision of patches to uplift>`
   - Example of uplifting one patch to ESR140:
     `moz-phab uplift --no-rebase --train firefox-esr140 -i --no-wip HEAD^ HEAD`
     (`HEAD^` means the penultimate commit).
10. Open the URL as suggested at the bottom of the output.
11. Complete the uplift request form. See the
    [section below](#submit-the-uplift-request-form) on how to submit the form.

## Submit the Uplift Request Form

Once your revision is created in Phabricator, you must complete the uplift
request assessment form. This form is a short questionnaire that helps Release
Managers understand the reasoning behind the uplift request and the risk
associated with this patch landing directly on a release repository without
riding the release trains.

1. Go to the Lando revision in the uplift request stack.
   1. Click "View Stack in Lando" on the right hand side of the Phabricator
      revision.
   2. You can also swap the Phabricator domain with the Lando domain. For
      example, change <https://phabricator.services.mozilla.com/D262303> to
      <https://lando.moz.tools/D262303>.
2. Click the "Uplift Assessment" button in the "Uplifts" section of the page.
   ([screenshot](https://wiki.mozilla.org/File:Lando-Uplift-edit.png))
3. Complete the form and submit.
4. Wait for Release Manager approval.

A bot will automatically set Release Managers as reviewers for the patch after
submitting the form. If the patch stack does not apply cleanly to the uplift
train due to a merge conflict, you may need to resolve merge conflicts and
re-submit the stack. The uplift request will sit in a pending state until a
Release Manager approves, the Release Manager will land after approval.

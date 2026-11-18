# Regressions from GitHub

Release Management and the weekly regression triage must be aware of the
status of all reported regressions in order to assure we are not
shipping known regressions in Firefox releases.

If a team is using GitHub to manage their part of the Firefox project,
there’s a risk that those groups might not see a regression.

We need an agreed to standard for how we keep track of these.

## Policy

*All Firefox components, even if their bugs are tracked off of Bugzilla,
must have a component in Bugzilla.*

*If a regression bug is found in any of the release trains (Nightly,
Beta, Release, or ESR) and the bug is in a Firefox component which uses
an external repository, the regression must be tracked by a bug in
Bugzilla (whether or not the component in question uses an external
issue tracker).*

*Unless approved by Release Management, any GitHub managed code with
open regressions will not be merged to the primary Firefox repository. Even if the
regression is not code that has been previously merged into
the primary Firefox repository.*

*All Firefox code managed in GitHub which uses GitHub to manage
issues* [must use the shared
tags](https://mozilla.github.io/bmo-harmony/labels).

### Comments

The bug **must** have the regression keyword.

The bug **must** have release flags set.

If the team works in an external bug tracker, then the Bugzilla bug
**must** reference, using the see-also field, the URL of the bug in the
external tracker.

The bug **must not** be RESOLVED until the code from the external
repository containing the change set for the bug has landed in
the primary Firefox repository. When the change set lands in the primary Firefox repository, the
Bugzilla tracking bug should be set to RESOLVED FIXED and release status
flags should be updated to reflect the release trains the fix has been
landed or uplifted into.

If the change set containing the patch for the regression is reverted
from the primary Firefox repository, for any reason, then the tracking bug for the
regression **must** be set to REOPENED and the release status flags
updated accordingly.

If the change set containing the patch for the bug is backed out, for
any reason, the bug must be reopened and the status flags on the
Bugzilla tracking bug updated.

The team responsible for the component with the regression **should**
strive to create a patch for the primary Firefox repository which contains the fix for
the bug alone, not a monolithic patch containing changes for several
other bugs or features.

Landings of third-party libraries [must contain a manifest
file](https://docs.google.com/document/d/12ihxPXBo9zBBaU_pBsPrc_wNHds4Upr-PwFfiSHrbu8).

## Best Practices

### You must file a regression bug in Bugzilla

*If the code with the regression has landed in the primary Firefox repository, you must
file a regression bug.*

#### Example

While using a release of Firefox (Nightly, Beta, Release, ESR) you run
across a bug. Upon research using MozRegression or other tools you find
that the bug was introduced in a change set imported from a component
whose code and issues are managed in GitHub.

##### Actions to take

- Open a new bug in Bugzilla in appropriate component and add the
  REGRESSION keyword
- Set affected status for the releases where the bug appears
- Open an issue in the corresponding GitHub project, put the Bugzilla
  bug number in the title with the prefix ‘Bug’ (i.e. “Bug 99999:
  Regression in foo”)
- Add the REGRESSION label to the new issue
- Add the link to the GitHub issue into the ‘See Also” field in the
  Bugzilla bug

##### Consequences

*Until the regression is fixed or backed out of the GitHub repo, the
project cannot merge code into the primary Firefox repository*

#### Example

You import a development branch of a component managed in GitHub into
your copy of master. You find a bug and isolate it to the imported
branch. The code is managed in their own GitHub project, but bugs are
managed in Bugzilla.

##### Actions to take

- Open a new bug in Bugzilla in appropriate component and add the
  REGRESSION keyword
- Set affected status for the releases where the bug appears

##### Consequences

*Until the regression is fixed or backed out of the GitHub repo, the
project cannot merge code into the primary Firefox repository*

### Do not file a regression bug in Bugzilla

*If the code with the regression has not landed in the primary Firefox repository, you
do not need to file a bug.*

#### Example

You import a development branch of a component managed in GitHub into
your copy of master. You find a bug and isolate it to the imported
branch. The code and issues are managed in their own GitHub project.

##### Actions to take

- File new issue in the GitHub repository of the imported code.
- Label issue as REGRESSION

##### Consequence

*Issue blocks merge of GitHub project with the primary Firefox repository until
resolved or backed out.*

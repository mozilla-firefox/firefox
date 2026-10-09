# Update Implementation

This page will describe, at a high level, all the bits of Firefox Application
Update and how they interact. It is recommended that you familiarize yourself
with the [Application Update Concepts](Concepts.md) page first as those concepts
will be referred to here.

## A Top Level Overview

Very broadly speaking application update follows these steps:

1.  Firefox is built on our build server. This creates the installers and
    [MAR files](MarFiles.md).
2.  The installers are uploaded to the website. The MARs are uploaded to our
    update server ([Balrog](../../../../update-infrastructure/index.md)).
3.  Users download, install, and run Firefox. We ensure, on installation, that
    we delete any pending updates so that they are not improperly installed at
    startup. Note that the instance of Firefox that runs may be a regular,
    [interactive instance of Firefox](InAppUpdateProcess.rst) or an instance of
    the [Background Update Task](Concepts.md#background-update). In either case,
    the process is quite similar.
4.  An update check is initiated. This can either happen via a timer, or be
    initiated by a user's interaction with the update UI. The Background Update
    Task initiates update using a very similar mechanism to what a user would.
    There is also a mechanism that initiates an update check quickly after
    startup if the current version is especially old.
5.  If the check found an update, Firefox starts downloading it. The update may
    list an available [partial and complete MAR](MarFiles.md#types-of-mars) in
    which case we default to downloading the partial, falling back to the
    complete on certain kinds of failures. At this point, the Background Update
    Task immediately exits, allowing the download happen in the background using
    [BITS](Concepts.md#bits).
6.  When the download completes, we begin [staging](Concepts.md#staging), if we
    are able to do so. Either way, we set up the
    [update directory](Concepts.md#update-directory) to have the MAR in the
    right place and the [update status](Concepts.md#update-state) set correctly.
7.  At this point, we are waiting for Firefox to restart. But, we also continue
    checking for updates. If we find one, we download it also and swap out the
    update we had for the newer one. Note that we only swap out partial MARs for
    other partial MARs. Since complete MARs are much larger, downloading many of
    them that never actually end up being used is unappealing.
8.  Eventually, Firefox is restarted. It doesn't matter whether this is the
    result of clicking a "Restart to Update" prompt, or any other method of
    Firefox being closed and re-launched. Early in startup, Firefox reads the
    `update.status` file. If the status is `pending` or `pending-service`,
    it launches the update binary and exits.
9.  The update binary installs the update, sets `update.status` to convey the
    results back to Firefox, re-launches Firefox with the appropriate arguments,
    and exits.
10. Firefox eventually initiates the Update Service. This happens earlier when
    `update.status` exists, but not nearly as early as potentially launching
    the update binary. This analyzes the results of the update. If necessary, it
    falls back to other ways of updating. For example, we might fall back from a
    partial MAR to a complete MAR or fall back from updating with the Mozilla
    Maintenance Service to updating without it.
11. If relevant, the "What's New Page" (WNP) is shown to the user to notify them
    of changes to this version.
12. Return to checking for updates.

## Mozilla Maintenance Service

The Mozilla Maintenance Service (MMS) exists so that Windows users do not have
to accept a User Account Control prompt every time Firefox updates. It is
installed by default if the user accepts the User Account Control prompt when
installing Firefox. In addition to installing the Maintenance Service, the
installer also writes several keys to the Windows Registry (in a location that
requires elevation, to prevent tampering). These are specific to the install
directory and contain certificate information that the MMS uses to verify that
the Update Binary is a valid updater signed by Mozilla. This is very important
to ensure that it doesn't grant elevation to processes inappropriately.

The MMS is installed as a Windows Service, but it does not follow the
conventions of a normal Windows Service. Rather than running persistently in
the background, it is run on-demand. The only real reason for it to be a Service
is that low-privilege processes can request that a Service be started with a
particular set of arguments and Windows will run it with the privileges
associated with the Service rather than the privileges of the calling process.

The unelevated updater decides whether to launch the MMS based on several
criteria including whether elevation appears to actually be required for the
update and
[whether the installation is in the Program Files directory](https://bugzilla.mozilla.org/show_bug.cgi?id=1643199).
It passes the MMS pretty much the same arguments that it was passed, which will
be passed on to the elevated updater. The MMS does parse these arguments because
it needs several of them to safely and successfully launch the elevated updater.
It passes the arguments on to the elevated updater unchanged.

Even for multiple installations of Firefox, we only install a single MMS. In
order to make sure it is accessible by 32-bit installations as well, we always
install into the 32-bit install directory (`Program Files (x86)`).

## Update Binary

This is a standalone binary. Most of the source for this lives in
`toolkit/mozapps/update/updater/updater.cpp`. This is the program that is
responsible for performing staging, replace requests, and non-staged updates.

Staging is very similar to doing a non-staged update, except that we effectively
install a separate copy of the installation into a temporary directory.

A replace request takes place after staging and simply involves deleting the
original installation and swapping it with the contents of the `updated`
directory.

A non-staged update (also known as an "in place update") involves copying files
out of the MAR and potentially patching existing files with patches stored in
the MAR. Some files may also be deleted. The updater tracks each change made
and, if an error is encountered, can roll all the changes back to restore the
installation to its original state.

Applying a MAR, whether staging or not, runs in four phases, named after the
tokens that they log in the update log: `PREPARE`, `DRAFT`, `EXECUTE` and
`FINISH`. `DRAFT` exists to limit
how much there is to roll back: it writes the new contents of every file that a
non-staged update produces to a `.moz-draft` file next to the destination,
without replacing any installed file. Only once every one of those succeeded
does `EXECUTE` proceed to actual replacement, by renaming any existing old
version to a `.moz-backup` file and renaming the new draft into place. `FINISH`
then discards the backups, or restores them if anything failed. So a failure
while decompressing the MAR or while applying a patch never replaces an
installed file, and a rollback only ever has to undo a series of renames. Note
that `DRAFT` still writes into the installation directory, and that it creates
the parent directories of added files there, which a failure does not remove.
Staged updates don't need any of this: they write into their own directory
during `EXECUTE`, where nothing is in use, and have nothing to do during
`DRAFT`.

The update process often requires elevation. Elevating is only supported on
macOS and Windows. The macOS process is documented [here](MacElevatedUpdate.md).
The Windows process involves:

1. Attempting to create and open a file in the installation directory to test
   if elevation is needed.
2. If the file cannot be opened, several checks are performed to see if the
   [Maintenance Service](#mozilla-maintenance-service) can be used. If it
   cannot, elevation is obtained via a User Account Control (UAC) prompt. When
   staging, we exit with failure rather than use the UAC prompt as we don't want
   to show one when the user is not expecting it.
3. A new, elevated process is created which does the actual update. It is very
   important, for security reasons, not to attempt to write to any non-secured
   areas with this extra permissions as filesystem links can cause us to be
   tricked into overwriting important files. Thus, status information is written
   into the installation directory for the Maintenance Service.
4. The elevated update process exits.
5. The unelevated update process continues running and goes to look for status
   information in the Maintenance Service install directory. It copies this back
   to the [update directory](Concepts.md#update-directory).

Except when staging, there may be a post update process that is run before the
updater exits. Currently, we only do this on Windows. The Windows post update
process involves running the updated uninstaller binary with the `/PostUpdate`
argument. Note that during an elevated Windows update, we run the post update
process twice. Once with elevation and once without it. The reasons for the post
update process are (a) so that we can do some things that the installer does
(such as changing registry keys around to reflect our current configuration)
without needing to have separate NSIS and C++ copies of that code and (b) so
that we can run code from the updated version of Firefox, allowing the update
process to perform actions that the old version of the updater wasn't aware of.

When the update is complete (successfully or otherwise), Firefox is re-launched
with its original arguments (which it passes to the update binary when invoking
it).

## Update Driver

The update driver is the part of Firefox that invokes the update binary in order
to stage or install an update. The update driver's functionality is declared in
`toolkit/xre/nsUpdateDriver.h` and implements `nsIUpdateProcessor` in order to
be accessible from Javascript. It is invoked from the update service where it is
used to stage updates and from `toolkit/xre/nsAppRunner.cpp` to install an
update on Firefox startup.

## Update Checker

The update checker has the interface `nsIUpdateChecker` and is implemented by
`CheckerService` in `toolkit/mozapps/update/UpdateService.sys.mjs`. This
interface allows foreground or background checks (foreground checks set the
[force parameter](Concepts.md#force-parameter)). The returned result will
resolve with an object containing an array of updates that were found. Using
this interface does not cause an update to be downloaded automatically.

## Update Manager

The update manager lives in `toolkit/mozapps/update/UpdateService.sys.mjs` and
keeps track of the various `Update` objects. This includes the update history,
`downloadingUpdate`, and `readyUpdate`. In-progress updates are stored in
`downloadingUpdate` while they download and are moved into `readyUpdate` when
downloading completes. Similarly, the downloading and ready updates are stored
in different subdirectories of the
[update directory](Concepts.md#update-directory). This allows there to
potentially be a separate downloading and ready update, simultaneously. When
updates complete (successfully or otherwise), they are moved to the update
history.

## Update Listener

The update listener lives in `toolkit/mozapps/update/UpdateListener.sys.mjs`. It
listens for update status notifications and shows UI to the user, generally
prompting the user to take action. When an update is ready to be downloaded, but
the user has
[set the updater to require permission before downloading](HowToDisable.md#update-only-with-user-permission),
it asks for this permission. When an update is ready to be installed, it prompts
the user to restart the browser. And when there is a persistent update failure,
it prompts the user to download a new installer so that they can update that
way.

Notifications can be shown in two different ways: as a badge or as a doorhanger.
The badge shows an update badge icon in the corner of the hamburger menu
button and adds an entry to the hamburger menu asking the user to restart to
update. The doorhanger is a small popup notification anchored to the hamburger
menu that the user can accept or dismiss. If it is dismissed, it effectively
turns into a badge notification.

The notification prompting the user to restart is generally not shown
immediately. There is a configurable "Badge Wait Time" and "Prompt Wait Time"
that dictates how long we wait to show the badge and the doorhanger,
respectively. Both can be controlled by prefs: `app.update.badgeWaitTime` and
`app.update.promptWaitTime`, respectively. The prompt wait time can also be
set via the `Update` object sent by Balrog in the
[update XML](Concepts.md#update-xmls). It isn't really useful to have a
badge wait time that is longer than the prompt wait time. If it is greater,
the badge and the prompt are shown at the same time.

## Update Service

The `UpdateService` lives in `toolkit/mozapps/update/UpdateService.sys.mjs` and
is the heart of the in-app half of the Application Update system (with the
[Update Binary](#update-binary) being the out-of-app half). It handles
downloading and staging updates, staging [langpacks](Concepts.md#langpacks),
the post update process, [state management](Concepts.md#update-state),
[update mutex management](Concepts.md#update-mutex-and-update-sync-manager), and
error handling. When using the update UI, `AppUpdater` takes care of certain parts of the
process, calling into the update checker directly, for example, but the update
service still manages the download. Timer-initiated
[background updates](Concepts.md#background-update), however, are entirely
handled by the update service.

Shortly after Firefox launches, we initialize the `UpdateServiceStub`. Its
purpose is entirely to defer launching the entirety of
`toolkit/mozapps/update/UpdateService.sys.mjs` early in startup, if possible.
However, it does some very basic analysis of the current state to check to see
if an update is in-progress or has just completed. In these cases, the stub
launches the update service earlier than usual.

When the update service initializes, it reads `update.status` and
`active-update.xml` in order to figure out where the update process left off the
last time Firefox ran. If the state is inconsistent, it is deleted and we
restart the update process from the beginning. If a download was in-progress, it
is resumed. If a completed update is found, we move it to the update history. If
it completed successfully, we provide some information during Firefox startup
so that the "What's New" page can be displayed. If it failed, we try to fall
back, potentially downloading and installing a different update MAR.

The update download and staging process generally follows this flow:

1. Call the `selectUpdate` method of the update service to pick an update to
   install.
2. Call the `downloadUpdate` method of the update service.
3. The update service creates a `Downloader` instance and registers to be
   notified of download updates.
4. Eventually the download completes and `Downloader.onStopRequest` is called.
   If we are not able to stage, the [AUS state](Concepts.md#update-state) is set
   to `STATE_PENDING` and we wait for the user to restart to install the update.
   If we are able to stage, the steps below will additionally be followed.
5. `Downloader.onStopRequest` calls into the [Update Driver](#update-driver)
   to initiate staging.
6. The update driver waits for the updater to complete with staging, then calls
   the update service's `refreshUpdateStatus` method.
7. On staging failure, `refreshUpdateStatus` handles error fallbacks. On
   success, the AUS state is set to `STATE_PENDING` and we wait for the user to
   restart to install the update.

When in `STATE_PENDING`, the update service can download additional updates.
This follows pretty much the same process as downloading the first update, but
with a few changes:

- Neither the AUS state nor `update.status` will reflect that we are downloading
  an update.
- If we restart Firefox while the download is in-progress, we will install the
  ready update and discard the downloading update.
- When the download completes, the AUS state will briefly change to `STATE_SWAP`
  while we are moving the downloading files into the ready update directory.
  If we can stage, the state will then switch to `STATE_STAGING`. Either way, it
  will then change back to `STATE_PENDING`.

## AppUpdater

`AppUpdater` in `toolkit/mozapps/update/AppUpdater.sys.mjs` is an interface for
running the update process interactively. It is used by Firefox's Application
Update User Interfaces and also by the
[Background Update Task](Concepts.md#background-update). The entry point is
always the `check` method, regardless of what state update is currently in. When
`check` is called, `AppUpdater` will look for and track the progress of an
existing update, if there is one. If there isn't, it will perform an update
check and, if configured to do so, automatically start downloading it.
`AppUpdater`'s progress and state can be monitored with the `addListener` method
and by examining the `update` property.

`AppUpdater` provides the ability to shut itself down quickly and cleanly,
regardless of what update is doing. This can be slightly tricky because some of
the promises that it uses only resolve when a large download completes or when
the user accepts an update download (which they may not do this session). To
make this easier, promises are typically wrapped in an `AbortablePromise` via
`makeAbortable`, which keeps track of all running promises and provides an
`abort` method for each to force it to reject immediately. These can be invoked
by calling `AppUpdater`'s `stop` method. Note that doing this only stops an
in-progress update if it is called before the update starts downloading. After
that, the entire process has been handed off to the update service and will
continue without any further interaction needed from `AppUpdater`.

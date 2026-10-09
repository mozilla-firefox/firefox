# Troubleshooting

This page will attempt to help you to determine the cause of a problem with
Firefox Application Update. This is both geared towards both Mozilla employees
triaging incoming Application Update bugs and towards users who want to figure
out for themselves why they are experiencing problems and how they might fix
them.

In order to make things a bit easier for employees doing triage, some sets of
troubleshooting steps will be formatted as code blocks to make it easier to
copy and paste them into a bug comment.

In general, the first step of the troubleshooting process is going to be
figuring out which stage of the update process is causing problems. This is
easiest with a solid understanding of the
[implementation of the update system](Implementation.md). Then you can figure
out how far through the update process we are making it successfully. With this
knowledge, we can figure out the correct logs to collect. If you are unsure,
start by collecting the Browser Console logs.

## Error Lookup

It is very common during troubleshooting to see errors in the status file or in
log messages. Since these errors are numeric, we need to translate them into
something more useful. This can be done using
`toolkit/mozapps/update/common/updatererrors.h`.

## Log File Analysis

Let's will walk through logs for a successful update of a copy of Firefox
Nightly for Windows in order to give a sense of what things are meant to look
like.

### Browser Console Logs

Browser console logs help us find problems that happened when downloading the
update and preparing it for the update binary. They can be collected like this:

```
 1. Navigate to `about:config`.
 2. Set `app.update.log` to `true`.
 3. Open the Browser Console either with the hotkey Control+Shift+J (Command+Shift+J on macOS), or via Hamburger Menu (☰) -> More Tools -> Browser Console
 4. In the Filter textbox at the top, enter `AUS:` to filter out everything except the update messages.
 5. Navigate to the "Update" section of `about:preferences`. It should automatically check for an update.
 6. Once the update check has completed, copy the messages out of the Browser Console and attach them to this bug.
```

We'll go through some things to look for when reading a browser console log.

It can be good to read through the initial status information:

```
AUS:SVC Logging current UpdateService status:
AUS:SVC UpdateService.canUsuallyCheckForUpdates - able to check for updates
AUS:SVC UpdateService.canCheckForUpdates - able to check for updates
AUS:SVC getCanApplyUpdates - testing write access C:\ProgramData\Mozilla-1de4eec8-1241-4177-a864-e594e8d1fb38\updates\6F193CCC56814779\update.test
AUS:SVC getCanApplyUpdates - bypass the write since elevation can be used on macOS and Windows
AUS:SVC isServiceInstalled - returning true
AUS:SVC shouldUseService - returning true
AUS:SVC getCanStageUpdates - able to stage updates using the service
AUS:SVC Elevation required: false
AUS:SVC Other instance of the application currently running: false
```

It looks like we are able to check for, apply, and stage updates. It also looks
like we can use the Mozilla Maintenance Service. Note that the line saying that
`Elevation required: false` is performing a macOS-only check. Also, the fact
that no other instances of the application are currently running is important
since they
[will interfere with the update process](Concepts.md#update-mutex-and-update-sync-manager).

After this, we should find log lines telling us whether Firefox started with an
existing `downloadingUpdate` or `readyUpdate`. In this case, we did not.

```
AUS:SVC UpdateManager:#reload - Reloaded downloadingUpdate as null
AUS:SVC UpdateManager:#reload - Reloaded readyUpdate as null
```

We can also see what the initial value of `update.status` was.

```
AUS:SVC UpdateService:#asyncInit - status = "null"
```

It may be useful to trace the entire `#asyncInit` flow to get a sense of what
the overall state is after initialization. In this example, the only other
initialization is

```
AUS:SVC UpdateService:#asyncInit - Resetting update state
```

This means that Firefox will not use any state from previous update attempts. It
is going to start fresh.

Now we will observe the update check being made.

```
AUS:SVC CheckerService:#updateCheck - sending request to: https://aus5.mozilla.org/update/6/Firefox/148.0a1/20251209143057/WINNT_x86_64-msvc-x64/en-US/nightly/Windows_NT%252010.0.0.0.26100.7171%2520(x64)/ISET%3ASSE4_2%2CMEM%3A32213/default/default/update.xml?force=1
```

At the time of writing this, pasting this URL into the browser yields this XML:

```XML
<updates>
  <update type="minor" displayVersion="148.0a1" appVersion="148.0a1"
          platformVersion="148.0a1" buildID="20251210095635">
    <patch type="complete"
           URL="https://archive.mozilla.org/pub/firefox/nightly/2025/12/2025-12-10-09-56-35-mozilla-central/firefox-148.0a1.en-US.win64.complete.mar"
           hashFunction="sha512"
           hashValue="0234e8875cd12eb0f87dc76709a4869043082efbe5067f1fae138517e5c5a524ab6013c7331526f86322c19acefa4a04ba4d7edda05608233294bdef3867dd46"
           size="94587463"/>
    <patch type="partial"
           URL="https://archive.mozilla.org/pub/firefox/nightly/partials/2025/12/2025-12-10-09-56-35-mozilla-central/firefox-mozilla-central-148.0a1-win64-en-US-20251209143057-20251210095635.partial.mar"
           hashFunction="sha512"
           hashValue="6d98fb6934274ce5cd6a6f22ee50145d889e9d59d2989f9633f26fef6cd0817f3f9bd0d5e3da3a81bd93ef8ca05754ee50a568f2c364f011093cfd348b061405"
           size="10738378"/>
  </update>
</updates>
```

Given this, we can determine that the result of this update check ought to be a
single update with two possible patches.

Next we see that Firefox is going to try to download this update.

```
AUS:SVC Creating Downloader
AUS:SVC Downloader:downloadUpdate
AUS:SVC Downloader:_selectPatch - Patch selected. Assigning update to downloadingUpdate.
AUS:SVC getCanUseBits - BITS can be used to download updates
AUS:SVC Downloader:_canUseBits - Patch is able to use BITS download
AUS:SVC Downloader:_maybeWithExtras - Adding extras
AUS:SVC UpdateService:makeBitsRequest - Starting BITS download with url: https://archive.mozilla.org/pub/firefox/nightly/partials/2025/12/2025-12-10-09-56-35-mozilla-central/firefox-mozilla-central-148.0a1-win64-en-US-20251209143057-20251210095635.partial.mar?backgroundTaskMode=0, updateDir: C:\ProgramData\Mozilla-1de4eec8-1241-4177-a864-e594e8d1fb38\updates\6F193CCC56814779\updates\downloading, filename: update.mar
AUS:SVC Downloader:downloadUpdate - BITS download running. BITS ID: {56F3868B-4CE2-40A5-8937-A3AA44D33D3F}
```

It looks like Firefox is going to use [BITS](Concepts.md#bits) to download this
update. If we wanted to, we could open Powershell while this download was
in-progress and run
`Get-BitsTransfer -JobId "{56F3868B-4CE2-40A5-8937-A3AA44D33D3F}"` to
investigate the status of the transfer.

Next, we see a [state change](Concepts.md#update-state):

```
AUS:SVC writeStatusFile - status: downloading, path: C:\ProgramData\Mozilla-1de4eec8-1241-4177-a864-e594e8d1fb38\updates\6F193CCC56814779\updates\0\update.status
AUS:SVC onStateAccessSuccess
AUS:SVC Downloader:downloadUpdate - Set status to downloading
AUS:SVC Downloader:downloadUpdate - Setting state to downloading
AUS:SVC Downloader:downloadUpdate - setting currentState to STATE_DOWNLOADING
AUS:SVC transitionState - "STATE_IDLE" -> "STATE_DOWNLOADING".
```

This indicates to us that Firefox believes it has successfully started
downloading the update.

Once we see `onStopRequest`, the download has completed.

```
AUS:SVC Downloader:onStopRequest - downloader: BITS, status: 0
AUS:SVC Downloader:onStopRequest - status: 0, current fail: 0, max fail: 10, retryTimeout: 2000
```

The `0` status indicates that the download completed successfully. Great! Let's
see what state we transition to from here:

```
AUS:SVC Downloader:onStopRequest - Clearing readyUpdate in preparation of moving downloadingUpdate into readyUpdate.
AUS:SVC isServiceInstalled - returning true
AUS:SVC shouldUseService - returning true
AUS:SVC isServiceInstalled - returning true
AUS:SVC shouldUseService - returning true
AUS:SVC getCanStageUpdates - able to stage updates using the service
AUS:SVC Downloader:onStopRequest - Ready to apply. Setting state to "pending-service".
AUS:SVC writeStatusFile - status: pending-service, path: C:\ProgramData\Mozilla-1de4eec8-1241-4177-a864-e594e8d1fb38\updates\6F193CCC56814779\updates\0\update.status
AUS:SVC onStateAccessSuccess
AUS:SVC Downloader:onStopRequest - Setting bitsResult to 0
AUS:SVC Downloader:onStopRequest - setting state to: pending-service
AUS:SVC Downloader:onStopRequest - Moving downloadingUpdate into readyUpdate
AUS:AUM AppUpdater:#awaitDownloadComplete.observer.onStopRequest - aRequest: [xpconnect wrapped nsIRequest], aStatusCode: 0
...
AUS:SVC transitionState - "STATE_DOWNLOADING" -> "STATE_STAGING".
```

Since we got `pending-service` and `STATE_STAGING` next, it looks like we can
use the Maintenance Service to [stage the update](Concepts.md#staging).

```
AUS:SVC UpdateManager:refreshUpdateStatus - Staging done.
AUS:SVC readStatusFile - status: applied, path: C:\ProgramData\Mozilla-1de4eec8-1241-4177-a864-e594e8d1fb38\updates\6F193CCC56814779\updates\0\update.status
AUS:SVC UpdateManager:refreshUpdateStatus - status = applied
```

`refreshUpdateStatus` is called when staging is complete. And the `applied`
status indicates that it completed successfully.

```
AUS:SVC isServiceInstalled - returning true
AUS:SVC shouldUseService - returning true
AUS:SVC UpdateManager:refreshUpdateStatus - Staging successful. Setting status to "applied-service"
...
AUS:SVC transitionState - "STATE_STAGING" -> "STATE_PENDING".
```

We are able to use the Maintenance Service for the last bit of the installation:
the replace request, so we set the status to `applied-service`. Since we are now
ready to restart to update, the AUS state is set to `STATE_PENDING`.

At this point, Firefox is ready to restart to install the update.

### Update Binary Logs

Update binary logs can help find problems that occurred during the actual
installation of an update. They can be collected like this:

```
 1. Navigate to `about:support`
 2. Find the "Update Folder" entry and click "Open Folder".
 3. Open the `updates` directory.
 4. Inside, you should find some `.log` files: `last-update.log`, `last-update-elevated.log`, `backup-update.log`, `backup-update-elevated.log`. They may not all exist. Please attach whichever ones do exist to this bug.
```

When the update binary runs, it initially logs to `updates/0/update.log`. If it
needs to invoke a [second updater](Implementation.md#update-binary) for
elevation, that log will end up at `updates/0/update-elevated.log`. When
Firefox runs and analyzes the results of the update, it will move these logs to
`updates/last-update.log` and `updates/last-update-elevated.log`, respectively.
If those files already exist, they are moved to `updates/backup-update.log` and
`updates/backup-update-elevated.log`, respectively. Since most people check for
logs after restarting Firefox to update and since staging is enabled by default,
it is common that the `backup-update` logs contain the log messages from staging
and the `last-update` logs contain the log messages from the actual
installation.

Note that lines in the update logs are prefixed with timestamps. This can be
helpful to find out if the update stalled somewhere. But it is also worth taking
note of them just to make sure that the timing makes sense. Make sure that, for
example, if the bug reporter says that this problem happened yesterday, that the
logs aren't from months ago. This could point towards the problem happening in
Firefox rather than in the update binary. If you see this, take another look at
the browser console logs.

#### Backup Update Log

Let's look at `backup-update.log` first. Updater logs should generally have a
section like this early on that does a good job of describing the way that the
updater was invoked:

```
2025-12-10 09:13:47-0800: sUsingService=false
2025-12-10 09:13:47-0800: sUpdateSilently=false
2025-12-10 09:13:47-0800: useService=true
2025-12-10 09:13:47-0800: isElevated=false
2025-12-10 09:13:47-0800: gInvocation=UpdaterInvocation::First
```

From this we can tell that we are allowed to use the Maintenance Service, but
are not using it yet. The updater does not appear to have elevation, which
makes sense given that this is the first invocation; we have not yet started the
elevated copy of the updater.

```
2025-12-10 09:13:47-0800: Performing a staged update
```

This confirms that `backup-update.log` describes the staging process.

```
2025-12-10 09:13:47-0800: Checking whether elevation is needed
2025-12-10 09:13:47-0800: Failed to open update lock file: 5
2025-12-10 09:13:47-0800: Can't open lock file - seems like we need elevation
2025-12-10 09:13:47-0800: After checking IsProgramFilesPath, useService=true
2025-12-10 09:13:47-0800: After checking IsLocalFile, useService=true
2025-12-10 09:13:47-0800: After checking IsUnpromptedElevation, useService=true
2025-12-10 09:13:47-0800: Writing status to file: failed: 58
```

Since we couldn't open the update lock file, the updater needs elevation. The
checks afterwards show that there is nothing stopping us from using the
Maintenance Service. We then see what looks like an error. But if we
[look it up](#error-lookup), we see that `58` corresponds to
`SERVICE_UPDATE_STATUS_UNCHANGED`. This is normal; we set this status as part of
our error detection that the elevated updater process worked properly.

```
2025-12-10 09:13:47-0800: Launched service successfully
2025-12-10 09:13:56-0800: Service stop detected.
2025-12-10 09:13:56-0800: Not showing a UAC prompt.
2025-12-10 09:13:56-0800: useService=true
```

This indicates that we launched the service and it ran successfully.

#### Backup Update Elevated Log

Next, let's look at the `backup-update-elevated.log` that pairs with the above
log. This should be the elevated portion of the staging from that process.

```
2025-12-10 09:13:47-0800: sUsingService=true
2025-12-10 09:13:47-0800: sUpdateSilently=false
2025-12-10 09:13:47-0800: useService=false
2025-12-10 09:13:47-0800: isElevated=true
2025-12-10 09:13:47-0800: gInvocation=UpdaterInvocation::Second
```

This looks expected. We are using the service on our second invocation of the
updater.

```
2025-12-10 09:13:47-0800: Going to update via this updater instance.
```

Great! We are going to do the actual staging now. This should be followed by
many `PREPARE` statements, then many `DRAFT` statements, then many `EXECUTE`
statements, then many `FINISH` statements showing the progress of the update.
The `DRAFT` statements do nothing here: only non-staged updates write draft
files, but the statements are logged either way so that staged and non-staged
updater tests can compare against the same expected update logs. Look through
all of these for any "failed" messages indicating a failure of the update
process. If there is one, we should immediately move to the `FINISH` phase
which, in this case, will involve rolling the installation back to its original
state.

```
2025-12-10 09:13:55-0800: succeeded
2025-12-10 09:13:55-0800: Writing status to file: applied
```

Here we see that the update was successful and we updated the status file
accordingly. This matches what we saw in the browser console log.

```
2025-12-10 09:13:55-0800: Running LaunchCallbackAndPostProcessApps
2025-12-10 09:13:55-0800: No callback arg. Skipping LaunchWinPostProcess and LaunchCallbackApp
```

We didn't re-launch Firefox after the update. Which is expected when staging.

#### Last Update Log

Now we are going to look at a `last-update.log` where we are not staging.

Once again, we'll look at the block at the top to see what state we are in:

```
2025-12-10 11:16:13-0800: sUsingService=false
2025-12-10 11:16:13-0800: sUpdateSilently=false
2025-12-10 11:16:13-0800: useService=true
2025-12-10 11:16:13-0800: isElevated=false
2025-12-10 11:16:13-0800: gInvocation=UpdaterInvocation::First
```

Note that there is no particular message to indicate an in-place update. But we
can tell from the lack of `Performing a staged update` and
`Performing a replace request` messages that this is an in-place update.

Much of the analysis from here goes pretty much the same way it did for the
[Backup Update Log](#backup-update-log). But towards the end we get this
difference:

```
2025-12-10 11:16:25-0800: Running LaunchWinPostProcess
2025-12-10 11:16:25-0800: LaunchWinPostProcess - Waiting for process to complete
2025-12-10 11:16:27-0800: LaunchWinPostProcess - Process completed
```

This means that we launched the Post Update process (described
[here](Implementation.md#update-binary)) successfully. This ought to be done
during an in-place update or a replace request, but not when staging.

#### Last Update Elevated Log

Next, let's look at the `last-update-elevated.log` that pairs with the above
log. This should be the elevated portion of the staging from that process.

Most of this should match closely with the
[Backup Update Elevated Log](#backup-update-elevated-log) that we already looked
at. But some parts towards the end will differ

```
2025-12-10 11:16:22-0800: NS_main: unable to remove directory: tobedeleted, err: 41
2025-12-10 11:16:22-0800: NS_main: directory will be removed on OS reboot: tobedeleted
```

This is normal during an in-place update. We will still be using some parts of
the installation (like the updater) that we can't delete yet. But we register
them with the OS to be removed on the next boot.

```
2025-12-10 11:16:22-0800: Running LaunchCallbackAndPostProcessApps
2025-12-10 11:16:22-0800: Launching Windows post update process
2025-12-10 11:16:22-0800: The file "C:\Program Files\Firefox Nightly\uninstall\helper.exe" is signed and the signature was verified.
2025-12-10 11:16:22-0800: LaunchWinPostProcess - Waiting for process to complete
2025-12-10 11:16:25-0800: LaunchWinPostProcess - Process completed
```

Like in the previous section, we now do the post update process, which should
be completed in both the unelevated and elevated updater.

```
2025-12-10 11:16:25-0800: Not starting service update. MMS will handle it.
```

When we update with the Maintenance Service, the Service handles updating
itself. But if we update without it, we may install an update to the Service at
this point.

```
2025-12-10 11:16:25-0800: LaunchCallbackAndPostProcessApps:3237 - Returning early. This is the second updater instance.
```

We don't launch the callback from the elevated updater, we let the unelevated
updater do that after this one exits.

## Common Classes of Problems

### Firefox Doesn't Update

Troubleshooting this generally involves some knowledge of how the update system
works. This allows us to determine where in the update process the failure is
happening. Depending on where the failure is, we will probably want to ask for
and analyze update logs. Potentially we may also want to look at the active
update (`active-update.xml`) or the update history `updates.xml`. Especially
look for [errors](#error-lookup) in the update logs that might shed light on
what is going on.

#### Permissions Problems

A tricky variation of this problem is when the permissions of the Firefox
installation directory are wrong. An example of this is
[Bug 1702276](https://bugzilla.mozilla.org/show_bug.cgi?id=1702276). This will
generally present as an update log that suggests that we should have the
permission to update, but attempts to actually write to the files result in
Windows Error 5
([Access Denied](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-)).
A good way to check for this is to ask the reporter:

```
Run this command, substituting your install directory: `icacls "<installdir>"`. For example, I would run `icacls "C:\Program Files\Firefox Nightly"`. Attach the output of this command to this bug.
```

We expect results extremely similar to these:

```
> icacls /c/Program\ Files/Firefox\ Nightly/
C:/Program Files/Firefox Nightly/
                                  S-1-15-3-1024-1238444810-1356253261-2257478630-1143196962-1563090664-2414759320-1282101916-4218287853:(OI)(CI)(RX)
                                  NT SERVICE\TrustedInstaller:(I)(F)
                                  NT SERVICE\TrustedInstaller:(I)(CI)(IO)(F)
                                  NT AUTHORITY\SYSTEM:(I)(F)
                                  NT AUTHORITY\SYSTEM:(I)(OI)(CI)(IO)(F)
                                  BUILTIN\Administrators:(I)(F)
                                  BUILTIN\Administrators:(I)(OI)(CI)(IO)(F)
                                  BUILTIN\Users:(I)(RX)
                                  BUILTIN\Users:(I)(OI)(CI)(IO)(GR,GE)
                                  CREATOR OWNER:(I)(OI)(CI)(IO)(F)
                                  APPLICATION PACKAGE AUTHORITY\ALL APPLICATION PACKAGES:(I)(RX)
                                  APPLICATION PACKAGE AUTHORITY\ALL APPLICATION PACKAGES:(I)(OI)(CI)(IO)(GR,GE)
                                  APPLICATION PACKAGE AUTHORITY\ALL RESTRICTED APPLICATION PACKAGES:(I)(RX)
                                  APPLICATION PACKAGE AUTHORITY\ALL RESTRICTED APPLICATION PACKAGES:(I)(OI)(CI)(IO)(GR,GE)
```

The line with the long user ID should be expected to have a different user ID.
Other than that, almost any variation from this is a sign for concern.
[The documentation](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/icacls#remarks)
can help decode this output to figure out what it means. The permissions that
will be most important here are `BUILTIN\Users`, which likely corresponds to the
permissions that users have without elevation, `BUILTIN\Administrators`, which
corresponds to the permissions that users have with UAC elevation, and `SYSTEM`,
which corresponds to the permissions that the Mozilla Maintenance Service has.

### Unexpected UAC Prompts / Mozilla Maintenance Service isn't Working

A common reported problem is that update works successfully on Windows, but
requires a User Account Control (UAC) prompt. This means that the
[Mozilla Maintenance Service](Implementation.md#mozilla-maintenance-service)
isn't working properly. The reason that this would happen could lie in the
browser console log or the update binary log. Either the browser console log
will indicate that it didn't set `applied-service` or `pending-service` or the
update binary log will indicate `useService=false` somewhere. This should
provide leads for why the Mozilla Maintenance Service isn't being used.

### Firefox Forces a Restart

These are very likely duplicates of
[Bug 1480452](https://bugzilla.mozilla.org/show_bug.cgi?id=1480452), but we
generally want to be sure about this. We want to know if there is a new
mechanism that can cause this problem.

Occasionally, this is instead something like
[Bug 1892995](https://bugzilla.mozilla.org/show_bug.cgi?id=1892995). In that
example, enterprise software is requesting us to open `about:restartrequired` in
order to encourage the user restart. But the browser actually continues working
since it has not actually been updated.

There is another example that we haven't seen in some time where external
software actually does update Firefox while it is running, causing the page to
be shown accurately. Nonetheless, this type of bug is INVALID since there isn't
really anything we can do to fix this. If some other program deletes our files
while Firefox is running, this is the inevitable result. This problem can be
uncovered by asking for the update history (`updates.xml`) to see if Firefox is
actually installing the problematic updates.

### Channel Changes or Version Downgrades

We go to a rather large amount of effort to prevent channel changes and version
downgrades. We check at multiple stages of the update process that we aren't
doing this. If a user reports this, ask them for a list of enterprise software
that is running on their machine. It typically turns out that some piece of
enterprise software is trying to force Firefox to some particular version and
it is at fault, not Firefox.

Another direction for troubleshooting this is to check the update history
(`updates.xml`). If no entries in the history match these updates, Firefox
probably isn't performing them.

We cannot generally do anything about this from within Firefox, so these are
generally resolved as INVALID.

## Other Useful Copy/Pastes for Bugzilla

For when having a problem specifically with the Maintenance Service itself.
Note that these problems are very unusual. You should check that we are actually
trying to invoke it first.

```
The most recent Maintenance Service logs should be located at `C:\Program Files (x86)\Mozilla Maintenance Service\logs\maintenanceservice.log`. Please attach that file to this bug.
```

Occasionally, it is useful to ask the bug reporter to run code in the browser
console. In which case this may be useful.

```
Enable running commands in the Browser Console by navigating to `about:config` and setting `devtools.chrome.enabled` to `true`.
```

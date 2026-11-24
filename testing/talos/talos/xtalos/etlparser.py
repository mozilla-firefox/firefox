#!/usr/bin/env python

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import csv
import json
import os
import re
import shutil
import subprocess
import sys

from talos.xtalos import xtalos

EVENTNAME_INDEX = 0
PROCESS_INDEX = 2
THREAD_ID_INDEX = 3
PARENT_PID_INDEX = 3
DISKBYTES_COL = "Size"
FNAME_COL = "FileName"
IMAGEFUNC_COL = "Image!Function"
EVENTGUID_COL = "EventGuid"
ACTIVITY_ID_COL = "etw:ActivityId"
NUMBYTES_COL = "NumBytes"
BYTESSENT_COL = "BytesSent"

CEVT_WINDOWS_RESTORED = "{917b96b1-ecad-4dab-a760-8d49027748ae}"
CEVT_XPCOM_SHUTDOWN = "{26d1e091-0ae7-4f49-a554-4214445c505c}"
NAME_SUBSTITUTIONS = [
    # Careful with your regex!
    # Substitution happens after combinations like \t \s \n ... are replaced
    # with their real representations. So, prepend them with extra backslash.
    # Read more: https://docs.python.org/2.7/library/re.html#re.sub
    (re.compile(r"{\w{8}-\w{4}-\w{4}-\w{4}-\w{12}}"), "{uuid}"),
    (re.compile(r"talos\\tests\\tp5n\\.*"), r"talos\\tests\{tp5n_files}"),
    (re.compile(r"nvidia corporation\\3d vision\\.*"), "{nvidia_3d_vision}"),
    (re.compile(r"cltbld\.t-w732-ix-\d+\.\d+"), "{cltbld}"),
    (
        re.compile(r"venv\\lib\\site-packages\\pip\\_vendor\\.*"),
        r"venv\\lib\\site-packages\\{pip_vendor}",
    ),
]
stages = ["startup", "normal", "shutdown"]
net_events = {
    "TcpDataTransferReceive": "recv",
    "UdpEndpointReceiveMessages": "recv",
    "TcpDataTransferSend": "send",
    "UdpEndpointSendMessages": "send",
}
gThreads = {}
gConnectionIDs = {}
gHeaders = {}
gBrowserPID = None


def uploadFile(filename):
    mud = os.environ.get("MOZ_UPLOAD_DIR", None)
    if mud:
        print(f"uploading raw file {filename} via blobber")
        mud_filename = os.path.join(mud, filename)
        shutil.copyfile(filename, f"{mud_filename}.log")


def filterOutHeader(data):
    # -1 means we have not yet found the header
    # 0 means we are in the header
    # 1+ means that we are past the header
    state = -1
    done = False
    while not done:
        try:
            row = next(data)
        except StopIteration:
            done = True
            break
        except UnicodeDecodeError:
            continue
        except csv.Error:
            continue

        if not len(row):
            continue

        if state < 0:
            # Keep looking for the header (denoted by "BeginHeader").
            if row[0] == "BeginHeader":
                state = 0
            continue

        if state == 0:
            # Eventually, we'll find the end (denoted by "EndHeader").
            if row[0] == "EndHeader":
                state = 1
                continue

            gHeaders[row[EVENTNAME_INDEX]] = row
            continue

        if state >= 1:
            state = state + 1

        # The line after "EndHeader" is also not useful, so we want to strip
        # that in addition to the header.
        if state > 2:
            yield row


def getIndex(eventName, *colNames):
    eventHeader = gHeaders[eventName]

    for colName in colNames:
        try:
            return eventHeader.index(colName)
        except ValueError:
            pass

    return None


def readFile(filename):
    data = csv.reader(
        open(filename, encoding="cp1252"),
        delimiter=",",
        quotechar='"',
        skipinitialspace=True,
    )
    data = filterOutHeader(data)
    return data


def fileSummary(row, stage, retVal):
    event = row[EVENTNAME_INDEX]

    # TODO: do we care about the other events?
    if event not in ("FileIoRead", "FileIoWrite"):
        return
    fname_index = getIndex(event, FNAME_COL)

    # We only care about events that have a file name.
    if fname_index is None:
        return

    # Some data rows are missing the filename?
    if len(row) <= fname_index:
        return

    thread_extra = ""
    if gThreads[row[THREAD_ID_INDEX]] == "main":
        thread_extra = " (main)"
    key_tuple = (
        row[fname_index],
        f"{row[THREAD_ID_INDEX]}{thread_extra}",
        stages[stage],
    )
    total_tuple = (
        row[fname_index],
        f"{row[THREAD_ID_INDEX]}{thread_extra}",
        "all",
    )

    if key_tuple not in retVal:
        retVal[key_tuple] = {
            "DiskReadBytes": 0,
            "DiskReadCount": 0,
            "DiskWriteBytes": 0,
            "DiskWriteCount": 0,
        }
        retVal[total_tuple] = retVal[key_tuple]

    if event == "FileIoRead":
        retVal[key_tuple]["DiskReadCount"] += 1
        retVal[total_tuple]["DiskReadCount"] += 1
        idx = getIndex(event, DISKBYTES_COL)
        retVal[key_tuple]["DiskReadBytes"] += int(row[idx], 16)
        retVal[total_tuple]["DiskReadBytes"] += int(row[idx], 16)
    elif event == "FileIoWrite":
        retVal[key_tuple]["DiskWriteCount"] += 1
        retVal[total_tuple]["DiskWriteCount"] += 1
        idx = getIndex(event, DISKBYTES_COL)
        retVal[key_tuple]["DiskWriteBytes"] += int(row[idx], 16)
        retVal[total_tuple]["DiskWriteBytes"] += int(row[idx], 16)


def etl2csv(xperf_path, etl_filename, debug=False):
    """
    Convert etl_filename to etl_filename.csv (temp file) which is the .csv
    representation of the .etl file Etlparser will read this .csv and parse
    the information we care about into the final output. This is done to keep
    things simple and to preserve resources on talos machines
    (large files == high memory + cpu)
    """

    xperf_cmd = [
        xperf_path,
        "-merge",
        f"{etl_filename}.user",
        f"{etl_filename}.kernel",
        etl_filename,
    ]
    if debug:
        print(f"executing '{subprocess.list2cmdline(xperf_cmd)}'")
    subprocess.call(xperf_cmd)

    csv_filename = f"{etl_filename}.csv"
    xperf_cmd = [xperf_path, "-i", etl_filename, "-o", csv_filename]
    if debug:
        print(f"executing '{subprocess.list2cmdline(xperf_cmd)}'")
    subprocess.call(xperf_cmd)
    return csv_filename


def trackProcess(row, firstFirefoxPID):
    global gBrowserPID
    if gBrowserPID:
        return

    # Without the launcher, the initial Firefox process *is* the browser
    # process. OTOH, with the launcher process enabled, the browser is actually
    # the first child process of the first Firefox process.
    parentPID = int(row[PARENT_PID_INDEX])
    if parentPID == firstFirefoxPID:
        proc = row[PROCESS_INDEX]
        gBrowserPID = int(re.search(r"^.* \(\s*(\d+)\)$", proc).group(1))


def getBrowserPID():
    return gBrowserPID


def trackThread(row, browserPID):
    event, proc, tid = row[EVENTNAME_INDEX], row[PROCESS_INDEX], row[THREAD_ID_INDEX]
    if event in ["T-DCStart", "T-Start"]:
        procName, procID = re.search(r"^(.*) \(\s*(\d+)\)$", proc).group(1, 2)
        if procID == str(browserPID):
            imgIdx = getIndex(event, IMAGEFUNC_COL)
            img = re.match("([^!]+)!", row[imgIdx]).group(1)
            if img == procName:
                gThreads[tid] = "main"
            else:
                gThreads[tid] = "nonmain"
    elif event in ["T-DCEnd", "T-End"] and tid in gThreads:
        del gThreads[tid]


def trackThreadFileIO(row, io, stage):
    event, tid = row[EVENTNAME_INDEX], row[THREAD_ID_INDEX]
    opType = {"FileIoWrite": "write", "FileIoRead": "read"}[event]
    th, stg = gThreads[tid], stages[stage]
    sizeIdx = getIndex(event, DISKBYTES_COL)
    bytes = int(row[sizeIdx], 16)
    io[(th, stg, f"file_{opType}_ops")] = io.get((th, stg, f"file_{opType}_ops"), 0) + 1
    io[(th, stg, f"file_{opType}_bytes")] = (
        io.get((th, stg, f"file_{opType}_bytes"), 0) + bytes
    )
    io[(th, stg, "file_io_bytes")] = io.get((th, stg, "file_io_bytes"), 0) + bytes


def trackThreadNetIO(row, io, stage):
    event, tid = row[EVENTNAME_INDEX], row[THREAD_ID_INDEX]
    connIdIdx = getIndex(event, ACTIVITY_ID_COL)
    connID = row[connIdIdx]
    if connID not in gConnectionIDs:
        gConnectionIDs[connID] = tid
    origThread = gConnectionIDs[connID]
    if origThread in gThreads:
        match = re.match(r"[\w-]+\/([\w-]+)?", event)
        if not match:
            raise xtalos.XTalosError(
                f"Could not find a regular expression match for event: {event}"
            )
        netEvt = match.group(1)

        if netEvt in net_events:
            opType = net_events[netEvt]
            th, stg = gThreads[origThread], stages[stage]
            # On newer versions of Windows, some net I/O events have switched to
            # using BYTESSENT_COL, so we try both
            lenIdx = getIndex(event, NUMBYTES_COL, BYTESSENT_COL)
            bytes = int(row[lenIdx])
            io[(th, stg, f"net_{opType}_bytes")] = (
                io.get((th, stg, f"net_{opType}_bytes"), 0) + bytes
            )
            io[(th, stg, "net_io_bytes")] = io.get((th, stg, "net_io_bytes"), 0) + bytes


def updateStage(row, stage):
    guidIdx = getIndex(row[EVENTNAME_INDEX], EVENTGUID_COL)
    if row[guidIdx] == CEVT_WINDOWS_RESTORED and stage == 0:
        stage = 1
    elif row[guidIdx] == CEVT_XPCOM_SHUTDOWN and stage == 1:
        stage = 2
    return stage


def loadAllowlist(filename):
    if not filename:
        return
    if not os.path.exists(filename):
        print(f"Warning: xperf allowlist {filename} was not found")
        return
    lines = open(filename).readlines()
    # Expand paths
    lines = [os.path.expandvars(elem.strip()) for elem in lines]
    files = set()
    dirs = set()
    recur = set()
    for line in lines:
        if line.startswith("#"):
            continue
        elif line.endswith("\\*\\*"):
            recur.add(line[:-4])
        elif line.endswith("\\*"):
            dirs.add(line[:-2])
        else:
            files.add(line)
    return (files, dirs, recur)


def checkAllowlist(filename, allowlist):
    if not allowlist:
        return False
    if filename in allowlist[0]:
        return True
    if os.path.dirname(filename) in allowlist[1]:
        return True
    head = filename
    while len(head) > 3:  # Length 3 implies root directory, e.g. C:\
        head, tail = os.path.split(head)
        if head in allowlist[2]:
            return True
    return False


def etlparser(
    xperf_path,
    etl_filename,
    processID,
    approot=None,
    configFile=None,
    outputFile=None,
    allowlist_file=None,
    error_filename=None,
    all_stages=False,
    all_threads=False,
    debug=False,
):

    # setup output file
    if outputFile:
        outFile = open(outputFile, "w")
    else:
        outFile = sys.stdout

    files = {}
    io = {}
    stage = 0

    csvname = etl2csv(xperf_path, etl_filename, debug=debug)
    for row in readFile(csvname):
        event = row[EVENTNAME_INDEX]
        if event == "P-Start":
            trackProcess(row, processID)
        elif event in ["T-DCStart", "T-Start", "T-DCEnd", "T-End"]:
            trackThread(row, getBrowserPID())
        elif (
            event in ["FileIoRead", "FileIoWrite"] and row[THREAD_ID_INDEX] in gThreads
        ):
            fileSummary(row, stage, files)
            trackThreadFileIO(row, io, stage)
        elif event.endswith("Event/Classic") and row[THREAD_ID_INDEX] in gThreads:
            stage = updateStage(row, stage)
        elif event.startswith("Microsoft-Windows-TCPIP"):
            trackThreadNetIO(row, io, stage)

    if debug:
        uploadFile(csvname)

    output = "thread, stage, counter, value\n"
    for cntr in sorted(io.keys()):
        output += f"{', '.join(cntr)}, {io[cntr]}\n"
    if outputFile:
        base, ext = os.path.splitext(outputFile)
        fname = f"{base}_thread_stats{ext}"
        with open(fname, "w") as f:
            f.write(output)

        if debug:
            uploadFile(fname)
    else:
        print(output)

    allowlist = loadAllowlist(allowlist_file)

    header = "filename, tid, stage, readcount, readbytes, writecount, writebytes"
    outFile.write(header + "\n")

    # Filter out stages, threads, and allowlisted files that we're not
    # interested in
    filekeys = [
        x
        for x in files.keys()
        if (all_stages or x[2] == stages[0])
        and (all_threads or x[1].endswith("(main)"))
        and (all_stages and x[2] != stages[0] or not checkAllowlist(x[0], allowlist))
    ]
    if debug:
        # in debug, we want stages = [startup+normal] and all threads, not just (main)
        # we will use this data to upload fileIO info to blobber only for debug mode
        outputData = [
            x
            for x in files.keys()
            if (all_stages or x[2] in [stages[0], stages[1]])
            and (
                all_stages
                and x[2] not in [stages[0], stages[1]]
                or not checkAllowlist(x[0], allowlist)
            )
        ]
    else:
        outputData = filekeys

    # output data
    for row in outputData:
        stats = files[row]
        output = (
            f"{row[0]}, {row[1]}, {row[2]}, {stats['DiskReadCount']}, "
            f"{stats['DiskReadBytes']}, {stats['DiskWriteCount']}, "
            f"{stats['DiskWriteBytes']}\n"
        )
        outFile.write(output)

    if outputFile:
        # close the file handle
        outFile.close()
        if debug:
            uploadFile(outputFile)

    # We still like to have the outputfile to record the raw data, now
    # filter out acceptable files/ranges
    allowlist_path = None
    wl_temp = {}
    dirname = os.path.dirname(__file__)
    if os.path.exists(os.path.join(dirname, "xperf_allowlist.json")):
        allowlist_path = os.path.join(dirname, "xperf_allowlist.json")
    elif os.path.exists(os.path.join(dirname, "xtalos")) and os.path.exists(
        os.path.join(dirname, "xtalos", "xperf_allowlist.json")
    ):
        allowlist_path = os.path.join(dirname, "xtalos", "xperf_allowlist.json")

    wl_temp = {}
    if allowlist_path:
        with open(allowlist_path) as fHandle:
            wl_temp = json.load(fHandle)

    # Approot is the full path where the application is located at
    # We depend on it for dependentlibs.list to ignore files required for
    # normal startup.
    if approot:
        if os.path.exists(f"{approot}\\dependentlibs.list"):
            with open(f"{approot}\\dependentlibs.list") as fhandle:
                libs = fhandle.readlines()

            for lib in libs:
                wl_temp[f"{{firefox}}\\{lib.strip()}"] = {"ignore": True}

    # Windows isn't case sensitive, this protects us against mismatched
    # systems.
    wl = {}
    for item in wl_temp:
        wl[item.lower()] = wl_temp[item]

    errors = []
    for row in filekeys:
        filename = original_filename = row[0]
        filename = filename.lower()
        # take care of 'program files (x86)' matching 'program files'
        filename = filename.replace(" (x86)", "")

        paths = ["profile", "firefox", "desktop", "talos"]
        for path in paths:
            pathname = f"{path}\\"
            parts = filename.split(pathname)
            if len(parts) >= 2:
                filename = f"{{{path}}}\\{pathname.join(parts[1:])}"

        parts = filename.split("\\installtime")
        if len(parts) >= 2:
            filename = f"{parts[0]}\\{{time}}"

        # NOTE: this is Prefetch or prefetch, not case sensitive operating
        # system
        parts = filename.split("refetch")
        if len(parts) >= 2:
            filename = f"{parts[0]}refetch\\{{prefetch}}.pf"

        for pattern, substitution in NAME_SUBSTITUTIONS:
            filename = re.sub(pattern, substitution, filename)

        if filename in wl:
            if "ignore" in wl[filename] and wl[filename]["ignore"]:
                continue

        # too noisy
        #            if wl[filename]['minbytes'] > (files[row]['DiskReadBytes'] +\
        #                   files[row]['DiskWriteBytes']):
        #                print "%s: read %s bytes, less than expected minimum: %s"
        #                       % (filename, (files[row]['DiskReadBytes'] +
        #                                     files[row]['DiskWriteBytes']),
        #                          wl[filename]['minbytes'])

        # don't report in first round
        #            elif wl[filename]['maxbytes'] < (files[row]['DiskReadBytes'] +\
        #                   files[row]['DiskWriteBytes']):
        #                errors.append("%s: read %s bytes, more than expected maximum:"
        #                              " %s"
        #                              % (filename, (files[row]['DiskReadBytes'] +
        #                                 files[row]['DiskWriteBytes']),
        #                              wl[filename]['maxbytes']))

        # too noisy
        #            elif wl[filename]['mincount'] > (files[row]['DiskReadCount'] +\
        #                   files[row]['DiskWriteCount']):
        #                print "%s: %s accesses, less than expected minimum: %s"
        #                       % (filename, (files[row]['DiskReadCount'] +
        #                                     files[row]['DiskWriteCount']),
        #                          wl[filename]['mincount'])

        # don't report in first round
        #            elif wl[filename]['maxcount'] < (files[row]['DiskReadCount'] +\
        #                   files[row]['DiskWriteCount']):
        #                errors.append("%s: %s accesses, more than expected maximum:"
        #                              " %s"
        #                              % (filename, (files[row]['DiskReadCount'] +
        #                                            files[row]['DiskWriteCount']),
        #                                 wl[filename]['maxcount']))
        else:
            errors.append(
                f"File '{filename}' (normalized from '{original_filename}') was accessed and we were not expecting"
                f" it.  DiskReadCount: {files[row]['DiskReadCount']}, DiskWriteCount: {files[row]['DiskWriteCount']},"
                f" DiskReadBytes: {files[row]['DiskReadBytes']}, DiskWriteBytes: {files[row]['DiskWriteBytes']}"
            )

    if errors:
        # output specific errors to be picked up by tbpl parser
        for error in errors:
            # NOTE: the ' :' is intentional, without the space before the :,
            # some parser will translate this
            print(f"TEST-UNEXPECTED-FAIL : xperf: {error}")

        # We detect if browser_failures.txt exists to exit and turn the job
        # orange
        if error_filename:
            with open(error_filename, "w") as errorFile:
                errorFile.write("\n".join(errors))

        if debug:
            uploadFile(etl_filename)


def etlparser_from_config(config_file, **kwargs):
    """start from a YAML config file"""

    # option defaults
    args = {
        "xperf_path": "xperf.exe",
        "etl_filename": "test.etl",
        "outputFile": "etl_output.csv",
        "processID": None,
        "approot": None,
        "allowlist_file": None,
        "error_filename": None,
        "all_stages": False,
        "all_threads": False,
    }
    args.update(kwargs)

    # override from YAML config file
    args = xtalos.options_from_config(args, config_file)

    # ensure process ID is given
    if not args.get("processID"):
        raise xtalos.XTalosError("No process ID option given")

    # ensure path to xperf given
    if not os.path.exists(args["xperf_path"]):
        raise xtalos.XTalosError(
            f"ERROR: xperf_path '{args['xperf_path']}' does not exist"
        )

    # update args with config file
    args["configFile"] = config_file

    # call etlparser
    etlparser(**args)


def main(args=sys.argv[1:]):
    # parse command line arguments
    parser = xtalos.XtalosOptions()
    args = parser.parse_args(args)
    args = parser.verifyOptions(args)
    if args is None:
        parser.error("Unable to verify arguments")
    if not args.processID:
        parser.error("No process ID argument given")

    # call API
    etlparser(
        args.xperf_path,
        args.etl_filename,
        args.processID,
        args.approot,
        args.configFile,
        args.outputFile,
        args.allowlist_file,
        args.error_filename,
        args.all_stages,
        args.all_threads,
        debug=args.debug_level >= xtalos.DEBUG_INFO,
    )


if __name__ == "__main__":
    main()

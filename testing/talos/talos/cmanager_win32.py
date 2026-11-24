# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import struct
from ctypes import (
    Structure,
    Union,
    byref,
    c_double,
    c_longlong,
    create_string_buffer,
    memmove,
    pointer,
    windll,
)
from ctypes.wintypes import DWORD, HANDLE, LONG, LPCSTR, LPCWSTR, LPSTR

from talos.cmanager_base import CounterManager
from talos.utils import TalosError

pdh = windll.pdh

_LONGLONG = c_longlong


class _PDH_COUNTER_PATH_ELEMENTS_A(Structure):
    _fields_ = [
        ("szMachineName", LPSTR),
        ("szObjectName", LPSTR),
        ("szInstanceName", LPSTR),
        ("szParentInstance", LPSTR),
        ("dwInstanceIndex", DWORD),
        ("szCounterName", LPSTR),
    ]


_PDH_MORE_DATA = -2147481646  # the need more space error


def _getExpandedCounterPaths(processName, counterName):
    """
    Get list of expanded counter paths given a counter name. Returns a
    list of strings or None, if no counter paths can be created
    """
    pcchPathListLength = DWORD(0)
    szWildCardPath = LPSTR(b"\\process(%s)\\%s" % (processName, counterName))
    if (
        pdh.PdhExpandCounterPathA(
            szWildCardPath, LPSTR(None), pointer(pcchPathListLength)
        )
        != _PDH_MORE_DATA
    ):
        return []

    pathListLength = pcchPathListLength.value
    szExpandedPathList = LPCSTR(b"\0" * pathListLength)
    if (
        pdh.PdhExpandCounterPathA(
            szWildCardPath, szExpandedPathList, pointer(pcchPathListLength)
        )
        != 0
    ):
        return []
    buffer = create_string_buffer(pcchPathListLength.value)
    memmove(buffer, szExpandedPathList, pcchPathListLength.value)

    paths = []
    i = 0
    path = ""
    for j in range(0, pcchPathListLength.value):
        c = str(struct.unpack_from("c", buffer, offset=j)[0])
        if c == "\0":
            if j == i:
                # double null: we're done
                break
            paths.append(path)
            path = ""
            i = j + 1
        else:
            path += c

    return paths


class _PDH_Counter_Union(Union):
    _fields_ = [
        ("longValue", LONG),
        ("doubleValue", c_double),
        ("largeValue", _LONGLONG),
        ("AnsiStringValue", LPCSTR),
        ("WideStringValue", LPCWSTR),
    ]


class _PDH_FMT_COUNTERVALUE(Structure):
    _fields_ = [("CStatus", DWORD), ("union", _PDH_Counter_Union)]


_PDH_FMT_LONG = 0x00000100


class WinCounterManager(CounterManager):
    def __init__(
        self, process_name, process, counters, childProcess="plugin-container"
    ):
        CounterManager.__init__(self)
        self.childProcess = childProcess
        self.registeredCounters = {}
        self.registerCounters(counters)
        # PDH might need to be "refreshed" if it has been queried while the
        # browser is closed
        pdh.PdhEnumObjectsA(None, None, 0, 0, 0, True)

        for counter in self.registeredCounters:
            try:
                # Add the counter path for the default process.
                self._addCounter(
                    bytes(process_name, encoding="utf-8"), b"process", counter
                )
            except TalosError:
                # Assume that this is a memory counter for the system,
                # not a process counter
                # If we got an error that has nothing to do with that,
                # the exception will almost certainly be re-raised
                self._addCounter(
                    bytes(process_name, encoding="utf-8"), b"Memory", counter
                )

            self._updateCounterPathsForChildProcesses(counter)

    def _addCounter(self, processName, counterType, counterName):
        pCounterPathElements = _PDH_COUNTER_PATH_ELEMENTS_A(
            LPSTR(None),
            LPSTR(counterType),
            LPSTR(processName),
            LPSTR(None),
            DWORD(-1),
            LPSTR(counterName),
        )

        pcchbufferSize = DWORD(0)

        # First run we just try to get the buffer size so we can allocate a
        # string big enough to fill it
        if (
            pdh.PdhMakeCounterPathA(
                pointer(pCounterPathElements),
                LPCSTR(0),
                pointer(pcchbufferSize),
                DWORD(0),
            )
            != _PDH_MORE_DATA
        ):
            raise TalosError(
                f"Could not create counter path for counter {counterName} for {processName}"
            )

        szFullPathBuffer = LPCSTR(b"\0" * pcchbufferSize.value)
        # Then we run to get the actual value
        if (
            pdh.PdhMakeCounterPathA(
                pointer(pCounterPathElements),
                szFullPathBuffer,
                pointer(pcchbufferSize),
                DWORD(0),
            )
            != 0
        ):
            raise TalosError(
                f"Could not create counter path for counter {counterName} for {processName}"
            )

        path = szFullPathBuffer.value

        hq = HANDLE()
        if pdh.PdhOpenQuery(None, None, byref(hq)) != 0:
            raise TalosError("Could not open win32 counter query")

        hc = HANDLE()
        if pdh.PdhAddCounterA(hq, path, 0, byref(hc)) != 0:
            raise TalosError(f"Could not add win32 counter {path}")

        self.registeredCounters[counterName] = [hq, [(hc, path)]]

    def registerCounters(self, counters):
        # self.registeredCounters[counter][0] is a counter query handle
        # self.registeredCounters[counter][1] is a list of tuples, the first
        # member of which is a counter handle, the second a counter path
        for counter in counters:
            # Main_RSS is collected inside of pageloader
            if counter.strip() == "Main_RSS":
                continue

            # mainthread_io is collected from the browser via environment
            # variables
            if counter.strip() == "mainthread_io":
                continue

            self.registeredCounters[bytes(counter, encoding="utf-8")] = []

    def _updateCounterPathsForChildProcesses(self, counter):
        # Create a counter path for each instance of the child process that
        # is running.  If any of these paths are not in our counter list,
        # add them to our counter query and append them to the counter list,
        # so that we'll begin tracking their statistics.  We don't need to
        # worry about removing invalid paths from the list, as
        # getCounterValue() will generate a value of 0 for those.
        hq = self.registeredCounters[counter][0]
        oldCounterListLength = len(self.registeredCounters[counter][1])

        pdh.PdhEnumObjectsA(None, None, 0, 0, 0, True)

        expandedPaths = _getExpandedCounterPaths(
            bytes(self.childProcess, encoding="utf-8"), counter
        )
        if not expandedPaths:
            return
        for expandedPath in expandedPaths:
            alreadyInCounterList = False
            for singleCounter in self.registeredCounters[counter][1]:
                if expandedPath == singleCounter[1]:
                    alreadyInCounterList = True
            if not alreadyInCounterList:
                try:
                    newhc = HANDLE()
                    if pdh.PdhAddCounterA(hq, expandedPath, 0, byref(newhc)) != 0:
                        raise TalosError(
                            f"Could not add expanded win32 counter {expandedPath}"
                        )
                    self.registeredCounters[counter][1].append((newhc, expandedPath))
                except Exception:
                    continue

        if oldCounterListLength != len(self.registeredCounters[counter][1]):
            pdh.PdhCollectQueryData(hq)

    def getCounterValue(self, counter):
        # Update counter paths, to catch any new child processes that might
        # have been launched since last call.  Then iterate through all
        # counter paths for this counter, and return a combined value.
        counter = bytes(counter, encoding="utf-8")
        if counter not in self.registeredCounters:
            return None

        if self.registeredCounters[counter] == []:
            return None

        self._updateCounterPathsForChildProcesses(counter)
        hq = self.registeredCounters[counter][0]

        # we'll just ignore the return value here, in case no counters
        # are valid anymore
        pdh.PdhCollectQueryData(hq)

        aggregateValue = 0
        for singleCounter in self.registeredCounters[counter][1]:
            hc = singleCounter[0]
            dwType = DWORD(0)
            value = _PDH_FMT_COUNTERVALUE()

            # if we can't get a value, just assume a value of 0
            if (
                pdh.PdhGetFormattedCounterValue(
                    hc, _PDH_FMT_LONG, byref(dwType), byref(value)
                )
                == 0
            ):
                aggregateValue += value.union.longValue

        return aggregateValue

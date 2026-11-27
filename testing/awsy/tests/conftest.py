# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import gzip
import json

import pytest

KIND_HEAP = 1
KIND_OTHER = 2


def report(process, path, amount, kind=KIND_OTHER):
    return {"process": process, "path": path, "kind": kind, "amount": amount}


def process_reports(process, explicit_heap=0, heap_allocated=0, scale=1):
    return [
        report(process, "explicit/heap/a", explicit_heap * scale, KIND_HEAP),
        report(process, "explicit/nonheap", 7 * scale),
        report(process, "explicit/images/img", 11 * scale, KIND_HEAP),
        report(process, "heap-allocated", heap_allocated * scale),
        report(process, "resident", 1000 * scale),
        report(process, "resident-unique", 300 * scale),
        report(process, "js-main-runtime/gc", 50 * scale),
    ]


@pytest.fixture
def write_report(tmp_path):
    def _write(name, reports, gzipped=True):
        path = tmp_path / name
        data = json.dumps({"reports": reports}).encode()
        if gzipped:
            with gzip.open(path, "wb") as f:
                f.write(data)
        else:
            path.write_bytes(data)
        return path

    return _write

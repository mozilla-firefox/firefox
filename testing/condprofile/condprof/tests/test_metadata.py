# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import datetime
import json

import mozunit
import pytest

from condprof.metadata import METADATA_NAME, Metadata


def test_new_metadata_writes_created_updated_and_zero_age(tmp_path):
    metadata = Metadata(str(tmp_path))
    assert len(metadata) == 0
    metadata["name"] = "settled"
    assert metadata["name"] == "settled"
    assert list(metadata) == ["name"]
    del metadata["name"]
    assert "name" not in metadata
    metadata.write(name="full", platform="linux64")
    data = json.loads((tmp_path / METADATA_NAME).read_text())
    assert data["name"] == "full"
    assert data["platform"] == "linux64"
    assert data["created"] == data["updated"]
    assert data["days"] == 0
    assert data["age"] == "days"


def test_existing_metadata_keeps_created_and_computes_age(tmp_path):
    created = str(datetime.datetime.now() + datetime.timedelta(days=40))
    (tmp_path / METADATA_NAME).write_text(json.dumps({"created": created}))
    metadata = Metadata(str(tmp_path))
    assert metadata["created"] == created
    metadata.write()
    data = json.loads((tmp_path / METADATA_NAME).read_text())
    assert data["created"] == created
    assert data["days"] in (39, 40)
    assert data["age"] == "months"


@pytest.mark.parametrize(
    "days,age",
    [
        (0, "days"),
        (6, "days"),
        (7, "weeks"),
        (29, "weeks"),
        (30, "months"),
        (179, "months"),
        (180, "old"),
    ],
)
def test_days2age_maps_days_to_age_bucket(tmp_path, days, age):
    assert Metadata(str(tmp_path))._days2age(days) == age


def test_delta_ignores_timezone(tmp_path):
    metadata = Metadata(str(tmp_path))
    assert (
        metadata._delta(
            "2026-11-23 10:00:00.000000+00:00", "2026-11-20 10:00:00.000000+00:00"
        )
        == 3
    )


if __name__ == "__main__":
    mozunit.main()

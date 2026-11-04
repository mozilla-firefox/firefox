# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import pathlib
import shutil
import tempfile

import pytest

here = os.path.realpath(__file__)
__TESTS_DIR = os.path.join(os.path.dirname(os.path.dirname(here)), "tests")


def remove_develop_files(starting_dir=__TESTS_DIR):
    for file_name in os.listdir(starting_dir):
        file_path = os.path.join(starting_dir, file_name)

        if file_name.endswith(".develop") and os.path.isfile(file_path):
            os.remove(file_path)
        elif os.path.isdir(file_path):
            remove_develop_files(file_path)


def patched_build_manifest(config, manifestName):
    # for unit testing, we aren't downloading the tp5 pageset; therefore
    # manifest file doesn't exist; create a dummy one so we can continue
    if not os.path.exists(manifestName):
        try:
            if not os.path.exists(os.path.dirname(manifestName)):
                os.makedirs(os.path.dirname(manifestName))
            f = open(manifestName, "w")
            f.close()
        except Exception as e:
            raise (e)

    # read manifest lines
    with open(manifestName) as fHandle:
        manifestLines = fHandle.readlines()

    # write modified manifest lines
    with open(manifestName + ".develop", "w") as newHandle:
        for line in manifestLines:
            newline = line.replace("localhost", config["webserver"])
            newline = newline.replace("page_load_test", "tests")
            newHandle.write(newline)

    newManifestName = manifestName + ".develop"

    # return new manifest
    return newManifestName


@pytest.fixture(scope="module")
def pdfpaint_dir_info():
    with tempfile.TemporaryDirectory() as tmpdir:
        try:
            # Setup the temporary files
            tmpdir_path = pathlib.Path(tmpdir)
            talos_pdfs_dir = pathlib.Path(tmpdir_path, "talos-pdfs")
            talos_pdfs_dir.mkdir(parents=True, exist_ok=True)

            pdf_count = 101
            pdf_manifest_json = []
            for i in range(pdf_count):
                pdf_manifest_json.append({"file": str(i), "talos": True})
            pdf_manifest_json.append({"file": "not_marked"})
            pdf_manifest_json.append({"file": "not_for_talos", "talos": False})

            pdf_manifest = talos_pdfs_dir / "test_manifest.json"
            with pdf_manifest.open("w", encoding="utf-8") as f:
                json.dump(pdf_manifest_json, f)

            yield tmpdir_path, pdf_count
        finally:
            shutil.rmtree(tmpdir)

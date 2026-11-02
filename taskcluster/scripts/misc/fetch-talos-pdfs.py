#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
This script gathers all the required PDFs from the test_manifest.json
file found in the mozilla pdf.js repo. The PDFs themselves come from the
private mozilla pdf.js.pdfs repo, which mirrors every PDF used by the pdf.js
test suite (including the linked ones). Both repos are provided through
MOZ_FETCHES_DIR.
"""

import hashlib
import json
import os
import pathlib
import shutil
import sys


def log(msg):
    print("fetch-talos-pdf: %s" % msg)


def md5sum(filepath):
    digest = hashlib.md5()
    with filepath.open("rb") as fd:
        for chunk in iter(lambda: fd.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def gather_talos_pdf(pdfs_folder, pdf_info, output_dir):
    """Gathers a PDF file into the output directory.

    :param Path pdfs_folder: The folder of the pdf.js.pdfs repo containing the PDFs.
    :param dict pdf_info: Information about the pdf we're currently gathering, and
        found in the test/test_manifest.json file from the pdf.js repo.
    :param Path output_dir: The directory to copy the PDF to.
    """
    if not pdf_info.get("talos", True):
        return
    pdf_name = pathlib.Path(pdf_info["file"]).name
    output_pdf_path = pathlib.Path(output_dir, pdf_name)

    log(f"Gathering PDF {pdf_name}...")
    if output_pdf_path.exists():
        log(f"{pdf_name} already exists in output location")
        return

    shutil.copy(pathlib.Path(pdfs_folder, pdf_name), output_pdf_path)
    if (actual := md5sum(output_pdf_path)) != pdf_info["md5"]:
        raise Exception(f"{pdf_name} has MD5 {actual}, expected {pdf_info['md5']}")


def gather_talos_pdfs(test_manifest_path, pdfs_repo, output_dir):
    """Gather all pdfs to be used in the talos pdfpaint test.

    :param Path test_manifest_path: Path to the test/test_manifest.json file
        from the Mozilla Github pdf.js repo.
    :param Path pdfs_repo: Path to the Mozilla Github pdf.js.pdfs repo.
    :param Path output_dir: Output directory for the PDFs.
    """
    pdfs_folder = pathlib.Path(pdfs_repo, "pdfs")

    test_manifest = json.loads(test_manifest_path.read_text())
    for pdf_info in test_manifest:
        gather_talos_pdf(pdfs_folder, pdf_info, output_dir)

    # Include the test manifest in the output directory as it
    # contains the names of the tests
    shutil.copy(test_manifest_path, pathlib.Path(output_dir, test_manifest_path.name))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise Exception(f"Usage: {sys.argv[0]} <output_dir>")

    output_dir_path = pathlib.Path(sys.argv[1]).resolve()
    output_dir_path.mkdir(parents=True, exist_ok=True)

    moz_fetches_dir = os.environ.get("MOZ_FETCHES_DIR", "")
    if not moz_fetches_dir:
        raise Exception(
            "MOZ_FETCHES_DIR is not set to the path containing the pdf.js "
            "and pdf.js.pdfs repos"
        )

    pdf_js_repo = pathlib.Path(moz_fetches_dir, "pdf.js")
    if not pdf_js_repo.exists():
        raise Exception("Can't find the pdf.js repository in MOZ_FETCHES_DIR")

    pdfs_repo = pathlib.Path(moz_fetches_dir, "pdf.js.pdfs")
    if not pdfs_repo.exists():
        raise Exception("Can't find the pdf.js.pdfs repository in MOZ_FETCHES_DIR")

    gather_talos_pdfs(
        pathlib.Path(pdf_js_repo, "test", "test_manifest.json"),
        pdfs_repo,
        output_dir_path,
    )

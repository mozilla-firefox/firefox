#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Prepare an ANGLE checkout downloaded by `mach vendor`.

Run this through the standard vendor entry point:

    ./mach vendor gfx/angle/moz.yaml -r <revision>

where `revision` can be a branch name or specific revision. If omitted, the
version of ANGLE currently used by Chromium's Beta channel will be chosen.

`mach vendor` fetches and extracts the specified version of ANGLE, along with a
few of its required dependencies, then calls this script to apply our patches,
generate `moz.build` files required to build ANGLE, and vendor the required
files into tree.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

import gn_processor
import mozfile

SCRIPT_DIR = Path(__file__).resolve().parent
TOPSRCDIR = SCRIPT_DIR.parent.parent
GN_CONFIG = SCRIPT_DIR / "gn-configs" / "angle.json"

STUBS = {
    "third_party/zlib/BUILD.gn": """config("zlib_config") {
}
source_set("zlib") {
    public_configs = [ ":zlib_config" ]
}""",
}


def stub_dependencies(angle_src_dir: Path) -> None:
    print("Stubbing dependencies")

    for file, contents in STUBS.items():
        path = angle_src_dir / file
        if not path.is_file():
            raise Exception(f"No dependency at {file} to stub")

        with path.open("w", encoding="utf-8", newline="\n") as output:
            output.write(contents)


def apply_patches(angle_src_dir: Path, patch_dir: Path) -> None:
    if not patch_dir.is_dir():
        return

    patches = sorted(patch_dir.glob("*.patch"))
    if not patches:
        return

    print("Applying patches")

    for patch in patches:
        print(f"Applying {patch}")
        with mozfile.NamedTemporaryFile(suffix=".rej") as reject_file:
            reject_path = Path(reject_file.name)
            command = [
                "patch",
                "-r",
                str(reject_path),
                "-p1",
                "--directory",
                str(angle_src_dir),
                "--input",
                str(patch),
                "--no-backup-if-mismatch",
                "--batch",
                "--forward",
            ]
            result = subprocess.run(command, check=False)
            reject_content = reject_path.read_text()
        if result.returncode != 0:
            print(f"""Could not apply {patch}.

    Please follow the instructions at https://firefox-source-docs.mozilla.org/gfx/VendoringANGLE.html to resolve.

    Patch rejection details:\n{reject_content}""")
            sys.exit(1)


def prune_unused_files(angle_src_dir: Path, gn_configs) -> None:
    print("Pruning unused files")

    # Ensure we retain the LICENSE file, and some headers that are incorrectly
    # missing as dependencies in the GN build scripts.
    required_files = {
        Path(path)
        for path in [
            "LICENSE",
            "src/third_party/khronos/GL/wglext.h",
            "third_party/spirv-headers/src/include/spirv/unified1/ArmExperimentalMLOperations.h",
            "third_party/spirv-headers/src/include/spirv/unified1/NonSemanticGraphDebugInfo.h",
            "third_party/spirv-headers/src/include/spirv/unified1/NonSemanticShaderDebugInfo.h",
            "third_party/spirv-headers/src/include/spirv/unified1/spirv.hpp11",
            "third_party/spirv-tools/src/source/val/instruction.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_av1std.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_av1std_decode.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_av1std_encode.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_h264std.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_h264std_decode.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_h264std_encode.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_h265std.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_h265std_decode.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_h265std_encode.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_vp9std.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codec_vp9std_decode.h",
            "third_party/vulkan-headers/src/include/vk_video/vulkan_video_codecs_common.h",
            "third_party/vulkan-headers/src/include/vulkan/vk_platform.h",
            "third_party/vulkan-headers/src/include/vulkan/vulkan.h",
            "third_party/vulkan-headers/src/include/vulkan/vulkan_android.h",
            "third_party/vulkan-headers/src/include/vulkan/vulkan_core.h",
        ]
    }

    # Resolve a GN-style path to a angle_src_dir-relative path.
    def resolve_path(path: str) -> Path:
        if path.startswith("//"):
            path = path[2:]
        return Path(path)

    for gn_config in gn_configs:
        # Preserving the input GN files allows developers to run gn_processor.py
        # again without having to run the entire vendoring procedure.
        required_files.update(
            resolve_path(path) for path in gn_config.get("gen_input_files", [])
        )
        for spec in gn_config["targets"].values():
            for spec_attr in ("inputs", "sources"):
                required_files.update(
                    resolve_path(path) for path in spec.get(spec_attr, [])
                )
            if "script" in spec:
                required_files.add(resolve_path(spec["script"]))

    for root, dirs, files in os.walk(angle_src_dir, topdown=False):
        root_path = Path(root)
        for filename in files:
            path = root_path / filename
            if path.relative_to(angle_src_dir) not in required_files:
                path.unlink()

        for dir in dirs:
            try:
                (root_path / dir).rmdir()
            except OSError:
                pass


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("tmpextractdir", type=Path)
    parser.add_argument("vendor_dir", type=Path)
    args = parser.parse_args()

    apply_patches(args.tmpextractdir, SCRIPT_DIR / "patches")
    stub_dependencies(args.tmpextractdir)

    # gn_processor must be executed from the TOPSRCDIR with the required files
    # already vendored in tree, so copy the entire checkout over now then prune
    # it later.
    args.vendor_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(
        args.tmpextractdir, args.vendor_dir, symlinks=True, dirs_exist_ok=True
    )

    with open(GN_CONFIG) as fh:
        config = mozfile.json.load(fh)
    gn_configs, mozbuild_results = zip(
        *gn_processor.generate_gn_configs(TOPSRCDIR, config)
    )

    prune_unused_files(args.vendor_dir, gn_configs)
    gn_processor.write_mozbuild_files(
        TOPSRCDIR, args.vendor_dir, mozbuild_results, config["write_mozbuild_variables"]
    )


if __name__ == "__main__":
    main()

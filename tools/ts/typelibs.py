# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import mozpack.path as mozpath
from typelib_inputs import conf_files, idl_files, js_files, listed_files, webidl_files

TYPES_DIR = "tools/@types"

PROCESSED = [
    # AppConstants.sys.mjs has a (better) manually created declaration file.
    "Readerable.sys.mjs",
    "FeatureManifest.sys.mjs",
    "PromiseWorker.js",
    "PromiseWorker.mjs",
    "RFPTargetConstants.sys.mjs",
]


def input_generators(topsrcdir, idl_list, conf_list):
    sys.path.append(mozpath.join(topsrcdir, "toolkit/components/glean"))
    import metrics_index

    def src(*paths):
        return [mozpath.join(topsrcdir, path) for path in paths]

    css = "layout/style/GenerateCSSProperties.py"
    glean = (
        "toolkit/components/glean/build_scripts/glean_parser_ext/run_glean_parser.py"
    )
    promiseworker = "toolkit/components/promiseworker/worker"
    rfp = "toolkit/components/resistfingerprinting"
    return {
        "CSSCounterStyleRule.webidl": (css, "gen_counter_style_rule_webidl", []),
        "CSSFontFaceDescriptors.webidl": (css, "gen_font_face_descriptors_webidl", []),
        "CSSPageDescriptors.webidl": (css, "gen_page_descriptors_webidl", []),
        "CSSPositionTryDescriptors.webidl": (
            css,
            "gen_position_try_descriptors_webidl",
            [],
        ),
        "CSSStyleProperties.webidl": (css, "gen_style_properties_webidl", []),
        "FeatureManifest.sys.mjs": (
            "toolkit/components/nimbus/generate/generate_feature_manifest.py",
            "generate_feature_manifest",
            src("toolkit/components/nimbus/FeatureManifest.yaml"),
        ),
        "PromiseWorker.js": (
            f"{promiseworker}/GeneratePromiseWorkerScript.py",
            "generate_script",
            src(f"{promiseworker}/PromiseWorker.template.worker.js"),
        ),
        "PromiseWorker.mjs": (
            f"{promiseworker}/GeneratePromiseWorkerScript.py",
            "generate_module",
            src(f"{promiseworker}/PromiseWorker.template.worker.js"),
        ),
        "RFPTargetConstants.sys.mjs": (
            f"{rfp}/extract_rfp_targets.py",
            "main",
            src(
                f"{rfp}/RFPTargets.inc",
                f"{rfp}/RFPTargetsDefaultBaseline.inc",
                f"{rfp}/RFPTargetsDefault.inc",
            ),
        ),
        "Readerable.sys.mjs": (
            "python/mozbuild/mozbuild/action/preprocessor.py",
            "generate",
            src("toolkit/components/reader/Readerable.sys.mjs"),
        ),
        "error_list.json": ("xpcom/base/ErrorList.py", "error_list_json", []),
        "lib.gecko.glean.d.ts": (
            glean,
            "ts_metrics_pings",
            src(
                *metrics_index.metrics_yamls,
                *metrics_index.pings_yamls,
                *metrics_index.tags_yamls,
            )
            # The app version, which does not change the TypeScript output.
            + ["1"],
        ),
        "services.json": ("tools/ts/services_typelib.py", "main", [conf_list]),
        "xpidl.d.json": ("tools/ts/xpidl_typelib.py", "main", [idl_list]),
    }


def run(cmd, cwd):
    try:
        output = subprocess.check_output(
            cmd, cwd=cwd, stderr=subprocess.STDOUT, encoding="utf-8", errors="replace"
        )
    except subprocess.CalledProcessError as e:
        print(e.output, end="")
        return e.returncode
    print(output, end="")
    return 0


def generate_inputs(topsrcdir, python, inputs_dir, files, log):
    idl_list = mozpath.join(inputs_dir, "idls.txt")
    conf_list = mozpath.join(inputs_dir, "confs.txt")
    for path, selected in (
        (idl_list, idl_files(files)),
        (conf_list, conf_files(files)),
    ):
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.writelines(f"{p}\n" for p in selected)

    runner = mozpath.join(topsrcdir, "tools/ts/generate_input.py")
    commands = []
    for output, (script, entry_point, args) in input_generators(
        topsrcdir, idl_list, conf_list
    ).items():
        log(f"Generating {output}")
        commands.append([
            python,
            runner,
            topsrcdir,
            inputs_dir,
            mozpath.join(topsrcdir, script),
            entry_point,
            mozpath.join(inputs_dir, output),
            mozpath.join(inputs_dir, f"{output}.pp"),
            output,
            *args,
        ])

    with ThreadPoolExecutor() as executor:
        results = executor.map(lambda cmd: run(cmd, inputs_dir), commands)
        return next((rv for rv in results if rv), 0)


def link_typelibs(topsrcdir, inputs_dir, files, out_dir):
    def link(lib, *args):
        lib_dts = mozpath.join(out_dir, f"lib.gecko.{lib}.d.ts")
        return node(topsrcdir, f"build_{lib}", lib_dts, *args)

    # Get all *.webidl files for now.
    webidls = [mozpath.relpath(path, "dom") for path in webidl_files(files)]
    if rv := link("dom", mozpath.join(topsrcdir, "dom"), inputs_dir, *webidls):
        return rv

    xpc_msg = mozpath.join(topsrcdir, "js/xpconnect/src/xpc.msg")
    errors = mozpath.join(inputs_dir, "error_list.json")
    if rv := link("nsresult", xpc_msg, errors):
        return rv
    if rv := link("services", mozpath.join(inputs_dir, "services.json")):
        return rv
    return link("xpcom", inputs_dir, "xpidl.d.json")


def link_paths(topsrcdir, inputs_dir, files, out_dir):
    js_list = mozpath.join(inputs_dir, "js.txt")
    with open(js_list, "w", encoding="utf-8", newline="\n") as fh:
        fh.writelines(f"{path}\n" for path in js_files(files))
    return node(
        topsrcdir,
        "build_paths",
        topsrcdir,
        js_list,
        ",".join(["AppConstants.sys.mjs", *PROCESSED]),
        mozpath.join(out_dir, "tspaths.json"),
        mozpath.join(out_dir, "lib.gecko.modules.d.ts"),
    )


def emit_subs(topsrcdir, inputs_dir, subs_dir, log):
    args = ["--declaration", "--emitDeclarationOnly", "--allowJs", "--outDir"]

    paths = [mozpath.join(inputs_dir, file) for file in PROCESSED]
    for path, file in zip(paths, PROCESSED):
        log(f"{path} -> {subs_dir}/{file}")
    tsc = mozpath.join(topsrcdir, "node_modules/typescript/bin/tsc")
    return node(topsrcdir, tsc, *args, subs_dir, *paths)


def stage_typelibs(topsrcdir, python, inputs_dir, stage_dir, log):
    generated_dir = mozpath.join(stage_dir, "generated")
    os.makedirs(inputs_dir)
    os.makedirs(generated_dir)

    files = listed_files(topsrcdir)
    with ThreadPoolExecutor(max_workers=1) as executor:
        paths = executor.submit(link_paths, topsrcdir, inputs_dir, files, generated_dir)
        if rv := generate_inputs(topsrcdir, python, inputs_dir, files, log):
            return rv
        if rv := link_typelibs(topsrcdir, inputs_dir, files, generated_dir):
            return rv
        shutil.copy(mozpath.join(inputs_dir, "lib.gecko.glean.d.ts"), generated_dir)
        if rv := emit_subs(topsrcdir, inputs_dir, mozpath.join(stage_dir, "subs"), log):
            return rv
        return paths.result()


def node(topsrcdir, script, *args):
    from mozbuild.nodeutil import find_node_executable

    node_path, _ = find_node_executable()
    path = mozpath.join(mozpath.dirname(__file__), script)
    return run([node_path, path, *args], topsrcdir)

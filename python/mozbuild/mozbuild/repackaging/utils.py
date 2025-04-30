# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import os
import pathlib
import shutil
import subprocess
import tarfile
import zipfile
from datetime import datetime
from email.utils import format_datetime
from pathlib import Path
from string import Template
from tempfile import TemporaryDirectory

import mozpack.path as mozpath
from jinja2 import Environment, FileSystemLoader

from mozbuild.repackaging.application_ini import get_application_ini_values
from mozbuild.repackaging.desktop_file import generate_browser_desktop_entry_file_text


def _extract_application_ini_data_from_directory(application_directory):
    values = get_application_ini_values(
        application_directory,
        dict(section="App", value="Name"),
        dict(section="App", value="CodeName", fallback="Name"),
        dict(section="App", value="Vendor"),
        dict(section="App", value="RemotingName"),
        dict(section="App", value="BuildID"),
    )

    data = {
        "name": next(values),
        "display_name": next(values),
        "vendor": next(values),
        "remoting_name": next(values),
        "build_id": next(values),
    }

    return data


def _extract_application_ini_data(input_tar_file):
    with TemporaryDirectory() as d:
        with tarfile.open(input_tar_file) as tar:
            application_ini_files = [
                tar_info
                for tar_info in tar.getmembers()
                if tar_info.name.endswith("/application.ini")
            ]
            if len(application_ini_files) == 0:
                raise ValueError(
                    f"Cannot find any application.ini file in archive {input_tar_file}"
                )
            if len(application_ini_files) > 1:
                raise ValueError(
                    f"Too many application.ini files found in archive {input_tar_file}. "
                    f"Found: {application_ini_files}"
                )

            tar.extract(application_ini_files[0], path=d)

        application_ini_data = _extract_application_ini_data_from_directory(d)

        return application_ini_data


def _parse_application_ini_data(application_ini_data, version, build_number):
    application_ini_data["timestamp"] = datetime.strptime(
        application_ini_data["build_id"], "%Y%m%d%H%M%S"
    )

    application_ini_data["remoting_name"] = application_ini_data[
        "remoting_name"
    ].lower()

    application_ini_data["pkg_version"] = f"{version}-{build_number}"

    return application_ini_data


def copy_plain_config(input_template_dir, source_dir):
    filenames = [
        mozpath.basename(filename)
        for filename in os.listdir(input_template_dir)
        if Path(filename).suffix not in [".j2", ".js", ".in"]
    ]
    os.makedirs(source_dir, exist_ok=True)

    for filename in filenames:
        shutil.copy(
            mozpath.join(input_template_dir, filename),
            mozpath.join(source_dir, filename),
        )


def get_build_variables(
    application_ini_data,
    arch,
    version,
    depends="",
    package_name_suffix="",
    description_suffix="",
    release_product="",
    build_number="1",
):
    if release_product == "devedition":
        pkg_install_path = "usr/lib/firefox-devedition"
        pkg_name = f"firefox-devedition{package_name_suffix}"
    else:
        pkg_install_path = f"usr/lib/{application_ini_data['remoting_name']}"
        pkg_name = f"{application_ini_data['remoting_name']}{package_name_suffix}"

    return {
        "DESCRIPTION": f"{application_ini_data['vendor']} {application_ini_data['display_name']}{description_suffix}",
        "PKG_INSTALL_PATH": pkg_install_path,
        "PKG_NAME": pkg_name,
        "PKG_BUILD_NUMBER": build_number,
        "PKG_VERSION": version,
        "PRODUCT_NAME": application_ini_data["name"],
        "DISPLAY_NAME": application_ini_data["display_name"],
        "CHANGELOG_DATE": format_datetime(application_ini_data["timestamp"]),
        "MANPAGE_DATE": application_ini_data["timestamp"].strftime("%B %d, %Y"),
        "ARCH_NAME": arch,
        "DEPENDS": depends,
        "Icon": pkg_name,
    }


def get_manifest_from_langpack(xpi_file, output_path):
    try:
        zip_file = zipfile.ZipFile(xpi_file)
        manifest_file = zip_file.extract("manifest.json", output_path)
        return json.loads(pathlib.Path(manifest_file).read_text())
    except json.JSONDecodeError:
        return None


def inject_desktop_entry_file(
    log,
    source_dir,
    build_variables,
    release_product,
    release_type,
    fluent_localization,
    fluent_resource_loader,
):
    desktop_entry_template_path = mozpath.join(
        source_dir, f"{build_variables['PRODUCT_NAME'].lower()}.desktop"
    )
    desktop_entry_file_filename = f"{build_variables['PKG_NAME']}.desktop"

    # Check to see if a .desktop file is already supplied in the templates directory
    # If not, continue to generate default Firefox .desktop file
    if os.path.exists(desktop_entry_template_path):
        shutil.move(
            desktop_entry_template_path,
            mozpath.join(source_dir, desktop_entry_file_filename),
        )

        return

    desktop_entry_file_text = generate_browser_desktop_entry_file_text(
        log,
        build_variables,
        release_product,
        release_type,
        fluent_localization,
        fluent_resource_loader,
    )
    os.makedirs(source_dir, exist_ok=True)
    with open(mozpath.join(source_dir, desktop_entry_file_filename), "w") as f:
        f.write(desktop_entry_file_text)


def inject_distribution_folder(source_dir, source_type, app_name):
    distribution_ini_path = mozpath.join(source_dir, source_type, "distribution.ini")

    # Check to see if a distribution.ini file is already supplied in the templates directory
    # If not, continue to download default Firefox distribution.ini from GitHub
    if os.path.exists(distribution_ini_path):
        os.makedirs(
            mozpath.join(source_dir, app_name.lower(), "distribution"), exist_ok=True
        )
        shutil.move(
            distribution_ini_path,
            mozpath.join(source_dir, app_name.lower(), "distribution"),
        )

        return

    with TemporaryDirectory() as git_clone_dir:
        subprocess.check_call(
            [
                "git",
                "clone",
                "https://github.com/mozilla-partners/deb.git",
                git_clone_dir,
            ],
        )
        shutil.copytree(
            mozpath.join(git_clone_dir, "desktop/deb/distribution"),
            mozpath.join(source_dir, app_name.lower(), "distribution"),
        )


def inject_prefs_file(source_dir, app_name, template_dir):
    src = mozpath.join(template_dir, "package-prefs.js")
    dst = mozpath.join(source_dir, app_name.lower(), "defaults/pref")
    shutil.copy(src, dst)


def load_application_ini_data(infile, version, build_number):
    application_ini_data = _extract_application_ini_data(infile)
    return _parse_application_ini_data(application_ini_data, version, build_number)


def mv_manpage_files(source_dir, build_variables):
    src = mozpath.join(source_dir, "manpage.1")
    if os.path.exists(src):
        dst = mozpath.join(source_dir, f"{build_variables['PKG_NAME']}.1")
        shutil.move(src, dst)
    src = mozpath.join(source_dir, "manpages")
    if os.path.exists(src):
        dst = mozpath.join(source_dir, f"{build_variables['PKG_NAME']}.manpages")
        shutil.move(src, dst)


def prepare_langpack_files(output_dir, xpi_directory):
    metadata = {}
    for xpi_file in Path(xpi_directory).rglob("*.langpack.xpi"):
        manifest = get_manifest_from_langpack(xpi_file, output_dir)
        if manifest is not None:
            language = manifest["langpack_id"]
            metadata[language] = manifest["description"]
            output_file = mozpath.join(output_dir, f"{language}.langpack.xpi")
            shutil.copy(xpi_file, output_file)

    return metadata


def render_templates(
    input_template_dir,
    source_dir,
    build_variables,
    exclude_file_names=None,
):
    exclude_file_names = [] if exclude_file_names is None else exclude_file_names

    filenames = [
        mozpath.basename(filename)
        for filename in os.listdir(input_template_dir)
        if Path(filename).suffix in [".j2", ".in"]
        and filename not in exclude_file_names
    ]
    os.makedirs(source_dir, exist_ok=True)

    for file_name in filenames:
        if file_name.endswith(".in"):
            with open(mozpath.join(input_template_dir, file_name)) as f:
                template = Template(f.read())
            with open(mozpath.join(source_dir, Path(file_name).stem), "w") as f:
                f.write(template.substitute(build_variables))
        elif file_name.endswith(".j2"):
            environment = Environment(loader=FileSystemLoader(input_template_dir))
            template = environment.get_template(file_name)
            with open(mozpath.join(source_dir, Path(file_name).stem), "w") as f:
                f.write(template.render(build_variables))

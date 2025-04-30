# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import logging
import os
import shutil
import tempfile

import requests
from redo import retry


class RemoteVCSError(Exception):
    """Raised when vcs server (e.g., hg or git) responds with an error code that is not 404 (i.e. when there is an outage)"""

    def __init__(self, msg) -> None:
        super().__init__(msg)


def generate_browser_desktop_entry_file_text(
    log,
    build_variables,
    release_product,
    release_type,
    fluent_localization,
    fluent_resource_loader,
):
    desktop_entry = generate_browser_desktop_entry(
        log,
        build_variables,
        release_product,
        release_type,
        fluent_localization,
        fluent_resource_loader,
    )
    desktop_entry_file_text = "\n".join(desktop_entry)
    return desktop_entry_file_text


def generate_browser_desktop_entry(
    log,
    build_variables,
    release_product,
    release_type,
    fluent_localization,
    fluent_resource_loader,
):
    localizations = _create_fluent_localizations(
        fluent_resource_loader, fluent_localization, release_type, release_product, log
    )
    return _generate_browser_desktop_entry(build_variables, localizations)


def _create_fluent_localizations(
    fluent_resource_loader, fluent_localization, release_type, release_product, log
):
    brand_fluent_filename = "brand.ftl"
    l10n_central_url = "https://raw.githubusercontent.com/mozilla-l10n/firefox-l10n"
    desktop_entry_fluent_filename = "linuxDesktopEntry.ftl"

    l10n_dir = tempfile.mkdtemp()

    loader = fluent_resource_loader(os.path.join(l10n_dir, "{locale}"))

    localizations = {}
    linux_l10n_changesets = _load_linux_l10n_changesets(
        "browser/locales/l10n-changesets.json"
    )
    locales = ["en-US"]
    locales.extend(linux_l10n_changesets.keys())
    en_US_brand_fluent_filename = _get_en_US_brand_fluent_filename(
        brand_fluent_filename, release_type, release_product
    )

    for locale in locales:
        locale_dir = os.path.join(l10n_dir, locale)
        os.mkdir(locale_dir)
        localized_desktop_entry_filename = os.path.join(
            locale_dir, desktop_entry_fluent_filename
        )
        if locale == "en-US":
            en_US_desktop_entry_fluent_filename = os.path.join(
                "browser", "locales", "en-US", "browser", desktop_entry_fluent_filename
            )
            shutil.copyfile(
                en_US_desktop_entry_fluent_filename,
                localized_desktop_entry_filename,
            )
        else:
            non_en_US_fluent_resource_file_url = f"{l10n_central_url}/{linux_l10n_changesets[locale]['revision']}/{locale}/browser/browser/{desktop_entry_fluent_filename}"
            response = requests.get(non_en_US_fluent_resource_file_url)
            response = retry(
                requests.get,
                args=[non_en_US_fluent_resource_file_url],
                attempts=5,
                sleeptime=3,
                jitter=2,
            )
            mgs = "Missing {fluent_resource_file_name} for {locale}: received HTTP {status_code} for GET {resource_file_url}"
            params = {
                "fluent_resource_file_name": desktop_entry_fluent_filename,
                "locale": locale,
                "resource_file_url": non_en_US_fluent_resource_file_url,
                "status_code": response.status_code,
            }
            action = "repackage-deb"
            if response.status_code == 404:
                log(
                    logging.WARNING,
                    action,
                    params,
                    mgs,
                )
                continue
            if response.status_code != 200:
                log(
                    logging.ERROR,
                    action,
                    params,
                    mgs,
                )
                raise RemoteVCSError(mgs.format(**params))

            with open(localized_desktop_entry_filename, "w", encoding="utf-8") as f:
                f.write(response.text)

        shutil.copyfile(
            en_US_brand_fluent_filename,
            os.path.join(locale_dir, brand_fluent_filename),
        )

        fallbacks = [locale]
        if locale != "en-US":
            fallbacks.append("en-US")
        localizations[locale] = fluent_localization(
            fallbacks, [desktop_entry_fluent_filename, brand_fluent_filename], loader
        )

    return localizations


def _get_en_US_brand_fluent_filename(
    brand_fluent_filename, release_type, release_product
):
    branding_fluent_filename_template = os.path.join(
        "browser/branding/{brand}/locales/en-US", brand_fluent_filename
    )
    if release_type == "nightly":
        return branding_fluent_filename_template.format(brand="nightly")
    elif release_type == "release" or release_type == "release-rc":
        return branding_fluent_filename_template.format(brand="official")
    elif release_type == "beta" and release_product == "firefox":
        return branding_fluent_filename_template.format(brand="official")
    elif release_type == "beta" and release_product == "devedition":
        return branding_fluent_filename_template.format(brand="aurora")
    elif release_type.startswith("esr"):
        return branding_fluent_filename_template.format(brand="official")
    else:
        return branding_fluent_filename_template.format(brand="unofficial")


def _load_linux_l10n_changesets(l10n_changesets_filename):
    with open(l10n_changesets_filename) as l10n_changesets_file:
        l10n_changesets = json.load(l10n_changesets_file)
        return {
            locale: changeset
            for locale, changeset in l10n_changesets.items()
            if any(platform.startswith("linux") for platform in changeset["platforms"])
        }


def _generate_browser_desktop_entry(build_variables, localizations):
    mime_types = [
        "application/json",
        "application/pdf",
        "application/rdf+xml",
        "application/rss+xml",
        "application/x-xpinstall",
        "application/xhtml+xml",
        "application/xml",
        "audio/flac",
        "audio/ogg",
        "audio/webm",
        "image/avif",
        "image/gif",
        "image/jpeg",
        "image/png",
        "image/svg+xml",
        "image/webp",
        "text/html",
        "text/xml",
        "video/ogg",
        "video/webm",
        "x-scheme-handler/chrome",
        "x-scheme-handler/http",
        "x-scheme-handler/https",
        "x-scheme-handler/mailto",
    ]

    categories = [
        "GNOME",
        "GTK",
        "Network",
        "WebBrowser",
    ]

    actions = [
        {
            "name": "new-window",
            "message": "desktop-action-new-window-name",
            "command": f"{build_variables['PKG_NAME']} --new-window %u",
        },
        {
            "name": "new-private-window",
            "message": "desktop-action-new-private-window-name",
            "command": f"{build_variables['PKG_NAME']} --private-window %u",
        },
        {
            "name": "open-profile-manager",
            "message": "desktop-action-open-profile-manager",
            "command": f"{build_variables['PKG_NAME']} --ProfileManager",
        },
    ]

    desktop_entry = _desktop_entry_section(
        "Desktop Entry",
        [
            {
                "key": "Version",
                "value": "1.0",
            },
            {
                "key": "Type",
                "value": "Application",
            },
            {
                "key": "Exec",
                "value": f"{build_variables['PKG_NAME']} %u",
            },
            {
                "key": "Terminal",
                "value": "false",
            },
            {
                "key": "X-MultipleArgs",
                "value": "false",
            },
            {
                "key": "Icon",
                "value": build_variables["Icon"],
            },
            {
                "key": "StartupWMClass",
                "value": (
                    "firefox-aurora"
                    if build_variables["PKG_NAME"] == "firefox-devedition"
                    else build_variables.get(
                        "StartupWMClass", build_variables["PKG_NAME"]
                    )
                ),
            },
            {
                "key": "DBusActivatable",
                "value": build_variables.get("DBusActivatable", "false"),
                "condition": "DBusActivatable" in build_variables,
            },
            {
                "key": "Categories",
                "value": _desktop_entry_list(categories),
            },
            {
                "key": "MimeType",
                "value": _desktop_entry_list(mime_types),
            },
            {
                "key": "StartupNotify",
                "value": "true",
            },
            {
                "key": "Actions",
                "value": _desktop_entry_list([action["name"] for action in actions]),
            },
            {"key": "Name", "value": "desktop-entry-name", "l10n": True},
            {"key": "Comment", "value": "desktop-entry-comment", "l10n": True},
            {"key": "GenericName", "value": "desktop-entry-generic-name", "l10n": True},
            {"key": "Keywords", "value": "desktop-entry-keywords", "l10n": True},
            {
                "key": "X-GNOME-FullName",
                "value": "desktop-entry-x-gnome-full-name",
                "l10n": True,
            },
        ],
        localizations,
    )

    for action in actions:
        desktop_entry.extend(
            _desktop_entry_section(
                f"Desktop Action {action['name']}",
                [
                    {
                        "key": "Name",
                        "value": action["message"],
                        "l10n": True,
                    },
                    {
                        "key": "Exec",
                        "value": action["command"],
                    },
                ],
                localizations,
            )
        )

    return desktop_entry


def _desktop_entry_list(iterable):
    delimiter = ";"
    return f"{delimiter.join(iterable)}{delimiter}"


def _desktop_entry_attribute(key, value, locale=None, localizations=None):
    if not locale and not localizations:
        return f"{key}={value}"
    if locale and locale == "en-US":
        return f"{key}={localizations[locale].format_value(value)}"
    else:
        return f"{key}[{locale.replace('-', '_')}]={localizations[locale].format_value(value)}"


def _desktop_entry_section(header, attributes, localizations):
    desktop_entry_section = [f"[{header}]"]
    l10n_attributes = [attribute for attribute in attributes if attribute.get("l10n")]
    non_l10n_attributes = [
        attribute for attribute in attributes if not attribute.get("l10n")
    ]
    for attribute in non_l10n_attributes:
        if not attribute.get("condition", True):
            continue
        desktop_entry_section.append(
            _desktop_entry_attribute(attribute["key"], attribute["value"])
        )
    for attribute in l10n_attributes:
        for locale in localizations:
            desktop_entry_section.append(
                _desktop_entry_attribute(
                    attribute["key"], attribute["value"], locale, localizations
                )
            )
    desktop_entry_section.append("")
    return desktop_entry_section

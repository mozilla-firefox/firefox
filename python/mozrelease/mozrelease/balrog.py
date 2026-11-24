# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


def _generate_show_url(context, entry):
    url = entry["url"]
    return {
        "actions": "showURL",
        "openURL": url.format(**context),
    }


def _generate_product_details(context, entry):
    url = entry["url"]
    return {
        "detailsURL": url.format(**context),
        "type": "minor",
    }


def _generate_force_background_update(context, entry):
    return {
        "forceBackgroundUpdate": True,
    }


_FIELD_TYPES = {
    "show-url": _generate_show_url,
    "product-details": _generate_product_details,
    "force-background-update": _generate_force_background_update,
}


def _generate_conditions(context, entry):
    if (
        "release-types" in entry
        and context["release-type"] not in entry["release-types"]
    ):
        return None
    if "blob-types" in entry and context["blob-type"] not in entry["blob-types"]:
        return None
    if "products" in entry and context["product"] not in entry["products"]:
        return None

    conditions = {}
    if "locales" in entry:
        conditions["locales"] = entry["locales"]
    if "versions" in entry:
        conditions["versions"] = [
            version.format(**context) for version in entry["versions"]
        ]
    if "update-channel" in entry:
        conditions["channels"] = [
            entry["update-channel"] + suffix
            for suffix in ("", "-localtest", "-cdntest")
        ]
    if "build-ids" in entry:
        conditions["buildIDs"] = [
            buildid.format(**context) for buildid in entry["build-ids"]
        ]
    return conditions


def generate_update_properties(context, config):
    result = []
    for entry in config:
        fields = _FIELD_TYPES[entry["type"]](context, entry)
        conditions = _generate_conditions(context, entry.get("conditions", {}))

        if conditions is not None:
            result.append({
                "fields": fields,
                "for": conditions,
            })
    return result

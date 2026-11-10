# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import plistlib
import re
import ssl
import sys
from io import BytesIO
from urllib.request import urlopen
from xml.dom import minidom

import certifi
from mozpack.macpkg import Pbzx, uncpio, unxar

STRING_VALUES_RE = re.compile(r'"(?P<key>[^"]+)"\s*=\s*"(?P<value>[^"]+)";')


def get_english(dict, default=None):
    english = dict.get("English")
    if english is None:
        english = dict.get("en", default)
    return english


def get_content_at(url):
    ssl_context = ssl.create_default_context(cafile=certifi.where())
    f = urlopen(url, context=ssl_context)
    return f.read()


def get_plist_at(url):
    return plistlib.loads(get_content_at(url))


def show_package_content(url, digest=None, size=None):
    package = get_content_at(url)
    if size is not None and len(package) != size:
        print(f"Package does not match size given in catalog: {url}", file=sys.stderr)
        sys.exit(1)
    # Ideally we'd check the digest, but it's not md5, sha1 or sha256...
    # if digest is not None and hashlib.???(package).hexdigest() != digest:
    #     print(f"Package does not match digest given in catalog: {url}", file=sys.stderr)
    #     sys.exit(1)
    for name, content in unxar(BytesIO(package)):
        if name == "Payload":
            for path, _, __ in uncpio(Pbzx(content)):
                if path:
                    print(path.decode("utf-8"))


def show_product_info(product, package_id=None):
    # An alternative here would be to look at the MetadataURLs in
    # product["Packages"], but going with Distributions allows to
    # only do one request.
    dist = get_english(product.get("Distributions"))
    data = get_content_at(dist)
    dom = minidom.parseString(data.decode("utf-8"))
    for pkg_ref in dom.getElementsByTagName("pkg-ref"):
        if pkg_ref.childNodes:
            if pkg_ref.hasAttribute("packageIdentifier"):
                id = pkg_ref.attributes["packageIdentifier"].value
            else:
                id = pkg_ref.attributes["id"].value

            if package_id and package_id != id:
                continue

            for child in pkg_ref.childNodes:
                if child.nodeType != minidom.Node.TEXT_NODE:
                    continue
                for p in product["Packages"]:
                    if p["URL"].endswith("/" + child.data):
                        if package_id:
                            show_package_content(
                                p["URL"], p.get("Digest"), p.get("Size")
                            )
                        else:
                            print(id, p["URL"])


def product_matches_filter(product, filter=None):
    return filter and all(
        filter not in package.get("MetadataURL", "")
        for package in product.get("Packages", [])
    )


def show_products(products, filter=None):
    for key, product in products.items():
        if product_matches_filter(product, filter):
            continue
        if metadata_url := product.get("ServerMetadataURL"):
            metadata = get_plist_at(metadata_url)
            localization = get_english(metadata.get("localization", {}), {})
            title = localization.get("title", None)
            version = metadata.get("CFBundleShortVersionString", None)
            print(key, title, version)
        elif dist := get_english(product.get("Distributions", {})):
            data = get_content_at(dist)
            dom = minidom.parseString(data.decode("utf-8"))
            strings = {}
            for s in dom.getElementsByTagName("strings"):
                if s.firstChild:
                    for m in STRING_VALUES_RE.finditer(s.firstChild.data):
                        strings[m.group("key")] = m.group("value")
            titles = dom.getElementsByTagName("title")
            title = (
                titles[0].firstChild.data if titles and titles[0].firstChild else None
            )
            title = strings.get(title, title)
            print(key, title)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--catalog",
        help="URL of the catalog",
        default="https://swscan.apple.com/content/catalogs/others/index-26-15-14-13-12-10.16-10.15-10.14-10.13-10.12-10.11-10.10-10.9-mountainlion-lion-snowleopard-leopard.merged-1.sucatalog",
    )
    parser.add_argument(
        "--filter", help="Only show entries with metadata url matching the filter"
    )
    parser.add_argument(
        "what", nargs="?", help="Show packages information about the given entry"
    )
    args = parser.parse_args()

    data = get_plist_at(args.catalog)
    products = data["Products"]

    if args.what:
        if args.filter:
            print(
                "Cannot use --filter when showing verbose information about an entry",
                file=sys.stderr,
            )
            sys.exit(1)
        product_id, _, package_id = args.what.partition("/")
        show_product_info(products[product_id], package_id)
    else:
        show_products(products, args.filter)


if __name__ == "__main__":
    main()

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import sys
from pathlib import Path
from typing import Union
from unittest.mock import Mock

from mach.site import MozSiteMetadata, SitePackagesSource


class NoopTelemetry:
    def __init__(self, failed_glean_import):
        self._failed_glean_import = failed_glean_import

    def metrics(self, metrics_path: Union[str, Path]):
        return Mock()

    def submit(self, is_bootstrap):
        if self._failed_glean_import and not is_bootstrap:
            active_site = MozSiteMetadata.from_runtime()
            if active_site.mach_site_packages_source == SitePackagesSource.SYSTEM:
                hint = (
                    "Mach is looking for glean in the system packages. This can be "
                    "resolved by installing it there, or by allowing Mach to run "
                    "without using the system Python packages."
                )
            elif active_site.mach_site_packages_source == SitePackagesSource.NONE:
                hint = (
                    "This is because Mach is currently configured without a source "
                    "for native Python packages."
                )
            else:
                hint = "You may need to run |mach bootstrap|."

            print(
                f"Glean could not be found, so telemetry will not be reported. {hint}",
                file=sys.stderr,
            )


class GleanTelemetry:
    """Records and sends Telemetry using Glean.

    Metrics are defined in python/mozbuild/metrics.yaml.
    Pings are defined in python/mozbuild/pings.yaml.

    The "metrics" and "pings" properties may be replaced with no-op implementations if
    Glean isn't available. This allows consumers to report telemetry without having
    to guard against incompatible environments.
    """

    def __init__(
        self,
        upload_enabled: bool,
        data_dir: Path,
    ):
        from glean import Glean, load_pings

        self._metrics_cache = {}
        # We must call load_pings before Glean.initialize to load and register pings;
        # otherwise, metrics won’t be recorded for them.
        self._pings = load_pings(Path(__file__).parent.parent / "pings.yaml")

        Glean.initialize(
            application_id="mozilla.mach",
            application_version="Unknown",
            upload_enabled=upload_enabled,
            data_dir=data_dir,
        )

    def metrics(self, metrics_path: Union[str, Path]):
        if metrics_path not in self._metrics_cache:
            from glean import load_metrics

            metrics = load_metrics(metrics_path)
            self._metrics_cache[metrics_path] = metrics

        return self._metrics_cache[metrics_path]

    def submit(self, _):
        self._pings.usage.submit()

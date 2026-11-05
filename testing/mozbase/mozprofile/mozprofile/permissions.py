# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.


"""
add permissions to the profile
"""

import codecs
import os
from urllib import parse

__all__ = [
    "MissingPrimaryLocationError",
    "MultiplePrimaryLocationsError",
    "DEFAULT_PORTS",
    "DuplicateLocationError",
    "BadPortLocationError",
    "LocationsSyntaxError",
    "Location",
    "ServerLocations",
    "Permissions",
]

# http://hg.mozilla.org/mozilla-central/file/b871dfb2186f/build/automation.py.in#l28
DEFAULT_PORTS = {"http": "8888", "https": "4443", "ws": "9988", "wss": "4443"}


class LocationError(Exception):
    """Signifies an improperly formed location."""

    def __str__(self):
        s = "Bad location"
        m = str(Exception.__str__(self))
        if m:
            s += ": %s" % m
        return s


class MissingPrimaryLocationError(LocationError):
    """No primary location defined in locations file."""

    def __init__(self):
        LocationError.__init__(self, "missing primary location")


class MultiplePrimaryLocationsError(LocationError):
    """More than one primary location defined."""

    def __init__(self):
        LocationError.__init__(self, "multiple primary locations")


class DuplicateLocationError(LocationError):
    """Same location defined twice."""

    def __init__(self, url):
        LocationError.__init__(self, "duplicate location: %s" % url)


class BadPortLocationError(LocationError):
    """Location has invalid port value."""

    def __init__(self, given_port):
        LocationError.__init__(self, "bad value for port: %s" % given_port)


class LocationsSyntaxError(Exception):
    """Signifies a syntax error on a particular line in server-locations.txt."""

    def __init__(self, lineno, err=None):
        self.err = err
        self.lineno = lineno

    def __str__(self):
        s = "Syntax error on line %s" % self.lineno
        if self.err:
            s += ": %s." % self.err
        else:
            s += "."
        return s


class Location:
    """Represents a location line in server-locations.txt."""

    attrs = ("scheme", "host", "port")

    def __init__(self, scheme, host, port, options):
        for attr in self.attrs:
            setattr(self, attr, locals()[attr])
        self.options = options
        try:
            int(self.port)
        except ValueError:
            raise BadPortLocationError(self.port)

    def isEqual(self, location):
        """compare scheme://host:port, but ignore options"""
        return len([
            i for i in self.attrs if getattr(self, i) == getattr(location, i)
        ]) == len(self.attrs)

    __eq__ = isEqual

    def __hash__(self):
        # pylint --py3k: W1641
        return hash(tuple(getattr(attr) for attr in self.attrs))

    def url(self):
        return "%s://%s:%s" % (self.scheme, self.host, self.port)

    def __str__(self):
        return "%s  %s" % (self.url(), ",".join(self.options))


class ServerLocations:
    """Iterable collection of locations.
    Use provided functions to add new locations, rather that manipulating
    _locations directly, in order to check for errors and to ensure the
    callback is called, if given.
    """

    def __init__(self, filename=None):
        self._locations = []
        self.hasPrimary = False
        if filename:
            self.read(filename)

    def __iter__(self):
        return self._locations.__iter__()

    def __len__(self):
        return len(self._locations)

    def add(self, location):
        if "primary" in location.options:
            if self.hasPrimary:
                raise MultiplePrimaryLocationsError()
            self.hasPrimary = True

        self._locations.append(location)

    def add_host(self, host, port="80", scheme="http", options="privileged"):
        if isinstance(options, str):
            options = options.split(",")
        self.add(Location(scheme, host, port, options))

    def read(self, filename, check_for_primary=True):
        """
        Reads the file and adds all valid locations to the ``self._locations`` array.

        :param filename: in the format of
          :searchfox:`server-locations.txt <build/pgo/server-locations.txt>`
        :param check_for_primary: if True, a ``MissingPrimaryLocationError`` exception is raised
          if no primary is found

        The only exception is that the port, if not defined, defaults to 80 or 443.

        FIXME: Shouldn't this default to the protocol-appropriate port?  Is
        there any reason to have defaults at all?
        """

        locationFile = codecs.open(filename, "r", "UTF-8")
        lineno = 0
        new_locations = []

        for line in locationFile:
            line = line.strip()
            lineno += 1

            # check for comments and blank lines
            if line.startswith("#") or not line:
                continue

            # split the server from the options
            try:
                server, options = line.rsplit(None, 1)
                options = options.split(",")
            except ValueError:
                server = line
                options = []

            # parse the server url
            if "://" not in server:
                server = "http://" + server
            scheme, netloc, path, query, fragment = parse.urlsplit(server)
            # get the host and port
            try:
                host, port = netloc.rsplit(":", 1)
            except ValueError:
                host = netloc
                port = DEFAULT_PORTS.get(scheme, "80")

            try:
                location = Location(scheme, host, port, options)
                self.add(location)
            except LocationError as e:
                raise LocationsSyntaxError(lineno, e)

            new_locations.append(location)

        # ensure that a primary is found
        if check_for_primary and not self.hasPrimary:
            raise LocationsSyntaxError(lineno + 1, MissingPrimaryLocationError())


class Permissions:
    """Allows handling of permissions for ``mozprofile``"""

    def __init__(self, locations=None):
        self._locations = ServerLocations()
        if locations:
            if isinstance(locations, ServerLocations):
                self._locations = locations
            elif isinstance(locations, list):
                for l in locations:
                    self._locations.add_host(**l)
            elif isinstance(locations, dict):
                self._locations.add_host(**locations)
            elif os.path.exists(locations):
                self._locations.read(locations)

    def network_prefs(self, proxy=None):
        """
        take known locations and generate preferences to handle permissions and proxy
        returns a tuple of prefs, user_prefs
        """

        prefs = []

        if proxy:
            dohServerPort = proxy.get("dohServerPort")
            if dohServerPort is not None:
                # make sure we don't use proxy
                user_prefs = [("network.proxy.type", 0)]
                # Use TRR_ONLY mode
                user_prefs.append(("network.trr.mode", 3))
                trrUri = f"https://foo.example.com:{dohServerPort}/dns-query"
                user_prefs.append(("network.trr.uri", trrUri))
                user_prefs.append(("network.trr.bootstrapAddr", "127.0.0.1"))
                user_prefs.append(("network.dns.force_use_https_rr", True))
                user_prefs.append((
                    "network.dns.https_rr.check_record_with_cname",
                    False,
                ))
                user_prefs.append(("network.dns.port_prefixed_qname_https_rr", False))
            else:
                user_prefs = self.pac_prefs(proxy)
        else:
            user_prefs = []

        return prefs, user_prefs

    def pac_prefs(self, user_proxy=None):
        """
        return preferences for Proxy Auto Config.
        """
        proxy = DEFAULT_PORTS.copy()

        # We need to proxy every server but the primary one.
        origins = ["'%s'" % l.url() for l in self._locations]
        origins = ", ".join(origins)
        proxy["origins"] = origins

        for l in self._locations:
            if "primary" in l.options:
                proxy["remote"] = l.host
                proxy[l.scheme] = l.port

        # overwrite defaults with user specified proxy
        if isinstance(user_proxy, dict):
            proxy.update(user_proxy)

        # TODO: this should live in a template!
        # If you must escape things in this string with backslashes, be aware
        # of the multiple layers of escaping at work:
        #
        # - Python will unescape backslashes;
        # - Writing out the prefs will escape things via JSON serialization;
        # - The prefs file reader will unescape backslashes;
        # - The JS engine parser will unescape backslashes.
        pacURL = (
            """data:text/plain,
var knownOrigins = (function () {
  return [%(origins)s].reduce(function(t, h) { t[h] = true; return t; }, {})
})();
var uriRegex = new RegExp('^([a-z][-a-z0-9+.]*)' +
                          '://' +
                          '(?:[^/@]*@)?' +
                          '(.*?)' +
                          '(?::(\\\\d+))?/');
var defaultPortsForScheme = {
  'http': 80,
  'ws': 80,
  'https': 443,
  'wss': 443
};
var originSchemesRemap = {
  'ws': 'http',
  'wss': 'https'
};
var proxyForScheme = {
  'http': 'PROXY %(remote)s:%(http)s',
  'https': 'PROXY %(remote)s:%(https)s',
  'ws': 'PROXY %(remote)s:%(ws)s',
  'wss': 'PROXY %(remote)s:%(wss)s'
};

function FindProxyForURL(url, host)
{
  var matches = uriRegex.exec(url);
  if (!matches)
    return 'DIRECT';
  var originalScheme = matches[1];
  var host = matches[2];
  var port = matches[3];
  if (!port && originalScheme in defaultPortsForScheme) {
    port = defaultPortsForScheme[originalScheme];
  }
  var schemeForOriginChecking = originSchemesRemap[originalScheme] || originalScheme;

  var origin = schemeForOriginChecking + '://' + host + ':' + port;
  if (!(origin in knownOrigins))
    return 'DIRECT';
  return proxyForScheme[originalScheme] || 'DIRECT';
}"""
            % proxy
        )
        pacURL = "".join(pacURL.splitlines())

        prefs = []
        prefs.append(("network.proxy.type", 2))
        prefs.append(("network.proxy.autoconfig_url", pacURL))

        return prefs

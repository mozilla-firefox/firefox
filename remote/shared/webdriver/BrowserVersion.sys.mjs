/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const WILDCARDS = ["*", "x", "X"];

/**
 * Determines whether a browser version satisfies a version requirement.
 *
 * This is a port of the version matching from Mozilla's Rust mozversion
 * implementation:
 * https://searchfox.org/firefox-main/source/testing/mozbase/rust/mozversion/src/lib.rs
 *
 * @param {string} browserVersion
 *     Browser version to test, such as `123.0`, `123.0.1esr`, or `50.0a1`.
 * @param {string} versionRequirement
 *     Requirement to test against, optionally a comma-separated list.
 *
 * @returns {boolean}
 *     True when every requirement matches the browser version.
 */
export function browserVersionMatches(browserVersion, versionRequirement) {
  const version = browserVersion.match(
    /^(?<major>\d+)\.(?<minor>\d+)(?:\.(?<patch>\d+))?(?:(?:esr)|(?:-|[a-z]+)\d+)?$/
  );
  if (!version) {
    return false;
  }

  if (WILDCARDS.includes(versionRequirement.trim())) {
    return true;
  }

  const actual = {
    major: Number(version.groups.major),
    minor: Number(version.groups.minor),
    patch: Number(version.groups.patch ?? 0),
  };
  const constraints = versionRequirement.split(",");

  return constraints.every(constraint =>
    browserVersionMatchesConstraint(actual, constraint)
  );
}

function browserVersionMatchesConstraint(actual, constraint) {
  constraint = constraint.trim();
  const match = constraint.match(
    /^(?<operator>\^|~|>=|<=|>|<|=)?\s*(?<major>0|[1-9]\d*)(?:\.(?<minor>0|[1-9]\d*|[*xX])(?:\.(?<patch>0|[1-9]\d*)(?:-(?<pre>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+(?:[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?|\.(?<patchWildcard>[*xX]))?)?$/
  );
  if (
    !match ||
    (WILDCARDS.includes(match.groups.minor) &&
      match.groups.patch !== undefined) ||
    (match.groups.pre !== undefined &&
      !browserVersionIsValidPrerelease(match.groups.pre))
  ) {
    return false;
  }

  const requested = {
    major: Number(match.groups.major),
    minor:
      match.groups.minor && !WILDCARDS.includes(match.groups.minor)
        ? Number(match.groups.minor)
        : null,
    patch: match.groups.patch !== undefined ? Number(match.groups.patch) : null,
    pre: match.groups.pre ?? null,
  };

  const operator =
    match.groups.operator ??
    (match.groups.patchWildcard || WILDCARDS.includes(match.groups.minor)
      ? "="
      : "^");

  switch (operator) {
    case "=":
      return browserVersionMatchesExactly(actual, requested);
    case ">":
      return browserVersionMatchesGreater(actual, requested);
    case ">=":
      return (
        browserVersionMatchesExactly(actual, requested) ||
        browserVersionMatchesGreater(actual, requested)
      );
    case "<":
      return browserVersionMatchesLess(actual, requested);
    case "<=":
      return (
        browserVersionMatchesExactly(actual, requested) ||
        browserVersionMatchesLess(actual, requested)
      );
    case "~":
      return browserVersionMatchesTilde(actual, requested);
    case "^":
      return browserVersionMatchesCaret(actual, requested);
  }

  return false;
}

function browserVersionIsValidPrerelease(prerelease) {
  return prerelease
    .split(".")
    .every(
      segment =>
        !(
          segment.length > 1 &&
          /^\d+$/.test(segment) &&
          segment.startsWith("0")
        )
    );
}

function browserVersionCompare(actual, requested) {
  if (actual.major !== requested.major) {
    return actual.major < requested.major ? -1 : 1;
  }
  if (requested.minor === null) {
    return 0;
  }
  if (actual.minor !== requested.minor) {
    return actual.minor < requested.minor ? -1 : 1;
  }
  if (requested.patch === null) {
    return 0;
  }
  if (actual.patch !== requested.patch) {
    return actual.patch < requested.patch ? -1 : 1;
  }
  return 0;
}

function browserVersionMatchesExactly(actual, requested) {
  return (
    browserVersionCompare(actual, requested) === 0 && requested.pre === null
  );
}

function browserVersionMatchesGreater(actual, requested) {
  const comparison = browserVersionCompare(actual, requested);
  return comparison === 1 || (comparison === 0 && requested.pre !== null);
}

function browserVersionMatchesLess(actual, requested) {
  return browserVersionCompare(actual, requested) === -1;
}

function browserVersionMatchesTilde(actual, requested) {
  return (
    actual.major == requested.major &&
    (requested.minor === null || actual.minor == requested.minor) &&
    (requested.patch === null || actual.patch >= requested.patch)
  );
}

function browserVersionMatchesCaret(actual, requested) {
  if (actual.major !== requested.major) {
    return false;
  }
  if (requested.minor === null) {
    return true;
  }
  if (requested.patch === null) {
    return requested.major > 0
      ? actual.minor >= requested.minor
      : actual.minor == requested.minor;
  }
  if (requested.major > 0) {
    return (
      actual.minor > requested.minor ||
      (actual.minor == requested.minor && actual.patch >= requested.patch)
    );
  }
  if (requested.minor > 0) {
    return actual.minor == requested.minor && actual.patch >= requested.patch;
  }
  return actual.minor == requested.minor && actual.patch == requested.patch;
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* This dictionary is used for the strip on share feature.
 * It stores a list of query parameters and a list of sites
 * for which the query parameters are stripped
 */
[GenerateInitFromJSON]
dictionary StripRule {
  sequence<UTF8String> queryParams = [];
  boolean isGlobal = false;
  sequence<UTF8String> origins = [];
  sequence<UTF8String> schemelessSites = [];
};

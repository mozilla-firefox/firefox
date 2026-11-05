/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

function NetworkGeoCoordsObject(lat, lon, acc) {
  this.latitude = lat;
  this.longitude = lon;
  this.accuracy = acc;

  // Neither GLS nor MLS return the following properties, so set them to NaN
  // here. nsGeoPositionCoords will convert NaNs to null for optional properties
  // of the JavaScript Coordinates object.
  this.altitude = NaN;
  this.altitudeAccuracy = NaN;
  this.heading = NaN;
  this.speed = NaN;
}

NetworkGeoCoordsObject.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIDOMGeoPositionCoords"]),
};

export function NetworkGeoPositionObject(lat, lng, acc) {
  this.coords = new NetworkGeoCoordsObject(lat, lng, acc);
  this.address = null;
  this.timestamp = Date.now();
}

NetworkGeoPositionObject.prototype = {
  QueryInterface: ChromeUtils.generateQI(["nsIDOMGeoPosition"]),
};

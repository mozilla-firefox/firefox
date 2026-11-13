/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */
import FeatureManifest
import Foundation

class PrefNimbusFeatures {
    private let _userDefaults: UserDefaults
    private let nimbus: HardcodedNimbusFeatures

    init(_ prefs: UserDefaults, _ nimbus: HardcodedNimbusFeatures) {
        self._userDefaults = prefs
        self.nimbus = nimbus
    }
}

extension PrefNimbusFeatures: FeaturesInterface {
    public var userDefaults: UserDefaults? {
        get {
            _userDefaults
        }
    }

    public func getVariables(featureId: String, sendExposureEvent: Bool) -> Variables {
        return nimbus.getVariables(featureId: featureId, sendExposureEvent: sendExposureEvent)
    }

    public func recordExposureEvent(featureId: String, experimentSlug: String?) {
        nimbus.recordExposureEvent(featureId: featureId, experimentSlug: experimentSlug)
    }

    public func recordMalformedConfiguration(featureId: String, with partId: String) {
        nimbus.recordMalformedConfiguration(featureId: featureId, with: partId)
    }
}

// Test the defaults still work.
let feature0 = AppConfig.shared.features.myFeature.value()

assert(feature0.myBoolean == false)
assert(feature0.myInt == 0)
assert(feature0.myString == "from manifest")
assert(feature0.myText == "from manifest")
assert(!feature0.isModified())

// Now test that JSON still has an effect.
let prefs = UserDefaults()
prefs.removeObject(forKey: "my-boolean-pref-key")
prefs.removeObject(forKey: "my-int-pref-key")
prefs.removeObject(forKey: "my-string-pref-key")
prefs.removeObject(forKey: "my-text-pref-key")

let nimbusFromRust = HardcodedNimbusFeatures(with:
    ["my-feature": [
        "my-boolean": false,
        "my-int": 100,
        "my-string": "from json",
        "my-text": "from json"
    ]]
)
let nimbus = PrefNimbusFeatures(prefs, nimbusFromRust)
AppConfig.shared.initialize { nimbus }

let feature = AppConfig.shared.features.myFeature.value()

assert(feature.myBoolean == false)
assert(feature.myInt == 100)
assert(feature.myString == "from json")
assert(feature.myText == "from json")
assert(!feature.isModified())

// Now set with prefs.

prefs.set(true, forKey: "my-boolean-pref-key")
prefs.set(42, forKey: "my-int-pref-key")
prefs.set("from pref", forKey: "my-string-pref-key")
prefs.set("from pref", forKey: "my-text-pref-key")

assert(feature.myBoolean == true)
assert(feature.myInt == 42)
assert(feature.myString == "from pref")
assert(feature.myText == "from pref")
assert(feature.isModified())

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import android.content.Context as MockContext
import android.content.SharedPreferences as MockSharedPreferences

import com.example.nimbus.AppConfig

import org.mozilla.experiments.nimbus.HardcodedNimbusFeatures
import org.mozilla.experiments.nimbus.FeaturesInterface

import org.json.JSONObject

class PrefNimbusFeatures(
    override val prefs: MockSharedPreferences,
    val nimbus: HardcodedNimbusFeatures,
): FeaturesInterface {
    override val context: MockContext = nimbus.context
    override fun getVariables(featureId: String, recordExposureEvent: Boolean) =
        nimbus.getVariables(featureId, recordExposureEvent)
}

val context = MockContext()
val prefs = MockSharedPreferences()
val nimbusFromRust = HardcodedNimbusFeatures(context,
    "my-feature" to JSONObject(mapOf(
        "my-boolean" to false,
        "my-int" to 100,
        "my-string" to "from json",
        "my-text" to "from json"
    ))
)

// Before initialization with hardcoded, just get values from the manifest.
val feature0 = AppConfig.features.myFeature.value()

assert(feature0.myBoolean == false)
assert(feature0.myInt == 0)
assert(feature0.myString == "from manifest")
assert(feature0.myText == "from manifest")
assert(!feature0.isModified())


val nimbus = PrefNimbusFeatures(prefs, nimbusFromRust)

AppConfig.initialize { nimbus }

val feature = AppConfig.features.myFeature.value()

assert(feature.myBoolean == false)
assert(feature.myInt == 100)
assert(feature.myString == "from json")
assert(feature.myText == "from json")
assert(!feature.isModified())

prefs.put("my-boolean-pref-key", true)
prefs.put("my-int-pref-key", 42)
prefs.put("my-string-pref-key", "from pref")
prefs.put("my-text-pref-key", "from pref")

assert(feature.myBoolean == true)
assert(feature.myInt == 42)
assert(feature.myString == "from pref")
assert(feature.myText == "from pref")
assert(feature.isModified())

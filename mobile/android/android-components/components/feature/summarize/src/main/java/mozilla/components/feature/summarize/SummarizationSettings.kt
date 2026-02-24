package mozilla.components.feature.summarize

interface SummarizationSettings {
    suspend fun getHasConsentedToShake(): Boolean
    suspend fun setHasConsentedToShake(newValue: Boolean)

    companion object {
        fun inMemory() = object : SummarizationSettings {
            var hasConsentedToShake = false

            override suspend fun getHasConsentedToShake() = hasConsentedToShake

            override suspend fun setHasConsentedToShake(newValue: Boolean) {
                hasConsentedToShake = newValue
            }
        }
    }
}

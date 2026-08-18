/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxrelay

import kotlinx.coroutines.test.runTest
import mozilla.appservices.relay.BounceStatus
import mozilla.appservices.relay.RelayAddress
import mozilla.appservices.relay.RelayApiException
import mozilla.appservices.relay.RelayClientInterface
import mozilla.appservices.relay.RelayProfile
import mozilla.components.concept.sync.AccessTokenInfo
import mozilla.components.concept.sync.AttachedClient
import mozilla.components.concept.sync.DeviceConstellation
import mozilla.components.concept.sync.FxAEntryPoint
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.Profile
import mozilla.components.concept.sync.StatePersistenceCallback
import mozilla.components.service.fxrelay.eligibility.RelayPlanTier
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FxRelayTest {

    // fetchAccountDetails tests
    @Test
    fun `GIVEN free tier with 0 masks WHEN fetchAccountDetails THEN totalMasksUsed is 0`() =
        runTest {
            val fxRelay = createFxRelay(profile = fakeProfile(hasPremium = false, totalMasks = 0L))
            val result = fxRelay.fetchAccountDetails()
            assertEquals(RelayPlanTier.FREE, result?.relayPlanTier)
            assertEquals(0, result?.totalMasksUsed)
        }

    @Test
    fun `GIVEN free tier with 1 mask WHEN fetchAccountDetails THEN totalMasksUsed is 1`() =
        runTest {
            val fxRelay = createFxRelay(profile = fakeProfile(hasPremium = false, totalMasks = 1L))
            val result = fxRelay.fetchAccountDetails()
            assertEquals(RelayPlanTier.FREE, result?.relayPlanTier)
            assertEquals(1, result?.totalMasksUsed)
        }

    // A free tier can have existing email masks higher than the tier limit if they were previously a premium user.
    @Test
    fun `GIVEN free tier with 6 masks WHEN fetchAccountDetails THEN totalMasksUsed is 6`() =
        runTest {
            val fxRelay = createFxRelay(profile = fakeProfile(hasPremium = false, totalMasks = 6L))
            val result = fxRelay.fetchAccountDetails()
            assertEquals(RelayPlanTier.FREE, result?.relayPlanTier)
            assertEquals(6, result?.totalMasksUsed)
        }

    @Test
    fun `GIVEN premium tier WHEN fetchAccountDetails THEN plan tier is PREMIUM`() = runTest {
        val fxRelay = createFxRelay(profile = fakeProfile(hasPremium = true, totalMasks = 6L))
        val result = fxRelay.fetchAccountDetails()
        assertEquals(RelayPlanTier.PREMIUM, result?.relayPlanTier)
        assertEquals(6, result?.totalMasksUsed)
    }

    @Test
    fun `GIVEN megabundle tier WHEN fetchAccountDetails THEN plan tier is PREMIUM`() = runTest {
        val fxRelay = createFxRelay(profile = fakeProfile(hasMegabundle = true))
        val result = fxRelay.fetchAccountDetails()

        assertEquals(RelayPlanTier.PREMIUM, result?.relayPlanTier)
    }

    @Test
    fun `GIVEN no access token WHEN fetchAccountDetails THEN returns null`() = runTest {
        assertNull(createFxRelay(token = null).fetchAccountDetails())
    }

    @Test
    fun `GIVEN profile fetch throws API error WHEN fetchAccountDetails THEN returns null`() =
        runTest {
            val fxRelay = createFxRelay(
                fetchProfileException = RelayApiException.Api(
                    404u,
                    "not_found",
                    "Profile not found",
                ),
            )
            assertNull(fxRelay.fetchAccountDetails())
        }

    @Test
    fun `GIVEN network error WHEN fetchAccountDetails THEN returns null`() = runTest {
        val fxRelay = createFxRelay(
            fetchProfileException = RelayApiException.Network("timeout"),
        )
        assertNull(fxRelay.fetchAccountDetails())
    }

    // fetchEmailMasks tests
    @Test
    fun `GIVEN valid token and addresses WHEN fetchEmailMasks THEN returns list of email masks`() =
        runTest {
            val fxRelay =
                createFxRelay(addresses = listOf(fakeAddress(fullAddress = "test@relay.firefox.com")))
            assertEquals(
                listOf(EmailMask("test@relay.firefox.com", null)),
                fxRelay.fetchEmailMasks(),
            )
        }

    @Test
    fun `GIVEN no addresses WHEN fetchEmailMasks THEN returns empty list`() = runTest {
        assertEquals(
            emptyList<EmailMask>(),
            createFxRelay(addresses = emptyList()).fetchEmailMasks(),
        )
    }

    @Test
    fun `GIVEN no access token WHEN fetchEmailMasks THEN returns null`() = runTest {
        assertNull(createFxRelay(token = null).fetchEmailMasks())
    }

    @Test
    fun `GIVEN network error WHEN fetchEmailMasks THEN returns null`() = runTest {
        val fxRelay = createFxRelay(
            fetchAddressesException = RelayApiException.Network("timeout"),
        )
        assertNull(fxRelay.fetchEmailMasks())
    }

    @Test
    fun `GIVEN API error WHEN fetchEmailMasks THEN returns null`() = runTest {
        val fxRelay = createFxRelay(
            fetchAddressesException = RelayApiException.Api(500u, "server_error", "Internal error"),
        )
        assertNull(fxRelay.fetchEmailMasks())
    }

    @Test
    fun `GIVEN other error WHEN fetchEmailMasks THEN returns null`() = runTest {
        val fxRelay = createFxRelay(
            fetchAddressesException = RelayApiException.Other("unexpected error"),
        )
        assertNull(fxRelay.fetchEmailMasks())
    }

    @Test
    fun `GIVEN multiple addresses WHEN fetchEmailMasks THEN returns all masks mapped correctly`() =
        runTest {
            val fxRelay = createFxRelay(
                addresses = listOf(
                    fakeAddress(fullAddress = "first@relay.firefox.com"),
                    fakeAddress(fullAddress = "second@relay.firefox.com"),
                    fakeAddress(fullAddress = "third@relay.firefox.com"),
                ),
            )
            assertEquals(
                listOf(
                    EmailMask("first@relay.firefox.com", null),
                    EmailMask("second@relay.firefox.com", null),
                    EmailMask("third@relay.firefox.com", null),
                ),
                fxRelay.fetchEmailMasks(),
            )
        }

    // createEmailMask tests
    @Test
    fun `GIVEN valid token WHEN createEmailMask THEN returns mask with GENERATED source`() =
        runTest {
            val fxRelay =
                createFxRelay(createAddressResult = fakeAddress(fullAddress = "new@relay.firefox.com"))
            val result = fxRelay.createEmailMask(generatedForHostUrl = "example.com", descriptionHostUrl = "test")
            assertEquals(EmailMask("new@relay.firefox.com", MaskSource.GENERATED), result)
        }

    @Test
    fun `GIVEN free tier limit reached and existing masks WHEN createEmailMask THEN returns existing mask with FREE_TIER_LIMIT source`() =
        runTest {
            val fxRelay = createFxRelay(
                addresses = listOf(fakeAddress(fullAddress = "existing@relay.firefox.com")),
                createAddressException = RelayApiException.Api(
                    400u,
                    "free_tier_limit",
                    "Limit reached",
                ),
            )
            val result = fxRelay.createEmailMask()
            assertEquals(MaskSource.FREE_TIER_LIMIT, result?.source)
            assertEquals("existing@relay.firefox.com", result?.fullAddress)
        }

    @Test
    fun `GIVEN free tier limit reached and no existing masks WHEN createEmailMask THEN returns null`() =
        runTest {
            val fxRelay = createFxRelay(
                addresses = emptyList(),
                createAddressException = RelayApiException.Api(
                    400u,
                    "free_tier_limit",
                    "Limit reached",
                ),
            )
            assertNull(fxRelay.createEmailMask())
        }

    @Test
    fun `GIVEN non-free_tier_limit API error WHEN createEmailMask THEN returns null`() = runTest {
        val fxRelay = createFxRelay(
            createAddressException = RelayApiException.Api(500u, "server_error", "Internal error"),
        )
        assertNull(fxRelay.createEmailMask())
    }

    @Test
    fun `GIVEN network error WHEN createEmailMask THEN returns null`() = runTest {
        val fxRelay = createFxRelay(
            createAddressException = RelayApiException.Network("no connection"),
        )
        assertNull(fxRelay.createEmailMask())
    }

    @Test
    fun `GIVEN Other error WHEN createEmailMask THEN returns null`() = runTest {
        val fxRelay = createFxRelay(
            createAddressException = RelayApiException.Other("unexpected error"),
        )
        assertNull(fxRelay.createEmailMask())
    }

    @Test
    fun `GIVEN no access token WHEN createEmailMask THEN returns null`() = runTest {
        assertNull(createFxRelay(token = null).createEmailMask())
    }

    // Client caching
    @Test
    fun `GIVEN same token on consecutive calls WHEN fetching THEN client is reused`() = runTest {
        var callCount = 0
        val fxRelay = FxRelayImpl(
            account = FakeOAuthAccount { "stable-token" },
            relayClientProvider = { callCount++; FakeRelayClient() },
        )
        fxRelay.fetchEmailMasks()
        fxRelay.fetchEmailMasks()
        assertEquals(1, callCount)
    }

    @Test
    fun `GIVEN token changes between calls WHEN fetching THEN new client is created`() = runTest {
        var callCount = 0
        var currentToken = "token-1"
        val fxRelay = FxRelayImpl(
            account = FakeOAuthAccount { currentToken },
            relayClientProvider = { callCount++; FakeRelayClient() },
        )
        fxRelay.fetchEmailMasks()
        currentToken = "token-2"
        fxRelay.fetchEmailMasks()
        assertEquals(2, callCount)
    }

    private fun createFxRelay(
        token: String? = "test-token",
        addresses: List<RelayAddress> = emptyList(),
        profile: RelayProfile = fakeProfile(),
        createAddressResult: RelayAddress = fakeAddress(),
        fetchAddressesException: RelayApiException? = null,
        fetchProfileException: RelayApiException? = null,
        createAddressException: RelayApiException? = null,
    ) = FxRelayImpl(
        account = FakeOAuthAccount { token },
        relayClientProvider = {
            FakeRelayClient(
                addresses = addresses,
                profile = profile,
                createAddressResult = createAddressResult,
                fetchAddressesException = fetchAddressesException,
                fetchProfileException = fetchProfileException,
                createAddressException = createAddressException,
            )
        },
    )

    private fun fakeProfile(
        hasPremium: Boolean = false,
        hasMegabundle: Boolean = false,
        totalMasks: Long = 0L,
    ) = RelayProfile(
        id = 0L,
        serverStorage = true,
        storePhoneLog = false,
        subdomain = "",
        hasPhone = false,
        hasVpn = false,
        hasPremium = hasPremium,
        hasMegabundle = hasMegabundle,
        onboardingState = 0L,
        onboardingFreeState = 0L,
        datePhoneRegistered = null,
        dateSubscribed = null,
        avatar = "",
        nextEmailTry = "",
        bounceStatus = BounceStatus(paused = false, bounceType = ""),
        apiToken = "",
        emailsBlocked = 0L,
        emailsForwarded = 0L,
        emailsReplied = 0L,
        levelOneTrackersBlocked = 0L,
        removeLevelOneEmailTrackers = false,
        totalMasks = totalMasks,
        atMaskLimit = false,
        metricsEnabled = false,
    )

    private fun fakeAddress(fullAddress: String = "fake@relay.firefox.com") = RelayAddress(
        maskType = "random",
        enabled = true,
        description = "",
        generatedFor = "",
        blockListEmails = false,
        usedOn = null,
        id = 1L,
        address = "fake",
        domain = 1L,
        fullAddress = fullAddress,
        createdAt = "",
        lastModifiedAt = "",
        lastUsedAt = null,
        numForwarded = 0L,
        numBlocked = 0L,
        numLevelOneTrackersBlocked = 0L,
        numReplied = 0L,
        numSpam = 0L,
    )

    private class FakeOAuthAccount(
        private val tokenProvider: () -> String?,
    ) : OAuthAccount {
        override suspend fun getAccessToken(singleScope: String): AccessTokenInfo? {
            val token = tokenProvider() ?: return null
            return AccessTokenInfo(
                scope = singleScope,
                token = token,
                key = null,
                expiresAt = Long.MAX_VALUE,
            )
        }

        override suspend fun getAttachedClient(): List<AttachedClient> = emptyList()
        override fun getCurrentDeviceId(): String? = null
        override suspend fun handleWebChannelLogin(jsonPayload: String) = Unit
        override fun getSignedInUserForWebChannel(): String? = null
        override fun hasScope(scopes: String) = false
        override suspend fun getProfile(ignoreCache: Boolean): Profile? = null
        override fun authErrorDetected() = Unit
        override suspend fun checkAuthorizationStatus(singleScope: String): Boolean? = null
        override suspend fun getTokenServerEndpointURL(): String? = null
        override suspend fun getManageAccountURL(entryPoint: FxAEntryPoint): String? = null
        override fun getPairingAuthorityURL() = ""
        override fun registerPersistenceCallback(callback: StatePersistenceCallback) = Unit
        override fun deviceConstellation(): DeviceConstellation =
            throw UnsupportedOperationException()

        override suspend fun disconnect() = false
        override fun toJSONString() = ""
        override fun close() = Unit
    }

    private class FakeRelayClient(
        private val addresses: List<RelayAddress> = emptyList(),
        private val profile: RelayProfile? = null,
        private val createAddressResult: RelayAddress? = null,
        private val fetchAddressesException: RelayApiException? = null,
        private val fetchProfileException: RelayApiException? = null,
        private val createAddressException: RelayApiException? = null,
    ) : RelayClientInterface {
        override fun fetchAddresses(): List<RelayAddress> {
            fetchAddressesException?.let { throw it }
            return addresses
        }

        override fun fetchProfile(): RelayProfile {
            fetchProfileException?.let { throw it }
            return profile ?: error("No profile configured for FakeRelayClient")
        }

        override fun createAddress(
            description: String,
            generatedFor: String,
            usedOn: String,
        ): RelayAddress {
            createAddressException?.let { throw it }
            return createAddressResult ?: error("No address configured for FakeRelayClient")
        }

        override fun acceptTerms() = Unit
    }
}

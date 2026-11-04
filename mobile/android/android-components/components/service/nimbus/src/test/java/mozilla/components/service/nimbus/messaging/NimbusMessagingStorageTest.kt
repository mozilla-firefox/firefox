/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.nimbus.messaging

import android.content.Context
import android.net.Uri
import java.util.UUID
import kotlin.test.assertNotNull
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.test.runTest
import mozilla.components.service.nimbus.messaging.ControlMessageBehavior.SHOW_NEXT_MESSAGE
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.mockito.MockitoAnnotations
import org.mozilla.experiments.nimbus.FeaturesInterface
import org.mozilla.experiments.nimbus.NimbusMessagingHelperInterface
import org.mozilla.experiments.nimbus.NimbusMessagingInterface
import org.mozilla.experiments.nimbus.NullVariables
import org.mozilla.experiments.nimbus.Res
import org.mozilla.experiments.nimbus.internal.FeatureHolder
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.robolectric.RobolectricTestRunner

private const val MOCK_TIME_MILLIS = 1000L

@RunWith(RobolectricTestRunner::class)
class NimbusMessagingStorageTest {
    @Mock private lateinit var metadataStorage: MessageMetadataStorage

    @Mock private lateinit var nimbus: NimbusMessagingInterface

    private lateinit var storage: NimbusMessagingStorage
    private lateinit var messagingFeature: FeatureHolder<Messaging>
    private var malformedWasReported = false
    private var malformedMessageIds = mutableSetOf<String>()
    private val reportMalformedMessage: (String) -> Unit = {
        malformedMessageIds.add(it)
        malformedWasReported = true
    }
    private lateinit var featuresInterface: FeaturesInterface

    private val displayOnceStyle = StyleData(maxDisplayCount = 1)

    @Before
    fun setup() {
        MockitoAnnotations.openMocks(this)
        NullVariables.instance.setContext(testContext)

        val (messagingFeatureTmp, featuresInterfaceTmp) = createMessagingFeature()

        messagingFeature = spy(messagingFeatureTmp)
        featuresInterface = featuresInterfaceTmp

        runBlocking {
            `when`(metadataStorage.getMetadata()).thenReturn(mapOf("message-1" to Message.Metadata(id = "message-1")))

            `when`(metadataStorage.addMetadata(any())).thenReturn(mock())
        }

        storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                messagingFeature,
            ) {
                MOCK_TIME_MILLIS
            }

        val helper: NimbusMessagingHelperInterface = mock()
        `when`(helper.evalJexl(any())).thenReturn(true)
        `when`(nimbus.createMessageHelper(any())).thenReturn(helper)
    }

    @After
    fun tearDown() {
        malformedWasReported = false
        malformedMessageIds.clear()
    }

    @Test
    fun `WHEN creating a messaging helper without trigger context THEN the custom attributes are used`() {
        val storage = createStorageWithCustomAttributes("""{"a":1,"b":{"c":true}}""")

        storage.createMessagingHelper()

        val captor = argumentCaptor<JSONObject>()
        verify(nimbus).createMessageHelper(captor.capture())
        assertEquals("""{"a":1,"b":{"c":true}}""", captor.value.toString())
    }

    @Test
    fun `WHEN creating a messaging helper with trigger context THEN it is nested under the triggerContext key`() {
        val storage = createStorageWithCustomAttributes("""{"a":1,"b":{"c":true}}""")

        storage.createMessagingHelper(mapOf("b" to mapOf("d" to "x"), "e" to "y"))

        val captor = argumentCaptor<JSONObject>()
        verify(nimbus).createMessageHelper(captor.capture())
        assertEquals("""{"a":1,"b":{"c":true},"triggerContext":{"b":{"d":"x"},"e":"y"}}""", captor.value.toString())
    }

    @Test
    fun `WHEN getting the next message with trigger context THEN the triggers are evaluated with it`() {
        val storage = createStorageWithCustomAttributes("""{"a":1}""")

        storage.getNextMessage(HOMESCREEN, listOf(), mapOf("e" to "y"))

        val captor = argumentCaptor<JSONObject>()
        verify(nimbus).createMessageHelper(captor.capture())
        assertEquals("""{"a":1,"triggerContext":{"e":"y"}}""", captor.value.toString())
    }

    private fun createStorageWithCustomAttributes(customAttributes: String) =
        NimbusMessagingStorage(
            testContext,
            metadataStorage,
            reportMalformedMessage,
            nimbus,
            messagingFeature,
            attributeProvider =
                object : JexlAttributeProvider {
                    override fun getCustomAttributes(context: Context) = JSONObject(customAttributes)
                },
        )

    @Test
    fun `WHEN calling getCoenrollingFeatureIds THEN messaging is in that list`() {
        assertTrue(FxNimbusMessaging.getCoenrollingFeatureIds().contains(MESSAGING_FEATURE_ID))
    }

    @Test
    fun `WHEN calling getMessages THEN provide a list of available messages for a given surface`() = runTest {
        val homescreenMessage = storage.getMessages().first()

        assertEquals("message-1", homescreenMessage.id)
        assertEquals("message-1", homescreenMessage.metadata.id)

        val notificationMessage = storage.getMessages().last()
        assertEquals("message-2", notificationMessage.id)
    }

    @Test
    fun `WHEN calling getMessages THEN provide a list of sorted messages by priority`() = runTest {
        val messages =
            mapOf(
                "low-message" to createMessageData(style = "low-priority"),
                "high-message" to createMessageData(style = "high-priority"),
                "medium-message" to createMessageData(style = "medium-priority"),
            )
        val styles =
            mapOf(
                "high-priority" to createStyle(priority = 100),
                "medium-priority" to createStyle(priority = 50),
                "low-priority" to createStyle(priority = 1),
            )
        val (messagingFeature, _) =
            createMessagingFeature(
                styles = styles,
                messages = messages,
            )

        val storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                messagingFeature,
            )

        val results = storage.getMessages()

        assertEquals("high-message", results[0].id)
        assertEquals("medium-message", results[1].id)
        assertEquals("low-message", results[2].id)
    }

    @Test
    fun `GIVEN pressed message WHEN calling getMessages THEN filter out the pressed message`() = runTest {
        val metadataList =
            mapOf(
                "pressed-message" to Message.Metadata(id = "pressed-message", pressed = true),
                "normal-message" to Message.Metadata(id = "normal-message", pressed = false),
            )
        val messages =
            mapOf(
                "pressed-message" to createMessageData(style = "high-priority"),
                "normal-message" to createMessageData(style = "high-priority"),
            )
        val styles = mapOf("high-priority" to createStyle(priority = 100))
        val metadataStorage: MessageMetadataStorage = mock()
        val (messagingFeature, _) =
            createMessagingFeature(
                styles = styles,
                messages = messages,
            )

        `when`(metadataStorage.getMetadata()).thenReturn(metadataList)

        val storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                messagingFeature,
            )

        val results = storage.getMessages()

        assertEquals(2, results.size)

        val message = storage.getNextMessage(HOMESCREEN, results)!!
        assertEquals("normal-message", message.id)
    }

    @Test
    fun `GIVEN dismissed message WHEN calling getMessages THEN filter out the dismissed message`() = runTest {
        val metadataList =
            mapOf(
                "dismissed-message" to Message.Metadata(id = "dismissed-message", dismissed = true),
                "normal-message" to Message.Metadata(id = "normal-message", dismissed = false),
            )
        val messages =
            mapOf(
                "dismissed-message" to createMessageData(style = "high-priority"),
                "normal-message" to createMessageData(style = "high-priority"),
            )
        val styles = mapOf("high-priority" to createStyle(priority = 100))
        val metadataStorage: MessageMetadataStorage = mock()
        val (messagingFeature, _) =
            createMessagingFeature(
                styles = styles,
                messages = messages,
            )

        `when`(metadataStorage.getMetadata()).thenReturn(metadataList)

        val storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                messagingFeature,
            )

        val results = storage.getMessages()
        assertEquals(2, results.size)

        val message = storage.getNextMessage(HOMESCREEN, results)!!
        assertEquals("normal-message", message.id)
    }

    @Test
    fun `GIVEN a message that the maxDisplayCount WHEN calling getMessages THEN filter out the message`() = runTest {
        val metadataList =
            mapOf(
                "shown-many-times-message" to
                    Message.Metadata(
                        id = "shown-many-times-message",
                        displayCount = 10,
                    ),
                "shown-two-times-message" to
                    Message.Metadata(
                        id = "shown-two-times-message",
                        displayCount = 2,
                    ),
                "normal-message" to Message.Metadata(id = "normal-message", displayCount = 0),
            )
        val messages =
            mapOf(
                "shown-many-times-message" to createMessageData(style = "high-priority"),
                "shown-two-times-message" to createMessageData(style = "high-priority"),
                "normal-message" to createMessageData(style = "high-priority"),
            )
        val styles = mapOf("high-priority" to createStyle(priority = 100, maxDisplayCount = 2))
        val metadataStorage: MessageMetadataStorage = mock()
        val (messagingFeature, _) =
            createMessagingFeature(
                styles = styles,
                messages = messages,
            )

        `when`(metadataStorage.getMetadata()).thenReturn(metadataList)

        val storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                messagingFeature,
            )

        val results = storage.getMessages()
        assertEquals(3, results.size)

        val message = storage.getNextMessage(HOMESCREEN, results)!!
        assertEquals("normal-message", message.id)
    }

    @Test
    fun `GIVEN a malformed message WHEN calling getMessages THEN provide a list of messages ignoring the malformed one`() =
        runTest {
            val messages = storage.getMessages()
            val firstMessage = messages.first()

            assertEquals("message-1", firstMessage.id)
            assertEquals("message-1", firstMessage.metadata.id)
            assertTrue(messages.size == 2)
            assertTrue(malformedWasReported)
        }

    @Test
    fun `GIVEN a malformed action WHEN calling sanitizeAction THEN return null`() {
        val actionsMap = mapOf("action-1" to "action-1-url")

        val notFoundAction = storage.sanitizeAction("no-found-action", actionsMap)
        val emptyAction = storage.sanitizeAction("", actionsMap)
        val blankAction = storage.sanitizeAction(" ", actionsMap)

        assertNull(notFoundAction)
        assertNull(emptyAction)
        assertNull(blankAction)
    }

    @Test
    fun `GIVEN a previously stored malformed action WHEN calling sanitizeAction THEN return null and not report malFormed`() {
        val actionsMap = mapOf("action-1" to "action-1-url")

        storage.malFormedMap["malformed-action"] = "messageId"

        val action = storage.sanitizeAction("malformed-action", actionsMap)

        assertNull(action)
        assertFalse(malformedWasReported)
    }

    @Test
    fun `GIVEN a non-previously stored malformed action WHEN calling sanitizeAction THEN return null`() {
        val actionsMap = mapOf("action-1" to "action-1-url")

        val action = storage.sanitizeAction("malformed-action", actionsMap)

        assertNull(action)
    }

    @Test
    fun `WHEN calling updateMetadata THEN delegate to metadataStorage`() = runTest {
        storage.updateMetadata(mock())

        verify(metadataStorage).updateMetadata(any())
    }

    @Test
    fun `WHEN calling onMessageDisplayed with message & boot id THEN metadata for count, lastTimeShown & latestBootIdentifier is updated`() =
        runTest {
            val message = storage.getMessage("message-1")!!
            assertEquals(0, message.displayCount)

            val bootId = "test boot id"
            val expectedMessage =
                message.copy(
                    metadata =
                        Message.Metadata(
                            id = "message-1",
                            displayCount = 1,
                            lastTimeShown = MOCK_TIME_MILLIS,
                            latestBootIdentifier = bootId,
                        )
                )

            assertEquals(expectedMessage, storage.onMessageDisplayed(message, bootId))
        }

    @Test
    fun `WHEN calling onMessageDisplayed with message THEN metadata for count, lastTimeShown is updated`() = runTest {
        val message = storage.getMessage("message-1")!!
        assertEquals(0, message.displayCount)

        val bootId = null
        val expectedMessage =
            message.copy(
                metadata =
                    Message.Metadata(
                        id = "message-1",
                        displayCount = 1,
                        lastTimeShown = MOCK_TIME_MILLIS,
                        latestBootIdentifier = bootId,
                    )
            )

        assertEquals(expectedMessage, storage.onMessageDisplayed(message, bootId))
    }

    @Test
    fun `GIVEN a valid action WHEN calling sanitizeAction THEN return the action`() {
        val actionsMap = mapOf("action-1" to "action-1-url")

        val validAction = storage.sanitizeAction("action-1", actionsMap)

        assertEquals("action-1-url", validAction)
    }

    @Test
    fun `GIVEN a trigger action WHEN calling sanitizeTriggers THEN return null`() {
        val triggersMap = mapOf("trigger-1" to "trigger-1-expression")

        val notFoundTrigger = storage.sanitizeTriggers(listOf("no-found-trigger"), triggersMap)
        val emptyTrigger = storage.sanitizeTriggers(listOf(""), triggersMap)
        val blankTrigger = storage.sanitizeTriggers(listOf(" "), triggersMap)

        assertNull(notFoundTrigger)
        assertNull(emptyTrigger)
        assertNull(blankTrigger)
    }

    @Test
    fun `GIVEN a previously stored malformed trigger WHEN calling sanitizeTriggers THEN no report malformed and return null`() {
        val triggersMap = mapOf("trigger-1" to "trigger-1-expression")

        storage.malFormedMap[" "] = "messageId"

        val trigger = storage.sanitizeTriggers(listOf(" "), triggersMap)

        assertNull(trigger)
        assertFalse(malformedWasReported)
    }

    @Test
    fun `GIVEN a non previously stored malformed trigger WHEN calling sanitizeTriggers THEN return null`() {
        val triggersMap = mapOf("trigger-1" to "trigger-1-expression")

        val trigger = storage.sanitizeTriggers(listOf(" "), triggersMap)

        assertNull(trigger)
    }

    @Test
    fun `GIVEN a valid trigger WHEN calling sanitizeAction THEN return the trigger`() {
        val triggersMap = mapOf("trigger-1" to "trigger-1-expression")

        val validTrigger = storage.sanitizeTriggers(listOf("trigger-1"), triggersMap)

        assertEquals(listOf("trigger-1-expression"), validTrigger)
    }

    @Test
    fun `GIVEN an eligible message WHEN calling isMessageEligible THEN return true`() {
        val helper: NimbusMessagingHelperInterface = mock()
        val message =
            Message(
                "same-id",
                mock(),
                action = "action",
                mock(),
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        `when`(helper.evalJexl(any())).thenReturn(true)

        val result = storage.isMessageEligible(message, helper)

        assertTrue(result)
    }

    @Test
    fun `GIVEN a malformed message key WHEN calling reportMalformedMessage THEN record a malformed feature event`() {
        val key = "malformed-message"
        storage.reportMalformedMessage(key)

        assertTrue(malformedWasReported)
        verify(featuresInterface).recordMalformedConfiguration("messaging", key)
    }

    @Test
    fun `GIVEN a malformed trigger WHEN calling isMessageEligible THEN return false`() {
        val helper: NimbusMessagingHelperInterface = mock()
        val message =
            Message(
                "same-id",
                mock(),
                action = "action",
                mock(),
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        `when`(helper.evalJexl(any())).then { throw NimbusException.EvaluationException("") }

        assertThrows(NimbusException.EvaluationException::class.java) {
            storage.isMessageEligible(message, helper)
        }
    }

    @Test
    fun `GIVEN a previously malformed trigger WHEN calling isMessageEligible THEN throw and not evaluate`() {
        val helper: NimbusMessagingHelperInterface = mock()
        val message =
            Message(
                "same-id",
                mock(),
                action = "action",
                mock(),
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        storage.malFormedMap["trigger"] = "same-id"

        `when`(helper.evalJexl(any())).then { throw NimbusException.EvaluationException("") }

        assertThrows(NimbusException.EvaluationException::class.java) {
            storage.isMessageEligible(message, helper)
        }

        verify(helper, never()).evalJexl("trigger")
    }

    @Test
    fun `GIVEN a non previously malformed trigger WHEN calling isMessageEligible THEN throw and not evaluate`() {
        val helper: NimbusMessagingHelperInterface = mock()
        val message =
            Message(
                "same-id",
                mock(),
                action = "action",
                mock(),
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        `when`(helper.evalJexl(any())).then { throw NimbusException.EvaluationException("") }

        assertFalse(storage.malFormedMap.containsKey("trigger"))

        assertThrows(NimbusException.EvaluationException::class.java) {
            storage.isMessageEligible(message, helper)
        }

        assertTrue(storage.malFormedMap.containsKey("trigger"))
    }

    @Test
    fun `GIVEN none available messages are eligible WHEN calling getNextMessage THEN return null`() {
        val spiedStorage = spy(storage)
        val message =
            Message(
                "same-id",
                mock(),
                action = "action",
                mock(),
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        doReturn(false).`when`(spiedStorage).isMessageEligible(any(), any())

        val result = spiedStorage.getNextMessage(HOMESCREEN, listOf(message))

        assertNull(result)
    }

    @Test
    fun `GIVEN an eligible message WHEN calling getNextMessage THEN return the message`() {
        val spiedStorage = spy(storage)
        val message =
            Message(
                "same-id",
                createMessageData(surface = HOMESCREEN),
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        doReturn(true).`when`(spiedStorage).isMessageEligible(any(), any())

        val result = spiedStorage.getNextMessage(HOMESCREEN, listOf(message))

        assertEquals(message.id, result!!.id)
    }

    @Test
    fun `GIVEN a message under experiment WHEN calling onMessageDisplayed THEN call recordExposureEvent`() = runTest {
        val spiedStorage = spy(storage)
        val experiment = "my-experiment"
        val messageData: MessageData = createMessageData(isControl = false, experiment = experiment)

        val message =
            Message(
                "same-id",
                messageData,
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        doReturn(true).`when`(spiedStorage).isMessageEligible(any(), any())

        val result = spiedStorage.getNextMessage(HOMESCREEN, listOf(message))
        verify(featuresInterface, never()).recordExposureEvent("messaging", experiment)

        spiedStorage.onMessageDisplayed(message)
        verify(featuresInterface).recordExposureEvent("messaging", experiment)
        assertEquals(message.id, result!!.id)
    }

    @Test
    fun `GIVEN a control message WHEN calling getNextMessage THEN return the next eligible message`() {
        val spiedStorage = spy(storage)
        val experiment = "my-experiment"
        val messageData: MessageData = createMessageData()
        val controlMessageData: MessageData = createMessageData(isControl = true, experiment = experiment)

        doReturn(SHOW_NEXT_MESSAGE).`when`(spiedStorage).getOnControlBehavior()

        val message =
            Message(
                "id",
                messageData,
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        val controlMessage =
            Message(
                "control-id",
                controlMessageData,
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        doReturn(true).`when`(spiedStorage).isMessageEligible(any(), any())

        val result =
            spiedStorage.getNextMessage(
                HOMESCREEN,
                listOf(controlMessage, message),
            )

        verify(messagingFeature).recordExperimentExposure(experiment)
        assertEquals(message.id, result!!.id)
    }

    @Test
    fun `GIVEN a malformed control message WHEN calling getNextMessage THEN return the next eligible message`() {
        val spiedStorage = spy(storage)
        val messageData: MessageData = createMessageData()
        // the message isControl, but has no experiment property.
        val controlMessageData: MessageData = createMessageData(isControl = true)

        doReturn(SHOW_NEXT_MESSAGE).`when`(spiedStorage).getOnControlBehavior()

        val message =
            Message(
                "id",
                messageData,
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        val controlMessage =
            Message(
                "control-id",
                controlMessageData,
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        doReturn(true).`when`(spiedStorage).isMessageEligible(any(), any())

        val result =
            spiedStorage.getNextMessage(
                HOMESCREEN,
                listOf(controlMessage, message),
            )

        verify(messagingFeature).recordMalformedConfiguration("control-id")

        assertEquals(message.id, result!!.id)
    }

    @Test
    fun `GIVEN a control message WHEN calling getNextMessage THEN return the next eligible message with the correct surface`() {
        val spiedStorage = spy(storage)
        val experiment = "my-experiment"
        val messageData: MessageData = createMessageData()
        val incorrectMessageData: MessageData = createMessageData(surface = NOTIFICATION)
        val controlMessageData: MessageData = createMessageData(isControl = true, experiment = experiment)

        doReturn(SHOW_NEXT_MESSAGE).`when`(spiedStorage).getOnControlBehavior()

        val message =
            Message(
                "id",
                messageData,
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        val incorrectMessage =
            Message(
                "incorrect-id",
                incorrectMessageData,
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        val controlMessage =
            Message(
                "control-id",
                controlMessageData,
                action = "action",
                style = displayOnceStyle,
                listOf("trigger"),
                metadata = Message.Metadata("same-id"),
            )

        doReturn(true).`when`(spiedStorage).isMessageEligible(any(), any())

        var result =
            spiedStorage.getNextMessage(
                HOMESCREEN,
                listOf(controlMessage, incorrectMessage, message),
            )

        verify(messagingFeature, times(1)).recordExperimentExposure(experiment)
        assertEquals(message.id, result!!.id)

        result =
            spiedStorage.getNextMessage(
                HOMESCREEN,
                listOf(controlMessage, incorrectMessage),
            )

        verify(messagingFeature, times(2)).recordExperimentExposure(experiment)
        assertNull(result)
    }

    @Test
    fun `WHEN a storage instance is created THEN do not invoke the feature`() = runTest {
        storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                messagingFeature,
            )

        // We should not be using the feature holder until getMessages is called.
        verify(messagingFeature, never()).value()
    }

    @Test
    fun `WHEN calling getMessage THEN return message with matching key OR null if doesn't exist`() = runTest {
        val messages =
            mapOf(
                "low-message" to createMessageData(style = "low-priority"),
                "high-message" to createMessageData(style = "high-priority"),
                "medium-message" to createMessageData(style = "medium-priority"),
            )
        val styles =
            mapOf(
                "high-priority" to createStyle(priority = 100),
                "medium-priority" to createStyle(priority = 50),
                "low-priority" to createStyle(priority = 1),
            )

        val (messagingFeature, _) =
            createMessagingFeature(
                styles = styles,
                messages = messages,
            )

        `when`(metadataStorage.getMetadata()).thenReturn(mapOf("message-1" to Message.Metadata(id = "message-1")))

        val storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                messagingFeature,
            )

        assertEquals("high-message", storage.getMessage("high-message")?.id)
        assertEquals("medium-message", storage.getMessage("medium-message")?.id)
        assertEquals("low-message", storage.getMessage("low-message")?.id)
        assertEquals(null, storage.getMessage("no-message")?.id)
    }

    @Test
    fun `GIVEN a message without text THEN reject the message and report it as malformed`() = runTest {
        val (feature, _) =
            createMessagingFeature(
                styles = mapOf("style-1" to createStyle(priority = 100)),
                triggers = mapOf("trigger-1" to "://trigger-1"),
                messages =
                    mapOf(
                        "missing-text" to createMessageData(text = ""),
                        "control" to createMessageData(text = "", isControl = true),
                        "ok" to createMessageData(),
                    ),
            )
        val storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                feature,
            )

        assertNotNull(storage.getMessage("ok"))
        assertNotNull(storage.getMessage("control"))
        assertNull(storage.getMessage("missing-text"))
        assertTrue(malformedMessageIds.contains("missing-text"))
        assertFalse(malformedMessageIds.contains("ok"))
        assertFalse(malformedMessageIds.contains("control"))
    }

    @Test
    fun `GIVEN a message with an action and params THEN do string interpolation`() = runTest {
        val (feature, _) =
            createMessagingFeature(
                actions = mapOf("OPEN_URL" to "://open", "INSTALL_FOCUS" to "market://details?app=org.mozilla.focus"),
                messages =
                    mapOf(
                        "open-url" to
                            createMessageData(
                                action = "OPEN_URL",
                                actionParams = mapOf("url" to "https://mozilla.org"),
                            ),
                        // with uuid in the param value
                        "open-url-with-uuid" to
                            createMessageData(
                                action = "OPEN_URL",
                                actionParams = mapOf("url" to "https://mozilla.org?uuid={uuid}"),
                            ),
                        // with ? in the action
                        "install-focus" to
                            createMessageData(
                                action = "INSTALL_FOCUS",
                                actionParams = mapOf("utm" to "my-utm"),
                            ),
                    ),
            )
        val storage =
            NimbusMessagingStorage(
                testContext,
                metadataStorage,
                reportMalformedMessage,
                nimbus,
                messagingFeature = feature,
            )

        val myUuid = UUID.randomUUID().toString()
        val helper =
            object : NimbusMessagingHelperInterface {
                override fun evalJexl(expression: String) = false

                override fun evalJexlDebug(expression: String): String {
                    return """{"success": false, "error": "Not implemented in test"}"""
                }

                override fun getUuid(template: String): String? =
                    if (template.contains("{uuid}")) {
                        myUuid
                    } else {
                        null
                    }

                override fun stringFormat(template: String, uuid: String?): String =
                    uuid?.let {
                        template.replace("{uuid}", it)
                    } ?: template
            }

        run {
            val message = storage.getMessage("open-url")!!
            assertEquals(message.action, "://open")
            val (uuid, url) = storage.generateUuidAndFormatMessage(message, helper)
            assertEquals(uuid, null)
            val urlParam = "https://mozilla.org"
            assertEquals(url, "://open?url=${Uri.encode(urlParam)}")
        }

        run {
            val message = storage.getMessage("open-url-with-uuid")!!
            assertEquals(message.action, "://open")
            val (uuid, url) = storage.generateUuidAndFormatMessage(message, helper)
            assertEquals(uuid, myUuid)
            val urlParam = "https://mozilla.org?uuid=$myUuid"
            assertEquals(url, "://open?url=${Uri.encode(urlParam)}")
        }

        run {
            val message = storage.getMessage("install-focus")!!
            assertEquals(message.action, "market://details?app=org.mozilla.focus")
            val (uuid, url) = storage.generateUuidAndFormatMessage(message, helper)
            assertEquals(uuid, null)
            assertEquals(url, "market://details?app=org.mozilla.focus&utm=my-utm")
        }
    }

    private fun createMessageData(
        text: String = "text-1",
        action: String = "action-1",
        actionParams: Map<String, String> = mapOf(),
        style: String = "style-1",
        triggers: List<String> = listOf("trigger-1"),
        surface: MessageSurfaceId = HOMESCREEN,
        isControl: Boolean = false,
        experiment: String? = null,
    ) =
        MessageData(
            action = action,
            actionParams = actionParams,
            style = style,
            triggerIfAll = triggers,
            surface = surface,
            isControl = isControl,
            text = Res.string(text),
            experiment = experiment,
        )

    private fun createMessagingFeature(
        triggers: Map<String, String> = mapOf("trigger-1" to "trigger-1-expression"),
        styles: Map<String, StyleData> = mapOf("style-1" to createStyle()),
        actions: Map<String, String> = mapOf("action-1" to "action-1-url"),
        messages: Map<String, MessageData> =
            mapOf(
                "message-1" to createMessageData(surface = HOMESCREEN),
                "message-2" to createMessageData(surface = NOTIFICATION),
                "malformed" to createMessageData(action = "malformed-action"),
                "blanktext" to createMessageData(text = ""),
            ),
    ): Pair<FeatureHolder<Messaging>, FeaturesInterface> {
        val messaging =
            Messaging(
                actions = actions,
                triggers = triggers,
                messages = messages,
                styles = styles,
            )
        val featureInterface: FeaturesInterface = mock()
        // "messaging" is a hard coded value generated from Nimbus.
        val messagingFeature =
            FeatureHolder({ featureInterface }, "messaging") { _, _ ->
                messaging
            }
        messagingFeature.withCachedValue(messaging)

        return messagingFeature to featureInterface
    }

    private fun createStyle(priority: Int = 1, maxDisplayCount: Int = 5): StyleData {
        val style1: StyleData = mock()
        `when`(style1.priority).thenReturn(priority)
        `when`(style1.maxDisplayCount).thenReturn(maxDisplayCount)
        return style1
    }

    companion object {
        private const val HOMESCREEN = "homescreen"
        private const val NOTIFICATION = "notification"
    }
}

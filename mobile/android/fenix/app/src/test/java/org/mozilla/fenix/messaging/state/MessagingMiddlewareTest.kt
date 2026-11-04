/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.messaging.state

import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.test.runTest
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageData
import mozilla.components.service.nimbus.messaging.NimbusMessagingController
import mozilla.components.service.nimbus.messaging.StyleData
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.Evaluate
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MessageClicked
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MessageDismissed
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MicrosurveyAction
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.MicrosurveyAction.Dismissed
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction.Restore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.messaging.FenixMessageSurfaceId
import org.mozilla.fenix.messaging.MessagingState

class MessagingMiddlewareTest {
    private lateinit var controller: NimbusMessagingController

    @Before
    fun setUp() {
        controller = mockk(relaxed = true)
    }

    @Test
    fun `WHEN restored THEN get messages from the storage`() = runTest {
        val store =
            AppStore(
                AppState(messaging = MessagingState(messages = emptyList())),
                listOf(MessagingMiddleware(controller, this)),
            )
        val message = createMessage()

        coEvery { controller.getMessages() } returns listOf(message)

        store.dispatch(Restore)
        testScheduler.advanceUntilIdle()

        assertEquals(listOf(message), store.state.messaging.messages)
    }

    @Test
    fun `WHEN Evaluate THEN getNextMessage from the storage and UpdateMessageToShow`() = runTest {
        val message = createMessage()
        val store =
            AppStore(
                AppState(messaging = MessagingState(messages = listOf(message))),
                listOf(MessagingMiddleware(controller, this)),
            )

        every {
            controller.getNextMessage(
                FenixMessageSurfaceId.HOMESCREEN,
                any(),
            )
        } returns message

        assertEquals(0, store.state.messaging.messageToShow.size)

        store.dispatch(Evaluate(FenixMessageSurfaceId.HOMESCREEN))

        // UpdateMessageToShow to causes messageToShow to append
        assertEquals(1, store.state.messaging.messageToShow.size)
    }

    @Test
    fun `WHEN Evaluate with trigger context THEN getNextMessage is called with the trigger context`() = runTest {
        val message = createMessage()
        val store =
            AppStore(
                AppState(messaging = MessagingState(messages = listOf(message))),
                listOf(MessagingMiddleware(controller, this)),
            )
        val triggerContext = mapOf("test" to mapOf("internal" to "value"))

        store.dispatch(Evaluate(FenixMessageSurfaceId.HOMESCREEN, triggerContext))

        verify { controller.getNextMessage(FenixMessageSurfaceId.HOMESCREEN, listOf(message), triggerContext) }
    }

    @Test
    fun `WHEN MessageClicked THEN update storage`() = runTest {
        val message = createMessage()
        val store =
            AppStore(
                AppState(messaging = MessagingState(messages = listOf(message))),
                listOf(MessagingMiddleware(controller, this)),
            )

        assertEquals(message, store.state.messaging.messages.first())

        store.dispatch(MessageClicked(message))
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.messaging.messages.isEmpty())
        coVerify { controller.onMessageClicked(message = message) }
    }

    @Test
    fun `GVIEN a microsurvey WHEN Started THEN only notify the controller`() = runTest {
        val message = createMessage(data = mockk<MessageData>(relaxed = true))
        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages = listOf(message),
                            messageToShow = mapOf(message.id to message),
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        assertEquals(message, store.state.messaging.messages.first())

        store.dispatch(MicrosurveyAction.Started(message.id))
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.messaging.messages.isEmpty())
        coVerify { controller.onMicrosurveyStarted(id = message.id) }
    }

    @Test
    fun `WHEN MessageDismissed THEN update storage`() = runTest {
        val metadata = createMetadata(displayCount = 1, "same-id")
        val message = createMessage(metadata)
        val store =
            AppStore(
                AppState(messaging = MessagingState(messages = listOf(message))),
                listOf(MessagingMiddleware(controller, this)),
            )
        store.dispatch(MessageDismissed(message))
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.messaging.messages.isEmpty())
        coVerify { controller.onMessageDismissed(message = message) }
    }

    @Test
    fun `WHEN onMicrosurveyDismissed THEN update storage`() = runTest {
        val metadata = createMetadata(displayCount = 1, "same-id")
        val message = createMessage(metadata)
        val store =
            AppStore(
                AppState(messaging = MessagingState(messages = listOf(message))),
                listOf(MessagingMiddleware(controller, this)),
            )
        store.dispatch(Dismissed(message.id))
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.messaging.messages.isEmpty())
        coVerify { controller.onMicrosurveyDismissed(message = message) }
    }

    @Test
    fun `GIVEN a microsurvey WHEN onMicrosurveyShown THEN only notify the controller`() = runTest {
        val message = createMessage()
        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages = listOf(message),
                            messageToShow = mapOf(message.id to message),
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        assertEquals(message, store.state.messaging.messages.first())

        store.dispatch(AppAction.MessagingAction.MicrosurveyAction.Shown(message.id))
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.messaging.messages.isEmpty())
        coVerify { controller.onMicrosurveyShown(id = message.id) }
    }

    @Test
    fun `GVIEN a microsurvey WHEN onMicrosurveyConfirmationShown THEN only notify the controller`() = runTest {
        val message = createMessage()
        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages = listOf(message),
                            messageToShow = mapOf(message.id to message),
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        assertEquals(message, store.state.messaging.messages.first())

        store.dispatch(AppAction.MessagingAction.MicrosurveyAction.SentConfirmationShown(message.id))
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.messaging.messages.isEmpty())
        coVerify { controller.onMicrosurveySentConfirmationShown(id = message.id) }
    }

    @Test
    fun `GVIEN a microsurvey WHEN onPrivacyNoticeTapped THEN only notify the controller`() = runTest {
        val message = createMessage()
        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages = listOf(message),
                            messageToShow = mapOf(message.id to message),
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        assertEquals(message, store.state.messaging.messages.first())

        store.dispatch(AppAction.MessagingAction.MicrosurveyAction.OnPrivacyNoticeTapped(message.id))
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.messaging.messages.isEmpty())
        coVerify { controller.onMicrosurveyPrivacyNoticeTapped(id = message.id) }
    }

    @Test
    fun `WHEN onMessageDismissed THEN remove the message from storage and state`() = runTest {
        val message = createMessage(createMetadata(displayCount = 1))
        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages = listOf(message),
                            messageToShow = mapOf(message.surface to message),
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        store.dispatch(MessageDismissed(message))

        // removeMessages causes messages size to be 0
        assertEquals(0, store.state.messaging.messages.size)
        // consumeMessageToShowIfNeeded causes messageToShow map size to be 0
        assertEquals(0, store.state.messaging.messageToShow.size)
    }

    @Test
    fun `WHEN onMicrosurveyDismissed THEN remove the message from storage and state`() = runTest {
        val message = createMessage(createMetadata(displayCount = 1))
        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages = listOf(message),
                            messageToShow = mapOf(message.surface to message),
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        store.dispatch(Dismissed(message.id))

        // removeMessages causes messages size to be 0
        assertEquals(0, store.state.messaging.messages.size)
        // consumeMessageToShowIfNeeded causes messageToShow map size to be 0
        assertEquals(0, store.state.messaging.messageToShow.size)
    }

    @Test
    fun `WHEN consumeMessageToShowIfNeeded THEN consume the message`() = runTest {
        val message = createMessage()
        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages = listOf(message),
                            messageToShow = mapOf(message.surface to message),
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        store.dispatch(MessageClicked(message))

        assertTrue(store.state.messaging.messages.isEmpty())
        assertTrue(store.state.messaging.messageToShow.isEmpty())
    }

    @Test
    fun `WHEN updateMessage THEN update available messages`() = runTest {
        val message = createMessage()
        val messageDisplayed = message.copy(metadata = createMetadata(displayCount = 1))
        val store =
            AppStore(
                AppState(messaging = MessagingState(messages = listOf(message))),
                listOf(MessagingMiddleware(controller, this)),
            )

        every {
            controller.getNextMessage(
                FenixMessageSurfaceId.HOMESCREEN,
                any(),
            )
        } returns message

        coEvery {
            controller.onMessageDisplayed(eq(message), any())
        } returns messageDisplayed

        store.dispatch(Evaluate(FenixMessageSurfaceId.HOMESCREEN))
        testScheduler.advanceUntilIdle()

        assertEquals(1, store.state.messaging.messages.count())
        assertEquals(1, store.state.messaging.messages.first().displayCount)
    }

    @Test
    fun `WHEN evaluate THEN update displayCount without altering message order`() = runTest {
        val message1 = createMessage()
        val message2 = message1.copy(id = "message2", action = "action2")
        // An updated message1 that has been displayed once.
        val messageDisplayed1 = incrementDisplayCount(message1)
        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages =
                                listOf(
                                    message1,
                                    message2,
                                )
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        every {
            controller.getNextMessage(
                FenixMessageSurfaceId.HOMESCREEN,
                any(),
            )
        } returns message1
        coEvery {
            controller.onMessageDisplayed(message1, any())
        } returns messageDisplayed1

        coEvery {
            controller.onMessageDisplayed(eq(message1), any())
        } returns messageDisplayed1

        store.dispatch(Evaluate(FenixMessageSurfaceId.HOMESCREEN))
        testScheduler.advanceUntilIdle()

        assertEquals(messageDisplayed1, store.state.messaging.messages[0])
        assertEquals(message2, store.state.messaging.messages[1])
    }

    @Test
    fun `GIVEN a message has not surpassed the maxDisplayCount WHEN evaluate THEN update the message displayCount`() =
        runTest {
            val message = createMessage()
            // An updated message that has been displayed once.
            val messageDisplayed = incrementDisplayCount(message)
            val store =
                AppStore(
                    AppState(messaging = MessagingState(messages = listOf(message))),
                    listOf(MessagingMiddleware(controller, this)),
                )

            every {
                controller.getNextMessage(
                    FenixMessageSurfaceId.HOMESCREEN,
                    any(),
                )
            } returns message
            coEvery {
                controller.onMessageDisplayed(message, any())
            } returns messageDisplayed

            coEvery {
                controller.onMessageDisplayed(eq(message), any())
            } returns messageDisplayed

            store.dispatch(Evaluate(FenixMessageSurfaceId.HOMESCREEN))
            testScheduler.advanceUntilIdle()

            assertEquals(messageDisplayed.displayCount, store.state.messaging.messages[0].displayCount)
            assertEquals(1, store.state.messaging.messages.size)
            assertEquals(1, store.state.messaging.messageToShow.size)
        }

    @Test
    fun `GIVEN a message has reached the maxDisplayCount WHEN onMessagedDisplayed THEN remove the message`() = runTest {
        val message = createMessage(createMetadata(displayCount = 4))
        val messageDisplayed = createMessage(createMetadata(displayCount = 5))
        assertFalse(message.isExpired)
        assertTrue(messageDisplayed.isExpired)

        val store =
            AppStore(
                AppState(
                    messaging =
                        MessagingState(
                            messages = listOf(message),
                            messageToShow = mapOf(message.surface to message),
                        )
                ),
                listOf(MessagingMiddleware(controller, this)),
            )

        every {
            controller.getNextMessage(
                FenixMessageSurfaceId.HOMESCREEN,
                any(),
            )
        } returns message
        coEvery {
            controller.onMessageDisplayed(message, any())
        } returns incrementDisplayCount(message)

        coEvery {
            controller.onMessageDisplayed(eq(message), any())
        } returns messageDisplayed

        store.dispatch(Evaluate(FenixMessageSurfaceId.HOMESCREEN))
        testScheduler.advanceUntilIdle()

        assertEquals(0, store.state.messaging.messages.size)
        assertEquals(1, store.state.messaging.messageToShow.size)
    }

    @Test
    fun `GIVEN a microsurvey message that surpassed the maxDisplayCount WHEN onMessagedDisplayed THEN remove the message`() =
        runTest {
            val message =
                createMessage(metadata = createMetadata(displayCount = 4), data = MessageData(surface = "microsurvey"))
            val messageDisplayed =
                createMessage(createMetadata(displayCount = 5), data = MessageData(surface = "microsurvey"))

            assertFalse(message.isExpired)
            assertTrue(messageDisplayed.isExpired)

            val store =
                AppStore(
                    AppState(
                        messaging =
                            MessagingState(
                                messages = listOf(message),
                                messageToShow = mapOf(message.surface to message),
                            )
                    ),
                    listOf(MessagingMiddleware(controller, this)),
                )

            every { controller.getNextMessage(FenixMessageSurfaceId.MICROSURVEY, any()) } returns message
            coEvery { controller.onMessageDisplayed(message, any()) } returns incrementDisplayCount(message)
            coEvery { controller.onMessageDisplayed(eq(message), any()) } returns messageDisplayed

            store.dispatch(Evaluate(FenixMessageSurfaceId.MICROSURVEY))
            testScheduler.advanceUntilIdle()

            assertEquals(0, store.state.messaging.messages.size)
            assertEquals(1, store.state.messaging.messageToShow.size)
        }

    @Test
    fun `GIVEN message is not found WHEN updateMessage THEN do not update the message list`() = runTest {
        val message = createMessage(messageId = "1")
        val message2 = createMessage(messageId = "2")
        val store =
            AppStore(
                AppState(messaging = MessagingState(messages = listOf(message))),
                listOf(MessagingMiddleware(controller, this)),
            )

        every {
            controller.getNextMessage(
                FenixMessageSurfaceId.HOMESCREEN,
                any(),
            )
        } returns message

        coEvery {
            controller.onMessageDisplayed(eq(message), any())
        } returns message2

        store.dispatch(Evaluate(FenixMessageSurfaceId.HOMESCREEN))

        assertEquals(1, store.state.messaging.messages.count())
        assertEquals(message, store.state.messaging.messages.first())
    }
}

private fun createMessage(
    metadata: Message.Metadata = createMetadata(),
    messageId: String = "message-id",
    data: MessageData = mockk(relaxed = true),
    action: String = "action",
    styleData: StyleData = StyleData(),
    triggers: List<String> = listOf("triggers"),
    except: List<String> = listOf(),
) = Message(messageId, data, action, styleData, triggers, except, metadata)

private fun createMetadata(
    displayCount: Int = 0,
    id: String = "same-id",
    pressed: Boolean = false,
    dismissed: Boolean = false,
    lastTimeShown: Long = 0L,
    latestBootIdentifier: String? = null,
) =
    Message.Metadata(
        id,
        displayCount,
        pressed,
        dismissed,
        lastTimeShown,
        latestBootIdentifier,
    )

private fun incrementDisplayCount(message: Message) =
    message.copy(metadata = createMetadata(displayCount = message.displayCount + 1))

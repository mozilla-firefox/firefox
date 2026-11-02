# Push

<div class="note">
<div class="admonition-title">Note</div>
This document describes how Firefox implements the Web Push standard internally, and is intended for developers working directly on Push. If you are looking for how to consume push, please refer to the <a href="https://developer.mozilla.org/en-US/docs/Web/API/Push_API" target="_blank">following MDN document</a>

</div>

## High level push architecture

The following sequence diagram describes the high level push architecture as observed by web application. The diagram describes the interactions between a Web application's client code running in the browser, Firefox, [Autopush](https://autopush.readthedocs.io/en/latest/) (Firefox's push server that delivers push notifications) and a third party server that sends the push notifications to Autopush

The dotted lines are done by the consumer of push.

```{mermaid}
sequenceDiagram
	participant TP as Web Application JS
	participant F as Firefox
	participant A as Autopush
	participant TPS as Third party Server
		TP->>F: subscribe(scope)
		activate TP
		activate F
		F->>A: subscribe(scope) using web socket
		activate A
		A->>F: URL
		deactivate A
		F->>F: Create pub/privKey Encryption pair
		F->>F: Persist URL, pubKey, privKey indexed using an id derived from scope
		F->>TP: URL + pubKey
		deactivate F
		TP-->>TPS: URL + pubKey
		deactivate TP
		TPS-->>TPS: Encrypt payload using pubKey
		TPS-->>A: Send encrypted payload using URL
		activate A
		A->>F: Send encrypted payload using web socket
		deactivate A
		activate F
		F->>F: Decrypt payload using privKey
		F->>F: Display Notification
		deactivate F
```

## Firefox Desktop

### Flow diagram for source code

The source code for push is available under [`dom/push`](https://searchfox.org/firefox-main/source/dom/push) in mozilla-central.

The following flow diagram describes how different modules interact with each other to provide the push API to consumers.

```{mermaid}
flowchart TD
    subgraph Public API

    W[Third party Web app]-->|imports| P[PushManager.webidl]
    end
    subgraph Browser Code
        P-->|Implemented by| MM
        MM{Main Thread?}-->|Yes| B[Push.sys.mjs]
        MM -->|NO| A[PushManager.cpp]
        B-->|subscribe,getSubscription| D[PushComponents.sys.mjs]
        A-->|subscribe,getSubscription| D
        D-->|subscribe,getSubscription| M[PushService.sys.mjs]
        M-->|Storage| S[PushDB.sys.mjs]
        M-->|Network| N[PushWebSocket.sys.mjs]
        F[FxAccountsPush.sys.mjs] -->|uses| D
    end
    subgraph Server
    N-. Send, Receive.-> O[Autopush]
    end
    subgraph Local Storage
    S-->|Read,Write| PP[(IndexedDB)]
    end
```

### The Push Web Socket

Push in Firefox Desktop communicates with Autopush using a web socket connection.

The web socket connection is created as the browser initializes and is managed by the following state diagram.

```{mermaid}
stateDiagram-v2
		state "Shut Down" as SD
		state "Waiting for WebSocket to start" as W1
		state "Waiting for server hello" as W2
		state "Ready" as R
    [*] --> SD
		 SD --> W1: beginWSSetup
		 W1 --> W2: wsOnStart Success
		 W2 --> R: handleHelloReply
		 R --> R: send (subscribe)<br/>receive + notify observers
		 R --> SD: wsOnStop
		 R --> SD: sendPing Fails
		 W1 --> SD: wsOnStart fails
		 W2 --> SD: invalid server hello
		 R --> [*]
```

Once the Push web socket is on the `Ready` state, it is ready to send new subscriptions to Autopush, and receive push notifications from those subscriptions.

Push uses an observer pattern to notify observers of any incoming push notifications. See the [high level architecture](#high-level-push-architecture) section.

### Push Storage

Push uses IndexedDB to store subscriptions for the following reasons:

1. In case the consumer attempts to re-subscribe, storage is used as a cache to serve the URL and the public key
1. In order to persist the private key, so that it can be used to decrypt any incoming push notifications

The following is what gets persisted:

```{mermaid}
erDiagram
    Subscription {
        string channelID "Key, Derived from scope"
        string pushEndpoint "Unique endpoint for this subscription"
        string scope "Usually the origin, unique value for internal consumers"
        Object p256dhPublicKey "Object representing the public key"
        Object p256dhPrivateKey "Object representing the private key"
    }
```

## Firefox for Android

See also:

* [Rust Push Component](https://mozilla.github.io/application-services/book/rust-docs/push/index.html)
* [Push Service Bridge HTTP Interface](https://autopush.readthedocs.io/en/latest/http.html#push-service-bridge-http-interface)
* [Android Components: Push](https://github.com/mozilla/gecko-dev/blob/master/mobile/android/android-components/components/feature/push/README.md)

### Flow diagram for source code

The source code for push is available under the following paths in mozilla-central:

* [`dom/push/`](https://searchfox.org/firefox-main/source/dom/push/)
* [`mobile/shared/components/geckoview/GeckoViewPush.sys.mjs`](https://searchfox.org/firefox-main/source/mobile/shared/components/geckoview/GeckoViewPush.sys.mjs)
* [`mobile/android/geckoview/src/main/java/org/mozilla/geckoview/WebPush*.java`](https://searchfox.org/firefox-main/source/mobile/android/geckoview/src/main/java/org/mozilla/geckoview/)
* Under `mobile/android/android-components/components`:
  * [`feature/push/src/main/java/mozilla/components/feature/push/`](https://searchfox.org/firefox-main/source/mobile/android/android-components/components/feature/push/src/main/java/mozilla/components/feature/push/)
  * [`browser/engine-gecko/src/main/java/mozilla/components/browser/engine/gecko/webpush/`](https://searchfox.org/firefox-main/source/mobile/android/android-components/components/browser/engine-gecko/src/main/java/mozilla/components/browser/engine/gecko/webpush/)
  * [`concept/engine/src/main/java/mozilla/components/concept/engine/webpush`](https://searchfox.org/firefox-main/source/mobile/android/android-components/components/concept/engine/src/main/java/mozilla/components/concept/engine/webpush/)
* [Rust Push Component](https://github.com/mozilla/application-services/tree/main/components/push)

The following flow diagram describes how different modules interact with each other to provide the push API to consumers.

```{mermaid}
flowchart TD
    subgraph Public API

    W[Third party Web app]-->|imports| P[PushManager.webidl]
    end
    subgraph Browser Code
        P-->|Implemented by| MM
        MM{Main Thread?}-->|Yes| B[Push.sys.mjs]
        MM -->|NO| A[PushManager.cpp]
        B-->|subscribe,getSubscription| D[GeckoViewPush.sys.mjs]
        A-->|subscribe,getSubscription| D
        D-->|PushSubscribe,PushGetSubscription| WPC[WebPushController.java]
        WPC-->|onSubscribe,onGetSubscription| WPEI[WebPushEngineIntegration.kt]
        WPEI-->|subscribe,getSubscription| APF[AutoPushFeature.kt]
        APF-->|start| PS[AbstractFirebasePushService.kt]
        FCM[Firebase Cloud Messaging]-->|onNewToken| PS
        PS-->|onNewToken| APF
        APF-->|subscribe,getSubscription| PRS[push/src/lib.rs]
        PRS-->|subscribe,get_subscription| PM[internal/push_manager.rs]
        PM-->|Storage| S[internal/storage/db.rs]
        PM-->|Network| N[internal/communications.rs]
    end
    subgraph Server
    N-. bridge interface.-> O[Autopush]
    end
    subgraph Local Storage
    S-->|Read,Write| SQL[(SQLite)]
    end
```

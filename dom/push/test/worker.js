// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/licenses/publicdomain/

// This worker is used for two types of tests. `handlePush` sends messages to
// `frame.html`, which verifies that the worker can receive push messages.

// `handleMessage` receives messages from `test_push_manager_worker.html`
// and `test_data.html`, and verifies that `PushManager` can be used from
// the worker.

/* globals PushEvent */

this.onpush = handlePush;
this.onmessage = handleMessage;
this.onpushsubscriptionchange = handlePushSubscriptionChange;

function getJSON(data) {
  var result = {
    ok: false,
  };
  try {
    result.value = data.json();
    result.ok = true;
  } catch (e) {
    // Ignore syntax errors for invalid JSON.
  }
  return result;
}

function assert(value, message) {
  if (!value) {
    throw new Error(message);
  }
}

function broadcast(event, promise) {
  event.waitUntil(
    Promise.resolve(promise).then(message => {
      return self.clients.matchAll().then(clients => {
        clients.forEach(client => client.postMessage(message));
      });
    })
  );
}

function reply(event, promise) {
  event.waitUntil(
    Promise.resolve(promise)
      .then(result => {
        event.ports[0].postMessage(result);
      })
      .catch(error => {
        event.ports[0].postMessage({
          error: String(error),
        });
      })
  );
}

function handlePush(event) {
  if (event instanceof PushEvent) {
    if (!("data" in event)) {
      broadcast(event, { type: "finished", okay: "yes" });
      return;
    }
    var message = {
      type: "finished",
      okay: "yes",
    };
    if (event.data) {
      message.data = {
        text: event.data.text(),
        arrayBuffer: event.data.arrayBuffer(),
        json: getJSON(event.data),
        blob: event.data.blob(),
        bytes: event.data.bytes(),
      };
    }
    broadcast(event, message);
    return;
  }
  broadcast(event, { type: "finished", okay: "no" });
}

var testHandlers = {
  publicKey() {
    return self.registration.pushManager
      .getSubscription()
      .then(subscription => ({
        p256dh: subscription.getKey("p256dh"),
        auth: subscription.getKey("auth"),
      }));
  },

  resubscribe(data) {
    return self.registration.pushManager
      .getSubscription()
      .then(subscription => {
        assert(
          subscription.endpoint == data.endpoint,
          "Wrong push endpoint in worker"
        );
        return subscription.unsubscribe();
      })
      .then(result => {
        assert(result, "Error unsubscribing in worker");
        return self.registration.pushManager.getSubscription();
      })
      .then(subscription => {
        assert(!subscription, "Subscription not removed in worker");
        return self.registration.pushManager.subscribe();
      })
      .then(subscription => {
        return {
          endpoint: subscription.endpoint,
        };
      });
  },

  denySubscribe() {
    return self.registration.pushManager
      .getSubscription()
      .then(subscription => {
        assert(
          !subscription,
          "Should not return worker subscription with revoked permission"
        );
        return self.registration.pushManager.subscribe().then(
          _ => {
            assert(false, "Expected error subscribing with revoked permission");
          },
          error => {
            return {
              isDOMException: error instanceof DOMException,
              name: error.name,
            };
          }
        );
      });
  },

  subscribeWithKey(data) {
    return self.registration.pushManager
      .subscribe({
        applicationServerKey: data.key,
      })
      .then(
        subscription => {
          return {
            endpoint: subscription.endpoint,
            key: subscription.options.applicationServerKey,
          };
        },
        error => {
          return {
            isDOMException: error instanceof DOMException,
            name: error.name,
          };
        }
      );
  },
};

function handleMessage(event) {
  var handler = testHandlers[event.data.type];
  if (handler) {
    reply(event, handler(event.data));
  } else {
    reply(event, Promise.reject("Invalid message type: " + event.data.type));
  }
}

function handlePushSubscriptionChange(event) {
  broadcast(event, { type: "changed", okay: "yes" });
}

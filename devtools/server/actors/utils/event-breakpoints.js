/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 *
 * @param {string} groupID
 * @param {string} eventType
 * @param {Function} condition: Optional function that takes a Window as parameter. When
 *                   passed, the event will only be included if the result of the function
 *                   call is `true` (See `getAvailableEventBreakpoints`).
 * @returns {object}
 */
function generalEvent(groupID, eventType, condition) {
  return {
    id: `event.${groupID}.${eventType}`,
    type: "event",
    name: eventType,
    message: `DOM '${eventType}' event`,
    eventType,
    // DOM Events which may fire on the global object, or on DOM Elements
    targetTypes: ["global", "node"],
    condition,
  };
}
function nodeEvent(groupID, eventType) {
  return {
    ...generalEvent(groupID, eventType),
    targetTypes: ["node"],
  };
}
function mediaNodeEvent(groupID, eventType) {
  return {
    ...generalEvent(groupID, eventType),
    targetTypes: ["node"],

    // Media events need some specific handling in `eventBreakpointForNotification()`
    // to ensure that the event is fired on either <video> or <audio> tags.
    isMediaEvent: true,
  };
}
function globalEvent(groupID, eventType) {
  return {
    ...generalEvent(groupID, eventType),
    message: `Global '${eventType}' event`,
    // DOM Events which are only fired on the global object
    targetTypes: ["global"],
  };
}
function xhrEvent(groupID, eventType) {
  return {
    ...generalEvent(groupID, eventType),
    message: `XHR '${eventType}' event`,
    targetTypes: ["xhr"],
  };
}

function closeWatcherEvent(groupID, eventType) {
  return {
    ...generalEvent(groupID, eventType),
    message: `CloseWatcher '${eventType}' event`,
    targetTypes: ["closewatcher"],
  };
}

function webSocketEvent(groupID, eventType) {
  return {
    ...generalEvent(groupID, eventType),
    message: `WebSocket '${eventType}' event`,
    targetTypes: ["websocket"],
  };
}

function workerEvent(eventType) {
  return {
    ...generalEvent("worker", eventType),
    message: `Worker '${eventType}' event`,
    targetTypes: ["worker"],
  };
}

function timerEvent(type, operation, name, notificationType) {
  return {
    id: `timer.${type}.${operation}`,
    type: "simple",
    name,
    message: name,
    notificationType,
  };
}

function animationEvent(operation, name, notificationType) {
  return {
    id: `animationframe.${operation}`,
    type: "simple",
    name,
    message: name,
    notificationType,
  };
}

const SCRIPT_FIRST_STATEMENT_BREAKPOINT = {
  id: "script.source.firstStatement",
  type: "script",
  name: "Script First Statement",
  message: "Script First Statement",
};

const AVAILABLE_BREAKPOINTS = [
  {
    name: "Animation",
    items: [
      animationEvent(
        "request",
        "Request Animation Frame",
        "requestAnimationFrame"
      ),
      animationEvent(
        "cancel",
        "Cancel Animation Frame",
        "cancelAnimationFrame"
      ),
      animationEvent(
        "fire",
        "Animation Frame fired",
        "requestAnimationFrameCallback"
      ),
    ],
  },
  {
    name: "Clipboard",
    items: [
      generalEvent("clipboard", "copy"),
      generalEvent("clipboard", "cut"),
      generalEvent("clipboard", "paste"),
      generalEvent("clipboard", "beforecopy"),
      generalEvent("clipboard", "beforecut"),
      generalEvent("clipboard", "beforepaste"),
    ],
  },
  {
    name: "CloseWatcher",
    items: [
      closeWatcherEvent("closewatcher", "cancel"),
      closeWatcherEvent("closewatcher", "close"),
    ],
  },
  {
    name: "Control",
    items: [
      generalEvent("control", "beforetoggle"),
      generalEvent("control", "blur"),
      generalEvent("control", "change"),
      generalEvent("control", "focus"),
      generalEvent("control", "focusin"),
      generalEvent("control", "focusout"),
      generalEvent("control", "command"),
      generalEvent("control", "reset"),
      generalEvent("control", "resize"),
      generalEvent("control", "scroll"),
      generalEvent("control", "scrollend"),
      generalEvent("control", "select"),
      generalEvent("control", "toggle"),
      generalEvent("control", "submit"),
      generalEvent("control", "zoom"),
    ],
  },
  {
    name: "DOM Mutation",
    items: [
      // Deprecated DOM events.
      nodeEvent("dom-mutation", "DOMActivate"),
      nodeEvent("dom-mutation", "DOMFocusIn"),
      nodeEvent("dom-mutation", "DOMFocusOut"),

      // DOM load events.
      nodeEvent("dom-mutation", "DOMContentLoaded"),
    ],
  },
  {
    name: "Device",
    items: [
      globalEvent("device", "deviceorientation"),
      globalEvent("device", "devicemotion"),
    ],
  },
  {
    name: "Drag and Drop",
    items: [
      generalEvent("drag-and-drop", "drag"),
      generalEvent("drag-and-drop", "dragstart"),
      generalEvent("drag-and-drop", "dragend"),
      generalEvent("drag-and-drop", "dragenter"),
      generalEvent("drag-and-drop", "dragover"),
      generalEvent("drag-and-drop", "dragleave"),
      generalEvent("drag-and-drop", "drop"),
    ],
  },
  {
    name: "Keyboard",
    items: [
      generalEvent("keyboard", "beforeinput"),
      generalEvent("keyboard", "input"),
      generalEvent("keyboard", "textInput", () =>
        // Services.prefs isn't available on worker targets
        Services.prefs?.getBoolPref("dom.events.textevent.enabled")
      ),
      generalEvent("keyboard", "keydown"),
      generalEvent("keyboard", "keyup"),
      generalEvent("keyboard", "keypress"),
      generalEvent("keyboard", "compositionstart"),
      generalEvent("keyboard", "compositionupdate"),
      generalEvent("keyboard", "compositionend"),
    ].filter(Boolean),
  },
  {
    name: "Load",
    items: [
      globalEvent("load", "load"),
      globalEvent("load", "beforeunload"),
      globalEvent("load", "unload"),
      globalEvent("load", "abort"),
      globalEvent("load", "error"),
      globalEvent("load", "hashchange"),
      globalEvent("load", "popstate"),
    ],
  },
  {
    name: "Media",
    items: [
      mediaNodeEvent("media", "play"),
      mediaNodeEvent("media", "pause"),
      mediaNodeEvent("media", "playing"),
      mediaNodeEvent("media", "canplay"),
      mediaNodeEvent("media", "canplaythrough"),
      mediaNodeEvent("media", "seeking"),
      mediaNodeEvent("media", "seeked"),
      mediaNodeEvent("media", "timeupdate"),
      mediaNodeEvent("media", "ended"),
      mediaNodeEvent("media", "ratechange"),
      mediaNodeEvent("media", "durationchange"),
      mediaNodeEvent("media", "volumechange"),
      mediaNodeEvent("media", "loadstart"),
      mediaNodeEvent("media", "progress"),
      mediaNodeEvent("media", "suspend"),
      mediaNodeEvent("media", "abort"),
      mediaNodeEvent("media", "error"),
      mediaNodeEvent("media", "emptied"),
      mediaNodeEvent("media", "stalled"),
      mediaNodeEvent("media", "loadedmetadata"),
      mediaNodeEvent("media", "loadeddata"),
      mediaNodeEvent("media", "waiting"),
    ],
  },
  {
    name: "Mouse",
    items: [
      generalEvent("mouse", "auxclick"),
      generalEvent("mouse", "click"),
      generalEvent("mouse", "dblclick"),
      generalEvent("mouse", "mousedown"),
      generalEvent("mouse", "mouseup"),
      generalEvent("mouse", "mouseover"),
      generalEvent("mouse", "mousemove"),
      generalEvent("mouse", "mouseout"),
      generalEvent("mouse", "mouseenter"),
      generalEvent("mouse", "mouseleave"),
      generalEvent("mouse", "mousewheel"),
      generalEvent("mouse", "wheel"),
      generalEvent("mouse", "contextmenu"),
    ],
  },
  {
    name: "Pointer",
    items: [
      generalEvent("pointer", "pointerover"),
      generalEvent("pointer", "pointerout"),
      generalEvent("pointer", "pointerenter"),
      generalEvent("pointer", "pointerleave"),
      generalEvent("pointer", "pointerdown"),
      generalEvent("pointer", "pointerup"),
      generalEvent("pointer", "pointermove"),
      generalEvent("pointer", "pointercancel"),
      generalEvent("pointer", "pointerrawupdate"),
      generalEvent("pointer", "gotpointercapture"),
      generalEvent("pointer", "lostpointercapture"),
    ],
  },
  {
    name: "Script",
    items: [SCRIPT_FIRST_STATEMENT_BREAKPOINT],
  },
  {
    name: "Timer",
    items: [
      timerEvent("timeout", "set", "setTimeout", "setTimeout"),
      timerEvent("timeout", "clear", "clearTimeout", "clearTimeout"),
      timerEvent("timeout", "fire", "setTimeout fired", "setTimeoutCallback"),
      timerEvent("interval", "set", "setInterval", "setInterval"),
      timerEvent("interval", "clear", "clearInterval", "clearInterval"),
      timerEvent(
        "interval",
        "fire",
        "setInterval fired",
        "setIntervalCallback"
      ),
    ],
  },
  {
    name: "Touch",
    items: [
      generalEvent("touch", "touchstart"),
      generalEvent("touch", "touchmove"),
      generalEvent("touch", "touchend"),
      generalEvent("touch", "touchcancel"),
    ],
  },
  {
    name: "WebSocket",
    items: [
      webSocketEvent("websocket", "open"),
      webSocketEvent("websocket", "message"),
      webSocketEvent("websocket", "error"),
      webSocketEvent("websocket", "close"),
    ],
  },
  {
    name: "Worker",
    items: [
      workerEvent("message"),
      workerEvent("messageerror"),

      // Service Worker events.
      globalEvent("serviceworker", "fetch"),
    ],
  },
  {
    name: "XHR",
    items: [
      xhrEvent("xhr", "readystatechange"),
      xhrEvent("xhr", "load"),
      xhrEvent("xhr", "loadstart"),
      xhrEvent("xhr", "loadend"),
      xhrEvent("xhr", "abort"),
      xhrEvent("xhr", "error"),
      xhrEvent("xhr", "progress"),
      xhrEvent("xhr", "timeout"),
    ],
  },
];

const FLAT_EVENTS = [];
for (const category of AVAILABLE_BREAKPOINTS) {
  for (const event of category.items) {
    FLAT_EVENTS.push(event);
  }
}
const EVENTS_BY_ID = {};
for (const event of FLAT_EVENTS) {
  if (EVENTS_BY_ID[event.id]) {
    throw new Error("Duplicate event ID detected: " + event.id);
  }
  EVENTS_BY_ID[event.id] = event;
}

const SIMPLE_EVENTS = {};
const DOM_EVENTS = {};
for (const eventBP of FLAT_EVENTS) {
  if (eventBP.type === "simple") {
    const { notificationType } = eventBP;
    if (SIMPLE_EVENTS[notificationType]) {
      throw new Error("Duplicate simple event");
    }
    SIMPLE_EVENTS[notificationType] = eventBP.id;
  } else if (eventBP.type === "event") {
    const { eventType, targetTypes } = eventBP;

    if (!Array.isArray(targetTypes) || !targetTypes.length) {
      throw new Error("Expect a targetTypes array for each event definition");
    }

    for (const targetType of targetTypes) {
      let byEventType = DOM_EVENTS[targetType];
      if (!byEventType) {
        byEventType = {};
        DOM_EVENTS[targetType] = byEventType;
      }

      if (byEventType[eventType]) {
        throw new Error("Duplicate dom event: " + eventType);
      }
      byEventType[eventType] = eventBP.id;
    }
  } else if (eventBP.type === "script") {
    // Nothing to do.
  } else {
    throw new Error("Unknown type: " + eventBP.type);
  }
}

exports.eventBreakpointForNotification = eventBreakpointForNotification;
function eventBreakpointForNotification(dbg, notification) {
  const notificationType = notification.type;

  if (notification.type === "domEvent") {
    const domEventNotification = DOM_EVENTS[notification.targetType];
    if (!domEventNotification) {
      return null;
    }

    // The 'event' value is a cross-compartment wrapper for the DOM Event object.
    // While we could use that directly in the main thread as an Xray wrapper,
    // when debugging workers we can't, because it is an opaque wrapper.
    // To make things work, we have to always interact with the Event object via
    // the Debugger.Object interface.
    const evt = dbg
      .makeGlobalObjectReference(notification.global)
      .makeDebuggeeValue(notification.event);

    const eventType = evt.getProperty("type").return;
    const id = domEventNotification[eventType];
    if (!id) {
      return null;
    }
    const eventBreakpoint = EVENTS_BY_ID[id];

    // Does some additional checks for media events to ensure the DOM Event
    // was fired on either <audio> or <video> tags.
    if (eventBreakpoint.isMediaEvent) {
      const currentTarget = evt.getProperty("currentTarget").return;
      if (!currentTarget) {
        return null;
      }

      const nodeType = currentTarget.getProperty("nodeType").return;
      const namespaceURI = currentTarget.getProperty("namespaceURI").return;
      if (
        nodeType !== 1 /* ELEMENT_NODE */ ||
        namespaceURI !== "http://www.w3.org/1999/xhtml"
      ) {
        return null;
      }

      const nodeName = currentTarget
        .getProperty("nodeName")
        .return.toLowerCase();
      if (nodeName !== "audio" && nodeName !== "video") {
        return null;
      }
    }

    return id;
  }

  return SIMPLE_EVENTS[notificationType] || null;
}

exports.makeEventBreakpointMessage = makeEventBreakpointMessage;
function makeEventBreakpointMessage(id) {
  return EVENTS_BY_ID[id].message;
}

exports.firstStatementBreakpointId = firstStatementBreakpointId;
function firstStatementBreakpointId() {
  return SCRIPT_FIRST_STATEMENT_BREAKPOINT.id;
}

exports.eventsRequireNotifications = eventsRequireNotifications;
function eventsRequireNotifications(ids) {
  for (const id of ids) {
    const eventBreakpoint = EVENTS_BY_ID[id];

    // Script events are implemented directly in the server and do not require
    // notifications from Gecko, so there is no need to watch for them.
    if (eventBreakpoint && eventBreakpoint.type !== "script") {
      return true;
    }
  }
  return false;
}

exports.getAvailableEventBreakpoints = getAvailableEventBreakpoints;
/**
 * Get all available event breakpoints
 *
 * @param {Window|WorkerGlobalScope} global
 * @returns {Array<object>} An array containing object with a few properties :
 *    - {String} id: unique identifier
 *    - {String} name: Description for the event to be displayed in UI (no translated)
 *    - {String} type: Either "simple" or "event"
 *    Only for type="simple":
 *    - {String} notificationType: platform name of the event
 *    Only for type="event":
 *    - {String} eventType: platform name of the event
 *    - {Array<String>} targetTypes: List of potential target on which the event is fired.
 *                                   Can be "global", "node", "xhr", "worker",...
 */
function getAvailableEventBreakpoints(global) {
  const available = [];
  for (const { name, items } of AVAILABLE_BREAKPOINTS) {
    available.push({
      name,
      events: items
        .filter(item => !item.condition || item.condition(global))
        .map(item => ({
          id: item.id,

          // The name to be displayed in UI
          name: item.name,

          // The type of event: either simple or event
          type: item.type,

          // For type=simple
          notificationType: item.notificationType,

          // For type=event
          eventType: item.eventType,
          targetTypes: item.targetTypes,
        })),
    });
  }
  return available;
}
exports.validateEventBreakpoint = validateEventBreakpoint;
function validateEventBreakpoint(id) {
  return !!EVENTS_BY_ID[id];
}

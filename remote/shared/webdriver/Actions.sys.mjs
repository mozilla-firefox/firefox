/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",

  AppInfo: "chrome://remote/content/shared/AppInfo.sys.mjs",
  assert: "chrome://remote/content/shared/webdriver/Assert.sys.mjs",
  AsyncQueue: "chrome://remote/content/shared/AsyncQueue.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  event: "chrome://remote/content/shared/webdriver/Event.sys.mjs",
  keyData: "chrome://remote/content/shared/webdriver/KeyData.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  pprint: "chrome://remote/content/shared/Format.sys.mjs",
  Sleep: "chrome://remote/content/marionette/sync.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.MARIONETTE)
);

// TODO? With ES 2016 and Symbol you can make a safer approximation
// to an enum e.g. https://gist.github.com/xmlking/e86e4f15ec32b12c4689
/**
 * Implements WebDriver Actions API: a low-level interface for providing
 * virtualized device input to the web browser.
 *
 * Typical usage is to construct an action chain and then dispatch it:
 * const state = new actions.State();
 * const chain = await actions.Chain.fromJSON(state, protocolData);
 * await chain.dispatch(state, window);
 *
 * @namespace
 */
export const actions = {};

// Max interval between two clicks that should result in a dblclick or a tripleclick (in ms)
export const CLICK_INTERVAL = 640;

/** Map from normalized key value to UI Events modifier key name */
const MODIFIER_NAME_LOOKUP = {
  Alt: "alt",
  Shift: "shift",
  Control: "ctrl",
  Meta: "meta",
};

/**
 * Object containing various callback functions to be used when deserializing
 * action sequences and dispatching these.
 *
 * @typedef {object} ActionsOptions
 * @property {Function} isElementOrigin
 *     Function to check if it's a valid origin element.
 * @property {Function} getElementOrigin
 *     Function to retrieve the element reference for an element origin.
 * @property {Function} assertInViewPort
 *     Function to check if the coordinates [x, y] are in the visible viewport.
 * @property {Function} dispatchEvent
 *     Function to use for dispatching events.
 * @property {Function} getClientRects
 *     Function that retrieves the client rects for an element.
 * @property {Function} getInViewCentrePoint
 *     Function that calculates the in-view center point for the given
 *     coordinates [x, y].
 */

/**
 * State associated with actions.
 *
 * Typically each top-level navigable in a WebDriver session should have a
 * single State object.
 */
actions.State = class {
  #actionsQueue;

  /**
   * Creates a new {@link State} instance.
   */
  constructor() {
    // A queue that ensures that access to the input state is serialized.
    this.#actionsQueue = new lazy.AsyncQueue();

    // Tracker for mouse button clicks.
    this.clickTracker = new ClickTracker();

    /**
     * A map between input ID and the device state for that input
     * source, with one entry for each active input source.
     *
     * Maps string => InputSource
     */
    this.inputStateMap = new Map();

    /**
     * List of {@link Action} associated with current session.  Used to
     * manage dispatching events when resetting the state of the input sources.
     * Reset operations are assumed to be idempotent.
     */
    this.inputsToCancel = new TickActions();

    // Map between string input id and numeric pointer id.
    this.pointerIdMap = new Map();
  }

  /**
   * Returns the list of inputs to cancel when releasing the actions.
   *
   * @returns {TickActions}
   *     The inputs to cancel.
   */
  get inputCancelList() {
    return this.inputsToCancel;
  }

  toString() {
    return `[object ${this.constructor.name} ${JSON.stringify(this)}]`;
  }

  /**
   * Enqueue a new action task.
   *
   * @param {Function} task
   *     The task to queue.
   *
   * @returns {Promise}
   *     Promise that resolves when the task is completed, with the resolved
   *     value being the result of the task.
   */
  enqueueAction(task) {
    return this.#actionsQueue.enqueue(task);
  }

  /**
   * Get the state for a given input source.
   *
   * @param {string} id
   *     Id of the input source.
   *
   * @returns {InputSource}
   *     State of the input source.
   */
  getInputSource(id) {
    return this.inputStateMap.get(id);
  }

  /**
   * Find or add state for an input source.
   *
   * The caller should verify that the returned state is the expected type.
   *
   * @param {string} id
   *     Id of the input source.
   * @param {InputSource} newInputSource
   *     State of the input source.
   *
   * @returns {InputSource}
   *     The input source.
   */
  getOrAddInputSource(id, newInputSource) {
    let inputSource = this.getInputSource(id);

    if (inputSource === undefined) {
      this.inputStateMap.set(id, newInputSource);
      inputSource = newInputSource;
    }

    return inputSource;
  }

  /**
   * Iterate over all input states of a given type.
   *
   * @param {string} type
   *     Type name of the input source (e.g. "pointer").
   *
   * @returns {Iterator<string, InputSource>}
   *     Iterator over id and input source.
   */
  *inputSourcesByType(type) {
    for (const [id, inputSource] of this.inputStateMap) {
      if (inputSource.type === type) {
        yield [id, inputSource];
      }
    }
  }

  /**
   * Get a numerical pointer id for a given pointer.
   *
   * Pointer ids are positive integers. Mouse pointers are typically
   * ids 0 or 1. Non-mouse pointers never get assigned id < 2. Each
   * pointer gets a unique id.
   *
   * @param {string} id
   *     Id of the pointer.
   * @param {string} type
   *     Type of the pointer.
   *
   * @returns {number}
   *     The numerical pointer id.
   */
  getPointerId(id, type) {
    let pointerId = this.pointerIdMap.get(id);

    if (pointerId === undefined) {
      // Reserve pointer ids 0 and 1 for mouse pointers
      const idValues = Array.from(this.pointerIdMap.values());

      if (type === "mouse") {
        for (const mouseId of [0, 1]) {
          if (!idValues.includes(mouseId)) {
            pointerId = mouseId;
            break;
          }
        }
      }

      if (pointerId === undefined) {
        pointerId = Math.max(1, ...idValues) + 1;
      }
      this.pointerIdMap.set(id, pointerId);
    }

    return pointerId;
  }
};

/**
 * Tracker for mouse button clicks.
 */
export class ClickTracker {
  #count;
  #lastButtonClicked;
  #timer;

  /**
   * Creates a new {@link ClickTracker} instance.
   */
  constructor() {
    this.#count = 0;
    this.#lastButtonClicked = null;
  }

  get count() {
    return this.#count;
  }

  #cancelTimer() {
    lazy.clearTimeout(this.#timer);
  }

  #startTimer() {
    this.#timer = lazy.setTimeout(this.reset.bind(this), CLICK_INTERVAL);
  }

  /**
   * Reset tracking mouse click counter.
   */
  reset() {
    this.#cancelTimer();
    this.#count = 0;
    this.#lastButtonClicked = null;
  }

  /**
   * Track |button| click to identify possible double or triple click.
   *
   * @param {number} button
   *     A positive integer that refers to a mouse button.
   */
  setClick(button) {
    this.#cancelTimer();

    if (
      this.#lastButtonClicked === null ||
      this.#lastButtonClicked === button
    ) {
      this.#count++;
    } else {
      this.#count = 1;
    }

    this.#lastButtonClicked = button;
    this.#startTimer();
  }
}

/**
 * Device state for an input source.
 */
class InputSource {
  #id;
  static type = null;

  /**
   * Creates a new {@link InputSource} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   */
  constructor(id) {
    this.#id = id;
    this.type = this.constructor.type;
  }

  toString() {
    return `[object ${this.constructor.name} id: ${this.#id} type: ${
      this.type
    }]`;
  }

  /**
   * Unmarshals a JSON Object to an {@link InputSource}.
   *
   * @see https://w3c.github.io/webdriver/#dfn-get-or-create-an-input-source
   *
   * @param {State} actionState
   *     Actions state.
   * @param {Sequence} actionSequence
   *     Actions for a specific input source.
   *
   * @returns {InputSource}
   *     An {@link InputSource} object for the type of the
   *     action {@link Sequence}.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionSequence</code> is invalid.
   */
  static fromJSON(actionState, actionSequence) {
    const { id, type } = actionSequence;

    lazy.assert.string(
      id,
      lazy.pprint`Expected "id" to be a string, got ${id}`
    );

    const cls = inputSourceTypes.get(type);
    if (cls === undefined) {
      throw new lazy.error.InvalidArgumentError(
        lazy.pprint`Expected known action type, got ${type}`
      );
    }

    const sequenceInputSource = cls.fromJSON(actionState, actionSequence);
    const inputSource = actionState.getOrAddInputSource(
      id,
      sequenceInputSource
    );

    if (inputSource.type !== type) {
      throw new lazy.error.InvalidArgumentError(
        lazy.pprint`Expected input source ${id} to be ` +
          `type ${inputSource.type}, got ${type}`
      );
    }
  }
}

/**
 * Input state not associated with a specific physical device.
 */
class NullInputSource extends InputSource {
  static type = "none";

  /**
   * Unmarshals a JSON Object to a {@link NullInputSource}.
   *
   * @param {State} actionState
   *     Actions state.
   * @param {Sequence} actionSequence
   *     Actions for a specific input source.
   *
   * @returns {NullInputSource}
   *     A {@link NullInputSource} object for the type of the
   *     action {@link Sequence}.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionSequence</code> is invalid.
   */
  static fromJSON(actionState, actionSequence) {
    const { id } = actionSequence;

    return new this(id);
  }
}

/**
 * Input state associated with a keyboard-type device.
 */
class KeyInputSource extends InputSource {
  static type = "key";

  /**
   * Creates a new {@link KeyInputSource} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   */
  constructor(id) {
    super(id);

    this.pressed = new Set();
    this.alt = false;
    this.shift = false;
    this.ctrl = false;
    this.meta = false;
  }

  /**
   * Unmarshals a JSON Object to a {@link KeyInputSource}.
   *
   * @param {State} actionState
   *     Actions state.
   * @param {Sequence} actionSequence
   *     Actions for a specific input source.
   *
   * @returns {KeyInputSource}
   *     A {@link KeyInputSource} object for the type of the
   *     action {@link Sequence}.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionSequence</code> is invalid.
   */
  static fromJSON(actionState, actionSequence) {
    const { id } = actionSequence;

    return new this(id);
  }

  /**
   * Update modifier state according to |key|.
   *
   * @param {string} key
   *     Normalized key value of a modifier key.
   * @param {boolean} value
   *     Value to set the modifier attribute to.
   *
   * @throws {InvalidArgumentError}
   *     If |key| is not a modifier.
   */
  setModState(key, value) {
    if (key in MODIFIER_NAME_LOOKUP) {
      this[MODIFIER_NAME_LOOKUP[key]] = value;
    } else {
      throw new lazy.error.InvalidArgumentError(
        lazy.pprint`Expected "key" to be one of ${Object.keys(
          MODIFIER_NAME_LOOKUP
        )}, got ${key}`
      );
    }
  }

  /**
   * Check whether |key| is pressed.
   *
   * @param {string} key
   *     Normalized key value.
   *
   * @returns {boolean}
   *     True if |key| is in set of pressed keys.
   */
  isPressed(key) {
    return this.pressed.has(key);
  }

  /**
   * Add |key| to the set of pressed keys.
   *
   * @param {string} key
   *     Normalized key value.
   *
   * @returns {boolean}
   *     True if |key| is in list of pressed keys.
   */
  press(key) {
    return this.pressed.add(key);
  }

  /**
   * Remove |key| from the set of pressed keys.
   *
   * @param {string} key
   *     Normalized key value.
   *
   * @returns {boolean}
   *     True if |key| was present before removal, false otherwise.
   */
  release(key) {
    return this.pressed.delete(key);
  }
}

/**
 * Input state associated with a pointer-type device.
 */
class PointerInputSource extends InputSource {
  static type = "pointer";

  /**
   * Creates a new {@link PointerInputSource} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {Pointer} pointer
   *     The specific {@link Pointer} type associated with this input source.
   */
  constructor(id, pointer) {
    super(id);

    this.pointer = pointer;
    this.x = 0;
    this.y = 0;
    this.pressed = new Set();
  }

  /**
   * Check whether |button| is pressed.
   *
   * @param {number} button
   *     Positive integer that refers to a mouse button.
   *
   * @returns {boolean}
   *     True if |button| is in set of pressed buttons.
   */
  isPressed(button) {
    lazy.assert.positiveInteger(
      button,
      lazy.pprint`Expected "button" to be a positive integer, got ${button}`
    );

    return this.pressed.has(button);
  }

  /**
   * Add |button| to the set of pressed keys.
   *
   * @param {number} button
   *     Positive integer that refers to a mouse button.
   */
  press(button) {
    lazy.assert.positiveInteger(
      button,
      lazy.pprint`Expected "button" to be a positive integer, got ${button}`
    );

    this.pressed.add(button);
  }

  /**
   * Remove |button| from the set of pressed buttons.
   *
   * @param {number} button
   *     A positive integer that refers to a mouse button.
   *
   * @returns {boolean}
   *     True if |button| was present before removals, false otherwise.
   */
  release(button) {
    lazy.assert.positiveInteger(
      button,
      lazy.pprint`Expected "button" to be a positive integer, got ${button}`
    );

    return this.pressed.delete(button);
  }

  /**
   * Unmarshals a JSON Object to a {@link PointerInputSource}.
   *
   * @param {State} actionState
   *     Actions state.
   * @param {Sequence} actionSequence
   *     Actions for a specific input source.
   *
   * @returns {PointerInputSource}
   *     A {@link PointerInputSource} object for the type of the
   *     action {@link Sequence}.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionSequence</code> is invalid.
   */
  static fromJSON(actionState, actionSequence) {
    const { id, parameters } = actionSequence;
    let pointerType = "mouse";

    if (parameters !== undefined) {
      lazy.assert.object(
        parameters,
        lazy.pprint`Expected "parameters" to be an object, got ${parameters}`
      );

      if (parameters.pointerType !== undefined) {
        pointerType = lazy.assert.string(
          parameters.pointerType,
          lazy.pprint(
            `Expected "pointerType" to be a string, got ${parameters.pointerType}`
          )
        );

        if (!["mouse", "pen", "touch"].includes(pointerType)) {
          throw new lazy.error.InvalidArgumentError(
            lazy.pprint`Expected "pointerType" to be one of "mouse", "pen", or "touch"`
          );
        }
      }
    }

    const pointerId = actionState.getPointerId(id, pointerType);
    const pointer = Pointer.fromJSON(pointerId, pointerType);

    return new this(id, pointer);
  }
}

/**
 * Input state associated with a wheel-type device.
 */
class WheelInputSource extends InputSource {
  static type = "wheel";

  /**
   * Unmarshals a JSON Object to a {@link WheelInputSource}.
   *
   * @param {State} actionState
   *     Actions state.
   * @param {Sequence} actionSequence
   *     Actions for a specific input source.
   *
   * @returns {WheelInputSource}
   *     A {@link WheelInputSource} object for the type of the
   *     action {@link Sequence}.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionSequence</code> is invalid.
   */
  static fromJSON(actionState, actionSequence) {
    const { id } = actionSequence;

    return new this(id);
  }
}

const inputSourceTypes = new Map();
for (const cls of [
  NullInputSource,
  KeyInputSource,
  PointerInputSource,
  WheelInputSource,
]) {
  inputSourceTypes.set(cls.type, cls);
}

/**
 * Representation of a coordinate origin
 */
class Origin {
  /**
   * Viewport coordinates of the origin of this coordinate system.
   *
   * This is overridden in subclasses to provide a class-specific origin.
   */
  getOriginCoordinates() {
    throw new Error(
      `originCoordinates not defined for ${this.constructor.name}`
    );
  }

  /**
   * Convert [x, y] coordinates to viewport coordinates.
   *
   * @param {InputSource} inputSource
   *     State of the current input device
   * @param {Array<number>} coords
   *     Coordinates [x, y] of the target relative to the origin.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Array<number>}
   *     Viewport coordinates [x, y].
   */
  async getTargetCoordinates(inputSource, coords, options) {
    const [x, y] = coords;
    const origin = await this.getOriginCoordinates(inputSource, options);

    return [origin.x + x, origin.y + y];
  }

  /**
   * Unmarshals a JSON Object to an {@link Origin}.
   *
   * @param {string|Element=} origin
   *     Type of origin, one of "viewport", "pointer", {@link Element}
   *     or undefined.
   * @param {ActionsOptions} options
   *     Configuration for actions.
   *
   * @returns {Promise<Origin>}
   *     Promise that resolves to an {@link Origin} object
   *     representing the origin.
   *
   * @throws {InvalidArgumentError}
   *     If <code>origin</code> isn't a valid origin.
   */
  static async fromJSON(origin, options) {
    const { context, getElementOrigin, isElementOrigin } = options;

    if (origin === undefined || origin === "viewport") {
      return new ViewportOrigin();
    }

    if (origin === "pointer") {
      return new PointerOrigin();
    }

    if (isElementOrigin(origin)) {
      const element = await getElementOrigin(origin, context);

      return new ElementOrigin(element);
    }

    throw new lazy.error.InvalidArgumentError(
      `Expected "origin" to be undefined, "viewport", "pointer", ` +
        lazy.pprint`or an element, got: ${origin}`
    );
  }
}

class ViewportOrigin extends Origin {
  getOriginCoordinates() {
    return { x: 0, y: 0 };
  }
}

class PointerOrigin extends Origin {
  getOriginCoordinates(inputSource) {
    return { x: inputSource.x, y: inputSource.y };
  }
}

/**
 * Representation of an element origin.
 */
class ElementOrigin extends Origin {
  /**
   * Creates a new {@link ElementOrigin} instance.
   *
   * @param {Element} element
   *     The element providing the coordinate origin.
   */
  constructor(element) {
    super();

    this.element = element;
  }

  /**
   * Retrieve the coordinates of the origin's in-view center point.
   *
   * @param {InputSource} _inputSource
   *     [unused] Current input device.
   * @param {ActionsOptions} options
   *
   * @returns {Promise<Array<number>>}
   *     Promise that resolves to the coordinates [x, y].
   */
  async getOriginCoordinates(_inputSource, options) {
    const { context, getClientRects, getInViewCentrePoint } = options;

    const clientRects = await getClientRects(this.element, context);

    // The spec doesn't handle this case: https://github.com/w3c/webdriver/issues/1642
    if (!clientRects.length) {
      throw new lazy.error.MoveTargetOutOfBoundsError(
        lazy.pprint`Origin element ${this.element} is not displayed`
      );
    }

    return getInViewCentrePoint(clientRects[0], context);
  }
}

/**
 * Represents the behavior of a single input source at a single
 * point in time.
 */
class Action {
  /** Type of the input source associated with this action */
  static type = null;
  /** Type of action specific to the input source */
  static subtype = null;
  /** Whether this kind of action affects the overall duration of a tick */
  affectsWallClockTime = false;

  /**
   * Creates a new {@link Action} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   */
  constructor(id) {
    this.id = id;
    this.type = this.constructor.type;
    this.subtype = this.constructor.subtype;
  }

  toString() {
    return `[${this.constructor.name} ${this.type}:${this.subtype}]`;
  }

  /**
   * Dispatch the action to the relevant window.
   *
   * This is overridden by subclasses to implement the type-specific
   * dispatch of the action.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  dispatch() {
    throw new Error(
      `Action subclass ${this.constructor.name} must override dispatch()`
    );
  }

  /**
   * Unmarshals a JSON Object to an {@link Action}.
   *
   * @param {string} type
   *     Type of {@link InputSource}.
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} actionItem
   *     Object representing a single action.
   * @param {ActionsOptions} options
   *     Configuration for actions.
   *
   * @returns {Promise<Action>}
   *     Promise that resolves to an action that can be dispatched.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionItem</code> attribute is invalid.
   */
  static fromJSON(type, id, actionItem, options) {
    lazy.assert.object(
      actionItem,
      lazy.pprint`Expected "action" to be an object, got ${actionItem}`
    );

    const subtype = actionItem.type;
    const subtypeMap = actionTypes.get(type);

    if (subtypeMap === undefined) {
      throw new lazy.error.InvalidArgumentError(
        lazy.pprint`Expected known action type, got ${type}`
      );
    }

    let cls = subtypeMap.get(subtype);
    // Non-device specific actions can happen for any action type
    if (cls === undefined) {
      cls = actionTypes.get("none").get(subtype);
    }
    if (cls === undefined) {
      throw new lazy.error.InvalidArgumentError(
        lazy.pprint`Expected known subtype for type ${type}, got ${subtype}`
      );
    }

    return cls.fromJSON(id, actionItem, options);
  }
}

/**
 * Action not associated with a specific input device.
 */
class NullAction extends Action {
  static type = "none";
}

/**
 * Action that waits for a given duration.
 */
class PauseAction extends NullAction {
  static subtype = "pause";
  affectsWallClockTime = true;

  /**
   * Creates a new {@link PauseAction} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} options
   * @param {number} options.duration
   *     Time to pause, in ms.
   */
  constructor(id, options) {
    super(id);

    const { duration } = options;
    this.duration = duration;
  }

  /**
   * Dispatch pause action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     Length of the current tick, in ms.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  dispatch(state, inputSource, tickDuration) {
    const ms = this.duration ?? tickDuration;

    lazy.logger.trace(
      ` Dispatch ${this.constructor.name} with ${this.id} ${ms}`
    );

    return lazy.Sleep(ms);
  }

  /**
   * Unmarshals a JSON Object to a {@link PauseAction}.
   *
   * @see https://w3c.github.io/webdriver/#dfn-process-a-null-action
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} actionItem
   *     Object representing a single action.
   *
   * @returns {PauseAction}
   *     A pause action that can be dispatched.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionItem</code> attribute is invalid.
   */
  static fromJSON(id, actionItem) {
    const { duration } = actionItem;

    if (duration !== undefined) {
      lazy.assert.positiveInteger(
        duration,
        lazy.pprint`Expected "duration" to be a positive integer, got ${duration}`
      );
    }

    return new this(id, { duration });
  }
}

/**
 * Action associated with a keyboard input device
 */
class KeyAction extends Action {
  static type = "key";

  /**
   * Creates a new {@link KeyAction} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} options
   * @param {string} options.value
   *     The key character.
   */
  constructor(id, options) {
    super(id);

    const { value } = options;

    this.value = value;
  }

  getEventData(inputSource) {
    let value = this.value;

    if (inputSource.shift) {
      value = lazy.keyData.getShiftedKey(value);
    }

    return new KeyEventData(value);
  }

  /**
   * Unmarshals a JSON Object to a {@link KeyAction}.
   *
   * @see https://w3c.github.io/webdriver/#dfn-process-a-key-action
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} actionItem
   *     Object representing a single action.
   *
   * @returns {KeyAction}
   *     A key action that can be dispatched.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionItem</code> attribute is invalid.
   */
  static fromJSON(id, actionItem) {
    const { value } = actionItem;

    lazy.assert.string(
      value,
      'Expected "value" to be a string that represents single code point ' +
        lazy.pprint`or grapheme cluster, got ${value}`
    );

    let segmenter = new Intl.Segmenter();
    lazy.assert.that(v => {
      let graphemeIterator = segmenter.segment(v)[Symbol.iterator]();
      // We should have exactly one grapheme cluster, so the first iterator
      // value must be defined, but the second one must be undefined
      return (
        graphemeIterator.next().value !== undefined &&
        graphemeIterator.next().value === undefined
      );
    }, `Expected "value" to be a string that represents single code point or grapheme cluster, got "${value}"`)(
      value
    );

    return new this(id, { value });
  }
}

/**
 * Action equivalent to pressing a key on a keyboard.
 *
 * @param {string} id
 *     Id of {@link InputSource}.
 * @param {object} options
 * @param {string} options.value
 *     The key character.
 */
class KeyDownAction extends KeyAction {
  static subtype = "keyDown";

  /**
   * Dispatch a keydown action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    const { context, dispatchEvent } = options;

    lazy.logger.trace(
      ` Dispatch ${this.constructor.name} with ${this.id} ${this.value}`
    );

    const keyEvent = this.getEventData(inputSource);
    keyEvent.repeat = inputSource.isPressed(keyEvent.key);
    inputSource.press(keyEvent.key);

    if (keyEvent.key in MODIFIER_NAME_LOOKUP) {
      inputSource.setModState(keyEvent.key, true);
    }

    keyEvent.update(state, inputSource);

    await dispatchEvent("synthesizeKeyDown", context, {
      x: inputSource.x,
      y: inputSource.y,
      eventData: keyEvent,
    });

    // Append a copy of |this| with keyUp subtype if event dispatched
    state.inputsToCancel.push(new KeyUpAction(this.id, this));
  }
}

/**
 * Action equivalent to releasing a key on a keyboard.
 *
 * @param {string} id
 *     Id of {@link InputSource}.
 * @param {object} options
 * @param {string} options.value
 *     The key character.
 */
class KeyUpAction extends KeyAction {
  static subtype = "keyUp";

  /**
   * Dispatch a keyup action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    const { context, dispatchEvent } = options;

    lazy.logger.trace(
      ` Dispatch ${this.constructor.name} with ${this.id} ${this.value}`
    );

    const keyEvent = this.getEventData(inputSource);

    if (!inputSource.isPressed(keyEvent.key)) {
      return;
    }

    if (keyEvent.key in MODIFIER_NAME_LOOKUP) {
      inputSource.setModState(keyEvent.key, false);
    }

    inputSource.release(keyEvent.key);
    keyEvent.update(state, inputSource);

    await dispatchEvent("synthesizeKeyUp", context, {
      x: inputSource.x,
      y: inputSource.y,
      eventData: keyEvent,
    });
  }
}

/**
 * Action associated with a pointer input device
 */
class PointerAction extends Action {
  static type = "pointer";

  /**
   * Creates a new {@link PointerAction} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} options
   * @param {number=} options.width
   *     Width of pointer in pixels.
   * @param {number=} options.height
   *     Height of pointer in pixels.
   * @param {number=} options.pressure
   *     Pressure of pointer.
   * @param {number=} options.tangentialPressure
   *     Tangential pressure of pointer.
   * @param {number=} options.tiltX
   *     X tilt angle of pointer.
   * @param {number=} options.tiltY
   *     Y tilt angle of pointer.
   * @param {number=} options.twist
   *     Twist angle of pointer.
   * @param {number=} options.altitudeAngle
   *     Altitude angle of pointer.
   * @param {number=} options.azimuthAngle
   *     Azimuth angle of pointer.
   */
  constructor(id, options) {
    super(id);

    const {
      width,
      height,
      pressure,
      tangentialPressure,
      tiltX,
      tiltY,
      twist,
      altitudeAngle,
      azimuthAngle,
    } = options;

    this.width = width;
    this.height = height;
    this.pressure = pressure;
    this.tangentialPressure = tangentialPressure;
    this.tiltX = tiltX;
    this.tiltY = tiltY;
    this.twist = twist;
    this.altitudeAngle = altitudeAngle;
    this.azimuthAngle = azimuthAngle;
  }

  /**
   * Validate properties common to all pointer types.
   *
   * @param {object} actionItem
   *     Object representing a single pointer action.
   *
   * @returns {object}
   *     Properties of the pointer action; contains `width`, `height`,
   *     `pressure`, `tangentialPressure`, `tiltX`, `tiltY`, `twist`,
   *     `altitudeAngle`, and `azimuthAngle`.
   */
  static validateCommon(actionItem) {
    const {
      width,
      height,
      pressure,
      tangentialPressure,
      tiltX,
      tiltY,
      twist,
      altitudeAngle,
      azimuthAngle,
    } = actionItem;

    if (width !== undefined) {
      lazy.assert.positiveInteger(
        width,
        lazy.pprint`Expected "width" to be a positive integer, got ${width}`
      );
    }
    if (height !== undefined) {
      lazy.assert.positiveInteger(
        height,
        lazy.pprint`Expected "height" to be a positive integer, got ${height}`
      );
    }
    if (pressure !== undefined) {
      lazy.assert.numberInRange(
        pressure,
        [0, 1],
        lazy.pprint`Expected "pressure" to be in range 0 to 1, got ${pressure}`
      );
    }
    if (tangentialPressure !== undefined) {
      lazy.assert.numberInRange(
        tangentialPressure,
        [-1, 1],
        'Expected "tangentialPressure" to be in range -1 to 1, ' +
          lazy.pprint`got ${tangentialPressure}`
      );
    }
    if (tiltX !== undefined) {
      lazy.assert.integerInRange(
        tiltX,
        [-90, 90],
        lazy.pprint`Expected "tiltX" to be in range -90 to 90, got ${tiltX}`
      );
    }
    if (tiltY !== undefined) {
      lazy.assert.integerInRange(
        tiltY,
        [-90, 90],
        lazy.pprint`Expected "tiltY" to be in range -90 to 90, got ${tiltY}`
      );
    }
    if (twist !== undefined) {
      lazy.assert.integerInRange(
        twist,
        [0, 359],
        lazy.pprint`Expected "twist" to be in range 0 to 359, got ${twist}`
      );
    }
    if (altitudeAngle !== undefined) {
      lazy.assert.numberInRange(
        altitudeAngle,
        [0, Math.PI / 2],
        'Expected "altitudeAngle" to be in range 0 to ${Math.PI / 2}, ' +
          lazy.pprint`got ${altitudeAngle}`
      );
    }
    if (azimuthAngle !== undefined) {
      lazy.assert.numberInRange(
        azimuthAngle,
        [0, 2 * Math.PI],
        'Expected "azimuthAngle" to be in range 0 to ${2 * Math.PI}, ' +
          lazy.pprint`got ${azimuthAngle}`
      );
    }

    return {
      width,
      height,
      pressure,
      tangentialPressure,
      tiltX,
      tiltY,
      twist,
      altitudeAngle,
      azimuthAngle,
    };
  }
}

/**
 * Action associated with a pointer input device being depressed.
 */
class PointerDownAction extends PointerAction {
  static subtype = "pointerDown";

  /**
   * Creates a new {@link PointerAction} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} options
   * @param {number} options.button
   *     Button being pressed. For devices without buttons (e.g. touch),
   *     this should be 0.
   * @param {number=} options.width
   *     Width of pointer in pixels.
   * @param {number=} options.height
   *     Height of pointer in pixels.
   * @param {number=} options.pressure
   *     Pressure of pointer.
   * @param {number=} options.tangentialPressure
   *     Tangential pressure of pointer.
   * @param {number=} options.tiltX
   *     X tilt angle of pointer.
   * @param {number=} options.tiltY
   *     Y tilt angle of pointer.
   * @param {number=} options.twist
   *     Twist angle of pointer.
   * @param {number=} options.altitudeAngle
   *     Altitude angle of pointer.
   * @param {number=} options.azimuthAngle
   *     Azimuth angle of pointer.
   */
  constructor(id, options) {
    super(id, options);

    const { button } = options;
    this.button = button;
  }

  /**
   * Dispatch a pointerdown action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    lazy.logger.trace(
      `Dispatch ${this.constructor.name} ${inputSource.pointer.type} with id: ${this.id} button: ${this.button}`
    );

    if (inputSource.isPressed(this.button)) {
      return;
    }

    inputSource.press(this.button);

    await inputSource.pointer.pointerDown(state, inputSource, this, options);

    // Append a copy of |this| with pointerUp subtype if event dispatched
    state.inputsToCancel.push(new PointerUpAction(this.id, this));
  }

  /**
   * Unmarshals a JSON Object to a {@link PointerDownAction}.
   *
   * @see https://w3c.github.io/webdriver/#dfn-process-a-pointer-up-or-pointer-down-action
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} actionItem
   *     Object representing a single action.
   *
   * @returns {PointerDownAction}
   *     A pointer down action that can be dispatched.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionItem</code> attribute is invalid.
   */
  static fromJSON(id, actionItem) {
    const { button } = actionItem;
    const props = PointerAction.validateCommon(actionItem);

    lazy.assert.positiveInteger(
      button,
      lazy.pprint`Expected "button" to be a positive integer, got ${button}`
    );

    props.button = button;

    return new this(id, props);
  }
}

/**
 * Action associated with a pointer input device being released.
 */
class PointerUpAction extends PointerAction {
  static subtype = "pointerUp";

  /**
   * Creates a new {@link PointerUpAction} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} options
   * @param {number} options.button
   *     Button being pressed. For devices without buttons (e.g. touch),
   *     this should be 0.
   * @param {number=} options.width
   *     Width of pointer in pixels.
   * @param {number=} options.height
   *     Height of pointer in pixels.
   * @param {number=} options.pressure
   *     Pressure of pointer.
   * @param {number=} options.tangentialPressure
   *     Tangential pressure of pointer.
   * @param {number=} options.tiltX
   *     X tilt angle of pointer.
   * @param {number=} options.tiltY
   *     Y tilt angle of pointer.
   * @param {number=} options.twist
   *     Twist angle of pointer.
   * @param {number=} options.altitudeAngle
   *     Altitude angle of pointer.
   * @param {number=} options.azimuthAngle
   *     Azimuth angle of pointer.
   */
  constructor(id, options) {
    super(id, options);

    const { button } = options;
    this.button = button;
  }

  /**
   * Dispatch a pointerup action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    lazy.logger.trace(
      `Dispatch ${this.constructor.name} ${inputSource.pointer.type} with id: ${this.id} button: ${this.button}`
    );

    if (!inputSource.isPressed(this.button)) {
      return;
    }

    inputSource.release(this.button);

    await inputSource.pointer.pointerUp(state, inputSource, this, options);
  }

  /**
   * Unmarshals a JSON Object to a {@link PointerUpAction}.
   *
   * @see https://w3c.github.io/webdriver/#dfn-process-a-pointer-up-or-pointer-down-action
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} actionItem
   *     Object representing a single action.
   *
   * @returns {PointerUpAction}
   *     A pointer up action that can be dispatched.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionItem</code> attribute is invalid.
   */
  static fromJSON(id, actionItem) {
    const { button } = actionItem;
    const props = PointerAction.validateCommon(actionItem);

    lazy.assert.positiveInteger(
      button,
      lazy.pprint`Expected "button" to be a positive integer, got ${button}`
    );

    props.button = button;

    return new this(id, props);
  }
}

/**
 * Action associated with a pointer input device being moved.
 */
class PointerMoveAction extends PointerAction {
  static subtype = "pointerMove";
  affectsWallClockTime = true;

  /**
   * Creates a new {@link PointerMoveAction} instance.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} options
   * @param {number} options.origin
   *     {@link Origin} of target coordinates.
   * @param {number} options.x
   *     X value of scroll coordinates.
   * @param {number} options.y
   *     Y value of scroll coordinates.
   * @param {number=} options.width
   *     Width of pointer in pixels.
   * @param {number=} options.height
   *     Height of pointer in pixels.
   * @param {number=} options.pressure
   *     Pressure of pointer.
   * @param {number=} options.tangentialPressure
   *     Tangential pressure of pointer.
   * @param {number=} options.tiltX
   *     X tilt angle of pointer.
   * @param {number=} options.tiltY
   *     Y tilt angle of pointer.
   * @param {number=} options.twist
   *     Twist angle of pointer.
   * @param {number=} options.altitudeAngle
   *     Altitude angle of pointer.
   * @param {number=} options.azimuthAngle
   *     Azimuth angle of pointer.
   */
  constructor(id, options) {
    super(id, options);

    const { duration, origin, x, y } = options;
    this.duration = duration;

    this.origin = origin;
    this.x = x;
    this.y = y;
  }

  /**
   * Dispatch a pointermove action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    const { assertInViewPort, context } = options;

    lazy.logger.trace(
      `Dispatch ${this.constructor.name} ${inputSource.pointer.type} with id: ${this.id} x: ${this.x} y: ${this.y}`
    );

    const target = await this.origin.getTargetCoordinates(
      inputSource,
      [this.x, this.y],
      options
    );

    await assertInViewPort(target, context);

    return moveOverTime(
      [[inputSource.x, inputSource.y]],
      [target],
      this.duration ?? tickDuration,
      async _target =>
        await this.performPointerMoveStep(state, inputSource, _target, options)
    );
  }

  /**
   * Perform one part of a pointer move corresponding to a specific emitted event.
   *
   * @param {State} state
   *     The {@link State} of actions.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {Array<Array<number>>} targets
   *     Array of [x, y] arrays specifying the viewport coordinates to move to.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   */
  async performPointerMoveStep(state, inputSource, targets, options) {
    if (targets.length !== 1) {
      throw new Error(
        "PointerMoveAction.performPointerMoveStep requires a single target"
      );
    }

    const target = targets[0];
    lazy.logger.trace(
      `PointerMoveAction.performPointerMoveStep ${JSON.stringify(target)}`
    );
    if (target[0] == inputSource.x && target[1] == inputSource.y) {
      return;
    }

    await inputSource.pointer.pointerMove(
      state,
      inputSource,
      this,
      target[0],
      target[1],
      options
    );

    inputSource.x = target[0];
    inputSource.y = target[1];
  }

  /**
   * Unmarshals a JSON Object to a {@link PointerMoveAction}.
   *
   * @see https://w3c.github.io/webdriver/#dfn-process-a-pointer-move-action
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} actionItem
   *     Object representing a single action.
   * @param {ActionsOptions} options
   *     Configuration for actions.
   *
   * @returns {Promise<PointerMoveAction>}
   *     A pointer move action that can be dispatched.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionItem</code> attribute is invalid.
   */
  static async fromJSON(id, actionItem, options) {
    const { duration, origin, x, y } = actionItem;

    if (duration !== undefined) {
      lazy.assert.positiveInteger(
        duration,
        lazy.pprint`Expected "duration" to be a positive integer, got ${duration}`
      );
    }

    const originObject = await Origin.fromJSON(origin, options);

    lazy.assert.number(
      x,
      lazy.pprint`Expected "x" to be a finite number, got ${x}`
    );
    lazy.assert.number(
      y,
      lazy.pprint`Expected "y" to be a finite number, got ${y}`
    );

    const props = PointerAction.validateCommon(actionItem);
    props.duration = duration;
    props.origin = originObject;
    props.x = x;
    props.y = y;

    return new this(id, props);
  }
}

/**
 * Action associated with a wheel input device.
 */
class WheelAction extends Action {
  static type = "wheel";
}

/**
 * Action associated with scrolling a scroll wheel
 */
class WheelScrollAction extends WheelAction {
  static subtype = "scroll";
  affectsWallClockTime = true;

  /**
   * Creates a new {@link WheelScrollAction} instance.
   *
   * @param {number} id
   *     Id of {@link InputSource}.
   * @param {object} options
   * @param {Origin} options.origin
   *     {@link Origin} of target coordinates.
   * @param {number} options.x
   *     X value of scroll coordinates.
   * @param {number} options.y
   *     Y value of scroll coordinates.
   * @param {number} options.deltaX
   *     Number of CSS pixels to scroll in X direction.
   * @param {number} options.deltaY
   *     Number of CSS pixels to scroll in Y direction.
   */
  constructor(id, options) {
    super(id);

    const { duration, origin, x, y, deltaX, deltaY } = options;

    this.duration = duration;
    this.origin = origin;
    this.x = x;
    this.y = y;
    this.deltaX = deltaX;
    this.deltaY = deltaY;
  }

  /**
   * Unmarshals a JSON Object to a {@link WheelScrollAction}.
   *
   * @param {string} id
   *     Id of {@link InputSource}.
   * @param {object} actionItem
   *     Object representing a single action.
   * @param {ActionsOptions} options
   *     Configuration for actions.
   *
   * @returns {Promise<WheelScrollAction>}
   *     Promise that resolves to a wheel scroll action
   *     that can be dispatched.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionItem</code> attribute is invalid.
   */
  static async fromJSON(id, actionItem, options) {
    const { duration, origin, x, y, deltaX, deltaY } = actionItem;

    if (duration !== undefined) {
      lazy.assert.positiveInteger(
        duration,
        lazy.pprint`Expected "duration" to be a positive integer, got ${duration}`
      );
    }

    const originObject = await Origin.fromJSON(origin, options);

    if (originObject instanceof PointerOrigin) {
      throw new lazy.error.InvalidArgumentError(
        `"pointer" origin not supported for "wheel" input source.`
      );
    }

    lazy.assert.integer(
      x,
      lazy.pprint`Expected "x" to be an Integer, got ${x}`
    );
    lazy.assert.integer(
      y,
      lazy.pprint`Expected "y" to be an Integer, got ${y}`
    );
    lazy.assert.integer(
      deltaX,
      lazy.pprint`Expected "deltaX" to be an Integer, got ${deltaX}`
    );
    lazy.assert.integer(
      deltaY,
      lazy.pprint`Expected "deltaY" to be an Integer, got ${deltaY}`
    );

    return new this(id, {
      duration,
      origin: originObject,
      x,
      y,
      deltaX,
      deltaY,
    });
  }

  /**
   * Dispatch a wheel scroll action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    const { assertInViewPort, context } = options;

    lazy.logger.trace(
      `Dispatch ${this.constructor.name} with id: ${this.id} deltaX: ${this.deltaX} deltaY: ${this.deltaY}`
    );

    const scrollCoordinates = await this.origin.getTargetCoordinates(
      inputSource,
      [this.x, this.y],
      options
    );

    await assertInViewPort(scrollCoordinates, context);

    const startX = 0;
    const startY = 0;
    // This is an action-local state that holds the amount of scroll completed
    const deltaPosition = [startX, startY];

    return moveOverTime(
      [[startX, startY]],
      [[this.deltaX, this.deltaY]],
      this.duration ?? tickDuration,
      async deltaTarget =>
        await this.performOneWheelScroll(
          state,
          scrollCoordinates,
          deltaPosition,
          deltaTarget,
          options
        )
    );
  }

  /**
   * Perform one part of a wheel scroll corresponding to a specific emitted event.
   *
   * @param {State} state
   *     The {@link State} of actions.
   * @param {Array<number>} scrollCoordinates
   *     The viewport coordinates [x, y] of the scroll action.
   * @param {Array<number>} deltaPosition
   *     [deltaX, deltaY] coordinates of the scroll before this event.
   * @param {Array<Array<number>>} deltaTargets
   *     Array of [deltaX, deltaY] coordinates to scroll to.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   */
  async performOneWheelScroll(
    state,
    scrollCoordinates,
    deltaPosition,
    deltaTargets,
    options
  ) {
    const { context, dispatchEvent } = options;

    if (deltaTargets.length !== 1) {
      throw new Error("Can only scroll one wheel at a time");
    }
    if (deltaPosition[0] == this.deltaX && deltaPosition[1] == this.deltaY) {
      return;
    }

    const deltaTarget = deltaTargets[0];
    const deltaX = deltaTarget[0] - deltaPosition[0];
    const deltaY = deltaTarget[1] - deltaPosition[1];
    const eventData = new WheelEventData({
      deltaX,
      deltaY,
      deltaZ: 0,
    });
    eventData.update(state);

    await dispatchEvent("synthesizeWheelAtPoint", context, {
      x: scrollCoordinates[0],
      y: scrollCoordinates[1],
      eventData,
    });

    // Update the current scroll position for the caller
    deltaPosition[0] = deltaTarget[0];
    deltaPosition[1] = deltaTarget[1];
  }
}

/**
 * Group of actions representing behavior of all touch pointers during
 * a single tick.
 *
 * For touch pointers, we need to call into the platform once with all
 * the actions so that they are regarded as simultaneous. This means
 * we don't use the `dispatch()` method on the underlying actions, but
 * instead use one on this group object.
 */
class TouchActionGroup {
  static type = null;

  /**
   * Creates a new {@link TouchActionGroup} instance.
   */
  constructor() {
    this.type = this.constructor.type;
    this.actions = new Map();
  }

  static forType(type) {
    const cls = touchActionGroupTypes.get(type);

    return new cls();
  }

  /**
   * Add action corresponding to a specific pointer to the group.
   *
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {Action} action
   *     Action to add to the group.
   */
  addPointer(inputSource, action) {
    if (action.subtype !== this.type) {
      throw new Error(
        `Added action of unexpected type, got ${action.subtype}, expected ${this.type}`
      );
    }

    this.actions.set(action.id, [inputSource, action]);
  }

  /**
   * Dispatch the action group to the relevant window.
   *
   * This is overridden by subclasses to implement the type-specific
   * dispatch of the action.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  dispatch() {
    throw new Error(
      "TouchActionGroup subclass missing dispatch implementation"
    );
  }
}

/**
 * Group of actions representing behavior of all touch pointers
 * depressed during a single tick.
 */
class PointerDownTouchActionGroup extends TouchActionGroup {
  static type = "pointerDown";

  /**
   * Dispatch a pointerdown touch action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    const { context, dispatchEvent } = options;

    lazy.logger.trace(
      `Dispatch ${this.constructor.name} with ${Array.from(
        this.actions.values()
      ).map(x => x[1].id)}`
    );

    if (inputSource !== null) {
      throw new Error(
        "Expected null inputSource for PointerDownTouchActionGroup.dispatch"
      );
    }

    // Only include pointers that are not already depressed
    const filteredActions = Array.from(this.actions.values()).filter(
      ([actionInputSource, action]) =>
        !actionInputSource.isPressed(action.button)
    );

    if (filteredActions.length) {
      const eventData = new MultiTouchEventData("touchstart");

      for (const [actionInputSource, action] of filteredActions) {
        eventData.addPointerEventData(actionInputSource, action);
        actionInputSource.press(action.button);
        eventData.update(state, actionInputSource);
      }

      // Touch start events must include all depressed touch pointers
      for (const [id, pointerInputSource] of state.inputSourcesByType(
        "pointer"
      )) {
        if (
          pointerInputSource.pointer.type === "touch" &&
          !this.actions.has(id) &&
          pointerInputSource.isPressed(0)
        ) {
          eventData.addPointerEventData(pointerInputSource, {});
          eventData.update(state, pointerInputSource);
        }
      }

      await dispatchEvent("synthesizeMultiTouch", context, { eventData });

      for (const [, action] of filteredActions) {
        // Append a copy of |action| with pointerUp subtype if event dispatched
        state.inputsToCancel.push(new PointerUpAction(action.id, action));
      }
    }
  }
}

/**
 * Group of actions representing behavior of all touch pointers
 * released during a single tick.
 */
class PointerUpTouchActionGroup extends TouchActionGroup {
  static type = "pointerUp";

  /**
   * Dispatch a pointerup touch action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    const { context, dispatchEvent } = options;

    lazy.logger.trace(
      `Dispatch ${this.constructor.name} with ${Array.from(
        this.actions.values()
      ).map(x => x[1].id)}`
    );

    if (inputSource !== null) {
      throw new Error(
        "Expected null inputSource for PointerUpTouchActionGroup.dispatch"
      );
    }

    // Only include pointers that are not already depressed
    const filteredActions = Array.from(this.actions.values()).filter(
      ([actionInputSource, action]) =>
        actionInputSource.isPressed(action.button)
    );

    if (filteredActions.length) {
      const eventData = new MultiTouchEventData("touchend");
      for (const [actionInputSource, action] of filteredActions) {
        eventData.addPointerEventData(actionInputSource, action);
        actionInputSource.release(action.button);
        eventData.update(state, actionInputSource);
      }

      await dispatchEvent("synthesizeMultiTouch", context, { eventData });
    }
  }
}

/**
 * Group of actions representing behavior of all touch pointers
 * moved during a single tick.
 */
class PointerMoveTouchActionGroup extends TouchActionGroup {
  static type = "pointerMove";

  /**
   * Dispatch a pointermove touch action.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {number} tickDuration
   *     [unused] Length of the current tick, in ms.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action is complete.
   */
  async dispatch(state, inputSource, tickDuration, options) {
    const { assertInViewPort, context } = options;

    lazy.logger.trace(
      `Dispatch ${this.constructor.name} with ${Array.from(this.actions).map(
        x => x[1].id
      )}`
    );
    if (inputSource !== null) {
      throw new Error(
        "Expected null inputSource for PointerMoveTouchActionGroup.dispatch"
      );
    }

    let startCoords = [];
    let targetCoords = [];

    for (const [actionInputSource, action] of this.actions.values()) {
      const target = await action.origin.getTargetCoordinates(
        actionInputSource,
        [action.x, action.y],
        options
      );

      await assertInViewPort(target, context);

      startCoords.push([actionInputSource.x, actionInputSource.y]);
      targetCoords.push(target);
    }

    // Touch move events must include all depressed touch pointers, even if they are static
    // This can end up generating pointermove events even for static pointers, but Gecko
    // seems to generate a lot of pointermove events anyway, so this seems like the lesser
    // problem.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=1779206
    const staticTouchPointers = [];
    for (const [id, pointerInputSource] of state.inputSourcesByType(
      "pointer"
    )) {
      if (
        pointerInputSource.pointer.type === "touch" &&
        !this.actions.has(id) &&
        pointerInputSource.isPressed(0)
      ) {
        staticTouchPointers.push(pointerInputSource);
      }
    }

    return moveOverTime(
      startCoords,
      targetCoords,
      this.duration ?? tickDuration,
      async currentTargetCoords =>
        await this.performPointerMoveStep(
          state,
          staticTouchPointers,
          currentTargetCoords,
          options
        )
    );
  }

  /**
   * Perform one part of a pointer move corresponding to a specific emitted event.
   *
   * @param {State} state
   *     The {@link State} of actions.
   * @param {Array<PointerInputSource>} staticTouchPointers
   *     Array of PointerInputSource objects for pointers that aren't
   *     involved in the touch move.
   * @param {Array<Array<number>>} targetCoords
   *     Array of [x, y] arrays specifying the viewport coordinates to move to.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   */
  async performPointerMoveStep(
    state,
    staticTouchPointers,
    targetCoords,
    options
  ) {
    const { context, dispatchEvent } = options;

    if (targetCoords.length !== this.actions.size) {
      throw new Error("Expected one target per pointer");
    }

    const perPointerData = Array.from(this.actions.values()).map(
      ([inputSource, action], i) => {
        const target = targetCoords[i];
        return [inputSource, action, target];
      }
    );
    const reachedTarget = perPointerData.every(
      ([inputSource, , target]) =>
        target[0] === inputSource.x && target[1] === inputSource.y
    );

    if (reachedTarget) {
      return;
    }

    const eventData = new MultiTouchEventData("touchmove");
    for (const [inputSource, action, target] of perPointerData) {
      inputSource.x = target[0];
      inputSource.y = target[1];
      eventData.addPointerEventData(inputSource, action);
      eventData.update(state, inputSource);
    }

    for (const inputSource of staticTouchPointers) {
      eventData.addPointerEventData(inputSource, {});
      eventData.update(state, inputSource);
    }

    await dispatchEvent("synthesizeMultiTouch", context, { eventData });
  }
}

const touchActionGroupTypes = new Map();
for (const cls of [
  PointerDownTouchActionGroup,
  PointerUpTouchActionGroup,
  PointerMoveTouchActionGroup,
]) {
  touchActionGroupTypes.set(cls.type, cls);
}

/**
 * Split a transition from startCoord to targetCoord linearly over duration.
 *
 * startCoords and targetCoords are lists of [x,y] positions in some space
 * (e.g. screen position or scroll delta). This function will linearly
 * interpolate intermediate positions, sending out roughly one event
 * per frame to simulate moving between startCoord and targetCoord in
 * a time of tickDuration milliseconds. The callback function is
 * responsible for actually emitting the event, given the current
 * position in the coordinate space.
 *
 * @param {Array<Array>} startCoords
 *     Array of initial [x, y] coordinates for each input source involved
 *     in the move.
 * @param {Array<Array<number>>} targetCoords
 *     Array of target [x, y] coordinates for each input source involved
 *     in the move.
 * @param {number} duration
 *     Time in ms the move will take.
 * @param {Function} callback
 *     Function that actually performs the move. This takes a single parameter
 *     which is an array of [x, y] coordinates corresponding to the move
 *     targets.
 */
async function moveOverTime(startCoords, targetCoords, duration, callback) {
  lazy.logger.trace(
    `moveOverTime start: ${startCoords} target: ${targetCoords} duration: ${duration}`
  );

  if (startCoords.length !== targetCoords.length) {
    throw new Error(
      "Expected equal number of start coordinates and target coordinates"
    );
  }

  if (
    !startCoords.every(item => item.length == 2) ||
    !targetCoords.every(item => item.length == 2)
  ) {
    throw new Error(
      "Expected start coordinates target coordinates to be Array of multiple [x,y] coordinates."
    );
  }

  if (duration === 0) {
    // transition to destination in one step
    await callback(targetCoords);
    return;
  }

  const timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  // interval between transitions in ms, based on common vsync
  const fps60 = 17;

  const distances = targetCoords.map((targetCoord, i) => {
    const startCoord = startCoords[i];
    return [targetCoord[0] - startCoord[0], targetCoord[1] - startCoord[1]];
  });
  const ONE_SHOT = Ci.nsITimer.TYPE_ONE_SHOT;
  const startTime = Date.now();
  const transitions = (async () => {
    // wait |fps60| ms before performing first incremental transition
    await new Promise(resolveTimer =>
      timer.initWithCallback(resolveTimer, fps60, ONE_SHOT)
    );

    let durationRatio = Math.floor(Date.now() - startTime) / duration;
    const epsilon = fps60 / duration / 10;
    while (1 - durationRatio > epsilon) {
      const intermediateTargets = startCoords.map((startCoord, i) => {
        let distance = distances[i];
        return [
          Math.floor(durationRatio * distance[0] + startCoord[0]),
          Math.floor(durationRatio * distance[1] + startCoord[1]),
        ];
      });

      await Promise.all([
        callback(intermediateTargets),

        // wait |fps60| ms before performing next transition
        new Promise(resolveTimer =>
          timer.initWithCallback(resolveTimer, fps60, ONE_SHOT)
        ),
      ]);

      durationRatio = Math.floor(Date.now() - startTime) / duration;
    }
  })();

  await transitions;

  // perform last transition after all incremental moves are resolved and
  // durationRatio is close enough to 1
  await callback(targetCoords);
}

const actionTypes = new Map();
for (const cls of [
  KeyDownAction,
  KeyUpAction,
  PauseAction,
  PointerDownAction,
  PointerUpAction,
  PointerMoveAction,
  WheelScrollAction,
]) {
  if (!actionTypes.has(cls.type)) {
    actionTypes.set(cls.type, new Map());
  }
  actionTypes.get(cls.type).set(cls.subtype, cls);
}

/**
 * Implementation of the behavior of a specific type of pointer.
 *
 * @abstract
 */
class Pointer {
  /** Type of pointer */
  static type = null;

  /**
   * Creates a new {@link Pointer} instance.
   *
   * @param {number} id
   *     Numeric pointer id.
   */
  constructor(id) {
    this.id = id;
    this.type = this.constructor.type;
  }

  /**
   * Implementation of depressing the pointer.
   */
  pointerDown() {
    throw new Error(`Unimplemented pointerDown for pointerType ${this.type}`);
  }

  /**
   * Implementation of releasing the pointer.
   */
  pointerUp() {
    throw new Error(`Unimplemented pointerUp for pointerType ${this.type}`);
  }

  /**
   * Implementation of moving the pointer.
   */
  pointerMove() {
    throw new Error(`Unimplemented pointerMove for pointerType ${this.type}`);
  }

  /**
   * Unmarshals a JSON Object to a {@link Pointer}.
   *
   * @param {number} pointerId
   *     Numeric pointer id.
   * @param {string} pointerType
   *     Pointer type.
   *
   * @returns {Pointer}
   *     An instance of the Pointer class for {@link pointerType}.
   *
   * @throws {InvalidArgumentError}
   *     If {@link pointerType} is not a valid pointer type.
   */
  static fromJSON(pointerId, pointerType) {
    const cls = pointerTypes.get(pointerType);

    if (cls === undefined) {
      throw new lazy.error.InvalidArgumentError(
        'Expected "pointerType" type to be one of ' +
          lazy.pprint`${pointerTypes}, got ${pointerType}`
      );
    }

    return new cls(pointerId);
  }
}

/**
 * Implementation of mouse pointer behavior.
 */
class MousePointer extends Pointer {
  static type = "mouse";

  /**
   * Emits a pointer down event.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {PointerDownAction} action
   *     The pointer down action to perform.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that resolves when the event has been dispatched.
   */
  async pointerDown(state, inputSource, action, options) {
    const { context, dispatchEvent } = options;

    const mouseEvent = new MouseEventData("mousedown", {
      button: action.button,
    });
    mouseEvent.update(state, inputSource);

    if (mouseEvent.ctrlKey) {
      if (lazy.AppInfo.isMac) {
        mouseEvent.button = 2;
        state.clickTracker.reset();
      }
    } else {
      mouseEvent.clickCount = state.clickTracker.count + 1;
    }

    await dispatchEvent("synthesizeMouseAtPoint", context, {
      x: inputSource.x,
      y: inputSource.y,
      eventData: mouseEvent,
    });

    if (
      lazy.event.MouseButton.isSecondary(mouseEvent.button) ||
      (mouseEvent.ctrlKey && lazy.AppInfo.isMac)
    ) {
      const contextMenuEvent = { ...mouseEvent, type: "contextmenu" };

      await dispatchEvent("synthesizeMouseAtPoint", context, {
        x: inputSource.x,
        y: inputSource.y,
        eventData: contextMenuEvent,
      });
    }
  }

  /**
   * Emits a pointer up event.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {PointerUpAction} action
   *     The pointer up action to perform.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that resolves when the event has been dispatched.
   */
  async pointerUp(state, inputSource, action, options) {
    const { context, dispatchEvent } = options;

    const mouseEvent = new MouseEventData("mouseup", {
      button: action.button,
    });
    mouseEvent.update(state, inputSource);

    state.clickTracker.setClick(action.button);
    mouseEvent.clickCount = state.clickTracker.count;

    await dispatchEvent("synthesizeMouseAtPoint", context, {
      x: inputSource.x,
      y: inputSource.y,
      eventData: mouseEvent,
    });
  }

  /**
   * Emits a pointer down event.
   *
   * @param {State} state
   *     The {@link State} of the action.
   * @param {InputSource} inputSource
   *     Current input device.
   * @param {PointerMoveAction} action
   *     The pointer down action to perform.
   * @param {number} targetX
   *     Target x position to move the pointer to.
   * @param {number} targetY
   *     Target y position to move the pointer to.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that resolves when the event has been dispatched.
   */
  async pointerMove(state, inputSource, action, targetX, targetY, options) {
    const { context, dispatchEvent } = options;

    const mouseEvent = new MouseEventData("mousemove");
    mouseEvent.update(state, inputSource);

    await dispatchEvent("synthesizeMouseAtPoint", context, {
      x: targetX,
      y: targetY,
      eventData: mouseEvent,
    });

    state.clickTracker.reset();
  }
}

/*
 * The implementation here is empty because touch actions have to go via the
 * TouchActionGroup. So if we end up calling these methods that's a bug in
 * the code.
 */
class TouchPointer extends Pointer {
  static type = "touch";
}

/*
 * Placeholder for future pen type pointer support.
 */
class PenPointer extends Pointer {
  static type = "pen";
}

const pointerTypes = new Map();
for (const cls of [MousePointer, TouchPointer, PenPointer]) {
  pointerTypes.set(cls.type, cls);
}

/**
 * Represents a series of ticks, specifying which actions to perform at
 * each tick.
 */
actions.Chain = class extends Array {
  toString() {
    return `[chain ${super.toString()}]`;
  }

  /**
   * Dispatch the action chain to the relevant window.
   *
   * @param {State} state
   *     The {@link State} of actions.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that is resolved once the action chain is complete.
   */
  dispatch(state, options) {
    let i = 1;

    const chainEvents = (async () => {
      for (const tickActions of this) {
        lazy.logger.trace(`Dispatching tick ${i++}/${this.length}`);
        await tickActions.dispatch(state, options);
      }
    })();

    // Reset the current click tracker counter. We shouldn't be able to simulate
    // a double click with multiple action chains.
    state.clickTracker.reset();

    return chainEvents;
  }

  /* eslint-disable no-shadow */ // Shadowing is intentional for `actions`.
  /**
   *
   * Unmarshals a JSON Object to a {@link Chain}.
   *
   * @see https://w3c.github.io/webdriver/#dfn-extract-an-action-sequence
   *
   * @param {State} actionState
   *     The {@link State} of actions.
   * @param {Array<object>} actions
   *     Array of objects that each represent an action sequence.
   * @param {ActionsOptions} options
   *     Configuration for actions.
   *
   * @returns {Promise<Chain>}
   *     Promise resolving to an object that allows dispatching
   *     a chain of actions.
   *
   * @throws {InvalidArgumentError}
   *     If <code>actions</code> doesn't correspond to a valid action chain.
   */
  static async fromJSON(actionState, actions, options) {
    lazy.assert.array(
      actions,
      lazy.pprint`Expected "actions" to be an array, got ${actions}`
    );

    const actionsByTick = new this();
    for (const actionSequence of actions) {
      lazy.assert.object(
        actionSequence,
        'Expected "actions" item to be an object, ' +
          lazy.pprint`got ${actionSequence}`
      );

      const inputSourceActions = await Sequence.fromJSON(
        actionState,
        actionSequence,
        options
      );

      for (let i = 0; i < inputSourceActions.length; i++) {
        // new tick
        if (actionsByTick.length < i + 1) {
          actionsByTick.push(new TickActions());
        }
        actionsByTick[i].push(inputSourceActions[i]);
      }
    }

    return actionsByTick;
  }
  /* eslint-enable no-shadow */
};

/**
 * Represents the action for each input device to perform in a single tick.
 */
class TickActions extends Array {
  /**
   * Tick duration in milliseconds.
   *
   * @returns {number}
   *     Longest action duration in |tickActions| if any, or 0.
   */
  getDuration() {
    let max = 0;

    for (const action of this) {
      if (action.affectsWallClockTime && action.duration) {
        max = Math.max(action.duration, max);
      }
    }

    return max;
  }

  /**
   * Dispatch sequence of actions for this tick.
   *
   * This creates a Promise for one tick that resolves once the Promise
   * for each tick-action is resolved, which takes at least |tickDuration|
   * milliseconds.  The resolved set of events for each tick is followed by
   * firing of pending DOM events.
   *
   * Note that the tick-actions are dispatched in order, but they may have
   * different durations and therefore may not end in the same order.
   *
   * @param {State} state
   *     The {@link State} of actions.
   * @param {ActionsOptions} options
   *     Configuration of actions dispatch.
   *
   * @returns {Promise}
   *     Promise that resolves when tick is complete.
   */
  dispatch(state, options) {
    const tickDuration = this.getDuration();
    const tickActions = this.groupTickActions(state);
    const pendingEvents = tickActions.map(([inputSource, action]) =>
      action.dispatch(state, inputSource, tickDuration, options)
    );

    return Promise.all(pendingEvents);
  }

  /**
   * Group together actions from input sources that have to be
   * dispatched together.
   *
   * The actual transformation here is to group together touch pointer
   * actions into {@link TouchActionGroup} instances.
   *
   * @param {State} state
   *     The {@link State} of actions.
   *
   * @returns {Array<Array<InputSource?,Action|TouchActionGroup>>}
   *    Array of pairs. For ungrouped actions each element is
   *    [InputSource, Action] For touch actions there are multiple
   *    pointers handled at once, so the first item of the array is
   *    null, meaning the group has to perform its own handling of the
   *    relevant state, and the second element is a TouchActionGroup.
   */
  groupTickActions(state) {
    const touchActions = new Map();
    const groupedActions = [];

    for (const action of this) {
      const inputSource = state.getInputSource(action.id);
      if (action.type == "pointer" && inputSource.pointer.type === "touch") {
        lazy.logger.debug(
          `Grouping action ${action.type} ${action.id} ${action.subtype}`
        );
        let group = touchActions.get(action.subtype);
        if (group === undefined) {
          group = TouchActionGroup.forType(action.subtype);
          touchActions.set(action.subtype, group);
          groupedActions.push([null, group]);
        }
        group.addPointer(inputSource, action);
      } else {
        groupedActions.push([inputSource, action]);
      }
    }

    return groupedActions;
  }
}

/**
 * Represents one input source action sequence; this is essentially an
 * |Array<Action>|.
 *
 * This is a temporary object only used when constructing an {@link
 * action.Chain}.
 */
class Sequence extends Array {
  toString() {
    return `[sequence ${super.toString()}]`;
  }

  /**
   * Unmarshals a JSON Object to a {@link Sequence}.
   *
   * @see https://w3c.github.io/webdriver/#dfn-process-an-input-source-action-sequence
   *
   * @param {State} actionState
   *     The {@link State} of actions.
   * @param {object} actionSequence
   *     Protocol representation of the actions for a specific input source.
   * @param {ActionsOptions} options
   *     Configuration for actions.
   *
   * @returns {Promise<Array<Array<InputSource, Action | TouchActionGroup>>>}
   *     Promise that resolves to an object that allows dispatching a
   *     sequence of actions.
   *
   * @throws {InvalidArgumentError}
   *     If the <code>actionSequence</code> doesn't correspond to a valid action sequence.
   */
  static async fromJSON(actionState, actionSequence, options) {
    // used here to validate 'type' in addition to InputSource type below
    const { actions: actionsFromSequence, id, type } = actionSequence;

    // type and id get validated in InputSource.fromJSON
    lazy.assert.array(
      actionsFromSequence,
      'Expected "actionSequence.actions" to be an array, ' +
        lazy.pprint`got ${actionSequence.actions}`
    );

    // This sets the input state in the global state map, if it's new.
    InputSource.fromJSON(actionState, actionSequence);

    const sequence = new this();
    for (const actionItem of actionsFromSequence) {
      sequence.push(await Action.fromJSON(type, id, actionItem, options));
    }

    return sequence;
  }
}

/**
 * Representation of an input event.
 */
class InputEventData {
  /**
   * Creates a new {@link InputEventData} instance.
   */
  constructor() {
    this.altKey = false;
    this.shiftKey = false;
    this.ctrlKey = false;
    this.metaKey = false;
  }

  /**
   * Update the input data based on global and input state
   */
  update() {}

  toString() {
    return `${this.constructor.name} ${JSON.stringify(this)}`;
  }
}

/**
 * Representation of a key input event.
 */
class KeyEventData extends InputEventData {
  /**
   * Creates a new {@link KeyEventData} instance.
   *
   * @param {string} rawKey
   *     The key value.
   */
  constructor(rawKey) {
    super();

    const { key, code, location, printable } = lazy.keyData.getData(rawKey);

    this.key = key;
    this.code = code;
    this.location = location;
    this.printable = printable;
    this.repeat = false;
    // keyCode will be computed by event.sendKeyDown
  }

  update(state, inputSource) {
    this.altKey = inputSource.alt;
    this.shiftKey = inputSource.shift;
    this.ctrlKey = inputSource.ctrl;
    this.metaKey = inputSource.meta;
  }
}

/**
 * Representation of a pointer input event.
 */
class PointerEventData extends InputEventData {
  /**
   * Creates a new {@link PointerEventData} instance.
   *
   * @param {string} type
   *     The event type.
   */
  constructor(type) {
    super();

    this.type = type;
    this.buttons = 0;
  }

  update(state, inputSource) {
    // set modifier properties based on whether any corresponding keys are
    // pressed on any key input source
    for (const [, otherInputSource] of state.inputSourcesByType("key")) {
      this.altKey = otherInputSource.alt || this.altKey;
      this.ctrlKey = otherInputSource.ctrl || this.ctrlKey;
      this.metaKey = otherInputSource.meta || this.metaKey;
      this.shiftKey = otherInputSource.shift || this.shiftKey;
    }
    const allButtons = Array.from(inputSource.pressed);
    this.buttons = allButtons.reduce(
      (a, i) => a + PointerEventData.getButtonFlag(i),
      0
    );
  }

  /**
   * Return a flag for buttons which indicates a button is pressed.
   *
   * @param {integer} button
   *     The mouse button number.
   */
  static getButtonFlag(button) {
    switch (button) {
      case 1:
        return 4;
      case 2:
        return 2;
      default:
        return Math.pow(2, button);
    }
  }
}

/**
 * Representation of a mouse input event.
 */
class MouseEventData extends PointerEventData {
  /**
   * Creates a new {@link MouseEventData} instance.
   *
   * @param {string} type
   *     The event type.
   * @param {object=} options
   * @param {number=} options.button
   *     The number of the mouse button. Defaults to 0.
   */
  constructor(type, options = {}) {
    super(type);

    const { button = 0 } = options;

    this.button = button;
    this.buttons = 0;

    // Some WPTs try to synthesize DnD only with mouse events.  However,
    // Gecko waits DnD events directly and non-WPT-tests use Gecko specific
    // test API to synthesize DnD.  Therefore, we want new path only for
    // synthesized events coming from the webdriver.
    this.allowToHandleDragDrop = true;
  }

  update(state, inputSource) {
    super.update(state, inputSource);

    this.id = inputSource.pointer.id;
  }
}

/**
 * Representation of a wheel scroll event.
 */
class WheelEventData extends InputEventData {
  /**
   * Creates a new {@link WheelEventData} instance.
   *
   * @param {object} options
   * @param {number} options.deltaX
   *     Scroll delta X.
   * @param {number} options.deltaY
   *     Scroll delta Y.
   * @param {number} options.deltaZ
   *     Scroll delta Z (current always 0).
   * @param {number=} options.deltaMode
   *     Scroll delta mode (current always 0).
   */
  constructor(options) {
    super();

    const { deltaX, deltaY, deltaZ, deltaMode = 0 } = options;

    this.deltaX = deltaX;
    this.deltaY = deltaY;
    this.deltaZ = deltaZ;
    this.deltaMode = deltaMode;

    this.altKey = false;
    this.ctrlKey = false;
    this.metaKey = false;
    this.shiftKey = false;
  }

  update(state) {
    // set modifier properties based on whether any corresponding keys are
    // pressed on any key input source
    for (const [, otherInputSource] of state.inputSourcesByType("key")) {
      this.altKey = otherInputSource.alt || this.altKey;
      this.ctrlKey = otherInputSource.ctrl || this.ctrlKey;
      this.metaKey = otherInputSource.meta || this.metaKey;
      this.shiftKey = otherInputSource.shift || this.shiftKey;
    }
  }
}

/**
 * Representation of a multi touch event.
 */
class MultiTouchEventData extends PointerEventData {
  #setGlobalState;

  /**
   * Creates a new {@link MultiTouchEventData} instance.
   *
   * @param {string} type
   *     The event type.
   */
  constructor(type) {
    super(type);

    this.id = [];
    this.x = [];
    this.y = [];
    this.rx = [];
    this.ry = [];
    this.angle = [];
    this.force = [];
    this.tiltx = [];
    this.tilty = [];
    this.twist = [];
    this.#setGlobalState = false;
  }

  /**
   * Add the data from one pointer to the event.
   *
   * @param {InputSource} inputSource
   *     The state of the pointer.
   * @param {PointerAction} action
   *     Action for the pointer.
   */
  addPointerEventData(inputSource, action) {
    this.x.push(inputSource.x);
    this.y.push(inputSource.y);
    this.id.push(inputSource.pointer.id);
    this.rx.push(action.width || 1);
    this.ry.push(action.height || 1);
    this.angle.push(0);
    this.force.push(action.pressure || (this.type === "touchend" ? 0 : 1));
    this.tiltx.push(action.tiltX || 0);
    this.tilty.push(action.tiltY || 0);
    this.twist.push(action.twist || 0);
  }

  update(state, inputSource) {
    // We call update once per input source, but only want to update global state once.
    // Instead of introducing a new lifecycle method, or changing the API to allow multiple
    // input sources in a single call, use a small bit of state to avoid repeatedly setting
    // global state.
    if (!this.#setGlobalState) {
      // set modifier properties based on whether any corresponding keys are
      // pressed on any key input source
      for (const [, otherInputSource] of state.inputSourcesByType("key")) {
        this.altKey = otherInputSource.alt || this.altKey;
        this.ctrlKey = otherInputSource.ctrl || this.ctrlKey;
        this.metaKey = otherInputSource.meta || this.metaKey;
        this.shiftKey = otherInputSource.shift || this.shiftKey;
      }
      this.#setGlobalState = true;
    }

    // Note that we currently emit Touch events that don't have this property
    // but pointer events should have a `buttons` property, so we'll compute it
    // anyway.
    const allButtons = Array.from(inputSource.pressed);
    this.buttons =
      this.buttons |
      allButtons.reduce((a, i) => a + PointerEventData.getButtonFlag(i), 0);
  }
}

// Helpers

/**
 * Assert that target is in the viewport of win.
 *
 * @param {Array<number>} target
 *     Coordinates [x, y] of the target relative to the viewport.
 * @param {WindowProxy} win
 *     The target window.
 *
 * @throws {MoveTargetOutOfBoundsError}
 *     If target is outside the viewport.
 */
export function assertTargetInViewPort(target, win) {
  const [x, y] = target;

  lazy.assert.number(
    x,
    lazy.pprint`Expected "x" to be finite number, got ${x}`
  );
  lazy.assert.number(
    y,
    lazy.pprint`Expected "y" to be finite number, got ${y}`
  );

  // Viewport includes scrollbars if rendered.
  if (x < 0 || y < 0 || x > win.innerWidth || y > win.innerHeight) {
    throw new lazy.error.MoveTargetOutOfBoundsError(
      `Move target (${x}, ${y}) is out of bounds of viewport dimensions ` +
        `(${win.innerWidth}, ${win.innerHeight})`
    );
  }
}

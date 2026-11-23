// Sourced from https://github.com/apple/password-manager-resources/blob/5f6da89483e75cdc4165a6fc4756796e0ced7a21/tools/PasswordRulesParser.js
// Copyright (c) 2019 - 2020 Apple Inc. Licensed under MIT License.

export const PasswordRulesParser = {
  parsePasswordRules,
  transformRulesToMap,
};

const Identifier = {
  ASCII_PRINTABLE: "ascii-printable",
  DIGIT: "digit",
  LOWER: "lower",
  SPECIAL: "special",
  UNICODE: "unicode",
  UPPER: "upper",
};

const RuleName = {
  ALLOWED: "allowed",
  MAX_CONSECUTIVE: "max-consecutive",
  REQUIRED: "required",
  MIN_LENGTH: "minlength",
  MAX_LENGTH: "maxlength",
};

const CHARACTER_CLASS_START_SENTINEL = "[";
const CHARACTER_CLASS_END_SENTINEL = "]";
const PROPERTY_VALUE_SEPARATOR = ",";
const PROPERTY_SEPARATOR = ";";
const PROPERTY_VALUE_START_SENTINEL = ":";

const SPACE_CODE_POINT = " ".codePointAt(0);

const SHOULD_NOT_BE_REACHED = "Should not be reached";

class Rule {
  constructor(name, value) {
    this._name = name;
    this.value = value;
  }
  get name() {
    return this._name;
  }
  toString() {
    return JSON.stringify(this);
  }
}

class NamedCharacterClass {
  constructor(name) {
    console.assert(_isValidRequiredOrAllowedPropertyValueIdentifier(name));
    this._name = name;
  }
  get name() {
    return this._name.toLowerCase();
  }
  toString() {
    return this._name;
  }
  toHTMLString() {
    return this._name;
  }
}

class CustomCharacterClass {
  constructor(characters) {
    console.assert(characters instanceof Array);
    this._characters = characters;
  }
  get characters() {
    return this._characters;
  }
  toString() {
    return `[${this._characters.join("")}]`;
  }
  toHTMLString() {
    return `[${this._characters.join("").replace('"', "&quot;")}]`;
  }
}

// MARK: Lexer functions

function _isIdentifierCharacter(c) {
  console.assert(c.length === 1);
  return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c === "-";
}

function _isASCIIDigit(c) {
  console.assert(c.length === 1);
  return c >= "0" && c <= "9";
}

function _isASCIIPrintableCharacter(c) {
  console.assert(c.length === 1);
  return c >= " " && c <= "~";
}

function _isASCIIWhitespace(c) {
  console.assert(c.length === 1);
  return c === " " || c === "\f" || c === "\n" || c === "\r" || c === "\t";
}

// MARK: ASCII printable character bit set and canonicalization functions

function _bitSetIndexForCharacter(c) {
  console.assert(c.length == 1);
  return c.codePointAt(0) - SPACE_CODE_POINT;
}

function _characterAtBitSetIndex(index) {
  return String.fromCodePoint(index + SPACE_CODE_POINT);
}

function _markBitsForNamedCharacterClass(bitSet, namedCharacterClass) {
  console.assert(bitSet instanceof Array);
  console.assert(namedCharacterClass.name !== Identifier.UNICODE);
  console.assert(namedCharacterClass.name !== Identifier.ASCII_PRINTABLE);
  if (namedCharacterClass.name === Identifier.UPPER) {
    bitSet.fill(
      true,
      _bitSetIndexForCharacter("A"),
      _bitSetIndexForCharacter("Z") + 1
    );
  } else if (namedCharacterClass.name === Identifier.LOWER) {
    bitSet.fill(
      true,
      _bitSetIndexForCharacter("a"),
      _bitSetIndexForCharacter("z") + 1
    );
  } else if (namedCharacterClass.name === Identifier.DIGIT) {
    bitSet.fill(
      true,
      _bitSetIndexForCharacter("0"),
      _bitSetIndexForCharacter("9") + 1
    );
  } else if (namedCharacterClass.name === Identifier.SPECIAL) {
    bitSet.fill(
      true,
      _bitSetIndexForCharacter(" "),
      _bitSetIndexForCharacter("/") + 1
    );
    bitSet.fill(
      true,
      _bitSetIndexForCharacter(":"),
      _bitSetIndexForCharacter("@") + 1
    );
    bitSet.fill(
      true,
      _bitSetIndexForCharacter("["),
      _bitSetIndexForCharacter("`") + 1
    );
    bitSet.fill(
      true,
      _bitSetIndexForCharacter("{"),
      _bitSetIndexForCharacter("~") + 1
    );
  } else {
    console.assert(false, SHOULD_NOT_BE_REACHED, namedCharacterClass);
  }
}

function _markBitsForCustomCharacterClass(bitSet, customCharacterClass) {
  for (let character of customCharacterClass.characters) {
    bitSet[_bitSetIndexForCharacter(character)] = true;
  }
}

function _canonicalizedPropertyValues(
  propertyValues,
  keepCustomCharacterClassFormatCompliant
) {
  let asciiPrintableBitSet = new Array(
    "~".codePointAt(0) - " ".codePointAt(0) + 1
  );

  for (let propertyValue of propertyValues) {
    if (propertyValue instanceof NamedCharacterClass) {
      if (propertyValue.name === Identifier.UNICODE) {
        return [new NamedCharacterClass(Identifier.UNICODE)];
      }

      if (propertyValue.name === Identifier.ASCII_PRINTABLE) {
        return [new NamedCharacterClass(Identifier.ASCII_PRINTABLE)];
      }

      _markBitsForNamedCharacterClass(asciiPrintableBitSet, propertyValue);
    } else if (propertyValue instanceof CustomCharacterClass) {
      _markBitsForCustomCharacterClass(asciiPrintableBitSet, propertyValue);
    }
  }

  let charactersSeen = [];

  function checkRange(start, end) {
    let temp = [];
    for (
      let i = _bitSetIndexForCharacter(start);
      i <= _bitSetIndexForCharacter(end);
      ++i
    ) {
      if (asciiPrintableBitSet[i]) {
        temp.push(_characterAtBitSetIndex(i));
      }
    }

    let result =
      temp.length ===
      _bitSetIndexForCharacter(end) - _bitSetIndexForCharacter(start) + 1;
    if (!result) {
      charactersSeen = charactersSeen.concat(temp);
    }
    return result;
  }

  let hasAllUpper = checkRange("A", "Z");
  let hasAllLower = checkRange("a", "z");
  let hasAllDigits = checkRange("0", "9");

  // Check for special characters, accounting for characters that are given special treatment (i.e. '-' and ']')
  let hasAllSpecial = false;
  let hasDash = false;
  let hasRightSquareBracket = false;
  let temp = [];
  for (
    let i = _bitSetIndexForCharacter(" ");
    i <= _bitSetIndexForCharacter("/");
    ++i
  ) {
    if (!asciiPrintableBitSet[i]) {
      continue;
    }

    let character = _characterAtBitSetIndex(i);
    if (keepCustomCharacterClassFormatCompliant && character === "-") {
      hasDash = true;
    } else {
      temp.push(character);
    }
  }
  for (
    let i = _bitSetIndexForCharacter(":");
    i <= _bitSetIndexForCharacter("@");
    ++i
  ) {
    if (asciiPrintableBitSet[i]) {
      temp.push(_characterAtBitSetIndex(i));
    }
  }
  for (
    let i = _bitSetIndexForCharacter("[");
    i <= _bitSetIndexForCharacter("`");
    ++i
  ) {
    if (!asciiPrintableBitSet[i]) {
      continue;
    }

    let character = _characterAtBitSetIndex(i);
    if (keepCustomCharacterClassFormatCompliant && character === "]") {
      hasRightSquareBracket = true;
    } else {
      temp.push(character);
    }
  }
  for (
    let i = _bitSetIndexForCharacter("{");
    i <= _bitSetIndexForCharacter("~");
    ++i
  ) {
    if (asciiPrintableBitSet[i]) {
      temp.push(_characterAtBitSetIndex(i));
    }
  }

  if (hasDash) {
    temp.unshift("-");
  }
  if (hasRightSquareBracket) {
    temp.push("]");
  }

  let numberOfSpecialCharacters =
    _bitSetIndexForCharacter("/") -
    _bitSetIndexForCharacter(" ") +
    1 +
    (_bitSetIndexForCharacter("@") - _bitSetIndexForCharacter(":") + 1) +
    (_bitSetIndexForCharacter("`") - _bitSetIndexForCharacter("[") + 1) +
    (_bitSetIndexForCharacter("~") - _bitSetIndexForCharacter("{") + 1);
  hasAllSpecial = temp.length === numberOfSpecialCharacters;
  if (!hasAllSpecial) {
    charactersSeen = charactersSeen.concat(temp);
  }

  let result = [];
  if (hasAllUpper && hasAllLower && hasAllDigits && hasAllSpecial) {
    return [new NamedCharacterClass(Identifier.ASCII_PRINTABLE)];
  }
  if (hasAllUpper) {
    result.push(new NamedCharacterClass(Identifier.UPPER));
  }
  if (hasAllLower) {
    result.push(new NamedCharacterClass(Identifier.LOWER));
  }
  if (hasAllDigits) {
    result.push(new NamedCharacterClass(Identifier.DIGIT));
  }
  if (hasAllSpecial) {
    result.push(new NamedCharacterClass(Identifier.SPECIAL));
  }
  if (charactersSeen.length) {
    result.push(new CustomCharacterClass(charactersSeen));
  }
  return result;
}

// MARK: Parser functions

function _indexOfNonWhitespaceCharacter(input, position = 0) {
  console.assert(position >= 0);
  console.assert(position <= input.length);

  let length = input.length;
  while (position < length && _isASCIIWhitespace(input[position])) {
    ++position;
  }

  return position;
}

function _parseIdentifier(input, position) {
  console.assert(position >= 0);
  console.assert(position < input.length);
  console.assert(_isIdentifierCharacter(input[position]));

  let length = input.length;
  let seenIdentifiers = [];
  do {
    let c = input[position];
    if (!_isIdentifierCharacter(c)) {
      break;
    }

    seenIdentifiers.push(c);
    ++position;
  } while (position < length);

  return [seenIdentifiers.join(""), position];
}

function _isValidRequiredOrAllowedPropertyValueIdentifier(identifier) {
  return (
    identifier && Object.values(Identifier).includes(identifier.toLowerCase())
  );
}

function _parseCustomCharacterClass(input, position) {
  console.assert(position >= 0);
  console.assert(position < input.length);
  console.assert(input[position] === CHARACTER_CLASS_START_SENTINEL);

  let length = input.length;
  ++position;
  if (position >= length) {
    console.error("Found end-of-line instead of character class character");
    return [null, position];
  }

  let initialPosition = position;
  let result = [];
  do {
    let c = input[position];
    if (!_isASCIIPrintableCharacter(c)) {
      ++position;
      continue;
    }

    if (c === "-" && position - initialPosition > 0) {
      // FIXME: Should this be an error?
      console.warn(
        "Ignoring '-'; a '-' may only appear as the first character in a character class"
      );
      ++position;
      continue;
    }

    result.push(c);
    ++position;
    if (c === CHARACTER_CLASS_END_SENTINEL) {
      break;
    }
  } while (position < length);

  if (
    (position < length && input[position] !== CHARACTER_CLASS_END_SENTINEL) ||
    (position == length && input[position - 1] == CHARACTER_CLASS_END_SENTINEL)
  ) {
    // Fix up result; we over consumed.
    result.pop();
    return [result, position];
  }

  if (position < length && input[position] == CHARACTER_CLASS_END_SENTINEL) {
    return [result, position + 1];
  }

  console.error("Found end-of-line instead of end of character class");
  return [null, position];
}

function _parsePasswordRequiredOrAllowedPropertyValue(input, position) {
  console.assert(position >= 0);
  console.assert(position < input.length);

  let length = input.length;
  let propertyValues = [];
  while (true) {
    if (_isIdentifierCharacter(input[position])) {
      let identifierStartPosition = position;
      var [propertyValue, position] = _parseIdentifier(input, position);
      if (!_isValidRequiredOrAllowedPropertyValueIdentifier(propertyValue)) {
        console.error(
          "Unrecognized property value identifier: " + propertyValue
        );
        return [null, identifierStartPosition];
      }
      propertyValues.push(new NamedCharacterClass(propertyValue));
    } else if (input[position] == CHARACTER_CLASS_START_SENTINEL) {
      var [propertyValue, position] = _parseCustomCharacterClass(
        input,
        position
      );
      if (propertyValue && propertyValue.length) {
        propertyValues.push(new CustomCharacterClass(propertyValue));
      }
    } else {
      console.error(
        "Failed to find start of property value: " + input.substr(position)
      );
      return [null, position];
    }

    position = _indexOfNonWhitespaceCharacter(input, position);
    if (position >= length || input[position] === PROPERTY_SEPARATOR) {
      break;
    }

    if (input[position] === PROPERTY_VALUE_SEPARATOR) {
      position = _indexOfNonWhitespaceCharacter(input, position + 1);
      if (position >= length) {
        console.error(
          "Found end-of-line instead of start of next property value"
        );
        return [null, position];
      }
      continue;
    }

    console.error(
      "Failed to find start of next property or property value: " +
        input.substr(position)
    );
    return [null, position];
  }
  return [propertyValues, position];
}

function _parsePasswordRule(input, position) {
  console.assert(position >= 0);
  console.assert(position < input.length);
  console.assert(_isIdentifierCharacter(input[position]));

  let length = input.length;

  let mayBeIdentifierStartPosition = position;
  var [identifier, position] = _parseIdentifier(input, position);
  if (!Object.values(RuleName).includes(identifier)) {
    console.error("Unrecognized property name: " + identifier);
    return [null, mayBeIdentifierStartPosition];
  }

  if (position >= length) {
    console.error("Found end-of-line instead of start of property value");
    return [null, position];
  }

  if (input[position] !== PROPERTY_VALUE_START_SENTINEL) {
    console.error(
      "Failed to find start of property value: " + input.substr(position)
    );
    return [null, position];
  }

  let property = { name: identifier, value: null };

  position = _indexOfNonWhitespaceCharacter(input, position + 1);
  // Empty value
  if (position >= length || input[position] === PROPERTY_SEPARATOR) {
    return [new Rule(property.name, property.value), position];
  }

  switch (identifier) {
    case RuleName.ALLOWED:
    case RuleName.REQUIRED: {
      var [
        propertyValue,
        position,
      ] = _parsePasswordRequiredOrAllowedPropertyValue(input, position);
      if (propertyValue) {
        property.value = propertyValue;
      }
      return [new Rule(property.name, property.value), position];
    }
    case RuleName.MAX_CONSECUTIVE: {
      var [propertyValue, position] = _parseMaxConsecutivePropertyValue(
        input,
        position
      );
      if (propertyValue) {
        property.value = propertyValue;
      }
      return [new Rule(property.name, property.value), position];
    }
    case RuleName.MIN_LENGTH:
    case RuleName.MAX_LENGTH: {
      var [propertyValue, position] = _parseMinLengthMaxLengthPropertyValue(
        input,
        position
      );
      if (propertyValue) {
        property.value = propertyValue;
      }
      return [new Rule(property.name, property.value), position];
    }
  }
  console.assert(false, SHOULD_NOT_BE_REACHED);
}

function _parseMinLengthMaxLengthPropertyValue(input, position) {
  return _parseInteger(input, position);
}

function _parseMaxConsecutivePropertyValue(input, position) {
  return _parseInteger(input, position);
}

function _parseInteger(input, position) {
  console.assert(position >= 0);
  console.assert(position < input.length);

  if (!_isASCIIDigit(input[position])) {
    console.error(
      "Failed to parse value of type integer; not a number: " +
        input.substr(position)
    );
    return [null, position];
  }

  let length = input.length;
  let initialPosition = position;
  let result = 0;
  do {
    result = 10 * result + parseInt(input[position], 10);
    ++position;
  } while (
    position < length &&
    input[position] !== PROPERTY_SEPARATOR &&
    _isASCIIDigit(input[position])
  );

  if (position >= length || input[position] === PROPERTY_SEPARATOR) {
    return [result, position];
  }

  console.error(
    "Failed to parse value of type integer; not a number: " +
      input.substr(initialPosition)
  );
  return [null, position];
}

function _parsePasswordRulesInternal(input) {
  let parsedProperties = [];
  let length = input.length;

  var position = _indexOfNonWhitespaceCharacter(input);
  while (position < length) {
    if (!_isIdentifierCharacter(input[position])) {
      console.warn(
        "Failed to find start of property: " + input.substr(position)
      );
      return parsedProperties;
    }

    var [parsedProperty, position] = _parsePasswordRule(input, position);
    if (parsedProperty && parsedProperty.value) {
      parsedProperties.push(parsedProperty);
    }

    position = _indexOfNonWhitespaceCharacter(input, position);
    if (position >= length) {
      break;
    }

    if (input[position] === PROPERTY_SEPARATOR) {
      position = _indexOfNonWhitespaceCharacter(input, position + 1);
      if (position >= length) {
        return parsedProperties;
      }

      continue;
    }

    console.error(
      "Failed to find start of next property: " + input.substr(position)
    );
    return null;
  }

  return parsedProperties;
}

function parsePasswordRules(input, formatRulesForMinifiedVersion) {
  let passwordRules = _parsePasswordRulesInternal(input) || [];

  // When formatting rules for minified version, we should keep the formatted rules
  // as similar to the input as possible. Avoid copying required rules to allowed rules.
  let suppressCopyingRequiredToAllowed = formatRulesForMinifiedVersion;

  let newPasswordRules = [];
  let newAllowedValues = [];
  let minimumMaximumConsecutiveCharacters = null;
  let maximumMinLength = 0;
  let minimumMaxLength = null;

  for (let rule of passwordRules) {
    switch (rule.name) {
      case RuleName.MAX_CONSECUTIVE:
        minimumMaximumConsecutiveCharacters = minimumMaximumConsecutiveCharacters
          ? Math.min(rule.value, minimumMaximumConsecutiveCharacters)
          : rule.value;
        break;

      case RuleName.MIN_LENGTH:
        maximumMinLength = Math.max(rule.value, maximumMinLength);
        break;

      case RuleName.MAX_LENGTH:
        minimumMaxLength = minimumMaxLength
          ? Math.min(rule.value, minimumMaxLength)
          : rule.value;
        break;

      case RuleName.REQUIRED:
        rule.value = _canonicalizedPropertyValues(
          rule.value,
          formatRulesForMinifiedVersion
        );
        newPasswordRules.push(rule);
        if (!suppressCopyingRequiredToAllowed) {
          newAllowedValues = newAllowedValues.concat(rule.value);
        }
        break;

      case RuleName.ALLOWED:
        newAllowedValues = newAllowedValues.concat(rule.value);
        break;
    }
  }

  newAllowedValues = _canonicalizedPropertyValues(
    newAllowedValues,
    suppressCopyingRequiredToAllowed
  );
  if (!suppressCopyingRequiredToAllowed && !newAllowedValues.length) {
    newAllowedValues = [new NamedCharacterClass(Identifier.ASCII_PRINTABLE)];
  }
  if (newAllowedValues.length) {
    newPasswordRules.push(new Rule(RuleName.ALLOWED, newAllowedValues));
  }

  if (minimumMaximumConsecutiveCharacters !== null) {
    newPasswordRules.push(
      new Rule(RuleName.MAX_CONSECUTIVE, minimumMaximumConsecutiveCharacters)
    );
  }

  if (maximumMinLength > 0) {
    newPasswordRules.push(new Rule(RuleName.MIN_LENGTH, maximumMinLength));
  }

  if (minimumMaxLength !== null) {
    newPasswordRules.push(new Rule(RuleName.MAX_LENGTH, minimumMaxLength));
  }

  return newPasswordRules;
}

/**
 * Transforms the parsed rules returned from PasswordRulesParser into a Map for easier access.
 * The returned Map could have the following keys: "allowed", "required", "maxlength", "minlength", and "max-consecutive"
 *
 * @example
 * // Returns a Map with a key-value pair of "allowed": "ascii-printable"
 * transformRulesToMap([{ _name: "allowed", value: [{ _name: "ascii-printable" }] }])
 * @param {object[]} rules rules from PasswordRulesParser.parsePasswordRules
 * @return {Map} mapped rules
 */
function transformRulesToMap(rules) {
  let map = new Map();
  for (let rule of rules) {
    let { _name, value } = rule;
    if (
      _name === "minlength" ||
      _name === "maxlength" ||
      _name === "max-consecutive"
    ) {
      map.set(_name, value);
    } else {
      let _value = [];
      if (map.get(_name)) {
        _value = map.get(_name);
      }
      for (let _class of value) {
        let { _name: _className } = _class;
        if (_className) {
          _value.push(_className);
        } else {
          let { _characters } = _class;
          _value.push(_characters);
        }
      }
      map.set(_name, _value);
    }
  }
  return map;
}

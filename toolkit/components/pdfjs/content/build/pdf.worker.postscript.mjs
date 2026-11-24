/**
 * @licstart The following is the entire license notice for the
 * JavaScript code in this page
 *
 * Copyright 2024 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * @licend The above is the entire license notice for the
 * JavaScript code in this page
 */

/**
 * pdfjsVersion = 7.0.229
 * pdfjsBuild = 32fb05b3b
 */
export const __webpack_esm_ids__ = ["postscript"];
export const __webpack_esm_modules__ = {

/***/ 6737
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {


// EXPORTS
__webpack_require__.d(__webpack_exports__, {
  buildPostScriptFunction: () => (/* binding */ buildPostScriptFunction)
});

// EXTERNAL MODULE: ./src/shared/util.js
var util = __webpack_require__(4292);
;// ./src/core/postscript/lexer.js
const TOKEN = {
  number: 0,
  lbrace: 1,
  rbrace: 2,
  true: 3,
  false: 4,
  add: 5,
  sub: 6,
  mul: 7,
  div: 8,
  idiv: 9,
  mod: 10,
  exp: 11,
  eq: 12,
  ne: 13,
  gt: 14,
  ge: 15,
  lt: 16,
  le: 17,
  and: 18,
  or: 19,
  xor: 20,
  bitshift: 21,
  abs: 22,
  neg: 23,
  ceiling: 24,
  floor: 25,
  round: 26,
  truncate: 27,
  not: 28,
  sqrt: 29,
  sin: 30,
  cos: 31,
  ln: 32,
  log: 33,
  atan: 34,
  cvi: 35,
  cvr: 36,
  dup: 37,
  exch: 38,
  pop: 39,
  copy: 40,
  index: 41,
  roll: 42,
  if: 43,
  ifelse: 44,
  eof: 45,
  min: 46,
  max: 47
};
class Token {
  constructor(id, value = null) {
    this.id = id;
    this.value = value;
  }
}
class Lexer {
  static #singletons = null;
  static #operatorSingletons = null;
  static #initSingletons() {
    const singletons = Object.create(null);
    const operatorSingletons = Object.create(null);
    for (const [name, id] of Object.entries(TOKEN)) {
      if (name === "number") {
        continue;
      }
      const isOperator = id >= TOKEN.true && id <= TOKEN.ifelse;
      const token = new Token(id, isOperator ? name : null);
      singletons[name] = token;
      if (isOperator) {
        operatorSingletons[name] = token;
      }
    }
    this.#singletons = singletons;
    this.#operatorSingletons = operatorSingletons;
  }
  constructor(data) {
    if (!Lexer.#singletons) {
      Lexer.#initSingletons();
    }
    this.data = data;
    this.pos = 0;
    this.len = data.length;
    this._numberPattern = /[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?/iy;
    this._identifierPattern = /[a-z]+/y;
  }
  _skipComment() {
    const lf = this.data.indexOf("\n", this.pos);
    const cr = this.data.indexOf("\r", this.pos);
    const eol = Math.min(lf < 0 ? this.len : lf, cr < 0 ? this.len : cr);
    this.pos = Math.min(eol + 1, this.len);
  }
  _getNumber() {
    this._numberPattern.lastIndex = this.pos;
    const match = this._numberPattern.exec(this.data);
    if (!match) {
      this.pos++;
      return new Token(TOKEN.number, 0);
    }
    const number = parseFloat(match[0]);
    this.pos = this._numberPattern.lastIndex;
    return new Token(TOKEN.number, Number.isFinite(number) ? number : 0);
  }
  _getOperator() {
    this._identifierPattern.lastIndex = this.pos;
    const match = this._identifierPattern.exec(this.data);
    if (!match) {
      this.pos++;
      return new Token(TOKEN.number, 0);
    }
    this.pos = this._identifierPattern.lastIndex;
    const op = match[0];
    const token = Lexer.#operatorSingletons[op];
    return token ?? new Token(TOKEN.number, 0);
  }
  next() {
    while (this.pos < this.len) {
      const ch = this.data.charCodeAt(this.pos++);
      switch (ch) {
        case 0x00:
        case 0x09:
        case 0x0a:
        case 0x0c:
        case 0x0d:
        case 0x20:
          break;
        case 0x25:
          this._skipComment();
          break;
        case 0x7b:
          return Lexer.#singletons.lbrace;
        case 0x7d:
          return Lexer.#singletons.rbrace;
        case 0x2b:
        case 0x2d:
        case 0x2e:
          this.pos--;
          return this._getNumber();
        default:
          if (ch >= 0x30 && ch <= 0x39) {
            this.pos--;
            return this._getNumber();
          }
          if (ch >= 0x61 && ch <= 0x7a) {
            this.pos--;
            return this._getOperator();
          }
          return new Token(TOKEN.number, 0);
      }
    }
    return Lexer.#singletons.eof;
  }
}

;// ./src/core/postscript/ast.js


const PS_VALUE_TYPE = {
  numeric: 0,
  boolean: 1,
  unknown: 2
};
const PS_NODE = {
  program: 0,
  block: 1,
  number: 2,
  operator: 3,
  if: 4,
  ifelse: 5,
  arg: 6,
  const: 7,
  unary: 8,
  binary: 9,
  ternary: 10
};
class PsNode {
  constructor(type) {
    this.type = type;
  }
}
class PsProgram extends PsNode {
  constructor(body) {
    super(PS_NODE.program);
    this.body = body;
  }
}
class PsBlock extends PsNode {
  constructor(instructions) {
    super(PS_NODE.block);
    this.instructions = instructions;
  }
}
class PsNumber extends PsNode {
  constructor(value) {
    super(PS_NODE.number);
    this.value = value;
  }
}
class PsOperator extends PsNode {
  constructor(op) {
    super(PS_NODE.operator);
    this.op = op;
  }
}
class PsIf extends PsNode {
  constructor(then) {
    super(PS_NODE.if);
    this.then = then;
  }
}
class PsIfElse extends PsNode {
  constructor(then, otherwise) {
    super(PS_NODE.ifelse);
    this.then = then;
    this.otherwise = otherwise;
  }
}
class PsArgNode extends PsNode {
  constructor(index) {
    super(PS_NODE.arg);
    this.index = index;
    this.valueType = PS_VALUE_TYPE.numeric;
    this.depth = 0;
  }
}
class PsConstNode extends PsNode {
  constructor(value) {
    super(PS_NODE.const);
    this.value = value;
    this.valueType = typeof value === "boolean" ? PS_VALUE_TYPE.boolean : PS_VALUE_TYPE.numeric;
    this.depth = 0;
  }
}
class PsUnaryNode extends PsNode {
  constructor(op, operand, valueType = PS_VALUE_TYPE.unknown) {
    super(PS_NODE.unary);
    this.op = op;
    this.operand = operand;
    this.valueType = valueType;
    this.depth = operand.depth + 1;
  }
}
class PsBinaryNode extends PsNode {
  constructor(op, first, second, valueType = PS_VALUE_TYPE.unknown) {
    super(PS_NODE.binary);
    this.op = op;
    this.first = first;
    this.second = second;
    this.valueType = valueType;
    this.depth = Math.max(first.depth, second.depth) + 1;
  }
}
class PsTernaryNode extends PsNode {
  constructor(cond, then, otherwise, valueType = PS_VALUE_TYPE.unknown) {
    super(PS_NODE.ternary);
    this.cond = cond;
    this.then = then;
    this.otherwise = otherwise;
    this.valueType = valueType;
    this.depth = Math.max(cond.depth, then.depth, otherwise.depth) + 1;
  }
}
const MAX_NESTING_DEPTH = 1000;
class Parser {
  constructor(lexer) {
    this.lexer = lexer;
    this._token = null;
    this._depth = 0;
  }
  static _isRegularOperator(id) {
    return id >= TOKEN.true && id < TOKEN.if;
  }
  _advance() {
    this._token = this.lexer.next();
  }
  _expect(id) {
    if (this._token.id !== id) {
      throw new util.FormatError(`PostScript function: expected token id ${id}, got ${this._token.id}.`);
    }
    const tok = this._token;
    this._advance();
    return tok;
  }
  parse() {
    this._advance();
    this._expect(TOKEN.lbrace);
    const block = this._parseBlock();
    this._expect(TOKEN.rbrace);
    if (this._token.id !== TOKEN.eof) {
      (0,util.warn)("PostScript function: unexpected content after closing brace.");
    }
    return new PsProgram(block);
  }
  _parseBlock() {
    const instructions = [];
    while (true) {
      const tok = this._token;
      switch (tok.id) {
        case TOKEN.number:
          instructions.push(new PsNumber(tok.value));
          this._advance();
          break;
        case TOKEN.lbrace:
          {
            if (++this._depth > MAX_NESTING_DEPTH) {
              throw new util.FormatError("PostScript function: procedures are nested too deeply.");
            }
            this._advance();
            const thenBlock = this._parseBlock();
            this._expect(TOKEN.rbrace);
            if (this._token.id === TOKEN.if) {
              this._advance();
              instructions.push(new PsIf(thenBlock));
            } else if (this._token.id === TOKEN.lbrace) {
              this._advance();
              const elseBlock = this._parseBlock();
              this._expect(TOKEN.rbrace);
              this._expect(TOKEN.ifelse);
              instructions.push(new PsIfElse(thenBlock, elseBlock));
            } else {
              throw new util.FormatError("PostScript function: a procedure block must be followed by 'if' or '{…} ifelse'.");
            }
            this._depth--;
            break;
          }
        case TOKEN.rbrace:
        case TOKEN.eof:
          return new PsBlock(instructions);
        case TOKEN.if:
        case TOKEN.ifelse:
          throw new util.FormatError(`PostScript function: unexpected '${tok.value}' operator.`);
        default:
          if (Parser._isRegularOperator(tok.id)) {
            instructions.push(new PsOperator(tok.id));
            this._advance();
            break;
          }
          throw new util.FormatError(`PostScript function: unexpected token id ${tok.id}.`);
      }
    }
  }
}
function parsePostScriptFunction(source) {
  return new Parser(new Lexer(source)).parse();
}
function _evalBinaryConst(op, a, b) {
  switch (op) {
    case TOKEN.add:
      return a + b;
    case TOKEN.sub:
      return a - b;
    case TOKEN.mul:
      return a * b;
    case TOKEN.div:
      return b !== 0 ? a / b : 0;
    case TOKEN.idiv:
      return b !== 0 ? Math.trunc(a / b) : 0;
    case TOKEN.mod:
      return b !== 0 ? a - Math.trunc(a / b) * b : 0;
    case TOKEN.exp:
      {
        const r = a ** b;
        return Number.isFinite(r) ? r : undefined;
      }
    case TOKEN.atan:
      {
        let deg = Math.atan2(a, b) * (180 / Math.PI);
        if (deg < 0) {
          deg += 360;
        }
        return deg;
      }
    case TOKEN.eq:
      return a === b;
    case TOKEN.ne:
      return a !== b;
    case TOKEN.gt:
      return a > b;
    case TOKEN.ge:
      return a >= b;
    case TOKEN.lt:
      return a < b;
    case TOKEN.le:
      return a <= b;
    case TOKEN.and:
      return typeof a === "boolean" ? a && b : a & b | 0;
    case TOKEN.or:
      return typeof a === "boolean" ? a || b : a | b | 0;
    case TOKEN.xor:
      return typeof a === "boolean" ? a !== b : a ^ b | 0;
    case TOKEN.bitshift:
      return b >= 0 ? a << b | 0 : a >> -b | 0;
    case TOKEN.min:
      return Math.min(a, b);
    case TOKEN.max:
      return Math.max(a, b);
    default:
      return undefined;
  }
}
function _evalUnaryConst(op, v) {
  switch (op) {
    case TOKEN.abs:
      return Math.abs(v);
    case TOKEN.neg:
      return -v;
    case TOKEN.ceiling:
      return Math.ceil(v);
    case TOKEN.floor:
      return Math.floor(v);
    case TOKEN.round:
      return Math.round(v);
    case TOKEN.truncate:
      return Math.trunc(v);
    case TOKEN.sqrt:
      {
        const r = Math.sqrt(v);
        return Number.isFinite(r) ? r : undefined;
      }
    case TOKEN.sin:
      return Math.sin(v % 360 * Math.PI / 180);
    case TOKEN.cos:
      return Math.cos(v % 360 * Math.PI / 180);
    case TOKEN.ln:
      {
        const r = Math.log(v);
        return Number.isFinite(r) ? r : undefined;
      }
    case TOKEN.log:
      {
        const r = Math.log10(v);
        return Number.isFinite(r) ? r : undefined;
      }
    case TOKEN.cvi:
      return Math.trunc(v) | 0;
    case TOKEN.cvr:
      return v;
    case TOKEN.not:
      return typeof v === "boolean" ? !v : ~v;
    default:
      return undefined;
  }
}
const MAX_STACK_SIZE = 100;
const MAX_WORK = 100_000;
const MAX_FORK_DEPTH = 1000;
const MAX_TREE_DEPTH = 500;
function _unaryValueType(op, operandType) {
  return op === TOKEN.not ? operandType : PS_VALUE_TYPE.numeric;
}
function _binaryValueType(op, firstType, secondType) {
  switch (op) {
    case TOKEN.eq:
    case TOKEN.ne:
    case TOKEN.gt:
    case TOKEN.ge:
    case TOKEN.lt:
    case TOKEN.le:
      return PS_VALUE_TYPE.boolean;
    case TOKEN.and:
    case TOKEN.or:
    case TOKEN.xor:
      return firstType === secondType && firstType !== PS_VALUE_TYPE.unknown ? firstType : PS_VALUE_TYPE.unknown;
    default:
      return PS_VALUE_TYPE.numeric;
  }
}
class PSStackToTree {
  static #binaryOps = null;
  static #unaryOps = null;
  static #idempotentUnary = null;
  static #negatedComparison = null;
  static #init() {
    this.#binaryOps = new Set([TOKEN.add, TOKEN.sub, TOKEN.mul, TOKEN.div, TOKEN.idiv, TOKEN.mod, TOKEN.exp, TOKEN.atan, TOKEN.eq, TOKEN.ne, TOKEN.gt, TOKEN.ge, TOKEN.lt, TOKEN.le, TOKEN.and, TOKEN.or, TOKEN.xor, TOKEN.bitshift]);
    this.#unaryOps = new Set([TOKEN.abs, TOKEN.neg, TOKEN.ceiling, TOKEN.floor, TOKEN.round, TOKEN.truncate, TOKEN.sqrt, TOKEN.sin, TOKEN.cos, TOKEN.ln, TOKEN.log, TOKEN.cvi, TOKEN.cvr, TOKEN.not]);
    this.#idempotentUnary = new Set([TOKEN.abs, TOKEN.ceiling, TOKEN.cvi, TOKEN.cvr, TOKEN.floor, TOKEN.round, TOKEN.truncate]);
    this.#negatedComparison = new Map([[TOKEN.eq, TOKEN.ne], [TOKEN.ne, TOKEN.eq], [TOKEN.lt, TOKEN.ge], [TOKEN.le, TOKEN.gt], [TOKEN.gt, TOKEN.le], [TOKEN.ge, TOKEN.lt]]);
  }
  evaluate(program, numInputs, numOutputs) {
    if (!PSStackToTree.#binaryOps) {
      PSStackToTree.#init();
    }
    this._failed = false;
    this._work = 0;
    this._forkDepth = 0;
    if (numInputs > MAX_STACK_SIZE) {
      return null;
    }
    const stack = [];
    for (let i = 0; i < numInputs; i++) {
      stack.push(new PsArgNode(i));
    }
    this._evalBlock(program.body, stack);
    if (this._failed) {
      return null;
    }
    if (numOutputs !== undefined) {
      if (stack.length < numOutputs) {
        return null;
      }
      stack.splice(0, stack.length - numOutputs);
    }
    PSStackToTree.#markShared(stack);
    return stack;
  }
  static #markShared(outputs) {
    const refCount = new Map();
    const visit = node => {
      if (!node || node.type === PS_NODE.arg || node.type === PS_NODE.const) {
        return;
      }
      const prev = refCount.get(node) ?? 0;
      refCount.set(node, prev + 1);
      if (prev > 0) {
        return;
      }
      switch (node.type) {
        case PS_NODE.unary:
          visit(node.operand);
          break;
        case PS_NODE.binary:
          visit(node.first);
          visit(node.second);
          break;
        case PS_NODE.ternary:
          visit(node.cond);
          visit(node.then);
          visit(node.otherwise);
          break;
      }
    };
    for (const output of outputs) {
      visit(output);
    }
    for (const [node, count] of refCount) {
      if (count > 1) {
        node.shared = true;
        node.sharedCount = count;
      }
    }
  }
  _evalBlock(block, stack) {
    this._evalBlockFrom(block.instructions, 0, stack);
  }
  _evalBlockFrom(instructions, startIdx, stack) {
    for (let idx = startIdx; idx < instructions.length; idx++) {
      if (this._failed || !this._checkWorkLimit()) {
        break;
      }
      const instr = instructions[idx];
      switch (instr.type) {
        case PS_NODE.number:
          stack.push(new PsConstNode(instr.value));
          if (stack.length > MAX_STACK_SIZE) {
            this._failed = true;
          }
          break;
        case PS_NODE.operator:
          this._evalOp(instr.op, stack);
          break;
        case PS_NODE.if:
          {
            if (stack.length < 1) {
              this._failed = true;
              break;
            }
            const cond = stack.pop();
            const saved = stack.slice();
            this._evalBlock(instr.then, stack);
            if (this._failed) {
              break;
            }
            if (stack.length === saved.length) {
              for (let i = 0; i < stack.length; i++) {
                if (stack[i] !== saved[i]) {
                  stack[i] = this._makeTernary(cond, stack[i], saved[i]);
                }
              }
            } else if (stack.length > saved.length) {
              if (cond.type === PS_NODE.const) {
                if (!cond.value) {
                  stack.length = 0;
                  stack.push(...saved);
                }
                break;
              }
              if (this._forkDepth >= MAX_FORK_DEPTH) {
                this._failed = true;
                break;
              }
              this._forkDepth++;
              const trueStack = stack.slice();
              this._evalBlockFrom(instructions, idx + 1, trueStack);
              if (this._failed) {
                break;
              }
              const falseStack = saved;
              this._evalBlockFrom(instructions, idx + 1, falseStack);
              if (this._failed) {
                break;
              }
              this._forkDepth--;
              if (trueStack.length !== falseStack.length) {
                const zero = new PsConstNode(0);
                while (trueStack.length < falseStack.length) {
                  trueStack.unshift(zero);
                }
                while (falseStack.length < trueStack.length) {
                  falseStack.unshift(zero);
                }
              }
              stack.length = 0;
              for (let i = 0; i < trueStack.length; i++) {
                stack.push(this._makeTernary(cond, trueStack[i], falseStack[i]));
              }
              return;
            } else {
              this._failed = true;
            }
            break;
          }
        case PS_NODE.ifelse:
          {
            if (stack.length < 1) {
              this._failed = true;
              break;
            }
            const cond = stack.pop();
            const snapshot = stack.slice();
            const thenStack = snapshot.slice();
            this._evalBlock(instr.then, thenStack);
            if (this._failed) {
              break;
            }
            const elseStack = snapshot.slice();
            this._evalBlock(instr.otherwise, elseStack);
            if (this._failed) {
              break;
            }
            if (thenStack.length !== elseStack.length) {
              const zero = new PsConstNode(0);
              while (thenStack.length < elseStack.length) {
                thenStack.unshift(zero);
              }
              while (elseStack.length < thenStack.length) {
                elseStack.unshift(zero);
              }
            }
            stack.length = 0;
            for (let i = 0; i < thenStack.length; i++) {
              stack.push(this._makeTernary(cond, thenStack[i], elseStack[i]));
            }
            break;
          }
      }
    }
  }
  _evalOp(op, stack) {
    if (PSStackToTree.#binaryOps.has(op)) {
      if (stack.length < 2) {
        this._failed = true;
        return;
      }
      const first = stack.pop();
      const second = stack.pop();
      stack.push(this._makeBinary(op, first, second));
      return;
    }
    if (PSStackToTree.#unaryOps.has(op)) {
      if (stack.length < 1) {
        this._failed = true;
        return;
      }
      stack.push(this._makeUnary(op, stack.pop()));
      return;
    }
    switch (op) {
      case TOKEN.true:
        stack.push(new PsConstNode(true));
        if (stack.length > MAX_STACK_SIZE) {
          this._failed = true;
        }
        break;
      case TOKEN.false:
        stack.push(new PsConstNode(false));
        if (stack.length > MAX_STACK_SIZE) {
          this._failed = true;
        }
        break;
      case TOKEN.dup:
        if (stack.length < 1) {
          this._failed = true;
          break;
        }
        stack.push(stack.at(-1));
        if (stack.length > MAX_STACK_SIZE) {
          this._failed = true;
        }
        break;
      case TOKEN.exch:
        {
          if (stack.length < 2) {
            this._failed = true;
            break;
          }
          const a = stack.pop();
          const b = stack.pop();
          stack.push(a, b);
          break;
        }
      case TOKEN.pop:
        if (stack.length < 1) {
          this._failed = true;
          break;
        }
        stack.pop();
        break;
      case TOKEN.copy:
        {
          if (stack.length < 1) {
            this._failed = true;
            break;
          }
          const nNode = stack.pop();
          if (nNode.type === PS_NODE.const) {
            const n = nNode.value | 0;
            if (n === 0) {} else if (n < 0 || n > stack.length) {
              this._failed = true;
            } else {
              stack.push(...stack.slice(-n));
              if (stack.length > MAX_STACK_SIZE) {
                this._failed = true;
              }
            }
          } else {
            this._failed = true;
          }
          break;
        }
      case TOKEN.index:
        {
          if (stack.length < 1) {
            this._failed = true;
            break;
          }
          const nNode = stack.pop();
          if (nNode.type === PS_NODE.const) {
            const n = nNode.value | 0;
            if (n < 0 || n >= stack.length) {
              this._failed = true;
            } else {
              stack.push(stack.at(-n - 1));
            }
          } else {
            this._failed = true;
          }
          break;
        }
      case TOKEN.roll:
        {
          if (stack.length < 2) {
            this._failed = true;
            break;
          }
          const jNode = stack.pop();
          const nNode = stack.pop();
          if (nNode.type === PS_NODE.const && jNode.type === PS_NODE.const) {
            const n = nNode.value | 0;
            if (n === 0) {} else if (n < 0 || n > stack.length) {
              this._failed = true;
            } else {
              const j = ((jNode.value | 0) % n + n) % n;
              if (j > 0) {
                const slice = stack.splice(-n, n);
                stack.push(...slice.slice(n - j), ...slice.slice(0, n - j));
              }
            }
          } else {
            this._failed = true;
          }
          break;
        }
      default:
        this._failed = true;
        break;
    }
  }
  _checkWorkLimit() {
    if (++this._work > MAX_WORK) {
      this._failed = true;
      return false;
    }
    return true;
  }
  _checkDepth(node) {
    if (node.depth > MAX_TREE_DEPTH) {
      this._failed = true;
    }
    return node;
  }
  _nodesEqual(a, b) {
    if (a === b) {
      return true;
    }
    if (a.type !== b.type || !this._checkWorkLimit()) {
      return false;
    }
    switch (a.type) {
      case PS_NODE.arg:
        return a.index === b.index;
      case PS_NODE.const:
        return a.value === b.value;
      case PS_NODE.unary:
        return a.op === b.op && this._nodesEqual(a.operand, b.operand);
      case PS_NODE.binary:
        return a.op === b.op && this._nodesEqual(a.first, b.first) && this._nodesEqual(a.second, b.second);
      case PS_NODE.ternary:
        return this._nodesEqual(a.cond, b.cond) && this._nodesEqual(a.then, b.then) && this._nodesEqual(a.otherwise, b.otherwise);
      default:
        return false;
    }
  }
  _makeBinary(op, first, second) {
    if (first.type === PS_NODE.const && second.type === PS_NODE.const) {
      const v = _evalBinaryConst(op, second.value, first.value);
      if (v !== undefined) {
        return new PsConstNode(v);
      }
    }
    if (this._nodesEqual(first, second)) {
      switch (op) {
        case TOKEN.sub:
          return new PsConstNode(0);
        case TOKEN.xor:
          return new PsConstNode(first.valueType === PS_VALUE_TYPE.boolean ? false : 0);
        case TOKEN.and:
        case TOKEN.or:
          return first;
        case TOKEN.min:
        case TOKEN.max:
          return first;
        case TOKEN.eq:
        case TOKEN.ge:
        case TOKEN.le:
          return new PsConstNode(true);
        case TOKEN.ne:
        case TOKEN.gt:
        case TOKEN.lt:
          return new PsConstNode(false);
      }
    }
    if (op === TOKEN.eq || op === TOKEN.ne) {
      if (first.valueType === PS_VALUE_TYPE.unknown || second.valueType === PS_VALUE_TYPE.unknown) {
        this._failed = true;
      } else if (first.valueType !== second.valueType) {
        return new PsConstNode(op === TOKEN.ne);
      }
    }
    if (first.type === PS_NODE.const) {
      const b = first.value;
      switch (op) {
        case TOKEN.add:
          if (b === 0) {
            return second;
          }
          break;
        case TOKEN.sub:
          if (b === 0) {
            return second;
          }
          break;
        case TOKEN.mul:
          if (b === 1) {
            return second;
          }
          if (b === 0) {
            return first;
          }
          if (b === -1) {
            return this._makeUnary(TOKEN.neg, second);
          }
          break;
        case TOKEN.div:
          if (b !== 0) {
            return this._makeBinary(TOKEN.mul, new PsConstNode(1 / b), second);
          }
          break;
        case TOKEN.idiv:
          if (b === 1) {
            return this._makeUnary(TOKEN.truncate, second);
          }
          break;
        case TOKEN.exp:
          if (b === 1) {
            return second;
          }
          if (b === 0.5) {
            return this._makeUnary(TOKEN.sqrt, second);
          }
          if (b === 0.25) {
            const sqrtOnce = this._makeUnary(TOKEN.sqrt, second);
            return this._makeUnary(TOKEN.sqrt, sqrtOnce);
          }
          if (b === 2) {
            return this._makeBinary(TOKEN.mul, second, second);
          }
          if (b === 3) {
            return this._makeBinary(TOKEN.mul, this._makeBinary(TOKEN.mul, second, second), second);
          }
          if (b === 4) {
            const square = this._makeBinary(TOKEN.mul, second, second);
            return this._makeBinary(TOKEN.mul, square, square);
          }
          if (b === 0) {
            return new PsConstNode(1);
          }
          break;
        case TOKEN.and:
          if (b === true) {
            return second;
          }
          if (b === false) {
            return first;
          }
          break;
        case TOKEN.or:
          if (b === false) {
            return second;
          }
          if (b === true) {
            return first;
          }
          break;
        case TOKEN.min:
          if (second.type === PS_NODE.binary && second.op === TOKEN.max && second.first.type === PS_NODE.const && second.first.value >= b) {
            return first;
          }
          break;
        case TOKEN.max:
          if (second.type === PS_NODE.binary && second.op === TOKEN.min && second.first.type === PS_NODE.const && second.first.value <= b) {
            return first;
          }
          break;
      }
    }
    if (second.type === PS_NODE.const) {
      const a = second.value;
      switch (op) {
        case TOKEN.add:
          if (a === 0) {
            return first;
          }
          break;
        case TOKEN.sub:
          if (a === 0) {
            return this._makeUnary(TOKEN.neg, first);
          }
          break;
        case TOKEN.mul:
          if (a === 1) {
            return first;
          }
          if (a === 0) {
            return second;
          }
          if (a === -1) {
            return this._makeUnary(TOKEN.neg, first);
          }
          break;
        case TOKEN.and:
          if (a === true) {
            return first;
          }
          if (a === false) {
            return second;
          }
          break;
        case TOKEN.or:
          if (a === false) {
            return first;
          }
          if (a === true) {
            return second;
          }
          break;
      }
    }
    return this._checkDepth(new PsBinaryNode(op, first, second, _binaryValueType(op, first.valueType, second.valueType)));
  }
  _makeUnary(op, operand) {
    if (operand.type === PS_NODE.const) {
      const v = _evalUnaryConst(op, operand.value);
      if (v !== undefined) {
        return new PsConstNode(v);
      }
    }
    if (op === TOKEN.not && operand.type === PS_NODE.binary) {
      const negated = PSStackToTree.#negatedComparison.get(operand.op);
      if (negated !== undefined) {
        return new PsBinaryNode(negated, operand.first, operand.second, PS_VALUE_TYPE.boolean);
      }
    }
    if (op === TOKEN.neg && operand.type === PS_NODE.binary && operand.op === TOKEN.sub) {
      return this._makeBinary(TOKEN.sub, operand.second, operand.first);
    }
    if (operand.type === PS_NODE.unary) {
      if (op === TOKEN.neg && operand.op === TOKEN.neg || op === TOKEN.not && operand.op === TOKEN.not) {
        return operand.operand;
      }
      if (op === TOKEN.abs && operand.op === TOKEN.neg) {
        return this._makeUnary(TOKEN.abs, operand.operand);
      }
      if (PSStackToTree.#idempotentUnary.has(op) && op === operand.op) {
        return operand;
      }
    }
    return this._checkDepth(new PsUnaryNode(op, operand, _unaryValueType(op, operand.valueType)));
  }
  _makeTernary(cond, then, otherwise) {
    if (cond.type === PS_NODE.const) {
      return cond.value ? then : otherwise;
    }
    if (this._nodesEqual(then, otherwise)) {
      return then;
    }
    if (then.type === PS_NODE.const && otherwise.type === PS_NODE.const) {
      if (then.value === true && otherwise.value === false) {
        return cond;
      }
      if (then.value === false && otherwise.value === true) {
        return this._makeUnary(TOKEN.not, cond);
      }
    }
    if (cond.type === PS_NODE.binary) {
      const {
        op: cop,
        first: cf,
        second: cs
      } = cond;
      if (cop === TOKEN.gt || cop === TOKEN.ge) {
        if (this._nodesEqual(then, cf) && this._nodesEqual(otherwise, cs)) {
          return this._makeBinary(TOKEN.min, cf, cs);
        }
        if (this._nodesEqual(then, cs) && this._nodesEqual(otherwise, cf)) {
          return this._makeBinary(TOKEN.max, cf, cs);
        }
      } else if (cop === TOKEN.lt || cop === TOKEN.le) {
        if (this._nodesEqual(then, cf) && this._nodesEqual(otherwise, cs)) {
          return this._makeBinary(TOKEN.max, cf, cs);
        }
        if (this._nodesEqual(then, cs) && this._nodesEqual(otherwise, cf)) {
          return this._makeBinary(TOKEN.min, cf, cs);
        }
      }
    }
    return this._checkDepth(new PsTernaryNode(cond, then, otherwise, then.valueType === otherwise.valueType ? then.valueType : PS_VALUE_TYPE.unknown));
  }
}

// EXTERNAL MODULE: ./src/shared/math_clamp.js
var math_clamp = /*#__PURE__*/__webpack_require__(4566);
;// ./src/core/postscript/js_evaluator.js



const OP = {
  ARG: 0,
  CONST: 1,
  STORE: 2,
  IF: 3,
  JUMP: 4,
  ABS: 5,
  NEG: 6,
  CEIL: 7,
  FLOOR: 8,
  ROUND: 9,
  TRUNC: 10,
  NOT_B: 11,
  NOT_N: 12,
  SQRT: 13,
  SIN: 14,
  COS: 15,
  LN: 16,
  LOG10: 17,
  CVI: 18,
  SHIFT: 19,
  ADD: 20,
  SUB: 21,
  MUL: 22,
  DIV: 23,
  IDIV: 24,
  MOD: 25,
  POW: 26,
  EQ: 27,
  NE: 28,
  GT: 29,
  GE: 30,
  LT: 31,
  LE: 32,
  AND: 33,
  OR: 34,
  XOR: 35,
  ATAN: 36,
  MIN: 37,
  MAX: 38,
  TEE_TMP: 39,
  LOAD_TMP: 40
};
const _DEG_TO_RAD = Math.PI / 180;
const _RAD_TO_DEG = 180 / Math.PI;
class PsJsCompiler {
  static #stack = new Float64Array(64);
  static #tmp = new Float64Array(64);
  constructor(domain, range) {
    this.nIn = domain.length >> 1;
    this.nOut = range.length >> 1;
    this.range = range;
    this.ir = [];
    this._tmpMap = new Map();
    this._nextTmp = 0;
  }
  _compileNode(node) {
    if (node.shared) {
      const cached = this._tmpMap.get(node);
      if (cached !== undefined) {
        this.ir.push(OP.LOAD_TMP, cached);
        return true;
      }
      if (!this._compileNodeImpl(node)) {
        return false;
      }
      const slot = this._nextTmp++;
      this._tmpMap.set(node, slot);
      this.ir.push(OP.TEE_TMP, slot);
      return true;
    }
    return this._compileNodeImpl(node);
  }
  _compileNodeImpl(node) {
    switch (node.type) {
      case PS_NODE.arg:
        this.ir.push(OP.ARG, node.index);
        return true;
      case PS_NODE.const:
        {
          const v = node.value;
          this.ir.push(OP.CONST, typeof v === "boolean" ? Number(v) : v);
          return true;
        }
      case PS_NODE.unary:
        return this._compileUnary(node);
      case PS_NODE.binary:
        return this._compileBinary(node);
      case PS_NODE.ternary:
        return this._compileTernary(node);
      default:
        return false;
    }
  }
  _compileUnary(node) {
    const {
      op,
      operand,
      valueType
    } = node;
    if (op === TOKEN.cvr) {
      return this._compileNode(operand);
    }
    if (!this._compileNode(operand)) {
      return false;
    }
    switch (op) {
      case TOKEN.abs:
        this.ir.push(OP.ABS);
        break;
      case TOKEN.neg:
        this.ir.push(OP.NEG);
        break;
      case TOKEN.ceiling:
        this.ir.push(OP.CEIL);
        break;
      case TOKEN.floor:
        this.ir.push(OP.FLOOR);
        break;
      case TOKEN.round:
        this.ir.push(OP.ROUND);
        break;
      case TOKEN.truncate:
        this.ir.push(OP.TRUNC);
        break;
      case TOKEN.sqrt:
        this.ir.push(OP.SQRT);
        break;
      case TOKEN.sin:
        this.ir.push(OP.SIN);
        break;
      case TOKEN.cos:
        this.ir.push(OP.COS);
        break;
      case TOKEN.ln:
        this.ir.push(OP.LN);
        break;
      case TOKEN.log:
        this.ir.push(OP.LOG10);
        break;
      case TOKEN.cvi:
        this.ir.push(OP.CVI);
        break;
      case TOKEN.not:
        if (valueType === PS_VALUE_TYPE.boolean) {
          this.ir.push(OP.NOT_B);
        } else if (valueType === PS_VALUE_TYPE.numeric) {
          this.ir.push(OP.NOT_N);
        } else {
          return false;
        }
        break;
      default:
        return false;
    }
    return true;
  }
  _compileBinary(node) {
    const {
      op,
      first,
      second
    } = node;
    if (op === TOKEN.bitshift) {
      if (first.type !== PS_NODE.const || !Number.isInteger(first.value) || !this._compileNode(second)) {
        return false;
      }
      this.ir.push(OP.SHIFT, first.value);
      return true;
    }
    if (!this._compileNode(second) || !this._compileNode(first)) {
      return false;
    }
    switch (op) {
      case TOKEN.add:
        this.ir.push(OP.ADD);
        break;
      case TOKEN.sub:
        this.ir.push(OP.SUB);
        break;
      case TOKEN.mul:
        this.ir.push(OP.MUL);
        break;
      case TOKEN.div:
        this.ir.push(OP.DIV);
        break;
      case TOKEN.idiv:
        this.ir.push(OP.IDIV);
        break;
      case TOKEN.mod:
        this.ir.push(OP.MOD);
        break;
      case TOKEN.exp:
        this.ir.push(OP.POW);
        break;
      case TOKEN.eq:
        this.ir.push(OP.EQ);
        break;
      case TOKEN.ne:
        this.ir.push(OP.NE);
        break;
      case TOKEN.gt:
        this.ir.push(OP.GT);
        break;
      case TOKEN.ge:
        this.ir.push(OP.GE);
        break;
      case TOKEN.lt:
        this.ir.push(OP.LT);
        break;
      case TOKEN.le:
        this.ir.push(OP.LE);
        break;
      case TOKEN.and:
        this.ir.push(OP.AND);
        break;
      case TOKEN.or:
        this.ir.push(OP.OR);
        break;
      case TOKEN.xor:
        this.ir.push(OP.XOR);
        break;
      case TOKEN.atan:
        this.ir.push(OP.ATAN);
        break;
      case TOKEN.min:
        this.ir.push(OP.MIN);
        break;
      case TOKEN.max:
        this.ir.push(OP.MAX);
        break;
      default:
        return false;
    }
    return true;
  }
  _compileTernary(node) {
    if (!this._compileNode(node.cond)) {
      return false;
    }
    this.ir.push(OP.IF, 0);
    const ifPatch = this.ir.length - 1;
    if (!this._compileNode(node.then)) {
      return false;
    }
    this.ir.push(OP.JUMP, 0);
    const jumpPatch = this.ir.length - 1;
    this.ir[ifPatch] = this.ir.length;
    if (!this._compileNode(node.otherwise)) {
      return false;
    }
    this.ir[jumpPatch] = this.ir.length;
    return true;
  }
  compile(program) {
    const outputs = new PSStackToTree().evaluate(program, this.nIn, this.nOut);
    if (!outputs) {
      return null;
    }
    for (let i = 0; i < this.nOut; i++) {
      if (!this._compileNode(outputs[i])) {
        return null;
      }
      const min = this.range[i * 2];
      const max = this.range[i * 2 + 1];
      this.ir.push(OP.STORE, i, min, max);
    }
    return new Float64Array(this.ir);
  }
  static execute(ir, src, srcOffset, dest, destOffset) {
    let ip = 0,
      sp = 0;
    const n = ir.length;
    const stack = this.#stack;
    const tmp = this.#tmp;
    while (ip < n) {
      switch (ir[ip++] | 0) {
        case OP.ARG:
          stack[sp++] = src[srcOffset + (ir[ip++] | 0)];
          break;
        case OP.CONST:
          stack[sp++] = ir[ip++];
          break;
        case OP.STORE:
          {
            const slot = ir[ip++] | 0;
            const min = ir[ip++];
            const max = ir[ip++];
            dest[destOffset + slot] = (0,math_clamp.MathClamp)(stack[--sp], min, max);
            break;
          }
        case OP.IF:
          {
            const tgt = ir[ip++];
            if (stack[--sp] === 0) {
              ip = tgt;
            }
            break;
          }
        case OP.JUMP:
          ip = ir[ip];
          break;
        case OP.ABS:
          stack[sp - 1] = Math.abs(stack[sp - 1]);
          break;
        case OP.NEG:
          stack[sp - 1] = -stack[sp - 1];
          break;
        case OP.CEIL:
          stack[sp - 1] = Math.ceil(stack[sp - 1]);
          break;
        case OP.FLOOR:
          stack[sp - 1] = Math.floor(stack[sp - 1]);
          break;
        case OP.ROUND:
          stack[sp - 1] = Math.floor(stack[sp - 1] + 0.5);
          break;
        case OP.TRUNC:
          stack[sp - 1] = Math.trunc(stack[sp - 1]);
          break;
        case OP.NOT_B:
          stack[sp - 1] = stack[sp - 1] !== 0 ? 0 : 1;
          break;
        case OP.NOT_N:
          stack[sp - 1] = ~(stack[sp - 1] | 0);
          break;
        case OP.SQRT:
          stack[sp - 1] = Math.sqrt(stack[sp - 1]);
          break;
        case OP.SIN:
          stack[sp - 1] = Math.sin(stack[sp - 1] % 360 * _DEG_TO_RAD);
          break;
        case OP.COS:
          stack[sp - 1] = Math.cos(stack[sp - 1] % 360 * _DEG_TO_RAD);
          break;
        case OP.LN:
          stack[sp - 1] = Math.log(stack[sp - 1]);
          break;
        case OP.LOG10:
          stack[sp - 1] = Math.log10(stack[sp - 1]);
          break;
        case OP.CVI:
          stack[sp - 1] = Math.trunc(stack[sp - 1]) | 0;
          break;
        case OP.SHIFT:
          {
            const amt = ir[ip++];
            const v = stack[sp - 1] | 0;
            if (amt > 0) {
              stack[sp - 1] = v << amt;
            } else if (amt < 0) {
              stack[sp - 1] = v >> -amt;
            } else {
              stack[sp - 1] = v;
            }
            break;
          }
        case OP.ADD:
          {
            const b = stack[--sp];
            stack[sp - 1] += b;
            break;
          }
        case OP.SUB:
          {
            const b = stack[--sp];
            stack[sp - 1] -= b;
            break;
          }
        case OP.MUL:
          {
            const b = stack[--sp];
            stack[sp - 1] *= b;
            break;
          }
        case OP.DIV:
          {
            const b = stack[--sp];
            stack[sp - 1] = b !== 0 ? stack[sp - 1] / b : 0;
            break;
          }
        case OP.IDIV:
          {
            const b = stack[--sp];
            stack[sp - 1] = b !== 0 ? Math.trunc(stack[sp - 1] / b) : 0;
            break;
          }
        case OP.MOD:
          {
            const b = stack[--sp];
            stack[sp - 1] = b !== 0 ? stack[sp - 1] % b : 0;
            break;
          }
        case OP.POW:
          {
            const b = stack[--sp];
            stack[sp - 1] **= b;
            break;
          }
        case OP.EQ:
          {
            const b = stack[--sp];
            stack[sp - 1] = stack[sp - 1] === b ? 1 : 0;
            break;
          }
        case OP.NE:
          {
            const b = stack[--sp];
            stack[sp - 1] = stack[sp - 1] !== b ? 1 : 0;
            break;
          }
        case OP.GT:
          {
            const b = stack[--sp];
            stack[sp - 1] = stack[sp - 1] > b ? 1 : 0;
            break;
          }
        case OP.GE:
          {
            const b = stack[--sp];
            stack[sp - 1] = stack[sp - 1] >= b ? 1 : 0;
            break;
          }
        case OP.LT:
          {
            const b = stack[--sp];
            stack[sp - 1] = stack[sp - 1] < b ? 1 : 0;
            break;
          }
        case OP.LE:
          {
            const b = stack[--sp];
            stack[sp - 1] = stack[sp - 1] <= b ? 1 : 0;
            break;
          }
        case OP.AND:
          {
            const b = stack[--sp] | 0;
            stack[sp - 1] = (stack[sp - 1] | 0) & b;
            break;
          }
        case OP.OR:
          {
            const b = stack[--sp] | 0;
            stack[sp - 1] = stack[sp - 1] | 0 | b;
            break;
          }
        case OP.XOR:
          {
            const b = stack[--sp] | 0;
            stack[sp - 1] = (stack[sp - 1] | 0) ^ b;
            break;
          }
        case OP.ATAN:
          {
            const b = stack[--sp];
            const deg = Math.atan2(stack[sp - 1], b) * _RAD_TO_DEG;
            stack[sp - 1] = deg < 0 ? deg + 360 : deg;
            break;
          }
        case OP.MIN:
          {
            const b = stack[--sp];
            stack[sp - 1] = Math.min(stack[sp - 1], b);
            break;
          }
        case OP.MAX:
          {
            const b = stack[--sp];
            stack[sp - 1] = Math.max(stack[sp - 1], b);
            break;
          }
        case OP.TEE_TMP:
          tmp[ir[ip++] | 0] = stack[sp - 1];
          break;
        case OP.LOAD_TMP:
          stack[sp++] = tmp[ir[ip++] | 0];
          break;
      }
    }
  }
}
class PSStackBasedInterpreter {
  static #stack = new Float64Array(100);
  static #isBool = new Uint8Array(100);
  static #sp = 0;
  static #push(v, isBool = 0) {
    if (this.#sp < this.#stack.length) {
      this.#isBool[this.#sp] = isBool;
      this.#stack[this.#sp++] = v;
    }
  }
  static #execOp(op) {
    const stack = this.#stack;
    const isBool = this.#isBool;
    switch (op) {
      case TOKEN.true:
        this.#push(1, 1);
        return;
      case TOKEN.false:
        this.#push(0, 1);
        return;
      case TOKEN.abs:
        stack[this.#sp - 1] = Math.abs(stack[this.#sp - 1]);
        break;
      case TOKEN.neg:
        stack[this.#sp - 1] = -stack[this.#sp - 1];
        break;
      case TOKEN.ceiling:
        stack[this.#sp - 1] = Math.ceil(stack[this.#sp - 1]);
        break;
      case TOKEN.floor:
        stack[this.#sp - 1] = Math.floor(stack[this.#sp - 1]);
        break;
      case TOKEN.round:
        stack[this.#sp - 1] = Math.floor(stack[this.#sp - 1] + 0.5);
        break;
      case TOKEN.truncate:
        stack[this.#sp - 1] = Math.trunc(stack[this.#sp - 1]);
        break;
      case TOKEN.sqrt:
        stack[this.#sp - 1] = Math.sqrt(stack[this.#sp - 1]);
        break;
      case TOKEN.sin:
        stack[this.#sp - 1] = Math.sin(stack[this.#sp - 1] % 360 * _DEG_TO_RAD);
        break;
      case TOKEN.cos:
        stack[this.#sp - 1] = Math.cos(stack[this.#sp - 1] % 360 * _DEG_TO_RAD);
        break;
      case TOKEN.ln:
        stack[this.#sp - 1] = Math.log(stack[this.#sp - 1]);
        break;
      case TOKEN.log:
        stack[this.#sp - 1] = Math.log10(stack[this.#sp - 1]);
        break;
      case TOKEN.cvi:
        stack[this.#sp - 1] = Math.trunc(stack[this.#sp - 1]) | 0;
        break;
      case TOKEN.cvr:
        break;
      case TOKEN.not:
        {
          const v = stack[this.#sp - 1];
          stack[this.#sp - 1] = isBool[this.#sp - 1] ? 1 - v : ~(v | 0);
          return;
        }
      case TOKEN.add:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] += b;
          break;
        }
      case TOKEN.sub:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] -= b;
          break;
        }
      case TOKEN.mul:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] *= b;
          break;
        }
      case TOKEN.div:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = b !== 0 ? stack[this.#sp - 1] / b : 0;
          break;
        }
      case TOKEN.idiv:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = b !== 0 ? Math.trunc(stack[this.#sp - 1] / b) : 0;
          break;
        }
      case TOKEN.mod:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = b !== 0 ? stack[this.#sp - 1] % b : 0;
          break;
        }
      case TOKEN.exp:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] **= b;
          break;
        }
      case TOKEN.atan:
        {
          const dx = stack[--this.#sp];
          const deg = Math.atan2(stack[this.#sp - 1], dx) * _RAD_TO_DEG;
          stack[this.#sp - 1] = deg < 0 ? deg + 360 : deg;
          break;
        }
      case TOKEN.eq:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = stack[this.#sp - 1] === b && isBool[this.#sp - 1] === isBool[this.#sp] ? 1 : 0;
          isBool[this.#sp - 1] = 1;
          return;
        }
      case TOKEN.ne:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = stack[this.#sp - 1] !== b || isBool[this.#sp - 1] !== isBool[this.#sp] ? 1 : 0;
          isBool[this.#sp - 1] = 1;
          return;
        }
      case TOKEN.gt:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = stack[this.#sp - 1] > b ? 1 : 0;
          isBool[this.#sp - 1] = 1;
          return;
        }
      case TOKEN.ge:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = stack[this.#sp - 1] >= b ? 1 : 0;
          isBool[this.#sp - 1] = 1;
          return;
        }
      case TOKEN.lt:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = stack[this.#sp - 1] < b ? 1 : 0;
          isBool[this.#sp - 1] = 1;
          return;
        }
      case TOKEN.le:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = stack[this.#sp - 1] <= b ? 1 : 0;
          isBool[this.#sp - 1] = 1;
          return;
        }
      case TOKEN.and:
        {
          const b = stack[--this.#sp] | 0;
          stack[this.#sp - 1] = (stack[this.#sp - 1] | 0) & b;
          isBool[this.#sp - 1] &= isBool[this.#sp];
          return;
        }
      case TOKEN.or:
        {
          const b = stack[--this.#sp] | 0;
          stack[this.#sp - 1] = stack[this.#sp - 1] | 0 | b;
          isBool[this.#sp - 1] &= isBool[this.#sp];
          return;
        }
      case TOKEN.xor:
        {
          const b = stack[--this.#sp] | 0;
          stack[this.#sp - 1] = (stack[this.#sp - 1] | 0) ^ b;
          isBool[this.#sp - 1] &= isBool[this.#sp];
          return;
        }
      case TOKEN.bitshift:
        {
          const amt = stack[--this.#sp] | 0;
          const v = stack[this.#sp - 1] | 0;
          stack[this.#sp - 1] = amt > 0 ? v << amt : v >> -amt;
          break;
        }
      case TOKEN.min:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = Math.min(stack[this.#sp - 1], b);
          break;
        }
      case TOKEN.max:
        {
          const b = stack[--this.#sp];
          stack[this.#sp - 1] = Math.max(stack[this.#sp - 1], b);
          break;
        }
      case TOKEN.dup:
        this.#push(stack[this.#sp - 1], isBool[this.#sp - 1]);
        return;
      case TOKEN.exch:
        {
          const a = stack[--this.#sp];
          const aIsBool = isBool[this.#sp];
          const b = stack[--this.#sp];
          const bIsBool = isBool[this.#sp];
          this.#push(a, aIsBool);
          this.#push(b, bIsBool);
          return;
        }
      case TOKEN.pop:
        this.#sp--;
        return;
      case TOKEN.copy:
        {
          const n = Math.trunc(stack[--this.#sp]);
          const base = this.#sp - n;
          for (let k = 0; k < n; k++) {
            this.#push(stack[base + k], isBool[base + k]);
          }
          return;
        }
      case TOKEN.index:
        {
          const i = Math.trunc(stack[--this.#sp]);
          this.#push(stack[this.#sp - 1 - i], isBool[this.#sp - 1 - i]);
          return;
        }
      case TOKEN.roll:
        {
          const j = Math.trunc(stack[--this.#sp]);
          const n = Math.trunc(stack[--this.#sp]);
          if (n > 1 && j !== 0) {
            const mod = (j % n + n) % n;
            if (mod !== 0) {
              const base = this.#sp - n;
              const sub = stack.slice(base, this.#sp);
              const subIsBool = isBool.slice(base, this.#sp);
              for (let k = 0; k < n; k++) {
                stack[base + k] = sub[(k - mod + n) % n];
                isBool[base + k] = subIsBool[(k - mod + n) % n];
              }
            }
          }
          return;
        }
    }
    isBool[this.#sp - 1] = 0;
  }
  static #execBlock(instructions) {
    for (const instr of instructions) {
      switch (instr.type) {
        case PS_NODE.number:
          this.#push(instr.value);
          break;
        case PS_NODE.operator:
          this.#execOp(instr.op);
          break;
        case PS_NODE.if:
          if (this.#stack[--this.#sp] !== 0) {
            this.#execBlock(instr.then.instructions);
          }
          break;
        case PS_NODE.ifelse:
          if (this.#stack[--this.#sp] !== 0) {
            this.#execBlock(instr.then.instructions);
          } else {
            this.#execBlock(instr.otherwise.instructions);
          }
          break;
      }
    }
  }
  static build(program, domain, range) {
    const nIn = domain.length >> 1;
    const nOut = range.length >> 1;
    const {
      instructions
    } = program.body;
    return (src, srcOffset, dest, destOffset) => {
      this.#sp = 0;
      for (let i = 0; i < nIn; i++) {
        this.#push(src[srcOffset + i]);
      }
      this.#execBlock(instructions);
      const base = this.#sp - nOut;
      for (let i = 0; i < nOut; i++) {
        const v = base + i >= 0 ? this.#stack[base + i] : 0;
        dest[destOffset + i] = (0,math_clamp.MathClamp)(v, range[i * 2], range[i * 2 + 1]);
      }
    };
  }
}
function buildPostScriptJsFunction(source, domain, range, forceInterpreter = false) {
  const program = parsePostScriptFunction(source);
  const ir = !forceInterpreter && new PsJsCompiler(domain, range).compile(program);
  if (ir) {
    return (src, srcOffset, dest, destOffset) => {
      PsJsCompiler.execute(ir, src, srcOffset, dest, destOffset);
    };
  }
  return PSStackBasedInterpreter.build(program, domain, range);
}

;// ./src/core/postscript/wasm_compiler.js



const wasm_compiler_OP = {
  if: 0x04,
  else: 0x05,
  end: 0x0b,
  select: 0x1b,
  call: 0x10,
  local_get: 0x20,
  local_set: 0x21,
  local_tee: 0x22,
  i32_const: 0x41,
  i32_eqz: 0x45,
  i32_and: 0x71,
  i32_or: 0x72,
  i32_xor: 0x73,
  i32_shl: 0x74,
  i32_shr_s: 0x75,
  i32_trunc_f64_s: 0xaa,
  f64_const: 0x44,
  f64_eq: 0x61,
  f64_ne: 0x62,
  f64_lt: 0x63,
  f64_gt: 0x64,
  f64_le: 0x65,
  f64_ge: 0x66,
  f64_abs: 0x99,
  f64_neg: 0x9a,
  f64_ceil: 0x9b,
  f64_floor: 0x9c,
  f64_trunc: 0x9d,
  f64_nearest: 0x9e,
  f64_sqrt: 0x9f,
  f64_add: 0xa0,
  f64_sub: 0xa1,
  f64_mul: 0xa2,
  f64_div: 0xa3,
  f64_min: 0xa4,
  f64_max: 0xa5,
  f64_convert_i32_s: 0xb7,
  f64_store: 0x39,
  prefix_fc: 0xfc
};
const FC_OP = {
  i32_trunc_sat_f64_u: 0x03
};
const FUNC_TYPE = 0x60;
const F64 = 0x7c;
const SECTION = {
  type: 0x01,
  import: 0x02,
  function: 0x03,
  memory: 0x05,
  export: 0x07,
  code: 0x0a
};
const EXTERN_FUNC = 0x00;
const EXTERN_MEM = 0x02;
function unsignedLEB128(n) {
  const out = [];
  do {
    let byte = n & 0x7f;
    n >>>= 7;
    if (n !== 0) {
      byte |= 0x80;
    }
    out.push(byte);
  } while (n !== 0);
  return out;
}
function encodeASCIIString(s) {
  return [...unsignedLEB128(s.length), ...(0,util.stringToBytes)(s)];
}
function section(id, data) {
  return [id, ...unsignedLEB128(data.length), ...data];
}
function vec(items) {
  const out = unsignedLEB128(items.length);
  for (const item of items) {
    if (typeof item === "number") {
      out.push(item);
      continue;
    }
    for (const byte of item) {
      out.push(byte);
    }
  }
  return out;
}
const MATH_IMPORTS = [["sin", "Math", "sin", [F64], [F64]], ["cos", "Math", "cos", [F64], [F64]], ["atan2", "Math", "atan2", [F64, F64], [F64]], ["log", "Math", "log", [F64], [F64]], ["log10", "Math", "log10", [F64], [F64]], ["pow", "Math", "pow", [F64, F64], [F64]]];
const _mathImportObject = {
  Math: Object.fromEntries(MATH_IMPORTS.map(([name]) => [name, Math[name]]))
};
class PsWasmCompiler {
  static #initialized = false;
  static #comparisonToOp = null;
  static #importIdx = null;
  static #degToRad = 0;
  static #radToDeg = 0;
  static #importTypeEntries = null;
  static #importSection = null;
  static #functionSection = null;
  static #memorySection = null;
  static #exportSection = null;
  static #wasmMagicVersion = null;
  static #f64View = null;
  static #f64Arr = null;
  static #init() {
    this.#comparisonToOp = new Map([[TOKEN.eq, wasm_compiler_OP.f64_eq], [TOKEN.ne, wasm_compiler_OP.f64_ne], [TOKEN.lt, wasm_compiler_OP.f64_lt], [TOKEN.le, wasm_compiler_OP.f64_le], [TOKEN.gt, wasm_compiler_OP.f64_gt], [TOKEN.ge, wasm_compiler_OP.f64_ge]]);
    this.#importIdx = Object.create(null);
    for (let i = 0; i < MATH_IMPORTS.length; i++) {
      this.#importIdx[MATH_IMPORTS[i][0]] = i;
    }
    this.#degToRad = Math.PI / 180;
    this.#radToDeg = 180 / Math.PI;
    this.#importTypeEntries = MATH_IMPORTS.map(([,,, params, results]) => [FUNC_TYPE, ...vec(params), ...vec(results)]);
    this.#importSection = new Uint8Array(section(SECTION.import, vec(MATH_IMPORTS.map(([, mod, field], i) => [...encodeASCIIString(mod), ...encodeASCIIString(field), EXTERN_FUNC, ...unsignedLEB128(i + 1)]))));
    this.#functionSection = new Uint8Array(section(SECTION.function, vec([[0]])));
    this.#memorySection = new Uint8Array(section(SECTION.memory, vec([[0x00, 0x01]])));
    this.#exportSection = new Uint8Array(section(SECTION.export, vec([[...encodeASCIIString("fn"), EXTERN_FUNC, ...unsignedLEB128(MATH_IMPORTS.length)], [...encodeASCIIString("mem"), EXTERN_MEM, 0x00]])));
    this.#wasmMagicVersion = new Uint8Array([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]);
    const f64Buf = new ArrayBuffer(8);
    this.#f64View = new DataView(f64Buf);
    this.#f64Arr = new Uint8Array(f64Buf);
    this.#initialized = true;
  }
  constructor(domain, range) {
    if (!PsWasmCompiler.#initialized) {
      PsWasmCompiler.#init();
    }
    this._nIn = domain.length >> 1;
    this._nOut = range.length >> 1;
    this._range = range;
    this._code = [];
    this._nextLocal = this._nIn;
    this._freeLocals = [];
    this._sharedLocals = new Map();
  }
  _allocLocal() {
    return this._freeLocals.pop() ?? this._nextLocal++;
  }
  _releaseLocal(idx) {
    this._freeLocals.push(idx);
  }
  _emitULEB128(n) {
    do {
      let b = n & 0x7f;
      n >>>= 7;
      if (n !== 0) {
        b |= 0x80;
      }
      this._code.push(b);
    } while (n !== 0);
  }
  _emitSLEB128(n) {
    for (;;) {
      const b = n & 0x7f;
      n >>= 7;
      if (n === 0 && (b & 0x40) === 0 || n === -1 && (b & 0x40) !== 0) {
        this._code.push(b);
        return;
      }
      this._code.push(b | 0x80);
    }
  }
  _emitF64Const(value) {
    this._code.push(wasm_compiler_OP.f64_const);
    PsWasmCompiler.#f64View.setFloat64(0, value, true);
    for (let i = 0; i < 8; i++) {
      this._code.push(PsWasmCompiler.#f64Arr[i]);
    }
  }
  _emitLocalGet(idx) {
    this._code.push(wasm_compiler_OP.local_get);
    this._emitULEB128(idx);
  }
  _emitLocalSet(idx) {
    this._code.push(wasm_compiler_OP.local_set);
    this._emitULEB128(idx);
  }
  _emitLocalTee(idx) {
    this._code.push(wasm_compiler_OP.local_tee);
    this._emitULEB128(idx);
  }
  _emitToInt32() {
    const code = this._code;
    const local = this._allocLocal();
    code.push(wasm_compiler_OP.f64_trunc);
    this._emitLocalTee(local);
    this._emitLocalGet(local);
    this._emitF64Const(2 ** -32);
    code.push(wasm_compiler_OP.f64_mul, wasm_compiler_OP.f64_floor);
    this._emitF64Const(2 ** 32);
    code.push(wasm_compiler_OP.f64_mul, wasm_compiler_OP.f64_sub, wasm_compiler_OP.prefix_fc, FC_OP.i32_trunc_sat_f64_u);
    this._releaseLocal(local);
  }
  _compileNode(node) {
    if (node.shared) {
      const entry = this._sharedLocals.get(node);
      if (entry !== undefined) {
        this._emitLocalGet(entry.local);
        if (--entry.remaining === 0) {
          this._releaseLocal(entry.local);
        }
        return true;
      }
      if (!this._compileNodeImpl(node)) {
        return false;
      }
      const local = this._allocLocal();
      this._sharedLocals.set(node, {
        local,
        remaining: node.sharedCount - 1
      });
      this._emitLocalTee(local);
      return true;
    }
    return this._compileNodeImpl(node);
  }
  _compileNodeImpl(node) {
    switch (node.type) {
      case PS_NODE.arg:
        this._emitLocalGet(node.index);
        return true;
      case PS_NODE.const:
        {
          let v = node.value;
          if (typeof v === "boolean") {
            v = v ? 1 : 0;
          }
          this._emitF64Const(v);
          return true;
        }
      case PS_NODE.unary:
        return this._compileUnaryNode(node);
      case PS_NODE.binary:
        return this._compileBinaryNode(node);
      case PS_NODE.ternary:
        return this._compileTernaryNode(node);
      default:
        return false;
    }
  }
  _compileSinCosNode(node) {
    const local = this._allocLocal();
    try {
      if (!this._compileNode(node.operand)) {
        return false;
      }
      const code = this._code;
      this._emitLocalSet(local);
      this._emitLocalGet(local);
      this._emitLocalGet(local);
      this._emitF64Const(360);
      code.push(wasm_compiler_OP.f64_div, wasm_compiler_OP.f64_trunc);
      this._emitF64Const(360);
      code.push(wasm_compiler_OP.f64_mul, wasm_compiler_OP.f64_sub);
      this._emitF64Const(PsWasmCompiler.#degToRad);
      code.push(wasm_compiler_OP.f64_mul, wasm_compiler_OP.call);
      this._emitULEB128(PsWasmCompiler.#importIdx[node.op === TOKEN.sin ? "sin" : "cos"]);
      return true;
    } finally {
      this._releaseLocal(local);
    }
  }
  _compileUnaryNode(node) {
    const code = this._code;
    if (node.op === TOKEN.sin || node.op === TOKEN.cos) {
      return this._compileSinCosNode(node);
    }
    if (node.op === TOKEN.not) {
      if (node.valueType === PS_VALUE_TYPE.boolean) {
        if (!this._compileNodeAsBoolI32(node.operand)) {
          return false;
        }
        code.push(wasm_compiler_OP.i32_eqz, wasm_compiler_OP.f64_convert_i32_s);
        return true;
      }
      if (node.valueType === PS_VALUE_TYPE.numeric) {
        if (!this._compileNode(node.operand)) {
          return false;
        }
        this._emitToInt32();
        code.push(wasm_compiler_OP.i32_const, 0x7f, wasm_compiler_OP.i32_xor, wasm_compiler_OP.f64_convert_i32_s);
        return true;
      }
      return false;
    }
    if (!this._compileNode(node.operand)) {
      return false;
    }
    switch (node.op) {
      case TOKEN.abs:
        code.push(wasm_compiler_OP.f64_abs);
        break;
      case TOKEN.neg:
        code.push(wasm_compiler_OP.f64_neg);
        break;
      case TOKEN.sqrt:
        code.push(wasm_compiler_OP.f64_sqrt);
        break;
      case TOKEN.floor:
        code.push(wasm_compiler_OP.f64_floor);
        break;
      case TOKEN.ceiling:
        code.push(wasm_compiler_OP.f64_ceil);
        break;
      case TOKEN.round:
        this._emitF64Const(0.5);
        code.push(wasm_compiler_OP.f64_add, wasm_compiler_OP.f64_floor);
        break;
      case TOKEN.truncate:
        code.push(wasm_compiler_OP.f64_trunc);
        break;
      case TOKEN.cvi:
        this._emitToInt32();
        code.push(wasm_compiler_OP.f64_convert_i32_s);
        break;
      case TOKEN.cvr:
        break;
      case TOKEN.ln:
        code.push(wasm_compiler_OP.call);
        this._emitULEB128(PsWasmCompiler.#importIdx.log);
        break;
      case TOKEN.log:
        code.push(wasm_compiler_OP.call);
        this._emitULEB128(PsWasmCompiler.#importIdx.log10);
        break;
      default:
        return false;
    }
    return true;
  }
  _compileSafeDivNode(first, second) {
    const tmp = this._allocLocal();
    try {
      if (!this._compileNode(second) || !this._compileNode(first)) {
        return false;
      }
      const code = this._code;
      this._emitLocalTee(tmp);
      code.push(wasm_compiler_OP.f64_div);
      this._emitF64Const(0);
      this._emitLocalGet(tmp);
      this._emitF64Const(0);
      code.push(wasm_compiler_OP.f64_ne, wasm_compiler_OP.select);
      return true;
    } finally {
      this._releaseLocal(tmp);
    }
  }
  _compileSafeIdivNode(first, second) {
    const tmp = this._allocLocal();
    try {
      if (!this._compileNode(second) || !this._compileNode(first)) {
        return false;
      }
      const code = this._code;
      this._emitLocalTee(tmp);
      code.push(wasm_compiler_OP.f64_div, wasm_compiler_OP.f64_trunc);
      this._emitF64Const(0);
      this._emitLocalGet(tmp);
      this._emitF64Const(0);
      code.push(wasm_compiler_OP.f64_ne, wasm_compiler_OP.select);
      return true;
    } finally {
      this._releaseLocal(tmp);
    }
  }
  _compileBitshiftNode(first, second) {
    if (first.type !== PS_NODE.const || !Number.isInteger(first.value) || !this._compileNode(second)) {
      return false;
    }
    const code = this._code;
    this._emitToInt32();
    const shift = first.value;
    if (shift > 0) {
      code.push(wasm_compiler_OP.i32_const);
      this._emitSLEB128(shift);
      code.push(wasm_compiler_OP.i32_shl);
    } else if (shift < 0) {
      code.push(wasm_compiler_OP.i32_const);
      this._emitSLEB128(-shift);
      code.push(wasm_compiler_OP.i32_shr_s);
    }
    code.push(wasm_compiler_OP.f64_convert_i32_s);
    return true;
  }
  _compileModNode(first, second) {
    if (first.type === PS_NODE.const && first.value === 0) {
      this._emitF64Const(0);
      return true;
    }
    const localA = this._allocLocal();
    try {
      if (!this._compileNode(second)) {
        return false;
      }
      this._emitLocalTee(localA);
      const code = this._code;
      if (first.type === PS_NODE.const) {
        this._emitLocalGet(localA);
        this._emitF64Const(first.value);
        code.push(wasm_compiler_OP.f64_div, wasm_compiler_OP.f64_trunc);
        this._emitF64Const(first.value);
        code.push(wasm_compiler_OP.f64_mul, wasm_compiler_OP.f64_sub);
      } else {
        const localB = this._allocLocal();
        try {
          if (!this._compileNode(first)) {
            return false;
          }
          this._emitLocalSet(localB);
          this._emitLocalGet(localA);
          this._emitLocalGet(localB);
          code.push(wasm_compiler_OP.f64_div, wasm_compiler_OP.f64_trunc);
          this._emitLocalGet(localB);
          code.push(wasm_compiler_OP.f64_mul, wasm_compiler_OP.f64_sub);
          this._emitF64Const(0);
          this._emitLocalGet(localB);
          this._emitF64Const(0);
          code.push(wasm_compiler_OP.f64_ne, wasm_compiler_OP.select);
        } finally {
          this._releaseLocal(localB);
        }
      }
      return true;
    } finally {
      this._releaseLocal(localA);
    }
  }
  _compileAtanNode(first, second) {
    const localR = this._allocLocal();
    try {
      if (!this._compileNode(second) || !this._compileNode(first)) {
        return false;
      }
      const code = this._code;
      code.push(wasm_compiler_OP.call);
      this._emitULEB128(PsWasmCompiler.#importIdx.atan2);
      this._emitF64Const(PsWasmCompiler.#radToDeg);
      code.push(wasm_compiler_OP.f64_mul);
      this._emitLocalTee(localR);
      this._emitF64Const(0);
      code.push(wasm_compiler_OP.f64_lt, wasm_compiler_OP.if, F64);
      this._emitLocalGet(localR);
      this._emitF64Const(360);
      code.push(wasm_compiler_OP.f64_add, wasm_compiler_OP.else);
      this._emitLocalGet(localR);
      code.push(wasm_compiler_OP.end);
      return true;
    } finally {
      this._releaseLocal(localR);
    }
  }
  _compileBitwiseNode(op, first, second) {
    if (!this._compileBitwiseOperandI32(second) || !this._compileBitwiseOperandI32(first)) {
      return false;
    }
    const code = this._code;
    switch (op) {
      case TOKEN.and:
        code.push(wasm_compiler_OP.i32_and);
        break;
      case TOKEN.or:
        code.push(wasm_compiler_OP.i32_or);
        break;
      case TOKEN.xor:
        code.push(wasm_compiler_OP.i32_xor);
        break;
      default:
        return false;
    }
    code.push(wasm_compiler_OP.f64_convert_i32_s);
    return true;
  }
  _compileBitwiseOperandI32(node) {
    if (node.valueType === PS_VALUE_TYPE.boolean) {
      return this._compileNodeAsBoolI32(node);
    }
    if (!this._compileNode(node)) {
      return false;
    }
    this._emitToInt32();
    return true;
  }
  _compileStandardBinaryNode(op, first, second) {
    if (first === second && first.type !== PS_NODE.arg && first.type !== PS_NODE.const && !first.shared) {
      const tmp = this._allocLocal();
      try {
        if (!this._compileNode(first)) {
          return false;
        }
        this._emitLocalTee(tmp);
        this._emitLocalGet(tmp);
      } finally {
        this._releaseLocal(tmp);
      }
    } else if (!this._compileNode(second) || !this._compileNode(first)) {
      return false;
    }
    const code = this._code;
    switch (op) {
      case TOKEN.add:
        code.push(wasm_compiler_OP.f64_add);
        break;
      case TOKEN.sub:
        code.push(wasm_compiler_OP.f64_sub);
        break;
      case TOKEN.mul:
        code.push(wasm_compiler_OP.f64_mul);
        break;
      case TOKEN.exp:
        code.push(wasm_compiler_OP.call);
        this._emitULEB128(PsWasmCompiler.#importIdx.pow);
        break;
      case TOKEN.eq:
        code.push(wasm_compiler_OP.f64_eq, wasm_compiler_OP.f64_convert_i32_s);
        break;
      case TOKEN.ne:
        code.push(wasm_compiler_OP.f64_ne, wasm_compiler_OP.f64_convert_i32_s);
        break;
      case TOKEN.lt:
        code.push(wasm_compiler_OP.f64_lt, wasm_compiler_OP.f64_convert_i32_s);
        break;
      case TOKEN.le:
        code.push(wasm_compiler_OP.f64_le, wasm_compiler_OP.f64_convert_i32_s);
        break;
      case TOKEN.gt:
        code.push(wasm_compiler_OP.f64_gt, wasm_compiler_OP.f64_convert_i32_s);
        break;
      case TOKEN.ge:
        code.push(wasm_compiler_OP.f64_ge, wasm_compiler_OP.f64_convert_i32_s);
        break;
      case TOKEN.min:
        code.push(wasm_compiler_OP.f64_min);
        break;
      case TOKEN.max:
        code.push(wasm_compiler_OP.f64_max);
        break;
      default:
        return false;
    }
    return true;
  }
  _compileBinaryNode(node) {
    const {
      op,
      first,
      second
    } = node;
    if (op === TOKEN.bitshift) {
      return this._compileBitshiftNode(first, second);
    }
    if (op === TOKEN.div) {
      return this._compileSafeDivNode(first, second);
    }
    if (op === TOKEN.idiv) {
      return this._compileSafeIdivNode(first, second);
    }
    if (op === TOKEN.mod) {
      return this._compileModNode(first, second);
    }
    if (op === TOKEN.atan) {
      return this._compileAtanNode(first, second);
    }
    return op === TOKEN.and || op === TOKEN.or || op === TOKEN.xor ? this._compileBitwiseNode(op, first, second) : this._compileStandardBinaryNode(op, first, second);
  }
  _compileNodeAsBoolI32(node) {
    if (node.type === PS_NODE.binary) {
      const wasmOp = PsWasmCompiler.#comparisonToOp.get(node.op);
      if (wasmOp !== undefined) {
        if (!this._compileNode(node.second) || !this._compileNode(node.first)) {
          return false;
        }
        this._code.push(wasmOp);
        return true;
      }
      if (node.valueType === PS_VALUE_TYPE.boolean && (node.op === TOKEN.and || node.op === TOKEN.or || node.op === TOKEN.xor)) {
        if (!this._compileNodeAsBoolI32(node.second) || !this._compileNodeAsBoolI32(node.first)) {
          return false;
        }
        switch (node.op) {
          case TOKEN.and:
            this._code.push(wasm_compiler_OP.i32_and);
            break;
          case TOKEN.or:
            this._code.push(wasm_compiler_OP.i32_or);
            break;
          case TOKEN.xor:
            this._code.push(wasm_compiler_OP.i32_xor);
            break;
        }
        return true;
      }
    }
    if (node.type === PS_NODE.unary && node.op === TOKEN.not && node.valueType === PS_VALUE_TYPE.boolean) {
      if (!this._compileNodeAsBoolI32(node.operand)) {
        return false;
      }
      this._code.push(wasm_compiler_OP.i32_eqz);
      return true;
    }
    if (!this._compileNode(node)) {
      return false;
    }
    if (node.valueType === PS_VALUE_TYPE.boolean) {
      this._code.push(wasm_compiler_OP.i32_trunc_f64_s);
    } else {
      this._emitF64Const(0);
      this._code.push(wasm_compiler_OP.f64_ne);
    }
    return true;
  }
  _compileTernaryNode(node) {
    if (!this._compileNodeAsBoolI32(node.cond)) {
      return false;
    }
    this._code.push(wasm_compiler_OP.if, F64);
    if (!this._compileNode(node.then)) {
      return false;
    }
    this._code.push(wasm_compiler_OP.else);
    if (!this._compileNode(node.otherwise)) {
      return false;
    }
    this._code.push(wasm_compiler_OP.end);
    return true;
  }
  compile(program) {
    const outputs = new PSStackToTree().evaluate(program, this._nIn, this._nOut);
    if (!outputs) {
      return null;
    }
    const code = this._code;
    for (let i = 0; i < this._nOut; i++) {
      const min = this._range[i * 2];
      const max = this._range[i * 2 + 1];
      code.push(wasm_compiler_OP.i32_const);
      this._emitSLEB128(i * 8);
      if (!this._compileNode(outputs[i])) {
        return null;
      }
      this._emitF64Const(max);
      code.push(wasm_compiler_OP.f64_min);
      this._emitF64Const(min);
      code.push(wasm_compiler_OP.f64_max, wasm_compiler_OP.f64_store, 0x03, 0x00);
    }
    code.push(wasm_compiler_OP.end);
    const nIn = this._nIn;
    const nLocals = this._nextLocal - nIn;
    const paramTypes = Array(nIn).fill(F64);
    const resultTypes = [];
    const funcType = [FUNC_TYPE, ...vec(paramTypes), ...vec(resultTypes)];
    const typeSectionBytes = new Uint8Array(section(SECTION.type, vec([funcType, ...PsWasmCompiler.#importTypeEntries])));
    const localDecls = nLocals > 0 ? vec([[...unsignedLEB128(nLocals), F64]]) : vec([]);
    const funcBodyLen = localDecls.length + code.length;
    const codeSectionBytes = new Uint8Array(section(SECTION.code, vec([[...unsignedLEB128(funcBodyLen), ...localDecls, ...code]])));
    const magicVersion = PsWasmCompiler.#wasmMagicVersion;
    const importSection = PsWasmCompiler.#importSection;
    const functionSection = PsWasmCompiler.#functionSection;
    const memorySection = PsWasmCompiler.#memorySection;
    const exportSection = PsWasmCompiler.#exportSection;
    const totalLen = magicVersion.length + typeSectionBytes.length + importSection.length + functionSection.length + memorySection.length + exportSection.length + codeSectionBytes.length;
    const result = new Uint8Array(totalLen);
    let off = 0;
    result.set(magicVersion, off);
    off += magicVersion.length;
    result.set(typeSectionBytes, off);
    off += typeSectionBytes.length;
    result.set(importSection, off);
    off += importSection.length;
    result.set(functionSection, off);
    off += functionSection.length;
    result.set(memorySection, off);
    off += memorySection.length;
    result.set(exportSection, off);
    off += exportSection.length;
    result.set(codeSectionBytes, off);
    return result;
  }
}
function compilePostScriptToWasm(source, domain, range) {
  return new PsWasmCompiler(domain, range).compile(parsePostScriptFunction(source));
}
function _makeWrapper(exports, nIn, nOut) {
  const {
    fn,
    mem
  } = exports;
  const outView = new Float64Array(mem.buffer, 0, nOut);
  let writeOut;
  switch (nOut) {
    case 1:
      writeOut = (dest, destOffset) => {
        dest[destOffset] = outView[0];
      };
      break;
    case 2:
      writeOut = (dest, destOffset) => {
        dest[destOffset] = outView[0];
        dest[destOffset + 1] = outView[1];
      };
      break;
    case 3:
      writeOut = (dest, destOffset) => {
        dest[destOffset] = outView[0];
        dest[destOffset + 1] = outView[1];
        dest[destOffset + 2] = outView[2];
      };
      break;
    case 4:
      writeOut = (dest, destOffset) => {
        dest[destOffset] = outView[0];
        dest[destOffset + 1] = outView[1];
        dest[destOffset + 2] = outView[2];
        dest[destOffset + 3] = outView[3];
      };
      break;
    default:
      writeOut = (dest, destOffset) => {
        for (let i = 0; i < nOut; i++) {
          dest[destOffset + i] = outView[i];
        }
      };
  }
  switch (nIn) {
    case 1:
      return (src, srcOffset, dest, destOffset) => {
        fn(src[srcOffset]);
        writeOut(dest, destOffset);
      };
    case 2:
      return (src, srcOffset, dest, destOffset) => {
        fn(src[srcOffset], src[srcOffset + 1]);
        writeOut(dest, destOffset);
      };
    case 3:
      return (src, srcOffset, dest, destOffset) => {
        fn(src[srcOffset], src[srcOffset + 1], src[srcOffset + 2]);
        writeOut(dest, destOffset);
      };
    case 4:
      return (src, srcOffset, dest, destOffset) => {
        fn(src[srcOffset], src[srcOffset + 1], src[srcOffset + 2], src[srcOffset + 3]);
        writeOut(dest, destOffset);
      };
    default:
      {
        const inBuf = new Float64Array(nIn);
        return (src, srcOffset, dest, destOffset) => {
          for (let i = 0; i < nIn; i++) {
            inBuf[i] = src[srcOffset + i];
          }
          fn(...inBuf);
          writeOut(dest, destOffset);
        };
      }
  }
}
function buildPostScriptWasmFunction(source, domain, range) {
  const bytes = compilePostScriptToWasm(source, domain, range);
  if (!bytes) {
    return null;
  }
  try {
    const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes), _mathImportObject);
    return _makeWrapper(instance.exports, domain.length >> 1, range.length >> 1);
  } catch {
    return null;
  }
}

;// ./src/core/postscript/calculator.js



function buildPostScriptFunction(code, domain, range, useWasm) {
  if (useWasm) {
    try {
      const wasmFunction = buildPostScriptWasmFunction(code, domain, range);
      if (wasmFunction) {
        return wasmFunction;
      }
    } catch {}
  }
  ;(0,util.warn)("Failed to compile PostScript function to wasm, falling back to JS");
  return buildPostScriptJsFunction(code, domain, range);
}


/***/ }

};

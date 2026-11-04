/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

import { replaceNode } from "./utils/ast";
import { isTopLevel } from "./utils/helpers";

import generate from "@babel/generator";
import * as t from "@babel/types";

function getAssignmentTarget(node, bindings) {
  if (t.isObjectPattern(node)) {
    for (const property of node.properties) {
      if (t.isRestElement(property)) {
        property.argument = getAssignmentTarget(property.argument, bindings);
      } else {
        property.value = getAssignmentTarget(property.value, bindings);
      }
    }

    return node;
  }

  if (t.isArrayPattern(node)) {
    for (const [i, element] of node.elements.entries()) {
      node.elements[i] = getAssignmentTarget(element, bindings);
    }

    return node;
  }

  if (t.isAssignmentPattern(node)) {
    node.left = getAssignmentTarget(node.left, bindings);

    return node;
  }

  if (t.isRestElement(node)) {
    node.argument = getAssignmentTarget(node.argument, bindings);

    return node;
  }

  if (t.isIdentifier(node)) {
    return bindings.includes(node.name)
      ? node
      : t.memberExpression(t.identifier("self"), node);
  }

  return node;
}

// translates new bindings `var a = 3` into `self.a = 3`
// and existing bindings `var a = 3` into `a = 3` for re-assignments
function globalizeDeclaration(node, bindings) {
  return node.declarations.map(declaration =>
    t.expressionStatement(
      t.assignmentExpression(
        "=",
        getAssignmentTarget(declaration.id, bindings),
        declaration.init || t.unaryExpression("void", t.numericLiteral(0))
      )
    )
  );
}

// translates new bindings `a = 3` into `self.a = 3`
// and keeps assignments the same for existing bindings.
function globalizeAssignment(node, bindings) {
  return t.assignmentExpression(
    node.operator,
    getAssignmentTarget(node.left, bindings),
    node.right
  );
}

export default function mapExpressionBindings(expression, ast, bindings = []) {
  let isMapped = false;
  let shouldUpdate = true;

  t.traverse(ast, (node, ancestors) => {
    const parent = ancestors[ancestors.length - 1];

    if (t.isWithStatement(node)) {
      shouldUpdate = false;
      return;
    }

    if (!isTopLevel(ancestors)) {
      return;
    }

    if (t.isAssignmentExpression(node)) {
      if (t.isIdentifier(node.left) || t.isPattern(node.left)) {
        const newNode = globalizeAssignment(node, bindings);
        isMapped = true;
        replaceNode(ancestors, newNode);
        return;
      }

      return;
    }

    if (!t.isVariableDeclaration(node)) {
      return;
    }

    // Only `var` declarations are rewritten. `let`/`const` declarations
    // must reach the engine unchanged so they create a frame-local shadow
    // instead of mutating the outer binding.
    if (node.kind !== "var") {
      return;
    }

    if (!t.isForStatement(parent.node)) {
      const newNodes = globalizeDeclaration(node, bindings);
      isMapped = true;
      replaceNode(ancestors, newNodes);
    }
  });

  if (!shouldUpdate || !isMapped) {
    return expression;
  }

  return generate(ast).code;
}

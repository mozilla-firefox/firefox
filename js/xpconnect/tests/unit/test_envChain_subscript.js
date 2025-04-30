/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Verify the environment chain for subscripts described in
// js/src/vm/EnvironmentObject.h.

add_task(async function() {
  const target = {};
  Services.scriptloader.loadSubScript("resource://test/file_envChain_subscript.js", target);

  const envs = target.ENVS;

  Assert.equal(envs.length, 4);

  let i = 0, env;

  env = envs[i]; i++;
  Assert.equal(env.type, "NonSyntacticLexicalEnvironmentObject");
  Assert.equal(env.qualified, false);
  Assert.equal(env.unqualified, false);
  Assert.equal(env.lexical, true, "lexical must live in the NSLEO");
  Assert.equal(env.prop, false);

  env = envs[i]; i++;
  Assert.equal(env.type, "WithEnvironmentObject");
  Assert.equal(env.qualified, true, "qualified var must live in the with env");
  Assert.equal(env.unqualified, false);
  Assert.equal(env.lexical, false);
  Assert.equal(env.prop, true, "this property must live in the with env");

  env = envs[i]; i++;
  Assert.equal(env.type, "GlobalLexicalEnvironmentObject");
  Assert.equal(env.qualified, false);
  Assert.equal(env.unqualified, false);
  Assert.equal(env.lexical, false);
  Assert.equal(env.prop, false);

  env = envs[i]; i++;
  Assert.equal(env.type, "*global*");
  Assert.equal(env.qualified, false);
  Assert.equal(env.unqualified, true, "unqualified var must live in the global");
  Assert.equal(env.lexical, false);
  Assert.equal(env.prop, false);

  Assert.equal(target.qualified, 10, "qualified var must be reflected to the target object");
  Assert.equal(target.prop, 40, "this property must be reflected to the target object");
});

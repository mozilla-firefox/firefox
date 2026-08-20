/**
* AUTO-GENERATED - DO NOT EDIT. Source: https://github.com/gpuweb/cts
**/export const description = `
Validation tests for swizzle assignments.
`;import { makeTestGroup } from '../../../../common/framework/test_group.js';
import { keysOf } from '../../../../common/util/data_tables.js';
import { ShaderValidationTest } from '../shader_validation_test.js';

export const g = makeTestGroup(ShaderValidationTest);

g.test('valid').
desc('Valid swizzle assignments').
params((u) =>
u.combine('elemType', ['f32', 'i32', 'u32']).combine('vecSize', [2, 3, 4])
).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const { elemType, vecSize } = t.params;
  const swizzle = 'xyzw'.substring(0, vecSize);
  const vecType = `vec${swizzle.length}<${elemType}>`;
  const code = `
@fragment
fn main() {
  var v = vec4<${elemType}>(0);
  v.${swizzle} = ${vecType}(1);
}
`;
  t.expectCompileResult(true, code);
});

g.test('invalid_lhs_not_reference').
desc('Invalid swizzle assignment where LHS is not a reference').
params((u) => u.combine('lhs', ['vec4f()', 'const_vec', 'foo()'])).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const code = `
const const_vec = vec4f();

fn foo() -> vec4f {
  return vec4f();
}

@fragment
fn main() {
  var v = vec4f();
  ${t.params.lhs}.xyz = vec3(0.0);
}
`;
  t.expectCompileResult(false, code);
});

g.test('invalid_duplicate_components').
desc('Invalid swizzle assignment with duplicate LHS components').
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const code = `
@fragment
fn main() {
  var v = vec4f();
  v.xx = vec2f();
}
`;
  t.expectCompileResult(false, code);
});

g.test('invalid_component_mismatch').
desc('Invalid swizzle assignment with mismatched number of components').
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const code = `
@fragment
fn main() {
  var v = vec4f();
  v.xy = vec3f();
}
`;
  t.expectCompileResult(false, code);
});

g.test('invalid_component_oob').
desc('Invalid swizzle assignment with components out of bounds').
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const code = `
@fragment
fn main() {
  var v = vec3f();
  v.xyzw = vec4f();
}
`;
  t.expectCompileResult(false, code);
});

g.test('invalid_type_mismatch').
desc('Invalid swizzle assignment with mismatched types').
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const code = `
@fragment
fn main() {
  var v = vec4f();
  v.xy = vec2i();
}
`;
  t.expectCompileResult(false, code);
});

g.test('invalid_mixed_letter_schemes').
desc('Invalid swizzle assignment with mixed letter schemes (xyzw vs. rgba)').
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const code = `
@fragment
fn main() {
  var v = vec4f();
  v.xr = vec2i();
}
`;
  t.expectCompileResult(false, code);
});

g.test('invalid_address_of_swizzle_view').
desc('Invalid to take the address of a swizzle view').
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const code = `
@fragment
fn main() {
  var v = vec4f();
  let p = &v.xy;
}
`;
  t.expectCompileResult(false, code);
});

g.test('index_into_swizzle_view').
desc('Validate constant indexing into a swizzle view on the lhs').
params((u) =>
u.
combine('swizzle', ['x', 'xy', 'xx', 'xyz', 'xyzw', 'zxy']).
combine('index', [0, 1, 2, 3, 4])
).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const { swizzle, index } = t.params;

  const code = `
@fragment
fn main() {
  var v = vec4u();
  v.${swizzle}[${index}] = 1u;
}
`;

  const isVector = swizzle.length > 1;
  const inBounds = index < swizzle.length;
  const expected = isVector && inBounds;

  t.expectCompileResult(expected, code);
});

g.test('dynamic_index_into_swizzle_view').
desc('Validate dynamic indexing into a swizzle view on the lhs').
params((u) => u.combine('swizzle', ['x', 'xy', 'xx', 'xyz', 'xyzw', 'zxy'])).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const { swizzle } = t.params;

  const code = `
@fragment
fn main() {
  var v = vec4u();
  var i = 1u;
  v.${swizzle}[i] = 1u;
}
`;

  const isVector = swizzle.length > 1;

  t.expectCompileResult(isVector, code);
});

g.test('pointer_swizzle_assignment').
desc('Validate swizzle assignments on pointers').
params((u) =>
u.
combine('use_pointer', [true, false]).
combine('requires_pointer_composite_access', [true, false]).
combine('use_compound', [true, false])
).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  const { use_pointer, requires_pointer_composite_access, use_compound } = t.params;

  const requires_directive = requires_pointer_composite_access ?
  'requires pointer_composite_access;' :
  '';

  const lhs = use_pointer ? 'p.xy' : '(*p).xy';
  const op = use_compound ? '+=' : '=';

  const code = `
${requires_directive}
@fragment
fn main() {
  var v = vec4f();
  let p = &v;
  ${lhs} ${op} vec2f(1.0);
}
`;

  const has_feature = t.hasLanguageFeature('pointer_composite_access');
  const expected = !use_pointer || has_feature;

  t.expectCompileResult(expected, code);
});

const kChainedSwizzleCases = {
  // Valid cases:
  xy_y: { chain: 'xy.y', size: 1, pass: true }, // y
  xx_x: { chain: 'xx.x', size: 1, pass: true }, // x
  xyy_xy: { chain: 'xyy.xy', size: 2, pass: true }, // xy
  xxyy_zxy_xy: { chain: 'xxyy.zxy.xy', size: 2, pass: true }, // yx
  // Invalid cases:
  xxyy_xy: { chain: 'xxyy.xy', size: 2, pass: false }, // xx
  xyy_yy: { chain: 'xyy.yy', size: 2, pass: false }, // yy
  xxyy_zxy_yz: { chain: 'xxyy.zxy.yz', size: 2, pass: false }, // xx
  zy_xyx: { chain: 'zy.xyx', size: 3, pass: false } // zyz
};

g.test('chained_swizzle').
desc('Validate chained swizzles on the LHS of an assignment').
params((u) => u.combine('case', keysOf(kChainedSwizzleCases))).
fn((t) => {
  const { chain, size, pass } = kChainedSwizzleCases[t.params.case];
  const rhs = size === 1 ? '1.0' : `vec${size}f(1.0)`;
  const code = `
@fragment
fn main() {
  var v = vec4f();
  v.${chain} = ${rhs};
}
`;
  const expected = pass && t.hasLanguageFeature('swizzle_assignment');
  t.expectCompileResult(expected, code);
});
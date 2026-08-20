/**
* AUTO-GENERATED - DO NOT EDIT. Source: https://github.com/gpuweb/cts
**/export const description = `
Swizzle assignment execution.
`;import { makeTestGroup } from '../../../../common/framework/test_group.js';
import { keysOf } from '../../../../common/util/data_tables.js';

import { Float16Array } from '../../../../external/petamoriken/float16/float16.js';
import { AllFeaturesMaxLimitsGPUTest } from '../../../gpu_test.js';
import { runFlowControlTest } from '../flow_control/harness.js';

export const g = makeTestGroup(AllFeaturesMaxLimitsGPUTest);

/**
 * Builds, runs then checks the output of a shader test with a swizzle assignment.
 *
 * @param t The test object
 * @param elemType The type of the vector elements
 * @param expectedValues The expected final values of the vector after the assignment
 */
export function runSwizzleAssignmentTest(
t,
elemType,
expectedValues,
wgsl)
{
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  if (elemType === 'f16') {
    t.skipIfDeviceDoesNotHaveFeature('shader-f16');
  }

  const pipeline = t.device.createComputePipeline({
    layout: 'auto',
    compute: {
      module: t.device.createShaderModule({ code: wgsl }),
      entryPoint: 'main'
    }
  });

  const maxOutputValues = 1000;
  const outputBuffer = t.createBufferTracked({
    size: 4 * (1 + maxOutputValues),
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC
  });

  const bindGroup = t.device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [{ binding: 1, resource: { buffer: outputBuffer } }]
  });

  const encoder = t.device.createCommandEncoder();
  const pass = encoder.beginComputePass();
  pass.setPipeline(pipeline);
  pass.setBindGroup(0, bindGroup);
  pass.dispatchWorkgroups(1);
  pass.end();
  t.queue.submit([encoder.finish()]);

  let outputArrayConstructor;
  switch (elemType) {
    case 'u32':
      outputArrayConstructor = Uint32Array;
      break;
    case 'i32':
      outputArrayConstructor = Int32Array;
      break;
    case 'f32':
      outputArrayConstructor = Float32Array;
      break;
    case 'f16':
      outputArrayConstructor = Float16Array;
      break;
    case 'bool':
      outputArrayConstructor = Uint32Array;
      break;
  }
  t.expectGPUBufferValuesEqual(outputBuffer, new outputArrayConstructor(expectedValues));
}










const kSwizzleAssignmentCases = {
  // v = vec4u(1, 2, 3, 4)
  // v.w = 5;
  vec4u_w_literal: {
    elemType: 'u32',
    vecSize: 4,
    initial: [1, 2, 3, 4],
    swizzle: 'w',
    rhs: '5',
    expected: [1, 2, 3, 5]
  },
  // v = vec4u(1, 2, 3, 5)
  // v.xy = vec2u(6, 7);
  vec4u_xy_vec2u: {
    elemType: 'u32',
    vecSize: 4,
    initial: [1, 2, 3, 5],
    swizzle: 'xy',
    rhs: 'vec2u(6, 7)',
    expected: [6, 7, 3, 5]
  },
  // v = vec4u(6, 7, 3, 5)
  // v.zx = vec2u(8, 9);
  vec4u_zx_vec2u: {
    elemType: 'u32',
    vecSize: 4,
    initial: [6, 7, 3, 5],
    swizzle: 'zx',
    rhs: 'vec2u(8, 9)',
    expected: [9, 7, 8, 5]
  },
  // v = vec4u(1, 1, 1, 1)
  // v.xyzw = vec4u(10, 11, 12, 13);
  vec4u_xyzw_vec4u: {
    elemType: 'u32',
    vecSize: 4,
    initial: [1, 1, 1, 1],
    swizzle: 'xyzw',
    rhs: 'vec4u(10, 11, 12, 13)',
    expected: [10, 11, 12, 13]
  },
  // v = vec4u(10, 11, 12, 13)
  // v.xy = vec2(v.y, v.x);
  vec4u_xy_vec2_yx: {
    elemType: 'u32',
    vecSize: 4,
    initial: [10, 11, 12, 13],
    swizzle: 'xy',
    rhs: 'vec2(v.y, v.x)',
    expected: [11, 10, 12, 13]
  },
  // v = vec3i(-10, -20, -30)
  // v.y = 50;
  vec3i_y_literal: {
    elemType: 'i32',
    vecSize: 3,
    initial: [-10, -20, -30],
    swizzle: 'y',
    rhs: '-50',
    expected: [-10, -50, -30]
  },
  // v = vec3i(10, 20, 30)
  // v.zx = vec2i(40, 60);
  vec3i_zx_vec2i: {
    elemType: 'i32',
    vecSize: 3,
    initial: [10, 20, 30],
    swizzle: 'zx',
    rhs: 'vec2i(40, 60)',
    expected: [60, 20, 40]
  },
  // v = vec3f(1.0, 2.0, 3.0)
  // v.xy = vec2f(4.0, 5.0);
  vec3f_xy_vec2f: {
    elemType: 'f32',
    vecSize: 3,
    initial: [1.0, 2.0, 3.0],
    swizzle: 'xy',
    rhs: 'vec2f(4.0, 5.0)',
    expected: [4.0, 5.0, 3.0]
  },
  // v = vec2f(1.0, 2.0)
  // v.xy = v + v;
  vec2f_yx_v_plus_v: {
    elemType: 'f32',
    vecSize: 2,
    initial: [1.0, 2.0],
    swizzle: 'yx',
    rhs: 'v + v',
    expected: [4.0, 2.0]
  },
  // v = vec4f(10.0, 20.0, 30.0, 100.0)
  // v.rgb = vec3f(v.r, v.g, v.b) / 10;
  vec4f_rgb_vec3f_div_10: {
    elemType: 'f32',
    vecSize: 4,
    initial: [10.0, 20.0, 30.0, 100.0],
    swizzle: 'rgb',
    rhs: 'vec3f(v.r, v.g, v.b) / 10.0',
    expected: [1.0, 2.0, 3.0, 100.0]
  },
  // v = vec2h(1.0, 2.0)
  // v.yx = vec2h(4.0, 5.0);
  vec2h_yx_vec2h: {
    elemType: 'f16',
    vecSize: 2,
    initial: [1.0, 2.0],
    swizzle: 'yx',
    rhs: 'vec2h(4.0, 5.0)',
    expected: [5.0, 4.0]
  },
  // v = vec2<bool>(true, false)
  // v.y = true;
  vec2_bool_y_true: {
    elemType: 'bool',
    vecSize: 2,
    initial: [1, 0],
    swizzle: 'y',
    rhs: 'true',
    expected: [1, 1]
  },
  // v = vec3<bool>(true, true, true)
  // v.xz = vec2<bool>(false, false);
  vec3_bool_xz_vec2bool: {
    elemType: 'bool',
    vecSize: 3,
    initial: [1, 1, 1],
    swizzle: 'xz',
    rhs: 'vec2<bool>(false, false)',
    expected: [0, 1, 0]
  },
  // v = vec4u(1, 2, 3, 4)
  // v.xy.x = 5;
  vec4u_xy_x_literal: {
    elemType: 'u32',
    vecSize: 4,
    initial: [1, 2, 3, 4],
    swizzle: 'xy.x',
    rhs: '5',
    expected: [5, 2, 3, 4]
  },
  // v = vec3f(1.0, 2.0, 3.0)
  // v.zyx.yz = vec2f(5.0, 6.0);
  vec3f_zyx_yz_vec2f: {
    elemType: 'f32',
    vecSize: 3,
    initial: [1.0, 2.0, 3.0],
    swizzle: 'zyx.yz',
    rhs: 'vec2f(5.0, 6.0)',
    expected: [6.0, 5.0, 3.0]
  },
  // v = vec3i(-1, 0, -1)
  // v.xz.yx = vec2i(2);
  vec2i_xz_yx_vec2i: {
    elemType: 'i32',
    vecSize: 3,
    initial: [-1, 0, -1],
    swizzle: 'xz.yx',
    rhs: 'vec2i(2,3)',
    expected: [3, 0, 2]
  },
  // v = vec4u(1, 2, 3, 4)
  // v.xyz[0] = 5;
  vec4u_xyz_indexed_0_constant: {
    elemType: 'u32',
    vecSize: 4,
    initial: [1, 2, 3, 4],
    swizzle: 'xyz[0]',
    rhs: '5u',
    expected: [5, 2, 3, 4]
  },
  // v = vec3i(10, 20, 30)
  // v.zy[1] = 50;
  vec3i_zy_indexed_1_constant: {
    elemType: 'i32',
    vecSize: 3,
    initial: [10, 20, 30],
    swizzle: 'zy[1]',
    rhs: '50i',
    expected: [10, 50, 30]
  },
  // v = vec4f(1.0, 2.0, 3.0, 4.0)
  // v.yxzw[2] = 5.0;
  vec4f_yxzw_indexed_2_constant: {
    elemType: 'f32',
    vecSize: 4,
    initial: [1.0, 2.0, 3.0, 4.0],
    swizzle: 'yxzw[2]',
    rhs: '5.0f',
    expected: [1.0, 2.0, 5.0, 4.0]
  },
  // v = vec2h(1.0, 2.0)
  // v.yx[0] = 5.0;
  vec2h_yx_indexed_0_constant: {
    elemType: 'f16',
    vecSize: 2,
    initial: [1.0, 2.0],
    swizzle: 'yx[0]',
    rhs: '5.0h',
    expected: [1.0, 5.0]
  },
  // v = vec2<bool>(true, false)
  // v.yx[1] = true;
  vec2_bool_yx_indexed_1_constant: {
    elemType: 'bool',
    vecSize: 2,
    initial: [1, 0],
    swizzle: 'yx[1]',
    rhs: 'true',
    expected: [1, 0]
  },
  // v = vec3u(1, 2, 3)
  // v.xyy.xy = vec2u(4, 5);
  vec3u_chained_dup_lhs_pass: {
    elemType: 'u32',
    vecSize: 3,
    initial: [1, 2, 3],
    swizzle: 'xyy.xy',
    rhs: 'vec2u(4, 5)',
    expected: [4, 5, 3]
  },
  // v = vec2f(1.0, 2.0)
  // v.xx.x = 5.0;
  vec2f_chained_dup_lhs_pass: {
    elemType: 'f32',
    vecSize: 2,
    initial: [1.0, 2.0],
    swizzle: 'xx.x',
    rhs: '5.0f',
    expected: [5.0, 2.0]
  }
};

g.test('swizzle_assignment_vars').
desc(
  'Tests the value of a vector after swizzle assignment on different variable types, address spaces, and on pointer and reference memory views.'
).
params((u) =>
u.
combine('case', keysOf(kSwizzleAssignmentCases)).
beginSubcases().
combine('address_space', ['function', 'private', 'workgroup', 'storage']).
combine('memory_view', ['ref', 'ptr'])
).
fn((t) => {
  const { elemType, vecSize, initial, swizzle, rhs, expected } =
  kSwizzleAssignmentCases[t.params.case];

  t.skipIf(t.params.address_space === 'storage' && elemType === 'bool');
  if (t.params.memory_view === 'ptr') {
    t.skipIfLanguageFeatureNotSupported('pointer_composite_access');
  }

  const vecType = `vec${vecSize}<${elemType}>`;
  const initialValues =
  elemType === 'bool' ?
  initial.map((v) => v === 0 ? 'false' : 'true').join(', ') :
  initial.join(', ');
  const outputElemType = elemType === 'bool' ? 'u32' : elemType;

  const var_ref = t.params.address_space === 'storage' ? 'outputs.v' : 'v';
  const lhs =
  t.params.memory_view === 'ptr' ?
  `let ptr = &${var_ref}; ptr.${swizzle}` :
  `${var_ref}.${swizzle}`;
  const new_rhs = rhs.replaceAll(/\bv\b/g, `${var_ref}`);

  const wgsl = `
requires swizzle_assignment;
${t.params.memory_view === 'ptr' ? 'requires pointer_composite_access;' : ''}
${elemType === 'f16' ? 'enable f16;' : ''}

struct Outputs {
  ${t.params.address_space === 'storage' ? `v : ${vecType},` : ''}
  data : array<${outputElemType}>,
};

@group(0) @binding(1) var<storage, read_write> outputs : Outputs;

${
  t.params.address_space === 'private' || t.params.address_space === 'workgroup' ?
  `var<${t.params.address_space}> v : ${vecType};` :
  ''
  }

@compute @workgroup_size(1)
fn main() {

  ${t.params.address_space === 'function' ? `var v : ${vecType};` : ''}
  ${var_ref} = ${vecType}(${initialValues});
  ${lhs} = ${new_rhs};

  // Store result to Output
  for (var i = 0; i < ${vecSize}; i++) {
    ${
  elemType === 'bool' ?
  `outputs.data[i] = u32(${var_ref}[i]);` :
  `outputs.data[i] = ${var_ref}[i];`
  }
  }
}`;
  runSwizzleAssignmentTest(t, elemType, expected, wgsl);
});





const kSwizzleCompoundAssignmentCases = {
  // v = vec4u(1, 2, 3, 4)
  // v.w += 5;
  vec4u_w_add_5: {
    elemType: 'u32',
    vecSize: 4,
    initial: [1, 2, 3, 4],
    swizzle: 'w',
    op: '+=',
    rhs: '5',
    expected: [1, 2, 3, 9]
  },
  // v = vec4u(1, 2, 3, 4)
  // v.xy *= vec2u(6, 7);
  vec4u_xy_mul_vec2u: {
    elemType: 'u32',
    vecSize: 4,
    initial: [1, 2, 3, 4],
    swizzle: 'xy',
    op: '*=',
    rhs: 'vec2u(6, 7)',
    expected: [6, 14, 3, 4]
  },
  // v = vec3i(10, 20, 30)
  // v.zx += vec2i(100);
  vec3i_zx_add_vec2i: {
    elemType: 'i32',
    vecSize: 3,
    initial: [10, 20, 30],
    swizzle: 'zx',
    op: '+=',
    rhs: 'vec2i(100)',
    expected: [110, 20, 130]
  },
  // v = vec3f(1.0, 2.0, 3.0)
  // v.xy *= vec2f(0.5, 2.0);
  vec3f_xy_mul_vec2f: {
    elemType: 'f32',
    vecSize: 3,
    initial: [1.0, 2.0, 3.0],
    swizzle: 'xy',
    op: '*=',
    rhs: 'vec2f(0.5, 2.0)',
    expected: [0.5, 4.0, 3.0]
  },
  // v = vec4u(1, 2, 3, 4)
  // v.xyz[0] += 5;
  vec4u_xyz_indexed_0_add_5: {
    elemType: 'u32',
    vecSize: 4,
    initial: [1, 2, 3, 4],
    swizzle: 'xyz[0]',
    op: '+=',
    rhs: '5u',
    expected: [6, 2, 3, 4]
  },
  // v = vec3i(10, 20, 30)
  // v.zy[1] += 50;
  vec3i_zy_indexed_1_add_50: {
    elemType: 'i32',
    vecSize: 3,
    initial: [10, 20, 30],
    swizzle: 'zy[1]',
    op: '+=',
    rhs: '50i',
    expected: [10, 70, 30]
  },
  // v = vec4f(1.0, 2.0, 3.0, 4.0)
  // v.yxzw[2] *= 5.0;
  vec4f_yxzw_indexed_2_mul_5: {
    elemType: 'f32',
    vecSize: 4,
    initial: [1.0, 2.0, 3.0, 4.0],
    swizzle: 'yxzw[2]',
    op: '*=',
    rhs: '5.0f',
    expected: [1.0, 2.0, 15.0, 4.0]
  }
};

g.test('swizzle_compound_assignment').
desc('Tests the value of a vector after compound swizzle assignment.').
params((u) =>
u.
combine('case', keysOf(kSwizzleCompoundAssignmentCases)).
beginSubcases().
combine('address_space', ['function', 'private', 'workgroup', 'storage']).
combine('memory_view', ['ref', 'ptr'])
).
fn((t) => {
  const { elemType, vecSize, initial, swizzle, op, rhs, expected } =
  kSwizzleCompoundAssignmentCases[t.params.case];

  if (t.params.memory_view === 'ptr') {
    t.skipIfLanguageFeatureNotSupported('pointer_composite_access');
  }

  const vecType = `vec${vecSize}<${elemType}>`;
  const initialValues = initial.join(', ');

  const var_ref = t.params.address_space === 'storage' ? 'outputs.v' : 'v';
  const lhs =
  t.params.memory_view === 'ptr' ?
  `let ptr = &${var_ref}; ptr.${swizzle}` :
  `${var_ref}.${swizzle}`;
  const new_rhs = rhs.replaceAll(/\bv\b/g, `${var_ref}`);

  const wgsl = `
requires swizzle_assignment;
${t.params.memory_view === 'ptr' ? 'requires pointer_composite_access;' : ''}
${elemType === 'f16' ? 'enable f16;' : ''}

struct Outputs {
  ${t.params.address_space === 'storage' ? `v : ${vecType},` : ''}
  data : array<${elemType}>,
};

@group(0) @binding(1) var<storage, read_write> outputs : Outputs;

${
  t.params.address_space === 'private' || t.params.address_space === 'workgroup' ?
  `var<${t.params.address_space}> v : ${vecType};` :
  ''
  }

@compute @workgroup_size(1)
fn main() {

  ${t.params.address_space === 'function' ? `var v : ${vecType};` : ''}
  ${var_ref} = ${vecType}(${initialValues});
  ${lhs} ${op} ${new_rhs};

  // Store result to Output
  for (var i = 0; i < ${vecSize}; i++) {
    outputs.data[i] = ${var_ref}[i];
  }
}`;
  runSwizzleAssignmentTest(t, elemType, expected, wgsl);
});

g.test('eval_order').
desc(
  'Tests that the vec pointer on the lhs of a swizzle assignment is evaluated before the rhs, and the load of the lhs vec happens after rhs.'
).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  runFlowControlTest(t, (f) => ({
    entrypoint: `
  arr[0] = vec4u(1, 1, 1, 1);
  ${f.expect_order(0)}
  arr[foo()].xy = bar();
  ${f.expect_order(3)}
  if (all(arr[0] == vec4u(4, 5, 3, 8))) {
    ${f.expect_order(4)}
  } else {
    ${f.expect_not_reached()}
  }
`,
    extra: `
var<private> arr : array<vec4u, 1>;
fn foo() -> u32 {
  ${f.expect_order(1)}
  arr[0].x = 6;       // overwritten by swizzle
  arr[0].z = 7;       // overwritten by bar()
  arr[0].w = 8;       // persists
  return 0;
}
fn bar() -> vec2u {
  ${f.expect_order(2)}
  arr[0].z = 3;       // persists
  return vec2u(4, 5); // will set x,y
}
`
  }));
});

g.test('compound_eval_order').
desc(
  'Tests that the lhs of a swizzle compound assignment is evaluated before the rhs, and another load of the lhs vec happens after rhs evaluation, without re-evaluating the pointer to the lhs vec.'
).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  runFlowControlTest(t, (f) => ({
    entrypoint: `
  arr[0] = vec4u(1, 1, 1, 1);
  ${f.expect_order(0)}
  arr[foo()].xy += bar();
  ${f.expect_order(3)}
  if (all(arr[0] == vec4u(10, 6, 3, 8))) {
    ${f.expect_order(4)}
  } else {
    ${f.expect_not_reached()}
  }
`,
    extra: `
var<private> arr : array<vec4u, 1>;
fn foo() -> u32 {
  ${f.expect_order(1)}
  arr[0].x = 6;       // modifies x before add
  arr[0].z = 7;       // overwritten by bar()
  arr[0].w = 8;       // persists
  return 0;
}
fn bar() -> vec2u {
  ${f.expect_order(2)}
  arr[0].x = 2;       // no visible effect
  arr[0].z = 3;       // persists
  return vec2u(4, 5); // will add to x,y
}
`
  }));
});

g.test('dynamic_index_into_swizzle').
desc('Test dynamic array indexing into a swizzle view on LHS of assignment').
params((u) =>
u.
combine('elemType', ['u32', 'i32', 'f32', 'f16', 'bool']).
combine('vecSize', [2, 3, 4]).
combine('indexType', ['u32', 'i32']).
beginSubcases().
combine('address_space', ['function', 'private', 'workgroup', 'storage']).
combine('memory_view', ['ref', 'ptr']).
filter((p) => !(p.address_space === 'storage' && p.elemType === 'bool'))
).
fn((t) => {
  const { elemType, vecSize, indexType, address_space, memory_view } = t.params;

  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  if (elemType === 'f16') {
    t.skipIfDeviceDoesNotHaveFeature('shader-f16');
  }
  if (memory_view === 'ptr') {
    t.skipIfLanguageFeatureNotSupported('pointer_composite_access');
  }

  // Index 1 maps to x, for all vector size variations.
  const swizzle = 'yxzw'.substring(0, vecSize);

  const initialValues =
  elemType === 'bool' ?
  Array(vecSize).fill('false').join(', ') :
  [10, 20, 30, 40].slice(0, vecSize).join(', ');

  const rhsValue = elemType === 'bool' ? 'true' : '99';
  const expectedValues =
  elemType === 'bool' ? [1, 0, 0, 0].slice(0, vecSize) : [99, 20, 30, 40].slice(0, vecSize);

  const vecType = `vec${vecSize}<${elemType}>`;
  const outputElemType = elemType === 'bool' ? 'u32' : elemType;

  const indexSuffix = indexType === 'u32' ? 'u' : 'i';
  const indexDecl = `var idx = 1${indexSuffix};`;

  const var_ref = address_space === 'storage' ? 'outputs.v' : 'v';
  const lhs =
  memory_view === 'ptr' ?
  `let ptr = &${var_ref}; ptr.${swizzle}[idx]` :
  `${var_ref}.${swizzle}[idx]`;

  const wgsl = `
requires swizzle_assignment;
${memory_view === 'ptr' ? 'requires pointer_composite_access;' : ''}
${elemType === 'f16' ? 'enable f16;' : ''}

struct Outputs {
  ${address_space === 'storage' ? `v : ${vecType},` : ''}
  data : array<${outputElemType}>,
};

@group(0) @binding(1) var<storage, read_write> outputs : Outputs;

${
  address_space === 'private' || address_space === 'workgroup' ?
  `var<${address_space}> v : ${vecType};` :
  ''
  }

@compute @workgroup_size(1)
fn main() {
  ${indexDecl}
  ${address_space === 'function' ? `var v : ${vecType};` : ''}
  ${var_ref} = ${vecType}(${initialValues});
  ${lhs} = ${rhsValue};

  // Store result to Output
  for (var i = 0; i < ${vecSize}; i++) {
    outputs.data[i] = ${elemType === 'bool' ? `u32(${var_ref}[i])` : `${var_ref}[i]`};
  }
}`;

  runSwizzleAssignmentTest(t, elemType, expectedValues, wgsl);
});

g.test('indexed_swizzle_eval_order').
desc(
  'Tests that the lhs components of an indexed swizzle assignment are evaluated left-to-right (lhs pointer, then index, then rhs).'
).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  runFlowControlTest(t, (f) => ({
    entrypoint: `
  arr[0] = vec4u(1, 1, 1, 1);
  ${f.expect_order(0)}
  arr[foo()].xyz[bar()] = baz();
  ${f.expect_order(4)}
  if (all(arr[0] == vec4u(1, 99, 1, 1))) {
    ${f.expect_order(5)}
  } else {
    ${f.expect_not_reached()}
  }
`,
    extra: `
var<private> arr : array<vec4u, 1>;
fn foo() -> u32 {
  ${f.expect_order(1)}
  return 0;
}
fn bar() -> u32 {
  ${f.expect_order(2)}
  return 1;
}
fn baz() -> u32 {
  ${f.expect_order(3)}
  return 99;
}
`
  }));
});

g.test('indexed_swizzle_compound_eval_order').
desc(
  'Tests that the lhs components of an indexed swizzle compound assignment are evaluated left-to-right (lhs pointer, then index, then rhs).'
).
fn((t) => {
  t.skipIfLanguageFeatureNotSupported('swizzle_assignment');
  runFlowControlTest(t, (f) => ({
    entrypoint: `
  arr[0] = vec4u(1, 10, 1, 1);
  ${f.expect_order(0)}
  arr[foo()].xyz[bar()] += baz();
  ${f.expect_order(4)}
  if (all(arr[0] == vec4u(1, 15, 1, 1))) {
    ${f.expect_order(5)}
  } else {
    ${f.expect_not_reached()}
  }
`,
    extra: `
var<private> arr : array<vec4u, 1>;
fn foo() -> u32 {
  ${f.expect_order(1)}
  return 0;
}
fn bar() -> u32 {
  ${f.expect_order(2)}
  return 1;
}
fn baz() -> u32 {
  ${f.expect_order(3)}
  return 5;
}
`
  }));
});
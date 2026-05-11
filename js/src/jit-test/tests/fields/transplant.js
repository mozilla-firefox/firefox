class Base {
  constructor(o) {
    return o;
  }
}

class A extends Base {
  #x = 10;
  static gx(o) {
    return o.#x
  }
  static sx(o, v) {
    o.#x = v;
  }
}

function transplantTest(global) {
  var {object, transplant} = transplantableObject();

  new A(object);
  assertEq(A.gx(object), 10);
  A.sx(object, 15);
  assertEq(A.gx(object), 15);

  transplant(global);

  assertEq(A.gx(object), 15);
  A.sx(object, 29);
  assertEq(A.gx(object), 29);
}

const thisGlobal = this;
const otherGlobalSameCompartment = newGlobal({sameCompartmentAs: thisGlobal});
const otherGlobalNewCompartment = newGlobal({newCompartment: true});

const globals =
    [thisGlobal, otherGlobalSameCompartment, otherGlobalNewCompartment];

for (let global of globals) {
  transplantTest(global);
}

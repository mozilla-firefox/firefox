// Turn off concurrent marking zeal as it makes this test very slow.
unsetgczeal("ConcurrentMarking");

let v2 = [];
for (let v6 = 0; v6 < 550000; v6++) {
  v2[v6] = 0;
  delete v2[v6];
}

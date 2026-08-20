try {
  grayRoot()} catch {
}
newGlobal({newCompartment : true}).eval(`grayRoot()`);
gc();
gczeal(11);
while ((() => {
  v149 = gcstate();
  return v149 !="Sweep"})())
  gcslice(1000)

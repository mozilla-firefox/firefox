void *memcpy(void *dest, const void *src, unsigned n);

void foo(int **a, float **b) {
  memcpy((void**)a, (void**)b, sizeof(*a));
}

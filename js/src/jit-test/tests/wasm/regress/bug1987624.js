a = new ArrayBuffer(1 << 20)
b = new Proxy({}, {
    get(c, d) {
        if (d == 'builtins')
	  a.transfer(unescape)
    }
})
WebAssembly.validate(a, b)

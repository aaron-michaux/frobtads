
// Hints: https://www.w3.org/wiki/JavaScript_best_practices
// (1) Use `http://www.jslint.com/`
/*
// (2) Use modules with exports
myNameSpace = function(){
  var current = null;
  function init(){...}
  function change(){...}
  function verify(){...}
  return{
    init:init,
    set:change
  }
}();

myNameSpace.init(...);
myNameSpace.set(...);
*/

wasmFrob = function() {

    var asmLibraryArg = { "__cxa_atexit": ___cxa_atexit, "__handle_stack_overflow": ___handle_stack_overflow, "__map_file": ___map_file, "__sys_munmap": ___sys_munmap, "abort": _abort, "emscripten_get_sbrk_ptr": _emscripten_get_sbrk_ptr, "emscripten_longjmp": _emscripten_longjmp, "emscripten_memcpy_big": _emscripten_memcpy_big, "emscripten_resize_heap": _emscripten_resize_heap, "environ_get": _environ_get, "environ_sizes_get": _environ_sizes_get, "fd_close": _fd_close, "fd_seek": _fd_seek, "fd_write": _fd_write, "memory": wasmMemory, "setTempRet0": _setTempRet0, "table": wasmTable };
    
    // This is our recommended way of loading WebAssembly.
    function load() {
        console.log("load");

        var info = {
            'env': asmLibraryArg,
            'wasi_snapshot_preview1': asmLibraryArg
        };

        (async () => {
            console.log("creating fetch-promise");
            const data = await fetch('/frob.wasm');
            console.log("doing compile, with promise = " + data);
            const buffer = await data.arrayBuffer();
            const module = await WebAssembly.compile(buffer);
            console.log("instantiating");
            const instance = await WebAssembly.instantiate(module, info);

            // const { instance }
            //       = await WebAssembly.instantiateStreaming(fetchPromise, importObject);
            console.log("calculating result");
            const result = instance.exports.test_function(7, 3);
            console.log(result);
        })();
    }

    return { load:load }
}();

wasmFrob.load();

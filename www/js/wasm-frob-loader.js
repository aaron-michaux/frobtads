
// Hints: https://www.w3.org/wiki/JavaScript_best_practices

// called when the runtime is ready
var WasmTads = null;

var Module = {
    onRuntimeInitialized: function () {
        console.log('onRuntimeInitialized');
        
        // Final bit of glue for bindings
        WasmTads = function() {            

            function alloc_cstr_(s) {
                return allocate(intArrayFromString(s), 'i8', ALLOC_NORMAL);
            };
            
            function init_t3(t3_url) {                
                var ptr  = alloc_cstr_(t3_url);
                _init_t3(ptr);
                _free(ptr);
            }

            return {
                init_t3:init_t3
            }
        }();
    }
};

// called from main()
function onWasmTadsLoaded() {
    console.log('WASM Tads module is loaded');
    WasmTads.init_t3("MyGame.t3");
}


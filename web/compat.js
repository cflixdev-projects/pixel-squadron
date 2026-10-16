/* ---------------------------------------------------------------------------
   raylib 5.5 ships a miniaudio build whose WebAudio backend reaches for the
   heap views on the Module object (`Module.HEAPF32`). Emscripten 4 and newer
   only expose those views as plain globals, so on those toolchains the audio
   callback throws "Cannot read properties of undefined (reading 'buffer')"
   once per buffer and the game runs silent.

   Bridge the two with lazy getters. They are evaluated on each access, so
   they keep returning the current view after ALLOW_MEMORY_GROWTH reallocates
   the heap.
   --------------------------------------------------------------------------- */
(function () {
  if (typeof Module === 'undefined') return;

  var views = [
    'HEAP8', 'HEAPU8', 'HEAP16', 'HEAPU16', 'HEAP32', 'HEAPU32',
    'HEAPF32', 'HEAPF64', 'HEAP64', 'HEAPU64'
  ];

  for (var i = 0; i < views.length; i++) {
    (function (name) {
      if (Object.prototype.hasOwnProperty.call(Module, name)) return;
      try {
        Object.defineProperty(Module, name, {
          configurable: true,
          get: function () { return globalThis[name]; }
        });
      } catch (e) { /* non-fatal: worst case is the old behaviour */ }
    })(views[i]);
  }
})();

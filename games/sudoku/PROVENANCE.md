Canonical user-owned Sudoku engine handed off by Build an offline Sudoku generator.

Source SHA-256: e6c1324a58ab7ebe3521ede26cda78a179843ed86af47a7eb2a268438f0a642d.
Engine and calibration are preserved unchanged here. assets/sudoku/engine.js only removes ES-module export declarations and supplies performance.now from Date.now for the native JavaScriptCore script host. Generation runs away from the UI thread with explicit seeds; no network or Node installation is required by the app.

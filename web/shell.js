// PlaySuite page shell: configures the Emscripten module, mounts saves from
// IndexedDB at /state, then starts the program. ?game=<id> opens one game on
// the shelf; &standalone shows that game alone.
(function () {
  const status = document.getElementById('status');
  const statusText = document.getElementById('status-text');
  const progress = document.getElementById('status-progress');
  const params = new URLSearchParams(location.search);
  // Silent unless the page is opened with ?sound: every audio context the
  // program creates is held suspended, so nothing reaches the speakers.
  if (!params.has('sound')) {
    for (const name of ['AudioContext', 'webkitAudioContext']) {
      const Native = window[name];
      if (!Native) continue;
      window[name] = class extends Native {
        constructor(...options) { super(...options); super.suspend(); }
        resume() { return Promise.resolve(); }
      };
    }
  }
  const args = [];
  if (params.get('game')) {
    args.push('--game', params.get('game'));
    if (params.has('standalone')) args.push('--standalone');
  }
  function fail(message) {
    status.hidden = false;
    status.classList.add('failed');
    statusText.textContent = message;
  }
  if (!self.crossOriginIsolated) {
    fail('PlaySuite needs a cross-origin isolated page (COOP and COEP headers).');
    return;
  }
  let syncing = false, dirty = false;
  function persist() {
    if (syncing) { dirty = true; return; }
    syncing = true;
    Module.FS.syncfs(false, () => { syncing = false; if (dirty) { dirty = false; persist(); } });
  }
  window.Module = {
    guiFormsCanvas: 'gui-forms-canvas',
    // ?canvas2d presents with putImageData instead of WebGL2 (for comparison).
    guiFormsCanvas2D: params.has('canvas2d'),
    noInitialRun: true,
    print: (text) => console.log(text),
    printErr: (text) => console.warn(text),
    setStatus: (text) => {
      const match = /\((\d+(?:\.\d+)?)\/(\d+)\)/.exec(text);
      if (match) progress.value = Number(match[1]) / Number(match[2]);
    },
    preRun: [() => {
      Module.ENV.GAMES_ASSET_DIR = '/assets';
      Module.ENV.GAMES_STATE_DIR = '/state';
      Module.ENV.GUI_FORMS_FONT_DIR = '/gui_forms/fonts';
      Module.FS.mkdir('/state');
      Module.FS.mount(Module.IDBFS, {}, '/state');
      // ?trace-files logs each runtime asset the program opens (for packing
      // one game's files on its own).
      if (params.has('trace-files')) {
        const open = Module.FS.open;
        const seen = new Set();
        Module.FS.open = function (path, ...rest) {
          const name = typeof path === 'string' ? path : '';
          if (name.startsWith('/assets/') && !seen.has(name)) { seen.add(name); console.warn('asset-open ' + name); }
          return open.call(this, path, ...rest);
        };
      }
    }],
    onRuntimeInitialized: () => {
      statusText.textContent = 'Opening saves';
      Module.FS.syncfs(true, (error) => {
        if (error) console.warn('PlaySuite saves could not be read', error);
        status.hidden = true;
        // Saves are written by the program through ordinary files; mirror
        // them to IndexedDB every few seconds and when the page goes away.
        setInterval(persist, 4000);
        addEventListener('pagehide', persist);
        document.addEventListener('visibilitychange', () => { if (document.hidden) persist(); });
        Module.callMain(args);
      });
    },
    onAbort: (reason) => fail('PlaySuite stopped: ' + reason)
  };
  // The asset pack registers itself with Module before the program loads.
  function load(source, next) {
    const script = document.createElement('script');
    script.src = source;
    script.onload = next;
    script.onerror = () => fail('PlaySuite could not be loaded.');
    document.body.appendChild(script);
  }
  load('assets.js', () => load('games.js'));
})();

// SPDX-License-Identifier: AGPL-3.0-or-later
// Glue for the WebAssembly build (emcc --pre-js), around web/shell.html:
//  - /libsdl, SDL's pref folder (ALIFEPORT.INI and the saved games,
//    plat_save_dir), lives in IndexedDB (IDBFS). It is loaded before main
//    runs, and IDBFS's autoPersist writes it back whenever a file there is
//    closed or deleted.
//  - The game data is the package built in (AFTERLIFE_WASM_DATA, at /orig),
//    or else files the player picks once. Those are kept as blobs in an
//    IndexedDB store of their own (IDBFS can't hold ALIFE.000: browsers
//    cap a single serialized value below its 148 MB) and copied into
//    /userdata at every start. ?pick on the URL asks for them again.
//  - Arguments: the data folder, then each ?arg=... of the page's URL
//    (e.g. ?arg=ASCEN001.ASC&arg=--enhancements).
//  - When main returns (File > Quit) the page says so and offers a reload.

Module['arguments'] = Module['arguments'] || [];
Module['preRun'] = Module['preRun'] || [];

(function () {
  var PREF = '/libsdl', USER = '/userdata', BUILTIN = '/orig';
  var DATA_DB = 'afterlife-game-data', DATA_STORE = 'files';
  var params = new URLSearchParams(location.search);

  function hasGameData(dir) {
    try {
      return FS.readdir(dir).some(function (n) { return n.toUpperCase() === 'ALIFE.INI'; });
    } catch (e) {
      return false;
    }
  }

  // The files the port reads; anything else on the CD is skipped.
  function wanted(name) {
    var n = name.toUpperCase();
    return /\.000$/.test(n) || n === 'ALIFE.INI' || n === 'ALIFE.EXE' || /^ASCEN.*\.ASC$/.test(n);
  }

  // --- the picked files' store: name -> Blob ---------------------------
  function openDataDb(cb) {
    var req;
    try {
      req = indexedDB.open(DATA_DB, 1);
    } catch (e) {
      return cb(null);
    }
    req.onupgradeneeded = function () { req.result.createObjectStore(DATA_STORE); };
    req.onsuccess = function () { cb(req.result); };
    req.onerror = function () {
      console.warn('Afterlife: no IndexedDB for the game data', req.error);
      cb(null);
    };
  }

  // Copies the stored files into dir; cb() when done (also on failure).
  function loadStoredData(dir, cb) {
    openDataDb(function (db) {
      if (!db)
        return cb();
      var store = db.transaction(DATA_STORE, 'readonly').objectStore(DATA_STORE);
      var keys = store.getAllKeys(), vals = store.getAll();
      vals.onsuccess = function () {
        var names = keys.result, blobs = vals.result, left = blobs.length;
        db.close();
        if (!left)
          return cb();
        Module['setStatus']('Loading game data...');
        blobs.forEach(function (blob, i) {
          blob.arrayBuffer().then(function (buf) {
            FS.writeFile(dir + '/' + names[i], new Uint8Array(buf));
          }, function (e) {
            console.warn('Afterlife: reading stored ' + names[i] + ' failed', e);
          }).then(function () {
            if (--left === 0)
              cb();
          });
        });
      };
      vals.onerror = function () {
        db.close();
        cb();
      };
    });
  }

  function storeData(files, cb) {
    openDataDb(function (db) {
      if (!db)
        return cb();
      var tx = db.transaction(DATA_STORE, 'readwrite');
      var store = tx.objectStore(DATA_STORE);
      store.clear();
      files.forEach(function (f) { store.put(f, f.name); });
      tx.oncomplete = function () {
        db.close();
        cb();
      };
      tx.onerror = tx.onabort = function () {
        console.warn('Afterlife: storing the game data failed (it will be asked for again)', tx.error);
        db.close();
        cb();
      };
    });
  }

  // Shows the picker of web/shell.html; puts the files into dir.
  function pickData(dir, done) {
    Module['setStatus']('');
    var panel = document.getElementById('pick');
    var error = document.getElementById('pick-error');
    panel.hidden = false;
    function take(list) {
      var files = Array.prototype.filter.call(list, function (f) { return wanted(f.name); });
      if (!files.some(function (f) { return f.name.toUpperCase() === 'ALIFE.INI'; }) ||
          !files.some(function (f) { return /\.000$/i.test(f.name); })) {
        error.textContent = 'ALIFE.INI and at least one .000 bundle are needed.';
        return;
      }
      panel.hidden = true;
      Module['setStatus']('Storing game data...');
      var left = files.length;
      files.forEach(function (f) {
        f.arrayBuffer().then(function (buf) {
          FS.writeFile(dir + '/' + f.name, new Uint8Array(buf));
          if (--left === 0)
            storeData(files, done);
        });
      });
    }
    document.getElementById('pick-files').onchange = function (e) { take(e.target.files); };
    document.getElementById('pick-dir').onchange = function (e) { take(e.target.files); };
  }

  Module['preRun'].push(function () {
    FS.mkdir(PREF);
    FS.mount(IDBFS, { autoPersist: true }, PREF);
    if (!IDBFS.queuePersist) {
      // an Emscripten without autoPersist (before 4.0.11): write back now and then
      setInterval(function () { FS.syncfs(false, function () {}); }, 3000);
      Module['onExit'] = (function (onExit) {
        return function (status) {
          FS.syncfs(false, function () {});
          onExit(status);
        };
      })(Module['onExit']);
    }
    FS.mkdir(USER);
    addRunDependency('afterlife-storage');
    Module['setStatus']('Loading saved games...');
    function start(dir) {
      var args = Module['arguments'];
      args.push(dir);
      params.getAll('arg').forEach(function (a) { args.push(a); });
      Module['setStatus']('');
      removeRunDependency('afterlife-storage');
    }
    FS.syncfs(true, function (err) {
      if (err)
        console.warn('Afterlife: reading browser storage failed', err);
      if (hasGameData(BUILTIN))
        return start(BUILTIN);
      if (params.has('pick'))
        return pickData(USER, function () { start(USER); });
      loadStoredData(USER, function () {
        if (hasGameData(USER))
          start(USER);
        else
          pickData(USER, function () { start(USER); });
      });
    });
  });

  Module['onExit'] = function (status) {
    document.getElementById('ended').hidden = false;
    if (status)
      document.getElementById('ended-text').textContent = 'Afterlife has ended (exit code ' + status + ').';
  };
  Module['onAbort'] = function (what) {
    document.getElementById('ended').hidden = false;
    document.getElementById('ended-text').textContent = 'Afterlife has crashed: ' + what;
  };
})();

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
//    (e.g. ?arg=ASCEN001.ASC&arg=--no-enhancements).
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

  // A long step's bar: its label with the percentage, and the share done.
  function progress(label, done, total) {
    var share = total ? Math.min(done / total, 1) : 0;
    Module['setStatus'](label + ' ' + Math.floor(share * 100) + '%');
    Module['setProgress'](share);
  }

  // The bytes of a Blob, as Uint8Array; onBytes(n) is called as chunks arrive.
  function readBlob(blob, onBytes) {
    if (!blob.stream) {
      return blob.arrayBuffer().then(function (buf) {
        onBytes(buf.byteLength);
        return new Uint8Array(buf);
      });
    }
    var out = new Uint8Array(blob.size), at = 0, reader = blob.stream().getReader();
    function pump() {
      return reader.read().then(function (r) {
        if (r.done)
          return out;
        out.set(r.value, at);
        at += r.value.byteLength;
        onBytes(r.value.byteLength);
        return pump();
      });
    }
    return pump();
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
        var total = blobs.reduce(function (sum, b) { return sum + b.size; }, 0), done = 0;
        progress('Loading game data...', 0, total);
        blobs.forEach(function (blob, i) {
          readBlob(blob, function (n) {
            done += n;
            progress('Loading game data...', done, total);
          }).then(function (bytes) {
            FS.writeFile(dir + '/' + names[i], bytes);
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

  function storeData(files, total, cb) {
    openDataDb(function (db) {
      if (!db)
        return cb();
      var tx = db.transaction(DATA_STORE, 'readwrite');
      var store = tx.objectStore(DATA_STORE);
      var done = 0;
      store.clear();
      progress('Storing game data...', 0, total);
      files.forEach(function (f) {
        store.put(f, f.name).onsuccess = function () {
          done += f.size;
          progress('Storing game data...', done, total);
        };
      });
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
      var image = Array.prototype.find.call(list, function (f) { return /\.(zip|iso|bin|img)$/i.test(f.name); });
      if (image) {
        // a CD image or a zip: its game files are read out of it (web/iso9660.js, web/zip.js)
        error.textContent = '';
        panel.hidden = true;
        Module['setStatus']('Reading the CD...');
        var read = /\.zip$/i.test(image.name) ?
          Zip.extract(image, wanted, function (done, total) { progress('Reading the CD...', done, total); }) :
          Iso9660.extract(image, wanted);
        read.then(function (files) {
          use(files);
        }, function (e) {
          Module['setStatus']('');
          panel.hidden = false;
          error.textContent = e.message;
        });
        return;
      }
      use(Array.prototype.filter.call(list, function (f) { return wanted(f.name); }));
    }
    function use(files) {
      if (!files.some(function (f) { return f.name.toUpperCase() === 'ALIFE.INI'; }) ||
          !files.some(function (f) { return /\.000$/i.test(f.name); })) {
        Module['setStatus']('');
        panel.hidden = false;
        error.textContent = 'ALIFE.INI and at least one .000 bundle are needed.';
        return;
      }
      panel.hidden = true;
      var total = files.reduce(function (sum, f) { return sum + f.size; }, 0), copied = 0;
      var left = files.length;
      progress('Reading the game files...', 0, total);
      files.forEach(function (f) {
        readBlob(f, function (n) {
          copied += n;
          progress('Reading the game files...', copied, total);
        }).then(function (bytes) {
          FS.writeFile(dir + '/' + f.name, bytes);
          if (--left === 0)
            storeData(files, total, done);
        });
      });
    }
    document.getElementById('pick-files').onchange = function (e) { take(e.target.files); };
    document.getElementById('pick-dir').onchange = function (e) { take(e.target.files); };
    document.getElementById('pick-image').onchange = function (e) { take(e.target.files); };
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
      if (Module['expectedDataFileDownloads']) {
        // A build with AFTERLIFE_WASM_DATA: its package is still being
        // fetched (IDBFS is usually done first), so wait for /orig to fill.
        (function wait() {
          if (hasGameData(BUILTIN))
            start(BUILTIN);
          else
            setTimeout(wait, 50);
        })();
        return;
      }
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

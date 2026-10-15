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

  function hasGameData(dir: string): boolean {
    try {
      return FS.readdir(dir).some(function (n) { return n.toUpperCase() === 'ALIFE.INI'; });
    } catch (e) {
      return false;
    }
  }

  // The files the port reads; anything else on the CD is skipped.
  function wanted(name: string): boolean {
    var n = name.toUpperCase();
    return /\.000$/.test(n) || n === 'ALIFE.INI' || n === 'ALIFE.EXE' || /^ASCEN.*\.ASC$/.test(n);
  }

  // A long step's bar: its label with the percentage, and the share done.
  function progress(label: string, done: number, total: number): void {
    var share = total ? Math.min(done / total, 1) : 0;
    Module['setStatus'](label + ' ' + Math.floor(share * 100) + '%');
    Module['setProgress'](share);
  }

  // The bytes of a Blob, as Uint8Array; onBytes(n) is called as chunks arrive.
  function readBlob(blob: Blob, onBytes: (n: number) => void): Promise<Uint8Array> {
    if (!blob.stream) {
      return blob.arrayBuffer().then(function (buf) {
        onBytes(buf.byteLength);
        return new Uint8Array(buf);
      });
    }
    var out = new Uint8Array(blob.size), at = 0, reader = blob.stream().getReader();
    function pump(): Promise<Uint8Array> {
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
  function openDataDb(cb: (db: IDBDatabase | null) => void): void {
    var req: IDBOpenDBRequest;
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
  function loadStoredData(dir: string, cb: () => void): void {
    openDataDb(function (db) {
      if (!db)
        return cb();
      var store = db.transaction(DATA_STORE, 'readonly').objectStore(DATA_STORE);
      var keys = store.getAllKeys(), vals = store.getAll();
      vals.onsuccess = function () {
        var names = keys.result, blobs: Blob[] = vals.result, left = blobs.length;
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

  function storeData(files: File[], total: number, cb: () => void): void {
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
  function pickData(dir: string, done: () => void): void {
    Module['setStatus']('');
    var panel = document.getElementById('pick') as HTMLElement;
    var error = document.getElementById('pick-error') as HTMLElement;
    panel.hidden = false;
    function take(list: FileList): void {
      var image = Array.prototype.find.call(list, function (f: File) { return /\.(zip|iso|bin|img)$/i.test(f.name); }) as File | undefined;
      if (image) {
        // a CD image or a zip: its game files are read out of it (web/iso9660.ts, web/zip.ts)
        error.textContent = '';
        panel.hidden = true;
        Module['setStatus']('Reading the CD...');
        var read = /\.zip$/i.test(image.name) ?
          Zip.extract(image, wanted, function (done, total) { progress('Reading the CD...', done, total); }) :
          Iso9660.extract(image, wanted);
        read.then(function (files) {
          use(files);
        }, function (e: Error) {
          Module['setStatus']('');
          panel.hidden = false;
          error.textContent = e.message;
        });
        return;
      }
      use(Array.prototype.filter.call(list, function (f: File) { return wanted(f.name); }) as File[]);
    }
    function use(files: File[]): void {
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
    (document.getElementById('pick-files') as HTMLInputElement).onchange = function (e) { take((e.target as HTMLInputElement).files as FileList); };
    (document.getElementById('pick-dir') as HTMLInputElement).onchange = function (e) { take((e.target as HTMLInputElement).files as FileList); };
    (document.getElementById('pick-image') as HTMLInputElement).onchange = function (e) { take((e.target as HTMLInputElement).files as FileList); };
  }

  Module['preRun'].push(function () {
    FS.mkdir(PREF);
    FS.mount(IDBFS, { autoPersist: true }, PREF);
    if (!IDBFS.queuePersist) {
      // an Emscripten without autoPersist (before 4.0.11): write back now and then
      setInterval(function () { FS.syncfs(false, function () {}); }, 3000);
      Module['onExit'] = (function (onExit) {
        return function (status: number) {
          FS.syncfs(false, function () {});
          onExit(status);
        };
      })(Module['onExit']);
    }
    FS.mkdir(USER);
    addRunDependency('afterlife-storage');
    Module['setStatus']('Loading saved games...');
    function start(dir: string): void {
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

  Module['onExit'] = function (status: number) {
    (document.getElementById('ended') as HTMLElement).hidden = false;
    if (status)
      (document.getElementById('ended-text') as HTMLElement).textContent = 'Afterlife has ended (exit code ' + status + ').';
  };
  Module['onAbort'] = function (what: string) {
    (document.getElementById('ended') as HTMLElement).hidden = false;
    (document.getElementById('ended-text') as HTMLElement).textContent = 'Afterlife has crashed: ' + what;
  };
})();

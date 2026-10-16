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

Module.arguments ??= [];
Module.preRun ??= [];

(() => {
  const PREF = '/libsdl', USER = '/userdata', BUILTIN = '/orig';
  const DATA_DB = 'afterlife-game-data', DATA_STORE = 'files';
  const params = new URLSearchParams(location.search);

  // An element of web/shell.html, which must be there.
  function element<T extends HTMLElement>(id: string): T {
    const e = document.getElementById(id);
    if (!e)
      throw new Error(`Afterlife: no #${id} in the page`);
    return e as T;
  }

  function hasGameData(dir: string): boolean {
    try {
      return FS.readdir(dir).some((n) => n.toUpperCase() === 'ALIFE.INI');
    } catch {
      return false;
    }
  }

  // The files the port reads; anything else on the CD is skipped.
  function wanted(name: string): boolean {
    const n = name.toUpperCase();
    return /\.000$/.test(n) || n === 'ALIFE.INI' || n === 'ALIFE.EXE' || /^ASCEN.*\.ASC$/.test(n);
  }

  // A long step's bar: its label with the percentage, and the share done.
  function progress(label: string, done: number, total: number): void {
    const share = total ? Math.min(done / total, 1) : 0;
    Module.setStatus(`${label} ${Math.floor(share * 100)}%`);
    Module.setProgress(share);
  }

  // Copies a Blob into the file at path as its chunks arrive, so the file grows
  // with the copy (no whole-file buffer, and no long synchronous write at the end).
  // onBytes(n) is called per chunk written.
  async function copyBlob(blob: Blob, path: string, onBytes: (n: number) => void): Promise<void> {
    if (!blob.stream) {
      const buf = await blob.arrayBuffer();
      FS.writeFile(path, new Uint8Array(buf));
      onBytes(buf.byteLength);
      return;
    }
    const stream = FS.open(path, 'w'), reader = blob.stream().getReader();
    try {
      let at = 0;
      for (;;) {
        const r = await reader.read();
        if (r.done)
          break;
        FS.write(stream, r.value, 0, r.value.byteLength, at);
        at += r.value.byteLength;
        onBytes(r.value.byteLength);
      }
    } finally {
      FS.close(stream);
    }
  }

  // --- the picked files' store: name -> Blob ---------------------------
  function openDataDb(cb: (db: IDBDatabase | null) => void): void {
    let req: IDBOpenDBRequest;
    try {
      req = indexedDB.open(DATA_DB, 1);
    } catch {
      return cb(null);
    }
    req.onupgradeneeded = () => { req.result.createObjectStore(DATA_STORE); };
    req.onsuccess = () => cb(req.result);
    req.onerror = () => {
      console.warn('Afterlife: no IndexedDB for the game data', req.error);
      cb(null);
    };
  }

  // Copies the stored files into dir; cb() when done (also on failure).
  function loadStoredData(dir: string, cb: () => void): void {
    openDataDb((db) => {
      if (!db)
        return cb();
      const store = db.transaction(DATA_STORE, 'readonly').objectStore(DATA_STORE);
      const keys = store.getAllKeys(), vals = store.getAll();
      vals.onsuccess = () => {
        const names = keys.result, blobs: Blob[] = vals.result;
        db.close();
        if (!blobs.length)
          return cb();
        const total = blobs.reduce((sum, b) => sum + b.size, 0);
        let done = 0, i = 0;
        progress('Loading game data...', 0, total);
        // one file at a time, so the bar shows the copy into /userdata
        const next = (): void => {
          if (i === blobs.length)
            return cb();
          const k = i++;
          copyBlob(blobs[k], `${dir}/${names[k]}`, (n) => {
            done += n;
            progress('Loading game data...', done, total);
          }).catch((e) => {
            console.warn(`Afterlife: reading stored ${names[k]} failed`, e);
          }).then(next);
        };
        next();
      };
      vals.onerror = () => {
        db.close();
        cb();
      };
    });
  }

  function storeData(files: File[], total: number, cb: () => void): void {
    openDataDb((db) => {
      if (!db)
        return cb();
      const tx = db.transaction(DATA_STORE, 'readwrite');
      const store = tx.objectStore(DATA_STORE);
      let done = 0;
      store.clear();
      progress('Storing game data...', 0, total);
      files.forEach((f) => {
        store.put(f, f.name).onsuccess = () => {
          done += f.size;
          progress('Storing game data...', done, total);
        };
      });
      tx.oncomplete = () => {
        db.close();
        cb();
      };
      tx.onerror = tx.onabort = () => {
        console.warn('Afterlife: storing the game data failed (it will be asked for again)', tx.error);
        db.close();
        cb();
      };
    });
  }

  // Shows the picker of web/shell.html; puts the files into dir.
  function pickData(dir: string, done: () => void): void {
    Module.setStatus('');
    const panel = element('pick');
    const error = element('pick-error');
    panel.hidden = false;
    function take(list: File[]): void {
      const image = list.find((f) => /\.(zip|iso|bin|img)$/i.test(f.name));
      if (image) {
        // a CD image or a zip: its game files are read out of it (web/iso9660.ts, web/zip.ts)
        error.textContent = '';
        panel.hidden = true;
        Module.setStatus('Reading the CD...');
        const read = /\.zip$/i.test(image.name) ?
          Zip.extract(image, wanted, (d, t) => progress('Reading the CD...', d, t)) :
          Iso9660.extract(image, wanted);
        read.then(use, (e) => {
          Module.setStatus('');
          panel.hidden = false;
          error.textContent = e.message;
        });
        return;
      }
      use(list.filter((f) => wanted(f.name)));
    }
    function use(files: File[]): void {
      if (!files.some((f) => f.name.toUpperCase() === 'ALIFE.INI') ||
          !files.some((f) => /\.000$/i.test(f.name))) {
        Module.setStatus('');
        panel.hidden = false;
        error.textContent = 'ALIFE.INI and at least one .000 bundle are needed.';
        return;
      }
      panel.hidden = true;
      const total = files.reduce((sum, f) => sum + f.size, 0);
      let copied = 0, i = 0;
      progress('Reading the game files...', 0, total);
      const next = (): void => {
        if (i === files.length)
          return storeData(files, total, done);
        const f = files[i++];
        copyBlob(f, `${dir}/${f.name}`, (n) => {
          copied += n;
          progress('Reading the game files...', copied, total);
        }).then(next, (e) => {
          Module.setStatus('');
          panel.hidden = false;
          error.textContent = `Reading ${f.name} failed: ${e.message}`;
        });
      };
      next();
    }
    // the picker's inputs: the files, a folder, or a CD image
    for (const id of ['pick-files', 'pick-dir', 'pick-image']) {
      const input = element<HTMLInputElement>(id);
      input.onchange = () => take(Array.from(input.files ?? []));
    }
  }

  Module.preRun.push(() => {
    FS.mkdir(PREF);
    FS.mount(IDBFS, { autoPersist: true }, PREF);
    if (!IDBFS.queuePersist) {
      // an Emscripten without autoPersist (before 4.0.11): write back now and then
      setInterval(() => FS.syncfs(false, () => {}), 3000);
      const onExit = Module.onExit;
      Module.onExit = (status) => {
        FS.syncfs(false, () => {});
        onExit(status);
      };
    }
    FS.mkdir(USER);
    addRunDependency('afterlife-storage');
    Module.setStatus('Loading saved games...');
    const start = (dir: string): void => {
      Module.arguments.push(dir);
      params.getAll('arg').forEach((a) => Module.arguments.push(a));
      Module.setStatus('');
      removeRunDependency('afterlife-storage');
    };
    FS.syncfs(true, (err) => {
      if (err)
        console.warn('Afterlife: reading browser storage failed', err);
      if (Module.expectedDataFileDownloads) {
        // A build with AFTERLIFE_WASM_DATA: its package is still being
        // fetched (IDBFS is usually done first), so wait for /orig to fill.
        const wait = () => {
          if (hasGameData(BUILTIN))
            start(BUILTIN);
          else
            setTimeout(wait, 50);
        };
        wait();
        return;
      }
      if (params.has('pick'))
        return pickData(USER, () => start(USER));
      loadStoredData(USER, () => {
        if (hasGameData(USER))
          start(USER);
        else
          pickData(USER, () => start(USER));
      });
    });
  });

  Module.onExit = (status) => {
    element('ended').hidden = false;
    if (status)
      element('ended-text').textContent = `Afterlife has ended (exit code ${status}).`;
  };
  Module.onAbort = (what) => {
    element('ended').hidden = false;
    element('ended-text').textContent = `Afterlife has crashed: ${what}`;
  };
})();

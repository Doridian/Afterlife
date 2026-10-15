// SPDX-License-Identifier: AGPL-3.0-or-later
// Reads the game files out of a CD image in the browser, for the picker of
// web/pre.ts: an .iso (2048-byte sectors) or a raw .bin (2352-byte sectors, as
// Redump has it; mode 1 or mode 2 form 1). Only the ISO 9660 file system of the
// data track is read: the directory tree and the file extents. The image is
// never loaded whole, the files are Blobs that point into it.
//
// Iso9660.extract(file, wanted) -> Promise<File[]>, one File per file whose
// name (without the ";1" version) passes wanted(name).
const Iso9660 = (() => {
  const SECTOR = 2048;
  const RAW = [
    { size: 2352, ud: 16 }, // raw mode 1
    { size: 2352, ud: 24 }, // raw mode 2 form 1 (sub-header in front)
  ];

  interface Layout {
    size: number; // bytes per sector in the image
    ud: number; // offset of the user data in a sector
  }

  // The entries of a directory's data: { lba, size, dir, name }.
  interface Entry {
    lba: number;
    size: number;
    dir: boolean;
    name: string;
  }

  function bytes(blob: Blob, start: number, len: number): Promise<DataView> {
    return blob.slice(start, start + len).arrayBuffer().then((buf) => new DataView(buf));
  }

  function isVolume(dv: DataView): boolean {
    // a volume descriptor: type 1 (primary) and the "CD001" identifier
    if (dv.byteLength < 6)
      return false;
    let id = '';
    for (let i = 1; i < 6; i++)
      id += String.fromCharCode(dv.getUint8(i));
    return dv.getUint8(0) === 1 && id === 'CD001';
  }

  // The image's sector layout: the first volume descriptor is at sector 16.
  function layout(file: Blob): Promise<Layout> {
    const tries: Layout[] = [{ size: SECTOR, ud: 0 }, ...RAW];
    return tries.reduce<Promise<Layout | null>>((prev, lay) => prev.then((found) => {
      if (found)
        return found;
      return bytes(file, 16 * lay.size + lay.ud, 6).then((dv) => (isVolume(dv) ? lay : null));
    }), Promise.resolve(null)).then((lay) => {
      if (!lay)
        throw new Error('This is not a CD image (no ISO 9660 file system).');
      return lay;
    });
  }

  // The Blob parts of the user data of size bytes starting at sector lba.
  function parts(file: Blob, lay: Layout, lba: number, size: number): Blob[] {
    if (lay.size === SECTOR)
      return [file.slice(lba * SECTOR, lba * SECTOR + size)];
    const out: Blob[] = [];
    for (let off = 0; off < size; off += SECTOR) {
      const at = (lba + off / SECTOR) * lay.size + lay.ud;
      out.push(file.slice(at, at + Math.min(SECTOR, size - off)));
    }
    return out;
  }

  function entries(dv: DataView): Entry[] {
    const out: Entry[] = [];
    let pos = 0;
    while (pos < dv.byteLength) {
      const len = dv.getUint8(pos);
      if (len === 0) {
        // records don't cross sectors: the rest of this one is padding
        pos = (Math.floor(pos / SECTOR) + 1) * SECTOR;
        continue;
      }
      const nlen = dv.getUint8(pos + 32);
      if (nlen === 1 && dv.getUint8(pos + 33) < 2) {
        // "." and ".."
        pos += len;
        continue;
      }
      let name = '';
      for (let i = 0; i < nlen; i++)
        name += String.fromCharCode(dv.getUint8(pos + 33 + i));
      out.push({
        lba: dv.getUint32(pos + 2, true),
        size: dv.getUint32(pos + 10, true),
        dir: !!(dv.getUint8(pos + 25) & 2),
        name: name.replace(/;\d+$/, '').replace(/\.$/, ''),
      });
      pos += len;
    }
    return out;
  }

  function readDir(file: Blob, lay: Layout, lba: number, size: number): Promise<Entry[]> {
    return new Blob(parts(file, lay, lba, size)).arrayBuffer().then((buf) => entries(new DataView(buf)));
  }

  // Walks the tree below (lba, size), adding the wanted files to found.
  function walk(file: Blob, lay: Layout, lba: number, size: number,
      wanted: (name: string) => boolean, found: File[], depth: number): Promise<void> {
    return readDir(file, lay, lba, size).then((list) => list.reduce<Promise<void>>((prev, e) => prev.then(() => {
      if (e.dir) {
        if (depth < 8 && e.size)
          return walk(file, lay, e.lba, e.size, wanted, found, depth + 1);
      } else if (wanted(e.name)) {
        const blob = new Blob(parts(file, lay, e.lba, e.size));
        found.push(new File([blob], e.name));
      }
    }), Promise.resolve()));
  }

  function extract(file: Blob, wanted: (name: string) => boolean): Promise<File[]> {
    return layout(file).then((lay) =>
      // the root directory record is at offset 156 of the descriptor
      bytes(file, 16 * lay.size + lay.ud + 156, 34).then((dv) => {
        const root = { lba: dv.getUint32(2, true), size: dv.getUint32(10, true) };
        const found: File[] = [];
        return walk(file, lay, root.lba, root.size, wanted, found, 0).then(() => found);
      }));
  }

  return { extract };
})();

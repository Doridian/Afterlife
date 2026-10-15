// SPDX-License-Identifier: AGPL-3.0-or-later
// Reads a .zip in the browser, for the picker of web/pre.ts: the zip is the
// archive.org download of the CD (a .bin inside), or a zip of the CD's files.
// Stored and deflated entries are read; deflate uses DecompressionStream.
// Zip64 (4 GB and up) is not handled.
//
// Zip.extract(file, wanted, progress) -> Promise<File[]>: the wanted files of
// the zip, and the wanted files of any CD image (.bin, .iso, .img) it holds
// (read by web/iso9660.ts). progress(done, total) is optional (bytes read).
var Zip = (function () {
  var EOCD = 0x06054b50, CENTRAL = 0x02014b50, LOCAL = 0x04034b50;

  // The entries of the central directory: { name, method, csize, offset }.
  interface Entry {
    name: string;
    method: number;
    csize: number;
    offset: number;
  }

  function bytes(blob: Blob, start: number, len: number): Promise<DataView> {
    return blob.slice(start, start + len).arrayBuffer().then(function (buf) {
      return new DataView(buf);
    });
  }

  function text(dv: DataView, start: number, len: number): string {
    var s = '';
    for (var i = 0; i < len; i++)
      s += String.fromCharCode(dv.getUint8(start + i));
    return s;
  }

  function directory(file: Blob): Promise<Entry[]> {
    var tail = Math.min(file.size, 65557); // the end record plus a full comment
    return bytes(file, file.size - tail, tail).then(function (dv) {
      var eocd = -1;
      for (var i = dv.byteLength - 22; i >= 0; i--) {
        if (dv.getUint32(i, true) === EOCD) {
          eocd = i;
          break;
        }
      }
      if (eocd < 0)
        throw new Error('This is not a zip file.');
      var count = dv.getUint16(eocd + 10, true);
      var size = dv.getUint32(eocd + 12, true);
      var offset = dv.getUint32(eocd + 16, true);
      if (count === 0xffff || size === 0xffffffff || offset === 0xffffffff)
        throw new Error('This zip is too big for the browser (zip64).');
      return bytes(file, offset, size).then(function (cd) {
        var out: Entry[] = [], pos = 0;
        for (var n = 0; n < count; n++) {
          if (cd.getUint32(pos, true) !== CENTRAL)
            throw new Error('The zip directory is damaged.');
          var nlen = cd.getUint16(pos + 28, true);
          out.push({
            name: text(cd, pos + 46, nlen),
            method: cd.getUint16(pos + 10, true),
            csize: cd.getUint32(pos + 20, true),
            offset: cd.getUint32(pos + 42, true),
          });
          pos += 46 + nlen + cd.getUint16(pos + 30, true) + cd.getUint16(pos + 32, true);
        }
        return out;
      });
    });
  }

  // The file of one entry: its data is at the local header's name and extra.
  // onBytes(n) gets the compressed bytes of the entry as they are read.
  function member(file: Blob, e: Entry, onBytes: (n: number) => void): Promise<File> {
    return bytes(file, e.offset, 30).then(function (lh) {
      if (lh.getUint32(0, true) !== LOCAL)
        throw new Error('The zip is damaged.');
      var start = e.offset + 30 + lh.getUint16(26, true) + lh.getUint16(28, true);
      var data = file.slice(start, start + e.csize);
      var base = e.name.split('/').pop() as string;
      if (e.method === 0) {
        onBytes(e.csize);
        return new File([data], base);
      }
      if (e.method !== 8)
        throw new Error('This zip uses a compression the browser cannot read; unzip it instead.');
      if (typeof DecompressionStream === 'undefined')
        throw new Error('This browser cannot unzip; unzip the file and choose the .bin instead.');
      var counted: ReadableStream<BufferSource> = data.stream().pipeThrough(new TransformStream<BufferSource, BufferSource>({
        transform: function (chunk, out) {
          onBytes(chunk.byteLength);
          out.enqueue(chunk);
        },
      }));
      return new Response(counted.pipeThrough(new DecompressionStream('deflate-raw'))).blob()
        .then(function (blob) {
          return new File([blob], base);
        });
    });
  }

  // progress(done, total), if given, counts the compressed bytes of the entries read.
  function extract(file: Blob, wanted: (name: string) => boolean,
      progress?: (done: number, total: number) => void): Promise<File[]> {
    return directory(file).then(function (entries) {
      var found: File[] = [];
      var todo = entries.filter(function (e) {
        var base = e.name.split('/').pop() as string;
        return base && !/\/$/.test(e.name) && (/\.(iso|bin|img)$/i.test(base) || wanted(base));
      });
      var total = todo.reduce(function (sum, e) { return sum + e.csize; }, 0), done = 0;
      function onBytes(n: number) {
        done += n;
        if (progress)
          progress(done, total);
      }
      return todo.reduce(function (prev: Promise<void>, e) {
        return prev.then(function () {
          var base = e.name.split('/').pop() as string;
          if (/\.(iso|bin|img)$/i.test(base)) {
            return member(file, e, onBytes).then(function (image) {
              return Iso9660.extract(image, wanted);
            }).then(function (files) {
              Array.prototype.push.apply(found, files);
            });
          }
          return member(file, e, onBytes).then(function (f) {
            found.push(f);
          });
        });
      }, Promise.resolve()).then(function () {
        return found;
      });
    });
  }

  return { extract: extract };
})();

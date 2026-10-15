// SPDX-License-Identifier: AGPL-3.0-or-later
// Reads a .zip in the browser, for the picker of web/pre.js: the zip is the
// archive.org download of the CD (a .bin inside), or a zip of the CD's files.
// Stored and deflated entries are read; deflate uses DecompressionStream.
// Zip64 (4 GB and up) is not handled.
//
// Zip.extract(file, wanted) -> Promise<File[]>: the wanted files of the zip,
// and the wanted files of any CD image (.bin, .iso, .img) it holds (read by
// web/iso9660.js).
var Zip = (function () {
  var EOCD = 0x06054b50, CENTRAL = 0x02014b50, LOCAL = 0x04034b50;

  function bytes(blob, start, len) {
    return blob.slice(start, start + len).arrayBuffer().then(function (buf) {
      return new DataView(buf);
    });
  }

  function text(dv, start, len) {
    var s = '';
    for (var i = 0; i < len; i++)
      s += String.fromCharCode(dv.getUint8(start + i));
    return s;
  }

  // The entries of the central directory: { name, method, csize, offset }.
  function directory(file) {
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
        var out = [], pos = 0;
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
  function member(file, e) {
    return bytes(file, e.offset, 30).then(function (lh) {
      if (lh.getUint32(0, true) !== LOCAL)
        throw new Error('The zip is damaged.');
      var start = e.offset + 30 + lh.getUint16(26, true) + lh.getUint16(28, true);
      var data = file.slice(start, start + e.csize);
      var base = e.name.split('/').pop();
      if (e.method === 0)
        return new File([data], base);
      if (e.method !== 8)
        throw new Error('This zip uses a compression the browser cannot read; unzip it instead.');
      if (typeof DecompressionStream === 'undefined')
        throw new Error('This browser cannot unzip; unzip the file and choose the .bin instead.');
      return new Response(data.stream().pipeThrough(new DecompressionStream('deflate-raw'))).blob()
        .then(function (blob) {
          return new File([blob], base);
        });
    });
  }

  function extract(file, wanted) {
    return directory(file).then(function (entries) {
      var found = [];
      return entries.reduce(function (prev, e) {
        return prev.then(function () {
          var base = e.name.split('/').pop();
          if (!base || /\/$/.test(e.name))
            return; // a folder
          if (/\.(iso|bin|img)$/i.test(base)) {
            return member(file, e).then(function (image) {
              return Iso9660.extract(image, wanted);
            }).then(function (files) {
              Array.prototype.push.apply(found, files);
            });
          }
          if (wanted(base))
            return member(file, e).then(function (f) {
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

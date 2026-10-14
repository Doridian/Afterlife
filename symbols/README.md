# Symbol files

`<PROGRAM>.txt` (e.g. `ALIFE.EXE.txt`) is the shared, text form of what we know
about a binary. The Ghidra project in `ghidra/` is local and disposable; this
file is the source of truth. `tools/gh apply` loads it into Ghidra,
`tools/gh export` writes Ghidra's user-defined names back into it.

## Format

One symbol per line:

```
ADDR KIND NAME [| SIGNATURE] [# COMMENT]

0x004e1950 func codec_register  # (proc): codecList[proc(2)] = proc
0x004d8410 func vh_mem_free | void vh_mem_free(void * ptr, char * file, int line)
0x00567a80 data codec_list  # codecList[15]
0x00567a80 data codec_list | uint[15]
```

- `ADDR`: hex virtual address (`0x` optional).
- `KIND`: `func` (creates the function if Ghidra has none there) or `data` (a label).
- `NAME`: lowercase_snake, module prefix where known (`bndl_`, `cmprs_`, `codec_`,
  `preload_`, `cbres_`, `vh_`, ...). CRT functions keep their standard names.
- `SIGNATURE` (optional): for `func`, a C prototype. Without an explicit
  convention it is applied as `__cdecl` (ALIFE.EXE is MSVC-built). Types must
  be built-in or already exist in the Ghidra DB. For `data`, a data type
  (`int`, `char[16]`, `uint *`).
- `COMMENT` (optional): free text, kept in the file only.
- Lines starting with `#` are comments. A comment block is attached to the
  entry below it.

`tools/gh export` and `tools/gh rename` rewrite the file sorted by address, so
keep section comments in address order. Export only adds or updates entries.
It never deletes one, so remove stale lines by hand.

Name only what you're confident about. Use the comment for evidence (asserts,
strings, call sites) and for guesses.

// SPDX-License-Identifier: AGPL-3.0-or-later
// The Emscripten runtime the glue (web/*.ts, the --pre-js files) runs in: what
// it reads of Module, FS, IDBFS and the run dependencies. Declarations only,
// the runtime itself is in the generated afterlife.js.

interface EmscriptenModule {
  arguments: string[];
  preRun: (() => void)[];
  expectedDataFileDownloads?: number;
  setStatus(text: string): void;
  setProgress(share: number): void;
  onExit: (status: number) => void;
  onAbort: (what: string) => void;
  // for the browser tests (web/audio.ts)
  afAudioState?: () => AfAudio | null;
}

declare const Module: EmscriptenModule;

declare const FS: {
  mkdir(path: string): void;
  mount(type: object, options: { autoPersist?: boolean }, mountpoint: string): void;
  readdir(path: string): string[];
  writeFile(path: string, data: Uint8Array): void;
  syncfs(populate: boolean, callback: (err: unknown) => void): void;
};

declare const IDBFS: { queuePersist?: unknown };

declare function addRunDependency(id: string): void;
declare function removeRunDependency(id: string): void;

declare const HEAP16: Int16Array;

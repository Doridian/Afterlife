// SPDX-License-Identifier: AGPL-3.0-or-later
// The web build's audio output (emcc --pre-js), used by plat_audio_* in
// src/platform/platform_sdl.c instead of SDL's audio stream. SDL's web backend
// plays through a ScriptProcessorNode, which runs on the main thread and adds a
// callback of latency. Here an AudioWorklet plays on the audio thread, so
// the only queue is what the mixer has handed over (plat_audio_queued_frames).
//
// The mixer's frames are 16-bit stereo at the game's rate (22050 Hz). The
// AudioContext runs at that rate, so the browser does the resampling.
// Samples go to the worklet as Int16Array messages, and the worklet reports
// how many frames it has consumed, so the queue length is exact (the worklet
// counts what it played or dropped; frames still in flight are the difference).

var afAudio = null;

var afAudioWorkletSource = [
  'class Feed extends AudioWorkletProcessor {',
  '  constructor() {',
  '    super();',
  '    this.q = [];',
  '    this.off = 0;',
  '    this.consumed = 0;',
  '    this.reported = -1;',
  '    this.port.onmessage = (e) => {',
  '      if (e.data.clear) {',
  '        let dropped = 0;',
  '        for (let j = 0; j < this.q.length; j++) {',
  '          dropped += this.q[j].length / 2 - (j === 0 ? this.off : 0);',
  '        }',
  '        this.consumed += dropped;',
  '        this.q = [];',
  '        this.off = 0;',
  '      } else {',
  '        this.q.push(e.data.frames);',
  '      }',
  '    };',
  '  }',
  '  process(inputs, outputs) {',
  '    const L = outputs[0][0];',
  '    const R = outputs[0][1];',
  '    const n = L.length;',
  '    let i = 0;',
  '    while (i < n && this.q.length) {',
  '      const b = this.q[0];',
  '      const frames = b.length / 2;',
  '      const take = Math.min(frames - this.off, n - i);',
  '      for (let k = 0; k < take; k++) {',
  '        const s = (this.off + k) * 2;',
  '        L[i + k] = b[s] / 32768;',
  '        R[i + k] = b[s + 1] / 32768;',
  '      }',
  '      i += take;',
  '      this.off += take;',
  '      if (this.off >= frames) {',
  '        this.q.shift();',
  '        this.off = 0;',
  '      }',
  '    }',
  '    for (let k = i; k < n; k++) {',
  '      L[k] = 0;',
  '      R[k] = 0;',
  '    }',
  '    this.consumed += i;',
  '    if (this.consumed !== this.reported) {',
  '      this.reported = this.consumed;',
  '      this.port.postMessage(this.consumed);',
  '    }',
  '    return true;',
  '  }',
  '}',
  'registerProcessor("feed", Feed);',
].join('\n');

// Browsers start audio only after a user gesture; the context is resumed on
// the first one (SDL did the same).
function afAudioResumeOnGesture(ctx) {
  var resume = function () {
    ctx.resume();
    ['pointerdown', 'keydown', 'touchend'].forEach(function (t) {
      document.removeEventListener(t, resume, true);
    });
  };
  ['pointerdown', 'keydown', 'touchend'].forEach(function (t) {
    document.addEventListener(t, resume, true);
  });
}

// Returns 1 if the output is set up (it starts once the worklet has loaded).
function afAudioOpen(rate) {
  if (typeof AudioWorkletNode === 'undefined' || typeof AudioContext === 'undefined') {
    return 0;
  }
  afAudioClose();
  var ctx;
  try {
    ctx = new AudioContext({ sampleRate: rate, latencyHint: 'interactive' });
  } catch (e) {
    return 0;
  }
  afAudio = { ctx: ctx, node: null, sent: 0, consumed: 0, pending: [], pendingFrames: 0 };
  var url = URL.createObjectURL(new Blob([afAudioWorkletSource], { type: 'application/javascript' }));
  ctx.audioWorklet.addModule(url).then(function () {
    URL.revokeObjectURL(url);
    if (!afAudio || afAudio.ctx !== ctx) return;
    var node = new AudioWorkletNode(ctx, 'feed', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
    node.port.onmessage = function (e) { afAudio.consumed = e.data; };
    node.connect(ctx.destination);
    afAudio.node = node;
    afAudio.pending.forEach(function (chunk) { node.port.postMessage({ frames: chunk }, [chunk.buffer]); });
    afAudio.pending = [];
    afAudio.pendingFrames = 0;
  }).catch(function (e) {
    console.warn('audio: worklet failed to load', e);
  });
  afAudioResumeOnGesture(ctx);
  return 1;
}

// frames: an Int16Array of interleaved stereo, copied out of the heap.
function afAudioQueue(ptr, frameCount) {
  if (!afAudio) return;
  var chunk = HEAP16.slice(ptr >> 1, (ptr >> 1) + frameCount * 2);
  afAudio.sent += frameCount;
  if (afAudio.node) {
    afAudio.node.port.postMessage({ frames: chunk }, [chunk.buffer]);
  } else {
    afAudio.pending.push(chunk);
    afAudio.pendingFrames += frameCount;
  }
}

function afAudioQueued() {
  return afAudio ? afAudio.sent - afAudio.consumed : 0;
}

// Drops everything queued (pause).
function afAudioClear() {
  if (!afAudio) return;
  if (afAudio.node) {
    afAudio.node.port.postMessage({ clear: true });
  } else {
    afAudio.sent -= afAudio.pendingFrames;
    afAudio.pending = [];
    afAudio.pendingFrames = 0;
  }
}

function afAudioClose() {
  if (!afAudio) return;
  afAudio.ctx.close();
  afAudio = null;
}

// For the browser tests: the backend's state (context, queue counts).
Module['afAudioState'] = function () { return afAudio; };

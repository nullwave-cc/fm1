// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 nullwave contributors
// The installer's wait for an FM-1 back from an update (tests/installer_reconnect_test.py runs it): the old port gone,
// the FM-1 back under a new USB identity on a port listed only in a new MIDIAccess (a computer's new device, as when a
// serial number first appears). Fake WebMIDI ports, nothing sent anywhere.
//   node tests/web/installer_back.mjs DIR    (DIR: an fm1ota.js and its fm1ports.js) -> exit 0 found, 1 not
import { pathToFileURL } from "node:url";
import { join } from "node:path";

const { Updater, pack7 } = await import(pathToFileURL(join(process.argv[2], "fm1ota.js")).href);
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7].join();
function port(name, ident) {
  const p = {
    input: { name, state: "connected", open: async () => {}, onmidimessage: null },
    output: {
      name, state: "connected", open: async () => {},
      send: (b) => {
        if (Array.from(b).join() !== HS) return;
        const t = [...ident].map((c) => c.charCodeAt(0));
        const data = Uint8Array.from([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
        setTimeout(() => p.input.onmidimessage && p.input.onmidimessage({ data }), 1);
      },
    },
  };
  return p;
}
const midi = (ports) => ({ inputs: new Map(ports.map((p, k) => ["i" + k, p.input])), outputs: new Map(ports.map((p, k) => ["o" + k, p.output])) });
const old = port("nullwave", "FM-1_60000101");
const before = midi([old]);
const after = midi([port("FM-1", "FM-1_60000102")]);
Object.defineProperty(globalThis, "navigator", { configurable: true, value: { requestMIDIAccess: async () => after } });
const up = new Updater(before);
up.watch();
old.input.state = old.output.state = "disconnected";   // (the FM-1 restarted: its old port gone)
const t0 = Date.now();
const back = await up.waitFor((id) => !/^ota-/i.test(id.model), 12000);
console.log(back ? `found ${back.name} ${back.id.text} after ${Date.now() - t0} ms` : "not found");
process.exit(back && back.id.text === "FM-1_60000102" ? 0 : 1);

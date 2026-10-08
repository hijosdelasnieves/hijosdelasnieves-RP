'use strict';
const assert = require('node:assert/strict');
const {StudioClient} = require('../client/StudioClient.cjs');
let count = 0;
function check(condition) { assert.ok(condition); count++; }
let calls = [], serial = 0, active = 0;
function native(cls, method, self, ...args) {
  check(cls === 'HdnTailorStudio' && self === undefined);
  calls.push({method, args});
  if (method === 'ApiVersion') return 1;
  if (method === 'BeginSession') return active = ++serial;
  if (method === 'EndSession') { check(args[0] === active); active = 0; return true; }
  if (method === 'Snapshot') { check(args[0] === active); return true; }
  if (method === 'Frame') return args[0] === active;
  if (method === 'Viewport') return args[0] === active;
  if (method === 'GetStatus') return args[0] === active ? 3 : 0;
}
let studio = new StudioClient(native);
check(studio.open());
check(studio.yaw === 0 && studio.zoom === 1);
check(!studio.snapshot(20));
check(!studio.snapshot(0x132ab));
check(!studio.snapshot(-1));
check(!studio.snapshot(0xff000100 + 0.5));
check(!studio.snapshot(0x1ff000100));
check(studio.snapshot(0xff000100));
let snap = calls.find(c => c.method === 'Snapshot');
check(snap.args[2] === (0xff000100 | 0));
check(snap.args[1] === 1);
check(studio.snapshot(0xff000100));
check(calls.filter(c => c.method === 'Snapshot')[1].args[1] === 2);
check(studio.rotate(45));
check(studio.rotate(-90));
check(studio.yaw === 315);
check(!studio.rotate(NaN));
check(studio.frame());
check(studio.viewport({x:0.5,y:0.5,width:0.25,height:0.6}));
check(!studio.viewport({x:0,y:0,width:0.25,height:0.6}));
check(!studio.viewport({x:0.5,y:0.5,width:NaN,height:0.6}));
check(!studio.viewport({x:0.5,y:0.5,width:0,height:0.6}));
check(studio.status() === 3);
const first = studio.token;
check(studio.open());
check(studio.token > first && studio.yaw === 0);
studio.close();
const length = calls.length;
studio.close();
check(calls.length === length);
check(!studio.frame() && !studio.snapshot(0xff000100) && studio.status() === 0);
studio = new StudioClient(() => { throw Error('Plugin missing'); });
check(!studio.open());
studio = new StudioClient(() => 0);
check(!studio.open());
for (const token of [-1, 1.5, NaN, '1']) {
  studio = new StudioClient((cls, method) => method === 'ApiVersion' ? 1 : token);
  check(!studio.open());
}
console.log(`${count} adapter checks passed (mock API, not a Skyrim render test)`);

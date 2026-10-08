'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const code=fs.readFileSync(require('node:path').join(__dirname,'../client/Viewport.js'),'utf8');
let visible=true,interval,resize,messages=[],intervals=0;
let box={left:400,width:1100,top:100,bottom:900};
const stage={
  getBoundingClientRect:()=>box,
  querySelector:selector=>({getBoundingClientRect:()=>selector==='.ts-stage-title'?{bottom:150}:{top:850}})
};
const ctx=vm.createContext({innerWidth:1920,innerHeight:1080,document:{getElementById:()=>({querySelector:()=>stage})}});
ctx.window={
  hdnVendor:{isVisible:()=>visible},
  skyrimPlatform:{sendMessage:message=>messages.push(JSON.parse(message))},
  addEventListener:(event,handler)=>{assert.equal(event,'resize');resize=handler;},
  setInterval:(handler,delay)=>{assert.equal(delay,250);interval=handler;intervals++;}
};
vm.runInContext(code,ctx);vm.runInContext(code,ctx);
assert.equal(intervals,1,'Bootstrap is idempotent');
interval();assert.equal(messages.length,1);
const first=messages[0];
assert.equal(first.kind,'hdnVendorPrivateViewport');
assert.equal(first.bounds.x,(400+550)/1920);
assert.equal(first.bounds.y,(162+838)/2/1080);
assert.equal(first.bounds.width,1076/1920);
assert.equal(first.bounds.height,676/1080);
interval();interval();assert.equal(messages.length,1,'Unchanged bounds do not spam');
box={left:284,width:652,top:100,bottom:900};ctx.innerWidth=960;
resize();assert.equal(messages.length,2);
assert.equal(messages[1].bounds.x,(284+326)/960);
visible=false;interval();assert.equal(messages.length,2);
visible=true;interval();assert.equal(messages.length,3,'Reopening reports even identical bounds');
box.width=20;interval();assert.equal(messages.length,3,'Collapsed stage is ignored');
console.log('Viewport reporter checks passed (mock DOM, not a Skyrim render test)');

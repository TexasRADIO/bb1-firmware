// Actual firmware/WASM control journey; no browser or hardware acceptance claim.
// Usage: node journey.mjs MODULE LIBRARY NEW_OUTPUT
import fs from 'node:fs';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
const [modulePath,libraryPath,out]=process.argv.slice(2);
fs.mkdirSync(out,{recursive:false});
const raw=fs.readFileSync(modulePath),pack=fs.readFileSync(libraryPath);
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const module=new WebAssembly.Module(raw);assert.deepEqual(WebAssembly.Module.imports(module),[]);
const x=new WebAssembly.Instance(module,{}).exports;
new Uint8Array(x.memory.buffer).set(pack,x.emu_sound_window());x.emu_init();
assert.equal(x.emu_sound_check(),0);assert.equal(x.emu_sound_kits(),7);
let frames=0,energy=0,values=0;const captures=[];
function frame(n=1){while(n--){x.emu_frame();frames++;const count=x.emu_audio_take();for(const v of new Float32Array(x.memory.buffer,x.emu_audio_ptr(),count*2)){assert(Number.isFinite(v));energy+=v*v;values++;}}}
function tap(b){x.emu_button(b,1);frame(3);x.emu_button(b,0);frame(3);}
function capture(name){frame(90);const fb=Buffer.from(new Uint8Array(x.memory.buffer,x.emu_fb(),240*240*2));fs.writeFileSync(`${out}/${name}.rgb565`,fb);const title=Buffer.concat(Array.from({length:23},(_,y)=>fb.subarray(y*480,y*480+190)));captures.push({name,sha256:hash(fb),title_sha256:hash(title),running:x.emu_running(),focus:x.emu_focus_row()});}
frame(8);assert.equal(x.emu_running(),0);capture('welcome');
tap(10);frame(50);assert.equal(x.emu_running(),1);capture('rhythm-conductor');
tap(9);capture('drum-steps');tap(8);capture('drum-pads');
frame(20);x.emu_turn(0,2);frame(20);assert.equal(x.emu_focus_row(),2);capture('dillo-keys');tap(9);capture('dillo-steps');
tap(0);capture('fx');
x.emu_button(6,1);frame(70);capture('help');x.emu_button(6,0);frame(3);
assert.notEqual(captures.find(c=>c.name==='dillo-keys').title_sha256,captures.find(c=>c.name==='dillo-steps').title_sha256);
tap(10);frame(4);assert.equal(x.emu_running(),0);assert(energy>0);
const result={module_sha256:hash(raw),library_sha256:hash(pack),frames,values,energy,captures,finite_audio:true,play_stop_pass:true,visual_review:false,device_contact:false};
fs.writeFileSync(`${out}/RESULT.json`,JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));

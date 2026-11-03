// Compare actual old/new modules under the same controls; LED observation must not alter music/UI.
import fs from 'node:fs';
import assert from 'node:assert/strict';
const [before,after,library]=process.argv.slice(2);
const modules=[before,after].map(p=>new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(p)),{}).exports);
for(const x of modules){new Uint8Array(x.memory.buffer,x.emu_sound_window(),245760).set(fs.readFileSync(library));x.emu_init();}
const [a,b]=modules;let frames=0;const states=[];
const bytes=(x,p,n)=>Buffer.from(new Uint8Array(x.memory.buffer,p,n));
function frame(n=1){while(n--){for(const x of modules)x.emu_frame();frames++;
assert.deepEqual(bytes(a,a.emu_fb(),115200),bytes(b,b.emu_fb(),115200));
const na=a.emu_audio_take(),nb=b.emu_audio_take();assert.equal(na,nb);assert.deepEqual(bytes(a,a.emu_audio_ptr(),na*8),bytes(b,b.emu_audio_ptr(),nb*8));
assert.equal(a.emu_running(),b.emu_running());
for(let i=0;i<41;i++){const s=b.emu_led_state(i);assert(s>=0&&s<16);assert.equal(b.emu_led_state(i),s);}
assert.equal(b.emu_led_state(41),0);assert.equal(b.emu_led_state(0xffffffff),0);
}}
function tap(id){for(const x of modules)x.emu_button(id,1);frame(3);for(const x of modules)x.emu_button(id,0);frame(3);}
function snapshot(name){const lights=Array.from({length:41},(_,i)=>b.emu_led_state(i));assert(lights.some(x=>x!==0));states.push({name,lights});}
frame(12);snapshot('welcome');tap(10);frame(20);assert(b.emu_led_state(10)&1);snapshot('rhythm_conductor');tap(9);frame(20);snapshot('drum_steps');
for(const x of modules)x.emu_turn(0,2);frame(20);tap(8);frame(20);snapshot('dillo_keys');tap(0);frame(20);snapshot('fx');
for(const x of modules)x.emu_button(6,1);frame(70);snapshot('help');for(const x of modules)x.emu_button(6,0);frame(3);tap(10);frame(5);assert.equal(b.emu_running(),0);
assert(states.some(s=>s.lights.some(v=>v&4)), 'mid illumination must be observed');
assert(states.some(s=>s.lights.some(v=>v===0)), 'unavailable controls stay dark');
console.log(JSON.stringify({status:'PASS',frames,framebuffers_exact:true,audio_bytes_exact:true,states},null,2));

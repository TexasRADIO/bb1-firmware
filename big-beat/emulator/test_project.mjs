// Current-version WASM project roundtrip and refusal tests; no legacy SLOOP compatibility claim.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
const [modulePath,libraryPath,out]=process.argv.slice(2);
fs.mkdirSync(out,{recursive:false});
const raw=fs.readFileSync(modulePath),library=fs.readFileSync(libraryPath);
function machine(){const x=new WebAssembly.Instance(new WebAssembly.Module(raw),{}).exports;new Uint8Array(x.memory.buffer,x.emu_sound_window(),library.length).set(library);x.emu_init();return x;}
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function frame(x,n=1){while(n--){x.emu_frame();x.emu_audio_take();}}
function tap(x,b){x.emu_button(b,1);frame(x,3);x.emu_button(b,0);frame(x,3);}
function acquire(x){let r=x.emu_project_begin();for(let n=0;r===65&&n<100;n++){frame(x);r=x.emu_project_state();}assert.equal(r,0);}
function snapshot(x){acquire(x);try{const n=x.emu_project_export();assert(n>=64,`export error ${x.emu_project_error()}`);return Buffer.from(new Uint8Array(x.memory.buffer,x.emu_project_ptr(),n));}finally{x.emu_project_cancel();}}
function load(x,wire){acquire(x);try{new Uint8Array(x.memory.buffer,x.emu_project_ptr(),wire.length).set(wire);return x.emu_project_import(wire.length);}finally{x.emu_project_cancel();}}
const a=machine();frame(a,12);tap(a,10);frame(a,50);tap(a,10);frame(a,12);
// Use real UI: GLOBAL white 4 -> SONG, white 1 -> ADD, EDIT -> append.
function key(x,k){x.emu_key(k,1);frame(x,3);x.emu_key(k,0);frame(x,3);}
tap(a,5);key(a,3);key(a,0);tap(a,4);frame(a,12);
assert.equal(a.emu_arr_rows(),1,'UI must add a real composition row');
// Copy it and change repeats through SONG controls.
key(a,2);a.emu_turn(4,2);frame(a,12);
assert.equal(a.emu_arr_rows(),2);assert(a.emu_arr_repeats()>1);
const before=snapshot(a);fs.writeFileSync(out+'/composition.bbp2',before);
assert.equal(a.emu_project_has_file(),0,'download alone must not mark file loaded');
const b=machine();frame(b,12);assert.equal(load(b,before),0);frame(b,12);
assert.equal(b.emu_project_has_file(),1);assert.equal(b.emu_project_dirty(),0);assert.equal(b.emu_running(),0);assert.equal(b.emu_arr_rows(),a.emu_arr_rows());
const restored=snapshot(b);assert.deepEqual(restored,before,'roundtrip snapshot differs');
// Editing a restored file must become dirty; exporting must not claim a disk save.
// An immediate edit during export resume is refused without changing the song.
key(b,2);assert.equal(b.emu_arr_rows(),2);
// Once resume commands drain, the same edit succeeds.
frame(b,12);
// Import returns directly to SONG; white 3 copies the selected row.
key(b,2);frame(b,8);
fs.writeFileSync(out+'/after-copy.rgb565',Buffer.from(new Uint8Array(b.memory.buffer,b.emu_fb(),115200)));
assert.equal(b.emu_arr_rows(),3);assert.equal(b.emu_project_dirty(),1);
const edited=snapshot(b);assert.notDeepEqual(edited,restored);assert.equal(b.emu_project_dirty(),1);
assert.equal(load(b,before),0);frame(b,8);assert.equal(b.emu_arr_rows(),2);assert.equal(b.emu_project_dirty(),0);
assert.deepEqual(snapshot(b),restored);
const corrupt=Buffer.from(before);corrupt[corrupt.length-1]^=1;
assert.equal(load(b,corrupt),4,'checksum corruption must be refused');assert.deepEqual(snapshot(b),restored,'corruption changed current project');
assert.notEqual(load(b,before.subarray(0,63)),0,'truncated snapshot accepted');assert.deepEqual(snapshot(b),restored,'truncation changed current project');
tap(b,10);assert.equal(b.emu_running(),1);assert.equal(b.emu_project_begin(),64,'running import/export must be refused');b.emu_project_cancel();assert.equal(b.emu_running(),1);tap(b,10);frame(b,5);assert.equal(b.emu_running(),0);
const result={status:'PASS',module_sha256:hash(raw),library_sha256:hash(library),snapshot_sha256:hash(before),bytes:before.length,rows:a.emu_arr_rows(),roundtrip_exact:true,corruption_refused_unchanged:true,truncation_refused_unchanged:true,running_refusal:true,download_does_not_mark_saved:true,transient_resume_edit_refused_unchanged:true,edit_marks_dirty:true,reopen_restores_composition:true,scope:'Current-version WASM owner/file bridge only; browser download/upload and physical saves untested'};
fs.writeFileSync(out+'/RESULT.json',JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));

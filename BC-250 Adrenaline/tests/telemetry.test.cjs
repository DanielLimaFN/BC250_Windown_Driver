const test=require('node:test'),assert=require('node:assert/strict');
const {sample,Session,heatColor,fields,read}=require('../telemetry.js');
test('consistent mock sensors, power, eight chips and aggregates across four scenarios',()=>{
 for(const s of ['idle','balanced','gaming','hot'])for(let i=0;i<100;i++){const d=sample(i,s,12345);
 assert.equal(d.source,'mock');assert.equal(d.timestamp,12345);assert.equal(d.memory.chips_c.length,8);
 assert.equal(d.memory.hotspot_c,Math.max(...d.memory.chips_c));assert.equal(d.memory.chips_c[d.memory.hotspot_chip],d.memory.hotspot_c);
 assert.ok(Math.abs(d.hardware.total_power-d.hardware.cpu.pout-d.hardware.gpu.pout)<.11);
 for(const f of fields)assert.equal(typeof read(d,f.path),'number',f.id);
 }});
test('missing readings are null, never invented zeros',()=>{const d=sample(1,'unavailable');for(const f of fields)assert.equal(read(d,f.path),null,f.id);});
test('60-sample history and extrema survive missing readings',()=>{const s=new Session();for(let i=0;i<80;i++)s.next('gaming',i*1000);assert.equal(s.history.length,60);assert.equal(s.history[0].timestamp,20000);const prev=JSON.stringify(s.extrema);s.next('unavailable',81000);assert.equal(JSON.stringify(s.extrema),prev);});
test('heat scale clamps values and missing readings use gray',()=>{assert.equal(heatColor(20),heatColor(-50));assert.equal(heatColor(120),heatColor(180));assert.equal(heatColor(null),'#73818a');assert.notEqual(heatColor(35),heatColor(95));});

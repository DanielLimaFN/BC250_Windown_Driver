const {chromium}=require('@playwright/test');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
(async()=>{const browser=await chromium.launch({channel:'msedge',headless:true});try{
 const page=await browser.newPage({deviceScaleFactor:1});const svg=fs.readFileSync(path.join(root,'assets/icon.svg'),'utf8');
 const images=[];
 for(const size of [16,24,32,48,64,128,256]){
 await page.setViewportSize({width:size,height:size});await page.setContent('<html><body style="margin:0;background:transparent">'+svg.replace('<svg ','<svg style="width:100vw;height:100vh;display:block" ')+'</body></html>');
 const png=await page.screenshot({omitBackground:true});images.push({size,png});
 if(size===256)fs.writeFileSync(path.join(root,'assets/icon.png'),png);
 }
 const header=Buffer.alloc(6+16*images.length);header.writeUInt16LE(1,2);header.writeUInt16LE(images.length,4);
 let offset=header.length;images.forEach(({size,png},i)=>{const pos=6+16*i;header[pos]=size===256?0:size;header[pos+1]=size===256?0:size;header.writeUInt16LE(1,pos+4);header.writeUInt16LE(32,pos+6);header.writeUInt32LE(png.length,pos+8);header.writeUInt32LE(offset,pos+12);offset+=png.length;});
 fs.writeFileSync(path.join(root,'assets/icon.ico'),Buffer.concat([header,...images.map(i=>i.png)]));console.log('Icon rendered: SVG, PNG and multi-resolution ICO (16–256 px).');
 }finally{await browser.close();}})().catch(e=>{console.error(e);process.exitCode=1;});

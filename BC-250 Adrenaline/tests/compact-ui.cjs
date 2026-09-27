const {chromium}=require('@playwright/test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {spawn}=require('node:child_process');const root=path.resolve(__dirname,'..');
(async()=>{const server=spawn(process.execPath,['server.cjs'],{cwd:root,windowsHide:true,env:{...process.env,PORT:'4175'},stdio:['ignore','pipe','pipe']});let browser;
try{await new Promise((resolve,reject)=>{server.stdout.once('data',resolve);server.once('error',reject);});browser=await chromium.launch({channel:'msedge',headless:true});
const page=await browser.newPage({viewport:{width:1280,height:820},colorScheme:'light',deviceScaleFactor:1});const errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.goto('http://127.0.0.1:4175');assert.equal(await page.locator('.install-hero').count(),0);
assert.ok((await page.locator('.open-source-strip').boundingBox()).height<110);
await page.screenshot({path:path.join(root,'artifacts/compact-installation.png'),fullPage:true});
await page.locator('[data-page=sensors]').click();await page.locator('[data-action=skip-install]').click();
for(const width of [1440,1280,1024,800,640]){
 await page.setViewportSize({width,height:820});await page.locator('[data-page=overview]').click();
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,'page overflow at '+width);
 assert.ok((await page.locator('.metric').first().boundingBox()).height<=125,'metric height at '+width);
 assert.ok((await page.locator('.board-stage').boundingBox()).height<=340,'board height at '+width);
 assert.equal(await page.locator('.component-detail').isVisible(),true);
 await page.screenshot({path:path.join(root,'artifacts/compact-dashboard-'+width+'.png'),fullPage:true});
 await page.locator('[data-page=system]').click();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,'system overflow at '+width);
 await page.locator('.nav-item[data-page=settings]').click();assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,'settings overflow at '+width);
}
await page.setViewportSize({width:1280,height:820});await page.locator('#theme-select').selectOption('dark');await page.locator('[data-page=overview]').click();await page.screenshot({path:path.join(root,'artifacts/compact-dashboard-dark.png'),fullPage:true});
assert.deepEqual(errors,[]);console.log('PASS: compact open-source strip; 1440/1280/1024/800/640 layouts; no horizontal page overflow; cards <=125px; board <=340px; component details visible; zero renderer errors.');
}finally{await browser?.close();server.kill();}})().catch(e=>{console.error(e);process.exitCode=1;});

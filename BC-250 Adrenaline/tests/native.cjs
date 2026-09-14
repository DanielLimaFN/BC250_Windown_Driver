const {_electron:electron}=require('@playwright/test');
const path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
(async()=>{let app;try{
 app=await electron.launch({executablePath:path.join(root,'dist/BC-250-Adrenaline-win32-x64/BC-250-Adrenaline.exe'),args:['--user-data-dir='+path.join(root,'artifacts/native-test-profile-'+Date.now())],timeout:30000});
 const window=await app.firstWindow();await window.waitForSelector('#install-button',{state:'visible'});
 const security=await app.evaluate(({BrowserWindow})=>{const w=BrowserWindow.getAllWindows()[0];return {title:w.getTitle(),web:w.webContents.getLastWebPreferences()};});
 assert.equal(security.title,'BC-250 Adrenaline');assert.equal(security.web.nodeIntegration,false);assert.equal(security.web.contextIsolation,true);assert.equal(security.web.sandbox,true);
 assert.equal(await window.evaluate(()=>typeof window.desktop?.setTheme),'function');
 await window.screenshot({path:path.join(root,'artifacts/06-executavel-windows.png')});
 await window.locator('[data-page=sensors]').click();await window.locator('[data-action=skip-install]').click();
 await window.waitForTimeout(1200);assert.notEqual(await window.locator('.sparkline path').first().getAttribute('d'),null);
 assert.equal(await window.locator('#thermal-board #cpu').getAttribute('role'),'button');
 await window.locator('.nav-item[data-page=settings]').click();await window.locator('#theme-select').selectOption('dark');
 assert.equal(await window.locator('html').getAttribute('data-theme'),'dark');
 assert.equal(await window.locator('h1#page-title').evaluate(el=>getComputedStyle(el).color),'rgb(241, 241, 241)');
 await window.locator('[data-page=overview]').click();await window.screenshot({path:path.join(root,'artifacts/07-executavel-dashboard.png'),fullPage:true});
 console.log('PASS: packaged Windows EXE launched, local SVG, live charts, mocked driver branch, native theme bridge, isolated renderer and sandbox.');
 }finally{await app?.close();}})().catch(e=>{console.error(e);process.exitCode=1;});

const {app, BrowserWindow, ipcMain, nativeTheme, clipboard, shell} = require('electron');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {config,copyValue} = require('./donations.js');
const localURL=pathToFileURL(path.join(__dirname,'index.html')).href;
app.setAppUserModelId('com.bc250.adrenaline');
function trustedSender(event){return event.senderFrame===event.sender.mainFrame && event.senderFrame.url===localURL;}
function approvedCheckout(url){try{const u=new URL(url);return u.protocol==='https:'&&!u.username&&!u.password&&['ko-fi.com','www.paypal.com','paypal.com','checkout.stripe.com'].includes(u.hostname);}catch{return false;}}
function createWindow() {
 const win = new BrowserWindow({width:1280,height:820,minWidth:640,minHeight:560,show:false,
  title:'BC-250 Adrenaline',icon:path.join(__dirname,'assets/icon.ico'),backgroundColor:'#f3f3f3',titleBarStyle:'hidden',
  titleBarOverlay:{color:'#f3f3f3',symbolColor:'#202020',height:40},
  webPreferences:{preload:path.join(__dirname,'preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:true}});
 win.setMenu(null);
 win.webContents.setWindowOpenHandler(({url})=>{if(approvedCheckout(url))shell.openExternal(url).catch(()=>{});return {action:'deny'};});
 win.webContents.on('will-navigate',e=>e.preventDefault());
 win.webContents.session.setPermissionRequestHandler((_webContents,_permission,callback)=>callback(false));
 win.loadFile('index.html');win.once('ready-to-show',()=>win.show());
}
ipcMain.on('theme',(event,theme)=>{
 if(!trustedSender(event)||!['dark','light'].includes(theme))return;
 const win=BrowserWindow.fromWebContents(event.sender);if(!win)return;
 nativeTheme.themeSource=theme;win.setTitleBarOverlay({color:theme==='dark'?'#202020':'#f3f3f3',symbolColor:theme==='dark'?'#f5f5f5':'#202020',height:40});
});
ipcMain.handle('copy-donation',(event,kind,id)=>{
 if(!trustedSender(event))return false;const value=copyValue(kind,id);if(!value)return false;
 clipboard.writeText(value);return true;
});
ipcMain.handle('open-kofi',async event=>{if(!trustedSender(event))return false;try{await shell.openExternal(config.kofi.url);return true;}catch{return false;}});
app.whenReady().then(createWindow);app.on('window-all-closed',()=>app.quit());

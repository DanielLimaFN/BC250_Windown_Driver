const {app, BrowserWindow, ipcMain, nativeTheme, clipboard, shell} = require('electron');
const path = require('node:path');
const {exec} = require('node:child_process');
const util = require('node:util');
const os = require('node:os');
const execPromise = util.promisify(exec);
const {pathToFileURL} = require('node:url');
const {config,copyValue} = require('./donations.js');
const localURL=pathToFileURL(path.join(__dirname,'index.html')).href;
app.setAppUserModelId('com.bc250.adrenaline');
function trustedSender(event){return event.senderFrame===event.sender.mainFrame && event.senderFrame.url===localURL;}
function approvedCheckout(url){try{const u=new URL(url);return u.protocol==='https:'&&!u.username&&!u.password&&['buymeacoffee.com','www.buymeacoffee.com','ko-fi.com','www.paypal.com','paypal.com','checkout.stripe.com'].includes(u.hostname);}catch{return false;}}
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
ipcMain.handle('open-bmc',async event=>{if(!trustedSender(event))return false;try{await shell.openExternal(config.bmc?.url||config.kofi.url);return true;}catch{return false;}});
ipcMain.handle('open-kofi',async event=>{if(!trustedSender(event))return false;try{await shell.openExternal(config.bmc?.url||config.kofi.url);return true;}catch{return false;}});

ipcMain.handle('check-driver-installed', async event => {
 if (!trustedSender(event)) return { found: false, installed: false };
 try {
  const psCmd = `powershell -NoProfile -ExecutionPolicy Bypass -Command "$dev = Get-PnpDevice -InstanceId '*1002*13FE*' -ErrorAction SilentlyContinue | Select-Object -First 1; if ($dev) { [PSCustomObject]@{ found = $true; service = $dev.Service; friendlyName = $dev.FriendlyName; status = $dev.Status; installed = ($dev.Service -eq 'bc250mcdm' -or ($dev.FriendlyName -like '*BC-250*' -and $dev.Service -ne 'BasicDisplay')) } | ConvertTo-Json -Compress } else { [PSCustomObject]@{ found = $false; installed = $false } | ConvertTo-Json -Compress }"`;
  const { stdout } = await execPromise(psCmd, { timeout: 4000 });
  const data = JSON.parse(stdout.trim());
  return data;
 } catch (err) {
  return { found: false, installed: false, error: err.message };
 }
});

let prevCpuTimes = null;
function calculateCpuUsage() {
  const cpus = os.cpus();
  let idle = 0, total = 0;
  for (const c of cpus) {
    for (const t in c.times) total += c.times[t];
    idle += c.times.idle;
  }
  if (!prevCpuTimes) {
    prevCpuTimes = { idle, total };
    return 0;
  }
  const dIdle = idle - prevCpuTimes.idle;
  const dTotal = total - prevCpuTimes.total;
  prevCpuTimes = { idle, total };
  if (dTotal <= 0) return 0;
  return Math.max(0, Math.min(100, Math.round((100 - (100 * dIdle / dTotal)) * 10) / 10));
}
calculateCpuUsage();

let cachedCpuInfo = null;
async function getCpuInfo() {
  if (cachedCpuInfo) return cachedCpuInfo;
  try {
    const psCmd = `powershell -NoProfile -ExecutionPolicy Bypass -Command "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; Get-CimInstance Win32_Processor | Select-Object -First 1 Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, CurrentClockSpeed | ConvertTo-Json -Compress"`;
    const { stdout } = await execPromise(psCmd, { timeout: 4000 });
    const data = JSON.parse(stdout.trim());
    cachedCpuInfo = {
      name: data.Name || os.cpus()[0]?.model || 'AMD BC-250',
      cores: data.NumberOfCores || 6,
      threads: data.NumberOfLogicalProcessors || os.cpus().length || 12,
      clockMhz: data.CurrentClockSpeed || data.MaxClockSpeed || 3194
    };
  } catch {
    cachedCpuInfo = {
      name: os.cpus()[0]?.model || 'AMD BC-250',
      cores: 6,
      threads: os.cpus().length || 12,
      clockMhz: 3194
    };
  }
  return cachedCpuInfo;
}

ipcMain.handle('get-system-telemetry', async event => {
  if (!trustedSender(event)) return null;
  try {
    const info = await getCpuInfo();
    const cpuUsage = calculateCpuUsage();
    const totalMem = os.totalmem();
    const freeMem = os.freemem();
    return {
      cpuCount: info.cores,
      cpuCores: info.cores,
      cpuThreads: info.threads,
      cpuModel: info.name,
      cpuUsagePct: cpuUsage,
      avgFreqMhz: info.clockMhz,
      cpuTempC: null, // Sem sensor ACPI exposto pelo firmware no Windows -> exibe '-'
      totalMemGb: Number((totalMem / (1024 * 1024 * 1024)).toFixed(1)),
      usedMemGb: Number(((totalMem - freeMem) / (1024 * 1024 * 1024)).toFixed(1)),
      freeMemGb: Number((freeMem / (1024 * 1024 * 1024)).toFixed(1))
    };
  } catch (err) {
    return null;
  }
});

ipcMain.handle('apply-cpu-overclock', async (event, freqMhz) => {
  if (!trustedSender(event)) return { success: false, message: 'Untrusted sender' };
  const freq = parseInt(freqMhz, 10);
  if (isNaN(freq) || freq < 3000 || freq > 4200) {
    return { success: false, message: 'Frequência fora da faixa permitida (3.0 GHz a 4.2 GHz)' };
  }
  try {
    const scriptPath = path.resolve(__dirname, '..', 'tools', 'set-bc250-cpu-clock.ps1');
    const psCmd = `powershell -NoProfile -ExecutionPolicy Bypass -File "${scriptPath}" -FrequencyMhz ${freq}`;
    const { stdout } = await execPromise(psCmd, { timeout: 6000 });
    const data = JSON.parse(stdout.trim());
    return data;
  } catch (err) {
    return {
      success: false,
      frequencyMhz: freq,
      hardwareSuccess: false,
      cpuResponse: 'Erro ao executar ferramenta de overclock: ' + err.message,
      message: err.message
    };
  }
});

ipcMain.handle('get-cpu-overclock', async event => {
  if (!trustedSender(event)) return { success: false, frequencyMhz: 3500 };
  try {
    const scriptPath = path.resolve(__dirname, '..', 'tools', 'set-bc250-cpu-clock.ps1');
    const psCmd = `powershell -NoProfile -ExecutionPolicy Bypass -File "${scriptPath}" -Get`;
    const { stdout } = await execPromise(psCmd, { timeout: 4000 });
    return JSON.parse(stdout.trim());
  } catch (err) {
    return { success: false, frequencyMhz: 3500 };
  }
});

app.whenReady().then(createWindow);app.on('window-all-closed',()=>app.quit());

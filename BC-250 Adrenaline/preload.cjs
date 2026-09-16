const {contextBridge,ipcRenderer}=require('electron');
 contextBridge.exposeInMainWorld('desktop',{
 setTheme:theme=>ipcRenderer.send('theme',theme),
 copyDonation:(kind,id)=>ipcRenderer.invoke('copy-donation',kind,id),
 openBMC:()=>ipcRenderer.invoke('open-bmc'),
 openKoFi:()=>ipcRenderer.invoke('open-bmc'),
 checkDriverInstalled:()=>ipcRenderer.invoke('check-driver-installed'),
 getSystemTelemetry:()=>ipcRenderer.invoke('get-system-telemetry'),
 applyCpuOverclock:freqMhz=>ipcRenderer.invoke('apply-cpu-overclock',freqMhz),
 getCpuOverclock:()=>ipcRenderer.invoke('get-cpu-overclock')
});

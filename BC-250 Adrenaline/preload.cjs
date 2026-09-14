const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('desktop',{
 setTheme:theme=>ipcRenderer.send('theme',theme),
 copyDonation:(kind,id)=>ipcRenderer.invoke('copy-donation',kind,id),
 openKoFi:()=>ipcRenderer.invoke('open-kofi')
});

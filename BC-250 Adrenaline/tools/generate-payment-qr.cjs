const QRCode=require('qrcode'),fs=require('node:fs'),path=require('node:path');
const {config}=require('../donations.js');
(async()=>{const root=path.resolve(__dirname,'..');fs.mkdirSync(path.join(root,'assets/payments'),{recursive:true});
const entries=[{payload:config.pix.payload,file:config.pix.qr},...config.crypto.filter(c=>c.enabled&&c.network).map(c=>({payload:c.address,file:c.qr}))];
for(const {payload,file} of entries)await QRCode.toFile(path.join(root,file),payload,{type:'png',width:512,margin:4,errorCorrectionLevel:'M',color:{dark:'#000000',light:'#ffffff'}});
console.log('Generated '+entries.length+' local QR codes from the canonical payment configuration.');
})().catch(e=>{console.error(e);process.exitCode=1;});

(function(root){
 const config={
  pix:{payload:'00020126580014BR.GOV.BCB.PIX01361fd7ab76-29f4-418d-88f8-4b211f1b1b7f5204000053039865802BR5924DANIEL DE LIMA FRANCISCO6015SAO JOSE DOS CA62070503***63044128',key:'1fd7ab76-29f4-418d-88f8-4b211f1b1b7f',keyType:'Chave aleatória',recipient:'DANIEL DE LIMA FRANCISCO',qr:'assets/payments/pix.png'},
  crypto:[
   {id:'btc',name:'Bitcoin',symbol:'BTC',network:'Bitcoin',address:'bc1qlq3xn00m6mtsueasak6yj7e2m5h5l4au6uvakp',enabled:true,qr:'assets/payments/btc.png'},
   {id:'sol',name:'Solana',symbol:'SOL',network:'Solana',address:'8Z3Dx3TKga4CjRuN66M6MVQrp9w1kFNN1e5SeoB7zgE3',enabled:true,qr:'assets/payments/sol.png'},
   {id:'eth',name:'Ethereum',symbol:'ETH',network:null,address:'0x15AC419674297ad8D1542a6390557482cD7622d6',enabled:false,reason:'Rede de depósito não confirmada'},
   {id:'usdt',name:'Tether',symbol:'USDT',network:null,address:'0x15AC419674297ad8D1542a6390557482cD7622d6',enabled:false,reason:'Rede de depósito não confirmada'}
  ],
  kofi:{url:'https://ko-fi.com/bc250windv',embed:'https://ko-fi.com/bc250windv/?hidefeed=true&widget=true&embed=true&preview=true'}
 };
 function freeze(value){Object.values(value).forEach(v=>{if(v&&typeof v==='object')freeze(v);});return Object.freeze(value);}
 function copyValue(kind,id){if(kind==='pix-code')return config.pix.payload;if(kind==='pix-key')return config.pix.key;if(kind==='crypto'){const item=config.crypto.find(c=>c.id===id&&c.enabled&&c.network);return item?.address??null;}return null;}
 const api=Object.freeze({config:freeze(config),copyValue});
 if(typeof module!=='undefined')module.exports=api;else root.Donations=api;
})(typeof window==='undefined'?globalThis:window);

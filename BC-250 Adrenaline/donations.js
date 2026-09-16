(function(root){
 const config={
  pix:{payload:'00020126580014BR.GOV.BCB.PIX01361fd7ab76-29f4-418d-88f8-4b211f1b1b7f5204000053039865802BR5924DANIEL DE LIMA FRANCISCO6015SAO JOSE DOS CA62070503***63044128',key:'1fd7ab76-29f4-418d-88f8-4b211f1b1b7f',keyType:'Chave aleatória',recipient:'DANIEL DE LIMA FRANCISCO',qr:'assets/payments/pix.png'},
  crypto:[
   {id:'btc',name:'Bitcoin',symbol:'BTC',network:'Bitcoin',address:'bc1qlq3xn00m6mtsueasak6yj7e2m5h5l4au6uvakp',enabled:true,qr:'assets/payments/btc.png'},
   {id:'eth',name:'Ethereum',symbol:'ETH',network:'Ethereum',address:'0x15AC419674297ad8D1542a6390557482cD7622d6',enabled:true,qr:'assets/payments/eth.png'},
   {id:'ltc',name:'Litecoin',symbol:'LTC',network:'Litecoin',address:'ltc1qmqmzss4a7qftvpa3teesrwtvmgp6275w2jj87z',enabled:true,qr:'assets/payments/ltc.png'}
  ],
  bmc:{id:'danieldelima',url:'https://www.buymeacoffee.com/danieldelima',embed:'https://www.buymeacoffee.com/widget/page/danieldelima?description=Support%20me%20on%20Buy%20me%20a%20coffee!&color=%23FF5F5F',qr:'assets/qr-code.png'},
  kofi:{url:'https://www.buymeacoffee.com/danieldelima',embed:'https://www.buymeacoffee.com/widget/page/danieldelima?description=Support%20me%20on%20Buy%20me%20a%20coffee!&color=%23FF5F5F'}
 };
 function freeze(value){Object.values(value).forEach(v=>{if(v&&typeof v==='object')freeze(v);});return Object.freeze(value);}
 function copyValue(kind,id){if(kind==='pix-code')return config.pix.payload;if(kind==='pix-key')return config.pix.key;if(kind==='crypto'){const item=config.crypto.find(c=>c.id===id&&c.enabled&&c.network);return item?.address??null;}if(kind==='bmc'||kind==='bmc-url'||kind==='card')return config.bmc.url;return null;}
 const api=Object.freeze({config:freeze(config),copyValue});
 if(typeof module!=='undefined')module.exports=api;else root.Donations=api;
})(typeof window==='undefined'?globalThis:window);

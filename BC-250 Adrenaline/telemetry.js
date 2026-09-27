/* UI-only source. No driver, registry, PMBus or device access. */
(function(root){
  const scenarios={
    balanced:{name:'Uso cotidiano',description:'Navegação e tarefas leves',icon:'home',load:28,temp:51},
    idle:{name:'Em repouso',description:'Clocks e consumo reduzidos',icon:'moon',load:5,temp:35},
    gaming:{name:'Em jogo',description:'GPU em carga simulada',icon:'game',load:83,temp:68},
    hot:{name:'Carga elevada',description:'Explore as cores do mapa',icon:'fire',load:96,temp:91},
    unavailable:{name:'Indisponível',description:'Sensores sem leitura',icon:'info',load:0,temp:0}
  };
  const fields=[
    ['cpu_temp_c','CPU · temperatura','°C','software.cpu_temp_c'],
    ['gpu_temp_c','GPU · temperatura','°C','software.gpu_temp_c'],
    ['cpu_freq_mhz','CPU · clock médio','MHz','software.cpu_freq_mhz'],
    ['gpu_sclk_mhz','GPU · clock','MHz','software.gpu_sclk_mhz'],
    ['cpu_load','CPU · utilização','%','software.cpu_load'],
    ['gpu_load','GPU · utilização','%','software.gpu_load'],
    ['gpu_ppt_w','GPU · PPT','W','software.gpu_ppt_w'],
    ['nvme_temp_c','NVMe · temperatura','°C','software.nvme_temp_c'],
    ['nct_t14_c','NCT · sensor T14','°C','software.nct_t14_c'],
    ['nct_t15_c','NCT · sensor T15','°C','software.nct_t15_c'],
    ['fan_rpm','Ventoinha · rotação','RPM','cooling.fan_rpm'],
    ['fan_pwm_pct','Ventoinha · PWM','%','cooling.fan_pwm_pct'],
    ['cpu_vin','CPU VRM · entrada','V','hardware.cpu.vin'],
    ['cpu_vout','CPU VRM · saída','V','hardware.cpu.vout'],
    ['cpu_iout','CPU VRM · corrente','A','hardware.cpu.iout'],
    ['cpu_pout','CPU VRM · potência','W','hardware.cpu.pout'],
    ['cpu_vrm_temp','CPU VRM · temperatura','°C','hardware.cpu.temp'],
    ['gpu_vin','GPU VRM · entrada','V','hardware.gpu.vin'],
    ['gpu_vout','GPU VRM · saída','V','hardware.gpu.vout'],
    ['gpu_iout','GPU VRM · corrente','A','hardware.gpu.iout'],
    ['gpu_pout','GPU VRM · potência','W','hardware.gpu.pout'],
    ['gpu_vrm_temp','GPU VRM · temperatura','°C','hardware.gpu.temp'],
    ['total_power','VRM · potência total','W','hardware.total_power'],
    ['memory_average','GDDR6 · temperatura média','°C','memory.average_c'],
    ['memory_hotspot','GDDR6 · hotspot','°C','memory.hotspot_c'],
    ['memory_used','Memória compartilhada · uso','GB','software.memory_used'],
    ...Array.from({length:8},(_,i)=>['ram_'+i,'Memória '+(i+1)+' · temperatura','°C','memory.chips_c.'+i])
  ].map(([id,label,unit,path])=>({id,label,unit,path}));
  const rounded=(n,d=1)=>Number(n.toFixed(d));
  const clamp=(n,min,max)=>Math.max(min,Math.min(max,n));
  function heatColor(t){
    if(!Number.isFinite(t))return '#73818a';
    const level=clamp((t-20)/100,0,1)*2;
    const colors=[[56,189,248],[251,191,36],[251,113,133]];
    const segment=Math.min(1,Math.floor(level));
    const mix=level-segment;
    return 'rgb('+colors[segment].map((n,i)=>Math.round(n+(colors[segment+1][i]-n)*mix)).join(', ')+')';
  }
  function sample(index=0,scenario='balanced',now=Date.now()){
    const config=scenarios[scenario]||scenarios.balanced;
    const valid=scenario!=='unavailable';
    const wave=Math.sin(index*.21)*2.1+Math.sin(index*.73)*.7;
    const gpu=rounded(config.temp+wave);
    const cpu=rounded(config.temp-3+Math.sin(index*.15+1)*3.5);
    const gpuLoad=rounded(clamp(config.load+Math.sin(index*.5)*6,1,100));
    const cpuLoad=rounded(clamp(config.load*.59+Math.sin(index*.37)*7,1,100));
    const chips=Array.from({length:8},(_,i)=>valid?rounded(config.temp-5+[0,3,-2,7,2,5,-1,4][i]+Math.sin(index*.16+i)*2):null);
    const cpuVout=rounded(.7+cpuLoad*.003,3),gpuVout=rounded(.65+gpuLoad*.003,3);
    const cpuCurrent=rounded(5+cpuLoad*.35),gpuCurrent=rounded(8+gpuLoad*.63);
    const cpuPower=rounded(cpuVout*cpuCurrent),gpuPower=rounded(gpuVout*gpuCurrent);
    const value=n=>valid?n:null;
    return {source:'mock',schema_version:1,timestamp:now,scenario,
      hardware:{cpu:{valid,vin:value(12.1),vout:value(cpuVout),iout:value(cpuCurrent),pout:value(cpuPower),temp:value(rounded(cpu-4))},
        gpu:{valid,vin:value(12.1),vout:value(gpuVout),iout:value(gpuCurrent),pout:value(gpuPower),temp:value(rounded(gpu-6))},
        total_power:value(rounded(cpuPower+gpuPower)),total_power_valid:valid},
      software:{cpu_temp_c:value(cpu),gpu_temp_c:value(gpu),cpu_freq_mhz:value(Math.round(1700+cpuLoad*18)),gpu_sclk_mhz:value(Math.round(400+gpuLoad*16)),gpu_ppt_w:value(rounded(gpuPower+16)),nvme_temp_c:value(rounded(34+config.load*.12+wave*.3)),nct_t14_c:value(rounded(30+config.load*.16)),nct_t15_c:value(rounded(32+config.load*.18)),cpu_load:value(cpuLoad),gpu_load:value(gpuLoad),memory_used:value(rounded(2+config.load*.095))},
      cooling:{fan_rpm:value(Math.round(680+config.load*19+wave*12)),fan_pwm_pct:value(Math.round(20+config.load*.67))},
      memory:{valid,status:valid?'ok':'unavailable',error:valid?null:'Cenário de sensores indisponíveis',age_ms:valid?0:null,chips_c:chips,average_c:value(rounded(chips.reduce((a,b)=>a+b,0)/8)),hotspot_c:valid?Math.max(...chips):null,hotspot_chip:valid?chips.indexOf(Math.max(...chips)):null,saturated:false,saturated_chips:[],raw:Array(8).fill(null)}
    };
  }
  function read(data,path){return path.split('.').reduce((o,k)=>o?.[k],data)??null;}
  function isValid(v){return typeof v==='number'&&Number.isFinite(v);}
  class Session {
    constructor(){this.history=[];this.extrema={};this.index=0;}
    next(scenario,now){const data=sample(this.index++,scenario,now);this.history.push(data);if(this.history.length>60)this.history.shift();
      for(const field of fields){const v=read(data,field.path);if(!isValid(v))continue;
        const prev=this.extrema[field.id];this.extrema[field.id]={min:prev?Math.min(prev.min,v):v,max:prev?Math.max(prev.max,v):v};}
      return data;}
  }
  const api={scenarios,fields,heatColor,sample,read,isValid,Session};
  if(typeof module!=='undefined')module.exports=api;else root.Telemetry=api;
})(typeof window==='undefined'?globalThis:window);

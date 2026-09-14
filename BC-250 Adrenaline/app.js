'use strict';
const $=s=>document.querySelector(s), $$=s=>Array.from(document.querySelectorAll(s));
const icons={
  home:'M3 10 12 3l9 7M5 9v11h5v-6h4v6h5V9',
  chip:'M7 7h10v10H7zM3 8h4m-4 4h4m-4 4h4m10-8h4m-4 4h4m-4 4h4M8 3v4m4-4v4m4-4v4M8 17v4m4-4v4m4-4v4',
  pulse:'M2 12h5l3-8 4 16 3-8h5',chart:'M4 3v17h17M8 15l4-5 4 3 5-7',download:'M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5',
  heart:'M12 20 4 12C-2 5 7 0 12 7c5-7 14-2 8 5z',settings:'M9 3h6l1 3 3 1 2 5-2 5-3 1-1 3H9l-1-3-3-1-2-5 2-5 3-1zM15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0',
  info:'M12 16v-5m0-4v.1M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0',check:'m5 12 4 4L19 6',
  shield:'M12 2 3 6v6c0 5 9 10 9 10s9-5 9-10V6zM8 12l3 3 5-6',pause:'M8 5v14M16 5v14',play:'m8 4 12 8-12 8z',
  board:'M2 5h20v14H2zM6 8h6v8H6zM16 8h3m-3 4h3m-3 4h3',fan:'M12 10C1-3 24-1 15 11m-1 2c17-2 3 17-2 2m-2-1C-5 21-2 0 10 11M14 12a2 2 0 1 1-4 0 2 2 0 0 1 4 0',
  search:'M16 10a6 6 0 1 1-12 0 6 6 0 0 1 12 0m-2 4 7 7',refresh:'M20 7a9 9 0 1 0 1 8M20 2v6h-6',
  monitor:'M2 3h20v14H2zM12 17v4m-5 0h10',disk:'M5 3h14v18H5zM15 14a3 3 0 1 1-6 0 3 3 0 0 1 6 0M8 6h8',sun:'M16 12a4 4 0 1 1-8 0 4 4 0 0 1 8 0M12 1v3m0 16v3M1 12h3m16 0h3M4 4l2 2m12 12 2 2M4 20l2-2M18 6l2-2',
  coffee:'M4 8h12v9a4 4 0 0 1-4 4H8a4 4 0 0 1-4-4zM16 9h2a3 3 0 0 1 0 6h-2M7 2v3m5-3v3',
  temp:'M9 14V5a3 3 0 0 1 6 0v9a5 5 0 1 1-6 0M12 8v10',bolt:'m13 2-9 12h7l-1 8 10-13h-7z',
  memory:'M3 6h18v11H3zM6 17v3m4-3v3m4-3v3m4-3v3M7 9v5m5-5v5m5-5v5',moon:'M20 15A9 9 0 0 1 9 4 9 9 0 1 0 20 15',
  game:'M7 7h10c4 0 8 13 3 13-2 0-3-4-5-4H9c-2 0-3 4-5 4C-1 20 3 7 7 7zM6 10v5m-2-2h4m8-2v.1m3 3v.1',fire:'M12 2c1 6 7 6 7 13a7 7 0 0 1-14 0c0-4 2-6 4-8 0 3 1 4 2 4s3-3 1-9'
};
function icon(name){return '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="'+(icons[name]||icons.info)+'"/></svg>';}
function paintIcons(){$$('[data-icon]').forEach(el=>{el.innerHTML=icon(el.dataset.icon);el.removeAttribute('data-icon');});}
const defaults={installed:false,installedAt:null,theme:'system',interval:1000,scenario:'balanced',monitor:'LG UltraWide 29"',nvme:'Kingston NV2 · 1 TB'};
let preferences={...defaults};
try{const saved=JSON.parse(localStorage.getItem('bc250-adrenaline-v1')||'null');if(saved&&typeof saved==='object')preferences={...defaults,...saved};}catch{}
preferences.installed=preferences.installed===true;
if(!['system','light','dark'].includes(preferences.theme))preferences.theme='system';
if(![700,1000,2000].includes(preferences.interval))preferences.interval=1000;
if(!Telemetry.scenarios[preferences.scenario])preferences.scenario='balanced';
for(const key of ['monitor','nvme'])if(typeof preferences[key]!=='string')preferences[key]=defaults[key];
let page='overview',paused=false,selectedComponent='cpu',timer=null,current=null,installTimer=null,installPercent=0,paymentMethod='pix',donationReturn=null,toastTimer;
let session=new Telemetry.Session();
const titles={overview:['Visão geral','Um olhar completo sobre a sua placa.'],sensors:['Sensores','Todas as leituras da placa, em um só lugar.'],performance:['Desempenho','Explore o comportamento da sua BC-250.'],driver:['Driver','Gerencie a experiência de instalação da sua placa.'],system:['Sistema','Conheça o hardware por trás da experiência.'],settings:['Configurações','Deixe o Adrenaline do seu jeito.']};
function save(){try{localStorage.setItem('bc250-adrenaline-v1',JSON.stringify(preferences));}catch{toast('Não foi possível salvar as preferências. Elas valem apenas nesta sessão.');}}
function toast(message){clearTimeout(toastTimer);$('#toast').textContent=message;$('#toast').hidden=false;toastTimer=setTimeout(()=>$('#toast').hidden=true,4500);}
const media=matchMedia('(prefers-color-scheme: dark)');
function applyTheme(){const theme=preferences.theme==='system'?(media.matches?'dark':'light'):preferences.theme;document.documentElement.dataset.theme=theme;window.desktop?.setTheme(theme);}
media.addEventListener('change',applyTheme);
function format(v,unit='',digits){if(!Telemetry.isValid(v))return '—';const precision=digits??(['MHz','RPM'].includes(unit)?0:unit==='V'?3:1);return new Intl.NumberFormat('pt-BR',{maximumFractionDigits:precision}).format(v)+(unit?' '+unit:'');}
function escapeHtml(value){return String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function navigate(next,focus=false){
  if(!titles[next])return;page=next;
  $$('.nav-item[data-page]').forEach(el=>{el.classList.toggle('selected',el.dataset.page===page);if(el.dataset.page===page)el.setAttribute('aria-current','page');else el.removeAttribute('aria-current');});
  const installation=!preferences.installed&&['overview','driver'].includes(page);
  const locked=!preferences.installed&&!installation&&page!=='settings';
  $('#install-view').hidden=!installation;$('#locked-view').hidden=!locked;$('#dashboard-view').hidden=installation||locked;
  $('#locked-title').textContent=titles[page][0];$('#page-title').textContent=titles[page][0];$('#page-subtitle').textContent=titles[page][1];
  $$('[data-view]').forEach(el=>el.hidden=el.dataset.view!==page);
  $('#driver-dot').hidden=preferences.installed;
  $('.page-tools').hidden=!preferences.installed||['settings','driver','system'].includes(page);
  if(preferences.installedAt&&Number.isFinite(Date.parse(preferences.installedAt)))$('#installed-at').textContent=new Date(preferences.installedAt).toLocaleDateString('pt-BR');
  if(current)renderTelemetry();
  if(focus){window.scrollTo({top:0});$('#main').focus({preventScroll:true});}
}
function boardSetup(){
  $('#thermal-board').innerHTML=window.BC250_BOARD;
  ['cpu',...Array.from({length:8},(_,i)=>'ram-'+(i+1))].forEach(id=>{
    const el=$('#thermal-board #'+id);if(!el)return;
    el.setAttribute('role','button');el.setAttribute('tabindex','0');
    const title=document.createElementNS('http://www.w3.org/2000/svg','title');el.appendChild(title);
    el.addEventListener('click',()=>selectComponent(id));
    el.addEventListener('keydown',e=>{if(['Enter',' '].includes(e.key)){e.preventDefault();selectComponent(id);}});
  });
  $('#memory-grid').innerHTML=Array.from({length:8},(_,i)=>'<button class="memory-cell" data-component="ram-'+(i+1)+'"><span>MEMÓRIA '+(i+1)+'</span><strong>—</strong><span class="heat-bar"></span></button>').join('');
  updateDeviceNames();
}
function updateDeviceNames(){
  const monitor=preferences.monitor.trim()||defaults.monitor,nvme=preferences.nvme.trim()||defaults.nvme;
  ['#dp-monitor-name','#preview-dp-monitor-name'].forEach(id=>{const el=$(id);if(el)el.textContent=monitor;});
  ['#nvme-info','#preview-nvme-info'].forEach(id=>{const el=$(id);if(el)el.textContent=nvme;});
  $('#system-monitor').textContent=monitor;$('#system-nvme').textContent=nvme;
}
function selectComponent(id){selectedComponent=id;renderBoard();}
function renderBoard(){
  if(!current)return;
  const cpu=current.software.cpu_temp_c,gpu=current.software.gpu_temp_c;
  const apu=Telemetry.isValid(cpu)&&Telemetry.isValid(gpu)?Math.max(cpu,gpu):null;
  for(const id of ['cpu',...Array.from({length:8},(_,i)=>'ram-'+(i+1))]){
    const v=id==='cpu'?apu:current.memory.chips_c[Number(id.split('-')[1])-1];
    const el=$('#thermal-board #'+id);if(!el)continue;
    const label=(id==='cpu'?'APU · CPU / GPU':'Memória '+id.split('-')[1])+': '+(v===null?'Sensor indisponível':format(v,'°C'))+' · simulado';
    el.setAttribute('fill',Telemetry.heatColor(v));el.setAttribute('aria-label',label);el.setAttribute('aria-pressed',String(id===selectedComponent));el.querySelector('title').textContent=label;
    const cell=$('[data-component="'+id+'"]');if(cell){cell.style.setProperty('--color',Telemetry.heatColor(v));cell.querySelector('strong').textContent=format(v,'°C');cell.classList.toggle('selected',id===selectedComponent);cell.setAttribute('aria-pressed',String(id===selectedComponent));}
  }
  const n=selectedComponent==='cpu'?null:Number(selectedComponent.split('-')[1]);
  const v=n===null?apu:current.memory.chips_c[n-1];
  $('#component-name').textContent=n===null?'APU · CPU / GPU':'Memória '+n+' · GDDR6';
  $('#component-temp').textContent=format(v);$('#component-temp').style.color=Telemetry.heatColor(v);
  $('#component-source').textContent=n===null?'Maior entre CPU e GPU':'Chip '+n+' · posição visual';
  $('#component-band').textContent=v===null?'Sensor indisponível':v<50?'Faixa fria · simulação':v<80?'Faixa intermediária':'Faixa quente · simulação';
  $('#sample-interval').textContent=preferences.interval===700?'700 ms':preferences.interval/1000+' segundo'+(preferences.interval>1000?'s':'');
}
const metrics=[['gpu_temp_c','Temperatura GPU','temp','°C'],['cpu_temp_c','Temperatura CPU','chip','°C'],['total_power','Potência dos VRMs','bolt','W'],['memory_used','Memória compartilhada','memory','GB']];
function setupMetrics(){
 $('#overview-metrics').innerHTML=metrics.map(([id,label,ic,unit])=>'<article class="card metric" id="metric-'+id+'"><div class="metric-title"><i>'+icon(ic)+'</i>'+label+'</div><div class="metric-value"><span>—</span><small>'+unit+'</small></div><p class="metric-sub"></p><svg class="sparkline" viewBox="0 0 250 30" preserveAspectRatio="none" aria-hidden="true"><path fill="none" stroke="'+(id==='cpu_temp_c'?'#8853cf':'#0078d4')+'" stroke-width="1.5"/></svg></article>').join('');
 $('#scenario-cards').innerHTML=Object.entries(Telemetry.scenarios).map(([id,c])=>'<button class="scenario-card" data-scenario="'+id+'"><i>'+icon(c.icon)+'</i><strong>'+c.name+'</strong><span>'+c.description+'</span></button>').join('');
 syncScenario();
}
function syncScenario(){
 $('#scenario-select').value=preferences.scenario;
 $$('[data-scenario]').forEach(el=>{el.classList.toggle('selected',el.dataset.scenario===preferences.scenario);el.setAttribute('aria-pressed',String(el.dataset.scenario===preferences.scenario));});
}
function setScenario(scenario){if(!Telemetry.scenarios[scenario])return;preferences.scenario=scenario;save();syncScenario();if(!paused&&preferences.installed)tick();else toast('O cenário será aplicado quando a simulação continuar.');}
function linePath(values,w,h,min,max,padding=0){
  let active=false;return values.map((v,i)=>{if(!Telemetry.isValid(v)){active=false;return '';}
    const x=padding+i/Math.max(1,values.length-1)*(w-padding*2),y=padding+(1-(v-min)/(max-min))*(h-padding*2);
    const cmd=active?'L':'M';active=true;return cmd+x.toFixed(1)+','+y.toFixed(1);}).join(' ');
}
function chart(container,paths,colors,unit,max){
  const w=460,h=145,left=30,top=8,plotH=105,plotW=423;
  let content='';for(let i=0;i<4;i++){const y=top+i*plotH/3;content+='<line class="grid-line" x1="'+left+'" x2="455" y1="'+y+'" y2="'+y+'"/><text x="0" y="'+(y+3)+'">'+Math.round(max-i*max/3)+'</text>';}
  paths.forEach((path,i)=>{const vals=session.history.map(s=>Telemetry.read(s,path));content+='<path transform="translate('+left+','+top+')" d="'+linePath(vals,plotW,plotH,0,max)+'" stroke="'+colors[i]+'" stroke-width="2" fill="none" stroke-linejoin="round"/>';});
  const seconds=session.history.length>1?Math.round((session.history.at(-1).timestamp-session.history[0].timestamp)/1000):0;
  content+='<text x="30" y="140">'+seconds+' s atrás</text><text x="455" y="140" text-anchor="end">Agora</text>';
  if(!session.history.some(s=>paths.some(p=>Telemetry.isValid(Telemetry.read(s,p)))))content+='<text x="245" y="65" text-anchor="middle">Nenhuma leitura disponível</text>';
  $(container).innerHTML='<svg viewBox="0 0 '+w+' '+h+'" preserveAspectRatio="none" role="img" aria-label="Histórico simulado em '+unit+'">'+content+'</svg>';

}
function renderTelemetry(){
 if(!current)return;
 const sw=current.software;
 $('#sample-status').innerHTML='<span class="live-dot"></span>'+(paused?'Simulação pausada':current.memory.valid?'Simulação ao vivo':'Sensores indisponíveis');
 $('#sample-status .live-dot').style.background=paused?'#8b8b8b':current.memory.valid?'var(--green)':'#b67b20';
 $('#last-update').textContent=(paused?'Pausado · última amostra ':'Amostra simulada · ')+new Date(current.timestamp).toLocaleTimeString('pt-BR');
 $$('[data-reading]').forEach(el=>{const field=Telemetry.fields.find(f=>f.id===el.dataset.reading);el.textContent=format(Telemetry.read(current,field.path),field.unit);});
 if(page==='overview'){
  metrics.forEach(([id,label,ic,unit])=>{const field=Telemetry.fields.find(f=>f.id===id),card=$('#metric-'+id),v=Telemetry.read(current,field.path);card.querySelector('.metric-value>span').textContent=format(v);
   const detail={gpu_temp_c:format(sw.gpu_sclk_mhz,'MHz')+' · Clock gráfico',cpu_temp_c:format(sw.cpu_freq_mhz,'MHz')+' · Clock médio',total_power:'CPU + GPU · potência de saída',memory_used:'de 16 GB · memória compartilhada'};
   card.querySelector('.metric-sub').textContent=v===null?'Sensor indisponível':detail[id];
   const values=session.history.map(s=>Telemetry.read(s,field.path)),good=values.filter(Telemetry.isValid),min=good.length?Math.min(...good)-3:0,max=good.length?Math.max(...good)+3:100;
   card.querySelector('.sparkline path').setAttribute('d',linePath(values,250,30,min,max,3));
  });
  renderBoard();chart('#temp-chart',['software.cpu_temp_c','software.gpu_temp_c'],['#8853cf','#0078d4'],'°C',120);
  const rows=[['fan','Ventoinha',format(current.cooling.fan_rpm,'RPM')+' · '+format(current.cooling.fan_pwm_pct,'%')],['memory','Memória · média',format(current.memory.average_c,'°C')],['temp','Memória · hotspot',format(current.memory.hotspot_c,'°C')+(current.memory.valid?' · Chip '+(current.memory.hotspot_chip+1):'')],['disk','NVMe',format(sw.nvme_temp_c,'°C')]];
  $('#cooling-summary').innerHTML=rows.map(([ic,label,v])=>'<div class="summary-row"><i>'+icon(ic)+'</i><span>'+label+'</span><strong>'+v+'</strong></div>').join('');
 }
 if(page==='sensors')renderSensors();
 if(page==='performance'){chart('#cpu-chart',['software.cpu_load'],['#0078d4'],'%',100);chart('#gpu-chart',['software.gpu_load'],['#8853cf'],'%',100);$('#cpu-load').textContent=format(sw.cpu_load,'%');$('#gpu-load').textContent=format(sw.gpu_load,'%');}
}
function renderSensors(){
 const search=$('#sensor-search').value.toLocaleLowerCase('pt-BR');
 const fields=Telemetry.fields.filter(f=>f.label.toLocaleLowerCase('pt-BR').includes(search));
 $('#sensor-rows').innerHTML=fields.map(f=>{const value=Telemetry.read(current,f.path),ext=session.extrema[f.id];return '<tr><td>'+f.label+'</td><td>'+format(value,f.unit)+'</td><td>'+format(ext?.min,f.unit)+'</td><td>'+format(ext?.max,f.unit)+'</td><td><span class="pill">'+(value===null?'Indisponível':'Simulado')+'</span></td></tr>';}).join('');
 $('#no-sensors').hidden=fields.length>0;
}
function tick(){if(paused||!preferences.installed)return;current=session.next(preferences.scenario,Date.now());renderTelemetry();}
function startTimer(){clearInterval(timer);timer=setInterval(tick,preferences.interval);}
function setPaused(value){paused=value;$('#pause-button').innerHTML='<i>'+icon(paused?'play':'pause')+'</i><span>'+(paused?'Continuar':'Pausar')+'</span>';$('#pause-button').title=paused?'Continuar telemetria simulada':'Pausar telemetria simulada';if(!paused)tick();else renderTelemetry();}
function resetInstallUI(){
 installPercent=0;$('#installation-progress').hidden=true;$('#install-button').hidden=false;$('#cancel-install').hidden=true;$('#finish-install').hidden=true;$('#install-title').textContent='Prepare sua placa';$('#install-description').textContent='O driver ainda não está instalado neste cenário de demonstração.';$('#install-state').textContent='Instalação pendente';$('#install-state').className='pill amber';$('#install-progress').value=0;
}
function startInstall(){
 if(installTimer)return;
 $('#installation-progress').hidden=false;$('#install-button').hidden=true;$('#cancel-install').hidden=false;$('#install-title').textContent='Preparando sua experiência';$('#install-description').textContent='Acompanhe as etapas da instalação simulada.';$('#install-state').textContent='Em andamento · demo';$('#install-state').className='pill blue';
 const steps=['Verificando compatibilidade simulada…','Preparando o pacote de demonstração…','Simulando instalação do driver…','Verificando o ambiente de demonstração…'];
 installPercent=0;
 const advance=()=>{
  installPercent=Math.min(100,installPercent+2);const index=Math.min(3,Math.floor(installPercent/25));
  $('#install-progress').value=installPercent;$('#step-percent').textContent=installPercent+'%';$('#step-label').textContent=steps[index];
  $$('#install-steps li').forEach((el,i)=>{el.className=i<index?'done':i===index?'active':'';});
  if(installPercent%25===0)$('#install-live').textContent=steps[index];
  if(installPercent===100){clearInterval(installTimer);installTimer=null;preferences.installed=true;preferences.installedAt=new Date().toISOString();save();$('#cancel-install').hidden=true;$('#finish-install').hidden=false;$('#install-title').textContent='Instalação simulada concluída!';$('#install-description').textContent='Tudo pronto para conhecer sua BC-250. Nenhum driver real foi instalado.';$('#install-state').textContent='Sucesso · demonstração';$('#install-state').className='pill green';$('#step-label').textContent='Todas as etapas concluídas';$('#install-live').textContent='Instalação simulada concluída com sucesso.';$$('#install-steps li').forEach(el=>el.className='done');tick();openDonation();}
 };
 advance();installTimer=setInterval(advance,180);
}
function openDonation(){
 donationReturn=document.activeElement;renderPayment();if(!$('#donation-dialog').open)$('#donation-dialog').showModal();
}
function closeDonation(){
 $('#donation-dialog').close();DonationUI.clear();if(preferences.installed){navigate('overview',true);}else donationReturn?.focus();
}
function renderPayment(){
 $$('[data-method]').forEach(el=>{el.setAttribute('aria-selected',String(el.dataset.method===paymentMethod));el.tabIndex=el.dataset.method===paymentMethod?0:-1;});
 DonationUI.render(paymentMethod);
}

function resetDemo(){
 clearInterval(installTimer);installTimer=null;preferences.installed=false;preferences.installedAt=null;save();session=new Telemetry.Session();current=null;paused=false;$('#pause-button').innerHTML='<i>'+icon('pause')+'</i><span>Pausar</span>';resetInstallUI();$('#reset-dialog').close();navigate('overview',true);
}
function exportSample(){if(!current){toast('Ainda não há amostra. Conclua a instalação simulada.');return;}const blob=new Blob([JSON.stringify({...current,device:{monitor:preferences.monitor,nvme:preferences.nvme},session_extrema:session.extrema},null,2)],{type:'application/json'}),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='bc250-amostra-SIMULADA-'+new Date().toISOString().replace(/[:.]/g,'-')+'.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),10000);toast('Amostra simulada exportada em JSON.');}
document.addEventListener('click',e=>{
 const nav=e.target.closest('[data-page]');if(nav){navigate(nav.dataset.page,true);return;}
 const component=e.target.closest('[data-component]');if(component){selectComponent(component.dataset.component);return;}
 const scenario=e.target.closest('[data-scenario]');if(scenario){setScenario(scenario.dataset.scenario);return;}
 const method=e.target.closest('[data-method]');if(method){paymentMethod=method.dataset.method;renderPayment();return;}
 const action=e.target.closest('[data-action]')?.dataset.action;
 if(action==='donate')openDonation();
 if(action==='close-donation')closeDonation();
 if(action==='export')exportSample();
 if(action==='reset')$('#reset-dialog').showModal();
 if(action==='cancel-reset')$('#reset-dialog').close();
 if(action==='confirm-reset')resetDemo();
 if(action==='go-install')navigate('driver',true);
 if(action==='skip-install'){preferences.installed=true;preferences.installedAt=new Date().toISOString();save();tick();navigate('overview',true);}
 if(action==='check-update'){$('#update-result').textContent='Demonstração: você está na versão mais recente do cenário (0.1-demo). Nenhum servidor foi consultado.';}
});
$('#install-button').addEventListener('click',startInstall);
$('#cancel-install').addEventListener('click',()=>{clearInterval(installTimer);installTimer=null;resetInstallUI();toast('Instalação simulada cancelada. Você pode começar novamente.');});
$('#finish-install').addEventListener('click',()=>navigate('overview',true));
$('#pause-button').addEventListener('click',()=>setPaused(!paused));
$('#sensor-search').addEventListener('input',()=>{if(current)renderSensors();});
$('#donation-dialog').addEventListener('cancel',e=>{e.preventDefault();closeDonation();});
$('.payment-tabs').addEventListener('keydown',e=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(e.key))return;e.preventDefault();const methods=['pix','card','crypto'];let index=methods.indexOf(paymentMethod);index=e.key==='Home'?0:e.key==='End'?2:(index+(e.key==='ArrowRight'?1:2))%3;paymentMethod=methods[index];renderPayment();$('[data-method="'+paymentMethod+'"]').focus();});
$('#theme-select').value=preferences.theme;$('#theme-select').addEventListener('change',e=>{preferences.theme=e.target.value;save();applyTheme();});
$('#interval-select').value=preferences.interval;$('#interval-select').addEventListener('change',e=>{preferences.interval=Number(e.target.value);save();startTimer();renderBoard();});
$('#scenario-select').addEventListener('change',e=>setScenario(e.target.value));
for(const key of ['monitor','nvme']){const input=$('#'+key+'-input');input.value=preferences[key];input.addEventListener('input',()=>{preferences[key]=input.value;save();updateDeviceNames();});}
$$('.nav-item').forEach(el=>{el.title=el.textContent.trim();el.setAttribute('aria-label',el.textContent.trim());});
applyTheme();paintIcons();boardSetup();setupMetrics();navigate('overview');tick();startTimer();
window.addEventListener('beforeunload',()=>{clearInterval(timer);clearInterval(installTimer);});

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
const defaults={installed:false,installedAt:null,theme:'system',interval:1000,scenario:'balanced',monitor:'LG UltraWide 29"',nvme:'Kingston NV2 · 1 TB',language:'pt',vram:512,installPath:'C:\\Program Files\\AMD\\BC-250 Adrenaline'};
let preferences={...defaults};
try{const saved=JSON.parse(localStorage.getItem('bc250-adrenaline-v1')||'null');if(saved&&typeof saved==='object')preferences={...defaults,...saved};}catch{}
preferences.installed=preferences.installed===true;
if(!['system','light','dark'].includes(preferences.theme))preferences.theme='system';
if(![700,1000,2000].includes(preferences.interval))preferences.interval=1000;
if(!Telemetry.scenarios[preferences.scenario])preferences.scenario='balanced';
if(!['pt','en','es','ru'].includes(preferences.language))preferences.language='pt';
if(![256,512,2048,4096,8192].includes(preferences.vram))preferences.vram=512;
for(const key of ['monitor','nvme','installPath'])if(typeof preferences[key]!=='string')preferences[key]=defaults[key];
const i18n = {
  pt: {
    eyebrowWelcome: 'CONFIGURAÇÃO INICIAL DA PLACA',
    step1: 'Idioma', step2: 'Destino & VRAM', step3: 'Modo Beta', step4: 'Administrador (UAC)', step5: 'Instalação', step6: 'Conclusão',
    vstep1Desc: 'Interface e região',
    vstep2Desc: 'Diretório e memória',
    vstep3Desc: 'Assinatura de teste',
    vstep4Desc: 'Elevação UAC',
    vstep5Desc: 'Execução de scripts',
    vstep6Desc: 'Pós-boot e ativação',
    langTitle: 'Selecione o Idioma do Assistente',
    langSubtitle: 'Escolha o idioma preferencial para a interface, instalação e telemetria da sua BC-250.',
    btnContinue: 'Continuar', btnBack: 'Voltar',
    installPathTitle: 'Pasta de Destino da Instalação',
    installPathSubtitle: 'Os binários do driver KMD/UMD, arquivos INF e serviços WDDM serão gravados neste local.',
    btnBrowse: 'Procurar…',
    spaceRequiredLabel: 'Espaço necessário:',
    spaceAvailableLabel: 'Espaço livre em C:\\:',
    vramTitle: 'Destino da Instalação e Memória de Vídeo (VRAM)',
    vramSubtitle: 'Defina o diretório dos componentes do driver e os limites de memória da APU AMD BC-250.',
    vramRecommended: 'Recomendado',
    vramBarVram: 'VRAM Dedicada Fixa',
    vramBarRam: 'RAM Livre p/ Sistema',
    noticeHeader: 'Aviso Técnico de Alocação de Memória',
    vramWarning: 'Independente do valor selecionado o windows podera alocar mais memoria de video para o seu jogo sendo, sendo sua escolha apenas o valor minimo, que sera reduzido da memoria ram e o recomendando é 512mb',
    vramOpt256Desc: 'Leve · Foco em máxima memória RAM para a CPU',
    vramOpt512Desc: 'Equilíbrio ideal recomendado · Testado e validado',
    vramOpt2gDesc: 'Jogos casuais · Texturas médias e estabilidade',
    vramOpt4gDesc: 'Jogos pesados · Texturas altas e resoluções maiores',
    vramOpt8gDesc: 'Cargas intensas · 50% de VRAM dedicada (8 GB RAM restantes)',
    betaTitle: 'Assinatura Digital e Modo de Teste do Windows',
    betaSubtitle: 'Autorização para execução do driver em ambiente WDDM de desenvolvimento independente.',
    betaCardTitle: 'Driver Comunitário e Modo Testsigning',
    betaParagraph1: 'Por ser um projeto independente desenvolvido pela comunidade, este driver não possui o selo de certificação digital WHQL da Microsoft.',
    betaParagraph2: 'Para permitir que o subsistema gráfico do Windows 10/11 carregue o driver em modo kernel (KMD), é necessário colocar o Windows em um modo que aceite instalar e executar drivers beta (Modo de Teste / Testsigning).',
    betaItem1: 'Procedimento oficial suportado pela Microsoft para testes de engenharia de hardware.',
    betaItem2: 'Não compromete permanentemente a segurança nem altera arquivos de sistema.',
    betaItem3: 'Pode ser revertido a qualquer momento pelas configurações de boot do Windows.',
    betaConsent: 'Compreendo que este é um driver comunitário em estágio beta e autorizo a ativação do Modo de Teste no Windows.',
    stepUac: 'Administrador (UAC)',
    uacTitle: 'Privilégios de Administrador Necessários',
    uacSubtitle: 'A configuração de baixo nível do subsistema gráfico exige autorização administrativa do Windows.',
    uacCardTitle: 'Controle de Conta de Usuário (UAC)',
    uacDesc1: 'Execução de bcdedit.exe /set testsigning on no repositório BCD do sistema operacional.',
    uacDesc2: 'Gravação dos parâmetros de alocação de memória no Registro do Sistema (HKLM\\SYSTEM\\CurrentControlSet).',
    uacDesc3: 'Criação de tarefa no Agendador do Windows com os privilégios mais altos (Highest) para inicialização pós-boot.',
    uacWaiting: 'Aguardando autorização de Administrador',
    uacGranted: 'Privilégios Concedidos (Administrador: Ativo)',
    btnRequestAdmin: 'Conceder Acesso de Administrador (Solicitar UAC)',
    btnStartConfig: 'Confirmar e Iniciar Instalação',
    loadingTitle: 'Preparando o Ambiente do Sistema',
    loadingSubtitle: 'Executando procedimentos de configuração de boot, registro e tarefas agendadas...',
    step1Label: '1. Liberando instalação de drivers beta (Testsigning)',
    step2Label: '2. Configurando alocação mínima de VRAM no Registro',
    step3Label: '3. Criando tarefa pós-reinicialização (Vincular Hardware)',
    btnCancel: 'Cancelar',
    successTitle: 'Configuração do Sistema Concluída!',
    successSubtitle: 'O Windows foi preparado para carregar o driver após a próxima reinicialização.',
    postBootNoticeTitle: 'Próxima Etapa: Vincular driver ao Hardware',
    postBootNoticeText: 'A tarefa agendada foi registrada. Quando o computador ligar novamente, o aplicativo abrirá na tela "Vincular driver ao Hardware" para finalizar a ativação da BC-250.',
    btnSimulateLink: 'Vincular driver ao Hardware Agora (Simular Pós-Boot) ↗',
    btnFinishDashboard: 'Concluir e Ir para a Visão Geral →',
    donateSupportEyebrow: 'APOIO REAL · PROJETO INDEPENDENTE',
    donateTitle: 'Apoie o Desenvolvimento Independente',
    donateSubtitle: 'A AMD possui equipes inteiras para desenvolvimento de drivers. Este driver é feito por um único desenvolvedor com apoio da comunidade.',
    linkViewTitle: 'Vincular driver ao Hardware',
    linkViewSubtitle: 'Finalize a vinculação do driver WDDM ao silício AMD BC-250 detectado.',
    btnBindHardware: 'Vincular e Ativar Driver WDDM'
  },
  en: {
    eyebrowWelcome: 'INITIAL HARDWARE SETUP',
    step1: 'Language', step2: 'Destination & VRAM', step3: 'Beta Mode', step4: 'Administrator (UAC)', step5: 'Installation', step6: 'Complete',
    vstep1Desc: 'Interface & region',
    vstep2Desc: 'Directory & memory',
    vstep3Desc: 'Test-signing mode',
    vstep4Desc: 'UAC elevation',
    vstep5Desc: 'Script execution',
    vstep6Desc: 'Post-boot & activation',
    langTitle: 'Select Setup Language',
    langSubtitle: 'Choose your preferred language for the interface, setup, and telemetry of your BC-250.',
    btnContinue: 'Continue', btnBack: 'Back',
    installPathTitle: 'Installation Destination Folder',
    installPathSubtitle: 'KMD/UMD driver binaries, INF files, and WDDM services will be written to this location.',
    btnBrowse: 'Browse…',
    spaceRequiredLabel: 'Required space:',
    spaceAvailableLabel: 'Free space on C:\\:',
    vramTitle: 'Installation Destination & Video Memory (VRAM)',
    vramSubtitle: 'Configure the driver component directory and memory limits for the AMD BC-250 APU.',
    vramRecommended: 'Recommended',
    vramBarVram: 'Dedicated Fixed VRAM',
    vramBarRam: 'Free System RAM',
    noticeHeader: 'Technical Memory Allocation Notice',
    vramWarning: 'Regardless of the selected value, Windows can dynamically allocate additional video memory to your game, your choice being only the minimum value subtracted from system RAM, and 512 MB is recommended.',
    vramOpt256Desc: 'Light · Focus on maximum RAM for the CPU',
    vramOpt512Desc: 'Optimal balanced recommendation · Tested and validated',
    vramOpt2gDesc: 'Casual gaming · Medium textures and stability',
    vramOpt4gDesc: 'Heavy gaming · High textures and higher resolutions',
    vramOpt8gDesc: 'Intense workloads · 50% dedicated VRAM (8 GB system RAM remaining)',
    betaTitle: 'Digital Signature & Windows Test Mode',
    betaSubtitle: 'Authorization to load the driver in an independent WDDM development environment.',
    betaCardTitle: 'Community Driver & Testsigning Mode',
    betaParagraph1: 'As an independent open-source community project, this driver does not carry Microsoft WHQL certification.',
    betaParagraph2: 'To allow the Windows 10/11 graphics subsystem to load the kernel-mode driver (KMD), Windows must be configured to accept beta/test-signed drivers (Testsigning Mode).',
    betaItem1: 'Official Microsoft-supported procedure for engineering and hardware testing.',
    betaItem2: 'Does not permanently compromise security or modify core system files.',
    betaItem3: 'Can be reverted at any time via Windows boot settings.',
    betaConsent: 'I understand this is a beta community driver and authorize enabling Testsigning Mode in Windows.',
    stepUac: 'Administrator (UAC)',
    uacTitle: 'Administrator Privileges Required',
    uacSubtitle: 'Low-level configuration of the graphics subsystem requires Windows administrative authorization.',
    uacCardTitle: 'User Account Control (UAC)',
    uacDesc1: 'Executing bcdedit.exe /set testsigning on in the system BCD bootloader.',
    uacDesc2: 'Writing memory allocation parameters into the System Registry (HKLM\\SYSTEM\\CurrentControlSet).',
    uacDesc3: 'Creating a task in Windows Task Scheduler with highest privileges (Highest) for post-boot initialization.',
    uacWaiting: 'Waiting for Administrator authorization',
    uacGranted: 'Privileges Granted (Administrator: Active)',
    btnRequestAdmin: 'Grant Administrator Access (Request UAC)',
    btnStartConfig: 'Confirm and Begin Installation',
    loadingTitle: 'Preparing System Environment',
    loadingSubtitle: 'Applying boot configuration, registry keys, and scheduled tasks...',
    step1Label: '1. Enabling beta driver installation (Testsigning)',
    step2Label: '2. Configuring minimum VRAM allocation in Registry',
    step3Label: '3. Creating post-reboot task (Link Driver to Hardware)',
    btnCancel: 'Cancel',
    successTitle: 'System Configuration Complete!',
    successSubtitle: 'Windows is prepared to load the driver upon the next system restart.',
    postBootNoticeTitle: 'Next Step: Link Driver to Hardware',
    postBootNoticeText: 'The scheduled task has been created. When your computer reboots, the app will open directly to "Link Driver to Hardware" to complete BC-250 activation.',
    btnSimulateLink: 'Simulate Reboot / Link Hardware ↗',
    btnFinishDashboard: 'Finish and Go to Overview →',
    donateSupportEyebrow: 'REAL SUPPORT · INDEPENDENT PROJECT',
    donateTitle: 'Support Independent Development',
    donateSubtitle: 'AMD has full engineering teams for driver development. This driver is built by a solo developer supported by the community.',
    linkViewTitle: 'Link Driver to Hardware',
    linkViewSubtitle: 'Finalize binding the WDDM driver to the detected AMD BC-250 silicon.',
    btnBindHardware: 'Bind and Activate WDDM Driver'
  },
  es: {
    eyebrowWelcome: 'CONFIGURACIÓN INICIAL DE LA TARJETA',
    step1: 'Idioma', step2: 'Destino y VRAM', step3: 'Modo Beta', step4: 'Administrador (UAC)', step5: 'Instalación', step6: 'Finalizado',
    vstep1Desc: 'Interfaz y región',
    vstep2Desc: 'Carpeta y memoria',
    vstep3Desc: 'Firma de prueba',
    vstep4Desc: 'Elevación UAC',
    vstep5Desc: 'Ejecución de scripts',
    vstep6Desc: 'Post-reinicio y activación',
    langTitle: 'Seleccione el Idioma del Asistente',
    langSubtitle: 'Elija el idioma preferido para la interfaz, instalación y telemetría de su BC-250.',
    btnContinue: 'Continuar', btnBack: 'Volver',
    installPathTitle: 'Carpeta de Destino de la Instalación',
    installPathSubtitle: 'Los binarios del controlador KMD/UMD, archivos INF y servicios WDDM se instalarán aquí.',
    btnBrowse: 'Examinar…',
    spaceRequiredLabel: 'Espacio requerido:',
    spaceAvailableLabel: 'Espacio libre en C:\\:',
    vramTitle: 'Destino de la Instalación y Memoria de Video (VRAM)',
    vramSubtitle: 'Configure la carpeta de componentes y los límites de memoria para la APU AMD BC-250.',
    vramRecommended: 'Recomendado',
    vramBarVram: 'VRAM Dedicada Fija',
    vramBarRam: 'RAM Libre para Sistema',
    noticeHeader: 'Aviso Técnico de Asignación de Memoria',
    vramWarning: 'Independientemente del valor seleccionado, Windows podrá asignar más memoria de video para su juego, siendo su elección solo el valor mínimo que se reducirá de la memoria RAM, y lo recomendado son 512 MB.',
    vramOpt256Desc: 'Ligero · Máxima memoria RAM para la CPU',
    vramOpt512Desc: 'Equilibrio óptimo recomendado · Probado y validado',
    vramOpt2gDesc: 'Juegos casuales · Texturas medias y estabilidad',
    vramOpt4gDesc: 'Juegos pesados · Texturas altas y mayores resoluciones',
    vramOpt8gDesc: 'Cargas intensas · 50% de VRAM fija (8 GB de RAM restantes)',
    betaTitle: 'Firma Digital y Modo de Prueba de Windows',
    betaSubtitle: 'Autorización para cargar el controlador en entorno de desarrollo WDDM independiente.',
    betaCardTitle: 'Controlador Comunitario y Modo Testsigning',
    betaParagraph1: 'Al ser un proyecto independiente de la comunidad, este controlador no posee certificación digital WHQL de Microsoft.',
    betaParagraph2: 'Para permitir que Windows 10/11 cargue el controlador en modo kernel (KMD), es necesario habilitar el Modo de Prueba (Testsigning) que acepta controladores beta.',
    betaItem1: 'Procedimiento oficial soportado por Microsoft para pruebas de ingeniería.',
    betaItem2: 'No compromete de forma permanente la seguridad ni altera archivos base del sistema.',
    betaItem3: 'Se puede revertir en cualquier momento desde la configuración de Windows.',
    betaConsent: 'Entiendo que es un controlador beta comunitario y autorizo la activación del Modo de Prueba en Windows.',
    stepUac: 'Administrador (UAC)',
    uacTitle: 'Privilegios de Administrador Requeridos',
    uacSubtitle: 'La configuración de bajo nivel del subsistema gráfico requiere autorización administrativa de Windows.',
    uacCardTitle: 'Control de Cuentas de Usuario (UAC)',
    uacDesc1: 'Ejecución de bcdedit.exe /set testsigning on en el almacén BCD del sistema operativo.',
    uacDesc2: 'Escritura de parámetros de asignación de memoria en el Registro del Sistema (HKLM\\SYSTEM\\CurrentControlSet).',
    uacDesc3: 'Creación de tarea en el Programador de Windows con privilegios elevados (Highest).',
    uacWaiting: 'Esperando autorización de Administrador',
    uacGranted: 'Privilegios Concedidos (Administrador: Activo)',
    btnRequestAdmin: 'Conceder Acceso de Administrador (Solicitar UAC)',
    btnStartConfig: 'Confirmar e Iniciar Instalación',
    loadingTitle: 'Preparando el Entorno del Sistema',
    loadingSubtitle: 'Aplicando configuración de arranque, claves de registro y tareas programadas...',
    step1Label: '1. Habilitando instalación de controladores beta (Testsigning)',
    step2Label: '2. Configurando asignación mínima de VRAM en Registro',
    step3Label: '3. Creando tarea post-reinicio (Vincular Hardware)',
    btnCancel: 'Cancelar',
    successTitle: '¡Configuración del Sistema Completada!',
    successSubtitle: 'Windows está preparado para cargar el controlador tras el próximo reinicio.',
    postBootNoticeTitle: 'Siguiente Paso: Vincular controlador al Hardware',
    postBootNoticeText: 'La tarea programada ha sido registrada. Al reiniciar el equipo, la aplicación se abrirá en la pantalla "Vincular controlador al Hardware" para finalizar la activación.',
    btnSimulateLink: 'Simular Reinicio / Vincular Hardware ↗',
    btnFinishDashboard: 'Finalizar e Ir a la Vista General →',
    donateSupportEyebrow: 'APOYO REAL · PROJETO INDEPENDIENTE',
    donateTitle: 'Apoye el Desarrollo Independiente',
    donateSubtitle: 'AMD cuenta con departamentos enteros para controladores. Este proyecto es desarrollado por una sola persona con el apoyo de la comunidad.',
    linkViewTitle: 'Vincular controlador al Hardware',
    linkViewSubtitle: 'Finalice la vinculación del controlador WDDM al silicio AMD BC-250 detectado.',
    btnBindHardware: 'Vincular y Activar Controlador WDDM'
  },
  ru: {
    eyebrowWelcome: 'НАЧАЛЬНАЯ НАСТРОЙКА ВИДЕОКАРТЫ',
    step1: 'Язык', step2: 'Путь и VRAM', step3: 'Бета-режим', step4: 'Администратор (UAC)', step5: 'Установка', step6: 'Завершено',
    vstep1Desc: 'Интерфейс и регион',
    vstep2Desc: 'Папка и память',
    vstep3Desc: 'Тестовая подпись',
    vstep4Desc: 'Повышение прав UAC',
    vstep5Desc: 'Запуск скриптов',
    vstep6Desc: 'После перезагрузки',
    langTitle: 'Выберите язык мастера настройки',
    langSubtitle: 'Выберите предпочтительный язык для интерфейса, установки и телеметрии BC-250.',
    btnContinue: 'Продолжить', btnBack: 'Назад',
    installPathTitle: 'Папка назначения установки',
    installPathSubtitle: 'Бинарные файлы KMD/UMD, файлы INF и службы WDDM будут установлены в эту папку.',
    btnBrowse: 'Обзор…',
    spaceRequiredLabel: 'Требуемое место:',
    spaceAvailableLabel: 'Свободно на C:\\:',
    vramTitle: 'Папка установки и выделение видеопамяти (VRAM)',
    vramSubtitle: 'Настройте папку компонентов драйвера и лимиты памяти для APU AMD BC-250.',
    vramRecommended: 'Рекомендуется',
    vramBarVram: 'Выделенная VRAM',
    vramBarRam: 'Свободная RAM системы',
    noticeHeader: 'Техническое уведомление о памяти',
    vramWarning: 'Независимо от выбранного значения, Windows сможет выделить больше видеопамяти для вашей игры, ваш выбор определяет только минимальный объем, который вычитается из оперативной памяти, и рекомендуется 512 МБ.',
    vramOpt256Desc: 'Легкий · Максимум ОЗУ для процессора',
    vramOpt512Desc: 'Оптимальный баланс · Протестировано и подтверждено',
    vramOpt2gDesc: 'Обычные игры · Средние текстуры и стабильность',
    vramOpt4gDesc: 'Требовательные игры · Высокие текстуры и разрешения',
    vramOpt8gDesc: 'Тяжелые нагрузки · 50% выделенной VRAM (остается 8 ГБ ОЗУ)',
    betaTitle: 'Цифровая подпись и тестовый режим Windows',
    betaSubtitle: 'Авторизация загрузки драйвера в среде независимой разработки WDDM.',
    betaCardTitle: 'Драйвер сообщества и режим Testsigning',
    betaParagraph1: 'Будучи независимым проектом с открытым исходным кодом, этот драйвер не имеет сертификата Microsoft WHQL.',
    betaParagraph2: 'Чтобы графическая подсистема Windows 10/11 могла загрузить драйвер режима ядра (KMD), необходимо перевести Windows в режим загрузки бета-драйверов (Testsigning).',
    betaItem1: 'Официальная процедура Microsoft для инженерных испытаний оборудования.',
    betaItem2: 'Не снижает безопасность на постоянной основе и не меняет системные файлы ядра.',
    betaItem3: 'Может быть отключен в любой момент через параметры загрузки Windows.',
    betaConsent: 'Я понимаю, что это бета-драйвер сообщества, и разрешаю включение тестового режима Windows.',
    stepUac: 'Администратор (UAC)',
    uacTitle: 'Требуются права администратора',
    uacSubtitle: 'Низкоуровневая настройка графической подсистемы требует прав администратора Windows.',
    uacCardTitle: 'Контроль учетных записей (UAC)',
    uacDesc1: 'Выполнение bcdedit.exe /set testsigning on в хранилище BCD операционной системы.',
    uacDesc2: 'Запись параметров выделения памяти в системный реестр (HKLM\\SYSTEM\\CurrentControlSet).',
    uacDesc3: 'Создание задачи в Планировщике Windows с наивысшими правами (Highest) для запуска после перезагрузки.',
    uacWaiting: 'Ожидание авторизации администратора',
    uacGranted: 'Права предоставлены (Администратор: Активен)',
    btnRequestAdmin: 'Предоставить права администратора (Запросить UAC)',
    btnStartConfig: 'Подтвердить и начать установку',
    loadingTitle: 'Подготовка системной среды',
    loadingSubtitle: 'Применение параметров загрузки, реестра и планировщика задач...',
    step1Label: '1. Разрешение установки бета-драйверов (Testsigning)',
    step2Label: '2. Настройка минимального объема VRAM в реестре',
    step3Label: '3. Создание задачи после перезагрузки (Привязка к оборудованию)',
    btnCancel: 'Отмена',
    successTitle: 'Настройка системы завершена!',
    successSubtitle: 'Windows готова к загрузке драйвера после следующей перезагрузки.',
    postBootNoticeTitle: 'Следующий шаг: Привязка драйвера к оборудованию',
    postBootNoticeText: 'Задача планировщика зарегистрирована. При следующем входе в Windows приложение откроется на экране "Привязка драйвера к оборудованию" для завершения активации BC-250.',
    btnSimulateLink: 'Имитировать перезагрузку / Привязать к железу ↗',
    btnFinishDashboard: 'Завершить и перейти к обзору →',
    donateSupportEyebrow: 'ПОДДЕРЖКА ПРОЕКТА СООБЩЕСТВА',
    donateTitle: 'Поддержать независимую разработку',
    donateSubtitle: 'У AMD есть огромные команды разработчиков. Этот драйвер создается одним разработчиком при поддержке сообщества.',
    linkViewTitle: 'Привязка драйвера к оборудованию',
    linkViewSubtitle: 'Завершите связывание драйвера WDDM с обнаруженным чипом AMD BC-250.',
    btnBindHardware: 'Привязать и активировать драйвер WDDM'
  }
};
let currentOnboardingStep = 1;
let page='overview',paused=false,selectedComponent='cpu',timer=null,current=null,installTimer=null,installPercent=0,paymentMethod='card',donationReturn=null,toastTimer;
let session=new Telemetry.Session();
const titles={overview:['Visão geral','Um olhar completo sobre a sua placa.'],sensors:['Sensores','Todas as leituras da placa, em um só lugar.'],performance:['Desempenho','Explore o comportamento da sua BC-250.'],driver:['Driver','Gerencie a experiência de instalação da sua placa.'],system:['Sistema','Conheça o hardware por trás da experiência.'],settings:['Configurações','Deixe o Adrenaline do seu jeito.'],donate:['Apoiar o desenvolvimento','Um café faz a diferença para o projeto independente.']};
function save(){try{localStorage.setItem('bc250-adrenaline-v1',JSON.stringify(preferences));}catch{toast('Não foi possível salvar as preferências. Elas valem apenas nesta sessão.');}}
function toast(message){clearTimeout(toastTimer);$('#toast').textContent=message;$('#toast').hidden=false;toastTimer=setTimeout(()=>$('#toast').hidden=true,4500);}
const media=matchMedia('(prefers-color-scheme: dark)');
function applyTheme(){const theme=preferences.theme==='system'?(media.matches?'dark':'light'):preferences.theme;document.documentElement.dataset.theme=theme;window.desktop?.setTheme(theme);}
media.addEventListener('change',applyTheme);
function format(v,unit='',digits){if(!Telemetry.isValid(v))return '-';const precision=digits??(['MHz','RPM'].includes(unit)?0:unit==='V'?3:1);return new Intl.NumberFormat('pt-BR',{maximumFractionDigits:precision}).format(v)+(unit?' '+unit:'');}
function escapeHtml(value){return String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function navigate(next,focus=false){
  if(!titles[next])return;page=next;
  $$('.nav-item[data-page]').forEach(el=>{el.classList.toggle('selected',el.dataset.page===page);if(el.dataset.page===page)el.setAttribute('aria-current','page');else el.removeAttribute('aria-current');});
  const isInstalled = !!preferences.installed;
  const isDonate = page === 'donate';
  const showInstaller = !isInstalled && !isDonate;

  document.body.classList.toggle('installer-active', showInstaller);
  const installerApp = $('#installer-app');
  const dashboardApp = $('#dashboard-app');
  if(installerApp) installerApp.hidden = !showInstaller;
  if(dashboardApp) dashboardApp.hidden = showInstaller;

  $('#install-view').hidden=true;
  $('#locked-view').hidden=true;
  $('#dashboard-view').hidden=showInstaller;
  $('#page-title').textContent=titles[page][0];$('#page-subtitle').textContent=titles[page][1];
  $$('[data-view]').forEach(el=>el.hidden=el.dataset.view!==page);
  $('#driver-dot').hidden=preferences.installed;
  $('.page-tools').hidden=!preferences.installed||['settings','driver','system','donate'].includes(page);
  if(preferences.installedAt&&Number.isFinite(Date.parse(preferences.installedAt)))$('#installed-at').textContent=new Date(preferences.installedAt).toLocaleDateString('pt-BR');
  if(page==='donate')renderPayment();else DonationUI.clear();
  if(current)renderTelemetry();
  if(focus){window.scrollTo({top:0});$('#main')?.focus({preventScroll:true});}
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
 if(current.source==='hardware_real'){
  $('#sample-status').innerHTML='<span class="live-dot"></span>'+(paused?'Telemetria pausada':'Telemetria Real (Windows)');
  $('#sample-status .live-dot').style.background=paused?'#8b8b8b':'var(--green)';
  $('#last-update').textContent=(paused?'Pausado · última leitura ':'Leitura de hardware · ')+new Date(current.timestamp).toLocaleTimeString('pt-BR');
 } else {
  $('#sample-status').innerHTML='<span class="live-dot"></span>'+(paused?'Simulação pausada':current.memory.valid?'Simulação ao vivo':'Sensores indisponíveis');
  $('#sample-status .live-dot').style.background=paused?'#8b8b8b':current.memory.valid?'var(--green)':'#b67b20';
  $('#last-update').textContent=(paused?'Pausado · última amostra ':'Amostra simulada · ')+new Date(current.timestamp).toLocaleTimeString('pt-BR');
 }
 $$('[data-reading]').forEach(el=>{const field=Telemetry.fields.find(f=>f.id===el.dataset.reading);el.textContent=format(Telemetry.read(current,field.path),field.unit);});
 if(page==='overview'){
  metrics.forEach(([id,label,ic,unit])=>{const field=Telemetry.fields.find(f=>f.id===id),card=$('#metric-'+id),v=Telemetry.read(current,field.path);card.querySelector('.metric-value>span').textContent=format(v);
   const detail={gpu_temp_c:(sw.gpu_sclk_mhz!==null?format(sw.gpu_sclk_mhz,'MHz'):'-')+' · Clock gráfico',cpu_temp_c:(sw.cpu_freq_mhz!==null?format(sw.cpu_freq_mhz,'MHz'):'-')+' · Clock médio',total_power:'CPU + GPU · potência de saída',memory_used:'de 15,7 GB · memória compartilhada'};
   card.querySelector('.metric-sub').textContent=v===null?'Sem leitura física (-)':detail[id];
   const values=session.history.map(s=>Telemetry.read(s,field.path)),good=values.filter(Telemetry.isValid),min=good.length?Math.min(...good)-3:0,max=good.length?Math.max(...good)+3:100;
   card.querySelector('.sparkline path').setAttribute('d',linePath(values,250,30,min,max,3));
  });
  renderBoard();chart('#temp-chart',['software.cpu_temp_c','software.gpu_temp_c'],['#8853cf','#0078d4'],'°C',120);
  const rows=[['fan','Ventoinha',(current.cooling.fan_rpm!==null?format(current.cooling.fan_rpm,'RPM')+' · '+format(current.cooling.fan_pwm_pct,'%'):'-')],['memory','Memória · média',format(current.memory.average_c,'°C')],['temp','Memória · hotspot',format(current.memory.hotspot_c,'°C')+(current.memory.valid?' · Chip '+(current.memory.hotspot_chip+1):'')],['disk','NVMe',format(sw.nvme_temp_c,'°C')]];
  $('#cooling-summary').innerHTML=rows.map(([ic,label,v])=>'<div class="summary-row"><i>'+icon(ic)+'</i><span>'+label+'</span><strong>'+v+'</strong></div>').join('');
 }
 if(page==='sensors')renderSensors();
 if(page==='performance'){chart('#cpu-chart',['software.cpu_load'],['#0078d4'],'%',100);chart('#gpu-chart',['software.gpu_load'],['#8853cf'],'%',100);$('#cpu-load').textContent=format(sw.cpu_load,'%');$('#gpu-load').textContent=format(sw.gpu_load,'%');}
}
function renderSensors(){
 const search=$('#sensor-search').value.toLocaleLowerCase('pt-BR');
 const fields=Telemetry.fields.filter(f=>f.label.toLocaleLowerCase('pt-BR').includes(search));
 $('#sensor-rows').innerHTML=fields.map(f=>{
  const value=Telemetry.read(current,f.path),ext=session.extrema[f.id];
  const isReal=(f.id==='cpu_load'||f.id==='cpu_freq_mhz'||f.id==='memory_used')&&value!==null;
  const orig=(current.source==='hardware_real')?(isReal?'Real (Windows)':'-'):(value===null?'Indisponível':'Simulado');
  return '<tr><td>'+f.label+'</td><td>'+format(value,f.unit)+'</td><td>'+format(ext?.min,f.unit)+'</td><td>'+format(ext?.max,f.unit)+'</td><td><span class="pill '+(isReal?'green':'')+'">'+orig+'</span></td></tr>';
 }).join('');
 $('#no-sensors').hidden=fields.length>0;
}
async function tick(){
 if(paused||!preferences.installed)return;
 if(window.desktop?.getSystemTelemetry){
  try{
   const sys=await window.desktop.getSystemTelemetry();
   if(sys){
    const now=Date.now();
    current={
     source:'hardware_real',
     schema_version:1,
     timestamp:now,
     scenario:'hardware',
     hardware:{
      cpu:{valid:false,vin:null,vout:null,iout:null,pout:null,temp:null},
      gpu:{valid:false,vin:null,vout:null,iout:null,pout:null,temp:null},
      total_power:null,total_power_valid:false
     },
     software:{
      cpu_temp_c:sys.cpuTempC??null,
      gpu_temp_c:null,
      cpu_freq_mhz:sys.avgFreqMhz??null,
      gpu_sclk_mhz:null,
      gpu_ppt_w:null,
      nvme_temp_c:null,
      nct_t14_c:null,
      nct_t15_c:null,
      cpu_load:sys.cpuUsagePct??null,
      gpu_load:null,
      memory_used:sys.usedMemGb??null
     },
     cooling:{fan_rpm:null,fan_pwm_pct:null},
     memory:{
      valid:false,status:'unavailable',error:'Sem leitura física',age_ms:null,
      chips_c:Array(8).fill(null),average_c:null,hotspot_c:null,hotspot_chip:null,
      saturated:false,saturated_chips:[],raw:Array(8).fill(null)
     }
    };
    session.history.push(current);
    if(session.history.length>60)session.history.shift();
    for(const field of Telemetry.fields){
     const v=Telemetry.read(current,field.path);
     if(!Telemetry.isValid(v))continue;
     const prev=session.extrema[field.id];
     session.extrema[field.id]={min:prev?Math.min(prev.min,v):v,max:prev?Math.max(prev.max,v):v};
    }
    renderTelemetry();
    return;
   }
  }catch(err){}
 }
 current=session.next(preferences.scenario,Date.now());
 renderTelemetry();
}
function startTimer(){clearInterval(timer);timer=setInterval(tick,preferences.interval);}
function setPaused(value){paused=value;$('#pause-button').innerHTML='<i>'+icon(paused?'play':'pause')+'</i><span>'+(paused?'Continuar':'Pausar')+'</span>';$('#pause-button').title=paused?'Continuar telemetria simulada':'Pausar telemetria simulada';if(!paused)tick();else renderTelemetry();}
function applyLanguage(lang) {
  if (!i18n[lang]) lang = 'pt';
  preferences.language = lang;
  save();
  document.documentElement.lang = lang;
  const dict = i18n[lang];
  $$('[data-i18n]').forEach(el => {
    const key = el.dataset.i18n;
    if (dict[key]) el.textContent = dict[key];
  });
  $$('[data-lang]').forEach(el => {
    const active = el.dataset.lang === lang;
    el.classList.toggle('selected', active);
    el.setAttribute('aria-checked', String(active));
  });
  updateMemoryVisualizer();
}

function updateMemoryVisualizer() {
  const vram = preferences.vram || 512;
  const total = 16384;
  const pct = Math.max(1.5, (vram / total) * 100);
  const ramMb = total - vram;
  const ramGb = (ramMb / 1024).toFixed(1);
  const vramLabel = vram >= 1024 ? (vram / 1024) + ' GB' : vram + ' MB';
  
  const vramBar = $('#mem-bar-vram');
  const ramBar = $('#mem-bar-ram');
  if (vramBar) vramBar.style.width = pct + '%';
  if (ramBar) ramBar.style.width = (100 - pct) + '%';
  
  const summary = $('#mem-viz-summary');
  if (summary) summary.textContent = `VRAM: ${vramLabel} · RAM Livre: ${ramGb} GB`;
  const vramVal = $('#vram-val-legend');
  if (vramVal) vramVal.textContent = vramLabel;
  const ramVal = $('#ram-val-legend');
  if (ramVal) ramVal.textContent = `${ramGb} GB`;
  const linkVram = $('#link-vram-tag');
  if (linkVram) linkVram.textContent = `VRAM Mínima: ${vramLabel}`;
  
  $$('[data-vram]').forEach(el => {
    const active = Number(el.dataset.vram) === vram;
    el.classList.toggle('selected', active);
    el.setAttribute('aria-checked', String(active));
  });
}

function setOnboardingStep(step) {
  currentOnboardingStep = step;
  $$('.vstep').forEach(el => {
    const s = Number(el.dataset.vstep);
    el.classList.toggle('active', s === step);
    el.classList.toggle('done', s < step);
  });
  
  const cards = {
    1: '#step-lang-card',
    2: '#step-vram-card',
    3: '#step-beta-card',
    4: '#step-uac-card',
    5: '#step-loading-card',
    6: '#step-success-card'
  };
  
  Object.entries(cards).forEach(([s, selector]) => {
    const el = $(selector);
    if (el) el.hidden = Number(s) !== step;
  });
  const linkView = $('#hardware-link-view');
  if (linkView) linkView.hidden = true;
  
  if (step === 6) {
    DonationUI.render(paymentMethod);
  }
}

function requestUacElevation() {
  const btn = $('#btn-request-admin');
  const badge = $('#uac-status-badge');
  const note = $('#uac-verified-note');
  if (btn) btn.disabled = true;
  if (badge) {
    badge.className = 'pill blue';
    badge.innerHTML = '<span class="live-dot" style="background:var(--accent)"></span><span>Solicitando autorização UAC…</span>';
  }
  setTimeout(() => {
    if (btn) btn.disabled = false;
    if (badge) {
      badge.className = 'pill green';
      const dict = i18n[preferences.language] || i18n.pt;
      badge.innerHTML = `<span class="live-dot" style="background:var(--green)"></span><span>${dict.uacGranted}</span>`;
    }
    if (note) note.hidden = false;
    toast('Privilégios de Administrador concedidos com sucesso!');
  }, 400);
}

function resetInstallUI(){
  clearInterval(installTimer);
  installTimer = null;
  installPercent = 0;
  setOnboardingStep(1);
}

function startInstall(){
  if(installTimer) return;
  setOnboardingStep(5);
  const term = $('#terminal-log');
  if (term) term.textContent = 'PS C:\\Windows\\System32> Initializing BC-250 environment setup...\n';
  const progressEl = $('#install-progress');
  if (progressEl) progressEl.value = 0;
  const stepPercent = $('#script-step-percent');
  if (stepPercent) stepPercent.textContent = '0%';
  
  installPercent = 0;
  const vramVal = preferences.vram || 512;
  const vramBytes = vramVal * 1024 * 1024;
  const vramLabel = vramVal >= 1024 ? (vramVal / 1024) + ' GB' : vramVal + ' MB';
  const targetDir = preferences.installPath || 'C:\\Program Files\\AMD\\BC-250 Adrenaline';
  
  const b1 = $('#badge-script-1'), b2 = $('#badge-script-2'), b3 = $('#badge-script-3');
  if(b1) b1.className = 'script-badge active';
  if(b2) b2.className = 'script-badge';
  if(b3) b3.className = 'script-badge';

  const log = (msg) => {
    if(term) {
      term.textContent += msg + '\n';
      term.scrollTop = term.scrollHeight;
    }
  };

  const advance = () => {
    installPercent += 2;
    if(progressEl) progressEl.value = installPercent;
    if(stepPercent) stepPercent.textContent = installPercent + '%';

    if(installPercent === 10) {
      log('> bcdedit.exe /set testsigning on');
    }
    if(installPercent === 26) {
      log('Operação concluída com êxito. Modo Testsigning ativado no bootloader.');
      if(b1) b1.className = 'script-badge done';
      if(b2) b2.className = 'script-badge active';
      const dict = i18n[preferences.language] || i18n.pt;
      if($('#script-step-label')) $('#script-step-label').textContent = dict.step2Label;
    }
    if(installPercent === 42) {
      log(`> reg.exe add "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E968-E325-11CE-BFC1-08002BE10318}\\0001" /v "DedicatedVideoMemoryAllocation" /t REG_DWORD /d ${vramBytes} /f`);
    }
    if(installPercent === 62) {
      log(`Operação concluída com êxito. VRAM Mínima configurada para ${vramLabel}.`);
      if(b2) b2.className = 'script-badge done';
      if(b3) b3.className = 'script-badge active';
      const dict = i18n[preferences.language] || i18n.pt;
      if($('#script-step-label')) $('#script-step-label').textContent = dict.step3Label;
    }
    if(installPercent === 76) {
      log(`> schtasks.exe /create /tn "BC250_HardwareLink" /tr "\\"${targetDir}\\BC-250-Adrenaline.exe\\" --link-hardware" /sc ONLOGON /rl HIGHEST /f`);
    }
    if(installPercent === 94) {
      log('ÊXITO: A tarefa agendada "BC250_HardwareLink" foi criada com êxito.');
      if(b3) b3.className = 'script-badge done';
    }

    if(installPercent >= 100) {
      clearInterval(installTimer);
      installTimer = null;
      log('[CONCLUÍDO] Políticas de ambiente aplicadas com sucesso.');
      setOnboardingStep(6);
    }
  };

  installTimer = setInterval(advance, 65);
}

function simulateHardwareLink() {
  const cards = ['#step-lang-card', '#step-vram-card', '#step-beta-card', '#step-uac-card', '#step-loading-card', '#step-success-card'];
  cards.forEach(c => { const el = $(c); if(el) el.hidden = true; });
  const linkView = $('#hardware-link-view');
  if(linkView) linkView.hidden = false;
  const bindingBox = $('#binding-progress-box');
  if(bindingBox) bindingBox.hidden = true;
  const btnBind = $('#btn-bind-hardware');
  if(btnBind) btnBind.disabled = false;
}

function bindHardwareDriver() {
  const bindingBox = $('#binding-progress-box');
  if(bindingBox) bindingBox.hidden = false;
  const btnBind = $('#btn-bind-hardware');
  if(btnBind) btnBind.disabled = true;
  const pBar = $('#binding-progress-bar');
  const pLabel = $('#binding-step-label');
  const pPct = $('#binding-percent');

  let pct = 0;
  const msgs = [
    'Carregando serviço bc250mcdm em modo Testsigning...',
    'Inicializando adaptador PCI\\VEN_1002&DEV_13FE...',
    'Vinculando scanout DisplayPort e 24 Compute Units...',
    'Driver WDDM ativo!'
  ];

  const timer = setInterval(() => {
    pct += 10;
    if(pBar) pBar.value = pct;
    if(pPct) pPct.textContent = pct + '%';
    const msgIdx = Math.min(msgs.length - 1, Math.floor(pct / 30));
    if(pLabel) pLabel.textContent = msgs[msgIdx];

    if(pct >= 100) {
      clearInterval(timer);
      preferences.installed = true;
      preferences.installedAt = new Date().toISOString();
      save();
      tick();
      toast('Driver WDDM vinculado com sucesso!');
      navigate('overview', true);
    }
  }, 100);
}

function openDonation(){
 donationReturn=page!=='donate'?page:'overview';
 navigate('donate',true);
}
function closeDonation(){
 DonationUI.clear();
 if(preferences.installed){navigate('overview',true);}else{navigate(donationReturn||'overview',true);}
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
 const onbMethod=e.target.closest('[data-onboarding-method]');if(onbMethod){paymentMethod=onbMethod.dataset.onboardingMethod;$$('#step-success-card [data-onboarding-method]').forEach(b=>b.setAttribute('aria-selected',String(b===onbMethod)));renderPayment();return;}
 const langCard=e.target.closest('[data-lang]');if(langCard){applyLanguage(langCard.dataset.lang);return;}
 const vramCard=e.target.closest('[data-vram]');if(vramCard){preferences.vram=Number(vramCard.dataset.vram);save();updateMemoryVisualizer();return;}
 const action=e.target.closest('[data-action]')?.dataset.action;
 if(action==='donate')openDonation();
 if(action==='close-donation')closeDonation();
 if(action==='export')exportSample();
 if(action==='reset')$('#reset-dialog').showModal();
 if(action==='cancel-reset')$('#reset-dialog').close();
 if(action==='confirm-reset')resetDemo();
 if(action==='go-install'){navigate('driver',true);setOnboardingStep(1);}
 if(action==='skip-install'){preferences.installed=true;preferences.installedAt=new Date().toISOString();save();tick();navigate('overview',true);}
 if(action==='check-update'){$('#update-result').textContent='Demonstração: você está na versão mais recente do cenário (0.1-demo). Nenhum servidor foi consultado.';}
});
$('#install-button')?.addEventListener('click',()=>{
  if(currentOnboardingStep===1) setOnboardingStep(2);
  else if(currentOnboardingStep===2) setOnboardingStep(3);
  else startInstall();
});
$('#btn-step2-back')?.addEventListener('click',()=>setOnboardingStep(1));
$('#btn-step2-next')?.addEventListener('click',()=>setOnboardingStep(3));
$('#btn-step3-back')?.addEventListener('click',()=>setOnboardingStep(2));
$('#btn-step3-next')?.addEventListener('click',()=>setOnboardingStep(4));
$('#btn-step4-back')?.addEventListener('click',()=>setOnboardingStep(3));
$('#btn-request-admin')?.addEventListener('click',requestUacElevation);
$('#btn-start-install')?.addEventListener('click',startInstall);
$('#btn-start-install-step4')?.addEventListener('click',startInstall);
$('#cancel-install')?.addEventListener('click',()=>{clearInterval(installTimer);installTimer=null;setOnboardingStep(3);toast('Configuração cancelada.');});
$('#btn-simulate-link')?.addEventListener('click',simulateHardwareLink);
$('#btn-back-from-link')?.addEventListener('click',()=>setOnboardingStep(6));
$('#btn-bind-hardware')?.addEventListener('click',bindHardwareDriver);
$('#finish-install')?.addEventListener('click',()=>{preferences.installed=true;preferences.installedAt=new Date().toISOString();save();tick();navigate('overview',true);});
$('#btn-browse-path')?.addEventListener('click',()=>{
  const currentPath=$('#install-path-input').value;
  const paths=['C:\\Program Files\\AMD\\BC-250 Adrenaline','D:\\Games\\AMD\\BC-250 Adrenaline','C:\\AMD\\BC-250'];
  let nextIdx=(paths.indexOf(currentPath)+1)%paths.length;
  $('#install-path-input').value=paths[nextIdx];
  preferences.installPath=paths[nextIdx];
  save();
  toast('Diretório definido: '+paths[nextIdx]);
});
$('#install-path-input')?.addEventListener('input',e=>{preferences.installPath=e.target.value;save();});
$('#pause-button')?.addEventListener('click',()=>setPaused(!paused));
$('#sensor-search').addEventListener('input',()=>{if(current)renderSensors();});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&page==='donate')closeDonation();});
$('.payment-tabs').addEventListener('keydown',e=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(e.key))return;e.preventDefault();const methods=['card','crypto','pix'];let index=methods.indexOf(paymentMethod);index=e.key==='Home'?0:e.key==='End'?2:(index+(e.key==='ArrowRight'?1:2))%3;paymentMethod=methods[index];renderPayment();$('[data-method="'+paymentMethod+'"]').focus();});
$('#theme-select').value=preferences.theme;$('#theme-select').addEventListener('change',e=>{preferences.theme=e.target.value;save();applyTheme();});
$('#interval-select').value=preferences.interval;$('#interval-select').addEventListener('change',e=>{preferences.interval=Number(e.target.value);save();startTimer();renderBoard();});
$('#scenario-select').addEventListener('change',e=>setScenario(e.target.value));
for(const key of ['monitor','nvme']){const input=$('#'+key+'-input');input.value=preferences[key];input.addEventListener('input',()=>{preferences[key]=input.value;save();updateDeviceNames();});}
$$('.nav-item').forEach(el=>{el.title=el.textContent.trim();el.setAttribute('aria-label',el.textContent.trim());});
async function initDriverStatus(){
  if(window.desktop?.checkDriverInstalled){
    try {
      const res = await window.desktop.checkDriverInstalled();
      if(res && res.installed){
        preferences.installed = true;
        if(!preferences.installedAt) preferences.installedAt = new Date().toISOString();
        save();
        const statePill = $('.device-state .pill');
        if(statePill){
          statePill.textContent = 'Driver ativo · ' + (res.service || 'WDDM');
          statePill.className = 'pill green';
        }
        navigate('overview');
      }
    } catch(err) {
      console.warn('Erro ao verificar driver:', err);
    }
  }
}

async function initCpuOverclockUI(){
  const slider = $('#cpu-freq-slider');
  const display = $('#cpu-slider-display');
  const badge = $('#cpu-oc-badge');
  const targetVal = $('#perf-cpu-target');
  const feedback = $('#cpu-oc-feedback');
  const btnApply = $('#btn-apply-cpu-oc');
  const btnReset = $('#btn-reset-cpu-oc');

  if(!slider) return;

  const updateDisplay = (freq) => {
    const ghz = (freq / 1000).toFixed(1);
    display.textContent = `${format(freq, 'MHz')} (${ghz} GHz)`;
  };

  slider.addEventListener('input', () => {
    updateDisplay(Number(slider.value));
  });

  if(window.desktop?.getCpuOverclock){
    try {
      const res = await window.desktop.getCpuOverclock();
      if(res && res.FrequencyMhz){
        const f = Math.max(3000, Math.min(4200, res.FrequencyMhz));
        slider.value = f;
        updateDisplay(f);
        if(badge) badge.textContent = `${format(f, 'MHz')} ativo`;
        if(targetVal) targetVal.textContent = format(f, 'MHz');
      }
    } catch(err){}
  }

  if(btnApply){
    btnApply.addEventListener('click', async () => {
      const freq = Number(slider.value);
      btnApply.disabled = true;
      if(feedback) feedback.textContent = 'Enviando comando SMU Q3 0x8F (' + format(freq, 'MHz') + ') para o silício…';

      const resBox = $('#cpu-oc-response-box');
      const resBadge = $('#cpu-res-badge');
      const resStatus = $('#cpu-res-status');
      const resDiag = $('#cpu-res-diagnostic');
      const resRaw = $('#cpu-res-raw');
      const resRawBox = $('#cpu-res-raw-container');

      if (resBox) resBox.hidden = false;
      if (resBadge) { resBadge.textContent = 'Processando…'; resBadge.className = 'pill blue'; }
      if (resStatus) resStatus.textContent = 'Aguardando retorno do hardware…';

      try {
        let res = null;
        if(window.desktop?.applyCpuOverclock){
          res = await window.desktop.applyCpuOverclock(freq);
        }
        if(badge) badge.textContent = `${format(freq, 'MHz')} configurado`;
        if(targetVal) targetVal.textContent = format(freq, 'MHz');
        if (res) {
          if (res.HardwareSuccess) {
            if (resBadge) { resBadge.textContent = 'Silício OK (0x01)'; resBadge.className = 'pill green'; }
            if (resStatus) resStatus.textContent = res.CpuResponse || 'OK: Comando de boost aceito pela CPU';
            if (resDiag) resDiag.textContent = `A CPU Zen 2 BC-250 confirmou a recepção da mensagem SMU 0x8F e definiu a frequência de boost para ${format(freq, 'MHz')}.`;
            if (feedback) feedback.textContent = `Frequência de ${format(freq, 'MHz')} aplicada no silício com sucesso!`;
            toast(`Overclock aplicado no silício: ${format(freq, 'MHz')}`);
          } else {
            if (resBadge) { resBadge.textContent = 'KMD Offline'; resBadge.className = 'pill amber'; }
            if (resStatus) resStatus.textContent = res.CpuResponse || 'KMD Offline (GLE 2)';
            if (resDiag) resDiag.textContent = `O comando não pôde ser gravado diretamente nos registradores SMN/SMU da APU porque o driver KMD está offline (Adaptador em BasicDisplay). A frequência de ${format(freq, 'MHz')} foi salva no registro do Windows para aplicação no próximo bind do driver.`;
            if (feedback) feedback.textContent = res.CpuResponse || 'KMD offline. Registro configurado.';
            toast(`Frequência de ${format(freq, 'MHz')} salva no registro.`);
          }
          if (res.RawOutput && resRaw && resRawBox) {
            resRaw.textContent = res.RawOutput;
            resRawBox.hidden = false;
          }
        }
      } catch(err) {
        if (resBadge) { resBadge.textContent = 'Erro'; resBadge.className = 'pill red'; }
        if (resStatus) resStatus.textContent = err.message;
        if (feedback) feedback.textContent = `Erro ao aplicar overclock: ${err.message}`;
        toast('Erro ao aplicar overclock');
      } finally {
        btnApply.disabled = false;
      }
    });
  }

  if(btnReset){
    btnReset.addEventListener('click', async () => {
      slider.value = 3500;
      updateDisplay(3500);
      btnReset.disabled = true;
      try {
        if(window.desktop?.applyCpuOverclock){
          await window.desktop.applyCpuOverclock(3500);
        }
        if(badge) badge.textContent = '3.500 MHz ativo (Padrão)';
        if(targetVal) targetVal.textContent = '3.500 MHz';
        if(feedback) feedback.textContent = 'Frequência padrão restaurada (3.5 GHz).';
        toast('Frequência da CPU restaurada para 3.5 GHz');
      } catch(err){
        if(feedback) feedback.textContent = 'Erro ao restaurar frequência padrão.';
      } finally {
        btnReset.disabled = false;
      }
    });
  }
}

async function initSystemTelemetry(){
  if(window.desktop?.getSystemTelemetry){
    try {
      const info = await window.desktop.getSystemTelemetry();
      if(info){
        const cores = info.cpuCores || 6;
        const threads = info.cpuThreads || 12;
        const model = info.cpuModel || 'AMD BC-250';
        const txt = `${cores} núcleos · ${threads} threads · ${model}`;
        if($('#cpu-topology-info')) $('#cpu-topology-info').textContent = txt;
        if($('#system-cpu-spec')) $('#system-cpu-spec').textContent = txt;
      }
    } catch(err){}
  }
}

applyTheme();paintIcons();boardSetup();setupMetrics();initCpuOverclockUI();initSystemTelemetry();initDriverStatus();applyLanguage(preferences.language);updateMemoryVisualizer();setOnboardingStep(1);navigate('overview');tick();startTimer();
window.addEventListener('beforeunload',()=>{clearInterval(timer);clearInterval(installTimer);});


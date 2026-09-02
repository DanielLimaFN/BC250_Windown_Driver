/**
 * AMD BC-250 Adrenalin — Internationalization (i18n) Dictionary
 * Supported Languages: English (en - Default), Russian (ru), Spanish (es), Portuguese (pt-BR)
 */

const I18N_DICTIONARY = {
  en: {
    // Header & Meta
    appTitle: "BC250 WinDV",
    editionBadge: "BC-250 Edition",
    driverVersion: "v2.7.4-WDDM",
    chipSubtext: "Cyan Skillfish 2 • GC 10.1.3 • 24 Active CUs • 16 GB GDDR6",
    mcdmActive: "MCDM Active (WDDM 2.7)",
    mcdmStandby: "Driver Standby",
    supportProject: "Support Project",
    refreshSensors: "Refresh Sensors",
    minimizeToTray: "Minimize to Tray",

    // Sidebar
    navTitleMain: "Main Navigation",
    navTitleSystem: "System & Support",
    navOverview: "Overview",
    navTuning: "Performance & Tuning",
    navMetrics: "Real-Time Metrics",
    navMemory: "GCVM & VRAM Memory",
    navQueues: "Compute Queues",
    navDiagnostics: "Diagnostics & Driver",
    navDonate: "Support Development",
    chipBadgeMicrocode: "Microcode:",
    chipBadgeCpuCores: "Zen 2 Cores:",
    chipBadgeComputeCus: "Compute CUs:",
    chipBadgeVramTotal: "Total VRAM:",
    chipBadgeActive: "Active",

    // Overview Tab
    overviewBannerTitle: "AMD BC-250 Adrenalin Control Center",
    overviewBannerDesc: "Performance management, thermal telemetry, and asynchronous compute queues for custom console silicon.",
    overviewCurrentProfile: "Current Profile",
    overviewAdjustProfile: "Adjust Profile",
    statGpuClock: "GPU Clock",
    statGpuVoltage: "GFX Voltage:",
    statHotspotTemp: "Hotspot Temperature",
    statCpuTemp: "CPU Core TSI:",
    statVramAllocated: "Allocated GDDR6 VRAM",
    statGpuLoad: "GPU Load (GRBM)",
    statShaderStatus: "Shader Status:",
    statShaderReady: "IA/WD Ready",
    statShaderComputing: "IA/WD Computing",
    chartQuickTitle: "Real-Time Frequency & Activity",
    chartQuickSub: "Continuous monitoring of GC 10.1.3 GPU",
    chartSampleRate: "Rate: 1 Hz",
    enginesStatusTitle: "Engines & Validation Tripod",
    sdmaEngineTitle: "SDMA DMA Engine",
    sdmaEngineProof: "95% Proven",
    sdmaEngineDesc: "Physical fences in VRAM verified",
    computeQueuesTitle: "Compute / MEC Queues",
    computeQueuesProof: "100% Proven",
    computeQueuesDesc: "16/16 Persistent P2 submissions",
    gcvmTitle: "GCVM & GPUVA Translation",
    gcvmProof: "90% Proven",
    gcvmDesc: "PTE 0x10 active; safe isolation",
    miniportTitle: "Miniport WDDM / MCDM",
    miniportProof: "75% Production",
    miniportDesc: "Transition P2 -> P3 (Vulkan / UMD)",

    // Tuning Tab
    tuningBannerTitle: "Compute Profiles & Thermal Control",
    tuningBannerDesc: "Select the silicon operating profile and customize PWM fan curves.",
    profileEcoTitle: "Silent / Eco",
    profileEcoDesc: "Reduced clocks, optimized voltage for minimal heat and quiet operation.",
    profileBalancedTitle: "Balanced",
    profileBalancedDesc: "Optimal balance between sustained compute capability and acoustic comfort.",
    profileTurboTitle: "Turbo Compute",
    profileTurboDesc: "Default production profile for heavy AI workloads, LLM inference, and Vulkan compute.",
    profileExtremeTitle: "Extreme Compute",
    profileExtremeDesc: "Maximum sustained frequency with 100% fan curve for intense benchmarks.",
    fanControlTitle: "Manual Fan Control (PWM)",
    fanControlDesc: "Direct hardware control via SMN Mailbox 0x3B10068.",
    fanSilence: "20% (Silent)",
    fanTurbo: "100% (Turbo Max)",
    fanApplyBtn: "Apply PWM to Silicon",
    gfxoffTitle: "GFXOFF & Silicon Power Management",
    gfxoffDesc: "GFXOFF places the GPU into deep sleep. You can force active wakeup before dispatching heavy compute queues.",
    gfxoffCurrentState: "Current GFXOFF State:",
    gfxoffAwake: "GPU Awake (Active)",
    gfxoffSleep: "GPU Asleep (GFXOFF)",
    gfxoffWakeupBtn: "Trigger Wakeup",

    // Metrics Tab
    metricsBannerTitle: "Silicon Telemetry & Metrics (GC 10.1.3)",
    metricsBannerDesc: "High-precision real-time metrics based on BC-250 Telemetry Schema 127.",
    metricsSchemaBadge: "127 P1 FIELDS",
    metricsClockTitle: "GPU Frequency & VDD Voltage",
    metricsClockSub: "Direct SMU Q0 read (0x37 / 0x38)",
    metricsTempTitle: "Silicon Thermal Map (°C)",
    metricsTempSub: "Zen 2 TSI Diode (0x59800), Junction & GDDR6",
    metricsLoadTitle: "Shader & Engine Load (%)",
    metricsLoadSub: "Real-time sampling of IA_BUSY and WD_BUSY blocks",
    metricsFanTitle: "Fan Speed (RPM)",
    metricsFanSub: "Tachometer telemetry SMN 0x3B10064",

    // Memory Tab
    memoryBannerTitle: "Unified GDDR6 Memory Management & GCVM",
    memoryBannerDesc: "16 GB unified console memory structure with 256MB/16GB ReBAR and GPUVA translation.",
    memoryPoolTitle: "Memory Pool Allocation (16,384 MB)",
    memoryTotalInUse: "Total Allocation in Use",
    memoryDriverSegment: "Driver/Kernel",
    memoryQueueSegment: "Queues & Buffers",
    memoryGcvmSegment: "GCVM Page Tables",
    bar0Title: "BAR0 (VRAM Aperture)",
    bar1Title: "BAR1 (Doorbell Aperture)",
    bar5Title: "BAR5 (MMIO Registers)",
    rebarActive: "ReBAR Active",
    mecDoorbells: "MEC Doorbells",
    soc15Regs: "SOC15 Registers",
    gcvmStatusTitle: "GCVM Status (Translation)",
    gcvmPteDefault: "Default PTE:",
    gcvmAllocVmid: "Allocated VMIDs:",
    gcvmPageSize: "Page Size:",
    gcvmIsolation: "Isolation:",
    gcvmIsoActive: "Active & Secure",

    // Queues Tab
    queuesBannerTitle: "Asynchronous Compute Queues (MEC / SDMA)",
    queuesBannerDesc: "Real-time monitoring of physical ring buffers, RPTR/WPTR pointers, and hardware fences.",
    sdmaRingTitle: "SDMA 0 (DMA Transfer Engine)",
    sdmaTripodValid: "Tripod Validated",
    sdmaWptrLabel: "Ring WPTR (CPU Write):",
    sdmaRptrLabel: "Ring RPTR (GPU Read):",
    sdmaLastFenceLabel: "Last Physical Fence:",
    sdmaBandwidthLabel: "Transfer Bandwidth:",
    ringAligned: "Aligned",
    mecRingTitle: "MEC Pipe 0 Queue 0 (Compute Ring)",
    mecP2Valid: "P2 Persistent (16/16)",
    mecHqdStatus: "HQD Status:",
    mecDispatches: "Completed Dispatches:",
    dispatchesNoPoison: "100% Without Poison",

    // Diagnostics Tab
    diagBannerTitle: "Diagnostics & Recovery Tools",
    diagBannerDesc: "Manage the kernel service and perform silicon self-tests.",
    kernelServiceTitle: "Kernel Service",
    kernelServiceDesc: "Controls the driver service registered in Windows SCM.",
    serviceRestartBtn: "Restart",
    serviceStopBtn: "Stop",
    selfTestTitle: "Silicon Self-Test",
    selfTestDesc: "Triggers an SDMA copy and compute dispatch with full validation tripod verification.",
    runSelfTestBtn: "Run Silicon Self-Test",
    recoveryTitle: "Recovery Script",
    recoveryDesc: "Restores MMIO registers and clears hardware locks in case of a freeze.",
    openRecoveryBtn: "Run BC250_RECOVERY.cmd",
    liveLogsTitle: "Driver Event & Operation Logs",
    clearLogsBtn: "Clear",

    // Donate Tab & Startup Modal
    donateBadge: "Independent Open-Driver Project",
    donateMainTitle: "Support AMD BC-250 Driver Development",
    donateStory1: "Developing low-level graphics and compute drivers (WDDM/MCDM) for this custom console silicon (AMD BC-250 / GC 10.1.3) on Windows is a monumental task. While major manufacturers rely on hundreds of dedicated engineers, this driver is being built from the ground up by a single independent developer.",
    donateStory2: "Your financial support helps cover test hardware costs, diagnostic instruments, and enables full-time commitment to the next major milestones: Native Vulkan ICD, DirectML / LLM Direct Compute, and Full Graphic Acceleration.",
    
    // Modal Options (Order: Card/Ko-fi -> Crypto -> PIX)
    opt1CardTitle: "1. Ko-fi / Card & PayPal",
    opt1CardSub: "Instant international support via Card, Apple Pay, Google Pay, or PayPal (Ko-fi)",
    opt1CardBadge: "1st Recommended Option",
    opt1Coffee1: "☕ 1 Coffee ($5)",
    opt1Coffee3: "☕☕ Dev Fuel ($15)",
    opt1Coffee5: "🚀 Driver Sponsor ($50)",
    opt1SecureNote: "Secure checkout processed by Stripe / PayPal (Ko-fi)",
    opt1OpenPage: "Open Ko-fi Page (bc250windv)",
    opt1OpenWidget: "Donate with Ko-fi (Open Widget)",
    
    opt2CryptoTitle: "2. Cryptocurrencies",
    opt2CryptoSub: "Bitcoin, Ethereum, USDT & Solana",
    opt2CopyBtn: "Copy",
    opt2DirectNote: "Direct peer-to-peer transfers without middlemen",
    
    opt3PixTitle: "3. PIX (Brazil)",
    opt3PixSub: "Instant transfer in Brazilian Reais (BRL)",
    opt3RandomKey: "PIX Key (Email / Random)",
    opt3CopyBtn: "Copy PIX Key",
    opt3AnyAmount: "Any contribution empowers the project!",
    
    modalDontShow: "Don't show automatically on startup",
    modalContinue: "Continue to Adrenalin",
    
    // Toasts & Dynamic Messages
    toastCopied: "Copied to clipboard!",
    toastPixCopied: "PIX Key copied successfully!",
    toastProfileApplied: "Applied successfully!",
    toastFanApplied: "Fan curve applied to silicon!",
    toastGfxoffAwake: "GPU awakened from GFXOFF successfully!",
    toastSelfTestSuccess: "Silicon self-test 100% validated on hardware!",
    toastTrayMinimized: "Application minimized to system tray.",
    logInitSuccess: "BC250 Adrenalin dashboard initialized successfully.",
    logDeviceConnected: "Connection to \\\\.\\Bc250Compute established.",
    logSmuHandshake: "SMU Handshake: TestMessage echo=42 OK (SMU v41.22.0).",
    logChipDetected: "GC 10.1.3 detected: 24 active CUs, 16384 MB unified GDDR6."
  },

  ru: {
    // Header & Meta
    appTitle: "BC250 WinDV",
    editionBadge: "BC-250 Edition",
    driverVersion: "v2.7.4-WDDM",
    chipSubtext: "Cyan Skillfish 2 • GC 10.1.3 • 24 активных CU • 16 ГБ GDDR6",
    mcdmActive: "MCDM активен (WDDM 2.7)",
    mcdmStandby: "Драйвер в ожидании",
    supportProject: "Поддержать проект",
    refreshSensors: "Обновить датчики",
    minimizeToTray: "Свернуть в трей",

    // Sidebar
    navTitleMain: "Главное меню",
    navTitleSystem: "Система и поддержка",
    navOverview: "Обзор",
    navTuning: "Производительность и тюнинг",
    navMetrics: "Метрики реального времени",
    navMemory: "GCVM и память VRAM",
    navQueues: "Очереди вычислений",
    navDiagnostics: "Диагностика и драйвер",
    navDonate: "Поддержать разработку",
    chipBadgeMicrocode: "Микрокод:",
    chipBadgeCpuCores: "Ядра Zen 2:",
    chipBadgeComputeCus: "Блоки CU:",
    chipBadgeVramTotal: "Всего VRAM:",
    chipBadgeActive: "Активно",

    // Overview Tab
    overviewBannerTitle: "Панель управления AMD BC-250 Adrenalin",
    overviewBannerDesc: "Управление производительностью, тепловой телеметрией и асинхронными очередями консольного кремния.",
    overviewCurrentProfile: "Текущий профиль",
    overviewAdjustProfile: "Настроить",
    statGpuClock: "Частота GPU",
    statGpuVoltage: "Напряжение GFX:",
    statHotspotTemp: "Температура Hotspot",
    statCpuTemp: "Ядро CPU TSI:",
    statVramAllocated: "Выделенная память GDDR6",
    statGpuLoad: "Загрузка GPU (GRBM)",
    statShaderStatus: "Статус шейдеров:",
    statShaderReady: "IA/WD Готов",
    statShaderComputing: "IA/WD Вычисление",
    chartQuickTitle: "Частота и активность в реальном времени",
    chartQuickSub: "Непрерывный мониторинг кремния GC 10.1.3",
    chartSampleRate: "Частота: 1 Гц",
    enginesStatusTitle: "Движки и триада валидации",
    sdmaEngineTitle: "Движок SDMA DMA",
    sdmaEngineProof: "95% Доказано",
    sdmaEngineDesc: "Физические fence в VRAM подтверждены",
    computeQueuesTitle: "Очереди вычислений MEC",
    computeQueuesProof: "100% Доказано",
    computeQueuesDesc: "16/16 устойчивых сабмитов P2",
    gcvmTitle: "GCVM и трансляция GPUVA",
    gcvmProof: "90% Доказано",
    gcvmDesc: "PTE 0x10 активен; безопасная изоляция",
    miniportTitle: "Минипорт WDDM / MCDM",
    miniportProof: "75% Продакшн",
    miniportDesc: "Переход P2 -> P3 (Vulkan / UMD)",

    // Tuning Tab
    tuningBannerTitle: "Профили вычислений и термоконтроль",
    tuningBannerDesc: "Выберите режим работы чипа и настройте кривую вращения кулера PWM.",
    profileEcoTitle: "Тихий / Эко",
    profileEcoDesc: "Сниженные частоты и оптимизированное напряжение для тишины и низкого нагрева.",
    profileBalancedTitle: "Сбалансированный",
    profileBalancedDesc: "Оптимальный баланс между вычислительной мощностью и умеренным шумом.",
    profileTurboTitle: "Turbo Compute",
    profileTurboDesc: "Основной рабочий профиль для тяжелых вычислений ИИ, LLM и Vulkan.",
    profileExtremeTitle: "Extreme Compute",
    profileExtremeDesc: "Максимальная частота с работой вентилятора на 100% для бенчмарков.",
    fanControlTitle: "Ручное управление кулером (PWM)",
    fanControlDesc: "Прямой контроль через SMN Mailbox 0x3B10068.",
    fanSilence: "20% (Тихий)",
    fanTurbo: "100% (Макс)",
    fanApplyBtn: "Применить PWM",
    gfxoffTitle: "GFXOFF и управление питанием",
    gfxoffDesc: "GFXOFF переводит GPU в глубокий сон. Можно принудительно разбудить чип перед запуском вычислений.",
    gfxoffCurrentState: "Статус GFXOFF:",
    gfxoffAwake: "GPU Активен (Пробужден)",
    gfxoffSleep: "GPU в сне (GFXOFF)",
    gfxoffWakeupBtn: "Разбудить GPU",

    // Metrics Tab
    metricsBannerTitle: "Телеметрия и датчики чипа (GC 10.1.3)",
    metricsBannerDesc: "Высокоточная телеметрия реального времени на основе Schema 127.",
    metricsSchemaBadge: "127 ПОЛЕЙ P1",
    metricsClockTitle: "Частота GPU и напряжение VDD",
    metricsClockSub: "Прямое чтение SMU Q0 (0x37 / 0x38)",
    metricsTempTitle: "Тепловая карта чипа (°C)",
    metricsTempSub: "Диод Zen 2 TSI (0x59800), Junction и GDDR6",
    metricsLoadTitle: "Нагрузка шейдеров и движков (%)",
    metricsLoadSub: "Выборка блоков IA_BUSY и WD_BUSY в реальном времени",
    metricsFanTitle: "Обороты кулера (RPM)",
    metricsFanSub: "Тахометр SMN 0x3B10064",

    // Memory Tab
    memoryBannerTitle: "Управление объединенной памятью GDDR6 и GCVM",
    memoryBannerDesc: "16 ГБ единой памяти консоли с поддержкой ReBAR 256MB/16GB и GPUVA трансляцией.",
    memoryPoolTitle: "Распределение пула памяти (16 384 МБ)",
    memoryTotalInUse: "Всего выделено",
    memoryDriverSegment: "Драйвер/Ядро",
    memoryQueueSegment: "Очереди и буферы",
    memoryGcvmSegment: "Таблицы страниц GCVM",
    bar0Title: "BAR0 (Апертура VRAM)",
    bar1Title: "BAR1 (Апертура Doorbell)",
    bar5Title: "BAR5 (Регистры MMIO)",
    rebarActive: "ReBAR Активен",
    mecDoorbells: "MEC Doorbells",
    soc15Regs: "Регистры SOC15",
    gcvmStatusTitle: "Статус GCVM (Трансляция)",
    gcvmPteDefault: "Базовый PTE:",
    gcvmAllocVmid: "Выделенные VMID:",
    gcvmPageSize: "Размер страницы:",
    gcvmIsolation: "Изоляция:",
    gcvmIsoActive: "Активна и защищена",

    // Queues Tab
    queuesBannerTitle: "Асинхронные очереди вычислений (MEC / SDMA)",
    queuesBannerDesc: "Мониторинг кольцевых буферов, указателей RPTR/WPTR и аппаратных fences в реальном времени.",
    sdmaRingTitle: "SDMA 0 (Движок передачи DMA)",
    sdmaTripodValid: "Триада подтверждена",
    sdmaWptrLabel: "Ring WPTR (Запись CPU):",
    sdmaRptrLabel: "Ring RPTR (Чтение GPU):",
    sdmaLastFenceLabel: "Последний fence:",
    sdmaBandwidthLabel: "Пропускная способность:",
    ringAligned: "Синхронизирован",
    mecRingTitle: "MEC Pipe 0 Queue 0 (Кольцо Compute)",
    mecP2Valid: "P2 Устойчив (16/16)",
    mecHqdStatus: "Статус HQD:",
    mecDispatches: "Завершенные диспатчи:",
    dispatchesNoPoison: "100% Без Poison",

    // Diagnostics Tab
    diagBannerTitle: "Диагностика и инструменты восстановления",
    diagBannerDesc: "Управление службой ядра и запуск аппаратных тестов.",
    kernelServiceTitle: "Служба ядра",
    kernelServiceDesc: "Управление сервисом драйвера в диспетчере Windows SCM.",
    serviceRestartBtn: "Перезапустить",
    serviceStopBtn: "Остановить",
    selfTestTitle: "Самопроверка чипа",
    selfTestDesc: "Запуск копирования SDMA и вычислительного шейдера с проверкой триады.",
    runSelfTestBtn: "Запустить тест на кремнии",
    recoveryTitle: "Скрипт восстановления",
    recoveryDesc: "Восстанавливает регистры MMIO и сбрасывает зависания чипа.",
    openRecoveryBtn: "Открыть BC250_RECOVERY.cmd",
    liveLogsTitle: "Журнал событий и операций драйвера",
    clearLogsBtn: "Очистить",

    // Donate Tab & Startup Modal
    donateBadge: "Независимый проект открытого драйвера",
    donateMainTitle: "Поддержите разработку драйвера AMD BC-250",
    donateStory1: "Создание низкоуровневого графического и вычислительного драйвера (WDDM/MCDM) для заказного консольного чипа AMD BC-250 (GC 10.1.3) на Windows — это сложнейшая инженерная задача. В то время как крупные корпорации привлекают сотни инженеров, данный драйвер пишется с нуля одним независимым разработчиком.",
    donateStory2: "Ваша поддержка помогает компенсировать расходы на тестовые платформы, измерительные приборы и позволяет полностью сосредоточиться на ключевых этапах: нативном Vulkan ICD, DirectML / LLM Direct Compute и полной графической акселерации.",
    
    // Modal Options (Order: Card/Ko-fi -> Crypto -> PIX)
    opt1CardTitle: "1. Ko-fi / Карта и PayPal",
    opt1CardSub: "Быстрая международная поддержка через Карту или PayPal на Ko-fi",
    opt1CardBadge: "1-й рекомендуемый вариант",
    opt1Coffee1: "☕ 1 Кофе ($5)",
    opt1Coffee3: "☕☕ Поддержка ($15)",
    opt1Coffee5: "🚀 Спонсор ($50)",
    opt1SecureNote: "Безопасная оплата через Stripe / PayPal (Ko-fi)",
    opt1OpenPage: "Открыть страницу Ko-fi (bc250windv)",
    opt1OpenWidget: "Поддержать через Ko-fi (Открыть виджет)",
    
    opt2CryptoTitle: "2. Криптовалюты",
    opt2CryptoSub: "Bitcoin, Ethereum, USDT и Solana",
    opt2CopyBtn: "Копировать",
    opt2DirectNote: "Прямые P2P переводы без посредников",
    
    opt3PixTitle: "3. PIX (Бразилия)",
    opt3PixSub: "Мгновенный перевод в бразильских реалах (BRL)",
    opt3RandomKey: "Ключ PIX (Email / Случайный)",
    opt3CopyBtn: "Копировать ключ PIX",
    opt3AnyAmount: "Любой вклад ускоряет разработку!",
    
    modalDontShow: "Не показывать автоматически при запуске",
    modalContinue: "Перейти в Adrenalin",
    
    // Toasts & Dynamic Messages
    toastCopied: "Скопировано в буфер обмена!",
    toastPixCopied: "Ключ PIX успешно скопирован!",
    toastProfileApplied: "Профиль успешно применен!",
    toastFanApplied: "Кривая кулера применена к чипу!",
    toastGfxoffAwake: "GPU успешно выведен из режима GFXOFF!",
    toastSelfTestSuccess: "Тест чипа на 100% подтвержден аппаратно!",
    toastTrayMinimized: "Приложение свернуто в системный трей.",
    logInitSuccess: "Панель BC250 Adrenalin успешно запущена.",
    logDeviceConnected: "Подключение к \\\\.\\Bc250Compute установлено.",
    logSmuHandshake: "SMU Handshake: TestMessage echo=42 OK (SMU v41.22.0).",
    logChipDetected: "GC 10.1.3 обнаружен: 24 активных CU, 16384 МБ GDDR6."
  },

  es: {
    // Header & Meta
    appTitle: "BC250 WinDV",
    editionBadge: "BC-250 Edition",
    driverVersion: "v2.7.4-WDDM",
    chipSubtext: "Cyan Skillfish 2 • GC 10.1.3 • 24 CUs Activas • 16 GB GDDR6",
    mcdmActive: "MCDM Activo (WDDM 2.7)",
    mcdmStandby: "Driver en Espera",
    supportProject: "Apoyar Proyecto",
    refreshSensors: "Actualizar Sensores",
    minimizeToTray: "Minimizar a la Bandeja",

    // Sidebar
    navTitleMain: "Navegación Principal",
    navTitleSystem: "Sistema y Soporte",
    navOverview: "Visión General",
    navTuning: "Rendimiento y Ajustes",
    navMetrics: "Métricas en Tiempo Real",
    navMemory: "GCVM y Memoria VRAM",
    navQueues: "Colas de Cómputo",
    navDiagnostics: "Diagnóstico y Driver",
    navDonate: "Apoyar Desarrollo",
    chipBadgeMicrocode: "Microcódigo:",
    chipBadgeCpuCores: "Núcleos Zen 2:",
    chipBadgeComputeCus: "CUs de Cómputo:",
    chipBadgeVramTotal: "VRAM Total:",
    chipBadgeActive: "Activo",

    // Overview Tab
    overviewBannerTitle: "Panel de Control AMD BC-250 Adrenalin",
    overviewBannerDesc: "Gestión de rendimiento, telemetría térmica y colas de cómputo para silicio de consola.",
    overviewCurrentProfile: "Perfil Actual",
    overviewAdjustProfile: "Ajustar Perfil",
    statGpuClock: "Reloj de GPU",
    statGpuVoltage: "Voltaje GFX:",
    statHotspotTemp: "Temperatura Hotspot",
    statCpuTemp: "CPU Core TSI:",
    statVramAllocated: "VRAM GDDR6 Asignada",
    statGpuLoad: "Carga de GPU (GRBM)",
    statShaderStatus: "Estado de Shaders:",
    statShaderReady: "IA/WD Listo",
    statShaderComputing: "IA/WD Computando",
    chartQuickTitle: "Frecuencia y Actividad en Tiempo Real",
    chartQuickSub: "Monitoreo continuo de la GPU GC 10.1.3",
    chartSampleRate: "Tasa: 1 Hz",
    enginesStatusTitle: "Motores y Trípode de Validación",
    sdmaEngineTitle: "Motor SDMA DMA",
    sdmaEngineProof: "95% Probado",
    sdmaEngineDesc: "Fences físicas en VRAM verificadas",
    computeQueuesTitle: "Colas de Cómputo / MEC",
    computeQueuesProof: "100% Probado",
    computeQueuesDesc: "16/16 envíos persistentes P2",
    gcvmTitle: "GCVM y Traducción GPUVA",
    gcvmProof: "90% Probado",
    gcvmDesc: "PTE 0x10 activo; aislamiento seguro",
    miniportTitle: "Miniport WDDM / MCDM",
    miniportProof: "75% Producción",
    miniportDesc: "Transición P2 -> P3 (Vulkan / UMD)",

    // Tuning Tab
    tuningBannerTitle: "Perfiles de Cómputo y Control Térmico",
    tuningBannerDesc: "Seleccione el perfil de operación y personalice la curva del ventilador PWM.",
    profileEcoTitle: "Silencioso / Eco",
    profileEcoDesc: "Frecuencias reducidas y voltaje optimizado para mínimo calor y silencio.",
    profileBalancedTitle: "Equilibrado",
    profileBalancedDesc: "Excelente equilibrio entre potencia de cálculo y bajo nivel acústico.",
    profileTurboTitle: "Turbo Compute",
    profileTurboDesc: "Perfil de producción para cargas pesadas de IA, LLM y cómputo Vulkan.",
    profileExtremeTitle: "Extreme Compute",
    profileExtremeDesc: "Máxima frecuencia sostenida con ventilador al 100% para pruebas intensivas.",
    fanControlTitle: "Control Manual de Ventilador (PWM)",
    fanControlDesc: "Control directo por hardware mediante SMN Mailbox 0x3B10068.",
    fanSilence: "20% (Silencioso)",
    fanTurbo: "100% (Turbo Máx)",
    fanApplyBtn: "Aplicar PWM al Silicio",
    gfxoffTitle: "GFXOFF y Gestión de Energía",
    gfxoffDesc: "GFXOFF suspende la GPU. Puede forzar el despertar antes de lanzar colas de cómputo pesadas.",
    gfxoffCurrentState: "Estado de GFXOFF:",
    gfxoffAwake: "GPU Despierta (Activa)",
    gfxoffSleep: "GPU en Suspensión (GFXOFF)",
    gfxoffWakeupBtn: "Despertar GPU",

    // Metrics Tab
    metricsBannerTitle: "Telemetría y Métricas del Silicio (GC 10.1.3)",
    metricsBannerDesc: "Métricas de alta precisión en tiempo real basadas en Schema 127.",
    metricsSchemaBadge: "127 CAMPOS P1",
    metricsClockTitle: "Frecuencia GPU y Voltaje VDD",
    metricsClockSub: "Lectura directa SMU Q0 (0x37 / 0x38)",
    metricsTempTitle: "Mapa Térmico del Silicio (°C)",
    metricsTempSub: "Diodo Zen 2 TSI (0x59800), Junction y GDDR6",
    metricsLoadTitle: "Carga de Shaders y Motores (%)",
    metricsLoadSub: "Muestreo en tiempo real de bloques IA_BUSY y WD_BUSY",
    metricsFanTitle: "Velocidad de Ventilador (RPM)",
    metricsFanSub: "Telemetría de tacómetro SMN 0x3B10064",

    // Memory Tab
    memoryBannerTitle: "Gestión de Memoria Unificada GDDR6 y GCVM",
    memoryBannerDesc: "Estructura de 16 GB unificada con soporte ReBAR 256MB/16GB y traducción GPUVA.",
    memoryPoolTitle: "Distribución del Pool de Memoria (16,384 MB)",
    memoryTotalInUse: "Asignación Total en Uso",
    memoryDriverSegment: "Driver/Kernel",
    memoryQueueSegment: "Colas y Buffers",
    memoryGcvmSegment: "Tablas de Páginas GCVM",
    bar0Title: "BAR0 (Apertura VRAM)",
    bar1Title: "BAR1 (Apertura Doorbell)",
    bar5Title: "BAR5 (Registros MMIO)",
    rebarActive: "ReBAR Activo",
    mecDoorbells: "MEC Doorbells",
    soc15Regs: "Registros SOC15",
    gcvmStatusTitle: "Estado de GCVM (Traducción)",
    gcvmPteDefault: "PTE Predeterminado:",
    gcvmAllocVmid: "VMIDs Asignados:",
    gcvmPageSize: "Tamaño de Página:",
    gcvmIsolation: "Aislamiento:",
    gcvmIsoActive: "Activo y Seguro",

    // Queues Tab
    queuesBannerTitle: "Colas Asíncronas de Cómputo (MEC / SDMA)",
    queuesBannerDesc: "Monitoreo en tiempo real de anillos físicos, punteros RPTR/WPTR y fences.",
    sdmaRingTitle: "SDMA 0 (Motor de Transferencia DMA)",
    sdmaTripodValid: "Trípode Validado",
    sdmaWptrLabel: "Ring WPTR (Escritura CPU):",
    sdmaRptrLabel: "Ring RPTR (Lectura GPU):",
    sdmaLastFenceLabel: "Última Fence Física:",
    sdmaBandwidthLabel: "Ancho de Banda:",
    ringAligned: "Alineado",
    mecRingTitle: "MEC Pipe 0 Queue 0 (Anillo Compute)",
    mecP2Valid: "P2 Persistente (16/16)",
    mecHqdStatus: "Estado HQD:",
    mecDispatches: "Dispatches Completados:",
    dispatchesNoPoison: "100% Sin Poison",

    // Diagnostics Tab
    diagBannerTitle: "Diagnóstico y Herramientas de Recuperación",
    diagBannerDesc: "Administre el servicio de kernel y ejecute pruebas en hardware.",
    kernelServiceTitle: "Servicio de Kernel",
    kernelServiceDesc: "Controla el servicio del driver registrado en Windows SCM.",
    serviceRestartBtn: "Reiniciar",
    serviceStopBtn: "Detener",
    selfTestTitle: "Auto-Prueba en Silicio",
    selfTestDesc: "Ejecuta una copia SDMA y dispatch compute con verificación completa del trípode.",
    runSelfTestBtn: "Ejecutar Prueba en Silicio",
    recoveryTitle: "Script de Recuperación",
    recoveryDesc: "Restaura registros MMIO y desbloquea el silicio en caso de bloqueo.",
    openRecoveryBtn: "Abrir BC250_RECOVERY.cmd",
    liveLogsTitle: "Registro de Eventos y Operaciones",
    clearLogsBtn: "Limpiar",

    // Donate Tab & Startup Modal
    donateBadge: "Proyecto Independiente de Driver Abierto",
    donateMainTitle: "Apoye el Desarrollo del Driver AMD BC-250",
    donateStory1: "Desarrollar un driver gráfico y de cómputo de bajo nivel (WDDM/MCDM) para el chip personalizado de consola AMD BC-250 (GC 10.1.3) en Windows es un reto monumental. Mientras los grandes fabricantes cuentan con cientos de ingenieros dedicados, este driver está siendo construido desde cero por un único desarrollador independiente.",
    donateStory2: "Su apoyo financiero ayuda a cubrir costos de hardware de prueba, herramientas de medición y permite dedicación a tiempo completo para los próximos grandes hitos: Vulkan ICD Nativo, DirectML / Cómputo LLM y Aceleración Gráfica Completa.",
    
    // Modal Options (Order: Card/Ko-fi -> Crypto -> PIX)
    opt1CardTitle: "1. Ko-fi / Tarjeta y PayPal",
    opt1CardSub: "Apoyo internacional instantáneo mediante Tarjeta o PayPal en Ko-fi",
    opt1CardBadge: "1ª Opción Recomendada",
    opt1Coffee1: "☕ 1 Café ($5)",
    opt1Coffee3: "☕☕ Apoyo Desarrollador ($15)",
    opt1Coffee5: "🚀 Patrocinador del Driver ($50)",
    opt1SecureNote: "Procesamiento seguro mediante Stripe / PayPal (Ko-fi)",
    opt1OpenPage: "Abrir Página de Ko-fi (bc250windv)",
    opt1OpenWidget: "Donar con Ko-fi (Abrir Widget)",
    
    opt2CryptoTitle: "2. Criptomonedas",
    opt2CryptoSub: "Bitcoin, Ethereum, USDT y Solana",
    opt2CopyBtn: "Copiar",
    opt2DirectNote: "Transferencias directas P2P sin intermediarios",
    
    opt3PixTitle: "3. PIX (Brasil)",
    opt3PixSub: "Transferencia instantánea en Reales (BRL)",
    opt3RandomKey: "Clave PIX (Email / Aleatoria)",
    opt3CopyBtn: "Copiar Clave PIX",
    opt3AnyAmount: "¡Cualquier aporte fortalece el proyecto!",
    
    modalDontShow: "No mostrar automáticamente al iniciar",
    modalContinue: "Continuar a Adrenalin",
    
    // Toasts & Dynamic Messages
    toastCopied: "¡Copiado al portapapeles!",
    toastPixCopied: "¡Clave PIX copiada con éxito!",
    toastProfileApplied: "¡Perfil aplicado con éxito!",
    toastFanApplied: "¡Curva de ventilador aplicada al silicio!",
    toastGfxoffAwake: "¡GPU despertada de GFXOFF con éxito!",
    toastSelfTestSuccess: "¡Auto-prueba 100% validada en hardware!",
    toastTrayMinimized: "Aplicación minimizada a la bandeja del sistema.",
    logInitSuccess: "Panel BC250 Adrenalin inicializado con éxito.",
    logDeviceConnected: "Conexión con \\\\.\\Bc250Compute establecida.",
    logSmuHandshake: "SMU Handshake: TestMessage echo=42 OK (SMU v41.22.0).",
    logChipDetected: "GC 10.1.3 detectado: 24 CUs activas, 16384 MB GDDR6."
  },

  pt: {
    // Header & Meta
    appTitle: "BC250 WinDV",
    editionBadge: "BC-250 Edition",
    driverVersion: "v2.7.4-WDDM",
    chipSubtext: "Cyan Skillfish 2 • GC 10.1.3 • 24 CUs Ativas • 16 GB GDDR6",
    mcdmActive: "MCDM Ativo (WDDM 2.7)",
    mcdmStandby: "Driver em Repouso",
    supportProject: "Apoiar Projeto",
    refreshSensors: "Atualizar Sensores",
    minimizeToTray: "Minimizar para a Bandeja",

    // Sidebar
    navTitleMain: "Navegação Principal",
    navTitleSystem: "Sistema & Suporte",
    navOverview: "Visão Geral",
    navTuning: "Desempenho & Tuning",
    navMetrics: "Métricas em Tempo Real",
    navMemory: "GCVM & Memória VRAM",
    navQueues: "Filas de Computação",
    navDiagnostics: "Diagnóstico & Driver",
    navDonate: "Apoiar Desenvolvimento",
    chipBadgeMicrocode: "Microcódigo:",
    chipBadgeCpuCores: "Zen 2 Cores:",
    chipBadgeComputeCus: "Compute CUs:",
    chipBadgeVramTotal: "VRAM Total:",
    chipBadgeActive: "Ativo",

    // Overview Tab
    overviewBannerTitle: "Painel de Controle AMD BC-250 Adrenalin",
    overviewBannerDesc: "Gestão de desempenho, telemetria térmica e filas assíncronas do silício customizado de computação.",
    overviewCurrentProfile: "Perfil Atual",
    overviewAdjustProfile: "Ajustar Perfil",
    statGpuClock: "Clock da GPU",
    statGpuVoltage: "Voltagem GFX:",
    statHotspotTemp: "Temperatura Hotspot",
    statCpuTemp: "CPU Core TSI:",
    statVramAllocated: "VRAM GDDR6 Alocada",
    statGpuLoad: "Carga da GPU (GRBM)",
    statShaderStatus: "Status dos Shaders:",
    statShaderReady: "IA/WD Pronto",
    statShaderComputing: "IA/WD Computando",
    chartQuickTitle: "Frequência e Atividade em Tempo Real",
    chartQuickSub: "Acompanhamento contínuo da GPU GC 10.1.3",
    chartSampleRate: "Taxa: 1 Hz",
    enginesStatusTitle: "Engines & Tripé de Validação",
    sdmaEngineTitle: "SDMA DMA Engine",
    sdmaEngineProof: "95% Provado",
    sdmaEngineDesc: "Fences físicas em VRAM validadas",
    computeQueuesTitle: "Compute / MEC Queues",
    computeQueuesProof: "100% Provado",
    computeQueuesDesc: "16/16 submissões persistentes P2",
    gcvmTitle: "GCVM & Tradução GPUVA",
    gcvmProof: "90% Provado",
    gcvmDesc: "PTE 0x10 ativo; isolamento seguro",
    miniportTitle: "Miniport WDDM / MCDM",
    miniportProof: "75% Produção",
    miniportDesc: "Transição P2 -> P3 (Vulkan / UMD)",

    // Tuning Tab
    tuningBannerTitle: "Perfis de Computação & Controle Térmico",
    tuningBannerDesc: "Selecione o perfil de operação do silício e personalize o comportamento da ventoinha PWM.",
    profileEcoTitle: "Silencioso / Eco",
    profileEcoDesc: "Clocks reduzidos, voltagem otimizada para mínima emissão térmica e silêncio absoluto.",
    profileBalancedTitle: "Equilibrado",
    profileBalancedDesc: "Excelente equilíbrio entre capacidade de cálculo contínuo e curva acústica moderada.",
    profileTurboTitle: "Turbo Compute",
    profileTurboDesc: "Perfil padrão de produção para cargas pesadas de IA, LLM e renderização Vulkan.",
    profileExtremeTitle: "Extreme Compute",
    profileExtremeDesc: "Máxima frequência sustentada com ventoinha a 100% para benchmarks e inferência.",
    fanControlTitle: "Controle Manual da Ventoinha (PWM)",
    fanControlDesc: "Ajuste direto via SMN Mailbox 0x3B10068 no silício BC-250.",
    fanSilence: "20% (Silêncio)",
    fanTurbo: "100% (Turbo Max)",
    fanApplyBtn: "Aplicar PWM no Silício",
    gfxoffTitle: "GFXOFF & Gestão de Energia do Silício",
    gfxoffDesc: "O recurso GFXOFF coloca a GPU em repouso profundo. É possível forçar o Wakeup ativo antes de disparar filas de computação pesadas.",
    gfxoffCurrentState: "Estado Atual GFXOFF:",
    gfxoffAwake: "GPU Ativa (GFXOFF Despertado)",
    gfxoffSleep: "GPU em Repouso (GFXOFF)",
    gfxoffWakeupBtn: "Disparar Wakeup",

    // Metrics Tab
    metricsBannerTitle: "Telemetria & Métricas do Silício (GC 10.1.3)",
    metricsBannerDesc: "Gráficos dinâmicos de alta precisão baseados no Schema 127 de Telemetria do BC-250.",
    metricsSchemaBadge: "127 CAMPOS P1",
    metricsClockTitle: "Frequência da GPU & Voltagem VDD",
    metricsClockSub: "Leitura direta de SMU Q0 (0x37 / 0x38)",
    metricsTempTitle: "Mapa Térmico do Silício (°C)",
    metricsTempSub: "Diodo TSI Zen 2 (0x59800), Junction e GDDR6",
    metricsLoadTitle: "Carga dos Shaders & Engines GRBM (%)",
    metricsLoadSub: "Amostragem em tempo real dos blocos IA_BUSY e WD_BUSY",
    metricsFanTitle: "Velocidade da Ventoinha (RPM)",
    metricsFanSub: "Telemetria de tacômetro SMN 0x3B10064",

    // Memory Tab
    memoryBannerTitle: "Gestão de Memória Unificada GDDR6 & GCVM",
    memoryBannerDesc: "Estrutura de 16 GB unificada no console com suporte a ReBAR de 256MB/16GB e tradução GPUVA.",
    memoryPoolTitle: "Distribuição do Pool de Memória (16.384 MB)",
    memoryTotalInUse: "Alocação Total em Uso",
    memoryDriverSegment: "Driver/Kernel",
    memoryQueueSegment: "Filas & Buffers",
    memoryGcvmSegment: "GCVM Page Tables",
    bar0Title: "BAR0 (VRAM Aperture)",
    bar1Title: "BAR1 (Doorbell Aperture)",
    bar5Title: "BAR5 (MMIO Register)",
    rebarActive: "ReBAR Ativo",
    mecDoorbells: "MEC Doorbells",
    soc15Regs: "SOC15 Registers",
    gcvmStatusTitle: "Status GCVM (Tradução)",
    gcvmPteDefault: "PTE Default:",
    gcvmAllocVmid: "VMID Alocados:",
    gcvmPageSize: "Tamanho de Página:",
    gcvmIsolation: "Isolamento:",
    gcvmIsoActive: "Ativo & Seguro",

    // Queues Tab
    queuesBannerTitle: "Filas Assíncronas de Computação (MEC / SDMA)",
    queuesBannerDesc: "Monitoramento em tempo real dos anéis físicos de comando, ponteiros RPTR/WPTR e fences.",
    sdmaRingTitle: "SDMA 0 (Engine de Transferência DMA)",
    sdmaTripodValid: "Tripé Validado",
    sdmaWptrLabel: "Ring WPTR (Escrita CPU):",
    sdmaRptrLabel: "Ring RPTR (Leitura GPU):",
    sdmaLastFenceLabel: "Última Fence Física:",
    sdmaBandwidthLabel: "Taxa de Transferência:",
    ringAligned: "Alinhado",
    mecRingTitle: "MEC Pipe 0 Queue 0 (Compute Ring)",
    mecP2Valid: "P2 Persistente (16/16)",
    mecHqdStatus: "HQD Status:",
    mecDispatches: "Dispatches Concluídos:",
    dispatchesNoPoison: "100% Sem Poison",

    // Diagnostics Tab
    diagBannerTitle: "Diagnóstico & Ferramentas de Recuperação",
    diagBannerDesc: "Gerencie o serviço de kernel e execute auto-testes no silício.",
    kernelServiceTitle: "Serviço de Kernel",
    kernelServiceDesc: "Controla o driver registrado no Windows SCM.",
    serviceRestartBtn: "Reiniciar",
    serviceStopBtn: "Parar",
    selfTestTitle: "Auto-Teste de Silício",
    selfTestDesc: "Dispara uma cópia SDMA e dispatch compute com verificação completa de tripé.",
    runSelfTestBtn: "Executar Teste no Silício",
    recoveryTitle: "Script de Recuperação",
    recoveryDesc: "Restaura registradores MMIO e desassocia travas em caso de congelamento.",
    openRecoveryBtn: "Abrir BC250_RECOVERY.cmd",
    liveLogsTitle: "Logs de Operação e Eventos do Driver",
    clearLogsBtn: "Limpar",

    // Donate Tab & Startup Modal
    donateBadge: "Projeto Independente Open-Driver",
    donateMainTitle: "Apoie o Desenvolvimento do Driver AMD BC-250",
    donateStory1: "Desenvolver um driver gráfico e de computação de baixo nível (WDDM/MCDM) para o chip AMD BC-250 (GC 10.1.3 / Cyan Skillfish 2) no Windows é um desafio monumental. Enquanto as grandes fabricantes possuem centenas de engenheiros dedicados, este driver está sendo construído do zero por um único desenvolvedor independente.",
    donateStory2: "O seu apoio financeiro ajuda a cobrir custos de hardware de teste, instrumentos de medição e viabiliza a dedicação de tempo integral para as próximas grandes etapas: Vulkan ICD Nativo, DirectML / LLM Compute e Aceleração Gráfica Completa.",
    
    // Modal Options (Order: Card/Ko-fi -> Crypto -> PIX)
    opt1CardTitle: "1. Ko-fi / Cartão & PayPal",
    opt1CardSub: "Apoio internacional rápido via Cartão, Apple Pay, Google Pay ou PayPal (Ko-fi)",
    opt1CardBadge: "1ª Opção Recomendada",
    opt1Coffee1: "☕ 1 Café ($5)",
    opt1Coffee3: "☕☕ Apoio Desenvolvedor ($15)",
    opt1Coffee5: "🚀 Patrocinador do Driver ($50)",
    opt1SecureNote: "Processamento seguro por Stripe / PayPal (Ko-fi)",
    opt1OpenPage: "Abrir Página do Ko-fi (bc250windv)",
    opt1OpenWidget: "Doar com Ko-fi (Abrir Widget)",
    
    opt2CryptoTitle: "2. Criptomoedas",
    opt2CryptoSub: "Bitcoin, Ethereum, USDT & Solana",
    opt2CopyBtn: "Copiar",
    opt2DirectNote: "Transferências diretas sem intermediários",
    
    opt3PixTitle: "3. PIX (Brasil)",
    opt3PixSub: "Transferência instantânea em Reais (BRL)",
    opt3RandomKey: "Chave PIX (E-mail / Aleatória)",
    opt3CopyBtn: "Copiar Chave PIX",
    opt3AnyAmount: "Qualquer valor fortalece o projeto!",
    
    modalDontShow: "Não exibir automaticamente ao iniciar",
    modalContinue: "Continuar para o Adrenalin",
    
    // Toasts & Dynamic Messages
    toastCopied: "Copiado para a área de transferência!",
    toastPixCopied: "Chave PIX copiada com sucesso!",
    toastProfileApplied: "Perfil aplicado com sucesso!",
    toastFanApplied: "Curva de ventoinha aplicada no silício!",
    toastGfxoffAwake: "GPU despertada do GFXOFF com sucesso!",
    toastSelfTestSuccess: "Auto-teste de silício 100% validado em hardware!",
    toastTrayMinimized: "Aplicativo minimizado para a área de notificação.",
    logInitSuccess: "Painel BC250 Adrenalin inicializado com sucesso.",
    logDeviceConnected: "Conexão com o dispositivo \\\\.\\Bc250Compute estabelecida.",
    logSmuHandshake: "SMU Handshake: TestMessage echo=42 OK (SMU v41.22.0).",
    logChipDetected: "GC 10.1.3 detectado: 24 CUs ativas, 16384 MB GDDR6 unificada."
  }
};

/**
 * Detect language based on user preference or system locale
 * Default priority:
 * 1. Saved in localStorage
 * 2. System/Browser locale (pt -> pt, ru -> ru, es -> es)
 * 3. Fallback: 'en' (English default)
 */
function detectAppLanguage() {
  const saved = localStorage.getItem('bc250_adrenalin_lang');
  if (saved && I18N_DICTIONARY[saved]) {
    return saved;
  }
  
  const navLang = (navigator.language || navigator.userLanguage || 'en').toLowerCase();
  if (navLang.startsWith('pt')) return 'pt';
  if (navLang.startsWith('ru')) return 'ru';
  if (navLang.startsWith('es')) return 'es';
  
  return 'en'; // Default is always English
}

let currentLang = detectAppLanguage();

function t(key, fallback = "") {
  const dict = I18N_DICTIONARY[currentLang] || I18N_DICTIONARY.en;
  return dict[key] || I18N_DICTIONARY.en[key] || fallback || key;
}

function applyTranslations(lang) {
  if (I18N_DICTIONARY[lang]) {
    currentLang = lang;
    localStorage.setItem('bc250_adrenalin_lang', lang);
  }

  // Update HTML elements with data-i18n
  document.querySelectorAll('[data-i18n]').forEach(el => {
    const key = el.getAttribute('data-i18n');
    const translation = t(key);
    if (translation) {
      el.textContent = translation;
    }
  });

  // Update HTML elements with data-i18n-html
  document.querySelectorAll('[data-i18n-html]').forEach(el => {
    const key = el.getAttribute('data-i18n-html');
    const translation = t(key);
    if (translation) {
      el.innerHTML = translation;
    }
  });

  // Update selector dropdown if present
  const selector = document.getElementById('lang-selector');
  if (selector && selector.value !== currentLang) {
    selector.value = currentLang;
  }

  // Re-render icons
  if (window.lucide) {
    lucide.createIcons();
  }
}

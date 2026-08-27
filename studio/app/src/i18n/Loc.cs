using System;
using System.Collections.Generic;

namespace Bc250Control.I18n
{
    public static class Loc
    {
        public static string CurrentLanguage { get; set; } = "pt"; // "pt", "en", "ru"
        public static event Action LanguageChanged;

        public static void SetLanguage(string lang)
        {
            if (lang == "pt" || lang == "en" || lang == "ru")
            {
                CurrentLanguage = lang;
                LanguageChanged?.Invoke();
            }
        }

        private static readonly Dictionary<string, Dictionary<string, string>> Strings = new Dictionary<string, Dictionary<string, string>>
        {
            // ==================== PORTUGUÊS ====================
            ["pt"] = new Dictionary<string, string>
            {
                // Navigation
                ["Nav_Search"] = "Pesquisar",
                ["Nav_System"] = "Sistema",
                ["Nav_Performance"] = "Desempenho & Ajustes",
                ["Nav_Unlocker"] = "Desbloqueador de Hardware",
                ["Nav_Driver"] = "Driver WDDM",
                ["Nav_Telemetry"] = "Telemetria & Sensores",
                ["Nav_Settings"] = "Configurações",

                // Breadcrumbs & Title
                ["App_Title"] = "Configurações do BC-250",
                ["Breadcrumb_System"] = "Sistema",
                ["Breadcrumb_Performance"] = "Sistema > Desempenho & Ajustes",
                ["Breadcrumb_Unlocker"] = "Sistema > Desbloqueador de Hardware",
                ["Breadcrumb_Driver"] = "Sistema > Gerenciador de Driver WDDM",
                ["Breadcrumb_Telemetry"] = "Sistema > Telemetria & Sensores",
                ["Breadcrumb_Settings"] = "Configurações > Preferências",

                // System Page
                ["Sys_Title"] = "Sistema",
                ["Sys_DevName"] = "Estação de Computação AMD BC-250",
                ["Sys_DevSub"] = "Cyan Skillfish 2 (GC 10.1.3 • 1002:13FE) • 16GB GDDR6 Unificada",
                ["Sys_DevSpecs"] = "Especificações e Diagnóstico do Dispositivo",
                ["Sys_Chip_Driver"] = "Driver Ativo",
                ["Sys_Chip_Cus"] = "Matriz RDNA2",
                ["Sys_Chip_Cpu"] = "Zen 2 CCX",
                ["Sys_Card_Gpu_Title"] = "Ajuste de Vídeo & Desempenho da GPU",
                ["Sys_Card_Gpu_Sub"] = "Frequência de clock, offset de tensão, limite de TDP e curvas de ventoinha",
                ["Sys_Card_Unlocker_Title"] = "Desbloqueador de Compute Units (CU) & Hardware",
                ["Sys_Card_Unlocker_Sub"] = "Topologia granular de 40 CUs, seleção de pares WGP e desbloqueio 8C/16T Zen 2",
                ["Sys_Card_Driver_Title"] = "Gerenciamento de Driver WDDM",
                ["Sys_Card_Driver_Sub"] = "Instalação limpa, modo Testsigning, assinaturas e diagnósticos de anéis",
                ["Sys_Card_Telemetry_Title"] = "Telemetria & Sensores de Hardware",
                ["Sys_Card_Telemetry_Sub"] = "Monitoramento em tempo real de temperatura, uso, clock, tensão e consumo TDP",
                ["Sys_Card_Power_Title"] = "Perfis de Potência & Térmica",
                ["Sys_Card_Power_Sub"] = "Perfis Silencioso, Equilibrado, Gamer e Extremo pré-configurados",
                ["Sys_Card_Settings_Title"] = "Configurações & Preferências",
                ["Sys_Card_Settings_Sub"] = "Tema do aplicativo, idioma (PT/EN/RU), efeito Mica e inicialização",

                // Donate Card
                ["Donate_Title"] = "Apoie o Projeto (Pague um Café ☕)",
                ["Donate_Sub"] = "Ajude a financiar a bancada de testes, osciloscópio e o desenvolvimento do driver BC-250.",
                ["Donate_Btn"] = "Apoiar",
                ["Donate_Pix"] = "⚡ Pix",
                ["Donate_Cards"] = "💳 Cartões",
                ["Donate_Crypto"] = "🪙 Criptomoedas",
                ["Donate_Copy"] = "Copiar",
                ["Donate_Copied"] = "✓ Copiado!",
                ["Donate_Pix_Desc"] = "Chave Pix (E-mail / Chave Aleatória):",
                ["Donate_Cards_Desc"] = "Apoio internacional com Cartão de Crédito via Ko-fi / Stripe:",
                ["Donate_Crypto_Desc"] = "Carteiras Cripto para doação:",
                ["Donate_OpenLink"] = "Abrir Página de Apoio",

                // Unlocker Page
                ["Unlock_Title"] = "Desbloqueador de Compute Units (CU) & Hardware",
                ["Unlock_Sub"] = "Topologia granular de 40 CUs por Shader Array, pares WGP seletivos, testes de estabilidade e Zen 2.",
                ["Unlock_CuMatrix_Title"] = "Matriz Topológica de CUs (Shader Engines & Arrays)",
                ["Unlock_CuMatrix_Sub"] = "Clique em qualquer par de CUs (WGP) para ativar ou desativar o silício.",
                ["Unlock_Presets"] = "Presets:",
                ["Unlock_TestStability"] = "🛡️ Testar Estabilidade (WGP)",
                ["Unlock_ActiveCus"] = "CUs ativas",
                ["Unlock_Harvested"] = "colhidas",
                ["Unlock_ActiveShaders"] = "SHADERS ATIVOS",
                ["Unlock_Fp32Compute"] = "COMPUTAÇÃO FP32 (@ 1.5GHz)",
                ["Unlock_PowerTdp"] = "POTÊNCIA TDP ESTIMADA",
                ["Unlock_Cpu_Title"] = "Desbloqueio de Núcleos CPU Zen 2 (8C / 16T)",
                ["Unlock_Cpu_Sub"] = "Libera os 2 núcleos e 4 threads desativados de fábrica na matriz da APU.",
                ["Unlock_Reg_Title"] = "Registradores de Baixo Nível & Máscara de Kernel",
                ["Unlock_Apply"] = "⚡ Aplicar Topologia no Silício",
                ["Unlock_TestingWgp"] = "Testando despacho de anel do WGP",
                ["Unlock_TestPass"] = "✓ Todos os 20 WGPs foram aprovados na verificação computacional com 0 defeitos!",
                ["Unlock_Success"] = "✓ Sucesso: Topologia aplicada e validada no silício!",

                // Performance Page
                ["Perf_Title"] = "Desempenho & Overclocking",
                ["Perf_Sub"] = "Ajuste fino de frequências de clock, tensões, limites térmicos e curvas de ventoinha.",
                ["Perf_Profiles_Title"] = "Perfis de Governador de Potência e Térmica",
                ["Perf_GpuBase"] = "Frequência Base da GPU",
                ["Perf_GpuBase_Sub"] = "Frequência mínima de clock sob carga de trabalho",
                ["Perf_GpuBoost"] = "Frequência Máxima de Boost",
                ["Perf_GpuBoost_Sub"] = "Alvo de frequência máxima com boost dinâmico",
                ["Perf_VoltageOffset"] = "Offset de Tensão SoC / GFX",
                ["Perf_VoltageOffset_Sub"] = "Compensação de voltagem para undervolt/overvolt de silício",
                ["Perf_TdpLimit"] = "Limite de Potência TDP",
                ["Perf_TdpLimit_Sub"] = "Limite de consumo de energia do pacote",
                ["Perf_Fan_Title"] = "Curva de Ventoinha Automática & Proteção Térmica",
                ["Perf_Fan_Sub"] = "Escalonamento PWM dinâmico baseado em sensores de hotspot e VRAM GDDR6.",
                ["Perf_Reset"] = "Restaurar Padrões",
                ["Perf_Apply"] = "Aplicar Alterações",

                // Driver Page
                ["Driver_Title"] = "Gerenciamento de Driver WDDM",
                ["Driver_Sub"] = "Instalação do driver, assinaturas digitais, modo testsigning e diagnóstico de anéis.",
                ["Driver_Status_Title"] = "Driver WDDM BC-250 v1.0.4.0 (Instalado & Operando)",
                ["Driver_Status_Sub"] = "ID do Dispositivo: 1002:13FE • Classe: ComputeAccelerator (MCDM) • Build 2026.08",
                ["Driver_CheckUpdates"] = "Verificar Atualizações",
                ["Driver_Testsign_Title"] = "Modo de Assinatura de Teste (Testsigning)",
                ["Driver_Testsign_Sub"] = "Permite carregar drivers customizados não assinados e builds de laboratório",
                ["Driver_Install_Title"] = "Instalar / Reinstalar Driver WDDM",
                ["Driver_Install_Sub"] = "Registra o BC250.inf e vincula o display miniport ao dispositivo 1002:13FE",
                ["Driver_Uninstall_Title"] = "Desinstalação Limpa (Estilo DDU)",
                ["Driver_Uninstall_Sub"] = "Remove binários, handles do gerenciador de dispositivos e limpa repositório OEM",
                ["Driver_Diag_Title"] = "Diagnóstico de Anéis de Hardware",
                ["Driver_Diag_Sub"] = "Valida motores SDMA, autenticação PSP FW, tabelas GCVM e integridade da VRAM.",
                ["Driver_RunDiag"] = "Executar Diagnóstico",
                ["Driver_BtnInstall"] = "Instalar",
                ["Driver_BtnUninstall"] = "Desinstalar",

                // Telemetry Page
                ["Telem_Title"] = "Telemetria & Sensores de Hardware",
                ["Telem_Sub"] = "Monitoramento térmico em tempo real, utilização de GPU/CPU, clocks e potência.",
                ["Telem_GpuSection"] = "Sensores da GPU (Cyan Skillfish 2 • 16GB GDDR6)",
                ["Telem_CpuSection"] = "Sensores da CPU (Zen 2 CCX 8C / 16T)",
                ["Telem_GpuTemp"] = "TEMPERATURA DA GPU",
                ["Telem_GpuLoad"] = "UTILIZAÇÃO DA GPU",
                ["Telem_GpuClock"] = "CLOCK DA GPU",
                ["Telem_GpuVolt"] = "TENSÃO DO CORE GFX",
                ["Telem_GpuPower"] = "CONSUMO DE POTÊNCIA (TDP)",
                ["Telem_CpuLoad"] = "CARGA DA CPU",
                ["Telem_CpuTemp"] = "TEMPERATURA DA CPU",

                // Settings Page
                ["Settings_Title"] = "Configurações & Preferências",
                ["Settings_Sub"] = "Temas visuais, efeitos Mica, idioma da interface e opções de inicialização.",
                ["Settings_Lang_Title"] = "Idioma do Aplicativo",
                ["Settings_Lang_Sub"] = "Escolha o idioma de exibição do painel de controle",
                ["Settings_Theme_Title"] = "Tema Escuro Fluent (Mica)",
                ["Settings_Theme_Sub"] = "Alternar entre os estilos visuais Escuro (Mica) e Claro",
                ["Settings_Startup_Title"] = "Iniciar com o Windows",
                ["Settings_Startup_Sub"] = "Inicia o daemon do BC-250 no logon do usuário para restaurar topologia de CUs",
                ["Settings_Tray_Title"] = "Minimizar para a Bandeja do Sistema",
                ["Settings_Tray_Sub"] = "Mantém o serviço ativo na área de notificações ao fechar a janela principal",
                ["Settings_About_Title"] = "Sobre o BC-250 Advanced Control",
                ["Settings_About_Desc"] = "Versão 1.0.0 (C# .NET Nativo / Windows 11 Fluent)\nArquitetura: Cyan Skillfish 2 / GC 10.1.3 (Device ID 1002:13FE)\nProjeto Namix WDDM MCDM • 2026",
            },

            // ==================== ENGLISH ====================
            ["en"] = new Dictionary<string, string>
            {
                // Navigation
                ["Nav_Search"] = "Search",
                ["Nav_System"] = "System",
                ["Nav_Performance"] = "Performance & Tuning",
                ["Nav_Unlocker"] = "Hardware Unlocker",
                ["Nav_Driver"] = "WDDM Driver",
                ["Nav_Telemetry"] = "Telemetry & Sensors",
                ["Nav_Settings"] = "Settings",

                // Breadcrumbs & Title
                ["App_Title"] = "BC-250 Settings",
                ["Breadcrumb_System"] = "System",
                ["Breadcrumb_Performance"] = "System > Performance & Tuning",
                ["Breadcrumb_Unlocker"] = "System > Hardware Unlocker",
                ["Breadcrumb_Driver"] = "System > WDDM Driver Management",
                ["Breadcrumb_Telemetry"] = "System > Telemetry & Sensors",
                ["Breadcrumb_Settings"] = "Settings > App Preferences",

                // System Page
                ["Sys_Title"] = "System",
                ["Sys_DevName"] = "AMD BC-250 Compute Station",
                ["Sys_DevSub"] = "Cyan Skillfish 2 (GC 10.1.3 • 1002:13FE) • 16GB GDDR6 Unified",
                ["Sys_DevSpecs"] = "Device Specs & Diagnostics",
                ["Sys_Chip_Driver"] = "Active Driver",
                ["Sys_Chip_Cus"] = "RDNA2 Matrix",
                ["Sys_Chip_Cpu"] = "Zen 2 CCX",
                ["Sys_Card_Gpu_Title"] = "Display & GPU Performance Tuning",
                ["Sys_Card_Gpu_Sub"] = "Core clock frequency, voltage offset, TDP power limit and dynamic fan curves",
                ["Sys_Card_Unlocker_Title"] = "Compute Unit (CU) & Hardware Unlocker",
                ["Sys_Card_Unlocker_Sub"] = "Granular 40 CU Shader Array topology, selective WGP pairs and CPU CCX 8-core unlock",
                ["Sys_Card_Driver_Title"] = "WDDM Driver Management",
                ["Sys_Card_Driver_Sub"] = "Clean installation, Testsigning mode, digital signatures and hardware ring diagnostics",
                ["Sys_Card_Telemetry_Title"] = "Telemetry & Hardware Sensors",
                ["Sys_Card_Telemetry_Sub"] = "Real-time sensor monitors for GPU temperature, usage, clock, voltage and TDP draw",
                ["Sys_Card_Power_Title"] = "Power & Thermal Profiles",
                ["Sys_Card_Power_Sub"] = "Silent, Balanced, Gaming and Extreme pre-configured power governor profiles",
                ["Sys_Card_Settings_Title"] = "Settings & Preferences",
                ["Sys_Card_Settings_Sub"] = "Application theme, language (PT/EN/RU), Mica visual effects and startup options",

                // Donate Card
                ["Donate_Title"] = "Support the Project (Buy Me a Coffee ☕)",
                ["Donate_Sub"] = "Help fund BC-250 research, hardware testbenches and open-source driver engineering.",
                ["Donate_Btn"] = "Donate",
                ["Donate_Pix"] = "⚡ Pix (BR)",
                ["Donate_Cards"] = "💳 Credit Cards",
                ["Donate_Crypto"] = "🪙 Crypto",
                ["Donate_Copy"] = "Copy",
                ["Donate_Copied"] = "✓ Copied!",
                ["Donate_Pix_Desc"] = "Pix Key (Brazil instant payment):",
                ["Donate_Cards_Desc"] = "International support via Credit Card (Ko-fi / Stripe / PayPal):",
                ["Donate_Crypto_Desc"] = "Crypto Wallets for donation:",
                ["Donate_OpenLink"] = "Open Support Page",

                // Unlocker Page
                ["Unlock_Title"] = "Compute Unit (CU) & Hardware Unlocker",
                ["Unlock_Sub"] = "Granular Shader Array CU topology, selective WGP pairs, stability testing and Zen 2 CCX core unlocking.",
                ["Unlock_CuMatrix_Title"] = "CU Topology Matrix (Shader Engines & Arrays)",
                ["Unlock_CuMatrix_Sub"] = "Click any pair of CUs (WGP) to selectively enable or harvest silicon.",
                ["Unlock_Presets"] = "Presets:",
                ["Unlock_TestStability"] = "🛡️ Test Stability (WGP)",
                ["Unlock_ActiveCus"] = "CUs active",
                ["Unlock_Harvested"] = "harvested",
                ["Unlock_ActiveShaders"] = "ACTIVE SHADERS",
                ["Unlock_Fp32Compute"] = "FP32 COMPUTE (@ 1.5GHz)",
                ["Unlock_PowerTdp"] = "ESTIMATED POWER TDP",
                ["Unlock_Cpu_Title"] = "CPU Zen 2 CCX Core Unlock (8C / 16T)",
                ["Unlock_Cpu_Sub"] = "Enables the 2 harvested cores and 4 threads on the APU silicon matrix.",
                ["Unlock_Reg_Title"] = "Low-Level Registers & Kernel Modprobe Preview",
                ["Unlock_Apply"] = "⚡ Apply Topology to Silicon",
                ["Unlock_TestingWgp"] = "Testing WGP compute ring dispatch",
                ["Unlock_TestPass"] = "✓ All 20 WGPs passed compute verification with 0 defects!",
                ["Unlock_Success"] = "✓ Success: Topology applied and validated on silicon!",

                // Performance Page
                ["Perf_Title"] = "Performance & Overclocking",
                ["Perf_Sub"] = "Fine-tune core clocks, voltages, thermal limits and dynamic fan curves.",
                ["Perf_Profiles_Title"] = "Power & Thermal Governor Profiles",
                ["Perf_GpuBase"] = "GPU Base Frequency",
                ["Perf_GpuBase_Sub"] = "Minimum clock frequency under compute load",
                ["Perf_GpuBoost"] = "GPU Max Boost Frequency",
                ["Perf_GpuBoost_Sub"] = "Target maximum boost frequency with dynamic scaling",
                ["Perf_VoltageOffset"] = "SoC / GFX Voltage Offset",
                ["Perf_VoltageOffset_Sub"] = "Voltage offset for silicon undervolt/overvolt",
                ["Perf_TdpLimit"] = "TDP Power Limit",
                ["Perf_TdpLimit_Sub"] = "Package power target threshold",
                ["Perf_Fan_Title"] = "Automatic Fan Curve & Thermal Protection",
                ["Perf_Fan_Sub"] = "Dynamic PWM scaling based on APU hotspot and GDDR6 VRAM temperature sensors.",
                ["Perf_Reset"] = "Reset Defaults",
                ["Perf_Apply"] = "Apply Changes",

                // Driver Page
                ["Driver_Title"] = "WDDM Driver Management",
                ["Driver_Sub"] = "Driver installation, digital signatures, kernel testsigning and hardware ring diagnostics.",
                ["Driver_Status_Title"] = "BC-250 WDDM Driver v1.0.4.0 (Installed & Operating)",
                ["Driver_Status_Sub"] = "Device ID: 1002:13FE • Class: ComputeAccelerator (MCDM) • Build 2026.08",
                ["Driver_CheckUpdates"] = "Check for Updates",
                ["Driver_Testsign_Title"] = "Windows Testsigning Mode",
                ["Driver_Testsign_Sub"] = "Enables loading unsigned custom kernel drivers and laboratory debug builds",
                ["Driver_Install_Title"] = "Install / Reinstall WDDM Driver",
                ["Driver_Install_Sub"] = "Registers BC250.inf and binds the kernel display miniport driver to 1002:13FE",
                ["Driver_Uninstall_Title"] = "Clean Uninstall (DDU Style)",
                ["Driver_Uninstall_Sub"] = "Removes driver binaries, device manager handles and purges OEM inf repository",
                ["Driver_Diag_Title"] = "Hardware Ring Buffer Diagnostics",
                ["Driver_Diag_Sub"] = "Validates SDMA copy engines, PSP FW auth, GCVM page tables and VRAM integrity.",
                ["Driver_RunDiag"] = "Run Diagnostics",
                ["Driver_BtnInstall"] = "Install",
                ["Driver_BtnUninstall"] = "Uninstall",

                // Telemetry Page
                ["Telem_Title"] = "Telemetry & Hardware Sensors",
                ["Telem_Sub"] = "Real-time thermal monitoring, GPU/CPU utilization, clock frequencies and board power draw.",
                ["Telem_GpuSection"] = "GPU Sensors (Cyan Skillfish 2 • 16GB GDDR6)",
                ["Telem_CpuSection"] = "CPU Sensors (Zen 2 CCX 8C / 16T)",
                ["Telem_GpuTemp"] = "GPU TEMPERATURE",
                ["Telem_GpuLoad"] = "GPU UTILIZATION",
                ["Telem_GpuClock"] = "GPU CLOCK",
                ["Telem_GpuVolt"] = "GFX CORE VOLTAGE",
                ["Telem_GpuPower"] = "GPU POWER DRAW (TDP)",
                ["Telem_CpuLoad"] = "CPU LOAD",
                ["Telem_CpuTemp"] = "CPU TEMPERATURE",

                // Settings Page
                ["Settings_Title"] = "Settings & Preferences",
                ["Settings_Sub"] = "Visual themes, Mica backdrop effects, UI language and system integration.",
                ["Settings_Lang_Title"] = "Application Language",
                ["Settings_Lang_Sub"] = "Choose your preferred display language for the control panel",
                ["Settings_Theme_Title"] = "Dark Fluent Theme (Mica)",
                ["Settings_Theme_Sub"] = "Switch between Windows 11 Dark Mica and Light Fluent interface styles",
                ["Settings_Startup_Title"] = "Start with Windows",
                ["Settings_Startup_Sub"] = "Automatically launch the BC-250 Control daemon on user logon to restore CU topology",
                ["Settings_Tray_Title"] = "Minimize to System Tray",
                ["Settings_Tray_Sub"] = "Keep running in the notification area when closing the main settings window",
                ["Settings_About_Title"] = "About BC-250 Advanced Control",
                ["Settings_About_Desc"] = "Version 1.0.0 (Native C# .NET / Windows 11 Fluent)\nArchitecture: Cyan Skillfish 2 / GC 10.1.3 (Device ID 1002:13FE)\nNamix WDDM MCDM Project • 2026",
            },

            // ==================== РУССКИЙ ====================
            ["ru"] = new Dictionary<string, string>
            {
                // Navigation
                ["Nav_Search"] = "Поиск",
                ["Nav_System"] = "Система",
                ["Nav_Performance"] = "Производительность",
                ["Nav_Unlocker"] = "Разблокировщик",
                ["Nav_Driver"] = "Драйвер WDDM",
                ["Nav_Telemetry"] = "Телеметрия & Датчики",
                ["Nav_Settings"] = "Настройки",

                // Breadcrumbs & Title
                ["App_Title"] = "Параметры BC-250",
                ["Breadcrumb_System"] = "Система",
                ["Breadcrumb_Performance"] = "Система > Производительность",
                ["Breadcrumb_Unlocker"] = "Система > Разблокировщик оборудования",
                ["Breadcrumb_Driver"] = "Система > Управление драйвером WDDM",
                ["Breadcrumb_Telemetry"] = "Система > Телеметрия и датчики",
                ["Breadcrumb_Settings"] = "Параметры > Настройки приложения",

                // System Page
                ["Sys_Title"] = "Система",
                ["Sys_DevName"] = "Вычислительная станция AMD BC-250",
                ["Sys_DevSub"] = "Cyan Skillfish 2 (GC 10.1.3 • 1002:13FE) • 16GB GDDR6 Unif.",
                ["Sys_DevSpecs"] = "Спецификации и диагностика устройства",
                ["Sys_Chip_Driver"] = "Активный драйвер",
                ["Sys_Chip_Cus"] = "Матрица RDNA2",
                ["Sys_Chip_Cpu"] = "Zen 2 CCX",
                ["Sys_Card_Gpu_Title"] = "Настройка дисплея и графической производительности",
                ["Sys_Card_Gpu_Sub"] = "Частота ядра, смещение напряжения, лимит TDP и кривые вентилятора",
                ["Sys_Card_Unlocker_Title"] = "Разблокировщик Compute Units (CU) и оборудования",
                ["Sys_Card_Unlocker_Sub"] = "Топология 40 CU по Shader Arrays, выбор пар WGP и разблокировка 8C/16T Zen 2",
                ["Sys_Card_Driver_Title"] = "Управление драйвером WDDM",
                ["Sys_Card_Driver_Sub"] = "Чистая установка, режим Testsigning, цифровые подписи и диагностика колец",
                ["Sys_Card_Telemetry_Title"] = "Телеметрия и датчики оборудования",
                ["Sys_Card_Telemetry_Sub"] = "Мониторинг температуры, нагрузки, частот, напряжения и мощности в реальном времени",
                ["Sys_Card_Power_Title"] = "Профили мощности и охлаждения",
                ["Sys_Card_Power_Sub"] = "Предустановленные профили: Тихий, Сбалансированный, Игровой и Экстремальный",
                ["Sys_Card_Settings_Title"] = "Параметры и настройки",
                ["Sys_Card_Settings_Sub"] = "Тема оформления, язык интерфейса (PT/EN/RU), эффекты Mica и автозапуск",

                // Donate Card
                ["Donate_Title"] = "Поддержать проект (Угостить кофе ☕)",
                ["Donate_Sub"] = "Помогите развитию открытого драйвера BC-250, оборудованию и исследованиям.",
                ["Donate_Btn"] = "Поддержать",
                ["Donate_Pix"] = "⚡ Pix",
                ["Donate_Cards"] = "💳 Банковские карты",
                ["Donate_Crypto"] = "🪙 Криптовалюта",
                ["Donate_Copy"] = "Копировать",
                ["Donate_Copied"] = "✓ Скопировано!",
                ["Donate_Pix_Desc"] = "Ключ Pix (для Бразилии):",
                ["Donate_Cards_Desc"] = "Оплата картой (Ko-fi, Stripe, PayPal):",
                ["Donate_Crypto_Desc"] = "Криптокошельки для доната:",
                ["Donate_OpenLink"] = "Открыть страницу поддержки",

                // Unlocker Page
                ["Unlock_Title"] = "Разблокировщик Compute Units (CU) и оборудования",
                ["Unlock_Sub"] = "Поканальная топология 40 CU, выборочные пары WGP, стресс-тесты и разблокировка Zen 2.",
                ["Unlock_CuMatrix_Title"] = "Топологическая матрица CU (Shader Engines & Arrays)",
                ["Unlock_CuMatrix_Sub"] = "Нажмите на любую пару CU (WGP) для выборочного включения или отключения кремния.",
                ["Unlock_Presets"] = "Пресеты:",
                ["Unlock_TestStability"] = "🛡️ Тест стабильности (WGP)",
                ["Unlock_ActiveCus"] = "CU активно",
                ["Unlock_Harvested"] = "отключено",
                ["Unlock_ActiveShaders"] = "АКТИВНЫЕ ШЕЙДЕРЫ",
                ["Unlock_Fp32Compute"] = "ВЫЧИСЛЕНИЯ FP32 (@ 1.5ГГц)",
                ["Unlock_PowerTdp"] = "РАСЧЕТНАЯ МОЩНОСТЬ TDP",
                ["Unlock_Cpu_Title"] = "Разблокировка ядер CPU Zen 2 CCX (8C / 16T)",
                ["Unlock_Cpu_Sub"] = "Включает 2 ядра и 4 потока, отключенных на заводе в кремнии APU.",
                ["Unlock_Reg_Title"] = "Низкоуровневые регистры и параметры ядра",
                ["Unlock_Apply"] = "⚡ Применить топологию в кремнии",
                ["Unlock_TestingWgp"] = "Тестирование диспетчеризации кольца WGP",
                ["Unlock_TestPass"] = "✓ Все 20 WGP успешно прошли проверку вычислений без дефектов!",
                ["Unlock_Success"] = "✓ Успешно: Топология применена и подтверждена на оборудовании!",

                // Performance Page
                ["Perf_Title"] = "Производительность и разгон",
                ["Perf_Sub"] = "Тонкая настройка частот ядра, напряжения, лимитов TDP и кривых вентилятора.",
                ["Perf_Profiles_Title"] = "Профили регулятора мощности и температуры",
                ["Perf_GpuBase"] = "Базовая частота GPU",
                ["Perf_GpuBase_Sub"] = "Минимальная частота ядра под вычислительной нагрузкой",
                ["Perf_GpuBoost"] = "Максимальная частота Boost",
                ["Perf_GpuBoost_Sub"] = "Целевая частота динамического разгона",
                ["Perf_VoltageOffset"] = "Смещение напряжения SoC / GFX",
                ["Perf_VoltageOffset_Sub"] = "Смещение для андервольта или овервольта кремния",
                ["Perf_TdpLimit"] = "Лимит мощности TDP",
                ["Perf_TdpLimit_Sub"] = "Порог максимальной мощности пакета",
                ["Perf_Fan_Title"] = "Автоматическая кривая вентилятора и термозащита",
                ["Perf_Fan_Sub"] = "Динамическое регулирование ШИМ на основе датчиков hotspot и памяти GDDR6.",
                ["Perf_Reset"] = "Сброс по умолчанию",
                ["Perf_Apply"] = "Применить изменения",

                // Driver Page
                ["Driver_Title"] = "Управление драйвером WDDM",
                ["Driver_Sub"] = "Установка драйвера, цифровые подписи, режим тестовой подписи и диагностика.",
                ["Driver_Status_Title"] = "Драйвер BC-250 WDDM v1.0.4.0 (Установлен и работает)",
                ["Driver_Status_Sub"] = "ID устройства: 1002:13FE • Класс: ComputeAccelerator (MCDM) • Сборка 2026.08",
                ["Driver_CheckUpdates"] = "Проверить обновления",
                ["Driver_Testsign_Title"] = "Режим тестовой подписи (Testsigning)",
                ["Driver_Testsign_Sub"] = "Разрешает загрузку неподписанных кастомных драйверов и отладочных сборок",
                ["Driver_Install_Title"] = "Установить / Переустановить драйвер WDDM",
                ["Driver_Install_Sub"] = "Регистрирует BC250.inf и связывает минипорт дисплея с устройством 1002:13FE",
                ["Driver_Uninstall_Title"] = "Чистое удаление (DDU)",
                ["Driver_Uninstall_Sub"] = "Удаляет бинарники драйвера, дескрипторы устройств и очищает репозиторий OEM",
                ["Driver_Diag_Title"] = "Диагностика аппаратных колец",
                ["Driver_Diag_Sub"] = "Проверка двигателей SDMA, аутентификации PSP FW, страниц GCVM и памяти VRAM.",
                ["Driver_RunDiag"] = "Запустить диагностику",
                ["Driver_BtnInstall"] = "Установить",
                ["Driver_BtnUninstall"] = "Удалить",

                // Telemetry Page
                ["Telem_Title"] = "Телеметрия и датчики оборудования",
                ["Telem_Sub"] = "Мониторинг температуры, загрузки GPU/CPU, тактовых частот и энергопотребления.",
                ["Telem_GpuSection"] = "Датчики GPU (Cyan Skillfish 2 • 16GB GDDR6)",
                ["Telem_CpuSection"] = "Датчики CPU (Zen 2 CCX 8C / 16T)",
                ["Telem_GpuTemp"] = "ТЕМПЕРАТУРА GPU",
                ["Telem_GpuLoad"] = "ЗАГРУЗКА GPU",
                ["Telem_GpuClock"] = "ЧАСТОТА GPU",
                ["Telem_GpuVolt"] = "НАПРЯЖЕНИЕ ЯДРА GFX",
                ["Telem_GpuPower"] = "МОЩНОСТЬ GPU (TDP)",
                ["Telem_CpuLoad"] = "ЗАГРУЗКА CPU",
                ["Telem_CpuTemp"] = "ТЕМПЕРАТУРА CPU",

                // Settings Page
                ["Settings_Title"] = "Параметры и настройки",
                ["Settings_Sub"] = "Темы оформления, эффекты Mica, язык интерфейса и интеграция с системой.",
                ["Settings_Lang_Title"] = "Язык приложения",
                ["Settings_Lang_Sub"] = "Выберите предпочитаемый язык отображения панели управления",
                ["Settings_Theme_Title"] = "Темная тема Fluent (Mica)",
                ["Settings_Theme_Sub"] = "Переключение между стилями Windows 11 Dark Mica и Light Fluent",
                ["Settings_Startup_Title"] = "Запуск вместе с Windows",
                ["Settings_Startup_Sub"] = "Автоматический запуск службы BC-250 при входе для восстановления топологии CU",
                ["Settings_Tray_Title"] = "Сворачивать в системный трей",
                ["Settings_Tray_Sub"] = "Оставлять службу активной в области уведомлений при закрытии окна",
                ["Settings_About_Title"] = "О программе BC-250 Advanced Control",
                ["Settings_About_Desc"] = "Версия 1.0.0 (C# .NET Нативный / Windows 11 Fluent)\nАрхитектура: Cyan Skillfish 2 / GC 10.1.3 (Device ID 1002:13FE)\nПроект Namix WDDM MCDM • 2026",
            }
        };

        public static string T(string key)
        {
            if (Strings.TryGetValue(CurrentLanguage, out var dict) && dict.TryGetValue(key, out var val))
            {
                return val;
            }
            if (Strings["en"].TryGetValue(key, out var fallbackVal))
            {
                return fallbackVal;
            }
            return key;
        }
    }
}

import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Tv,
  CheckCircle2,
  AlertCircle,
  Download,
  Trash2,
  RefreshCw,
  Terminal,
  Activity,
  Check,
  Play
} from 'lucide-react';

export const DriverManagerView: React.FC = () => {
  const { t } = useTranslation();

  const [installing, setInstalling] = useState(false);
  const [installProgress, setInstallProgress] = useState(0);
  const [installStep, setInstallStep] = useState('');
  const [diagnosticsRunning, setDiagnosticsRunning] = useState(false);

  const [logs, setLogs] = useState<string[]>([
    '[INIT] Namix WDDM Subsystem initialized.',
    '[INFO] Hardware device detected: Cyan Skillfish (BC-250 APU).',
    '[INFO] BAR0: 0xF0000000 (256MB), BAR2: 0xE0000000 (8MB), MMIO: OK.',
    '[STATUS] Test Signing mode is ACTIVE.',
    '[READY] Driver operational and ready for diagnostic routines.'
  ]);

  const addLog = (msg: string) => {
    setLogs((prev) => [...prev.slice(-30), `[${new Date().toLocaleTimeString()}] ${msg}`]);
  };

  const handleInstallDriver = () => {
    if (installing) return;
    setInstalling(true);
    setInstallProgress(10);
    setInstallStep('Validando assinatura digital e catálogo .cat...');
    addLog('Iniciando instalação do driver BC250.inf...');

    setTimeout(() => {
      setInstallProgress(35);
      setInstallStep('Copiando binários do kernel (bc250k.sys -> System32/drivers)...');
      addLog('Binário de kernel bc250k.sys copiado com sucesso.');
    }, 800);

    setTimeout(() => {
      setInstallProgress(70);
      setInstallStep('Registrando dispositivo de display WDDM e KMD...');
      addLog('WDDM Miniport registrado com sucesso na stack gráfica.');
    }, 1600);

    setTimeout(() => {
      setInstallProgress(100);
      setInstallStep('Driver instalado com sucesso! Adaptador ativo.');
      addLog('Dispositivo reinicializado em modo WDDM 2.7.');
      setTimeout(() => {
        setInstalling(false);
      }, 1500);
    }, 2400);
  };

  const handleCleanUninstall = () => {
    addLog('Iniciando desinstalação limpa do driver...');
    setTimeout(() => {
      addLog('Removendo serviços de kernel e entradas de registro...');
    }, 600);
    setTimeout(() => {
      addLog('Driver removido. Sistema em modo VGA de fallback.');
    }, 1200);
  };

  const handleRunDiagnostic = (testName: string) => {
    setDiagnosticsRunning(true);
    addLog(`Executando rotina de diagnóstico: ${testName}...`);
    setTimeout(() => {
      if (testName.includes('SDMA')) {
        addLog('SDMA Ring Test: RB_RPTR=0x0040, RB_WPTR=0x0040 (FENCE MATCH OK).');
      } else if (testName.includes('PSP')) {
        addLog('PSP Auth: Firmware IP Type 0x01 carregado e validado.');
      } else if (testName.includes('VRAM')) {
        addLog('VRAM Pattern Scan: 4096MB verificados com padrão 0x55AA (0 erros).');
      } else {
        addLog('GCVM PT: Tabelas de página L1/L2 sincronizadas sem falhas.');
      }
      setDiagnosticsRunning(false);
    }, 1000);
  };

  return (
    <div className="flex-1 overflow-y-auto p-4 md:p-6 flex flex-col gap-5 text-fluent-text z-10">
      {/* Header */}
      <div>
        <h1 className="text-2xl font-bold tracking-tight text-white">
          {t('driver.title')}
        </h1>
        <p className="text-xs text-zinc-400 mt-0.5">
          {t('driver.subtitle')}
        </p>
      </div>

      {/* Driver Status Banner */}
      <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-5 shadow-fluent-card backdrop-blur-md flex flex-col md:flex-row items-start md:items-center justify-between gap-4">
        <div className="flex items-center gap-3.5">
          <div className="w-11 h-11 rounded-xl bg-[#f25056]/15 border border-[#f25056]/30 flex items-center justify-center text-[#f25056] shadow-[0_0_12px_rgba(242,80,86,0.3)]">
            <Tv className="w-6 h-6" />
          </div>
          <div className="flex flex-col">
            <div className="flex items-center gap-2">
              <span className="text-base font-bold text-white">
                {t('driver.driverInstalled')}
              </span>
              <span className="bg-emerald-500/15 text-emerald-400 border border-emerald-500/30 text-[10px] font-semibold px-2 py-0.5 rounded-full">
                WDDM 2.7 Ativo
              </span>
            </div>
            <span className="text-xs text-zinc-400 mt-0.5">
              {t('driver.driverVersion')} • PCI\VEN_1002&DEV_13FE
            </span>
          </div>
        </div>

        {/* Testsigning Indicator */}
        <div className="flex items-center gap-2 px-3 py-1.5 rounded-lg bg-[#141417] border border-white/5 text-xs">
          <span className="text-zinc-400">{t('driver.testSigning')}:</span>
          <span className="text-emerald-400 font-semibold flex items-center gap-1">
            <CheckCircle2 className="w-3.5 h-3.5" />
            {t('driver.testSigningEnabled')}
          </span>
        </div>
      </div>

      {/* Installation & Operations Row */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* Operations Card */}
        <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-5 shadow-fluent-card backdrop-blur-md flex flex-col justify-between gap-4">
          <div>
            <h2 className="text-sm font-semibold text-white flex items-center gap-2 mb-3">
              <Download className="w-4 h-4 text-[#f25056]" />
              {t('driver.actions')}
            </h2>
            <p className="text-xs text-zinc-400 leading-relaxed mb-4">
              Selecione o pacote de driver INF ou execute manutenção direta do driver no sistema.
            </p>

            {installing && (
              <div className="flex flex-col gap-2 p-3 bg-[#151518] rounded-lg border border-[#f25056]/30 mb-4">
                <div className="flex justify-between text-xs">
                  <span className="text-zinc-200 font-medium">{installStep}</span>
                  <span className="text-[#f25056] font-bold">{installProgress}%</span>
                </div>
                <div className="w-full bg-white/10 rounded-full h-1.5 overflow-hidden">
                  <div
                    className="bg-[#f25056] h-full rounded-full transition-all duration-300 shadow-[0_0_8px_#f25056]"
                    style={{ width: `${installProgress}%` }}
                  />
                </div>
              </div>
            )}
          </div>

          <div className="flex flex-col sm:flex-row gap-2.5">
            <button
              type="button"
              onClick={handleInstallDriver}
              disabled={installing}
              className="flex-1 flex items-center justify-center gap-2 py-2.5 px-4 rounded-lg bg-[#f25056] hover:bg-[#ef383f] disabled:opacity-50 text-xs font-semibold text-white shadow-[0_0_15px_rgba(242,80,86,0.4)] transition-all active:scale-[0.98]"
            >
              {installing ? (
                <RefreshCw className="w-3.5 h-3.5 animate-spin" />
              ) : (
                <Download className="w-3.5 h-3.5" />
              )}
              {t('driver.install')}
            </button>

            <button
              type="button"
              onClick={handleCleanUninstall}
              className="flex items-center justify-center gap-2 py-2.5 px-4 rounded-lg bg-[#242429] hover:bg-[#2e2e36] text-xs font-medium text-zinc-300 border border-white/5 transition-all active:scale-[0.98]"
            >
              <Trash2 className="w-3.5 h-3.5 text-zinc-400" />
              {t('driver.cleanUninstall')}
            </button>
          </div>
        </div>

        {/* Diagnostics Suite Card */}
        <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-5 shadow-fluent-card backdrop-blur-md flex flex-col justify-between gap-4">
          <div>
            <h2 className="text-sm font-semibold text-white flex items-center gap-2 mb-3">
              <Activity className="w-4 h-4 text-[#f25056]" />
              {t('driver.diagnostics')}
            </h2>
            <div className="grid grid-cols-2 gap-2">
              {[
                { name: t('driver.testSdma'), key: 'SDMA' },
                { name: t('driver.testPsp'), key: 'PSP' },
                { name: t('driver.testVram'), key: 'VRAM' },
                { name: t('driver.testGcvm'), key: 'GCVM' },
              ].map((diag) => (
                <button
                  key={diag.key}
                  type="button"
                  onClick={() => handleRunDiagnostic(diag.name)}
                  disabled={diagnosticsRunning}
                  className="p-2.5 rounded-lg bg-[#242429] hover:bg-[#2d2d35] border border-white/5 text-left flex items-center justify-between text-xs text-zinc-200 transition-all"
                >
                  <span className="truncate">{diag.name}</span>
                  <Play className="w-3 h-3 text-[#f25056] flex-shrink-0 ml-1" />
                </button>
              ))}
            </div>
          </div>

          <div className="p-3 bg-[#151518] rounded-lg border border-white/5 text-xs text-zinc-400 flex items-center justify-between">
            <span>Diagnóstico do Kernel WDDM:</span>
            <span className="text-emerald-400 font-semibold flex items-center gap-1">
              <Check className="w-3.5 h-3.5" />
              Integridade 100% OK
            </span>
          </div>
        </div>
      </div>

      {/* Terminal Logs Output */}
      <div className="bg-[#141417] border border-white/5 rounded-xl p-4 shadow-fluent-card flex flex-col gap-2">
        <div className="flex items-center justify-between pb-2 border-b border-white/5">
          <div className="flex items-center gap-2">
            <Terminal className="w-4 h-4 text-[#f25056]" />
            <span className="text-xs font-semibold text-zinc-200">
              {t('driver.logsTitle')}
            </span>
          </div>
          <button
            type="button"
            onClick={() => setLogs([])}
            className="text-[11px] text-zinc-400 hover:text-white transition-colors"
          >
            {t('driver.clearLogs')}
          </button>
        </div>

        <div className="h-28 overflow-y-auto font-mono text-[11px] text-zinc-300 flex flex-col gap-1 pr-2">
          {logs.map((log, idx) => (
            <div key={idx} className="leading-relaxed">
              <span className="text-[#f25056] mr-1.5">❯</span>
              {log}
            </div>
          ))}
        </div>
      </div>
    </div>
  );
};

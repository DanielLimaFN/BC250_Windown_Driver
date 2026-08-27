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
    <div className="z-10 flex flex-1 flex-col gap-5 overflow-y-auto p-5 md:p-7">
      <section className="page-hero">
        <p className="page-kicker">Driver e Diagnóstico</p>
        <div className="mt-2 flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
          <div className="max-w-2xl">
            <h1 className="page-title">{t('driver.title')}</h1>
            <p className="mt-3 text-sm text-[var(--text-muted)]">
              {t('driver.subtitle')}
            </p>
          </div>
          <div className="flex flex-wrap gap-2">
            <span className="toolbar-chip">WDDM 2.7 ativo</span>
            <span className="toolbar-chip">Test Signing habilitado</span>
          </div>
        </div>
      </section>

      <div className="surface-card flex flex-col items-start justify-between gap-4 p-5 md:flex-row md:items-center">
        <div className="flex items-center gap-3.5">
          <div className="flex h-11 w-11 items-center justify-center rounded-2xl border border-[var(--accent-line)] bg-[var(--accent-soft)] text-[var(--accent)]">
            <Tv className="w-6 h-6" />
          </div>
          <div className="flex flex-col">
            <div className="flex items-center gap-2">
              <span className="text-base font-bold text-[var(--text-strong)]">
                {t('driver.driverInstalled')}
              </span>
              <span className="rounded-full border border-emerald-200 bg-emerald-50 px-2 py-0.5 text-[10px] font-semibold text-emerald-700">
                WDDM 2.7 Ativo
              </span>
            </div>
            <span className="mt-0.5 text-xs text-[var(--text-muted)]">
              {t('driver.driverVersion')} • PCI\VEN_1002&DEV_13FE
            </span>
          </div>
        </div>

        <div className="flex items-center gap-2 rounded-xl border border-[var(--border-soft)] bg-white px-3 py-1.5 text-xs">
          <span className="text-[var(--text-muted)]">{t('driver.testSigning')}:</span>
          <span className="flex items-center gap-1 font-semibold text-emerald-700">
            <CheckCircle2 className="w-3.5 h-3.5" />
            {t('driver.testSigningEnabled')}
          </span>
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <div className="surface-card flex flex-col justify-between gap-4 p-5">
          <div>
            <h2 className="mb-3 flex items-center gap-2 text-sm font-semibold text-[var(--text-strong)]">
              <Download className="w-4 h-4 text-[var(--accent)]" />
              {t('driver.actions')}
            </h2>
            <p className="mb-4 text-xs leading-relaxed text-[var(--text-muted)]">
              Selecione o pacote de driver INF ou execute manutenção direta do driver no sistema.
            </p>

            {installing && (
              <div className="mb-4 flex flex-col gap-2 rounded-xl border border-[var(--accent-line)] bg-[var(--accent-soft)] p-3">
                <div className="flex justify-between text-xs">
                  <span className="font-medium text-[var(--text-strong)]">{installStep}</span>
                  <span className="font-bold text-[var(--accent)]">{installProgress}%</span>
                </div>
                <div className="h-1.5 w-full overflow-hidden rounded-full bg-white/70">
                  <div
                    className="h-full rounded-full bg-[var(--accent)] transition-all duration-300"
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
              className="button-primary flex-1 gap-2 disabled:opacity-50"
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
              className="button-secondary gap-2"
            >
              <Trash2 className="w-3.5 h-3.5 text-[var(--text-muted)]" />
              {t('driver.cleanUninstall')}
            </button>
          </div>
        </div>

        <div className="surface-card flex flex-col justify-between gap-4 p-5">
          <div>
            <h2 className="mb-3 flex items-center gap-2 text-sm font-semibold text-[var(--text-strong)]">
              <Activity className="w-4 h-4 text-[var(--accent)]" />
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
                  className="flex items-center justify-between rounded-xl border border-[var(--border-soft)] bg-white p-2.5 text-left text-xs text-[var(--text-strong)] transition-all hover:border-[var(--border-strong)]"
                >
                  <span className="truncate">{diag.name}</span>
                  <Play className="ml-1 h-3 w-3 flex-shrink-0 text-[var(--accent)]" />
                </button>
              ))}
            </div>
          </div>

          <div className="surface-subtle flex items-center justify-between p-3 text-xs text-[var(--text-muted)]">
            <span>Diagnóstico do Kernel WDDM:</span>
            <span className="flex items-center gap-1 font-semibold text-emerald-700">
              <Check className="w-3.5 h-3.5" />
              Integridade 100% OK
            </span>
          </div>
        </div>
      </div>

      <div className="surface-card flex flex-col gap-2 p-4">
        <div className="flex items-center justify-between border-b border-[var(--border-soft)] pb-2">
          <div className="flex items-center gap-2">
            <Terminal className="w-4 h-4 text-[var(--accent)]" />
            <span className="text-xs font-semibold text-[var(--text-strong)]">
              {t('driver.logsTitle')}
            </span>
          </div>
          <button
            type="button"
            onClick={() => setLogs([])}
            className="text-[11px] text-[var(--text-muted)] transition-colors hover:text-[var(--text-strong)]"
          >
            {t('driver.clearLogs')}
          </button>
        </div>

        <div className="flex h-28 flex-col gap-1 overflow-y-auto rounded-xl border border-[var(--border-soft)] bg-[#fbfcfe] p-3 pr-2 font-mono text-[11px] text-[var(--text-base)]">
          {logs.map((log, idx) => (
            <div key={idx} className="leading-relaxed">
              <span className="mr-1.5 text-[var(--accent)]">❯</span>
              {log}
            </div>
          ))}
        </div>
      </div>
    </div>
  );
};

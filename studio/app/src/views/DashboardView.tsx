import React, { useState } from 'react';
import { MoreHorizontal, CheckCircle2, RefreshCw, Heart } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { CircularGauge } from '../components/CircularGauge';
import { HistoryGraph } from '../components/HistoryGraph';
import { DualRangeSlider, SingleSlider } from '../components/FluentSlider';
import { FluentSwitch } from '../components/FluentSwitch';

interface TelemetryState {
  cpuLoad: number;
  cpuClock: number;
  cpuTemp: number;
  gpuTemp: number;
  gpuUsage: number;
  gpuClock: number;
  gpuVoltage: number;
  gpuPower: number;
  tempHistory: number[];
  usageHistory: number[];
}

interface DashboardViewProps {
  telemetry: TelemetryState;
  onNavigate: (view: 'dashboard' | 'system' | 'performance' | 'unlocker' | 'driver' | 'settings') => void;
  onOpenDonate?: () => void;
}

export const DashboardView: React.FC<DashboardViewProps> = ({ telemetry, onNavigate, onOpenDonate }) => {
  const { t } = useTranslation();

  // Overclocking states
  const [minFreq, setMinFreq] = useState(1800);
  const [maxFreq, setMaxFreq] = useState(2050);
  const [voltageOffset, setVoltageOffset] = useState(50);
  const [throttleTemp, setThrottleTemp] = useState('95');
  const [recoveryTemp, setRecoveryTemp] = useState('85');

  // Unlocker states
  const [cpuUnlocked, setCpuUnlocked] = useState(true);
  const [gpuUnlocked, setGpuUnlocked] = useState(true);

  // Driver action state
  const [activeDriverAction, setActiveDriverAction] = useState<string | null>('diag');
  const [diagnosticRunning, setDiagnosticRunning] = useState(false);
  const [actionNotice, setActionNotice] = useState<string | null>(null);

  const handleDriverAction = (action: string) => {
    setActiveDriverAction(action);
    if (action === 'diag') {
      setDiagnosticRunning(true);
      setActionNotice(t('driver.runningTest'));
      setTimeout(() => {
        setDiagnosticRunning(false);
        setActionNotice(t('driver.testPassed'));
      }, 1500);
    } else if (action === 'install') {
      setActionNotice('Iniciando assistente de instalação do driver...');
      setTimeout(() => onNavigate('driver'), 600);
    } else if (action === 'remove') {
      setActionNotice('Preparando desinstalação limpa...');
      setTimeout(() => onNavigate('driver'), 600);
    }
  };

  return (
    <div className="relative z-10 flex flex-1 flex-col gap-5 overflow-y-auto p-5 md:p-7">
      <section className="page-hero">
        <div className="flex flex-col gap-5 lg:flex-row lg:items-end lg:justify-between">
          <div className="max-w-2xl">
            <p className="page-kicker">Painel Principal</p>
            <h1 className="page-title mt-2">{t('dashboard.title')}</h1>
            <p className="mt-3 text-sm leading-relaxed text-[var(--text-muted)]">
              Uma visão central do sistema com telemetria simulada, ajustes rápidos e ações principais sem o visual carregado do layout anterior.
            </p>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <span className="toolbar-chip">
              BIOS P4.00G
            </span>
            <span className="toolbar-chip">
              Windows 11 23H2
            </span>
            {onOpenDonate && (
              <button
                type="button"
                onClick={onOpenDonate}
                className="inline-flex items-center gap-1.5 rounded-full border border-rose-200 bg-rose-50/80 px-3 py-1 text-[11px] font-semibold text-rose-600 shadow-2xs hover:bg-rose-100 transition-colors"
                title={t('donate.title', 'Apoiar o Desenvolvimento do Driver')}
              >
                <Heart className="h-3 w-3 fill-current text-rose-500" />
                {t('donate.buttonLabel', 'Doar')}
              </button>
            )}
            {actionNotice && (
              <span className="chip-status border-[var(--accent-line)] bg-[var(--accent-soft)] text-[var(--accent)]">
                {actionNotice}
              </span>
            )}
          </div>
        </div>

        <div className="mt-5 grid grid-cols-2 gap-3 xl:grid-cols-4">
          <div className="stat-card">
            <p className="text-[11px] uppercase tracking-[0.16em] text-[var(--text-soft)]">
              GPU Uso
            </p>
            <p className="mt-2 text-3xl font-semibold text-[var(--text-strong)]">
              {telemetry.gpuUsage}%
            </p>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              Carga atual da GPU simulada
            </p>
          </div>
          <div className="stat-card">
            <p className="text-[11px] uppercase tracking-[0.16em] text-[var(--text-soft)]">
              GPU Temp
            </p>
            <p className="mt-2 text-3xl font-semibold text-[var(--text-strong)]">
              {telemetry.gpuTemp}°C
            </p>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              Envelope térmico atual
            </p>
          </div>
          <div className="stat-card">
            <p className="text-[11px] uppercase tracking-[0.16em] text-[var(--text-soft)]">
              Clock GPU
            </p>
            <p className="mt-2 text-3xl font-semibold text-[var(--text-strong)]">
              {telemetry.gpuClock}
            </p>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              MHz em operação simulada
            </p>
          </div>
          <div className="stat-card">
            <p className="text-[11px] uppercase tracking-[0.16em] text-[var(--text-soft)]">
              Potência
            </p>
            <p className="mt-2 text-3xl font-semibold text-[var(--text-strong)]">
              {telemetry.gpuPower}W
            </p>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              Consumo estimado instantâneo
            </p>
          </div>
        </div>
      </section>

      <div className="grid grid-cols-1 gap-4 xl:grid-cols-12">
        <section className="surface-card flex flex-col gap-4 p-5 xl:col-span-8">
          <div className="flex items-center justify-between">
            <div>
              <h2 className="section-title">{t('dashboard.systemOverview')}</h2>
              <p className="section-subtitle mt-1">
                CPU e GPU agrupados em uma única área de monitoramento.
              </p>
            </div>
            <button
              type="button"
              onClick={() => onNavigate('performance')}
              aria-label="Ver mais opções de desempenho"
              className="button-ghost"
            >
              <MoreHorizontal className="w-4 h-4" />
            </button>
          </div>

          <div className="grid grid-cols-1 gap-4 lg:grid-cols-[1.15fr_1fr]">
            <div className="surface-subtle p-4">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-semibold text-[var(--text-strong)]">{t('dashboard.cpu')}</p>
                  <p className="mt-1 text-xs text-[var(--text-muted)]">Zen 2 • Telemetria base</p>
                </div>
                <span className="toolbar-chip">{telemetry.cpuClock.toFixed(1)} GHz</span>
              </div>

              <div className="mt-5 flex flex-wrap justify-center gap-4">
                <CircularGauge
                  value={telemetry.cpuLoad}
                  unit="%"
                  subValue={`${telemetry.cpuClock.toFixed(1)}GHz`}
                  label={t('dashboard.load')}
                  size={118}
                  strokeWidth={8}
                />
                <CircularGauge
                  value={telemetry.cpuTemp}
                  unit="°C"
                  label={t('dashboard.temperature')}
                  size={118}
                  strokeWidth={8}
                />
              </div>
            </div>

            <div className="surface-subtle p-4">
              <div className="flex items-center justify-between">
                <div>
                  <p className="text-sm font-semibold text-[var(--text-strong)]">{t('dashboard.gpu')}</p>
                  <p className="mt-1 text-xs text-[var(--text-muted)]">{t('dashboard.gpuModel')}</p>
                </div>
                <span className="toolbar-chip">{telemetry.gpuVoltage.toFixed(2)} V</span>
              </div>

              <div className="mt-5 grid grid-cols-2 gap-4 xl:grid-cols-3">
                <CircularGauge
                  value={telemetry.gpuTemp}
                  unit="°C"
                  label={t('dashboard.temperature')}
                  size={92}
                  strokeWidth={7}
                />
                <CircularGauge
                  value={telemetry.gpuUsage}
                  unit="%"
                  label={t('dashboard.usage')}
                  size={92}
                  strokeWidth={7}
                />
                <CircularGauge
                  value={telemetry.gpuClock}
                  min={500}
                  max={2500}
                  unit=" MHz"
                  label={t('dashboard.clock')}
                  size={92}
                  strokeWidth={7}
                />
                <CircularGauge
                  value={Number(telemetry.gpuVoltage.toFixed(2))}
                  min={0.7}
                  max={1.3}
                  unit="V"
                  label={t('dashboard.voltage')}
                  size={92}
                  strokeWidth={7}
                />
                <CircularGauge
                  value={telemetry.gpuPower}
                  min={0}
                  max={250}
                  unit="W"
                  label={t('dashboard.power')}
                  size={92}
                  strokeWidth={7}
                />
              </div>
            </div>
          </div>

          <div className="grid grid-cols-1 gap-3 lg:grid-cols-2">
            <HistoryGraph
              title={t('dashboard.tempHistory')}
              data={telemetry.tempHistory}
              min={30}
              max={95}
              unit="°C"
              currentValue={telemetry.gpuTemp}
            />
            <HistoryGraph
              title={t('dashboard.usageHistory')}
              data={telemetry.usageHistory}
              min={0}
              max={100}
              unit="%"
              isStep={true}
              currentValue={telemetry.gpuUsage}
            />
          </div>
        </section>

        <section className="flex flex-col gap-4 xl:col-span-4">
          <div className="surface-card flex flex-col gap-4 p-5">
            <div className="flex items-center justify-between">
              <div>
                <h2 className="section-title">{t('dashboard.performanceTuning')}</h2>
                <p className="section-subtitle mt-1">
                  Ajustes rápidos sem abrir outra página.
                </p>
              </div>
              <button
                type="button"
                onClick={() => onNavigate('performance')}
                aria-label="Ver mais opções de ajuste fino de desempenho"
                className="button-ghost"
              >
                <MoreHorizontal className="w-4 h-4" />
              </button>
            </div>

            <DualRangeSlider
              label={t('dashboard.coreFreq')}
              minVal={minFreq}
              maxVal={maxFreq}
              min={1000}
              max={2400}
              unit="MHz"
              onChange={(min, max) => {
                setMinFreq(min);
                setMaxFreq(max);
              }}
            />

            <SingleSlider
              label={t('dashboard.voltageOffset')}
              value={voltageOffset}
              min={0}
              max={150}
              unit=" mV"
              onChange={setVoltageOffset}
            />

            <div className="grid grid-cols-2 gap-3">
              <div className="flex flex-col gap-1.5">
                <label htmlFor="throttle-temp-input" className="text-xs font-medium text-[var(--text-muted)]">
                  {t('dashboard.throttleTemp')}
                </label>
                <input
                  id="throttle-temp-input"
                  type="number"
                  value={throttleTemp}
                  onChange={(e) => setThrottleTemp(e.target.value)}
                  className="field-input"
                />
              </div>

              <div className="flex flex-col gap-1.5">
                <label htmlFor="recovery-temp-input" className="text-xs font-medium text-[var(--text-muted)]">
                  {t('dashboard.recoveryTemp')}
                </label>
                <input
                  id="recovery-temp-input"
                  type="number"
                  value={recoveryTemp}
                  onChange={(e) => setRecoveryTemp(e.target.value)}
                  className="field-input"
                />
              </div>
            </div>
          </div>

          <div className="surface-card flex flex-col gap-4 p-5">
            <div className="flex items-center justify-between">
              <div>
                <h2 className="section-title">{t('dashboard.driverManager')}</h2>
                <p className="section-subtitle mt-1">
                  Ações diretas para instalar, remover ou validar.
                </p>
              </div>
              <button
                type="button"
                onClick={() => onNavigate('driver')}
                aria-label="Ver mais opções do gerenciador de driver"
                className="button-ghost"
              >
                <MoreHorizontal className="w-4 h-4" />
              </button>
            </div>

            <button
              type="button"
              onClick={() => handleDriverAction('diag')}
              className="button-primary w-full gap-2"
            >
              {diagnosticRunning ? (
                <RefreshCw className="h-3.5 w-3.5 animate-spin" />
              ) : (
                <CheckCircle2 className="h-3.5 w-3.5" />
              )}
              {t('dashboard.runDiagnostics')}
            </button>

            <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
              <button
                type="button"
                onClick={() => handleDriverAction('install')}
                className="button-secondary w-full"
              >
                {t('dashboard.installDriver')}
              </button>
              <button
                type="button"
                onClick={() => handleDriverAction('remove')}
                className="button-secondary w-full"
              >
                {t('dashboard.removeDriver')}
              </button>
            </div>
          </div>

          <div className="surface-card flex flex-col gap-3.5 p-5">
            <div className="flex items-center justify-between">
              <div>
                <h2 className="section-title">{t('dashboard.advUnlocker')}</h2>
                <p className="section-subtitle mt-1">
                  Estados principais do unlocker avançado.
                </p>
              </div>
              <button
                type="button"
                onClick={() => onNavigate('unlocker')}
                aria-label="Ver mais opções de desbloqueio avançado"
                className="button-ghost"
              >
                <MoreHorizontal className="w-4 h-4" />
              </button>
            </div>

            <FluentSwitch
              checked={cpuUnlocked}
              onChange={setCpuUnlocked}
              title={t('dashboard.cpuUnlock')}
              subtitle={cpuUnlocked ? t('dashboard.cpuUnlockDesc') : '6 Cores / 12 Threads'}
              secondarySubtitle={cpuUnlocked ? t('dashboard.cpuUnlockSub') : undefined}
              hasWarning={true}
              onWarningClick={() => onNavigate('unlocker')}
            />

            <FluentSwitch
              checked={gpuUnlocked}
              onChange={setGpuUnlocked}
              title={t('dashboard.gpuUnlock')}
              subtitle={gpuUnlocked ? t('dashboard.gpuUnlockDesc') : '24 CU (Modo Fábrica)'}
              secondarySubtitle={gpuUnlocked ? t('dashboard.gpuUnlockSub') : undefined}
            />
          </div>
        </section>
      </div>
    </div>
  );
};

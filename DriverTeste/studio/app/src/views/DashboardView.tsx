import React, { useState } from 'react';
import { MoreHorizontal, Play, CheckCircle2, RefreshCw } from 'lucide-react';
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
  onNavigate: (view: 'dashboard' | 'performance' | 'unlocker' | 'driver' | 'settings') => void;
}

export const DashboardView: React.FC<DashboardViewProps> = ({ telemetry, onNavigate }) => {
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
    <div className="flex-1 overflow-y-auto p-4 md:p-6 flex flex-col gap-4 text-fluent-text relative z-10">
      {/* View Header */}
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold tracking-tight text-white">
          {t('dashboard.title')}
        </h1>
        {actionNotice && (
          <span className="text-xs text-[#f87171] bg-[#f25056]/10 px-2.5 py-1 rounded-md border border-[#f25056]/20 animate-pulse">
            {actionNotice}
          </span>
        )}
      </div>

      {/* Main Grid: Left Column & Right Column */}
      <div className="grid grid-cols-1 lg:grid-cols-12 gap-4">
        {/* ========================================================================= */}
        {/* LEFT COLUMN: System Overview (Telemetria) + Driver Manager                 */}
        {/* ========================================================================= */}
        <div className="lg:col-span-7 flex flex-col gap-4">
          {/* Card: System Overview */}
          <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex flex-col gap-4 relative overflow-hidden">
            {/* Ambient Red Lighting inside Card */}
            <div className="absolute -top-10 -left-10 w-48 h-48 bg-[#f25056]/10 rounded-full blur-3xl pointer-events-none" />

            {/* Header */}
            <div className="flex items-center justify-between">
              <h2 className="text-sm font-semibold text-zinc-100">
                {t('dashboard.systemOverview')}
              </h2>
              <button
                type="button"
                onClick={() => onNavigate('performance')}
                aria-label="Ver mais opções de desempenho"
                className="text-zinc-500 hover:text-zinc-300 transition-colors p-1"
              >
                <MoreHorizontal className="w-4 h-4" />
              </button>
            </div>

            {/* Top Row: CPU & GPU Sections */}
            <div className="grid grid-cols-1 md:grid-cols-12 gap-3">
              {/* CPU Box */}
              <div className="md:col-span-5 bg-[#161619]/80 border border-white/5 rounded-lg p-3 flex flex-col justify-between">
                <div className="flex items-center justify-between mb-2">
                  <span className="text-sm font-bold text-white tracking-wide">
                    {t('dashboard.cpu')}
                  </span>
                </div>

                <div className="flex items-center justify-around py-1">
                  <CircularGauge
                    value={telemetry.cpuLoad}
                    unit="%"
                    subValue={`${telemetry.cpuClock.toFixed(1)}GHz`}
                    label={t('dashboard.load')}
                    size={90}
                    strokeWidth={7}
                  />
                  <CircularGauge
                    value={telemetry.cpuTemp}
                    unit="°C"
                    label={t('dashboard.temperature')}
                    size={90}
                    strokeWidth={7}
                  />
                </div>

                {/* BIOS & OS Info */}
                <div className="mt-3 pt-2 border-t border-white/5 flex flex-col text-[11px] text-zinc-400 gap-0.5">
                  <div className="flex justify-between">
                    <span>{t('dashboard.bios')}:</span>
                    <span className="text-zinc-200 font-medium">P4.00G</span>
                  </div>
                  <div className="flex justify-between">
                    <span>{t('dashboard.winVer')}:</span>
                    <span className="text-zinc-200 font-medium">11 23H2</span>
                  </div>
                </div>
              </div>

              {/* GPU Box (Cyan Skillfish) */}
              <div className="md:col-span-7 bg-[#161619]/80 border border-white/5 rounded-lg p-3 flex flex-col justify-between">
                <div className="flex items-center justify-between mb-1">
                  <div className="flex flex-col">
                    <span className="text-sm font-bold text-white tracking-wide">
                      {t('dashboard.gpu')}
                    </span>
                    <span className="text-[11px] text-zinc-400 font-medium">
                      {t('dashboard.gpuModel')}
                    </span>
                  </div>
                </div>

                {/* 6 GPU Dials */}
                <div className="grid grid-cols-3 gap-2 py-1">
                  <CircularGauge
                    value={telemetry.gpuTemp}
                    unit="°C"
                    label={t('dashboard.temperature')}
                    size={74}
                    strokeWidth={6}
                  />
                  <CircularGauge
                    value={telemetry.gpuUsage}
                    unit="%"
                    label={t('dashboard.usage')}
                    size={74}
                    strokeWidth={6}
                  />
                  <CircularGauge
                    value={telemetry.gpuClock}
                    min={500}
                    max={2500}
                    label={t('dashboard.power')}
                    size={74}
                    strokeWidth={6}
                  />
                  <CircularGauge
                    value={telemetry.gpuClock}
                    min={500}
                    max={2500}
                    unit=" MHz"
                    label={t('dashboard.clock')}
                    size={74}
                    strokeWidth={6}
                  />
                  <CircularGauge
                    value={Number(telemetry.gpuVoltage.toFixed(2))}
                    min={0.7}
                    max={1.3}
                    unit="V"
                    label={t('dashboard.voltage')}
                    size={74}
                    strokeWidth={6}
                  />
                  <CircularGauge
                    value={telemetry.gpuPower}
                    min={0}
                    max={250}
                    unit="W"
                    label={t('dashboard.power')}
                    size={74}
                    strokeWidth={6}
                  />
                </div>
              </div>
            </div>

            {/* Bottom Row: Temperature History & Usage History */}
            <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
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
          </div>

          {/* Card: Driver Manager (Quick Actions) */}
          <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex flex-col gap-3">
            <div className="flex items-center justify-between">
              <h2 className="text-sm font-semibold text-zinc-100">
                {t('dashboard.driverManager')}
              </h2>
              <button
                type="button"
                onClick={() => onNavigate('driver')}
                aria-label="Ver mais opções do gerenciador de driver"
                className="text-zinc-500 hover:text-zinc-300 transition-colors p-1"
              >
                <MoreHorizontal className="w-4 h-4" />
              </button>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-3 gap-2.5">
              {/* Install Driver */}
              <button
                type="button"
                onClick={() => handleDriverAction('install')}
                className="relative py-2.5 px-3 rounded-lg bg-[#242429] hover:bg-[#2b2b32] border border-white/5 hover:border-white/10 text-xs font-medium text-zinc-200 transition-all active:scale-[0.98] text-center overflow-hidden"
              >
                {t('dashboard.installDriver')}
                {activeDriverAction === 'install' && (
                  <div className="absolute bottom-0 left-3 right-3 h-0.5 bg-[#f25056] shadow-[0_0_6px_#f25056]" />
                )}
              </button>

              {/* Remove Driver */}
              <button
                type="button"
                onClick={() => handleDriverAction('remove')}
                className="relative py-2.5 px-3 rounded-lg bg-[#242429] hover:bg-[#2b2b32] border border-white/5 hover:border-white/10 text-xs font-medium text-zinc-200 transition-all active:scale-[0.98] text-center overflow-hidden"
              >
                {t('dashboard.removeDriver')}
                {activeDriverAction === 'remove' && (
                  <div className="absolute bottom-0 left-3 right-3 h-0.5 bg-[#f25056] shadow-[0_0_6px_#f25056]" />
                )}
              </button>

              {/* Run Diagnostic Tests */}
              <button
                type="button"
                onClick={() => handleDriverAction('diag')}
                className="relative py-2.5 px-3 rounded-lg bg-[#242429] hover:bg-[#2b2b32] border border-white/5 hover:border-white/10 text-xs font-medium text-zinc-200 transition-all active:scale-[0.98] flex items-center justify-center gap-1.5 overflow-hidden"
              >
                {diagnosticRunning ? (
                  <RefreshCw className="w-3.5 h-3.5 animate-spin text-[#f25056]" />
                ) : null}
                <span>{t('dashboard.runDiagnostics')}</span>
                {activeDriverAction === 'diag' && (
                  <div className="absolute bottom-0 left-3 right-3 h-0.5 bg-[#f25056] shadow-[0_0_6px_#f25056]" />
                )}
              </button>
            </div>
          </div>
        </div>

        {/* ========================================================================= */}
        {/* RIGHT COLUMN: Performance Tuning + Advanced CPU/GPU Unlocker              */}
        {/* ========================================================================= */}
        <div className="lg:col-span-5 flex flex-col gap-4">
          {/* Card: Performance Tuning */}
          <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex flex-col gap-4">
            <div className="flex items-center justify-between">
              <h2 className="text-sm font-semibold text-zinc-100">
                {t('dashboard.performanceTuning')}
              </h2>
              <button
                type="button"
                onClick={() => onNavigate('performance')}
                aria-label="Ver mais opções de ajuste fino de desempenho"
                className="text-zinc-500 hover:text-zinc-300 transition-colors p-1"
              >
                <MoreHorizontal className="w-4 h-4" />
              </button>
            </div>

            <div className="flex flex-col gap-3">
              <span className="text-xs font-semibold text-zinc-300">
                {t('dashboard.overclocking')}
              </span>

              {/* Dual Range Slider for Core Frequency */}
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

              {/* Single Slider for Voltage Offset */}
              <SingleSlider
                label={t('dashboard.voltageOffset')}
                value={voltageOffset}
                min={0}
                max={150}
                unit=" mV"
                onChange={setVoltageOffset}
              />

              {/* Throttle and Recovery Inputs */}
              <div className="grid grid-cols-2 gap-3 mt-1">
                <div className="flex flex-col gap-1.5">
                  <label htmlFor="throttle-temp-input" className="text-xs text-zinc-400 font-medium">
                    {t('dashboard.throttleTemp')}
                  </label>
                  <input
                    id="throttle-temp-input"
                    type="number"
                    value={throttleTemp}
                    onChange={(e) => setThrottleTemp(e.target.value)}
                    className="bg-[#242429] border border-white/5 rounded-lg px-3 py-1.5 text-sm text-white font-medium focus:outline-none focus:border-[#f25056] focus:ring-1 focus:ring-[#f25056] transition-all"
                  />
                </div>

                <div className="flex flex-col gap-1.5">
                  <label htmlFor="recovery-temp-input" className="text-xs text-zinc-400 font-medium">
                    {t('dashboard.recoveryTemp')}
                  </label>
                  <input
                    id="recovery-temp-input"
                    type="number"
                    value={recoveryTemp}
                    onChange={(e) => setRecoveryTemp(e.target.value)}
                    className="bg-[#242429] border border-white/5 rounded-lg px-3 py-1.5 text-sm text-white font-medium focus:outline-none focus:border-[#f25056] focus:ring-1 focus:ring-[#f25056] transition-all"
                  />
                </div>
              </div>
            </div>
          </div>

          {/* Card: Advanced CPU/GPU Unlocker */}
          <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex flex-col gap-3.5">
            <div className="flex items-center justify-between">
              <h2 className="text-sm font-semibold text-zinc-100">
                {t('dashboard.advUnlocker')}
              </h2>
              <button
                type="button"
                onClick={() => onNavigate('unlocker')}
                aria-label="Ver mais opções de desbloqueio avançado"
                className="text-zinc-500 hover:text-zinc-300 transition-colors p-1"
              >
                <MoreHorizontal className="w-4 h-4" />
              </button>
            </div>

            <div className="flex flex-col gap-2.5">
              {/* CPU Core Unlock Toggle */}
              <FluentSwitch
                checked={cpuUnlocked}
                onChange={setCpuUnlocked}
                title={t('dashboard.cpuUnlock')}
                subtitle={cpuUnlocked ? t('dashboard.cpuUnlockDesc') : '6 Cores / 12 Threads'}
                secondarySubtitle={cpuUnlocked ? t('dashboard.cpuUnlockSub') : undefined}
                hasWarning={true}
                onWarningClick={() => onNavigate('unlocker')}
              />

              {/* GPU Compute Unit (CU) Unlock Toggle */}
              <FluentSwitch
                checked={gpuUnlocked}
                onChange={setGpuUnlocked}
                title={t('dashboard.gpuUnlock')}
                subtitle={gpuUnlocked ? t('dashboard.gpuUnlockDesc') : 'Active: 18 CU'}
                secondarySubtitle={gpuUnlocked ? t('dashboard.gpuUnlockSub') : undefined}
              />
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

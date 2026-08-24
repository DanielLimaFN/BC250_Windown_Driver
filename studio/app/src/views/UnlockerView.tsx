import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Unlock, Cpu, Zap, AlertTriangle, ShieldCheck, CheckCircle2 } from 'lucide-react';
import { FluentSwitch } from '../components/FluentSwitch';

export const UnlockerView: React.FC = () => {
  const { t } = useTranslation();

  const [cpuUnlocked, setCpuUnlocked] = useState(true);
  const [gpuUnlocked, setGpuUnlocked] = useState(true);
  const [flashing, setFlashing] = useState(false);
  const [statusMessage, setStatusMessage] = useState<string | null>(null);

  const handleApplyUnlock = () => {
    setFlashing(true);
    setStatusMessage('Escrevendo registradores SMN e remapeando fuses...');
    setTimeout(() => {
      setFlashing(false);
      setStatusMessage('Fuses aplicados com sucesso! Estado ativo confirmado.');
      setTimeout(() => setStatusMessage(null), 3000);
    }, 1800);
  };

  return (
    <div className="flex-1 overflow-y-auto p-4 md:p-6 flex flex-col gap-5 text-fluent-text z-10">
      {/* Header */}
      <div>
        <h1 className="text-2xl font-bold tracking-tight text-white">
          {t('unlocker.title')}
        </h1>
        <p className="text-xs text-zinc-400 mt-0.5">
          {t('unlocker.subtitle')}
        </p>
      </div>

      {/* Warning Box */}
      <div className="bg-amber-500/10 border border-amber-500/30 rounded-xl p-4 flex items-start gap-3">
        <AlertTriangle className="w-5 h-5 text-amber-400 flex-shrink-0 mt-0.5" />
        <div className="flex flex-col gap-1">
          <span className="text-xs font-bold text-amber-300">
            {t('unlocker.safetyWarningTitle')}
          </span>
          <span className="text-xs text-amber-200/80 leading-relaxed">
            {t('unlocker.safetyWarningBody')}
          </span>
        </div>
      </div>

      {/* Unlocker Cards Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* CPU Unlock Card */}
        <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-5 shadow-fluent-card backdrop-blur-md flex flex-col justify-between gap-4">
          <div className="flex flex-col gap-3">
            <div className="flex items-center gap-2.5">
              <Cpu className="w-5 h-5 text-[#f25056]" />
              <h2 className="text-sm font-semibold text-white">
                {t('unlocker.cpuSection')}
              </h2>
            </div>

            <p className="text-xs text-zinc-400 leading-relaxed">
              {t('unlocker.cpuNote')}
            </p>

            {/* Core Topology Grid Visual */}
            <div className="grid grid-cols-4 gap-2 my-2">
              {[1, 2, 3, 4, 5, 6, 7, 8].map((core) => {
                const isExtra = core > 6;
                const active = !isExtra || cpuUnlocked;
                return (
                  <div
                    key={core}
                    className={`p-2 rounded border text-center transition-all ${
                      active
                        ? isExtra
                          ? 'bg-[#f25056]/20 border-[#f25056] text-white'
                          : 'bg-[#242429] border-white/10 text-zinc-200'
                        : 'bg-[#141416] border-white/5 text-zinc-600'
                    }`}
                  >
                    <span className="text-[10px] block font-medium">Core {core - 1}</span>
                    <span className="text-xs font-bold">
                      {active ? '2T' : 'OFF'}
                    </span>
                  </div>
                );
              })}
            </div>
          </div>

          <FluentSwitch
            checked={cpuUnlocked}
            onChange={setCpuUnlocked}
            title={t('dashboard.cpuUnlock')}
            subtitle={cpuUnlocked ? t('unlocker.cpuStatusUnlocked') : t('unlocker.cpuStatusLocked')}
            secondarySubtitle="Zen 2 7nm"
          />
        </div>

        {/* GPU Unlock Card */}
        <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-5 shadow-fluent-card backdrop-blur-md flex flex-col justify-between gap-4">
          <div className="flex flex-col gap-3">
            <div className="flex items-center gap-2.5">
              <Zap className="w-5 h-5 text-[#f25056]" />
              <h2 className="text-sm font-semibold text-white">
                {t('unlocker.gpuSection')}
              </h2>
            </div>

            <p className="text-xs text-zinc-400 leading-relaxed">
              {t('unlocker.gpuNote')}
            </p>

            {/* CU Visualizer Array (24 CUs) */}
            <div className="grid grid-cols-6 gap-1.5 my-2">
              {Array.from({ length: 24 }).map((_, idx) => {
                const isExtra = idx >= 18;
                const active = !isExtra || gpuUnlocked;
                return (
                  <div
                    key={idx}
                    className={`py-1.5 px-1 rounded text-center border text-[10px] font-semibold transition-all ${
                      active
                        ? isExtra
                          ? 'bg-[#f25056]/20 border-[#f25056] text-white shadow-[0_0_6px_rgba(242,80,86,0.4)]'
                          : 'bg-[#242429] border-white/10 text-zinc-300'
                        : 'bg-[#141416] border-white/5 text-zinc-600 line-through'
                    }`}
                  >
                    CU {idx}
                  </div>
                );
              })}
            </div>
          </div>

          <FluentSwitch
            checked={gpuUnlocked}
            onChange={setGpuUnlocked}
            title={t('dashboard.gpuUnlock')}
            subtitle={gpuUnlocked ? t('unlocker.gpuStatusUnlocked') : t('unlocker.gpuStatusLocked')}
            secondarySubtitle="RDNA2 Skillfish"
          />
        </div>
      </div>

      {/* Status Registers & Action Card */}
      <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex flex-col sm:flex-row items-center justify-between gap-4">
        <div className="flex items-center gap-3">
          <div className="w-8 h-8 rounded-full bg-emerald-500/15 border border-emerald-500/30 flex items-center justify-center text-emerald-400">
            <ShieldCheck className="w-4 h-4" />
          </div>
          <div className="flex flex-col">
            <span className="text-xs font-semibold text-white">
              {t('unlocker.fuseStatus')}: 0x000000FF (Unlocked)
            </span>
            <span className="text-[11px] text-zinc-400">
              {t('unlocker.smnStatus')} • SMU Msg Queue #0 Ready
            </span>
          </div>
        </div>

        <div className="flex items-center gap-3">
          {statusMessage && (
            <span className="text-xs text-emerald-400 font-medium flex items-center gap-1">
              <CheckCircle2 className="w-3.5 h-3.5" />
              {statusMessage}
            </span>
          )}

          <button
            type="button"
            onClick={handleApplyUnlock}
            disabled={flashing}
            className="px-5 py-2.5 rounded-lg bg-[#f25056] hover:bg-[#ef383f] disabled:opacity-50 text-xs font-semibold text-white shadow-[0_0_15px_rgba(242,80,86,0.5)] transition-all active:scale-[0.98]"
          >
            {flashing ? 'Gravando SMN...' : t('unlocker.toggleUnlock')}
          </button>
        </div>
      </div>
    </div>
  );
};

import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Gauge, Zap, Wind, Check, RotateCcw } from 'lucide-react';
import { SingleSlider, DualRangeSlider } from '../components/FluentSlider';

export const PerformanceView: React.FC = () => {
  const { t } = useTranslation();

  const [selectedProfile, setSelectedProfile] = useState<'silent' | 'balanced' | 'gaming' | 'extreme'>('gaming');
  const [minClock, setMinClock] = useState(1800);
  const [maxClock, setMaxClock] = useState(2050);
  const [socVoltage, setSocVoltage] = useState(1150);
  const [powerLimit, setPowerLimit] = useState(185);
  const [fanSpeed, setFanSpeed] = useState(75);
  const [fanAuto, setFanAuto] = useState(true);
  const [applied, setApplied] = useState(false);

  const applyProfile = (profile: 'silent' | 'balanced' | 'gaming' | 'extreme') => {
    setSelectedProfile(profile);
    if (profile === 'silent') {
      setMinClock(1200);
      setMaxClock(1600);
      setSocVoltage(950);
      setPowerLimit(120);
      setFanSpeed(45);
    } else if (profile === 'balanced') {
      setMinClock(1500);
      setMaxClock(1850);
      setSocVoltage(1050);
      setPowerLimit(150);
      setFanSpeed(60);
    } else if (profile === 'gaming') {
      setMinClock(1800);
      setMaxClock(2050);
      setSocVoltage(1150);
      setPowerLimit(185);
      setFanSpeed(75);
    } else if (profile === 'extreme') {
      setMinClock(2000);
      setMaxClock(2250);
      setSocVoltage(1225);
      setPowerLimit(220);
      setFanSpeed(95);
    }
  };

  const handleApply = () => {
    setApplied(true);
    setTimeout(() => setApplied(false), 2500);
  };

  const handleReset = () => {
    applyProfile('balanced');
  };

  return (
    <div className="z-10 flex flex-1 flex-col gap-5 overflow-y-auto p-5 md:p-7">
      <section className="page-hero">
        <p className="page-kicker">Perfis e Ajustes</p>
        <div className="mt-2 flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
          <div className="max-w-2xl">
            <h1 className="page-title">{t('performance.title')}</h1>
            <p className="mt-3 text-sm text-[var(--text-muted)]">
              {t('performance.subtitle')}
            </p>
          </div>
          <div className="flex flex-wrap gap-2">
            <span className="toolbar-chip">Perfil ativo: {t('performance.profileGaming')}</span>
            <span className="toolbar-chip">Fan {fanSpeed}%</span>
          </div>
        </div>
      </section>

      <div className="surface-card flex flex-col gap-3 p-5">
        <span className="text-xs font-semibold text-[var(--text-base)]">
          {t('performance.profiles')}
        </span>

        <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
          {[
            { id: 'silent' as const, label: t('performance.profileSilent'), icon: Wind, desc: '1600 MHz / 120W' },
            { id: 'balanced' as const, label: t('performance.profileBalanced'), icon: Gauge, desc: '1850 MHz / 150W' },
            { id: 'gaming' as const, label: t('performance.profileGaming'), icon: Zap, desc: '2050 MHz / 185W' },
            { id: 'extreme' as const, label: t('performance.profileExtreme'), icon: Zap, desc: '2250 MHz / 220W' },
          ].map((prof) => {
            const Icon = prof.icon;
            const isSelected = selectedProfile === prof.id;
            return (
              <button
                key={prof.id}
                type="button"
                onClick={() => applyProfile(prof.id)}
                className={`p-3.5 rounded-lg border flex flex-col items-start gap-1.5 transition-all text-left ${
                  isSelected
                    ? 'bg-[var(--accent-soft)] border-[var(--accent-line)] shadow-[var(--shadow-soft)]'
                    : 'bg-white border-[var(--border-soft)] hover:border-[var(--border-strong)]'
                }`}
              >
                <div className="flex items-center justify-between w-full">
                  <Icon className={`w-4 h-4 ${isSelected ? 'text-[var(--accent)]' : 'text-[var(--text-muted)]'}`} />
                  {isSelected && <Check className="w-3.5 h-3.5 text-[var(--accent)]" />}
                </div>
                <span className="mt-1 text-sm font-semibold text-[var(--text-strong)]">
                  {prof.label}
                </span>
                <span className="text-[11px] text-[var(--text-muted)]">
                  {prof.desc}
                </span>
              </button>
            );
          })}
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <div className="surface-card flex flex-col gap-4 p-5">
          <h2 className="section-title flex items-center gap-2">
            <Gauge className="w-4 h-4 text-[var(--accent)]" />
            {t('performance.clocks')}
          </h2>

          <DualRangeSlider
            label={t('dashboard.coreFreq')}
            minVal={minClock}
            maxVal={maxClock}
            min={1000}
            max={2400}
            unit="MHz"
            onChange={(min, max) => {
              setMinClock(min);
              setMaxClock(max);
            }}
          />

          <SingleSlider
            label={t('performance.socVoltage')}
            value={socVoltage}
            min={800}
            max={1300}
            step={25}
            unit=" mV"
            onChange={setSocVoltage}
          />

          <SingleSlider
            label={t('performance.powerLimit')}
            value={powerLimit}
            min={100}
            max={250}
            step={5}
            unit=" W"
            onChange={setPowerLimit}
          />
        </div>

        <div className="surface-card flex flex-col gap-4 p-5">
          <h2 className="section-title flex items-center gap-2">
            <Wind className="w-4 h-4 text-[var(--accent)]" />
            {t('performance.fanControl')}
          </h2>

          <div className="flex gap-2">
            <button
              type="button"
              onClick={() => setFanAuto(true)}
              className={`flex-1 py-2 px-3 rounded-lg text-xs font-medium border transition-all ${
                fanAuto
                  ? 'bg-[var(--accent-soft)] border-[var(--accent-line)] text-[var(--accent)]'
                  : 'bg-white border-[var(--border-soft)] text-[var(--text-muted)] hover:text-[var(--text-strong)]'
              }`}
            >
              {t('performance.fanAuto')}
            </button>
            <button
              type="button"
              onClick={() => setFanAuto(false)}
              className={`flex-1 py-2 px-3 rounded-lg text-xs font-medium border transition-all ${
                !fanAuto
                  ? 'bg-[var(--accent-soft)] border-[var(--accent-line)] text-[var(--accent)]'
                  : 'bg-white border-[var(--border-soft)] text-[var(--text-muted)] hover:text-[var(--text-strong)]'
              }`}
            >
              {t('performance.fanManual')}
            </button>
          </div>

          <SingleSlider
            label={t('performance.fanSpeed')}
            value={fanSpeed}
            min={20}
            max={100}
            unit="%"
            onChange={setFanSpeed}
          />

          <div className="surface-subtle flex flex-col gap-1 p-3 text-xs text-[var(--text-muted)]">
            <div className="flex justify-between">
              <span>Target Junction Temp:</span>
              <span className="font-semibold text-[var(--text-strong)]">82°C</span>
            </div>
            <div className="flex justify-between">
              <span>Acoustic Acoustic Limit:</span>
              <span className="font-semibold text-[var(--text-strong)]">2800 RPM</span>
            </div>
          </div>
        </div>
      </div>

      <div className="flex items-center justify-between pt-2">
        <button
          type="button"
          onClick={handleReset}
          className="button-secondary flex items-center gap-2"
        >
          <RotateCcw className="w-3.5 h-3.5" />
          {t('performance.revert')}
        </button>

        <div className="flex items-center gap-3">
          {applied && (
            <span className="flex items-center gap-1 text-xs font-medium text-emerald-600">
              <Check className="w-4 h-4" />
              {t('performance.appliedSuccess')}
            </span>
          )}
          <button
            type="button"
            onClick={handleApply}
            className="button-primary flex items-center gap-2 px-6"
          >
            {t('performance.apply')}
          </button>
        </div>
      </div>
    </div>
  );
};

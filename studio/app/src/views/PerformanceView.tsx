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
    <div className="flex-1 overflow-y-auto p-4 md:p-6 flex flex-col gap-5 text-fluent-text z-10">
      {/* Header */}
      <div>
        <h1 className="text-2xl font-bold tracking-tight text-white">
          {t('performance.title')}
        </h1>
        <p className="text-xs text-zinc-400 mt-0.5">
          {t('performance.subtitle')}
        </p>
      </div>

      {/* Profiles Grid */}
      <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex flex-col gap-3">
        <span className="text-xs font-semibold text-zinc-300">
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
                    ? 'bg-[#f25056]/15 border-[#f25056] shadow-[0_0_12px_rgba(242,80,86,0.3)]'
                    : 'bg-[#242429] border-white/5 hover:border-white/10'
                }`}
              >
                <div className="flex items-center justify-between w-full">
                  <Icon className={`w-4 h-4 ${isSelected ? 'text-[#f25056]' : 'text-zinc-400'}`} />
                  {isSelected && <Check className="w-3.5 h-3.5 text-[#f25056]" />}
                </div>
                <span className="text-sm font-semibold text-white mt-1">
                  {prof.label}
                </span>
                <span className="text-[11px] text-zinc-400">
                  {prof.desc}
                </span>
              </button>
            );
          })}
        </div>
      </div>

      {/* Manual Fine Tuning Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* Frequencies and Power */}
        <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex flex-col gap-4">
          <h2 className="text-sm font-semibold text-zinc-100 flex items-center gap-2">
            <Gauge className="w-4 h-4 text-[#f25056]" />
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

        {/* Fan Curve / Thermal Control */}
        <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex flex-col gap-4">
          <h2 className="text-sm font-semibold text-zinc-100 flex items-center gap-2">
            <Wind className="w-4 h-4 text-[#f25056]" />
            {t('performance.fanControl')}
          </h2>

          <div className="flex gap-2">
            <button
              type="button"
              onClick={() => setFanAuto(true)}
              className={`flex-1 py-2 px-3 rounded-lg text-xs font-medium border transition-all ${
                fanAuto
                  ? 'bg-[#f25056]/15 border-[#f25056] text-white'
                  : 'bg-[#242429] border-white/5 text-zinc-400 hover:text-white'
              }`}
            >
              {t('performance.fanAuto')}
            </button>
            <button
              type="button"
              onClick={() => setFanAuto(false)}
              className={`flex-1 py-2 px-3 rounded-lg text-xs font-medium border transition-all ${
                !fanAuto
                  ? 'bg-[#f25056]/15 border-[#f25056] text-white'
                  : 'bg-[#242429] border-white/5 text-zinc-400 hover:text-white'
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

          <div className="p-3 bg-[#151518] rounded-lg border border-white/5 text-xs text-zinc-400 flex flex-col gap-1">
            <div className="flex justify-between">
              <span>Target Junction Temp:</span>
              <span className="text-zinc-200 font-semibold">82°C</span>
            </div>
            <div className="flex justify-between">
              <span>Acoustic Acoustic Limit:</span>
              <span className="text-zinc-200 font-semibold">2800 RPM</span>
            </div>
          </div>
        </div>
      </div>

      {/* Action Footer */}
      <div className="flex items-center justify-between pt-2">
        <button
          type="button"
          onClick={handleReset}
          className="flex items-center gap-2 px-4 py-2 rounded-lg bg-[#242429] hover:bg-[#2e2e36] text-xs font-medium text-zinc-300 transition-all border border-white/5"
        >
          <RotateCcw className="w-3.5 h-3.5" />
          {t('performance.revert')}
        </button>

        <div className="flex items-center gap-3">
          {applied && (
            <span className="text-xs text-emerald-400 flex items-center gap-1 font-medium">
              <Check className="w-4 h-4" />
              {t('performance.appliedSuccess')}
            </span>
          )}
          <button
            type="button"
            onClick={handleApply}
            className="flex items-center gap-2 px-6 py-2.5 rounded-lg bg-[#f25056] hover:bg-[#ef383f] text-xs font-semibold text-white shadow-[0_0_15px_rgba(242,80,86,0.5)] transition-all active:scale-[0.98]"
          >
            {t('performance.apply')}
          </button>
        </div>
      </div>
    </div>
  );
};

import React from 'react';

interface SingleSliderProps {
  label: string;
  value: number;
  min: number;
  max: number;
  unit?: string;
  step?: number;
  onChange: (val: number) => void;
  showLimits?: boolean;
}

export const SingleSlider: React.FC<SingleSliderProps> = ({
  label,
  value,
  min,
  max,
  unit = '',
  step = 1,
  onChange,
  showLimits = true
}) => {
  const percentage = ((value - min) / (max - min)) * 100;

  return (
    <div className="flex flex-col gap-1.5 w-full select-none">
      <div className="flex justify-between items-center text-xs">
        <span className="font-medium text-[var(--text-base)]">{label}</span>
        <span className="rounded-full border border-[var(--border-soft)] bg-white px-2.5 py-1 font-semibold text-[var(--text-strong)] shadow-[var(--shadow-soft)]">
          {value}{unit}
        </span>
      </div>

      <div className="relative flex items-center w-full h-6">
        <div className="absolute w-full h-1.5 rounded-full bg-[rgba(111,123,140,0.18)]" />
        
        <div
          className="absolute h-1.5 rounded-full bg-[var(--accent)] shadow-[0_0_0_3px_rgba(194,65,74,0.12)]"
          style={{ width: `${percentage}%` }}
        />

        <input
          type="range"
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(e) => onChange(Number(e.target.value))}
          className="absolute w-full h-6 opacity-0 cursor-pointer z-10"
        />

        <div
          className="pointer-events-none absolute h-4 w-4 -translate-x-1/2 rounded-full border-[3px] border-[var(--accent)] bg-white shadow-[0_0_0_3px_rgba(194,65,74,0.12)] transition-transform"
          style={{ left: `${percentage}%` }}
        />
      </div>

      {showLimits && (
        <div className="flex justify-between text-[11px] font-medium text-[var(--text-soft)]">
          <span>{min}</span>
          <span>{max}</span>
        </div>
      )}
    </div>
  );
};

interface DualRangeSliderProps {
  label: string;
  minVal: number;
  maxVal: number;
  min: number;
  max: number;
  unit?: string;
  onChange: (min: number, max: number) => void;
}

export const DualRangeSlider: React.FC<DualRangeSliderProps> = ({
  label,
  minVal,
  maxVal,
  min,
  max,
  unit = '',
  onChange
}) => {
  const minPercent = ((minVal - min) / (max - min)) * 100;
  const maxPercent = ((maxVal - min) / (max - min)) * 100;

  const handleMinChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const val = Math.min(Number(e.target.value), maxVal - 50);
    onChange(val, maxVal);
  };

  const handleMaxChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const val = Math.max(Number(e.target.value), minVal + 50);
    onChange(minVal, val);
  };

  return (
    <div className="flex flex-col gap-1.5 w-full select-none">
      <div className="flex justify-between items-center text-xs">
        <span className="font-medium text-[var(--text-base)]">{label}</span>
        <div className="flex items-center gap-1">
          <span className="rounded-full border border-[var(--border-soft)] bg-white px-2.5 py-1 font-semibold text-[var(--text-strong)] shadow-[var(--shadow-soft)]">
            {minVal}
          </span>
          <span className="text-[var(--text-soft)]">-</span>
          <span className="rounded-full border border-[var(--border-soft)] bg-white px-2.5 py-1 font-semibold text-[var(--text-strong)] shadow-[var(--shadow-soft)]">
            {maxVal} {unit}
          </span>
        </div>
      </div>

      <div className="relative flex items-center w-full h-6">
        <div className="absolute w-full h-1.5 rounded-full bg-[rgba(111,123,140,0.18)]" />

        <div
          className="absolute h-1.5 rounded-full bg-[var(--accent)] shadow-[0_0_0_3px_rgba(194,65,74,0.12)]"
          style={{
            left: `${minPercent}%`,
            width: `${maxPercent - minPercent}%`,
          }}
        />

        <input
          type="range"
          min={min}
          max={max}
          step={25}
          value={minVal}
          onChange={handleMinChange}
          className="absolute w-full h-6 opacity-0 cursor-pointer z-20 pointer-events-auto"
        />
        <input
          type="range"
          min={min}
          max={max}
          step={25}
          value={maxVal}
          onChange={handleMaxChange}
          className="absolute w-full h-6 opacity-0 cursor-pointer z-30 pointer-events-auto"
        />

        <div
          className="pointer-events-none absolute h-4 w-4 -translate-x-1/2 rounded-full border-[3px] border-[var(--accent)] bg-white shadow-[0_0_0_3px_rgba(194,65,74,0.12)]"
          style={{ left: `${minPercent}%` }}
        />
        <div
          className="pointer-events-none absolute h-4 w-4 -translate-x-1/2 rounded-full border-[3px] border-[var(--accent)] bg-white shadow-[0_0_0_3px_rgba(194,65,74,0.12)]"
          style={{ left: `${maxPercent}%` }}
        />
      </div>

      <div className="flex justify-between text-[11px] font-medium text-[var(--text-soft)]">
        <span>{min}</span>
        <span>{max}</span>
      </div>
    </div>
  );
};

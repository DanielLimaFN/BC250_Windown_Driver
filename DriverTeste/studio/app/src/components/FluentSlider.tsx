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
        <span className="text-zinc-300 font-medium">{label}</span>
        <span className="bg-[#242429] px-2 py-0.5 rounded text-white font-semibold border border-white/5 shadow-inner">
          {value}{unit}
        </span>
      </div>

      <div className="relative flex items-center w-full h-6">
        {/* Track Background */}
        <div className="absolute w-full h-1.5 bg-white/10 rounded-full" />
        
        {/* Filled Active Track */}
        <div
          className="absolute h-1.5 bg-[#f25056] rounded-full shadow-[0_0_8px_rgba(242,80,86,0.6)]"
          style={{ width: `${percentage}%` }}
        />

        {/* Input */}
        <input
          type="range"
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(e) => onChange(Number(e.target.value))}
          className="absolute w-full h-6 opacity-0 cursor-pointer z-10"
        />

        {/* Thumb Visual */}
        <div
          className="absolute w-4 h-4 bg-[#f25056] border-2 border-white rounded-full shadow-[0_0_10px_rgba(242,80,86,0.9)] pointer-events-none transform -translate-x-1/2 transition-transform hover:scale-110"
          style={{ left: `${percentage}%` }}
        />
      </div>

      {showLimits && (
        <div className="flex justify-between text-[11px] text-zinc-500 font-medium">
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
        <span className="text-zinc-300 font-medium">{label}</span>
        <div className="flex items-center gap-1">
          <span className="bg-[#242429] px-2 py-0.5 rounded text-white font-semibold border border-white/5">
            {minVal}
          </span>
          <span className="text-zinc-500">-</span>
          <span className="bg-[#242429] px-2 py-0.5 rounded text-white font-semibold border border-white/5">
            {maxVal} {unit}
          </span>
        </div>
      </div>

      <div className="relative flex items-center w-full h-6">
        {/* Track Background */}
        <div className="absolute w-full h-1.5 bg-white/10 rounded-full" />

        {/* Highlighted Selected Range */}
        <div
          className="absolute h-1.5 bg-[#f25056] rounded-full shadow-[0_0_8px_rgba(242,80,86,0.6)]"
          style={{
            left: `${minPercent}%`,
            width: `${maxPercent - minPercent}%`,
          }}
        />

        {/* Native Inputs */}
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

        {/* Visual Thumbs */}
        <div
          className="absolute w-4 h-4 bg-[#f25056] border-2 border-white rounded-full shadow-[0_0_10px_rgba(242,80,86,0.9)] pointer-events-none transform -translate-x-1/2"
          style={{ left: `${minPercent}%` }}
        />
        <div
          className="absolute w-4 h-4 bg-[#f25056] border-2 border-white rounded-full shadow-[0_0_10px_rgba(242,80,86,0.9)] pointer-events-none transform -translate-x-1/2"
          style={{ left: `${maxPercent}%` }}
        />
      </div>

      <div className="flex justify-between text-[11px] text-zinc-500 font-medium">
        <span>{min}</span>
        <span>{max}</span>
      </div>
    </div>
  );
};

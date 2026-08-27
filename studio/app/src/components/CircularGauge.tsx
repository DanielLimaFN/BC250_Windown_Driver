import React from 'react';

interface CircularGaugeProps {
  value: number;
  min?: number;
  max?: number;
  unit?: string;
  subValue?: string;
  label: string;
  size?: number;
  strokeWidth?: number;
  color?: string;
  glow?: boolean;
}

export const CircularGauge: React.FC<CircularGaugeProps> = ({
  value,
  min = 0,
  max = 100,
  unit = '',
  subValue,
  label,
  size = 110,
  strokeWidth = 8,
  color = '#f25056',
  glow = true
}) => {
  const radius = (size - strokeWidth * 2) / 2;
  const circumference = 2 * Math.PI * radius;
  
  // We use a 270 degree arc (3/4 of a circle) with gap at the bottom
  const arcPercentage = 0.75;
  const totalArcLength = circumference * arcPercentage;
  
  const normalizedValue = Math.min(Math.max((value - min) / (max - min), 0), 1);
  const strokeDashoffset = totalArcLength - (normalizedValue * totalArcLength);

  return (
    <div className="flex flex-col items-center justify-center select-none group">
      <div
        className="relative flex items-center justify-center rounded-full border border-[var(--border-soft)] bg-white/60 shadow-[var(--shadow-soft)]"
        style={{ width: size, height: size }}
      >
        <svg
          width={size}
          height={size}
          viewBox={`0 0 ${size} ${size}`}
          className="transform rotate-[135deg] overflow-visible"
        >
          {/* Subtle Glow Filter */}
          <defs>
            <filter id={`glow-${label.replace(/\s+/g, '-')}`} x="-30%" y="-30%" width="160%" height="160%">
              <feGaussianBlur stdDeviation="3.5" result="blur" />
              <feMerge>
                <feMergeNode in="blur" />
                <feMergeNode in="SourceGraphic" />
              </feMerge>
            </filter>
            <linearGradient id={`grad-${label.replace(/\s+/g, '-')}`} x1="0%" y1="0%" x2="100%" y2="100%">
              <stop offset="0%" stopColor="#f87171" />
              <stop offset="100%" stopColor="#ef4444" />
            </linearGradient>
          </defs>

          {/* Background Track Arc */}
          <circle
            cx={size / 2}
            cy={size / 2}
            r={radius}
            fill="none"
            stroke="rgba(111, 123, 140, 0.14)"
            strokeWidth={strokeWidth}
            strokeDasharray={`${totalArcLength} ${circumference}`}
            strokeLinecap="round"
          />

          {/* Value Progress Arc */}
          <circle
            cx={size / 2}
            cy={size / 2}
            r={radius}
            fill="none"
            stroke={`url(#grad-${label.replace(/\s+/g, '-')})`}
            strokeWidth={strokeWidth}
            strokeDasharray={`${totalArcLength} ${circumference}`}
            strokeDashoffset={strokeDashoffset}
            strokeLinecap="round"
            filter={glow ? `url(#glow-${label.replace(/\s+/g, '-')})` : undefined}
            className="transition-all duration-500 ease-out"
          />
        </svg>

        {/* Center Content */}
        <div className="absolute inset-0 flex flex-col items-center justify-center pointer-events-none text-center">
          <span className="text-xl font-bold tracking-tight text-[var(--text-strong)] transition-all duration-300">
            {value}{unit}
          </span>
          {subValue && (
            <span className="text-[11px] font-medium -mt-0.5 text-[var(--text-muted)]">
              {subValue}
            </span>
          )}
        </div>
      </div>

      {/* Underneath Label */}
      <span className="mt-2 text-xs font-medium tracking-wide text-[var(--text-muted)]">
        {label}
      </span>
    </div>
  );
};

import React from 'react';

interface HistoryGraphProps {
  title: string;
  data: number[];
  min?: number;
  max?: number;
  unit?: string;
  isStep?: boolean;
  currentValue?: number;
}

export const HistoryGraph: React.FC<HistoryGraphProps> = ({
  title,
  data,
  min = 0,
  max = 100,
  unit = '',
  isStep = false,
  currentValue
}) => {
  const width = 240;
  const height = 65;
  const padding = 6;

  // Generate SVG path from data
  const points = data.map((val, idx) => {
    const x = padding + (idx / (data.length - 1 || 1)) * (width - padding * 2);
    const normalizedY = Math.min(Math.max((val - min) / (max - min), 0), 1);
    const y = height - padding - normalizedY * (height - padding * 2);
    return { x, y };
  });

  let pathD = '';
  if (points.length > 0) {
    if (isStep) {
      pathD = `M ${points[0].x} ${points[0].y}`;
      for (let i = 1; i < points.length; i++) {
        pathD += ` H ${points[i].x} V ${points[i].y}`;
      }
    } else {
      pathD = `M ${points[0].x} ${points[0].y}`;
      for (let i = 1; i < points.length; i++) {
        const prev = points[i - 1];
        const curr = points[i];
        const cx = (prev.x + curr.x) / 2;
        pathD += ` C ${cx} ${prev.y}, ${cx} ${curr.y}, ${curr.x} ${curr.y}`;
      }
    }
  }

  // Area path for gradient background
  const areaD = points.length > 0
    ? `${pathD} L ${points[points.length - 1].x} ${height} L ${points[0].x} ${height} Z`
    : '';

  const id = `graph-${title.replace(/\s+/g, '-')}`;

  return (
    <div className="bg-[#151518]/70 border border-white/5 rounded-lg p-2.5 flex flex-col justify-between relative overflow-hidden backdrop-blur-sm">
      <div className="flex justify-between items-center mb-1">
        <span className="text-xs text-zinc-400 font-medium">{title}</span>
        {currentValue !== undefined && (
          <span className="text-xs font-semibold text-white">
            {currentValue}{unit}
          </span>
        )}
      </div>

      <div className="relative w-full h-[65px] flex items-end">
        <svg
          viewBox={`0 0 ${width} ${height}`}
          className="w-full h-full overflow-visible"
          preserveAspectRatio="none"
        >
          <defs>
            <linearGradient id={`${id}-area`} x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#f25056" stopOpacity="0.25" />
              <stop offset="100%" stopColor="#f25056" stopOpacity="0.0" />
            </linearGradient>
            <filter id={`${id}-glow`} x="-20%" y="-20%" width="140%" height="140%">
              <feGaussianBlur stdDeviation="2" result="blur" />
              <feMerge>
                <feMergeNode in="blur" />
                <feMergeNode in="SourceGraphic" />
              </feMerge>
            </filter>
          </defs>

          {/* Area under curve */}
          {areaD && (
            <path
              d={areaD}
              fill={`url(#${id}-area)`}
              className="transition-all duration-300 ease-out"
            />
          )}

          {/* Glowing Stroke Line */}
          {pathD && (
            <path
              d={pathD}
              fill="none"
              stroke="#f25056"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
              filter={`url(#${id}-glow)`}
              className="transition-all duration-300 ease-out"
            />
          )}
        </svg>
      </div>
    </div>
  );
};

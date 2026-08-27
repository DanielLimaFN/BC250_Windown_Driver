import React from 'react';
import { AlertTriangle } from 'lucide-react';

interface FluentSwitchProps {
  checked: boolean;
  onChange: (checked: boolean) => void;
  title: string;
  subtitle: string;
  secondarySubtitle?: string;
  hasWarning?: boolean;
  onWarningClick?: () => void;
}

export const FluentSwitch: React.FC<FluentSwitchProps> = ({
  checked,
  onChange,
  title,
  subtitle,
  secondarySubtitle,
  hasWarning = false,
  onWarningClick,
}) => {
  return (
    <div className="flex items-center justify-between p-3 rounded-lg bg-[#18181c]/80 border border-white/5 hover:border-white/10 transition-all">
      <div className="flex items-center gap-3.5">
        {/* Toggle Switch */}
        <button
          type="button"
          role="switch"
          aria-checked={checked}
          onClick={() => onChange(!checked)}
          className={`relative inline-flex h-6 w-11 flex-shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ease-in-out focus:outline-none ${
            checked
              ? 'bg-[#f25056] shadow-[0_0_12px_rgba(242,80,86,0.6)]'
              : 'bg-white/20 hover:bg-white/30'
          }`}
        >
          <span
            className={`pointer-events-none inline-block h-5 w-5 transform rounded-full bg-white shadow-md ring-0 transition duration-200 ease-in-out ${
              checked ? 'translate-x-5' : 'translate-x-0'
            }`}
          />
        </button>

        {/* Text Details */}
        <div className="flex flex-col">
          <span className="text-sm font-medium text-white">{title}</span>
          <div className="flex items-center gap-1.5 text-xs text-zinc-400">
            <span className={checked ? 'text-white font-medium' : 'text-zinc-400'}>
              {subtitle}
            </span>
            {secondarySubtitle && (
              <span className="text-zinc-500 font-normal">
                ({secondarySubtitle})
              </span>
            )}
          </div>
        </div>
      </div>

      {/* Warning Icon if present */}
      {hasWarning && (
        <button
          type="button"
          onClick={onWarningClick}
          className="text-amber-500 hover:text-amber-400 p-1 transition-colors"
          title="Critical safety notice"
        >
          <AlertTriangle className="w-4 h-4" />
        </button>
      )}
    </div>
  );
};

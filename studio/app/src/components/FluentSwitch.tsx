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
    <div className="flex items-center justify-between rounded-2xl border border-[var(--border-soft)] bg-white p-3.5 transition-all hover:border-[var(--border-strong)] hover:shadow-[var(--shadow-soft)]">
      <div className="flex items-center gap-3.5">
        <button
          type="button"
          role="switch"
          aria-checked={checked}
          onClick={() => onChange(!checked)}
          className={`relative inline-flex h-6 w-11 flex-shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ease-in-out focus:outline-none ${
            checked
              ? 'bg-[var(--accent)] shadow-[0_0_0_4px_rgba(194,65,74,0.12)]'
              : 'bg-[#cfd6df]'
          }`}
        >
          <span
            className={`pointer-events-none inline-block h-5 w-5 transform rounded-full bg-white shadow-md ring-0 transition duration-200 ease-in-out ${
              checked ? 'translate-x-5' : 'translate-x-0'
            }`}
          />
        </button>

        <div className="flex flex-col">
          <span className="text-sm font-medium text-[var(--text-strong)]">{title}</span>
          <div className="flex items-center gap-1.5 text-xs text-[var(--text-muted)]">
            <span className={checked ? 'font-medium text-[var(--text-strong)]' : 'text-[var(--text-muted)]'}>
              {subtitle}
            </span>
            {secondarySubtitle && (
              <span className="font-normal text-[var(--text-soft)]">
                ({secondarySubtitle})
              </span>
            )}
          </div>
        </div>
      </div>

      {hasWarning && (
        <button
          type="button"
          onClick={onWarningClick}
          className="p-1 text-[var(--warning)] transition-colors hover:opacity-80"
          title="Critical safety notice"
        >
          <AlertTriangle className="w-4 h-4" />
        </button>
      )}
    </div>
  );
};

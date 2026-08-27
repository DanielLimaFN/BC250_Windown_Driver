import React from 'react';
import { Minus, Square, X, Cpu, Globe, Heart } from 'lucide-react';
import { useTranslation } from 'react-i18next';

interface TitleBarProps {
  currentLanguage: string;
  currentViewLabel: string;
  onLanguageChange: (lang: string) => void;
  onOpenDonate?: () => void;
}

export const TitleBar: React.FC<TitleBarProps> = ({
  currentLanguage,
  currentViewLabel,
  onLanguageChange,
  onOpenDonate,
}) => {
  const { t } = useTranslation();

  const languages = [
    { code: 'pt', label: 'PT-BR 🇧🇷' },
    { code: 'en', label: 'EN 🇺🇸' },
    { code: 'ru', label: 'RU 🇷🇺' },
    { code: 'es', label: 'ES 🇪🇸' },
  ];

  const handleMinimize = () => {
    // @ts-ignore
    if (window.electronAPI) window.electronAPI.minimize();
  };

  const handleMaximize = () => {
    // @ts-ignore
    if (window.electronAPI) window.electronAPI.maximize();
  };

  const handleClose = () => {
    // @ts-ignore
    if (window.electronAPI) window.electronAPI.close();
  };

  return (
    <header className="z-50 flex h-14 w-full items-center justify-between border-b border-[var(--border-soft)] bg-[rgba(255,255,255,0.88)] px-4 select-none titlebar-drag">
      <div className="flex items-center gap-3">
        <div className="flex h-9 w-9 items-center justify-center rounded-2xl border border-[var(--accent-line)] bg-[var(--accent-soft)] text-[var(--accent)] shadow-[var(--shadow-soft)]">
          <Cpu className="h-4 w-4" />
        </div>
        <div className="flex flex-col">
          <span className="text-sm font-semibold tracking-tight text-[var(--text-strong)]">
            {t('app.title')}
          </span>
          <span className="text-[11px] text-[var(--text-muted)]">
            {currentViewLabel} • Studio Preview
          </span>
        </div>
      </div>

      <div className="flex items-center gap-2.5 titlebar-no-drag">
        <div className="toolbar-chip hidden md:flex">
          <span className="h-2 w-2 rounded-full bg-[var(--success)]" />
          Modo visual sem integração
        </div>

        {onOpenDonate && (
          <button
            type="button"
            onClick={onOpenDonate}
            className="inline-flex items-center gap-1.5 rounded-full border border-rose-200 bg-rose-50/90 px-3 py-1.5 text-[11px] font-semibold text-rose-600 shadow-2xs transition-all hover:bg-rose-100 hover:border-rose-300"
            title={t('donate.title', 'Apoiar o Desenvolvimento do Driver')}
          >
            <Heart className="h-3.5 w-3.5 fill-current text-rose-500 animate-pulse" />
            <span>{t('donate.buttonLabel', 'Doar')}</span>
          </button>
        )}

        <div className="toolbar-chip">
          <Globe className="h-3.5 w-3.5 text-[var(--text-muted)]" />
          <select
            value={currentLanguage}
            onChange={(e) => onLanguageChange(e.target.value)}
            aria-label="Selecionar Idioma"
            className="cursor-pointer bg-transparent pr-1 text-[11px] font-medium text-[var(--text-strong)] focus:outline-none"
          >
            {languages.map((lang) => (
              <option key={lang.code} value={lang.code} className="bg-white text-[var(--text-strong)]">
                {lang.label}
              </option>
            ))}
          </select>
        </div>

        <div className="flex items-center gap-1">
          <button
            type="button"
            aria-label="Minimizar"
            onClick={handleMinimize}
            className="flex h-9 w-9 items-center justify-center rounded-xl text-[var(--text-muted)] transition-colors hover:bg-[var(--surface-3)] hover:text-[var(--text-strong)]"
          >
            <Minus className="h-3.5 w-3.5" />
          </button>
          <button
            type="button"
            aria-label="Maximizar"
            onClick={handleMaximize}
            className="flex h-9 w-9 items-center justify-center rounded-xl text-[var(--text-muted)] transition-colors hover:bg-[var(--surface-3)] hover:text-[var(--text-strong)]"
          >
            <Square className="h-3 w-3" />
          </button>
          <button
            type="button"
            aria-label="Fechar"
            onClick={handleClose}
            className="flex h-9 w-9 items-center justify-center rounded-xl text-[var(--text-muted)] transition-colors hover:bg-[#e81123] hover:text-[#ffffff]"
          >
            <X className="h-3.5 w-3.5" />
          </button>
        </div>
      </div>
    </header>
  );
};

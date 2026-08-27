import React from 'react';
import { Minus, Square, X, Cpu, Globe } from 'lucide-react';
import { useTranslation } from 'react-i18next';

interface TitleBarProps {
  currentLanguage: string;
  onLanguageChange: (lang: string) => void;
}

export const TitleBar: React.FC<TitleBarProps> = ({
  currentLanguage,
  onLanguageChange,
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
    <header className="h-9 w-full bg-[#121215] border-b border-white/5 flex items-center justify-between px-3 select-none z-50">
      {/* App Icon & Title */}
      <div className="flex items-center gap-2.5">
        <div className="w-4 h-4 rounded bg-gradient-to-br from-[#f25056] to-[#b91c1c] flex items-center justify-center shadow-[0_0_8px_rgba(242,80,86,0.5)]">
          <Cpu className="w-3 h-3 text-white" />
        </div>
        <span className="text-xs font-semibold tracking-wide text-zinc-200">
          {t('app.title')}
        </span>
      </div>

      {/* Center/Right Controls: Quick Language Switcher + Window buttons */}
      <div className="flex items-center gap-2">
        {/* Quick Language Toggle */}
        <div className="flex items-center bg-[#1c1c20] border border-white/10 rounded px-1.5 py-0.5 gap-1">
          <Globe className="w-3 h-3 text-zinc-400" />
          <select
            value={currentLanguage}
            onChange={(e) => onLanguageChange(e.target.value)}
            aria-label="Selecionar Idioma"
            className="bg-transparent text-[11px] text-zinc-200 font-medium focus:outline-none cursor-pointer pr-1"
          >
            {languages.map((lang) => (
              <option key={lang.code} value={lang.code} className="bg-[#1c1c20] text-zinc-200">
                {lang.label}
              </option>
            ))}
          </select>
        </div>

        {/* Windows Standard Buttons */}
        <div className="flex items-center -mr-2">
          <button
            type="button"
            aria-label="Minimizar"
            onClick={handleMinimize}
            className="w-9 h-9 flex items-center justify-center text-zinc-400 hover:text-white hover:bg-white/10 transition-colors"
          >
            <Minus className="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            aria-label="Maximizar"
            onClick={handleMaximize}
            className="w-9 h-9 flex items-center justify-center text-zinc-400 hover:text-white hover:bg-white/10 transition-colors"
          >
            <Square className="w-3 h-3" />
          </button>
          <button
            type="button"
            aria-label="Fechar"
            onClick={handleClose}
            className="w-9 h-9 flex items-center justify-center text-zinc-400 hover:text-white hover:bg-[#e81123] transition-colors"
          >
            <X className="w-3.5 h-3.5" />
          </button>
        </div>
      </div>
    </header>
  );
};

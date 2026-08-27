import React from 'react';
import { useTranslation } from 'react-i18next';
import { Globe, Palette, Cpu, Check, Info } from 'lucide-react';
import { FluentSwitch } from '../components/FluentSwitch';

interface SettingsViewProps {
  currentLanguage: string;
  onLanguageChange: (lang: string) => void;
  pollingRate: number;
  onPollingRateChange: (rate: number) => void;
}

export const SettingsView: React.FC<SettingsViewProps> = ({
  currentLanguage,
  onLanguageChange,
  pollingRate,
  onPollingRateChange,
}) => {
  const { t } = useTranslation();

  const [startWithWindows, setStartWithWindows] = React.useState(true);
  const [minimizeToTray, setMinimizeToTray] = React.useState(true);
  const [glowEnabled, setGlowEnabled] = React.useState(true);
  const [savedNotice, setSavedNotice] = React.useState(false);

  const languages = [
    { code: 'pt', label: t('settings.langPt'), flag: '🇧🇷' },
    { code: 'en', label: t('settings.langEn'), flag: '🇺🇸' },
    { code: 'ru', label: t('settings.langRu'), flag: '🇷🇺' },
    { code: 'es', label: t('settings.langEs'), flag: '🇪🇸' },
  ];

  const handleSave = () => {
    setSavedNotice(true);
    setTimeout(() => setSavedNotice(false), 2000);
  };

  return (
    <div className="flex-1 overflow-y-auto p-4 md:p-6 flex flex-col gap-5 text-fluent-text z-10">
      {/* Header */}
      <div>
        <h1 className="text-2xl font-bold tracking-tight text-white">
          {t('settings.title')}
        </h1>
        <p className="text-xs text-zinc-400 mt-0.5">
          {t('settings.subtitle')}
        </p>
      </div>

      {/* Language Setting Card */}
      <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-5 shadow-fluent-card backdrop-blur-md flex flex-col gap-4">
        <div className="flex items-center gap-2.5">
          <Globe className="w-5 h-5 text-[#f25056]" />
          <h2 className="text-sm font-semibold text-white">
            {t('settings.language')}
          </h2>
        </div>

        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-3">
          {languages.map((lang) => {
            const isSelected = currentLanguage === lang.code;
            return (
              <button
                key={lang.code}
                type="button"
                onClick={() => onLanguageChange(lang.code)}
                className={`p-3.5 rounded-lg border flex items-center justify-between transition-all text-left ${
                  isSelected
                    ? 'bg-[#f25056]/15 border-[#f25056] shadow-[0_0_12px_rgba(242,80,86,0.3)]'
                    : 'bg-[#242429] border-white/5 hover:border-white/10'
                }`}
              >
                <div className="flex items-center gap-2.5">
                  <span className="text-xl">{lang.flag}</span>
                  <span className="text-xs font-semibold text-white">
                    {lang.label}
                  </span>
                </div>
                {isSelected && <Check className="w-4 h-4 text-[#f25056]" />}
              </button>
            );
          })}
        </div>
      </div>

      {/* Telemetry Polling Rate & Appearance */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* Telemetry Setting */}
        <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-5 shadow-fluent-card backdrop-blur-md flex flex-col justify-between gap-4">
          <div className="flex flex-col gap-3">
            <h2 className="text-sm font-semibold text-white flex items-center gap-2">
              <Cpu className="w-4 h-4 text-[#f25056]" />
              {t('settings.pollingRate')}
            </h2>

            <div className="flex flex-col gap-2">
              {[
                { rate: 100, label: t('settings.rateFast'), desc: 'Mais responsivo (10x/s)' },
                { rate: 500, label: t('settings.rateNormal'), desc: 'Equilíbrio ideal (2x/s)' },
                { rate: 1000, label: t('settings.rateSlow'), desc: 'Menor uso de CPU (1x/s)' },
              ].map((item) => {
                const isSelected = pollingRate === item.rate;
                return (
                  <button
                    key={item.rate}
                    type="button"
                    onClick={() => onPollingRateChange(item.rate)}
                    className={`p-2.5 rounded-lg border flex items-center justify-between text-xs transition-all ${
                      isSelected
                        ? 'bg-[#f25056]/15 border-[#f25056] text-white'
                        : 'bg-[#242429] border-white/5 text-zinc-300 hover:text-white'
                    }`}
                  >
                    <span className="font-semibold">{item.label}</span>
                    <span className="text-[11px] text-zinc-400">{item.desc}</span>
                  </button>
                );
              })}
            </div>
          </div>
        </div>

        {/* System & Startup */}
        <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-5 shadow-fluent-card backdrop-blur-md flex flex-col justify-between gap-3">
          <h2 className="text-sm font-semibold text-white flex items-center gap-2">
            <Palette className="w-4 h-4 text-[#f25056]" />
            {t('settings.system')}
          </h2>

          <div className="flex flex-col gap-2.5">
            <FluentSwitch
              checked={startWithWindows}
              onChange={setStartWithWindows}
              title={t('settings.startWithWindows')}
              subtitle="Executar em segundo plano"
            />
            <FluentSwitch
              checked={minimizeToTray}
              onChange={setMinimizeToTray}
              title={t('settings.minimizeToTray')}
              subtitle="Manter telemetria ativa"
            />
            <FluentSwitch
              checked={glowEnabled}
              onChange={setGlowEnabled}
              title={t('settings.glowEffects')}
              subtitle="Iluminação Fluent Crimson"
            />
          </div>
        </div>
      </div>

      {/* About Box */}
      <div className="bg-[#1c1c20]/80 border border-white/5 rounded-xl p-4 shadow-fluent-card backdrop-blur-md flex items-center justify-between">
        <div className="flex items-center gap-3">
          <div className="w-9 h-9 rounded-xl bg-[#242429] flex items-center justify-center text-zinc-400">
            <Info className="w-4 h-4 text-[#f25056]" />
          </div>
          <div className="flex flex-col">
            <span className="text-xs font-semibold text-white">
              {t('settings.about')}
            </span>
            <span className="text-[11px] text-zinc-400">
              {t('settings.version')} • {t('settings.author')}
            </span>
          </div>
        </div>

        <button
          type="button"
          onClick={handleSave}
          className="px-5 py-2 rounded-lg bg-[#f25056] hover:bg-[#ef383f] text-xs font-semibold text-white shadow-[0_0_12px_rgba(242,80,86,0.4)] transition-all active:scale-[0.98]"
        >
          {savedNotice ? 'Salvo!' : t('settings.save')}
        </button>
      </div>
    </div>
  );
};

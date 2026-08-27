import React from 'react';
import { useTranslation } from 'react-i18next';
import { Globe, Palette, Cpu, Check, Info, Heart } from 'lucide-react';
import { FluentSwitch } from '../components/FluentSwitch';

interface SettingsViewProps {
  currentLanguage: string;
  onLanguageChange: (lang: string) => void;
  pollingRate: number;
  onPollingRateChange: (rate: number) => void;
  onOpenDonate?: () => void;
}

export const SettingsView: React.FC<SettingsViewProps> = ({
  currentLanguage,
  onLanguageChange,
  pollingRate,
  onPollingRateChange,
  onOpenDonate,
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
    <div className="z-10 flex flex-1 flex-col gap-5 overflow-y-auto p-5 md:p-7">
      <section className="page-hero">
        <p className="page-kicker">Preferências</p>
        <div className="mt-2 flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
          <div className="max-w-2xl">
            <h1 className="page-title">{t('settings.title')}</h1>
            <p className="mt-3 text-sm text-[var(--text-muted)]">
              {t('settings.subtitle')}
            </p>
          </div>
          <div className="flex flex-wrap gap-2">
            <span className="toolbar-chip">Idioma: {currentLanguage.toUpperCase()}</span>
            <span className="toolbar-chip">Polling: {pollingRate} ms</span>
          </div>
        </div>
      </section>

      <div className="surface-card flex flex-col gap-4 p-5">
        <div className="flex items-center gap-2.5">
          <Globe className="w-5 h-5 text-[var(--accent)]" />
          <h2 className="section-title">
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
                    ? 'bg-[var(--accent-soft)] border-[var(--accent-line)] shadow-[var(--shadow-soft)]'
                    : 'bg-white border-[var(--border-soft)] hover:border-[var(--border-strong)]'
                }`}
              >
                <div className="flex items-center gap-2.5">
                  <span className="text-xl">{lang.flag}</span>
                  <span className="text-xs font-semibold text-[var(--text-strong)]">
                    {lang.label}
                  </span>
                </div>
                {isSelected && <Check className="w-4 h-4 text-[var(--accent)]" />}
              </button>
            );
          })}
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <div className="surface-card flex flex-col justify-between gap-4 p-5">
          <div className="flex flex-col gap-3">
            <h2 className="section-title flex items-center gap-2">
              <Cpu className="w-4 h-4 text-[var(--accent)]" />
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
                        ? 'bg-[var(--accent-soft)] border-[var(--accent-line)] text-[var(--text-strong)]'
                        : 'bg-white border-[var(--border-soft)] text-[var(--text-base)] hover:border-[var(--border-strong)]'
                    }`}
                  >
                    <span className="font-semibold">{item.label}</span>
                    <span className="text-[11px] text-[var(--text-muted)]">{item.desc}</span>
                  </button>
                );
              })}
            </div>
          </div>
        </div>

        <div className="surface-card flex flex-col justify-between gap-3 p-5">
          <h2 className="section-title flex items-center gap-2">
            <Palette className="w-4 h-4 text-[var(--accent)]" />
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

      {onOpenDonate && (
        <div className="surface-card flex flex-col items-start justify-between gap-4 border-rose-200/80 bg-gradient-to-r from-rose-50/70 via-white to-amber-50/50 p-5 sm:flex-row sm:items-center">
          <div className="flex items-center gap-3.5">
            <div className="flex h-10 w-10 items-center justify-center rounded-2xl border border-rose-200 bg-rose-100 text-rose-600 shadow-2xs">
              <Heart className="h-5 w-5 fill-current text-rose-500" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <span className="text-xs font-bold text-[var(--text-strong)]">
                  {t('donate.settingsTitle', 'Apoie o Desenvolvimento do Driver')}
                </span>
                <span className="rounded-full bg-rose-100 px-2 py-0.5 text-[10px] font-semibold text-rose-700">
                  Open Source
                </span>
              </div>
              <p className="mt-0.5 text-[11px] text-[var(--text-muted)]">
                {t('donate.settingsSub', 'Ajude a acelerar a chegada do driver Vulkan, suporte a DirectML e testes no silício.')}
              </p>
            </div>
          </div>

          <button
            type="button"
            onClick={onOpenDonate}
            className="inline-flex items-center gap-2 rounded-xl bg-gradient-to-r from-rose-500 to-rose-600 px-4 py-2 text-xs font-semibold text-white shadow-xs transition-all hover:brightness-105"
          >
            <Heart className="h-3.5 w-3.5 fill-current text-white" />
            <span>{t('donate.buttonLabel', 'Fazer Doação ☕')}</span>
          </button>
        </div>
      )}

      <div className="surface-card flex items-center justify-between p-4">
        <div className="flex items-center gap-3">
          <div className="flex h-9 w-9 items-center justify-center rounded-xl border border-[var(--border-soft)] bg-[var(--surface-2)] text-[var(--text-muted)]">
            <Info className="w-4 h-4 text-[var(--accent)]" />
          </div>
          <div className="flex flex-col">
            <span className="text-xs font-semibold text-[var(--text-strong)]">
              {t('settings.about')}
            </span>
            <span className="text-[11px] text-[var(--text-muted)]">
              {t('settings.version')} • {t('settings.author')}
            </span>
          </div>
        </div>

        <button
          type="button"
          onClick={handleSave}
          className="button-primary px-5 py-2"
        >
          {savedNotice ? 'Salvo!' : t('settings.save')}
        </button>
      </div>
    </div>
  );
};

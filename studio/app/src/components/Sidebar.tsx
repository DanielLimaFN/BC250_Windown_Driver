import React from 'react';
import {
  Menu,
  LayoutDashboard,
  HardDrive,
  Gauge,
  Unlock,
  Tv,
  Settings,
  Heart,
} from 'lucide-react';
import { useTranslation } from 'react-i18next';

export type ViewType = 'dashboard' | 'system' | 'performance' | 'unlocker' | 'driver' | 'settings';

interface SidebarProps {
  currentView: ViewType;
  onViewChange: (view: ViewType) => void;
  collapsed: boolean;
  onToggleCollapse: () => void;
  onOpenDonate?: () => void;
}

export const Sidebar: React.FC<SidebarProps> = ({
  currentView,
  onViewChange,
  collapsed,
  onToggleCollapse,
  onOpenDonate,
}) => {
  const { t } = useTranslation();

  const mainNavItems = [
    { id: 'dashboard' as ViewType, label: t('nav.dashboard'), icon: LayoutDashboard },
    { id: 'system' as ViewType, label: t('nav.system', 'Sistema'), icon: HardDrive },
    { id: 'performance' as ViewType, label: t('nav.performance'), icon: Gauge },
    { id: 'unlocker' as ViewType, label: t('nav.unlocker'), icon: Unlock },
    { id: 'driver' as ViewType, label: t('nav.driverManager'), icon: Tv },
  ];

  return (
    <aside
      className={`z-20 flex h-full flex-col justify-between border-r border-[var(--border-soft)] bg-[rgba(245,248,252,0.72)] px-3 py-4 transition-all duration-300 select-none ${
        collapsed ? 'w-[88px]' : 'w-[220px]'
      }`}
    >
      <div className="flex flex-col gap-4">
        <div className={`flex items-center ${collapsed ? 'justify-center' : 'justify-between'} px-1`}>
          {!collapsed && (
            <div>
              <p className="text-[11px] font-semibold uppercase tracking-[0.18em] text-[var(--text-soft)]">
                Navegação
              </p>
              <p className="mt-1 text-sm font-semibold text-[var(--text-strong)]">
                BC250 Studio
              </p>
            </div>
          )}

          <button
            onClick={onToggleCollapse}
            className="rail-button"
            title="Toggle Navigation"
          >
            <Menu className="h-4 w-4" />
          </button>
        </div>

        <nav className={`flex flex-col gap-2 ${collapsed ? 'items-center' : ''}`}>
          {mainNavItems.map((item) => {
            const Icon = item.icon;
            const isActive = currentView === item.id;
            return (
              <button
                key={item.id}
                onClick={() => onViewChange(item.id)}
                className={collapsed
                  ? `rail-button ${isActive ? 'rail-button-active' : ''}`
                  : `nav-button ${isActive ? 'nav-button-active' : 'border border-transparent bg-white/50'}`
                }
                title={collapsed ? item.label : undefined}
              >
                {!collapsed && isActive && (
                  <div className="absolute left-1 top-1/2 h-8 w-1 -translate-y-1/2 rounded-full bg-[var(--accent)]" />
                )}
                <Icon
                  className={`h-4 w-4 flex-shrink-0 transition-colors ${
                    isActive ? 'text-[var(--accent)]' : 'text-[var(--text-muted)]'
                  }`}
                />
                {!collapsed && (
                  <span className="truncate">{item.label}</span>
                )}
              </button>
            );
          })}
        </nav>

        {!collapsed && (
          <div className="page-hero p-4">
            <p className="page-kicker">
              Layout Novo
            </p>
            <p className="mt-2 text-sm font-semibold text-[var(--text-strong)]">
              Menos blocos, mais foco.
            </p>
            <p className="mt-1 text-[12px] leading-relaxed text-[var(--text-muted)]">
              Estrutura mais limpa para o app evoluir depois com integração real.
            </p>
          </div>
        )}
      </div>

      <div className="flex flex-col gap-3">
        {onOpenDonate && (
          collapsed ? (
            <button
              type="button"
              onClick={onOpenDonate}
              className="rail-button border-rose-200/80 bg-rose-50/70 text-rose-600 hover:bg-rose-100 hover:border-rose-300"
              title={t('donate.title', 'Apoiar o Projeto')}
            >
              <Heart className="h-4 w-4 fill-current text-rose-500 animate-pulse" />
            </button>
          ) : (
            <div className="rounded-2xl border border-rose-200/70 bg-gradient-to-b from-rose-50/80 to-amber-50/60 p-3.5 shadow-2xs">
              <div className="flex items-center gap-2">
                <div className="flex h-7 w-7 items-center justify-center rounded-xl bg-rose-100 text-rose-600">
                  <Heart className="h-3.5 w-3.5 fill-current text-rose-500" />
                </div>
                <span className="text-xs font-bold text-[var(--text-strong)]">
                  {t('donate.sidebarTitle', 'Apoie o Driver')}
                </span>
              </div>
              <p className="mt-1.5 text-[11px] leading-relaxed text-[var(--text-muted)]">
                {t('donate.sidebarSub', 'Ajude a financiar a bancada de testes e o avanço no silício.')}
              </p>
              <button
                type="button"
                onClick={onOpenDonate}
                className="mt-2.5 flex w-full items-center justify-center gap-1.5 rounded-xl bg-gradient-to-r from-rose-500 to-rose-600 px-3 py-2 text-xs font-semibold text-white shadow-xs transition-all hover:brightness-105"
              >
                <Heart className="h-3 w-3 fill-current text-white" />
                <span>{t('donate.sidebarBtn', 'Fazer Doação ☕')}</span>
              </button>
            </div>
          )
        )}

        {!collapsed && (
          <div className="stat-card px-3 py-3">
            <p className="text-[11px] font-medium uppercase tracking-[0.16em] text-[var(--text-soft)]">
              Estado
            </p>
            <p className="mt-2 text-sm font-semibold text-[var(--text-strong)]">
              Preview offline
            </p>
            <p className="mt-1 text-[11px] text-[var(--text-muted)]">
              Sem comunicação com driver nesta etapa.
            </p>
          </div>
        )}

        <button
          onClick={() => onViewChange('settings')}
          className={collapsed
            ? `rail-button ${currentView === 'settings' ? 'rail-button-active' : ''}`
            : `nav-button w-full ${currentView === 'settings' ? 'nav-button-active' : 'border border-transparent bg-white/50'}`
          }
          title={collapsed ? t('nav.settings') : undefined}
        >
          {!collapsed && currentView === 'settings' && (
            <div className="absolute left-1 top-1/2 h-8 w-1 -translate-y-1/2 rounded-full bg-[var(--accent)]" />
          )}

          <Settings
            className={`h-4 w-4 flex-shrink-0 transition-colors ${
              currentView === 'settings'
                ? 'text-[var(--accent)]'
                : 'text-[var(--text-muted)]'
            }`}
          />

          {!collapsed && (
            <span className="truncate">{t('nav.settings')}</span>
          )}
        </button>
      </div>
    </aside>
  );
};

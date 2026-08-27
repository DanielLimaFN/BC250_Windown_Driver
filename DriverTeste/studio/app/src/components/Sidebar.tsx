import React from 'react';
import {
  Menu,
  LayoutDashboard,
  Gauge,
  Unlock,
  Tv,
  Settings,
} from 'lucide-react';
import { useTranslation } from 'react-i18next';

export type ViewType = 'dashboard' | 'performance' | 'unlocker' | 'driver' | 'settings';

interface SidebarProps {
  currentView: ViewType;
  onViewChange: (view: ViewType) => void;
  collapsed: boolean;
  onToggleCollapse: () => void;
}

export const Sidebar: React.FC<SidebarProps> = ({
  currentView,
  onViewChange,
  collapsed,
  onToggleCollapse,
}) => {
  const { t } = useTranslation();

  const mainNavItems = [
    { id: 'dashboard' as ViewType, label: t('nav.dashboard'), icon: LayoutDashboard },
    { id: 'performance' as ViewType, label: t('nav.performance'), icon: Gauge },
    { id: 'unlocker' as ViewType, label: t('nav.unlocker'), icon: Unlock },
    { id: 'driver' as ViewType, label: t('nav.driverManager'), icon: Tv },
  ];

  return (
    <aside
      className={`h-full bg-[#141417]/90 border-r border-white/5 flex flex-col justify-between transition-all duration-300 select-none z-20 ${
        collapsed ? 'w-14' : 'w-48'
      }`}
    >
      {/* Top Section */}
      <div className="flex flex-col gap-1 p-2">
        {/* Toggle Hamburger Button */}
        <button
          onClick={onToggleCollapse}
          className="w-10 h-10 rounded-md flex items-center justify-center text-zinc-400 hover:text-white hover:bg-white/5 transition-colors mb-2"
          title="Toggle Navigation"
        >
          <Menu className="w-4 h-4" />
        </button>

        {/* Navigation Items */}
        <nav className="flex flex-col gap-1">
          {mainNavItems.map((item) => {
            const Icon = item.icon;
            const isActive = currentView === item.id;
            return (
              <button
                key={item.id}
                onClick={() => onViewChange(item.id)}
                className={`relative flex items-center gap-3 px-3 py-2.5 rounded-lg text-xs font-medium transition-all group ${
                  isActive
                    ? 'bg-[#222228] text-white shadow-sm'
                    : 'text-zinc-400 hover:text-zinc-200 hover:bg-white/5'
                }`}
              >
                {/* Active Red Indicator Pill */}
                {isActive && (
                  <div className="absolute left-0 top-1/2 -translate-y-1/2 w-1 h-5 bg-[#f25056] rounded-r shadow-[0_0_8px_rgba(242,80,86,0.8)]" />
                )}

                <Icon
                  className={`w-4 h-4 flex-shrink-0 transition-colors ${
                    isActive ? 'text-[#f25056]' : 'text-zinc-400 group-hover:text-zinc-200'
                  }`}
                />

                {!collapsed && (
                  <span className="truncate tracking-wide">{item.label}</span>
                )}
              </button>
            );
          })}
        </nav>
      </div>

      {/* Bottom Settings Button */}
      <div className="p-2 border-t border-white/5">
        <button
          onClick={() => onViewChange('settings')}
          className={`relative w-full flex items-center gap-3 px-3 py-2.5 rounded-lg text-xs font-medium transition-all group ${
            currentView === 'settings'
              ? 'bg-[#222228] text-white shadow-sm'
              : 'text-zinc-400 hover:text-zinc-200 hover:bg-white/5'
          }`}
        >
          {currentView === 'settings' && (
            <div className="absolute left-0 top-1/2 -translate-y-1/2 w-1 h-5 bg-[#f25056] rounded-r shadow-[0_0_8px_rgba(242,80,86,0.8)]" />
          )}

          <Settings
            className={`w-4 h-4 flex-shrink-0 transition-colors ${
              currentView === 'settings'
                ? 'text-[#f25056]'
                : 'text-zinc-400 group-hover:text-zinc-200'
            }`}
          />

          {!collapsed && (
            <span className="truncate tracking-wide">{t('nav.settings')}</span>
          )}
        </button>
      </div>
    </aside>
  );
};

import React, { useState, useEffect, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { TitleBar } from './components/TitleBar';
import { Sidebar, ViewType } from './components/Sidebar';
import { DonationModal } from './components/DonationModal';
import { DashboardView } from './views/DashboardView';
import { SystemView } from './views/SystemView';
import { PerformanceView } from './views/PerformanceView';
import { UnlockerView } from './views/UnlockerView';
import { DriverManagerView } from './views/DriverManagerView';
import { SettingsView } from './views/SettingsView';

export const App: React.FC = () => {
  const { i18n, t } = useTranslation();
  const [currentView, setCurrentView] = useState<ViewType>('dashboard');
  const [sidebarCollapsed, setSidebarCollapsed] = useState(true);
  const [language, setLanguage] = useState(i18n.language || 'pt');
  const [pollingRate, setPollingRate] = useState(500);
  const [isDonateOpen, setIsDonateOpen] = useState(false);

  // Live Telemetry Mock Engine
  const [telemetry, setTelemetry] = useState({
    cpuLoad: 45,
    cpuClock: 3.5,
    cpuTemp: 68,
    gpuTemp: 74,
    gpuUsage: 95,
    gpuClock: 1950,
    gpuVoltage: 1.15,
    gpuPower: 185,
    tempHistory: [
      62, 64, 63, 65, 66, 68, 67, 72, 70, 74, 73, 75, 74, 73, 74, 75, 74, 73, 74
    ],
    usageHistory: [
      15, 18, 20, 22, 92, 94, 95, 96, 95, 94, 95, 96, 95, 95, 96, 94, 95, 95, 95
    ],
  });

  // Change Language
  const handleLanguageChange = (lang: string) => {
    setLanguage(lang);
    i18n.changeLanguage(lang);
    localStorage.setItem('bc250_language', lang);
  };

  // Real-time telemetry ticker
  useEffect(() => {
    const timer = setInterval(() => {
      setTelemetry((prev) => {
        // Organic jitter around target values
        const newCpuLoad = Math.min(100, Math.max(10, Math.round(45 + (Math.random() * 8 - 4))));
        const newCpuTemp = Math.min(95, Math.max(40, Math.round(68 + (Math.random() * 4 - 2))));
        const newGpuTemp = Math.min(95, Math.max(40, Math.round(74 + (Math.random() * 4 - 2))));
        const newGpuUsage = Math.min(100, Math.max(80, Math.round(95 + (Math.random() * 6 - 3))));
        const newGpuClock = Math.round(1950 + (Math.random() * 30 - 15));
        const newGpuVoltage = Number((1.15 + (Math.random() * 0.04 - 0.02)).toFixed(2));
        const newGpuPower = Math.round(185 + (Math.random() * 8 - 4));

        const updatedTempHist = [...prev.tempHistory.slice(1), newGpuTemp];
        const updatedUsageHist = [...prev.usageHistory.slice(1), newGpuUsage];

        return {
          cpuLoad: newCpuLoad,
          cpuClock: 3.5,
          cpuTemp: newCpuTemp,
          gpuTemp: newGpuTemp,
          gpuUsage: newGpuUsage,
          gpuClock: newGpuClock,
          gpuVoltage: newGpuVoltage,
          gpuPower: newGpuPower,
          tempHistory: updatedTempHist,
          usageHistory: updatedUsageHist,
        };
      });
    }, pollingRate);

    return () => clearInterval(timer);
  }, [pollingRate]);

  const viewLabels = useMemo<Record<ViewType, string>>(
    () => ({
      dashboard: t('nav.dashboard'),
      system: t('nav.system', 'Sistema'),
      performance: t('nav.performance'),
      unlocker: t('nav.unlocker'),
      driver: t('nav.driverManager'),
      settings: t('nav.settings'),
    }),
    [t]
  );

  return (
    <div className="relative h-screen w-screen overflow-hidden p-3 md:p-4">
      <div className="ambient-glow-left" />
      <div className="ambient-glow-right" />

      {/* Global Donation Modal */}
      <DonationModal
        isOpen={isDonateOpen}
        onClose={() => setIsDonateOpen(false)}
      />

      <div className="app-shell relative z-10 flex h-full w-full flex-col">
        <TitleBar
          currentLanguage={language}
          currentViewLabel={viewLabels[currentView]}
          onLanguageChange={handleLanguageChange}
          onOpenDonate={() => setIsDonateOpen(true)}
        />

        <div className="flex flex-1 overflow-hidden bg-[rgba(255,255,255,0.22)]">
          <Sidebar
            currentView={currentView}
            onViewChange={setCurrentView}
            collapsed={sidebarCollapsed}
            onToggleCollapse={() => setSidebarCollapsed(!sidebarCollapsed)}
            onOpenDonate={() => setIsDonateOpen(true)}
          />

          <main className="flex-1 overflow-hidden bg-[linear-gradient(180deg,rgba(249,251,254,0.74)_0%,rgba(239,244,251,0.78)_100%)]">
            {currentView === 'dashboard' && (
              <DashboardView
                telemetry={telemetry}
                onNavigate={setCurrentView}
                onOpenDonate={() => setIsDonateOpen(true)}
              />
            )}
            {currentView === 'system' && (
              <SystemView onOpenDonate={() => setIsDonateOpen(true)} />
            )}
            {currentView === 'performance' && <PerformanceView />}
            {currentView === 'unlocker' && <UnlockerView />}
            {currentView === 'driver' && <DriverManagerView />}
            {currentView === 'settings' && (
              <SettingsView
                currentLanguage={language}
                onLanguageChange={handleLanguageChange}
                pollingRate={pollingRate}
                onPollingRateChange={setPollingRate}
                onOpenDonate={() => setIsDonateOpen(true)}
              />
            )}
          </main>
        </div>
      </div>
    </div>
  );
};

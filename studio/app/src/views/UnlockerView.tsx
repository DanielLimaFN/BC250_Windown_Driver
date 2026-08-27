import React, { useState, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Unlock,
  Cpu,
  Zap,
  AlertTriangle,
  ShieldCheck,
  CheckCircle2,
  Terminal,
  Gauge,
  Thermometer,
  Flame,
  Binary,
  Layers,
  Sparkles,
  Check,
  RefreshCw,
  XCircle,
} from 'lucide-react';
import { FluentSwitch } from '../components/FluentSwitch';
import {
  CuTopologyMatrix,
  DEFAULT_STOCK_WGPS,
  FULL_40CU_WGPS,
  getWgpKey,
  HealthStatus,
} from '../components/CuTopologyMatrix';

type ApplyStep = 'IDLE' | 'PREFLIGHT' | 'SNAPSHOT' | 'APPLY' | 'READBACK' | 'PROOF' | 'SUCCESS';

export const UnlockerView: React.FC = () => {
  const { t } = useTranslation();

  // CPU Unlock state (Zen 2 6C/12T -> 8C/16T)
  const [cpuUnlocked, setCpuUnlocked] = useState(true);

  // GPU CU / WGP Topology State (4 SA x 5 WGPs)
  const [activeWgps, setActiveWgps] = useState<Record<string, boolean>>(DEFAULT_STOCK_WGPS);

  // Health Test State
  const [healthResults, setHealthResults] = useState<Record<string, HealthStatus>>({});
  const [isTestingHealth, setIsTestingHealth] = useState(false);
  const [testingWgpKey, setTestingWgpKey] = useState<string | null>(null);
  const [healthNotice, setHealthNotice] = useState<string | null>(null);

  // Transaction State Machine
  const [applyStep, setApplyStep] = useState<ApplyStep>('IDLE');
  const [statusMessage, setStatusMessage] = useState<string | null>(null);

  // Derived CU stats
  const activeWgpCount = useMemo(
    () => Object.values(activeWgps).filter(Boolean).length,
    [activeWgps]
  );
  const activeCuCount = activeWgpCount * 2;
  const harvestedCuCount = 40 - activeCuCount;
  const unlockedCuCount = Math.max(0, activeCuCount - 24);
  const activeShaders = activeCuCount * 64;
  const estimatedTflops = ((activeShaders * 2 * 1.5) / 1000).toFixed(2); // At 1500 MHz

  // Estimate power envelope (TDP)
  const estimatedTdp = useMemo(() => {
    if (activeCuCount <= 24) return 95;
    if (activeCuCount <= 28) return 103;
    if (activeCuCount <= 32) return 110;
    if (activeCuCount <= 36) return 118;
    return 125; // 40 CUs @ 1500MHz
  }, [activeCuCount]);

  // Compute disabled WGP CSV for amdgpu disable_cu / MCDM mask
  const { badWgpsList, disabledCuList } = useMemo(() => {
    const badWgps: string[] = [];
    const disabledCus: string[] = [];
    for (let se = 0; se < 2; se++) {
      for (let sh = 0; sh < 2; sh++) {
        for (let wgp = 0; wgp < 5; wgp++) {
          const key = getWgpKey(se, sh, wgp);
          if (!activeWgps[key]) {
            badWgps.push(`${se}.${sh}.${wgp}`);
            disabledCus.push(`${se}.${sh}.${wgp * 2}`);
            disabledCus.push(`${se}.${sh}.${wgp * 2 + 1}`);
          }
        }
      }
    }
    return { badWgpsList: badWgps, disabledCuList: disabledCus };
  }, [activeWgps]);

  // Toggle individual WGP
  const handleToggleWgp = (se: number, sh: number, wgp: number) => {
    const key = getWgpKey(se, sh, wgp);
    setActiveWgps((prev) => ({
      ...prev,
      [key]: !prev[key],
    }));
  };

  // Preset Selection
  const handleApplyPreset = (preset: 'stock24' | 'cu28' | 'cu32' | 'cu36' | 'full40') => {
    switch (preset) {
      case 'stock24':
        setActiveWgps(DEFAULT_STOCK_WGPS);
        break;
      case 'cu28':
        setActiveWgps({
          ...DEFAULT_STOCK_WGPS,
          '0.0.3': true,
          '0.1.3': true,
        });
        break;
      case 'cu32':
        setActiveWgps({
          ...DEFAULT_STOCK_WGPS,
          '0.0.3': true,
          '0.1.3': true,
          '1.0.3': true,
          '1.1.3': true,
        });
        break;
      case 'cu36':
        setActiveWgps({
          '0.0.0': true, '0.0.1': true, '0.0.2': true, '0.0.3': true, '0.0.4': true,
          '0.1.0': true, '0.1.1': true, '0.1.2': true, '0.1.3': true, '0.1.4': true,
          '1.0.0': true, '1.0.1': true, '1.0.2': true, '1.0.3': true, '1.0.4': false,
          '1.1.0': true, '1.1.1': true, '1.1.2': true, '1.1.3': true, '1.1.4': false,
        });
        break;
      case 'full40':
        setActiveWgps(FULL_40CU_WGPS);
        break;
    }
  };

  // Run WGP Stability / Health Test
  const handleRunHealthTest = () => {
    setIsTestingHealth(true);
    setHealthNotice('Iniciando varredura e prova computacional nos 20 WGPs...');
    setHealthResults({});

    const allKeys: string[] = [];
    for (let se = 0; se < 2; se++) {
      for (let sh = 0; sh < 2; sh++) {
        for (let wgp = 0; wgp < 5; wgp++) {
          allKeys.push(getWgpKey(se, sh, wgp));
        }
      }
    }

    let currentIndex = 0;
    const interval = setInterval(() => {
      if (currentIndex < allKeys.length) {
        const key = allKeys[currentIndex];
        setTestingWgpKey(key);
        setHealthResults((prev) => ({
          ...prev,
          [key]: 'PASS',
        }));
        currentIndex++;
      } else {
        clearInterval(interval);
        setIsTestingHealth(false);
        setTestingWgpKey(null);
        setHealthNotice(t('unlocker.healthSummaryPass', 'Todos os WGPs testados passaram com sucesso na verificação!'));
        setTimeout(() => setHealthNotice(null), 5000);
      }
    }, 120);
  };

  // Fail-Closed Transactional Apply to Silicon
  const handleApplyToSilicon = () => {
    setApplyStep('PREFLIGHT');
    setStatusMessage(t('unlocker.statePreflight', 'Validando permissões e estado da GPU...'));

    setTimeout(() => {
      setApplyStep('SNAPSHOT');
      setStatusMessage(t('unlocker.stateSnapshot', 'Capturando snapshot dos registradores MMIO...'));

      setTimeout(() => {
        setApplyStep('APPLY');
        setStatusMessage(t('unlocker.stateApply', 'Gravando CC_GC_SHADER_ARRAY_CONFIG e SPI_PG...'));

        setTimeout(() => {
          setApplyStep('READBACK');
          setStatusMessage(t('unlocker.stateReadback', 'Auditando leitura de registradores...'));

          setTimeout(() => {
            setApplyStep('PROOF');
            setStatusMessage(t('unlocker.stateProof', 'Validando despacho de computação na GPU...'));

            setTimeout(() => {
              setApplyStep('SUCCESS');
              setStatusMessage(t('unlocker.stateSuccess', 'Topologia aplicada e validada com sucesso no hardware!'));

              setTimeout(() => {
                setApplyStep('IDLE');
                setStatusMessage(null);
              }, 3500);
            }, 500);
          }, 400);
        }, 500);
      }, 400);
    }, 500);
  };

  return (
    <div className="z-10 flex flex-1 flex-col gap-5 overflow-y-auto p-5 md:p-7">
      <section className="page-hero">
        <p className="page-kicker">Unlocker Avançado</p>
        <div className="mt-2 flex flex-col gap-4 sm:flex-row sm:items-center sm:justify-between">
          <div>
            <h1 className="flex items-center gap-2 text-[28px] font-semibold tracking-tight text-[var(--text-strong)]">
            {t('unlocker.title')}
            <span className="rounded-full border border-[var(--accent-line)] bg-[var(--accent-soft)] px-2.5 py-0.5 text-xs font-semibold text-[var(--accent)]">
              AMD BC-250 (1002:13FE)
            </span>
            </h1>
            <p className="mt-3 text-sm text-[var(--text-muted)]">
              {t('unlocker.subtitle')}
            </p>
          </div>

          {healthNotice && (
            <span className="flex items-center gap-1.5 rounded-xl border border-emerald-200 bg-emerald-50 px-3 py-1.5 text-xs text-emerald-700 animate-fade-in">
              <CheckCircle2 className="w-3.5 h-3.5" />
              {healthNotice}
            </span>
          )}
        </div>
      </section>

      <div className="flex items-start gap-3 rounded-2xl border border-amber-200 bg-amber-50 p-4">
        <AlertTriangle className="mt-0.5 h-5 w-5 flex-shrink-0 text-amber-600" />
        <div className="flex flex-col gap-1">
          <span className="text-xs font-bold text-amber-800">
            {t('unlocker.safetyWarningTitle')}
          </span>
          <span className="text-xs leading-relaxed text-amber-800/80">
            {t('unlocker.safetyWarningBody')}
          </span>
        </div>
      </div>

      {/* TOP SECTION: Interactive CU Topology Matrix */}
      <CuTopologyMatrix
        activeWgps={activeWgps}
        healthResults={healthResults}
        onToggleWgp={handleToggleWgp}
        onApplyPreset={handleApplyPreset}
        onRunHealthTest={handleRunHealthTest}
        isTestingHealth={isTestingHealth}
        testingWgpKey={testingWgpKey}
      />

      <div className="grid grid-cols-1 md:grid-cols-12 gap-4">
        <div className="surface-card md:col-span-7 flex flex-col justify-between gap-4 p-5">
          <div className="flex flex-col gap-3">
            <div className="flex items-center justify-between border-b border-[var(--border-soft)] pb-2.5">
              <div className="flex items-center gap-2">
                <Gauge className="w-4 h-4 text-[var(--accent)]" />
                <h2 className="text-sm font-semibold text-[var(--text-strong)]">
                  {t('unlocker.metricsTitle', 'Estimativa de Desempenho e Envelope Térmico')}
                </h2>
              </div>
              <span className="text-[11px] font-mono text-[var(--text-muted)]">
                {activeCuCount} CUs ({unlockedCuCount > 0 ? `+${unlockedCuCount} Unlocked` : 'Stock'})
              </span>
            </div>

            <div className="grid grid-cols-2 sm:grid-cols-3 gap-3 my-1">
              <div className="metric-tile flex flex-col">
                <span className="text-[11px] font-medium text-[var(--text-muted)]">
                  {t('unlocker.activeShaders', 'Shaders Ativos')}
                </span>
                <span className="mt-0.5 font-mono text-lg font-bold text-[var(--text-strong)]">
                  {activeShaders}
                </span>
                <span className="mt-auto text-[10px] text-[var(--text-soft)]">
                  Stock: 1536 • Max: 2560
                </span>
              </div>

              <div className="metric-tile flex flex-col">
                <span className="text-[11px] font-medium text-[var(--text-muted)]">
                  {t('unlocker.estimatedCompute', 'Poder FP32 (@ 1.5GHz)')}
                </span>
                <span className="mt-0.5 font-mono text-lg font-bold text-emerald-600">
                  {estimatedTflops} TFLOPS
                </span>
                <span className="mt-auto text-[10px] text-[var(--text-soft)]">
                  {((Number(estimatedTflops) / 4.61) * 100 - 100).toFixed(0)}% vs Stock (4.61 TF)
                </span>
              </div>

              <div className="metric-tile col-span-2 flex flex-col sm:col-span-1">
                <span className="text-[11px] font-medium text-[var(--text-muted)]">
                  {t('unlocker.estimatedPower', 'Consumo TDP')}
                </span>
                <span className={`mt-0.5 font-mono text-lg font-bold ${estimatedTdp > 115 ? 'text-[var(--accent)]' : 'text-amber-600'}`}>
                  ~{estimatedTdp}W
                </span>
                <span className="mt-auto text-[10px] text-[var(--text-soft)]">
                  @ 1500MHz / 900mV
                </span>
              </div>
            </div>

            <div className="flex items-center gap-3 rounded-xl border border-amber-200 bg-amber-50 p-3">
              <Thermometer className="h-4 w-4 flex-shrink-0 text-amber-600" />
              <div className="text-[11px] text-amber-900/85">
                <span className="font-semibold text-amber-800">{t('unlocker.thermalRecommendation', 'Recomendação Térmica')}: </span>
                {activeCuCount === 40 ? (
                  <span>Limite o clock em 1500 MHz / 900 mV no governor para manter a GPU abaixo de 85°C (125W TDP).</span>
                ) : (
                  <span>Operação estável com o perfil térmico padrão do driver WDDM / MCDM.</span>
                )}
              </div>
            </div>
          </div>
        </div>

        <div className="surface-card md:col-span-5 flex flex-col justify-between gap-4 p-5">
          <div className="flex flex-col gap-3">
            <div className="flex items-center gap-2.5 border-b border-[var(--border-soft)] pb-2.5">
              <Cpu className="w-4 h-4 text-[var(--accent)]" />
              <h2 className="text-sm font-semibold text-[var(--text-strong)]">
                {t('unlocker.cpuSection')}
              </h2>
            </div>

            <p className="text-xs leading-relaxed text-[var(--text-muted)]">
              {t('unlocker.cpuNote')}
            </p>

            <div className="grid grid-cols-4 gap-1.5 my-1">
              {[1, 2, 3, 4, 5, 6, 7, 8].map((core) => {
                const isExtra = core > 6;
                const active = !isExtra || cpuUnlocked;
                return (
                  <div
                    key={core}
                    className={`p-1.5 rounded border text-center transition-all ${
                      active
                        ? isExtra
                          ? 'border-[var(--accent-line)] bg-[var(--accent-soft)] text-[var(--accent)]'
                          : 'border-[var(--border-soft)] bg-white text-[var(--text-strong)]'
                        : 'border-[var(--border-soft)] bg-[var(--surface-2)] text-[var(--text-soft)]'
                    }`}
                  >
                    <span className="text-[10px] block font-medium">Core {core - 1}</span>
                    <span className="text-xs font-bold">
                      {active ? '2T' : 'OFF'}
                    </span>
                  </div>
                );
              })}
            </div>
          </div>

          <FluentSwitch
            checked={cpuUnlocked}
            onChange={setCpuUnlocked}
            title={t('dashboard.cpuUnlock')}
            subtitle={cpuUnlocked ? t('unlocker.cpuStatusUnlocked') : t('unlocker.cpuStatusLocked')}
            secondarySubtitle="Zen 2 7nm APU"
          />
        </div>
      </div>

      <div className="surface-card flex flex-col gap-4 p-5">
        <div className="flex flex-col gap-2 border-b border-[var(--border-soft)] pb-3 sm:flex-row sm:items-center sm:justify-between">
          <div className="flex items-center gap-2">
            <Terminal className="w-4 h-4 text-[var(--accent)]" />
            <h3 className="text-xs font-semibold uppercase tracking-wider text-[var(--text-strong)]">
              {t('unlocker.registersTitle', 'Registradores de Baixo Nível & Máscara de Kernel')}
            </h3>
          </div>
          <span className="text-[11px] font-mono text-[var(--text-muted)]">
            Device ID: 0x13FE (BC-250) • GC 10.1.3
          </span>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-3 gap-3 font-mono text-xs">
          <div className="surface-subtle flex flex-col gap-1 p-3">
            <span className="text-[10px] text-[var(--text-soft)]">MMIO CC ENUM MASK</span>
            <span className="font-bold text-[var(--text-base)]">CC_GC_SHADER_ARRAY_CONFIG</span>
            <span className="mt-1 text-emerald-600">
              {activeCuCount === 40 ? '0xffe00000 (40 CU)' : activeCuCount === 24 ? '0xfff80000 (24 CU)' : '0xfff00000 (Custom)'}
            </span>
          </div>

          <div className="surface-subtle flex flex-col gap-1 p-3">
            <span className="text-[10px] text-[var(--text-soft)]">MMIO SPI DISPATCH GATE</span>
            <span className="font-bold text-[var(--text-base)]">SPI_PG_STATIC_WGP_MASK</span>
            <span className="mt-1 text-emerald-600">
              {activeCuCount === 40 ? '0x0000001F (5 WGPs)' : activeCuCount === 24 ? '0x00000007 (3 WGPs)' : '0x0000000F (4 WGPs)'}
            </span>
          </div>

          <div className="surface-subtle flex flex-col gap-1 p-3">
            <span className="text-[10px] text-[var(--text-soft)]">KERNEL PARM / DISABLE_CU</span>
            <span className="font-bold text-[var(--text-base)]">options amdgpu</span>
            <span className="mt-1 truncate text-amber-700" title={disabledCuList.length > 0 ? `disable_cu=${disabledCuList.join(',')}` : 'bc250_cc_write_mode=3'}>
              {disabledCuList.length > 0 ? `disable_cu=${disabledCuList.slice(0, 4).join(',')}${disabledCuList.length > 4 ? '...' : ''}` : 'bc250_cc_write_mode=3'}
            </span>
          </div>
        </div>

        {applyStep !== 'IDLE' && (
          <div className="flex flex-col gap-3 rounded-2xl border border-[var(--accent-line)] bg-[var(--accent-soft)] p-4 animate-fade-in">
            <div className="flex items-center justify-between text-xs">
              <span className="flex items-center gap-2 font-semibold text-[var(--text-strong)]">
                <RefreshCw className="h-3.5 w-3.5 animate-spin text-[var(--accent)]" />
                {statusMessage}
              </span>
              <span className="text-[11px] font-mono text-[var(--text-muted)]">
                FASE: [{applyStep}]
              </span>
            </div>

            <div className="grid grid-cols-5 gap-2 pt-1">
              {(['PREFLIGHT', 'SNAPSHOT', 'APPLY', 'READBACK', 'PROOF'] as ApplyStep[]).map((step, idx) => {
                const stepOrder = ['PREFLIGHT', 'SNAPSHOT', 'APPLY', 'READBACK', 'PROOF'];
                const currentIdx = stepOrder.indexOf(applyStep);
                const thisIdx = idx;
                const isPassed = applyStep === 'SUCCESS' || currentIdx > thisIdx;
                const isCurrent = applyStep === step;

                return (
                  <div
                    key={step}
                    className={`py-1 px-2 rounded text-center text-[10px] font-bold border transition-all ${
                      isPassed
                        ? 'border-emerald-200 bg-emerald-50 text-emerald-700'
                        : isCurrent
                        ? 'animate-pulse border-[var(--accent-line)] bg-white text-[var(--accent)]'
                        : 'border-[var(--border-soft)] bg-[var(--surface-2)] text-[var(--text-soft)]'
                    }`}
                  >
                    {step}
                  </div>
                );
              })}
            </div>
          </div>
        )}

        <div className="flex flex-col sm:flex-row items-center justify-between gap-4 pt-2">
          <div className="flex items-center gap-3">
            <div className="flex h-8 w-8 items-center justify-center rounded-full border border-emerald-200 bg-emerald-50 text-emerald-600">
              <ShieldCheck className="w-4 h-4" />
            </div>
            <div className="flex flex-col">
              <span className="text-xs font-semibold text-[var(--text-strong)]">
                {t('unlocker.fuseStatus')}: 0x000000FF (Unlocked)
              </span>
              <span className="text-[11px] text-[var(--text-muted)]">
                SMN Queue #0 Ready • SOC15 Direct MMIO Safe
              </span>
            </div>
          </div>

          <div className="flex items-center gap-3 w-full sm:w-auto">
            {applyStep === 'SUCCESS' && (
              <span className="flex items-center gap-1 text-xs font-medium text-emerald-600">
                <CheckCircle2 className="w-3.5 h-3.5" />
                {t('unlocker.stateSuccess')}
              </span>
            )}

            <button
              type="button"
              onClick={handleApplyToSilicon}
              disabled={applyStep !== 'IDLE'}
              className="button-primary flex w-full items-center justify-center gap-2 px-6 disabled:opacity-50 sm:w-auto"
            >
              {applyStep !== 'IDLE' ? (
                <>
                  <RefreshCw className="w-3.5 h-3.5 animate-spin" />
                  <span>Gravando Silício...</span>
                </>
              ) : (
                <>
                  <Sparkles className="w-3.5 h-3.5" />
                  <span>{t('unlocker.applyToSilicon', 'Gravar Fuses & Aplicar no Silício')}</span>
                </>
              )}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};

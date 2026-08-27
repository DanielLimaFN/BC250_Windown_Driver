import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { CheckCircle2, XCircle, Sparkles, RefreshCw, Layers, ShieldCheck, Activity } from 'lucide-react';

export interface WgpKey {
  se: number;
  sh: number;
  wgp: number;
}

export type HealthStatus = 'PASS' | 'FAIL' | 'UNTESTED' | 'TESTING';

export interface CuTopologyMatrixProps {
  activeWgps: Record<string, boolean>; // key: `${se}.${sh}.${wgp}`
  healthResults?: Record<string, HealthStatus>; // key: `${se}.${sh}.${wgp}`
  onToggleWgp: (se: number, sh: number, wgp: number) => void;
  onApplyPreset: (preset: 'stock24' | 'cu28' | 'cu32' | 'cu36' | 'full40') => void;
  onRunHealthTest?: () => void;
  isTestingHealth?: boolean;
  testingWgpKey?: string | null;
}

export const getWgpKey = (se: number, sh: number, wgp: number): string => `${se}.${sh}.${wgp}`;

export const DEFAULT_STOCK_WGPS: Record<string, boolean> = {
  '0.0.0': true, '0.0.1': true, '0.0.2': true, '0.0.3': false, '0.0.4': false,
  '0.1.0': true, '0.1.1': true, '0.1.2': true, '0.1.3': false, '0.1.4': false,
  '1.0.0': true, '1.0.1': true, '1.0.2': true, '1.0.3': false, '1.0.4': false,
  '1.1.0': true, '1.1.1': true, '1.1.2': true, '1.1.3': false, '1.1.4': false,
};

export const FULL_40CU_WGPS: Record<string, boolean> = {
  '0.0.0': true, '0.0.1': true, '0.0.2': true, '0.0.3': true, '0.0.4': true,
  '0.1.0': true, '0.1.1': true, '0.1.2': true, '0.1.3': true, '0.1.4': true,
  '1.0.0': true, '1.0.1': true, '1.0.2': true, '1.0.3': true, '1.0.4': true,
  '1.1.0': true, '1.1.1': true, '1.1.2': true, '1.1.3': true, '1.1.4': true,
};

export const CuTopologyMatrix: React.FC<CuTopologyMatrixProps> = ({
  activeWgps,
  healthResults = {},
  onToggleWgp,
  onApplyPreset,
  onRunHealthTest,
  isTestingHealth = false,
  testingWgpKey = null,
}) => {
  const { t } = useTranslation();
  const [hoveredWgp, setHoveredWgp] = useState<{ se: number; sh: number; wgp: number } | null>(null);

  // Compute active stats
  const activeWgpCount = Object.values(activeWgps).filter(Boolean).length;
  const activeCuCount = activeWgpCount * 2;
  const harvestedCuCount = 40 - activeCuCount;
  const unlockedCuCount = Math.max(0, activeCuCount - 24);

  const shaderArrays = [
    { se: 0, sh: 0, label: 'SE0 SH0', baseOffset: '0x000' },
    { se: 0, sh: 1, label: 'SE0 SH1', baseOffset: '0x040' },
    { se: 1, sh: 0, label: 'SE1 SH0', baseOffset: '0x080' },
    { se: 1, sh: 1, label: 'SE1 SH1', baseOffset: '0x0C0' },
  ];

  return (
    <div className="surface-card flex flex-col gap-4 p-5 text-[var(--text-base)]">
      <div className="flex flex-col gap-3 border-b border-[var(--border-soft)] pb-3 sm:flex-row sm:items-center sm:justify-between">
        <div className="flex items-center gap-2.5">
          <div className="flex h-8 w-8 items-center justify-center rounded-xl border border-[var(--accent-line)] bg-[var(--accent-soft)] text-[var(--accent)]">
            <Layers className="w-4 h-4" />
          </div>
          <div>
            <h2 className="flex items-center gap-2 text-sm font-semibold text-[var(--text-strong)]">
              {t('unlocker.cuMatrixTitle', 'Matriz Topológica de CUs (Shader Engines & Arrays)')}
              <span className="rounded-full border border-[var(--accent-line)] bg-[var(--accent-soft)] px-2 py-0.5 text-[10px] font-bold text-[var(--accent)]">
                RDNA2 10.1.3
              </span>
            </h2>
            <p className="text-[11px] text-[var(--text-muted)]">
              {t('unlocker.cuMatrixSubtitle', 'Selecione individualmente quais pares de Compute Units (WGP) ativar na GPU.')}
            </p>
          </div>
        </div>

        <div className="flex items-center gap-3">
          <div className="flex items-center gap-2 rounded-xl border border-[var(--border-soft)] bg-white px-3 py-1.5">
            <span className="text-xs font-mono font-bold text-[var(--success)]">
              {activeCuCount}/40
            </span>
            <span className="text-[11px] text-[var(--text-muted)]">
              {t('unlocker.cusActive', 'CUs ativas')}
            </span>
            <span className="text-[10px] font-mono text-[var(--text-soft)]">
              ({harvestedCuCount} {t('unlocker.cusHarvested', 'colhidas')})
            </span>
          </div>
        </div>
      </div>

      <div className="flex flex-wrap items-center gap-2">
        <span className="mr-1 flex items-center gap-1 text-xs font-medium text-[var(--text-muted)]">
          <Sparkles className="h-3.5 w-3.5 text-[var(--accent)]" />
          {t('unlocker.presetsLabel', 'Presets:')}
        </span>

        <button
          type="button"
          onClick={() => onApplyPreset('stock24')}
          className={`px-3 py-1.5 rounded-lg text-xs font-medium border transition-all ${
            activeCuCount === 24
              ? 'border-blue-200 bg-blue-50 text-blue-700'
              : 'border-[var(--border-soft)] bg-white text-[var(--text-base)] hover:border-[var(--border-strong)]'
          }`}
        >
          {t('unlocker.presetStock', 'Padrão (24 CU Stock)')}
        </button>

        <button
          type="button"
          onClick={() => onApplyPreset('cu28')}
          className={`px-3 py-1.5 rounded-lg text-xs font-medium border transition-all ${
            activeCuCount === 28
              ? 'border-purple-200 bg-purple-50 text-purple-700'
              : 'border-[var(--border-soft)] bg-white text-[var(--text-base)] hover:border-[var(--border-strong)]'
          }`}
        >
          +4 CU (28 CU)
        </button>

        <button
          type="button"
          onClick={() => onApplyPreset('cu32')}
          className={`px-3 py-1.5 rounded-lg text-xs font-medium border transition-all ${
            activeCuCount === 32
              ? 'border-amber-200 bg-amber-50 text-amber-700'
              : 'border-[var(--border-soft)] bg-white text-[var(--text-base)] hover:border-[var(--border-strong)]'
          }`}
        >
          {t('unlocker.presetBalanced', '+8 CU (32 CU Balanceado)')}
        </button>

        <button
          type="button"
          onClick={() => onApplyPreset('cu36')}
          className={`px-3 py-1.5 rounded-lg text-xs font-medium border transition-all ${
            activeCuCount === 36
              ? 'border-orange-200 bg-orange-50 text-orange-700'
              : 'border-[var(--border-soft)] bg-white text-[var(--text-base)] hover:border-[var(--border-strong)]'
          }`}
        >
          +12 CU (36 CU)
        </button>

        <button
          type="button"
          onClick={() => onApplyPreset('full40')}
          className={`px-3 py-1.5 rounded-lg text-xs font-medium border transition-all ${
            activeCuCount === 40
              ? 'border-[var(--accent-line)] bg-[var(--accent-soft)] text-[var(--accent)] shadow-[var(--shadow-soft)]'
              : 'border-[var(--border-soft)] bg-white text-[var(--text-base)] hover:border-[var(--border-strong)]'
          }`}
        >
          {t('unlocker.presetFull', 'Desbloqueio Total (40 CU)')}
        </button>

        {onRunHealthTest && (
          <button
            type="button"
            onClick={onRunHealthTest}
            disabled={isTestingHealth}
            className="ml-auto flex items-center gap-1.5 rounded-lg border border-emerald-200 bg-emerald-50 px-3 py-1.5 text-xs font-medium text-emerald-700 transition-all disabled:opacity-50"
          >
            {isTestingHealth ? (
              <RefreshCw className="w-3.5 h-3.5 animate-spin text-emerald-400" />
            ) : (
              <ShieldCheck className="w-3.5 h-3.5 text-emerald-400" />
            )}
            <span>{isTestingHealth ? t('unlocker.testingHealth', 'Testando...') : t('unlocker.testHealth', 'Testar Estabilidade')}</span>
          </button>
        )}
      </div>

      <div className="flex flex-col gap-3 rounded-2xl border border-[var(--border-soft)] bg-[#f8fafc] p-4 font-mono">
        <div className="flex items-center justify-between border-b border-[var(--border-soft)] pb-2 text-[11px] text-[var(--text-soft)]">
          <span>ARRAY</span>
          <div className="flex items-center gap-4 sm:gap-6 pr-2">
            <span>WGP 0 (CU 0-1)</span>
            <span>WGP 1 (CU 2-3)</span>
            <span>WGP 2 (CU 4-5)</span>
            <span className="font-semibold text-[var(--accent)]">WGP 3 (CU 6-7)</span>
            <span className="font-semibold text-[var(--accent)]">WGP 4 (CU 8-9)</span>
          </div>
          <span>STATUS</span>
        </div>

        {shaderArrays.map(({ se, sh, label }) => {
          let rowActiveCus = 0;
          for (let wgp = 0; wgp < 5; wgp++) {
            if (activeWgps[getWgpKey(se, sh, wgp)]) {
              rowActiveCus += 2;
            }
          }

          return (
            <div
              key={label}
              className="flex flex-col justify-between gap-2 rounded-xl border border-[var(--border-soft)] bg-white p-2.5 transition-all hover:border-[var(--border-strong)] sm:flex-row sm:items-center"
            >
              <div className="flex items-center gap-2 min-w-[100px]">
                <span className="text-xs font-bold text-[var(--text-base)]">{label}:</span>
              </div>

              <div className="flex items-center gap-2.5 sm:gap-3">
                {[0, 1, 2, 3, 4].map((wgp) => {
                  const key = getWgpKey(se, sh, wgp);
                  const isActive = !!activeWgps[key];
                  const isStock = wgp < 3;
                  const isUnlocked = !isStock && isActive;
                  const health = healthResults[key];
                  const isBeingTested = isTestingHealth && testingWgpKey === key;

                  return (
                    <button
                      key={wgp}
                      type="button"
                      onClick={() => onToggleWgp(se, sh, wgp)}
                      onMouseEnter={() => setHoveredWgp({ se, sh, wgp })}
                      onMouseLeave={() => setHoveredWgp(null)}
                      title={`SE${se} SH${sh} WGP ${wgp} (CUs ${wgp * 2}-${wgp * 2 + 1}) - ${
                        isActive ? 'ATIVO' : 'DESATIVADO'
                      }`}
                      className={`relative group px-2.5 py-1.5 rounded-md border flex items-center gap-1.5 transition-all cursor-pointer select-none ${
                        isBeingTested
                          ? 'animate-pulse border-amber-300 bg-amber-50 text-amber-700'
                          : isActive
                          ? isUnlocked
                            ? 'border-[var(--accent-line)] bg-[var(--accent-soft)] text-[var(--accent)] hover:border-[var(--accent)]'
                            : 'border-emerald-200 bg-emerald-50 text-emerald-700 hover:bg-emerald-100'
                          : 'border-[var(--border-soft)] bg-[var(--surface-2)] text-[var(--text-soft)] hover:border-[var(--border-strong)] hover:text-[var(--text-base)]'
                      }`}
                    >
                      <span className="text-sm font-bold tracking-tight">
                        {isActive ? '■' : '□'}
                      </span>
                      <span className="text-sm font-bold tracking-tight">
                        {isActive ? '■' : '□'}
                      </span>

                      <span className="ml-0.5 font-sans text-[9px] opacity-60">
                        W{wgp}
                      </span>

                      {health === 'PASS' && (
                        <CheckCircle2 className="absolute -right-1.5 -top-1.5 h-3 w-3 rounded-full bg-white text-emerald-500" />
                      )}
                      {health === 'FAIL' && (
                        <XCircle className="absolute -right-1.5 -top-1.5 h-3 w-3 rounded-full bg-white text-red-500" />
                      )}
                    </button>
                  );
                })}
              </div>

              <div className="text-[11px] text-right min-w-[120px] font-sans">
                <span className={`font-semibold ${rowActiveCus > 6 ? 'text-[var(--accent)]' : 'text-[var(--text-base)]'}`}>
                  {rowActiveCus}/10 CUs
                </span>{' '}
                <span className="text-[var(--text-soft)]">({rowActiveCus / 2}/5 WGPs)</span>
              </div>
            </div>
          );
        })}
      </div>

      <div className="flex flex-col items-start justify-between gap-2 rounded-xl border border-[var(--border-soft)] bg-white p-3 text-xs sm:flex-row sm:items-center">
        <div className="flex items-center gap-2">
          <Activity className="h-4 w-4 text-[var(--accent)]" />
          {hoveredWgp ? (
            <span>
              <span className="font-bold text-[var(--text-strong)]">
                SE{hoveredWgp.se} SH{hoveredWgp.sh} WGP {hoveredWgp.wgp}
              </span>{' '}
              • CUs {hoveredWgp.wgp * 2} & {hoveredWgp.wgp * 2 + 1} •{' '}
              <span className={hoveredWgp.wgp < 3 ? 'text-emerald-600' : 'text-[var(--accent)]'}>
                {hoveredWgp.wgp < 3 ? t('unlocker.stockWgp', 'WGP Padrão de Fábrica') : t('unlocker.harvestedWgp', 'WGP Colhido / Desbloqueável')}
              </span>
            </span>
          ) : (
            <span className="text-[var(--text-muted)]">
              {t('unlocker.hoverHelp', 'Passe o mouse ou clique sobre qualquer par de CUs (WGP) para alternar sua ativação.')}
            </span>
          )}
        </div>

        <div className="flex items-center gap-3 text-[11px] text-[var(--text-muted)]">
          <span className="flex items-center gap-1">
            <span className="text-emerald-600">■</span> Stock (24 CU)
          </span>
          <span className="flex items-center gap-1">
            <span className="text-[var(--accent)]">■</span> Desbloqueado (+{unlockedCuCount} CU)
          </span>
          <span className="flex items-center gap-1">
            <span className="text-[var(--text-soft)]">□</span> Desativado ({harvestedCuCount} CU)
          </span>
        </div>
      </div>
    </div>
  );
};

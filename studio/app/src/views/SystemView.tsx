import React from 'react';
import { useTranslation } from 'react-i18next';
import {
  Cpu,
  HardDrive,
  Activity,
  Layers,
  ShieldAlert,
  ShieldCheck,
  CheckCircle2,
  Terminal,
  Heart,
  ExternalLink,
  Zap,
  Boxes,
  Microchip,
  Sparkles,
  Info
} from 'lucide-react';

interface SystemViewProps {
  onOpenDonate: () => void;
}

export const SystemView: React.FC<SystemViewProps> = ({ onOpenDonate }) => {
  const { t } = useTranslation();

  return (
    <div className="z-10 flex flex-1 flex-col gap-5 overflow-y-auto p-5 md:p-7">
      {/* Hero Section */}
      <section className="page-hero">
        <p className="page-kicker">Visão Geral do Hardware</p>
        <div className="mt-2 flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
          <div className="max-w-2xl">
            <h1 className="page-title">{t('system.title', 'Especificações da APU BC-250')}</h1>
            <p className="mt-3 text-sm leading-relaxed text-[var(--text-muted)]">
              {t('system.subtitle', 'Detalhes completos da arquitetura do silício AMD Cyan Skillfish 2 (Oberon Plus), barramento PCIe, topologia de CUs e subsistema de memória GDDR6.')}
            </p>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <span className="toolbar-chip font-mono">1002:13FE</span>
            <span className="toolbar-chip">GC 10.1.3</span>
            <button
              type="button"
              onClick={onOpenDonate}
              className="inline-flex items-center gap-1.5 rounded-full border border-rose-200 bg-rose-50/90 px-3.5 py-1 text-[11px] font-semibold text-rose-600 shadow-sm transition-all hover:bg-rose-100"
            >
              <Heart className="h-3 w-3 fill-current text-rose-500" />
              {t('donate.buttonLabel', 'Doar para o Driver')}
            </button>
          </div>
        </div>

        {/* Quick Identity Stat Cards */}
        <div className="mt-5 grid grid-cols-2 gap-3 xl:grid-cols-4">
          <div className="stat-card">
            <p className="text-[11px] uppercase tracking-[0.16em] text-[var(--text-soft)]">
              Silício / SoC
            </p>
            <p className="mt-2 text-xl font-bold text-[var(--text-strong)]">
              Cyan Skillfish 2
            </p>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              Custom Console APU (Zen 2 + RDNA2)
            </p>
          </div>

          <div className="stat-card">
            <p className="text-[11px] uppercase tracking-[0.16em] text-[var(--text-soft)]">
              Memória Unificada
            </p>
            <p className="mt-2 text-xl font-bold text-[var(--text-strong)]">
              16 GB GDDR6
            </p>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              256-bit @ 14 Gbps (224 GB/s)
            </p>
          </div>

          <div className="stat-card">
            <p className="text-[11px] uppercase tracking-[0.16em] text-[var(--text-soft)]">
              Topologia CU
            </p>
            <p className="mt-2 text-xl font-bold text-[var(--text-strong)]">
              24 / 40 CUs
            </p>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              1536 Shaders (expansível a 2560)
            </p>
          </div>

          <div className="stat-card">
            <p className="text-[11px] uppercase tracking-[0.16em] text-[var(--text-soft)]">
              Modelo de Driver
            </p>
            <p className="mt-2 text-xl font-bold text-[var(--text-strong)]">
              WDDM MCDM
            </p>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              Compute-Only (Desacoplado de display)
            </p>
          </div>
        </div>
      </section>

      {/* Main Hardware Specs Grid */}
      <div className="grid grid-cols-1 gap-5 xl:grid-cols-2">
        {/* GPU & Compute Engine Details */}
        <div className="surface-card flex flex-col gap-4 p-5">
          <div className="flex items-center justify-between border-b border-[var(--border-soft)] pb-3">
            <div className="flex items-center gap-2.5">
              <div className="flex h-9 w-9 items-center justify-center rounded-xl bg-rose-50 text-rose-600">
                <Zap className="h-4 w-4 text-[var(--accent)]" />
              </div>
              <div>
                <h2 className="section-title">Acelerador Gráfico & Computacional</h2>
                <p className="section-subtitle">GC 10.1.3 / RDNA 2 Custom Pipeline</p>
              </div>
            </div>
            <span className="chip-status border-emerald-200 bg-emerald-50 text-emerald-700">
              Operacional
            </span>
          </div>

          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 text-xs">
            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">PCI Vendor / Device ID</span>
              <p className="mt-1 font-mono font-semibold text-[var(--text-strong)]">0x1002 : 0x13FE (Rev 00)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Shader Engines & Arrays</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">2 SEs × 2 SAs (4 SAs total)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Workgroup Processors (WGP)</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">12 WGPs Stock (20 WGPs Físicos)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Poder FP32 Estimado</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">6.14 TFLOPs @ 1500MHz</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Engine de DMA</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">SDMA0 + SDMA1 (Dual Ring)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Processador de Comandos</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">CP PM4 / Microengine RLC</p>
            </div>
          </div>
        </div>

        {/* Memory & Bus Subsystem */}
        <div className="surface-card flex flex-col gap-4 p-5">
          <div className="flex items-center justify-between border-b border-[var(--border-soft)] pb-3">
            <div className="flex items-center gap-2.5">
              <div className="flex h-9 w-9 items-center justify-center rounded-xl bg-blue-50 text-blue-600">
                <HardDrive className="h-4 w-4" />
              </div>
              <div>
                <h2 className="section-title">Subsistema de Memória & Barramento</h2>
                <p className="section-subtitle">Arquitetura de VRAM Unificada e Mapeamento MMIO</p>
              </div>
            </div>
            <span className="chip-status">224 GB/s</span>
          </div>

          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 text-xs">
            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Tipo de Memória Física</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">GDDR6 (Samsung / Micron)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Largura de Barramento</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">256-bit (8 Canais x 32-bit)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">PCIe Aperture BAR0 (VRAM)</span>
              <p className="mt-1 font-mono font-semibold text-[var(--text-strong)]">0xF0000000 (256 MB Window)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">PCIe MMIO BAR2 (SOC15)</span>
              <p className="mt-1 font-mono font-semibold text-[var(--text-strong)]">0xE0000000 (8 MB Range)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Tradução de Endereços</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">GCVM Hub 2-Level Page Tables</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Link PCIe Ativo</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">PCIe 4.0 x8 (16 GT/s)</p>
            </div>
          </div>
        </div>

        {/* Central Processing Unit (Zen 2) */}
        <div className="surface-card flex flex-col gap-4 p-5">
          <div className="flex items-center justify-between border-b border-[var(--border-soft)] pb-3">
            <div className="flex items-center gap-2.5">
              <div className="flex h-9 w-9 items-center justify-center rounded-xl bg-purple-50 text-purple-600">
                <Cpu className="h-4 w-4" />
              </div>
              <div>
                <h2 className="section-title">Processador Central (Zen 2)</h2>
                <p className="section-subtitle">Complexo CCX Integrado de 8 Núcleos Físicos</p>
              </div>
            </div>
            <span className="chip-status border-purple-200 bg-purple-50 text-purple-700">
              Desbloqueável 8C/16T
            </span>
          </div>

          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 text-xs">
            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Configuração Ativa</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">6 Núcleos / 12 Threads (Stock)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Frequência Base / Boost</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">3.20 GHz / 3.50 GHz All-Core</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Memória Cache L3</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">8 MB Unified CCX Cache</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Conjunto de Instruções</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">x86-64, AVX2, BMI2, SHA</p>
            </div>
          </div>
        </div>

        {/* Firmware, Security & Platform */}
        <div className="surface-card flex flex-col gap-4 p-5">
          <div className="flex items-center justify-between border-b border-[var(--border-soft)] pb-3">
            <div className="flex items-center gap-2.5">
              <div className="flex h-9 w-9 items-center justify-center rounded-xl bg-amber-50 text-amber-600">
                <ShieldCheck className="h-4 w-4" />
              </div>
              <div>
                <h2 className="section-title">Plataforma, BIOS e Segurança</h2>
                <p className="section-subtitle">Camada de Inicialização e Firmware PSP</p>
              </div>
            </div>
            <span className="chip-status">BIOS P4.00G</span>
          </div>

          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 text-xs">
            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Placa-mãe / Firmware</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">BC-250 Desktop Conversion (P4.00G)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Platform Security Processor</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">AMD PSP v11.0.3 (Secure OS)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Sistema Operacional</span>
              <p className="mt-1 font-semibold text-[var(--text-strong)]">Windows 11 Pro 64-bit (23H2)</p>
            </div>

            <div className="rounded-xl border border-[var(--border-soft)] bg-white/70 p-3">
              <span className="text-[11px] text-[var(--text-muted)]">Assinatura de Driver (KDN)</span>
              <p className="mt-1 font-semibold text-emerald-600">TestSigning Ativo (Driver Lab OK)</p>
            </div>
          </div>
        </div>
      </div>

      {/* Donation / Support Project Banner */}
      <div className="surface-card flex flex-col items-center justify-between gap-4 border-rose-200/60 bg-gradient-to-r from-rose-50/70 via-white to-amber-50/50 p-6 sm:flex-row">
        <div className="flex items-center gap-4">
          <div className="flex h-12 w-12 items-center justify-center rounded-2xl border border-rose-200 bg-rose-100/80 text-rose-600 shadow-sm">
            <Heart className="h-6 w-6 fill-current text-rose-500" />
          </div>
          <div>
            <div className="flex items-center gap-2">
              <h3 className="text-sm font-bold text-[var(--text-strong)]">
                Apoie o Projeto de Driver Aberto para AMD BC-250
              </h3>
              <span className="rounded-full bg-rose-100 px-2 py-0.5 text-[10px] font-semibold text-rose-700">
                Comunidade
              </span>
            </div>
            <p className="mt-1 text-xs text-[var(--text-muted)]">
              Contribua com qualquer valor para financiar equipamentos de laboratório, osciloscópio e o desenvolvimento do driver Vulkan & LLM.
            </p>
          </div>
        </div>

        <button
          type="button"
          onClick={onOpenDonate}
          className="button-primary whitespace-nowrap px-6 py-2.5 shadow-md"
        >
          <Heart className="mr-1.5 h-4 w-4 fill-current text-white" />
          Doar para o Driver
        </button>
      </div>
    </div>
  );
};

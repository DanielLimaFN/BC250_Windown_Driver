import React, { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Heart,
  X,
  Copy,
  Check,
  ExternalLink,
  QrCode,
  CreditCard,
  Coins,
  Sparkles,
  ShieldCheck,
  Zap,
  Coffee,
} from 'lucide-react';

interface DonationModalProps {
  isOpen: boolean;
  onClose: () => void;
}

export const DonationModal: React.FC<DonationModalProps> = ({ isOpen, onClose }) => {
  const { t } = useTranslation();
  const [activeTab, setActiveTab] = useState<'pix' | 'cards' | 'crypto'>('pix');
  const [copiedField, setCopiedField] = useState<string | null>(null);

  // Sample donation addresses from project specs
  const pixKey = 'bc250.driver.lab@gmail.com';
  const btcAddress = 'bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh';
  const ethAddress = '0x71C83d5483981881855aD8aEcB1d14878aE7F302';
  const usdtAddress = 'TYDzsYUEpvnYmQk4zGP9sWWcTEd3TgKa2c';
  const kofiUrl = 'https://ko-fi.com';
  const githubSponsorsUrl = 'https://github.com/sponsors';

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    if (isOpen) {
      window.addEventListener('keydown', handleKeyDown);
    }
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [isOpen, onClose]);

  if (!isOpen) return null;

  const handleCopy = (text: string, fieldId: string) => {
    navigator.clipboard.writeText(text);
    setCopiedField(fieldId);
    setTimeout(() => {
      setCopiedField((prev) => (prev === fieldId ? null : prev));
    }, 2200);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4">
      {/* Backdrop */}
      <div
        className="fixed inset-0 bg-slate-900/60 backdrop-blur-md transition-opacity"
        onClick={onClose}
      />

      {/* Modal Dialog */}
      <div className="relative z-10 flex max-h-[92vh] w-full max-w-2xl flex-col overflow-hidden rounded-3xl border border-white/80 bg-white/95 shadow-2xl backdrop-blur-xl">
        {/* Modal Header */}
        <div className="relative border-b border-[var(--border-soft)] bg-gradient-to-r from-rose-50/70 via-white to-amber-50/60 p-6">
          <div className="flex items-start justify-between">
            <div className="flex items-center gap-3">
              <div className="flex h-11 w-11 items-center justify-center rounded-2xl border border-rose-200 bg-rose-50 text-rose-600 shadow-sm">
                <Heart className="h-5 w-5 fill-rose-500 text-rose-500" />
              </div>
              <div>
                <div className="flex items-center gap-2">
                  <h2 className="text-lg font-bold text-[var(--text-strong)]">
                    {t('donate.title', 'Apoie o Desenvolvimento do Driver')}
                  </h2>
                  <span className="inline-flex items-center gap-1 rounded-full bg-rose-100 px-2 py-0.5 text-[10px] font-semibold text-rose-700">
                    <Sparkles className="h-3 w-3" />
                    Open Source
                  </span>
                </div>
                <p className="mt-0.5 text-xs text-[var(--text-muted)]">
                  {t('donate.subtitle', 'Ajude a financiar a bancada de testes, osciloscópio e o driver WDDM/Vulkan para BC-250.')}
                </p>
              </div>
            </div>

            <button
              onClick={onClose}
              className="flex h-8 w-8 items-center justify-center rounded-xl text-slate-400 hover:bg-slate-100 hover:text-slate-700 transition-colors"
              title="Fechar"
            >
              <X className="h-4 w-4" />
            </button>
          </div>

          {/* Project Goals / Progress Bar */}
          <div className="mt-4 rounded-xl border border-rose-100 bg-white/80 p-3.5 shadow-sm">
            <div className="flex items-center justify-between text-xs">
              <span className="font-semibold text-[var(--text-strong)] flex items-center gap-1.5">
                <Zap className="h-3.5 w-3.5 text-amber-500" />
                {t('donate.roadmapTitle', 'Meta Atual: Fechamento do Ring PM4 & Shaders RDNA2')}
              </span>
              <span className="font-bold text-rose-600">72%</span>
            </div>
            <div className="mt-2 h-2 w-full overflow-hidden rounded-full bg-slate-100">
              <div
                className="h-full rounded-full bg-gradient-to-r from-rose-500 to-amber-500 transition-all duration-500"
                style={{ width: '72%' }}
              />
            </div>
            <div className="mt-2 flex flex-wrap items-center justify-between text-[11px] text-[var(--text-muted)]">
              <span>✓ SDMA 100% no Silício</span>
              <span>✓ MMIO & GCVM Gate 20/20</span>
              <span className="font-medium text-rose-600">Próximo: Vulkan RADV + LLM</span>
            </div>
          </div>

          {/* Navigation Tabs */}
          <div className="mt-4 flex gap-1 rounded-xl bg-slate-100/80 p-1">
            <button
              type="button"
              onClick={() => setActiveTab('pix')}
              className={`flex flex-1 items-center justify-center gap-2 rounded-lg py-2 text-xs font-semibold transition-all ${
                activeTab === 'pix'
                  ? 'bg-white text-[var(--text-strong)] shadow-sm'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-strong)]'
              }`}
            >
              <QrCode className="h-3.5 w-3.5 text-emerald-600" />
              Pix (Brasil 🇧🇷)
            </button>
            <button
              type="button"
              onClick={() => setActiveTab('cards')}
              className={`flex flex-1 items-center justify-center gap-2 rounded-lg py-2 text-xs font-semibold transition-all ${
                activeTab === 'cards'
                  ? 'bg-white text-[var(--text-strong)] shadow-sm'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-strong)]'
              }`}
            >
              <CreditCard className="h-3.5 w-3.5 text-blue-600" />
              {t('donate.cardsTab', 'Cartões & Internacional 💳')}
            </button>
            <button
              type="button"
              onClick={() => setActiveTab('crypto')}
              className={`flex flex-1 items-center justify-center gap-2 rounded-lg py-2 text-xs font-semibold transition-all ${
                activeTab === 'crypto'
                  ? 'bg-white text-[var(--text-strong)] shadow-sm'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-strong)]'
              }`}
            >
              <Coins className="h-3.5 w-3.5 text-amber-500" />
              {t('donate.cryptoTab', 'Criptomoedas 🪙')}
            </button>
          </div>
        </div>

        {/* Modal Body */}
        <div className="flex-1 overflow-y-auto p-6">
          {/* PIX TAB */}
          {activeTab === 'pix' && (
            <div className="flex flex-col gap-5">
              <div className="flex flex-col items-center gap-4 sm:flex-row">
                {/* Visual QR Code SVG Mock */}
                <div className="flex flex-col items-center rounded-2xl border border-emerald-200/80 bg-emerald-50/40 p-4 shadow-sm">
                  <div className="relative flex h-36 w-36 items-center justify-center rounded-xl bg-white p-2 shadow-inner">
                    <svg
                      viewBox="0 0 100 100"
                      className="h-full w-full text-slate-800"
                      fill="currentColor"
                    >
                      {/* Corner markers */}
                      <rect x="5" y="5" width="26" height="26" rx="4" fill="none" stroke="currentColor" strokeWidth="6" />
                      <rect x="12" y="12" width="12" height="12" fill="currentColor" />
                      <rect x="69" y="5" width="26" height="26" rx="4" fill="none" stroke="currentColor" strokeWidth="6" />
                      <rect x="76" y="12" width="12" height="12" fill="currentColor" />
                      <rect x="5" y="69" width="26" height="26" rx="4" fill="none" stroke="currentColor" strokeWidth="6" />
                      <rect x="12" y="76" width="12" height="12" fill="currentColor" />

                      {/* Dot patterns */}
                      <rect x="36" y="8" width="6" height="6" />
                      <rect x="48" y="14" width="6" height="6" />
                      <rect x="58" y="8" width="6" height="6" />
                      <rect x="36" y="24" width="6" height="6" />
                      <rect x="48" y="28" width="6" height="6" />

                      <rect x="8" y="36" width="6" height="6" />
                      <rect x="18" y="44" width="6" height="6" />
                      <rect x="28" y="36" width="6" height="6" />

                      <rect x="40" y="40" width="20" height="20" rx="3" fill="#c2414a" />
                      <path d="M47 50 L53 50 M50 47 L50 53" stroke="white" strokeWidth="2.5" strokeLinecap="round" />

                      <rect x="72" y="38" width="6" height="6" />
                      <rect x="82" y="46" width="6" height="6" />
                      <rect x="68" y="54" width="6" height="6" />

                      <rect x="38" y="68" width="6" height="6" />
                      <rect x="48" y="76" width="6" height="6" />
                      <rect x="60" y="70" width="6" height="6" />
                      <rect x="74" y="74" width="6" height="6" />
                      <rect x="86" y="84" width="6" height="6" />
                      <rect x="52" y="86" width="6" height="6" />
                    </svg>
                  </div>
                  <span className="mt-2 text-[10px] font-semibold text-emerald-800">
                    Escaneie pelo App do seu Banco
                  </span>
                </div>

                <div className="flex flex-1 flex-col gap-3">
                  <div>
                    <label className="text-xs font-semibold text-[var(--text-strong)]">
                      {t('donate.pixKeyLabel', 'Chave Pix Oficial (E-mail / Chave Direta):')}
                    </label>
                    <p className="text-[11px] text-[var(--text-muted)]">
                      {t('donate.pixKeySub', 'Recebimento instantâneo destinado exclusivamente ao projeto BC-250.')}
                    </p>
                  </div>

                  <div className="flex items-center gap-2">
                    <div className="flex-1 rounded-xl border border-[var(--border-soft)] bg-slate-50 px-3 py-2.5 font-mono text-xs text-[var(--text-strong)]">
                      {pixKey}
                    </div>
                    <button
                      type="button"
                      onClick={() => handleCopy(pixKey, 'pix')}
                      className={`inline-flex items-center gap-1.5 rounded-xl px-4 py-2.5 text-xs font-semibold transition-all ${
                        copiedField === 'pix'
                          ? 'bg-emerald-600 text-white'
                          : 'button-primary'
                      }`}
                    >
                      {copiedField === 'pix' ? (
                        <>
                          <Check className="h-3.5 w-3.5" />
                          {t('donate.copied', 'Copiado!')}
                        </>
                      ) : (
                        <>
                          <Copy className="h-3.5 w-3.5" />
                          {t('donate.copy', 'Copiar')}
                        </>
                      )}
                    </button>
                  </div>

                  {/* Quick amount suggestions */}
                  <div className="mt-1">
                    <span className="text-[11px] font-medium text-[var(--text-muted)]">
                      Sugestões de Apoio:
                    </span>
                    <div className="mt-1.5 grid grid-cols-2 gap-2 sm:grid-cols-4">
                      {[
                        { amount: 'R$ 10', label: 'Cafezinho ☕' },
                        { amount: 'R$ 25', label: 'Dev Apoiador 🛠️' },
                        { amount: 'R$ 50', label: 'Bancada 🔬' },
                        { amount: 'R$ 100+', label: 'Patrono 👑' },
                      ].map((tier, idx) => (
                        <div
                          key={idx}
                          className="flex flex-col items-center rounded-xl border border-[var(--border-soft)] bg-white p-2 text-center shadow-2xs hover:border-rose-300 transition-colors"
                        >
                          <span className="text-xs font-bold text-[var(--text-strong)]">
                            {tier.amount}
                          </span>
                          <span className="text-[10px] text-[var(--text-muted)]">
                            {tier.label}
                          </span>
                        </div>
                      ))}
                    </div>
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* CARDS & INTERNATIONAL TAB */}
          {activeTab === 'cards' && (
            <div className="flex flex-col gap-4">
              <p className="text-xs text-[var(--text-muted)]">
                {t('donate.cardsDesc', 'Apoie internacionalmente com Cartão de Crédito, PayPal ou assinaturas mensais.')}
              </p>

              <div className="grid grid-cols-1 gap-3 sm:grid-cols-2">
                <a
                  href={kofiUrl}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="flex items-center justify-between rounded-2xl border border-[var(--border-soft)] bg-white p-4 transition-all hover:border-rose-300 hover:shadow-md"
                >
                  <div className="flex items-center gap-3">
                    <div className="flex h-10 w-10 items-center justify-center rounded-xl bg-amber-50 text-amber-600">
                      <Coffee className="h-5 w-5" />
                    </div>
                    <div>
                      <span className="text-xs font-bold text-[var(--text-strong)]">
                        Ko-fi (Buy Me a Coffee)
                      </span>
                      <p className="text-[11px] text-[var(--text-muted)]">
                        Cartão de crédito internacional & PayPal
                      </p>
                    </div>
                  </div>
                  <ExternalLink className="h-4 w-4 text-slate-400" />
                </a>

                <a
                  href={githubSponsorsUrl}
                  target="_blank"
                  rel="noopener noreferrer"
                  className="flex items-center justify-between rounded-2xl border border-[var(--border-soft)] bg-white p-4 transition-all hover:border-rose-300 hover:shadow-md"
                >
                  <div className="flex items-center gap-3">
                    <div className="flex h-10 w-10 items-center justify-center rounded-xl bg-slate-100 text-slate-700">
                      <Heart className="h-5 w-5 text-rose-500 fill-rose-500" />
                    </div>
                    <div>
                      <span className="text-xs font-bold text-[var(--text-strong)]">
                        GitHub Sponsors
                      </span>
                      <p className="text-[11px] text-[var(--text-muted)]">
                        Apoio direto recorrente na plataforma
                      </p>
                    </div>
                  </div>
                  <ExternalLink className="h-4 w-4 text-slate-400" />
                </a>
              </div>

              <div className="rounded-xl border border-blue-100 bg-blue-50/50 p-3.5 text-xs text-blue-900">
                <p className="font-semibold flex items-center gap-1.5">
                  <ShieldCheck className="h-4 w-4 text-blue-600" />
                  Transparência de Uso dos Recursos
                </p>
                <p className="mt-1 text-[11px] leading-relaxed text-blue-800">
                  Todo valor arrecadado é revertido na aquisição de amostras físicas de hardware BC-250 para testes destrutivos de fuses, pontas de prova para osciloscópio e servidores de compilação contínua.
                </p>
              </div>
            </div>
          )}

          {/* CRYPTO TAB */}
          {activeTab === 'crypto' && (
            <div className="flex flex-col gap-3">
              <p className="text-xs text-[var(--text-muted)]">
                {t('donate.cryptoDesc', 'Doações descentralizadas e anônimas para manter o projeto independente:')}
              </p>

              {/* Bitcoin */}
              <div className="flex flex-col gap-1.5 rounded-xl border border-[var(--border-soft)] bg-white p-3 shadow-2xs">
                <div className="flex items-center justify-between">
                  <span className="text-xs font-bold text-[var(--text-strong)]">
                    Bitcoin (BTC - Rede Nativa)
                  </span>
                  <span className="rounded bg-amber-100 px-1.5 py-0.5 text-[10px] font-semibold text-amber-800">
                    BTC
                  </span>
                </div>
                <div className="flex items-center gap-2">
                  <span className="flex-1 truncate font-mono text-[11px] text-[var(--text-muted)]">
                    {btcAddress}
                  </span>
                  <button
                    type="button"
                    onClick={() => handleCopy(btcAddress, 'btc')}
                    className={`inline-flex items-center gap-1 rounded-lg px-2.5 py-1 text-[11px] font-semibold transition-all ${
                      copiedField === 'btc'
                        ? 'bg-emerald-600 text-white'
                        : 'button-secondary'
                    }`}
                  >
                    {copiedField === 'btc' ? <Check className="h-3 w-3" /> : <Copy className="h-3 w-3" />}
                    {copiedField === 'btc' ? 'OK!' : 'Copiar'}
                  </button>
                </div>
              </div>

              {/* Ethereum / ERC-20 */}
              <div className="flex flex-col gap-1.5 rounded-xl border border-[var(--border-soft)] bg-white p-3 shadow-2xs">
                <div className="flex items-center justify-between">
                  <span className="text-xs font-bold text-[var(--text-strong)]">
                    Ethereum / USDT (ERC-20 / EVM)
                  </span>
                  <span className="rounded bg-blue-100 px-1.5 py-0.5 text-[10px] font-semibold text-blue-800">
                    ETH / USDT
                  </span>
                </div>
                <div className="flex items-center gap-2">
                  <span className="flex-1 truncate font-mono text-[11px] text-[var(--text-muted)]">
                    {ethAddress}
                  </span>
                  <button
                    type="button"
                    onClick={() => handleCopy(ethAddress, 'eth')}
                    className={`inline-flex items-center gap-1 rounded-lg px-2.5 py-1 text-[11px] font-semibold transition-all ${
                      copiedField === 'eth'
                        ? 'bg-emerald-600 text-white'
                        : 'button-secondary'
                    }`}
                  >
                    {copiedField === 'eth' ? <Check className="h-3 w-3" /> : <Copy className="h-3 w-3" />}
                    {copiedField === 'eth' ? 'OK!' : 'Copiar'}
                  </button>
                </div>
              </div>

              {/* Tether TRC-20 */}
              <div className="flex flex-col gap-1.5 rounded-xl border border-[var(--border-soft)] bg-white p-3 shadow-2xs">
                <div className="flex items-center justify-between">
                  <span className="text-xs font-bold text-[var(--text-strong)]">
                    Tether USDT (Rede TRC-20)
                  </span>
                  <span className="rounded bg-emerald-100 px-1.5 py-0.5 text-[10px] font-semibold text-emerald-800">
                    TRC-20
                  </span>
                </div>
                <div className="flex items-center gap-2">
                  <span className="flex-1 truncate font-mono text-[11px] text-[var(--text-muted)]">
                    {usdtAddress}
                  </span>
                  <button
                    type="button"
                    onClick={() => handleCopy(usdtAddress, 'usdt')}
                    className={`inline-flex items-center gap-1 rounded-lg px-2.5 py-1 text-[11px] font-semibold transition-all ${
                      copiedField === 'usdt'
                        ? 'bg-emerald-600 text-white'
                        : 'button-secondary'
                    }`}
                  >
                    {copiedField === 'usdt' ? <Check className="h-3 w-3" /> : <Copy className="h-3 w-3" />}
                    {copiedField === 'usdt' ? 'OK!' : 'Copiar'}
                  </button>
                </div>
              </div>
            </div>
          )}
        </div>

        {/* Modal Footer */}
        <div className="flex items-center justify-between border-t border-[var(--border-soft)] bg-slate-50 px-6 py-3.5">
          <span className="text-[11px] text-[var(--text-muted)]">
            Agradecemos a todos os entusiastas e desenvolvedores apoiadores! 🚀
          </span>
          <button
            type="button"
            onClick={onClose}
            className="button-secondary px-4 py-1.5"
          >
            {t('donate.close', 'Fechar')}
          </button>
        </div>
      </div>
    </div>
  );
};

/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        fluent: {
          bg: '#111113',
          card: '#1b1b1e',
          cardBorder: 'rgba(255, 255, 255, 0.08)',
          subCard: '#151518',
          accent: '#e63946',
          accentGlow: 'rgba(230, 57, 70, 0.35)',
          crimson: '#f25056',
          crimsonDark: '#991b1b',
          muted: '#8e8e93',
          text: '#f2f2f7',
          dim: '#48484a',
          hover: 'rgba(255, 255, 255, 0.05)',
          active: 'rgba(255, 255, 255, 0.08)'
        }
      },
      fontFamily: {
        sans: ['Segoe UI Variable Text', 'Segoe UI', '-apple-system', 'BlinkMacSystemFont', 'Roboto', 'sans-serif'],
      },
      boxShadow: {
        'glow-red': '0 0 20px -3px rgba(242, 80, 86, 0.4)',
        'glow-subtle': '0 0 12px 0 rgba(242, 80, 86, 0.25)',
        'fluent-card': '0 8px 24px -4px rgba(0, 0, 0, 0.45)',
      },
      animation: {
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
      }
    },
  },
  plugins: [],
}

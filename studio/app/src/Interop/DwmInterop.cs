using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace Bc250Control.Interop
{
    public static class DwmInterop
    {
        [DllImport("dwmapi.dll")]
        public static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

        public const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
        public const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
        public const int DWMWA_SYSTEMBACKDROP_TYPE = 38;

        public const int DWMWCP_ROUND = 2; // Rounded window corners in Windows 11
        public const int DWMSBT_MAINWINDOW = 2; // Mica backdrop
        public const int DWMSBT_TRANSIENTWINDOW = 3; // Acrylic backdrop

        public static void ApplyMicaAndRoundedCorners(Window window, bool isDark = true)
        {
            try
            {
                var helper = new WindowInteropHelper(window);
                IntPtr hwnd = helper.EnsureHandle();

                // 1. Dark / Light mode
                int darkMode = isDark ? 1 : 0;
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref darkMode, sizeof(int));

                // 2. Rounded corners
                int cornerPref = DWMWCP_ROUND;
                DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, ref cornerPref, sizeof(int));

                // 3. Mica backdrop (Windows 11 22H2+)
                int backdropType = DWMSBT_MAINWINDOW;
                DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, ref backdropType, sizeof(int));
            }
            catch
            {
                // Fallback gracefully on older Windows 10 versions
            }
        }
    }
}

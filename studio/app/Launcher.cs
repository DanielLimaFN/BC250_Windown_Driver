using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

namespace BC250StudioLauncher
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            try
            {
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;

                // Priority 1: Standalone bundled executable in dist-win
                string distWinExe = Path.Combine(baseDir, "dist-win", "BC250Studio.exe");
                if (File.Exists(distWinExe))
                {
                    ProcessStartInfo psi = new ProcessStartInfo
                    {
                        FileName = distWinExe,
                        WorkingDirectory = Path.Combine(baseDir, "dist-win"),
                        UseShellExecute = false
                    };
                    Process.Start(psi);
                    return;
                }

                // Priority 2: Direct Electron runtime from node_modules
                string electronExe = Path.Combine(baseDir, "node_modules", "electron", "dist", "electron.exe");
                string mainScript = Path.Combine(baseDir, "electron", "main.cjs");

                if (File.Exists(electronExe) && File.Exists(mainScript))
                {
                    ProcessStartInfo psi = new ProcessStartInfo
                    {
                        FileName = electronExe,
                        Arguments = "\"" + mainScript + "\"",
                        WorkingDirectory = baseDir,
                        UseShellExecute = false,
                        CreateNoWindow = true
                    };
                    Process.Start(psi);
                    return;
                }

                // Priority 3: Native WPF binary in bin
                string wpfExe = Path.Combine(baseDir, "bin", "BC250Control.exe");
                if (File.Exists(wpfExe))
                {
                    Process.Start(new ProcessStartInfo(wpfExe) { WorkingDirectory = baseDir });
                    return;
                }

                MessageBox.Show("Não foi possível localizar o executável do BC-250 Studio.", "BC-250 Studio", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Erro ao inicializar o BC-250 Studio: " + ex.Message, "BC-250 Studio", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}

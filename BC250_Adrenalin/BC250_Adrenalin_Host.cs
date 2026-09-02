using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Net;
using System.Threading;
using System.Windows.Forms;

namespace BC250_Adrenalin
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new AdrenalinApp());
        }
    }

    public class AdrenalinApp : ApplicationContext
    {
        private NotifyIcon trayIcon;
        private Process serverProcess;
        private Process browserProcess;
        private System.Windows.Forms.Timer watchdogTimer;
        private string appUrl = "http://127.0.0.1:8250";

        public AdrenalinApp()
        {
            InitializeTray();
            StartMicroServer();
            OpenAppWindow();

            watchdogTimer = new System.Windows.Forms.Timer();
            watchdogTimer.Interval = 3000;
            watchdogTimer.Tick += WatchdogTimer_Tick;
            watchdogTimer.Start();
        }

        private void InitializeTray()
        {
            ContextMenu contextMenu = new ContextMenu();
            
            MenuItem titleItem = new MenuItem("BC250 WinDV (Control Center)");
            titleItem.Enabled = false;
            contextMenu.MenuItems.Add(titleItem);
            contextMenu.MenuItems.Add(new MenuItem("-"));

            MenuItem openItem = new MenuItem("Abrir Painel BC250 WinDV", (s, e) => OpenAppWindow());
            openItem.DefaultItem = true;
            contextMenu.MenuItems.Add(openItem);

            MenuItem donateItem = new MenuItem("Apoiar Desenvolvimento (Donate)", (s, e) => OpenDonateUrl());
            contextMenu.MenuItems.Add(donateItem);

            MenuItem restartDriverItem = new MenuItem("Reiniciar Servico do Driver", (s, e) => RestartDriverService());
            contextMenu.MenuItems.Add(restartDriverItem);

            contextMenu.MenuItems.Add(new MenuItem("-"));
            MenuItem exitItem = new MenuItem("Sair", (s, e) => ExitApplication());
            contextMenu.MenuItems.Add(exitItem);

            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string iconPath = Path.Combine(baseDir, "logo.ico");
            string pngPath = Path.Combine(baseDir, "logo.png");

            Icon appIcon = null;
            if (File.Exists(iconPath))
            {
                try { appIcon = new Icon(iconPath); } catch { }
            }
            if (appIcon == null && File.Exists(pngPath))
            {
                try
                {
                    using (Bitmap b = new Bitmap(pngPath))
                    {
                        using (Bitmap resized = new Bitmap(b, new Size(32, 32)))
                        {
                            appIcon = Icon.FromHandle(resized.GetHicon());
                        }
                    }
                }
                catch { }
            }

            if (appIcon == null)
            {
                Bitmap bmp = new Bitmap(16, 16);
                using (Graphics g = Graphics.FromImage(bmp))
                {
                    g.Clear(Color.FromArgb(237, 28, 36));
                    using (Font font = new Font("Arial", 8, FontStyle.Bold))
                    {
                        g.DrawString("R", font, Brushes.White, new PointF(2, 1));
                    }
                }
                appIcon = Icon.FromHandle(bmp.GetHicon());
            }

            trayIcon = new NotifyIcon();
            trayIcon.Text = "BC250 WinDV (GC 10.1.3)";
            trayIcon.Icon = appIcon;
            trayIcon.ContextMenu = contextMenu;
            trayIcon.Visible = true;
            trayIcon.DoubleClick += (s, e) => OpenAppWindow();

            trayIcon.ShowBalloonTip(3000, "BC250 WinDV", "Painel de Gestao do Silicio ativo na bandeja.", ToolTipIcon.Info);
        }

        private void StartMicroServer()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string pyServer = Path.Combine(baseDir, "server.py");
            string jsServer = Path.Combine(baseDir, "server.js");

            try
            {
                // Test if server is already running
                HttpWebRequest req = (HttpWebRequest)WebRequest.Create(appUrl + "/api/status");
                req.Timeout = 800;
                using (HttpWebResponse resp = (HttpWebResponse)req.GetResponse())
                {
                    if (resp.StatusCode == HttpStatusCode.OK) return;
                }
            }
            catch { }

            try
            {
                ProcessStartInfo psi = new ProcessStartInfo();
                if (File.Exists(pyServer))
                {
                    psi.FileName = "python.exe";
                    psi.Arguments = "\"" + pyServer + "\"";
                }
                else if (File.Exists(jsServer))
                {
                    psi.FileName = "node.exe";
                    psi.Arguments = "\"" + jsServer + "\"";
                }
                else
                {
                    return;
                }

                psi.WorkingDirectory = baseDir;
                psi.CreateNoWindow = true;
                psi.UseShellExecute = false;
                serverProcess = Process.Start(psi);
                Thread.Sleep(500);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Erro ao iniciar micro-servidor: " + ex.Message, "BC-250 Adrenalin", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        private void OpenAppWindow()
        {
            try
            {
                if (browserProcess != null && !browserProcess.HasExited)
                {
                    // Focus existing process if possible
                    return;
                }

                string edgePath = @"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe";
                if (!File.Exists(edgePath))
                {
                    edgePath = @"C:\Program Files\Microsoft\Edge\Application\msedge.exe";
                }

                string userData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BC250_Adrenalin_Web");

                if (File.Exists(edgePath))
                {
                    ProcessStartInfo psi = new ProcessStartInfo();
                    psi.FileName = edgePath;
                    psi.Arguments = string.Format("--app=\"{0}\" --user-data-dir=\"{1}\" --window-size=1200,820", appUrl, userData);
                    browserProcess = Process.Start(psi);
                }
                else
                {
                    Process.Start(appUrl);
                }
            }
            catch (Exception)
            {
                Process.Start(appUrl);
            }
        }

        private void OpenDonateUrl()
        {
            OpenAppWindow();
        }

        private void RestartDriverService()
        {
            try
            {
                HttpWebRequest req = (HttpWebRequest)WebRequest.Create(appUrl + "/api/service/restart");
                req.Method = "POST";
                req.ContentLength = 0;
                using (HttpWebResponse resp = (HttpWebResponse)req.GetResponse()) { }
                trayIcon.ShowBalloonTip(2000, "Driver BC-250", "Servico de kernel reiniciado.", ToolTipIcon.Info);
            }
            catch { }
        }

        private void WatchdogTimer_Tick(object sender, EventArgs e)
        {
            // Keep background server alive
            if (serverProcess != null && serverProcess.HasExited)
            {
                StartMicroServer();
            }
        }

        private void ExitApplication()
        {
            if (trayIcon != null)
            {
                trayIcon.Visible = false;
                trayIcon.Dispose();
            }

            if (serverProcess != null && !serverProcess.HasExited)
            {
                try { serverProcess.Kill(); } catch { }
            }

            if (browserProcess != null && !browserProcess.HasExited)
            {
                try { browserProcess.Kill(); } catch { }
            }

            Application.Exit();
        }
    }
}

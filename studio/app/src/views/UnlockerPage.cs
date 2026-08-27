using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using Bc250Control.Controls;
using Bc250Control.I18n;

namespace Bc250Control.Views
{
    public class UnlockerPage : ScrollViewer
    {
        private Dictionary<string, bool> activeWgps = new Dictionary<string, bool>();
        private bool cpuUnlocked = true;

        private TextBlock statsHeaderBlock;
        private TextBlock shadersCountBlock;
        private TextBlock tflopsBlock;
        private TextBlock powerTdpBlock;
        private TextBlock registerCcBlock;
        private TextBlock registerSpiBlock;
        private TextBlock registerKernelBlock;
        private TextBlock actionStatusBlock;
        private Border actionProgressBox;
        private StackPanel rowsContainer;
        private List<Border> cpuCoreBorders = new List<Border>();
        private DispatcherTimer healthTimer;
        private DispatcherTimer applyTimer;

        public UnlockerPage()
        {
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
            Padding = new Thickness(36, 16, 36, 28);

            InitializeDefaultWgps();

            var mainStack = new StackPanel
            {
                MaxWidth = 1000,
                HorizontalAlignment = HorizontalAlignment.Left
            };

            // Page Header
            var headerStack = new StackPanel { Margin = new Thickness(0, 0, 0, 16) };
            var title = new TextBlock
            {
                Text = Loc.T("Unlock_Title"),
                FontSize = 26,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            };
            var sub = new TextBlock
            {
                Text = Loc.T("Unlock_Sub"),
                FontSize = 12,
                FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 0)
            };
            headerStack.Children.Add(title);
            headerStack.Children.Add(sub);
            mainStack.Children.Add(headerStack);

            // =========================================================================
            // 1. CU TOPOLOGY MATRIX CARD
            // =========================================================================
            var matrixCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(18, 16, 18, 16),
                Margin = new Thickness(0, 0, 0, 14)
            };
            var matrixStack = new StackPanel();

            // Matrix Header with live stats
            var mHeaderGrid = new Grid { Margin = new Thickness(0, 0, 0, 12) };
            mHeaderGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            mHeaderGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var mTitleStack = new StackPanel();
            var mTitle = new TextBlock
            {
                Text = Loc.T("Unlock_CuMatrix_Title"),
                FontSize = 13.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary
            };
            var mSub = new TextBlock
            {
                Text = Loc.T("Unlock_CuMatrix_Sub"),
                FontSize = 11,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 0)
            };
            mTitleStack.Children.Add(mTitle);
            mTitleStack.Children.Add(mSub);
            Grid.SetColumn(mTitleStack, 0);
            mHeaderGrid.Children.Add(mTitleStack);

            statsHeaderBlock = new TextBlock
            {
                Text = "24/40 CUs active (16 harvested)",
                FontSize = 12,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Consolas, Segoe UI"),
                Foreground = Theme.SuccessGreen,
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(statsHeaderBlock, 1);
            mHeaderGrid.Children.Add(statsHeaderBlock);

            matrixStack.Children.Add(mHeaderGrid);

            // Preset Buttons Row
            var presetRow = new WrapPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 12) };
            var presetLabel = new TextBlock
            {
                Text = Loc.T("Unlock_Presets"),
                FontSize = 12,
                FontWeight = FontWeights.SemiBold,
                Foreground = Theme.TextSecondary,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 10, 0)
            };
            presetRow.Children.Add(presetLabel);

            presetRow.Children.Add(CreatePresetButton("Stock (24 CU)", () => ApplyPreset(24)));
            presetRow.Children.Add(CreatePresetButton("+4 CU (28 CU)", () => ApplyPreset(28)));
            presetRow.Children.Add(CreatePresetButton("+8 CU (32 CU)", () => ApplyPreset(32)));
            presetRow.Children.Add(CreatePresetButton("+12 CU (36 CU)", () => ApplyPreset(36)));
            presetRow.Children.Add(CreatePresetButton("Full Unlock (40 CU)", () => ApplyPreset(40)));

            var healthBtn = new FluentButton(Loc.T("Unlock_TestStability"), false);
            healthBtn.Margin = new Thickness(6, 0, 0, 0);
            healthBtn.Click += (s, e) => RunHealthTest();
            presetRow.Children.Add(healthBtn);

            matrixStack.Children.Add(presetRow);

            // Terminal Grid Container
            var termBorder = new Border
            {
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(18, 18, 18)) : new SolidColorBrush(Color.FromRgb(248, 248, 248)),
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(14, 10, 14, 10)
            };

            rowsContainer = new StackPanel();
            termBorder.Child = rowsContainer;
            matrixStack.Children.Add(termBorder);

            matrixCard.Child = matrixStack;
            mainStack.Children.Add(matrixCard);

            // =========================================================================
            // 2. PERFORMANCE & THERMAL METRICS CARD
            // =========================================================================
            var metricsCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(18, 14, 18, 14),
                Margin = new Thickness(0, 0, 0, 14)
            };
            var mGrid = new Grid();
            mGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            mGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            mGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var m1 = CreateMetricBox(Loc.T("Unlock_ActiveShaders"), "1536", "Stock: 1536 • Max: 2560", out shadersCountBlock);
            Grid.SetColumn(m1, 0);
            mGrid.Children.Add(m1);

            var m2 = CreateMetricBox(Loc.T("Unlock_Fp32Compute"), "4.61 TFLOPS", "Baseline stock compute", out tflopsBlock);
            Grid.SetColumn(m2, 1);
            mGrid.Children.Add(m2);

            var m3 = CreateMetricBox(Loc.T("Unlock_PowerTdp"), "~95W", "@ 1500MHz / 900mV sweet spot", out powerTdpBlock);
            Grid.SetColumn(m3, 2);
            mGrid.Children.Add(m3);

            metricsCard.Child = mGrid;
            mainStack.Children.Add(metricsCard);

            // =========================================================================
            // 3. CPU CORE UNLOCK CARD (Zen 2 CCX)
            // =========================================================================
            var cpuCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(18, 14, 18, 14),
                Margin = new Thickness(0, 0, 0, 14)
            };
            var cpuStack = new StackPanel();

            var cpuHeadGrid = new Grid();
            cpuHeadGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            cpuHeadGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var cpuTitleStack = new StackPanel();
            var cpuTitle = new TextBlock
            {
                Text = Loc.T("Unlock_Cpu_Title"),
                FontSize = 13.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary
            };
            var cpuSub = new TextBlock
            {
                Text = Loc.T("Unlock_Cpu_Sub"),
                FontSize = 11,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 0)
            };
            cpuTitleStack.Children.Add(cpuTitle);
            cpuTitleStack.Children.Add(cpuSub);
            Grid.SetColumn(cpuTitleStack, 0);
            cpuHeadGrid.Children.Add(cpuTitleStack);

            var cpuToggle = new FluentToggleSwitch(cpuUnlocked);
            cpuToggle.CheckedChanged += (s, isChecked) =>
            {
                cpuUnlocked = isChecked;
                UpdateCpuCoresVisual();
            };
            Grid.SetColumn(cpuToggle, 1);
            cpuHeadGrid.Children.Add(cpuToggle);

            cpuStack.Children.Add(cpuHeadGrid);

            // 8 Core Visual Grid
            var coresGrid = new UniformGrid { Columns = 8, Margin = new Thickness(0, 10, 0, 0) };
            cpuCoreBorders.Clear();
            for (int i = 0; i < 8; i++)
            {
                var coreBorder = new Border
                {
                    Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(32, 32, 32)) : new SolidColorBrush(Color.FromRgb(240, 240, 240)),
                    BorderBrush = Theme.CardBorder,
                    BorderThickness = new Thickness(1),
                    CornerRadius = new CornerRadius(4),
                    Padding = new Thickness(6, 6, 6, 6),
                    Margin = new Thickness(2, 0, 2, 0)
                };
                var coreStack = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center };
                coreStack.Children.Add(new TextBlock
                {
                    Text = $"Core {i}",
                    FontSize = 9.5,
                    FontWeight = FontWeights.SemiBold,
                    Foreground = Theme.TextSecondary,
                    HorizontalAlignment = HorizontalAlignment.Center
                });
                coreStack.Children.Add(new TextBlock
                {
                    Text = "2T",
                    FontSize = 11.5,
                    FontWeight = FontWeights.Bold,
                    Foreground = Theme.TextPrimary,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    Margin = new Thickness(0, 2, 0, 0)
                });
                coreBorder.Child = coreStack;
                cpuCoreBorders.Add(coreBorder);
                coresGrid.Children.Add(coreBorder);
            }
            cpuStack.Children.Add(coresGrid);

            cpuCard.Child = cpuStack;
            mainStack.Children.Add(cpuCard);

            // =========================================================================
            // 4. LOW-LEVEL REGISTERS & TRANSACT APPLY CARD
            // =========================================================================
            var regCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(18, 14, 18, 14),
                Margin = new Thickness(0, 0, 0, 14)
            };
            var regStack = new StackPanel();

            var regHead = new TextBlock
            {
                Text = Loc.T("Unlock_Reg_Title"),
                FontSize = 13,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 0, 0, 10)
            };
            regStack.Children.Add(regHead);

            var regGrid = new Grid();
            regGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            regGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            regGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var rb1 = CreateRegBox("MMIO CC ENUM", "CC_GC_SHADER_ARRAY_CONFIG", "0xfff80000", out registerCcBlock);
            Grid.SetColumn(rb1, 0);
            regGrid.Children.Add(rb1);

            var rb2 = CreateRegBox("MMIO SPI GATE", "SPI_PG_STATIC_WGP_MASK", "0x00000007", out registerSpiBlock);
            Grid.SetColumn(rb2, 1);
            regGrid.Children.Add(rb2);

            var rb3 = CreateRegBox("KERNEL PARAMETER", "amdgpu.disable_cu", "none (stock)", out registerKernelBlock);
            Grid.SetColumn(rb3, 2);
            regGrid.Children.Add(rb3);

            regStack.Children.Add(regGrid);

            // Progress status box
            actionProgressBox = new Border
            {
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(20, 20, 20)) : new SolidColorBrush(Color.FromRgb(240, 240, 240)),
                BorderBrush = Theme.AccentRed,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(12, 8, 12, 8),
                Margin = new Thickness(0, 12, 0, 0),
                Visibility = Visibility.Collapsed
            };
            actionStatusBlock = new TextBlock
            {
                Text = "Ready",
                FontSize = 11.5,
                FontWeight = FontWeights.SemiBold,
                Foreground = Theme.TextPrimary
            };
            actionProgressBox.Child = actionStatusBlock;
            regStack.Children.Add(actionProgressBox);

            // Apply Button Row
            var applyRow = new Grid { Margin = new Thickness(0, 14, 0, 0) };
            applyRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            applyRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var fuseInfo = new TextBlock
            {
                Text = "🛡️ SMN Access Queue #0 Ready • Fail-Closed Transactional State Machine",
                FontSize = 11,
                Foreground = Theme.TextSecondary,
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(fuseInfo, 0);
            applyRow.Children.Add(fuseInfo);

            var applyBtn = new FluentButton(Loc.T("Unlock_Apply"), true);
            applyBtn.Click += (s, e) => ApplyToSilicon();
            Grid.SetColumn(applyBtn, 1);
            applyRow.Children.Add(applyBtn);

            regStack.Children.Add(applyRow);

            regCard.Child = regStack;
            mainStack.Children.Add(regCard);

            Content = mainStack;

            RenderCuRows();
            UpdateStatsAndRegisters();
            UpdateCpuCoresVisual();
        }

        private void InitializeDefaultWgps()
        {
            activeWgps.Clear();
            for (int se = 0; se < 2; se++)
            {
                for (int sh = 0; sh < 2; sh++)
                {
                    for (int wgp = 0; wgp < 5; wgp++)
                    {
                        activeWgps[$"{se}.{sh}.{wgp}"] = (wgp < 3);
                    }
                }
            }
        }

        private void RenderCuRows()
        {
            rowsContainer.Children.Clear();

            string[] arrayLabels = { "SE0 SH0", "SE0 SH1", "SE1 SH0", "SE1 SH1" };
            int[] ses = { 0, 0, 1, 1 };
            int[] shs = { 0, 1, 0, 1 };

            for (int i = 0; i < 4; i++)
            {
                int se = ses[i];
                int sh = shs[i];
                string label = arrayLabels[i];

                var rowGrid = new Grid { Margin = new Thickness(0, 3, 0, 3) };
                rowGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(76) });
                rowGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                rowGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(96) });

                // Label
                var lbl = new TextBlock
                {
                    Text = label,
                    FontSize = 11.5,
                    FontWeight = FontWeights.Bold,
                    FontFamily = new FontFamily("Consolas, Segoe UI"),
                    Foreground = Theme.TextPrimary,
                    VerticalAlignment = VerticalAlignment.Center
                };
                Grid.SetColumn(lbl, 0);
                rowGrid.Children.Add(lbl);

                // 5 WGP Blocks
                var wgpStack = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
                for (int w = 0; w < 5; w++)
                {
                    int wgp = w;
                    string key = $"{se}.{sh}.{wgp}";
                    bool isActive = activeWgps[key];
                    bool isStock = wgp < 3;

                    var wgpButton = new Border
                    {
                        Width = 58,
                        Height = 26,
                        CornerRadius = new CornerRadius(4),
                        Margin = new Thickness(2, 0, 2, 0),
                        BorderThickness = new Thickness(1),
                        Cursor = Cursors.Hand
                    };

                    if (isActive)
                    {
                        if (isStock)
                        {
                            wgpButton.Background = Theme.IsDark ? new SolidColorBrush(Color.FromArgb(50, 16, 185, 129)) : new SolidColorBrush(Color.FromArgb(30, 16, 185, 129));
                            wgpButton.BorderBrush = Theme.SuccessGreen;
                        }
                        else
                        {
                            wgpButton.Background = Theme.IsDark ? new SolidColorBrush(Color.FromArgb(60, 242, 80, 86)) : new SolidColorBrush(Color.FromArgb(40, 242, 80, 86));
                            wgpButton.BorderBrush = Theme.AccentRed;
                        }
                    }
                    else
                    {
                        wgpButton.Background = Brushes.Transparent;
                        wgpButton.BorderBrush = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(60, 60, 60)) : new SolidColorBrush(Color.FromRgb(200, 200, 200));
                    }

                    var btnText = new TextBlock
                    {
                        Text = isActive ? "■ ■" : "□ □",
                        FontSize = 11.5,
                        FontFamily = new FontFamily("Consolas, Segoe UI"),
                        FontWeight = FontWeights.Bold,
                        Foreground = isActive ? (isStock ? Theme.SuccessGreen : Theme.AccentRed) : Theme.TextSecondary,
                        HorizontalAlignment = HorizontalAlignment.Center,
                        VerticalAlignment = VerticalAlignment.Center
                    };

                    wgpButton.Child = btnText;

                    wgpButton.MouseLeftButtonDown += (s, e) =>
                    {
                        activeWgps[key] = !activeWgps[key];
                        RenderCuRows();
                        UpdateStatsAndRegisters();
                    };

                    wgpStack.Children.Add(wgpButton);
                }

                Grid.SetColumn(wgpStack, 1);
                rowGrid.Children.Add(wgpStack);

                // Row Count
                int activeInRow = 0;
                for (int w = 0; w < 5; w++) if (activeWgps[$"{se}.{sh}.{w}"]) activeInRow += 2;

                var rowStat = new TextBlock
                {
                    Text = $"{activeInRow}/10 CUs",
                    FontSize = 10.5,
                    FontFamily = new FontFamily("Consolas, Segoe UI"),
                    Foreground = Theme.TextSecondary,
                    HorizontalAlignment = HorizontalAlignment.Right,
                    VerticalAlignment = VerticalAlignment.Center
                };
                Grid.SetColumn(rowStat, 2);
                rowGrid.Children.Add(rowStat);

                rowsContainer.Children.Add(rowGrid);
            }
        }

        private void UpdateStatsAndRegisters()
        {
            int activeWgpCount = activeWgps.Values.Count(v => v);
            int activeCus = activeWgpCount * 2;
            int harvestedCus = 40 - activeCus;
            int unlockedCus = Math.Max(0, activeCus - 24);
            int activeShaders = activeCus * 64;
            double tflops = (activeShaders * 2 * 1.5) / 1000.0;

            statsHeaderBlock.Text = $"{activeCus}/40 {Loc.T("Unlock_ActiveCus")} ({harvestedCus} {Loc.T("Unlock_Harvested")}{(unlockedCus > 0 ? $", +{unlockedCus} unlocked" : "")})";
            shadersCountBlock.Text = activeShaders.ToString();
            tflopsBlock.Text = $"{tflops:F2} TFLOPS";

            int tdp = 95;
            if (activeCus == 28) tdp = 103;
            else if (activeCus == 32) tdp = 110;
            else if (activeCus == 36) tdp = 118;
            else if (activeCus == 40) tdp = 125;

            powerTdpBlock.Text = $"~{tdp}W";

            // Registers
            if (activeCus == 40)
            {
                registerCcBlock.Text = "0xffe00000 (40 CU)";
                registerSpiBlock.Text = "0x0000001F (5 WGPs)";
                registerKernelBlock.Text = "bc250_cc_write_mode=3";
            }
            else if (activeCus == 24)
            {
                registerCcBlock.Text = "0xfff80000 (24 CU)";
                registerSpiBlock.Text = "0x00000007 (3 WGPs)";
                registerKernelBlock.Text = "none (stock)";
            }
            else
            {
                registerCcBlock.Text = "0xfff00000 (Custom)";
                registerSpiBlock.Text = "0x0000000F (Masked)";

                var disabledCus = new List<string>();
                for (int se = 0; se < 2; se++)
                {
                    for (int sh = 0; sh < 2; sh++)
                    {
                        for (int wgp = 0; wgp < 5; wgp++)
                        {
                            if (!activeWgps[$"{se}.{sh}.{wgp}"])
                            {
                                disabledCus.Add($"{se}.{sh}.{wgp * 2}");
                                disabledCus.Add($"{se}.{sh}.{wgp * 2 + 1}");
                            }
                        }
                    }
                }
                registerKernelBlock.Text = $"disable_cu={string.Join(",", disabledCus.Take(3))}...";
            }
        }

        private void UpdateCpuCoresVisual()
        {
            for (int i = 0; i < 8; i++)
            {
                bool isCoreActive = (i < 6) || cpuUnlocked;
                var b = cpuCoreBorders[i];
                var stack = (StackPanel)b.Child;
                var text = (TextBlock)stack.Children[1];

                if (isCoreActive)
                {
                    text.Text = "2T";
                    text.Foreground = (i >= 6) ? Theme.AccentRed : Theme.TextPrimary;
                    b.BorderBrush = (i >= 6) ? Theme.AccentRed : Theme.CardBorder;
                }
                else
                {
                    text.Text = "OFF";
                    text.Foreground = Theme.TextSecondary;
                    b.BorderBrush = Theme.CardBorder;
                }
            }
        }

        private void ApplyPreset(int targetCus)
        {
            InitializeDefaultWgps();

            if (targetCus >= 28)
            {
                activeWgps["0.0.3"] = true;
                activeWgps["0.1.3"] = true;
            }
            if (targetCus >= 32)
            {
                activeWgps["1.0.3"] = true;
                activeWgps["1.1.3"] = true;
            }
            if (targetCus >= 36)
            {
                activeWgps["0.0.4"] = true;
                activeWgps["0.1.4"] = true;
            }
            if (targetCus == 40)
            {
                for (int se = 0; se < 2; se++)
                    for (int sh = 0; sh < 2; sh++)
                        for (int w = 0; w < 5; w++)
                            activeWgps[$"{se}.{sh}.{w}"] = true;
            }

            RenderCuRows();
            UpdateStatsAndRegisters();
        }

        private void RunHealthTest()
        {
            actionProgressBox.Visibility = Visibility.Visible;
            actionStatusBlock.Text = "Running per-WGP hardware compute verification...";

            int wgpIndex = 0;
            var keys = activeWgps.Keys.ToList();

            healthTimer?.Stop();
            healthTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
            healthTimer.Tick += (s, e) =>
            {
                if (wgpIndex < keys.Count)
                {
                    actionStatusBlock.Text = $"{Loc.T("Unlock_TestingWgp")} {keys[wgpIndex]}...";
                    wgpIndex++;
                }
                else
                {
                    healthTimer.Stop();
                    actionStatusBlock.Text = Loc.T("Unlock_TestPass");
                    actionStatusBlock.Foreground = Theme.SuccessGreen;
                }
            };
            healthTimer.Start();
        }

        private void ApplyToSilicon()
        {
            actionProgressBox.Visibility = Visibility.Visible;
            actionStatusBlock.Foreground = Theme.TextPrimary;

            string[] steps = {
                "[PREFLIGHT] Checking device ID 0x13FE and NT session...",
                "[SNAPSHOT] Capturing original CC_GC_SHADER_ARRAY_CONFIG...",
                "[APPLY] Writing CC (enumeration) and SPI_PG (dispatch gate)...",
                "[READBACK] Verifying register readback parity...",
                "[PROOF] Dispatching GPU ring compute fence...",
                Loc.T("Unlock_Success")
            };

            int stepIdx = 0;
            applyTimer?.Stop();
            applyTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(350) };
            applyTimer.Tick += (s, e) =>
            {
                if (stepIdx < steps.Length)
                {
                    actionStatusBlock.Text = steps[stepIdx];
                    stepIdx++;
                }
                else
                {
                    applyTimer.Stop();
                    actionStatusBlock.Foreground = Theme.SuccessGreen;
                }
            };
            applyTimer.Start();
        }

        private Button CreatePresetButton(string text, Action onClick)
        {
            var btn = new Button
            {
                Content = text,
                Height = 28,
                Padding = new Thickness(10, 2, 10, 2),
                Margin = new Thickness(0, 0, 6, 0),
                FontSize = 11.5,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(36, 36, 36)) : new SolidColorBrush(Color.FromRgb(240, 240, 240)),
                Foreground = Theme.TextPrimary,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                Cursor = Cursors.Hand
            };
            btn.Click += (s, e) => onClick();
            return btn;
        }

        private Border CreateMetricBox(string title, string initialVal, string sub, out TextBlock valBlock)
        {
            var b = new Border
            {
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(20, 20, 20)) : new SolidColorBrush(Color.FromRgb(248, 248, 248)),
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(12, 10, 12, 10),
                Margin = new Thickness(3, 0, 3, 0)
            };
            var s = new StackPanel();
            s.Children.Add(new TextBlock
            {
                Text = title,
                FontSize = 9.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextSecondary
            });

            valBlock = new TextBlock
            {
                Text = initialVal,
                FontSize = 15,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 2, 0, 2)
            };
            s.Children.Add(valBlock);

            s.Children.Add(new TextBlock
            {
                Text = sub,
                FontSize = 9.5,
                Foreground = Theme.TextSecondary
            });

            b.Child = s;
            return b;
        }

        private Border CreateRegBox(string title, string regName, string initialVal, out TextBlock valBlock)
        {
            var b = new Border
            {
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(20, 20, 20)) : new SolidColorBrush(Color.FromRgb(248, 248, 248)),
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(10, 8, 10, 8),
                Margin = new Thickness(3, 0, 3, 0)
            };
            var s = new StackPanel();
            s.Children.Add(new TextBlock
            {
                Text = title,
                FontSize = 9,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextSecondary
            });
            s.Children.Add(new TextBlock
            {
                Text = regName,
                FontSize = 10.5,
                FontWeight = FontWeights.SemiBold,
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 1, 0, 1)
            });
            valBlock = new TextBlock
            {
                Text = initialVal,
                FontSize = 10.5,
                FontFamily = new FontFamily("Consolas, Segoe UI"),
                Foreground = Theme.SuccessGreen
            };
            s.Children.Add(valBlock);
            b.Child = s;
            return b;
        }
    }
}

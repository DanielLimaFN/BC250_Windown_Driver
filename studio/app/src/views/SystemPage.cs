using System;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using Bc250Control.Controls;
using Bc250Control.I18n;

namespace Bc250Control.Views
{
    public class SystemPage : ScrollViewer
    {
        public Action<string> OnNavigate { get; set; }

        private Border donateDrawer;
        private bool isDonateExpanded = false;
        private string activeDonateTab = "pix";
        private StackPanel donateContentStack;

        // Sample donation addresses
        private const string PixKey = "bc250.driver.lab@gmail.com";
        private const string BtcAddress = "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh";
        private const string EthAddress = "0x71C83d5483981881855aD8aEcB1d14878aE7F302";
        private const string UsdtTrc20 = "TYDzsYUEpvnYmQk4zGP9sWWcTEd3TgKa2c";
        private const string KofiUrl = "https://ko-fi.com";

        public SystemPage(Action<string> navigateAction)
        {
            OnNavigate = navigateAction;
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
            Padding = new Thickness(36, 16, 36, 28);

            var mainStack = new StackPanel
            {
                MaxWidth = 1000,
                HorizontalAlignment = HorizontalAlignment.Left
            };

            // Page Header Title
            var title = new TextBlock
            {
                Text = Loc.T("Sys_Title"),
                FontSize = 28,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 0, 0, 18)
            };
            mainStack.Children.Add(title);

            // =========================================================================
            // 1. HERO CARD (Windows 11 Settings Top Device Card)
            // =========================================================================
            var heroCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(20, 16, 20, 16),
                Margin = new Thickness(0, 0, 0, 12)
            };

            var heroGrid = new Grid();
            heroGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(96) });
            heroGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            heroGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            // Device Thumbnail Badge
            var thumbBorder = new Border
            {
                Width = 80,
                Height = 56,
                CornerRadius = new CornerRadius(6),
                Background = new LinearGradientBrush(
                    Color.FromRgb(0, 120, 215),
                    Color.FromRgb(0, 45, 120),
                    new Point(0, 0),
                    new Point(1, 1)
                ),
                BorderBrush = new SolidColorBrush(Color.FromArgb(40, 255, 255, 255)),
                BorderThickness = new Thickness(1),
                VerticalAlignment = VerticalAlignment.Center,
                HorizontalAlignment = HorizontalAlignment.Left
            };
            var thumbContent = new StackPanel
            {
                VerticalAlignment = VerticalAlignment.Center,
                HorizontalAlignment = HorizontalAlignment.Center
            };
            thumbContent.Children.Add(new TextBlock
            {
                Text = "BC-250",
                FontSize = 12,
                FontWeight = FontWeights.Bold,
                Foreground = Brushes.White,
                HorizontalAlignment = HorizontalAlignment.Center
            });
            thumbContent.Children.Add(new TextBlock
            {
                Text = "APU HOST",
                FontSize = 8.5,
                FontWeight = FontWeights.SemiBold,
                Foreground = new SolidColorBrush(Color.FromArgb(200, 255, 255, 255)),
                HorizontalAlignment = HorizontalAlignment.Center
            });
            thumbBorder.Child = thumbContent;
            Grid.SetColumn(thumbBorder, 0);
            heroGrid.Children.Add(thumbBorder);

            // Device Info
            var infoStack = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(4, 0, 12, 0) };
            var devName = new TextBlock
            {
                Text = Loc.T("Sys_DevName"),
                FontSize = 15,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            };
            var devSub = new TextBlock
            {
                Text = Loc.T("Sys_DevSub"),
                FontSize = 11.5,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 2)
            };
            var renameLink = new TextBlock
            {
                Text = Loc.T("Sys_DevSpecs"),
                FontSize = 11,
                Foreground = Theme.Accent,
                Cursor = Cursors.Hand,
                FontWeight = FontWeights.SemiBold
            };
            renameLink.MouseLeftButtonDown += (s, e) => OnNavigate?.Invoke("unlocker");

            infoStack.Children.Add(devName);
            infoStack.Children.Add(devSub);
            infoStack.Children.Add(renameLink);
            Grid.SetColumn(infoStack, 1);
            heroGrid.Children.Add(infoStack);

            // Status Chips
            var chipsStack = new WrapPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
            chipsStack.Children.Add(CreateStatusChip("📦", "WDDM 1.0.4.0", Loc.T("Sys_Chip_Driver")));
            chipsStack.Children.Add(CreateStatusChip("🎮", "24/40 CUs", Loc.T("Sys_Chip_Cus")));
            chipsStack.Children.Add(CreateStatusChip("🧠", "8C / 16T", Loc.T("Sys_Chip_Cpu")));

            Grid.SetColumn(chipsStack, 2);
            heroGrid.Children.Add(chipsStack);

            heroCard.Child = heroGrid;
            mainStack.Children.Add(heroCard);

            // =========================================================================
            // 2. DONATE / BUY ME A COFFEE CARD (☕ Pix, Cartões, Criptomoedas)
            // =========================================================================
            var donateCard = new Border
            {
                Background = Theme.IsDark
                    ? new SolidColorBrush(Color.FromRgb(38, 35, 30)) // Subtle warm dark gold
                    : new SolidColorBrush(Color.FromRgb(255, 252, 245)),
                BorderBrush = Theme.IsDark
                    ? new SolidColorBrush(Color.FromArgb(90, 255, 185, 0))
                    : new SolidColorBrush(Color.FromArgb(120, 255, 185, 0)),
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(18, 14, 18, 14),
                Margin = new Thickness(0, 0, 0, 16)
            };

            var donateOuterStack = new StackPanel();

            // Donate Header Row
            var donateHeadGrid = new Grid();
            donateHeadGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(38) });
            donateHeadGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            donateHeadGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var coffeeIcon = new Border
            {
                Width = 30,
                Height = 30,
                CornerRadius = new CornerRadius(15),
                Background = new SolidColorBrush(Color.FromArgb(40, 255, 185, 0)),
                Child = new TextBlock
                {
                    Text = "☕",
                    FontSize = 15,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                }
            };
            Grid.SetColumn(coffeeIcon, 0);
            donateHeadGrid.Children.Add(coffeeIcon);

            var donateTextStack = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            donateTextStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Donate_Title"),
                FontSize = 13.5,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            });
            donateTextStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Donate_Sub"),
                FontSize = 11,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 1, 0, 0)
            });
            Grid.SetColumn(donateTextStack, 1);
            donateHeadGrid.Children.Add(donateTextStack);

            // Expand / Toggle Button
            var donateToggleBtn = new Button
            {
                Content = isDonateExpanded ? "▲" : Loc.T("Donate_Btn") + " ☕",
                Height = 30,
                Padding = new Thickness(14, 2, 14, 2),
                FontSize = 11.5,
                FontWeight = FontWeights.SemiBold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Background = new SolidColorBrush(Color.FromRgb(255, 185, 0)),
                Foreground = Brushes.Black,
                BorderThickness = new Thickness(0),
                Cursor = Cursors.Hand
            };

            donateToggleBtn.Click += (s, e) =>
            {
                isDonateExpanded = !isDonateExpanded;
                donateDrawer.Visibility = isDonateExpanded ? Visibility.Visible : Visibility.Collapsed;
                donateToggleBtn.Content = isDonateExpanded ? "▲" : Loc.T("Donate_Btn") + " ☕";
            };

            Grid.SetColumn(donateToggleBtn, 2);
            donateHeadGrid.Children.Add(donateToggleBtn);

            donateOuterStack.Children.Add(donateHeadGrid);

            // =========================================================================
            // DONATE EXPANDABLE DRAWER (Tabs: Pix, Cartões, Cripto)
            // =========================================================================
            donateDrawer = new Border
            {
                Margin = new Thickness(0, 14, 0, 0),
                Padding = new Thickness(14, 12, 14, 12),
                CornerRadius = new CornerRadius(6),
                Background = Theme.IsDark
                    ? new SolidColorBrush(Color.FromRgb(28, 26, 23))
                    : new SolidColorBrush(Color.FromRgb(250, 247, 240)),
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                Visibility = Visibility.Collapsed
            };

            var drawerStack = new StackPanel();

            // Tabs Selector Header
            var tabsRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 12) };
            tabsRow.Children.Add(CreateDonateTabButton(Loc.T("Donate_Pix"), "pix"));
            tabsRow.Children.Add(CreateDonateTabButton(Loc.T("Donate_Cards"), "cards"));
            tabsRow.Children.Add(CreateDonateTabButton(Loc.T("Donate_Crypto"), "crypto"));
            drawerStack.Children.Add(tabsRow);

            // Dynamic Content Area
            donateContentStack = new StackPanel();
            RenderDonateTabContent("pix");
            drawerStack.Children.Add(donateContentStack);

            donateDrawer.Child = drawerStack;
            donateOuterStack.Children.Add(donateDrawer);

            donateCard.Child = donateOuterStack;
            mainStack.Children.Add(donateCard);

            // =========================================================================
            // 3. SETTINGS CARDS LIST (Grouped List identical to Windows 11 Settings)
            // =========================================================================
            mainStack.Children.Add(new FluentSettingsCard(
                "🎮",
                Loc.T("Sys_Card_Gpu_Title"),
                Loc.T("Sys_Card_Gpu_Sub"),
                null,
                () => OnNavigate?.Invoke("performance")
            ));

            mainStack.Children.Add(new FluentSettingsCard(
                "🔓",
                Loc.T("Sys_Card_Unlocker_Title"),
                Loc.T("Sys_Card_Unlocker_Sub"),
                null,
                () => OnNavigate?.Invoke("unlocker")
            ));

            mainStack.Children.Add(new FluentSettingsCard(
                "📦",
                Loc.T("Sys_Card_Driver_Title"),
                Loc.T("Sys_Card_Driver_Sub"),
                null,
                () => OnNavigate?.Invoke("driver")
            ));

            mainStack.Children.Add(new FluentSettingsCard(
                "📊",
                Loc.T("Sys_Card_Telemetry_Title"),
                Loc.T("Sys_Card_Telemetry_Sub"),
                null,
                () => OnNavigate?.Invoke("telemetry")
            ));

            mainStack.Children.Add(new FluentSettingsCard(
                "⚡",
                Loc.T("Sys_Card_Power_Title"),
                Loc.T("Sys_Card_Power_Sub"),
                null,
                () => OnNavigate?.Invoke("performance")
            ));

            mainStack.Children.Add(new FluentSettingsCard(
                "⚙️",
                Loc.T("Sys_Card_Settings_Title"),
                Loc.T("Sys_Card_Settings_Sub"),
                null,
                () => OnNavigate?.Invoke("settings")
            ));

            Content = mainStack;
        }

        private Button CreateDonateTabButton(string label, string tabKey)
        {
            var btn = new Button
            {
                Content = label,
                Height = 28,
                Padding = new Thickness(12, 2, 12, 2),
                Margin = new Thickness(0, 0, 6, 0),
                FontSize = 11.5,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                FontWeight = (activeDonateTab == tabKey) ? FontWeights.Bold : FontWeights.Normal,
                Background = (activeDonateTab == tabKey)
                    ? new SolidColorBrush(Color.FromArgb(40, 255, 185, 0))
                    : Brushes.Transparent,
                Foreground = Theme.TextPrimary,
                BorderBrush = (activeDonateTab == tabKey)
                    ? new SolidColorBrush(Color.FromRgb(255, 185, 0))
                    : Theme.CardBorder,
                BorderThickness = new Thickness(1),
                Cursor = Cursors.Hand
            };

            btn.Click += (s, e) =>
            {
                activeDonateTab = tabKey;
                RenderDonateTabContent(tabKey);
            };

            return btn;
        }

        private void RenderDonateTabContent(string tabKey)
        {
            donateContentStack.Children.Clear();

            if (tabKey == "pix")
            {
                // PIX Tab
                var pStack = new StackPanel();
                pStack.Children.Add(new TextBlock
                {
                    Text = Loc.T("Donate_Pix_Desc"),
                    FontSize = 11,
                    Foreground = Theme.TextSecondary,
                    Margin = new Thickness(0, 0, 0, 6)
                });

                var copyGrid = CreateCopyRow(PixKey);
                pStack.Children.Add(copyGrid);
                donateContentStack.Children.Add(pStack);
            }
            else if (tabKey == "cards")
            {
                // Cartões Tab (Ko-fi / Stripe / Credit Card)
                var cStack = new StackPanel();
                cStack.Children.Add(new TextBlock
                {
                    Text = Loc.T("Donate_Cards_Desc"),
                    FontSize = 11,
                    Foreground = Theme.TextSecondary,
                    Margin = new Thickness(0, 0, 0, 8)
                });

                var openBtn = new FluentButton("💳 " + Loc.T("Donate_OpenLink") + " (Ko-fi / Card)", true);
                openBtn.Click += (s, e) =>
                {
                    try
                    {
                        Process.Start(new ProcessStartInfo(KofiUrl) { UseShellExecute = true });
                    }
                    catch { }
                };
                cStack.Children.Add(openBtn);
                donateContentStack.Children.Add(cStack);
            }
            else if (tabKey == "crypto")
            {
                // Criptomoedas Tab (BTC, ETH, USDT)
                var crStack = new StackPanel();
                crStack.Children.Add(new TextBlock
                {
                    Text = Loc.T("Donate_Crypto_Desc"),
                    FontSize = 11,
                    Foreground = Theme.TextSecondary,
                    Margin = new Thickness(0, 0, 0, 8)
                });

                crStack.Children.Add(CreateCryptoRow("Bitcoin (BTC)", BtcAddress));
                crStack.Children.Add(CreateCryptoRow("Ethereum / USDT (ERC-20)", EthAddress));
                crStack.Children.Add(CreateCryptoRow("Tether USDT (TRC-20)", UsdtTrc20));

                donateContentStack.Children.Add(crStack);
            }
        }

        private Grid CreateCopyRow(string textToCopy)
        {
            var grid = new Grid { Margin = new Thickness(0, 0, 0, 4) };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var box = new Border
            {
                Height = 32,
                CornerRadius = new CornerRadius(4),
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(18, 18, 18)) : new SolidColorBrush(Color.FromRgb(240, 240, 240)),
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                Padding = new Thickness(10, 0, 10, 0),
                Margin = new Thickness(0, 0, 8, 0)
            };

            var txt = new TextBox
            {
                Text = textToCopy,
                IsReadOnly = true,
                BorderThickness = new Thickness(0),
                Background = Brushes.Transparent,
                Foreground = Theme.TextPrimary,
                FontFamily = new FontFamily("Consolas, Segoe UI"),
                FontSize = 11.5,
                VerticalAlignment = VerticalAlignment.Center
            };
            box.Child = txt;
            Grid.SetColumn(box, 0);
            grid.Children.Add(box);

            var copyBtn = new Button
            {
                Content = Loc.T("Donate_Copy"),
                Height = 32,
                Padding = new Thickness(14, 2, 14, 2),
                FontSize = 11.5,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                FontWeight = FontWeights.SemiBold,
                Background = Theme.CardBg,
                Foreground = Theme.TextPrimary,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                Cursor = Cursors.Hand
            };

            copyBtn.Click += (s, e) =>
            {
                try
                {
                    Clipboard.SetText(textToCopy);
                    copyBtn.Content = Loc.T("Donate_Copied");
                    copyBtn.Foreground = Theme.SuccessGreen;

                    var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
                    timer.Tick += (ts, te) =>
                    {
                        timer.Stop();
                        copyBtn.Content = Loc.T("Donate_Copy");
                        copyBtn.Foreground = Theme.TextPrimary;
                    };
                    timer.Start();
                }
                catch { }
            };

            Grid.SetColumn(copyBtn, 1);
            grid.Children.Add(copyBtn);

            return grid;
        }

        private StackPanel CreateCryptoRow(string label, string address)
        {
            var stack = new StackPanel { Margin = new Thickness(0, 0, 0, 8) };
            stack.Children.Add(new TextBlock
            {
                Text = label,
                FontSize = 10.5,
                FontWeight = FontWeights.SemiBold,
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 0, 0, 3)
            });
            stack.Children.Add(CreateCopyRow(address));
            return stack;
        }

        private Border CreateStatusChip(string icon, string title, string sub)
        {
            var border = new Border
            {
                Background = Brushes.Transparent,
                Margin = new Thickness(6, 0, 0, 0),
                Padding = new Thickness(8, 4, 8, 4),
                CornerRadius = new CornerRadius(4)
            };

            var stack = new StackPanel { Orientation = Orientation.Horizontal };
            var iconBlock = new TextBlock
            {
                Text = icon,
                FontSize = 13,
                FontFamily = new FontFamily("Segoe UI Emoji, Segoe UI"),
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 8, 0)
            };

            var textStack = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            var tBlock = new TextBlock
            {
                Text = title,
                FontSize = 11,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            };
            var sBlock = new TextBlock
            {
                Text = sub,
                FontSize = 9.5,
                FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
                Foreground = Theme.TextSecondary
            };
            textStack.Children.Add(tBlock);
            textStack.Children.Add(sBlock);

            stack.Children.Add(iconBlock);
            stack.Children.Add(textStack);

            border.Child = stack;
            return border;
        }
    }
}

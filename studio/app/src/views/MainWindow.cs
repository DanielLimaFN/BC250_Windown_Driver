using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Bc250Control.Controls;
using Bc250Control.I18n;
using Bc250Control.Interop;

namespace Bc250Control.Views
{
    public class MainWindow : Window
    {
        private List<FluentNavigationItem> navItems = new List<FluentNavigationItem>();
        private ContentControl contentHost;
        private TextBlock breadcrumbTitle;
        private Border sidebarBorder;
        private Grid rootGrid;
        private ColumnDefinition sidebarCol;
        private string currentPageKey = "system";
        private Border backBtnBorder;
        private TextBox searchInput;

        public MainWindow()
        {
            Title = Loc.T("App_Title");
            Width = 1100;
            Height = 700;
            MinWidth = 840;
            MinHeight = 540;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            Background = Theme.WindowBg;

            // Use Standard Windows 11 Native TitleBar (Snap Layouts, Native Caption buttons, Mica)
            WindowStyle = WindowStyle.SingleBorderWindow;

            Loaded += (s, e) => DwmInterop.ApplyMicaAndRoundedCorners(this, Theme.IsDark);
            SizeChanged += MainWindow_SizeChanged;
            Loc.LanguageChanged += OnLanguageOrThemeChanged;

            BuildShell();
            NavigateTo("system");
        }

        private void MainWindow_SizeChanged(object sender, SizeChangedEventArgs e)
        {
            if (sidebarCol != null)
            {
                if (e.NewSize.Width < 880)
                {
                    sidebarCol.Width = new GridLength(200);
                }
                else
                {
                    sidebarCol.Width = new GridLength(240);
                }
            }
        }

        private void BuildShell()
        {
            Title = Loc.T("App_Title");
            rootGrid = new Grid { Background = Theme.WindowBg };
            
            sidebarCol = new ColumnDefinition { Width = new GridLength(240) };
            rootGrid.ColumnDefinitions.Add(sidebarCol);
            rootGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            // =========================================================================
            // LEFT SIDEBAR (Windows 11 Settings Left Navigation Pane)
            // =========================================================================
            sidebarBorder = new Border
            {
                Background = Theme.SidebarBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(0, 0, 1, 0)
            };

            var sidebarStack = new StackPanel();

            // Profile Header
            var profileBox = new Border
            {
                Padding = new Thickness(14, 16, 14, 8),
                Margin = new Thickness(0, 0, 0, 2)
            };
            var profileGrid = new Grid();
            profileGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(38) });
            profileGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var avatar = new Border
            {
                Width = 32,
                Height = 32,
                CornerRadius = new CornerRadius(16),
                Background = new LinearGradientBrush(
                    Color.FromRgb(0, 103, 192),
                    Color.FromRgb(80, 170, 255),
                    new Point(0, 0),
                    new Point(1, 1)
                ),
                Child = new TextBlock
                {
                    Text = "DB",
                    FontSize = 12,
                    FontWeight = FontWeights.Bold,
                    Foreground = Brushes.White,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                }
            };
            Grid.SetColumn(avatar, 0);
            profileGrid.Children.Add(avatar);

            var profileText = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 0, 0) };
            profileText.Children.Add(new TextBlock
            {
                Text = "Diego Baca",
                FontSize = 13,
                FontWeight = FontWeights.SemiBold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            });
            profileText.Children.Add(new TextBlock
            {
                Text = "diego@contoso.com",
                FontSize = 10.5,
                Foreground = Theme.TextSecondary
            });
            Grid.SetColumn(profileText, 1);
            profileGrid.Children.Add(profileText);

            profileBox.Child = profileGrid;
            sidebarStack.Children.Add(profileBox);

            // Search Box
            var searchBox = new Border
            {
                Height = 30,
                Margin = new Thickness(10, 0, 10, 10),
                Padding = new Thickness(8, 0, 8, 0),
                CornerRadius = new CornerRadius(5),
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1)
            };
            var sGrid = new Grid();
            sGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            sGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(18) });

            searchInput = new TextBox
            {
                Text = Loc.T("Nav_Search"),
                BorderThickness = new Thickness(0),
                Background = Brushes.Transparent,
                Foreground = Theme.TextSecondary,
                FontSize = 11.5,
                VerticalAlignment = VerticalAlignment.Center
            };
            searchInput.GotFocus += (s, e) => { if (searchInput.Text == Loc.T("Nav_Search")) searchInput.Text = ""; searchInput.Foreground = Theme.TextPrimary; };
            searchInput.LostFocus += (s, e) => { if (string.IsNullOrWhiteSpace(searchInput.Text)) { searchInput.Text = Loc.T("Nav_Search"); searchInput.Foreground = Theme.TextSecondary; } };
            Grid.SetColumn(searchInput, 0);
            sGrid.Children.Add(searchInput);

            var sIcon = new TextBlock
            {
                Text = "🔍",
                FontSize = 10,
                Foreground = Theme.TextSecondary,
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(sIcon, 1);
            sGrid.Children.Add(sIcon);

            searchBox.Child = sGrid;
            sidebarStack.Children.Add(searchBox);

            // Navigation Items
            navItems.Clear();
            AddNavItem(sidebarStack, "🖥️", Loc.T("Nav_System"), "system");
            AddNavItem(sidebarStack, "⚡", Loc.T("Nav_Performance"), "performance");
            AddNavItem(sidebarStack, "🔓", Loc.T("Nav_Unlocker"), "unlocker");
            AddNavItem(sidebarStack, "📦", Loc.T("Nav_Driver"), "driver");
            AddNavItem(sidebarStack, "📊", Loc.T("Nav_Telemetry"), "telemetry");
            AddNavItem(sidebarStack, "⚙️", Loc.T("Nav_Settings"), "settings");

            sidebarBorder.Child = sidebarStack;
            Grid.SetColumn(sidebarBorder, 0);
            rootGrid.Children.Add(sidebarBorder);

            // =========================================================================
            // RIGHT MAIN CONTENT AREA WITH BREADCRUMB
            // =========================================================================
            var rightGrid = new Grid();
            rightGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(38) });
            rightGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });

            // Breadcrumbs bar
            var topBar = new Border
            {
                Background = Brushes.Transparent,
                Padding = new Thickness(36, 8, 36, 0)
            };
            var topStack = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                VerticalAlignment = VerticalAlignment.Center
            };

            backBtnBorder = new Border
            {
                Width = 26,
                Height = 26,
                CornerRadius = new CornerRadius(13),
                Background = Brushes.Transparent,
                Cursor = Cursors.Hand,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(0, 0, 6, 0)
            };
            var backGlyph = new TextBlock
            {
                Text = "←",
                FontSize = 13,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextSecondary,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            };
            backBtnBorder.Child = backGlyph;
            backBtnBorder.MouseEnter += (s, e) => backBtnBorder.Background = Theme.CardHoverBg;
            backBtnBorder.MouseLeave += (s, e) => backBtnBorder.Background = Brushes.Transparent;
            backBtnBorder.MouseLeftButtonDown += (s, e) => NavigateTo("system");
            topStack.Children.Add(backBtnBorder);

            breadcrumbTitle = new TextBlock
            {
                Text = Loc.T("Breadcrumb_System"),
                FontSize = 12,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                FontWeight = FontWeights.Medium,
                Foreground = Theme.TextSecondary,
                VerticalAlignment = VerticalAlignment.Center
            };
            topStack.Children.Add(breadcrumbTitle);

            topBar.Child = topStack;
            Grid.SetRow(topBar, 0);
            rightGrid.Children.Add(topBar);

            // Main Page Host
            contentHost = new ContentControl();
            Grid.SetRow(contentHost, 1);
            rightGrid.Children.Add(contentHost);

            Grid.SetColumn(rightGrid, 1);
            rootGrid.Children.Add(rightGrid);

            Content = rootGrid;
        }

        private void AddNavItem(StackPanel container, string icon, string title, string key)
        {
            var item = new FluentNavigationItem(icon, title, key);
            item.Selected += (s, e) => NavigateTo(key);
            navItems.Add(item);
            container.Children.Add(item);
        }

        public void NavigateTo(string key)
        {
            currentPageKey = key;
            foreach (var item in navItems)
            {
                item.IsSelected = (item.PageKey == key);
            }

            backBtnBorder.Visibility = (key == "system") ? Visibility.Collapsed : Visibility.Visible;

            switch (key)
            {
                case "system":
                    breadcrumbTitle.Text = Loc.T("Breadcrumb_System");
                    contentHost.Content = new SystemPage(NavigateTo);
                    break;
                case "performance":
                    breadcrumbTitle.Text = Loc.T("Breadcrumb_Performance");
                    contentHost.Content = new PerformancePage();
                    break;
                case "unlocker":
                    breadcrumbTitle.Text = Loc.T("Breadcrumb_Unlocker");
                    contentHost.Content = new UnlockerPage();
                    break;
                case "driver":
                    breadcrumbTitle.Text = Loc.T("Breadcrumb_Driver");
                    contentHost.Content = new DriverManagerPage();
                    break;
                case "telemetry":
                    breadcrumbTitle.Text = Loc.T("Breadcrumb_Telemetry");
                    contentHost.Content = new TelemetryPage();
                    break;
                case "settings":
                    breadcrumbTitle.Text = Loc.T("Breadcrumb_Settings");
                    contentHost.Content = new SettingsPage(OnLanguageOrThemeChanged);
                    break;
            }
        }

        private void OnLanguageOrThemeChanged()
        {
            DwmInterop.ApplyMicaAndRoundedCorners(this, Theme.IsDark);
            BuildShell();
            NavigateTo(currentPageKey);
        }
    }
}

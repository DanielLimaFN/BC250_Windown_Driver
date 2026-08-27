using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Bc250Control.Controls;
using Bc250Control.I18n;

namespace Bc250Control.Views
{
    public class SettingsPage : ScrollViewer
    {
        public Action OnThemeOrLangChanged { get; set; }

        public SettingsPage(Action themeOrLangChangedAction)
        {
            OnThemeOrLangChanged = themeOrLangChangedAction;
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
            Padding = new Thickness(36, 16, 36, 28);

            var mainStack = new StackPanel
            {
                MaxWidth = 1000,
                HorizontalAlignment = HorizontalAlignment.Left
            };

            // Header
            var headerStack = new StackPanel { Margin = new Thickness(0, 0, 0, 16) };
            var title = new TextBlock
            {
                Text = Loc.T("Settings_Title"),
                FontSize = 26,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            };
            var sub = new TextBlock
            {
                Text = Loc.T("Settings_Sub"),
                FontSize = 12,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 0)
            };
            headerStack.Children.Add(title);
            headerStack.Children.Add(sub);
            mainStack.Children.Add(headerStack);

            // =========================================================================
            // LANGUAGE SELECTOR CARD
            // =========================================================================
            var langCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(16, 12, 16, 12),
                Margin = new Thickness(0, 0, 0, 4)
            };
            var langGrid = new Grid();
            langGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(36) });
            langGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            langGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var lIcon = new TextBlock
            {
                Text = "🌐",
                FontSize = 15,
                VerticalAlignment = VerticalAlignment.Center,
                HorizontalAlignment = HorizontalAlignment.Left
            };
            Grid.SetColumn(lIcon, 0);
            langGrid.Children.Add(lIcon);

            var lTextStack = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            lTextStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Settings_Lang_Title"),
                FontSize = 13,
                FontWeight = FontWeights.SemiBold,
                Foreground = Theme.TextPrimary
            });
            lTextStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Settings_Lang_Sub"),
                FontSize = 11,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 1, 0, 0)
            });
            Grid.SetColumn(lTextStack, 1);
            langGrid.Children.Add(lTextStack);

            // Language Buttons Stack
            var langBtns = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
            langBtns.Children.Add(CreateLangButton("🇧🇷 Português", "pt"));
            langBtns.Children.Add(CreateLangButton("🇺🇸 English", "en"));
            langBtns.Children.Add(CreateLangButton("🇷🇺 Русский", "ru"));
            Grid.SetColumn(langBtns, 2);
            langGrid.Children.Add(langBtns);

            langCard.Child = langGrid;
            mainStack.Children.Add(langCard);

            // =========================================================================
            // THEME TOGGLE
            // =========================================================================
            var darkToggle = new FluentToggleSwitch(Theme.IsDark);
            darkToggle.CheckedChanged += (s, isDark) =>
            {
                Theme.IsDark = isDark;
                OnThemeOrLangChanged?.Invoke();
            };

            mainStack.Children.Add(new FluentSettingsCard(
                "🎨",
                Loc.T("Settings_Theme_Title"),
                Loc.T("Settings_Theme_Sub"),
                darkToggle
            ));

            mainStack.Children.Add(new FluentSettingsCard(
                "⚡",
                Loc.T("Settings_Startup_Title"),
                Loc.T("Settings_Startup_Sub"),
                new FluentToggleSwitch(true)
            ));

            mainStack.Children.Add(new FluentSettingsCard(
                "📥",
                Loc.T("Settings_Tray_Title"),
                Loc.T("Settings_Tray_Sub"),
                new FluentToggleSwitch(true)
            ));

            // =========================================================================
            // ABOUT CARD
            // =========================================================================
            var aboutCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(18, 14, 18, 14),
                Margin = new Thickness(0, 14, 0, 0)
            };
            var aStack = new StackPanel();
            aStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Settings_About_Title"),
                FontSize = 13.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary
            });
            aStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Settings_About_Desc"),
                FontSize = 11,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 4, 0, 0)
            });

            aboutCard.Child = aStack;
            mainStack.Children.Add(aboutCard);

            Content = mainStack;
        }

        private Button CreateLangButton(string label, string langCode)
        {
            bool isCurrent = (Loc.CurrentLanguage == langCode);
            var btn = new Button
            {
                Content = label,
                Height = 28,
                Padding = new Thickness(10, 2, 10, 2),
                Margin = new Thickness(4, 0, 0, 0),
                FontSize = 11,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                FontWeight = isCurrent ? FontWeights.Bold : FontWeights.Normal,
                Background = isCurrent
                    ? Theme.Accent
                    : (Theme.IsDark ? new SolidColorBrush(Color.FromRgb(36, 36, 36)) : new SolidColorBrush(Color.FromRgb(240, 240, 240))),
                Foreground = isCurrent ? Brushes.White : Theme.TextPrimary,
                BorderBrush = isCurrent ? Theme.Accent : Theme.CardBorder,
                BorderThickness = new Thickness(1),
                Cursor = System.Windows.Input.Cursors.Hand
            };
            btn.Click += (s, e) =>
            {
                Loc.SetLanguage(langCode);
                OnThemeOrLangChanged?.Invoke();
            };
            return btn;
        }
    }
}

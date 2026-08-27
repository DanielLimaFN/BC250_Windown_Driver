using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Bc250Control.Controls;
using Bc250Control.I18n;

namespace Bc250Control.Views
{
    public class PerformancePage : ScrollViewer
    {
        public PerformancePage()
        {
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
                Text = Loc.T("Perf_Title"),
                FontSize = 26,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            };
            var sub = new TextBlock
            {
                Text = Loc.T("Perf_Sub"),
                FontSize = 12,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 0)
            };
            headerStack.Children.Add(title);
            headerStack.Children.Add(sub);
            mainStack.Children.Add(headerStack);

            // =========================================================================
            // POWER PROFILES CARD
            // =========================================================================
            var profileCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(18, 14, 18, 14),
                Margin = new Thickness(0, 0, 0, 14)
            };
            var profileStack = new StackPanel();
            profileStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Perf_Profiles_Title"),
                FontSize = 13.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 0, 0, 8)
            });

            var profBtnRow = new WrapPanel { Orientation = Orientation.Horizontal };
            profBtnRow.Children.Add(CreateProfileButton("Silent (350MHz / 700mV)", "TDP 45W"));
            profBtnRow.Children.Add(CreateProfileButton("Balanced (1500MHz / 900mV)", "TDP 95W (Sweet Spot)"));
            profBtnRow.Children.Add(CreateProfileButton("Gaming (1800MHz / 1050mV)", "TDP 140W"));
            profBtnRow.Children.Add(CreateProfileButton("Extreme (2000MHz / 1150mV)", "TDP 180W+"));
            profileStack.Children.Add(profBtnRow);

            profileCard.Child = profileStack;
            mainStack.Children.Add(profileCard);

            // =========================================================================
            // CLOCKS & VOLTAGE SLIDERS
            // =========================================================================
            mainStack.Children.Add(CreateSliderCard(Loc.T("Perf_GpuBase"), Loc.T("Perf_GpuBase_Sub"), 500, 2400, 1500, "MHz"));
            mainStack.Children.Add(CreateSliderCard(Loc.T("Perf_GpuBoost"), Loc.T("Perf_GpuBoost_Sub"), 1000, 2500, 1950, "MHz"));
            mainStack.Children.Add(CreateSliderCard(Loc.T("Perf_VoltageOffset"), Loc.T("Perf_VoltageOffset_Sub"), -100, 150, 0, "mV"));
            mainStack.Children.Add(CreateSliderCard(Loc.T("Perf_TdpLimit"), Loc.T("Perf_TdpLimit_Sub"), 45, 220, 125, "Watts"));

            // =========================================================================
            // FAN CONTROL & THERMALS
            // =========================================================================
            var fanCard = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(18, 14, 18, 14),
                Margin = new Thickness(0, 0, 0, 14)
            };
            var fanStack = new StackPanel();
            var fanHead = new Grid();
            fanHead.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            fanHead.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            var fanTitleStack = new StackPanel();
            fanTitleStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Perf_Fan_Title"),
                FontSize = 13.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary
            });
            fanTitleStack.Children.Add(new TextBlock
            {
                Text = Loc.T("Perf_Fan_Sub"),
                FontSize = 11.5,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 0)
            });
            Grid.SetColumn(fanTitleStack, 0);
            fanHead.Children.Add(fanTitleStack);

            var fanSwitch = new FluentToggleSwitch(true);
            Grid.SetColumn(fanSwitch, 1);
            fanHead.Children.Add(fanSwitch);

            fanStack.Children.Add(fanHead);
            fanCard.Child = fanStack;
            mainStack.Children.Add(fanCard);

            // Apply Actions
            var actionRow = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 8, 0, 0) };
            var resetBtn = new FluentButton(Loc.T("Perf_Reset"), false);
            resetBtn.Margin = new Thickness(0, 0, 10, 0);
            actionRow.Children.Add(resetBtn);

            var saveBtn = new FluentButton(Loc.T("Perf_Apply"), true);
            actionRow.Children.Add(saveBtn);

            mainStack.Children.Add(actionRow);

            Content = mainStack;
        }

        private Border CreateSliderCard(string title, string sub, double min, double max, double current, string unit)
        {
            var card = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(16, 12, 16, 12),
                Margin = new Thickness(0, 0, 0, 6)
            };

            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(180) });

            var textStack = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            textStack.Children.Add(new TextBlock
            {
                Text = title,
                FontSize = 13,
                FontWeight = FontWeights.SemiBold,
                Foreground = Theme.TextPrimary
            });
            textStack.Children.Add(new TextBlock
            {
                Text = sub,
                FontSize = 11,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 0)
            });
            Grid.SetColumn(textStack, 0);
            grid.Children.Add(textStack);

            var sliderStack = new StackPanel { VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Right };
            var valLabel = new TextBlock
            {
                Text = $"{current} {unit}",
                FontSize = 12,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary,
                HorizontalAlignment = HorizontalAlignment.Right,
                Margin = new Thickness(0, 0, 0, 4)
            };

            var slider = new Slider
            {
                Minimum = min,
                Maximum = max,
                Value = current,
                Width = 160,
                IsSnapToTickEnabled = true,
                TickFrequency = (max - min) / 20.0
            };
            slider.ValueChanged += (s, e) =>
            {
                valLabel.Text = $"{Math.Round(e.NewValue)} {unit}";
            };

            sliderStack.Children.Add(valLabel);
            sliderStack.Children.Add(slider);
            Grid.SetColumn(sliderStack, 1);
            grid.Children.Add(sliderStack);

            card.Child = grid;
            return card;
        }

        private Button CreateProfileButton(string title, string sub)
        {
            var btn = new Button
            {
                Height = 44,
                Padding = new Thickness(12, 4, 12, 4),
                Margin = new Thickness(0, 0, 8, 6),
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(36, 36, 36)) : new SolidColorBrush(Color.FromRgb(240, 240, 240)),
                Foreground = Theme.TextPrimary,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                Cursor = System.Windows.Input.Cursors.Hand
            };

            var stack = new StackPanel();
            stack.Children.Add(new TextBlock
            {
                Text = title,
                FontSize = 11.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary
            });
            stack.Children.Add(new TextBlock
            {
                Text = sub,
                FontSize = 9.5,
                Foreground = Theme.TextSecondary
            });
            btn.Content = stack;
            return btn;
        }
    }
}

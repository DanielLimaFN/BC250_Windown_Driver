using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;

namespace Bc250Control.Controls
{
    public static class Theme
    {
        public static bool IsDark { get; set; } = true;

        public static SolidColorBrush WindowBg => IsDark ? new SolidColorBrush(Color.FromRgb(32, 32, 32)) : new SolidColorBrush(Color.FromRgb(243, 243, 243));
        public static SolidColorBrush SidebarBg => IsDark ? new SolidColorBrush(Color.FromRgb(26, 26, 26)) : new SolidColorBrush(Color.FromRgb(235, 235, 235));
        public static SolidColorBrush CardBg => IsDark ? new SolidColorBrush(Color.FromRgb(43, 43, 43)) : new SolidColorBrush(Color.FromRgb(255, 255, 255));
        public static SolidColorBrush CardHoverBg => IsDark ? new SolidColorBrush(Color.FromRgb(52, 52, 52)) : new SolidColorBrush(Color.FromRgb(249, 249, 249));
        public static SolidColorBrush CardBorder => IsDark ? new SolidColorBrush(Color.FromArgb(35, 255, 255, 255)) : new SolidColorBrush(Color.FromRgb(229, 229, 229));
        public static SolidColorBrush TextPrimary => IsDark ? new SolidColorBrush(Color.FromRgb(255, 255, 255)) : new SolidColorBrush(Color.FromRgb(31, 31, 31));
        public static SolidColorBrush TextSecondary => IsDark ? new SolidColorBrush(Color.FromRgb(160, 160, 160)) : new SolidColorBrush(Color.FromRgb(95, 95, 95));
        public static SolidColorBrush Accent => new SolidColorBrush(Color.FromRgb(0, 103, 192)); // Windows 11 Blue
        public static SolidColorBrush AccentRed => new SolidColorBrush(Color.FromRgb(242, 80, 86)); // BC-250 Crimson
        public static SolidColorBrush SuccessGreen => new SolidColorBrush(Color.FromRgb(16, 185, 129));
    }

    public class FluentNavigationItem : Border
    {
        private Rectangle indicator;
        private TextBlock iconBlock;
        private TextBlock titleBlock;
        private bool isSelected = false;

        public string PageKey { get; set; }
        public event EventHandler Selected;

        public bool IsSelected
        {
            get => isSelected;
            set
            {
                isSelected = value;
                UpdateVisualState();
            }
        }

        public FluentNavigationItem(string icon, string title, string pageKey)
        {
            PageKey = pageKey;
            Height = 36;
            Margin = new Thickness(8, 2, 8, 2);
            CornerRadius = new CornerRadius(5);
            Background = Brushes.Transparent;
            Cursor = Cursors.Hand;

            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(4) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(30) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            indicator = new Rectangle
            {
                Width = 3,
                Height = 16,
                RadiusX = 1.5,
                RadiusY = 1.5,
                Fill = Theme.Accent,
                HorizontalAlignment = HorizontalAlignment.Left,
                Visibility = Visibility.Collapsed
            };
            Grid.SetColumn(indicator, 0);

            iconBlock = new TextBlock
            {
                Text = icon,
                FontSize = 14,
                FontFamily = new FontFamily("Segoe UI Emoji, Segoe MDL2 Assets, Segoe UI"),
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
                Foreground = Theme.TextPrimary
            };
            Grid.SetColumn(iconBlock, 1);

            titleBlock = new TextBlock
            {
                Text = title,
                FontSize = 12.5,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                FontWeight = FontWeights.Normal,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(6, 0, 0, 0),
                Foreground = Theme.TextPrimary
            };
            Grid.SetColumn(titleBlock, 2);

            grid.Children.Add(indicator);
            grid.Children.Add(iconBlock);
            grid.Children.Add(titleBlock);

            Child = grid;

            MouseEnter += (s, e) => { if (!isSelected) Background = Theme.CardHoverBg; };
            MouseLeave += (s, e) => { if (!isSelected) Background = Brushes.Transparent; };
            MouseLeftButtonDown += (s, e) => Selected?.Invoke(this, EventArgs.Empty);
        }

        private void UpdateVisualState()
        {
            if (isSelected)
            {
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromArgb(28, 255, 255, 255)) : new SolidColorBrush(Color.FromArgb(20, 0, 0, 0));
                indicator.Visibility = Visibility.Visible;
                titleBlock.FontWeight = FontWeights.SemiBold;
            }
            else
            {
                Background = Brushes.Transparent;
                indicator.Visibility = Visibility.Collapsed;
                titleBlock.FontWeight = FontWeights.Normal;
            }
        }
    }

    public class FluentSettingsCard : Border
    {
        public FluentSettingsCard(string icon, string title, string subtitle, UIElement rightAction = null, Action onClick = null)
        {
            Margin = new Thickness(0, 0, 0, 4);
            Padding = new Thickness(16, 12, 16, 12);
            CornerRadius = new CornerRadius(6);
            Background = Theme.CardBg;
            BorderBrush = Theme.CardBorder;
            BorderThickness = new Thickness(1);
            Cursor = onClick != null ? Cursors.Hand : Cursors.Arrow;

            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(36) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            // Icon Box
            var iconBox = new Border
            {
                Width = 26,
                Height = 26,
                CornerRadius = new CornerRadius(4),
                Background = Brushes.Transparent,
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Center,
                Child = new TextBlock
                {
                    Text = icon,
                    FontSize = 15,
                    FontFamily = new FontFamily("Segoe UI Emoji, Segoe MDL2 Assets, Segoe UI"),
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center,
                    Foreground = Theme.TextPrimary
                }
            };
            Grid.SetColumn(iconBox, 0);
            grid.Children.Add(iconBox);

            // Title and Subtitle Stack
            var textStack = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
            var titleText = new TextBlock
            {
                Text = title,
                FontSize = 13,
                FontWeight = FontWeights.SemiBold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            };
            textStack.Children.Add(titleText);

            if (!string.IsNullOrEmpty(subtitle))
            {
                var subText = new TextBlock
                {
                    Text = subtitle,
                    FontSize = 11,
                    FontFamily = new FontFamily("Segoe UI Variable Text, Segoe UI"),
                    Foreground = Theme.TextSecondary,
                    Margin = new Thickness(0, 1, 0, 0)
                };
                textStack.Children.Add(subText);
            }
            Grid.SetColumn(textStack, 1);
            grid.Children.Add(textStack);

            // Right Action / Chevron
            if (rightAction != null)
            {
                Grid.SetColumn(rightAction, 2);
                grid.Children.Add(rightAction);
            }
            else if (onClick != null)
            {
                var chevron = new TextBlock
                {
                    Text = "›",
                    FontSize = 20,
                    FontWeight = FontWeights.Light,
                    Foreground = Theme.TextSecondary,
                    VerticalAlignment = VerticalAlignment.Center,
                    Margin = new Thickness(8, 0, 2, 0)
                };
                Grid.SetColumn(chevron, 2);
                grid.Children.Add(chevron);
            }

            Child = grid;

            if (onClick != null)
            {
                MouseEnter += (s, e) => Background = Theme.CardHoverBg;
                MouseLeave += (s, e) => Background = Theme.CardBg;
                MouseLeftButtonDown += (s, e) => onClick();
            }
        }
    }

    public class FluentToggleSwitch : Border
    {
        private Border thumb;
        private bool isChecked = false;
        public event EventHandler<bool> CheckedChanged;

        public bool IsChecked
        {
            get => isChecked;
            set
            {
                isChecked = value;
                UpdateSwitchVisual();
            }
        }

        public FluentToggleSwitch(bool initialValue = false)
        {
            isChecked = initialValue;
            Width = 40;
            Height = 20;
            CornerRadius = new CornerRadius(10);
            BorderThickness = new Thickness(1);
            Cursor = Cursors.Hand;

            var canvas = new Canvas();
            thumb = new Border
            {
                Width = 12,
                Height = 12,
                CornerRadius = new CornerRadius(6),
                Background = Brushes.White,
            };
            Canvas.SetTop(thumb, 3);
            canvas.Children.Add(thumb);

            Child = canvas;
            UpdateSwitchVisual();

            MouseLeftButtonDown += (s, e) =>
            {
                IsChecked = !IsChecked;
                CheckedChanged?.Invoke(this, IsChecked);
            };
        }

        private void UpdateSwitchVisual()
        {
            if (isChecked)
            {
                Background = Theme.Accent;
                BorderBrush = Theme.Accent;
                Canvas.SetLeft(thumb, 23);
                thumb.Background = Brushes.White;
            }
            else
            {
                Background = Theme.IsDark ? new SolidColorBrush(Color.FromRgb(40, 40, 40)) : new SolidColorBrush(Color.FromRgb(240, 240, 240));
                BorderBrush = Theme.TextSecondary;
                Canvas.SetLeft(thumb, 3);
                thumb.Background = Theme.TextSecondary;
            }
        }
    }

    public class FluentButton : Button
    {
        public FluentButton(string text, bool isAccent = false)
        {
            Content = text;
            Height = 30;
            Padding = new Thickness(14, 2, 14, 2);
            FontSize = 12;
            FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI");
            FontWeight = FontWeights.SemiBold;
            Cursor = Cursors.Hand;
            BorderThickness = new Thickness(1);

            if (isAccent)
            {
                Background = Theme.Accent;
                Foreground = Brushes.White;
                BorderBrush = Theme.Accent;
            }
            else
            {
                Background = Theme.CardBg;
                Foreground = Theme.TextPrimary;
                BorderBrush = Theme.CardBorder;
            }
        }
    }

    public class WindowCaptionButton : Button
    {
        public WindowCaptionButton(string glyph, bool isClose = false)
        {
            Content = glyph;
            Width = 46;
            Height = 32;
            FontSize = 10;
            FontFamily = new FontFamily("Segoe MDL2 Assets, Segoe UI Symbol, Arial");
            Background = Brushes.Transparent;
            Foreground = Theme.TextPrimary;
            BorderThickness = new Thickness(0);
            Cursor = Cursors.Arrow;

            MouseEnter += (s, e) =>
            {
                if (isClose)
                {
                    Background = new SolidColorBrush(Color.FromRgb(232, 17, 35));
                    Foreground = Brushes.White;
                }
                else
                {
                    Background = Theme.CardHoverBg;
                }
            };
            MouseLeave += (s, e) =>
            {
                Background = Brushes.Transparent;
                Foreground = Theme.TextPrimary;
            };
        }
    }
}

using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using Bc250Control.Controls;
using Bc250Control.I18n;

namespace Bc250Control.Views
{
    public class TelemetryPage : ScrollViewer
    {
        private TextBlock gpuTempBlock;
        private TextBlock gpuLoadBlock;
        private TextBlock gpuClockBlock;
        private TextBlock gpuVoltBlock;
        private TextBlock gpuPowerBlock;
        private TextBlock cpuLoadBlock;
        private TextBlock cpuTempBlock;
        private DispatcherTimer ticker;

        public TelemetryPage()
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
                Text = Loc.T("Telem_Title"),
                FontSize = 26,
                FontWeight = FontWeights.Bold,
                FontFamily = new FontFamily("Segoe UI Variable Display, Segoe UI"),
                Foreground = Theme.TextPrimary
            };
            var sub = new TextBlock
            {
                Text = Loc.T("Telem_Sub"),
                FontSize = 12,
                Foreground = Theme.TextSecondary,
                Margin = new Thickness(0, 2, 0, 0)
            };
            headerStack.Children.Add(title);
            headerStack.Children.Add(sub);
            mainStack.Children.Add(headerStack);

            // =========================================================================
            // GPU SENSORS GRID
            // =========================================================================
            var gpuSectionHeader = new TextBlock
            {
                Text = Loc.T("Telem_GpuSection"),
                FontSize = 13.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 0, 0, 10)
            };
            mainStack.Children.Add(gpuSectionHeader);

            var gpuGrid = new Grid { Margin = new Thickness(0, 0, 0, 12) };
            gpuGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            gpuGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            gpuGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var b1 = CreateSensorCard(Loc.T("Telem_GpuTemp"), "74 °C", "Hotspot: 79°C • Throttle: 95°C", out gpuTempBlock);
            Grid.SetColumn(b1, 0);
            gpuGrid.Children.Add(b1);

            var b2 = CreateSensorCard(Loc.T("Telem_GpuLoad"), "95 %", "Compute Engine active", out gpuLoadBlock);
            Grid.SetColumn(b2, 1);
            gpuGrid.Children.Add(b2);

            var b3 = CreateSensorCard(Loc.T("Telem_GpuClock"), "1950 MHz", "Target Boost: 2000MHz", out gpuClockBlock);
            Grid.SetColumn(b3, 2);
            gpuGrid.Children.Add(b3);

            mainStack.Children.Add(gpuGrid);

            var gpuGrid2 = new Grid { Margin = new Thickness(0, 0, 0, 20) };
            gpuGrid2.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            gpuGrid2.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var b4 = CreateSensorCard(Loc.T("Telem_GpuVolt"), "1.15 V", "SoC Plane Regulated", out gpuVoltBlock);
            Grid.SetColumn(b4, 0);
            gpuGrid2.Children.Add(b4);

            var b5 = CreateSensorCard(Loc.T("Telem_GpuPower"), "185 W", "Board Total Power", out gpuPowerBlock);
            Grid.SetColumn(b5, 1);
            gpuGrid2.Children.Add(b5);

            mainStack.Children.Add(gpuGrid2);

            // =========================================================================
            // CPU SENSORS
            // =========================================================================
            var cpuSectionHeader = new TextBlock
            {
                Text = Loc.T("Telem_CpuSection"),
                FontSize = 13.5,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 0, 0, 10)
            };
            mainStack.Children.Add(cpuSectionHeader);

            var cpuGrid = new Grid();
            cpuGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            cpuGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var cb1 = CreateSensorCard(Loc.T("Telem_CpuLoad"), "42 %", "3.50 GHz All-Core", out cpuLoadBlock);
            Grid.SetColumn(cb1, 0);
            cpuGrid.Children.Add(cb1);

            var cb2 = CreateSensorCard(Loc.T("Telem_CpuTemp"), "68 °C", "Tctl/Tdie sensor", out cpuTempBlock);
            Grid.SetColumn(cb2, 1);
            cpuGrid.Children.Add(cb2);

            mainStack.Children.Add(cpuGrid);

            Content = mainStack;

            // Live Ticker
            var rnd = new Random();
            ticker = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
            ticker.Tick += (s, e) =>
            {
                int gTemp = 74 + rnd.Next(-2, 3);
                int gLoad = 94 + rnd.Next(-3, 4);
                int gClock = 1950 + rnd.Next(-15, 16);
                double gVolt = 1.15 + (rnd.NextDouble() * 0.04 - 0.02);
                int gPower = 185 + rnd.Next(-4, 5);

                int cLoad = 42 + rnd.Next(-4, 5);
                int cTemp = 68 + rnd.Next(-2, 3);

                gpuTempBlock.Text = $"{gTemp} °C";
                gpuLoadBlock.Text = $"{gLoad} %";
                gpuClockBlock.Text = $"{gClock} MHz";
                gpuVoltBlock.Text = $"{gVolt:F2} V";
                gpuPowerBlock.Text = $"{gPower} W";

                cpuLoadBlock.Text = $"{cLoad} %";
                cpuTempBlock.Text = $"{cTemp} °C";
            };
            ticker.Start();
        }

        private Border CreateSensorCard(string title, string initialVal, string sub, out TextBlock valBlock)
        {
            var b = new Border
            {
                Background = Theme.CardBg,
                BorderBrush = Theme.CardBorder,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(16, 12, 16, 12),
                Margin = new Thickness(3, 0, 3, 0)
            };
            var s = new StackPanel();
            s.Children.Add(new TextBlock
            {
                Text = title,
                FontSize = 10,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextSecondary
            });

            valBlock = new TextBlock
            {
                Text = initialVal,
                FontSize = 19,
                FontWeight = FontWeights.Bold,
                Foreground = Theme.TextPrimary,
                Margin = new Thickness(0, 3, 0, 3)
            };
            s.Children.Add(valBlock);

            s.Children.Add(new TextBlock
            {
                Text = sub,
                FontSize = 10.5,
                Foreground = Theme.TextSecondary
            });

            b.Child = s;
            return b;
        }
    }
}

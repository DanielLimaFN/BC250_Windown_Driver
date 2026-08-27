using System;
using System.Windows;
using Bc250Control.Views;

namespace Bc250Control
{
    public static class Program
    {
        [STAThread]
        public static void Main(string[] args)
        {
            var app = new Application();
            var window = new MainWindow();
            app.Run(window);
        }
    }
}

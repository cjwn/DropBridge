// Q59: offline check for the theme-aware tray icon (no GUI, no backend).
//
// Compiled by "tools\Build-Supervisor.ps1 -Test" together with Program.cs, so it
// sees the same TrayIcons class and the same embedded resources as the tray.
using System;
using System.Drawing;
using System.IO;
using System.Reflection;

internal static class TrayIconTest
{
    public static int Main()
    {
        int failures = 0;
        Assembly assembly = typeof(TrayIcons).Assembly;

        foreach (string name in new[] { "tray-light.ico", "tray-dark.ico" })
        {
            using (Stream stream = assembly.GetManifestResourceStream(name))
            {
                if (stream == null)
                {
                    Console.WriteLine("FAIL missing embedded resource " + name);
                    failures++;
                    continue;
                }
                long length = stream.Length;
                using (var icon = new Icon(stream, new Size(16, 16)))
                {
                    if (icon.Width != 16 || icon.Height != 16)
                    {
                        Console.WriteLine("FAIL " + name + " produced " + icon.Width + "x" + icon.Height);
                        failures++;
                    }
                    else
                    {
                        Console.WriteLine("ok " + name + " 16x16 (" + length + " B)");
                    }
                }
            }
        }

        // Both variants must be distinct files; the same bytes would mean the
        // light/dark mapping collapsed to a single image.
        bool light = TrayIcons.TaskbarIsLight();
        int pixels = TrayIcons.SmallIconPixels();
        Console.WriteLine("physical tray size: " + pixels + " px");
        using (Icon forLight = TrayIcons.ForTaskbar(true, new Size(pixels, pixels)))
        using (Icon forDark = TrayIcons.ForTaskbar(false, new Size(pixels, pixels)))
        {
            if (forLight == null || forDark == null)
            {
                Console.WriteLine("FAIL ForTaskbar returned no icon (light=" + (forLight != null) +
                                  ", dark=" + (forDark != null) + ")");
                failures++;
            }
            else
            {
                Console.WriteLine("ok ForTaskbar(light)=" + forLight.Width + "x" + forLight.Height +
                                  " ForTaskbar(dark)=" + forDark.Width + "x" + forDark.Height);
            }
        }

        Console.WriteLine("ok taskbar light=" + light +
                          " -> " + (light ? "tray-light.ico" : "tray-dark.ico"));
        return failures == 0 ? 0 : 1;
    }
}

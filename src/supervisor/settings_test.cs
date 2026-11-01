// Q60: offline checks for data\settings.ini and the device-name validation.
//
// Compiled by "tools\Build-Supervisor.ps1 -Test" together with Program.cs, so it
// exercises the same Settings class the tray uses. Nothing touches the real
// package: every case runs in a throw-away directory.
using System;
using System.IO;

internal static class SettingsTest
{
    public static int Main()
    {
        int failures = 0;
        string root = Path.Combine(Path.GetTempPath(), "dropbridge-settings-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            Settings fresh = Settings.Load(root);
            Check(ref failures, fresh.DeviceName == "DropBridge", "default device name");
            Check(ref failures, fresh.Beacon, "beacon defaults to on");
            Check(ref failures, !File.Exists(Path.Combine(root, "settings.ini")),
                "loading alone does not create a file");

            File.WriteAllText(Path.Combine(root, "beacon-off"), "legacy marker");
            Settings migrated = Settings.Load(root);
            Check(ref failures, !migrated.Beacon, "legacy beacon-off marker turns the beacon off");
            File.Delete(Path.Combine(root, "beacon-off"));

            fresh.SetDeviceName("客厅的电脑");
            fresh.SetBeacon(false);
            Settings again = Settings.Load(root);
            Check(ref failures, again.DeviceName == "客厅的电脑", "device name persists");
            Check(ref failures, !again.Beacon, "beacon=off persists");

            // Q64: the still-copy switch defaults to on and is remembered.
            Check(ref failures, again.StillCopy, "stillCopy defaults to on");
            again.SetStillCopy(false);
            Check(ref failures, !Settings.Load(root).StillCopy, "stillCopy=off persists");

            Check(ref failures, Settings.CleanName("DropBridge") == "DropBridge", "plain name accepted");
            Check(ref failures, Settings.CleanName("  小林 的电脑  ") == "小林 的电脑", "trimmed");
            Check(ref failures, Settings.CleanName("") == null, "empty rejected");
            Check(ref failures, Settings.CleanName("   ") == null, "blank rejected");
            Check(ref failures, Settings.CleanName(new string('a', 64)) == null, "64 chars rejected");
            Check(ref failures, Settings.CleanName(new string('a', 63)) != null, "63 chars accepted");
            Check(ref failures, Settings.CleanName("a/b") == null, "slash rejected");
            Check(ref failures, Settings.CleanName("a\\b") == null, "backslash rejected");
            Check(ref failures, Settings.CleanName("a:b") == null, "colon rejected");
            Check(ref failures, Settings.CleanName("a\tb") == null, "control char rejected");
            Check(ref failures, Settings.CleanName("a\u202Eb") == null, "bidi override rejected");
            Check(ref failures, Settings.CleanName("d\u00e9j\u00e0-vu") == "d\u00e9j\u00e0-vu", "accents accepted");

            // A hand-edited file with an unusable name must not poison the tray.
            File.WriteAllText(Path.Combine(root, "settings.ini"), "deviceName=a/b\nbeacon=false\n");
            Settings edited = Settings.Load(root);
            Check(ref failures, edited.DeviceName == "DropBridge", "invalid name in the file falls back");
            Check(ref failures, !edited.Beacon, "the rest of the file is still read");
        }
        finally
        {
            try { Directory.Delete(root, true); }
            catch (Exception) { }
        }

        Console.WriteLine(failures == 0 ? "PASS settings" : "FAIL settings (" + failures + " case(s))");
        return failures == 0 ? 0 : 1;
    }

    private static void Check(ref int failures, bool ok, string what)
    {
        if (ok) return;
        failures++;
        Console.WriteLine("FAIL " + what);
    }
}

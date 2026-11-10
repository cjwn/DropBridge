// Q4 prototype: one exe that carries the headless package and unpacks it.
//
// Deliberately small and dependency-free: the compiler that ships with Windows
// (.NET Framework 4.8) builds it, and System.IO.Compression reads the payload,
// so the result needs no installer, no Python, no Flutter and no extra runtime
// on Windows 10/11.
//
//   DropBridge.exe             unpack (once per build) and start the supervisor
//   DropBridge.exe --extract   unpack only, print the directory
//   DropBridge.exe --where     print the directory without unpacking
//
// Everything stays inside the folder the exe lives in: the payload under
// app\<build> and the data (keys, instance id, received files, logs) under
// data\. Nothing is written to %LOCALAPPDATA% or any other machine directory.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;

internal static class Program
{
    private const string ResourceName = "payload.zip";

    private static string VersionTag()
    {
        return File.GetLastWriteTime(Assembly.GetExecutingAssembly().Location)
            .ToString("yyyyMMdd-HHmmss");
    }

    private static string ExeDir()
    {
        return Path.GetDirectoryName(Path.GetFullPath(Assembly.GetExecutingAssembly().Location));
    }

    /* Everything the app writes stays under the folder the user put the exe in:
     * the payload goes to <folder>\app\<build> and the persistent data (keys,
     * received files, logs, instance id) to <folder>\data, shared by every
     * build so an upgrade does not change the AirDrop identity. */
    private static string InstallDir()
    {
        return Path.Combine(ExeDir(), "app", VersionTag());
    }

    private static string DataDir()
    {
        return Path.Combine(ExeDir(), "data");
    }

    private static void PruneOldBuilds(string current)
    {
        try
        {
            string appRoot = Path.Combine(ExeDir(), "app");
            var builds = new List<DirectoryInfo>(new DirectoryInfo(appRoot).GetDirectories());
            builds.Sort((left, right) => right.CreationTimeUtc.CompareTo(left.CreationTimeUtc));
            int seen = 0;
            foreach (DirectoryInfo build in builds)
            {
                if (string.Equals(build.FullName.TrimEnd('\\'), current.TrimEnd('\\'),
                                  StringComparison.OrdinalIgnoreCase))
                {
                    seen++;
                    continue;
                }
                seen++;
                if (seen > 2)
                {
                    // A tray from an older build may still be running: its locked
                    // exes would survive, but its scripts and Python sources would
                    // not, and it would lose toasts and receiver restarts.
                    if (BuildInUse(build.FullName)) continue;
                    try { build.Delete(true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
                }
            }
        }
        catch (DirectoryNotFoundException)
        {
        }
    }

    /* True when any process we can inspect runs an exe from this build. When a
     * DropBridge/AirBridge32 process cannot be inspected, assume it might be
     * from this build and keep the directory; pruning can wait for next launch. */
    private static bool BuildInUse(string buildDirectory)
    {
        string prefix = buildDirectory.TrimEnd('\\') + "\\";
        int self = Process.GetCurrentProcess().Id;
        foreach (string name in new[]
                 {
                     "DropBridge", "AirBridge32", "airbridge_radio", "radio_pipe_bridge",
                     "awdl_netd", "awdld", "python",
                 })
        {
            Process[] processes;
            try { processes = Process.GetProcessesByName(name); }
            catch (Exception) { continue; }
            foreach (Process process in processes)
            {
                try
                {
                    if (process.Id == self) continue;
                    string path = process.MainModule.FileName;
                    if (path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return true;
                }
                catch (Exception)
                {
                    // Cannot read the path (other user, elevated, exited, or a
                    // 32/64-bit mismatch). Only a tray process name is worth
                    // being careful about; others are unrelated programs.
                    if (name == "DropBridge" || name == "AirBridge32") return true;
                }
                finally
                {
                    process.Dispose();
                }
            }
        }
        return false;
    }

    private static void Extract(string target)
    {
        Directory.CreateDirectory(target);
        string root = Path.GetFullPath(target) + Path.DirectorySeparatorChar;
        using (Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName))
        {
            if (stream == null) throw new InvalidOperationException("embedded payload is missing");
            using (var archive = new ZipArchive(stream, ZipArchiveMode.Read))
            {
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    // Compress-Archive can write backslashes and bare directory
                    // entries; normalise before touching the disk.
                    string name = entry.FullName.Replace('\\', '/').TrimStart('/');
                    if (name.Length == 0) continue;
                    bool isDirectory = name.EndsWith("/", StringComparison.Ordinal);
                    string destination = Path.GetFullPath(
                        Path.Combine(target, name.Replace('/', Path.DirectorySeparatorChar)));
                    if (!destination.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("payload entry escapes the install directory");
                    if (isDirectory)
                    {
                        Directory.CreateDirectory(destination);
                        continue;
                    }
                    Directory.CreateDirectory(Path.GetDirectoryName(destination));
                    using (Stream input = entry.Open())
                    using (FileStream output = File.Create(destination))
                    {
                        input.CopyTo(output);
                    }
                }
            }
        }
        File.WriteAllText(Path.Combine(target, "extracted.txt"),
                          DateTime.UtcNow.ToString("o") + Environment.NewLine);
    }

    public static int Main(string[] args)
    {
        try
        {
            string target = InstallDir();
            if (args.Length > 0 && args[0] == "--where")
            {
                Console.WriteLine(target);
                return 0;
            }
            bool extractOnly = args.Length > 0 && args[0] == "--extract";
            if (!File.Exists(Path.Combine(target, "extracted.txt")))
            {
                Extract(target);
            }
            if (extractOnly)
            {
                Console.WriteLine(target);
                return 0;
            }
            string launcher = Path.Combine(target, @"backend\DropBridge.exe");
            if (!File.Exists(launcher))
            {
                launcher = Path.Combine(target, @"backend\AirBridge32.exe");
            }
            if (!File.Exists(launcher))
            {
                launcher = Path.Combine(target, "Start.cmd");
            }
            if (!File.Exists(launcher))
            {
                Console.Error.WriteLine("DropBridge: no launcher in " + target);
                return 2;
            }
            var start = new ProcessStartInfo(launcher)
            {
                WorkingDirectory = target,
                UseShellExecute = true,
                Arguments = "--root \"" + target + "\" --data \"" + DataDir() + "\"",
            };
            Process.Start(start);
            PruneOldBuilds(target);
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("DropBridge: " + error.Message);
            return 1;
        }
    }
}

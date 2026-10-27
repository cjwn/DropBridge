// Q4 prototype: one exe that carries the headless package and unpacks it.
//
// Deliberately small and dependency-free: the compiler that ships with Windows
// (.NET Framework 4.8) builds it, and System.IO.Compression reads the payload,
// so the result needs no installer, no Python, no Flutter and no extra runtime
// on Windows 10/11.
//
//   AirBridge32.exe            unpack (once per build) and start Start.cmd
//   AirBridge32.exe --extract  unpack only, print the directory
//   AirBridge32.exe --where    print the directory without unpacking
//
// The directory is versioned by the exe's own timestamp, so a new build never
// overwrites the data of the previous one.
using System;
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

    private static string InstallDir()
    {
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return Path.Combine(Path.Combine(local, "AirBridge32"), VersionTag());
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
            string launcher = Path.Combine(target, @"backend\AirBridge32.exe");
            if (!File.Exists(launcher))
            {
                launcher = Path.Combine(target, "Start.cmd");
            }
            if (!File.Exists(launcher))
            {
                Console.Error.WriteLine("AirBridge32: no launcher in " + target);
                return 2;
            }
            var start = new ProcessStartInfo(launcher)
            {
                WorkingDirectory = target,
                UseShellExecute = true,
            };
            Process.Start(start);
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("AirBridge32: " + error.Message);
            return 1;
        }
    }
}

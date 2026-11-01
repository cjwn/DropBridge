// Q5: the user-facing supervisor and tray. One program starts the backend
// processes in order, keeps them alive, reports what happened, and stops them
// cleanly.
//
// Built with the in-box C# compiler (.NET Framework 4.8), so Windows 10/11
// needs no extra runtime. Compiled as winexe: no console window.
//
// Command line (all optional; double-click starts the tray):
//   --root <dir>       package root that contains backend\ (default: parent of backend\)
//   --check [--out f]  validate paths and print the plan, start nothing
//   --smoke <seconds>  start, wait, stop, write data\logs\<session>\smoke.txt
//   --auto-exit <s>    tray test hook: quit cleanly after <s> seconds
//   --autostart-on | --autostart-off
//   --chanseq social|fixed  diagnostic A/B; default remains social
//   --no-tcp-merge       pass -j 0 to awdld; default remains -j 400
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Management;
using System.Globalization;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

// Q55: every backend lives in a Windows job object with KILL_ON_JOB_CLOSE, so
// when this process dies - including Stop-Process/TerminateProcess, which run no
// cleanup at all - the OS tears the whole chain down with it. That is what used
// to leave airbridge_radio/awdl_netd/python holding 49876/49877/8771 and made
// the next start fail with "port already in use".
internal static class ChildJob
{
    private const int JobObjectExtendedLimitInformation = 9;
    private const uint JobObjectLimitKillOnJobClose = 0x2000;

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct BasicLimit
    {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
    }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct IoCounters
    {
        public ulong ReadOperationCount;
        public ulong WriteOperationCount;
        public ulong OtherOperationCount;
        public ulong ReadTransferCount;
        public ulong WriteTransferCount;
        public ulong OtherTransferCount;
    }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct ExtendedLimit
    {
        public BasicLimit BasicLimitInformation;
        public IoCounters IoInfo;
        public UIntPtr ProcessMemoryLimit;
        public UIntPtr JobMemoryLimit;
        public UIntPtr PeakProcessMemoryUsed;
        public UIntPtr PeakJobMemoryUsed;
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private static extern IntPtr CreateJobObject(IntPtr attributes, string name);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedLimit info, int length);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

    private static IntPtr _job = IntPtr.Zero;

    public static void Ensure()
    {
        if (_job != IntPtr.Zero) return;
        try
        {
            IntPtr job = CreateJobObject(IntPtr.Zero, null);
            if (job == IntPtr.Zero) return;
            var info = new ExtendedLimit();
            info.BasicLimitInformation.LimitFlags = JobObjectLimitKillOnJobClose;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, ref info,
                    System.Runtime.InteropServices.Marshal.SizeOf(typeof(ExtendedLimit))))
            {
                return;
            }
            _job = job;
        }
        catch
        {
        }
    }

    /// <summary>Put a freshly started child under the kill-on-close job.</summary>
    public static void Add(Process process)
    {
        try
        {
            Ensure();
            if (_job != IntPtr.Zero && process != null)
                AssignProcessToJobObject(_job, process.Handle);
        }
        catch
        {
        }
    }
}

internal sealed class LogSink
{
    private readonly object _gate = new object();
    private readonly string _dir;
    private readonly bool _enabled;

    public LogSink(string dir, bool enabled)
    {
        _dir = dir;
        _enabled = enabled;
        if (_enabled) Directory.CreateDirectory(dir);
    }

    public string DirectoryPath { get { return _dir; } }

    public void Write(string name, string line)
    {
        if (!_enabled) return;
        lock (_gate)
        {
            try
            {
                string path = Path.Combine(_dir, name + ".log");
                var info = new FileInfo(path);
                if (info.Exists && info.Length > 5 * 1024 * 1024)
                {
                    string rotated = path + ".1";
                    if (File.Exists(rotated)) File.Delete(rotated);
                    File.Move(path, rotated);
                }
                File.AppendAllText(path,
                    DateTime.Now.ToString("HH:mm:ss.fff") + " " + line + Environment.NewLine,
                    Encoding.UTF8);
            }
            catch (IOException)
            {
                // A full disk must not take the supervisor down.
            }
        }
    }
}

internal sealed class Child
{
    public readonly string Name;
    public readonly ManualResetEventSlim Ready = new ManualResetEventSlim(false);
    private readonly string _exe;
    private readonly string _arguments;
    private readonly Dictionary<string, string> _environment;
    private readonly string _workdir;
    private readonly string _needle;
    private readonly LogSink _log;
    private readonly Action<string> _lineSeen;
    private Process _process;
    private long _lastOutputTicks = DateTime.UtcNow.Ticks;

    /// <summary>
    /// When this child last printed anything. The radio prints a stats line every
    /// ~5 s even when idle, so silence is the only reliable sign that it is stuck
    /// inside a USB call while still looking alive to the process table.
    /// </summary>
    public DateTime LastOutputUtc
    {
        get { return new DateTime(Interlocked.Read(ref _lastOutputTicks), DateTimeKind.Utc); }
    }

    public Child(string name, string exe, string arguments,
                 Dictionary<string, string> environment, string workdir,
                 string needle, LogSink log, Action<string> lineSeen = null)
    {
        Name = name;
        _exe = exe;
        _arguments = arguments;
        _environment = environment ?? new Dictionary<string, string>();
        _workdir = workdir;
        _needle = needle;
        _log = log;
        _lineSeen = lineSeen;
    }

    public bool HasExited
    {
        get { return _process == null || _process.HasExited; }
    }

    public bool Start(out string error)
    {
        error = null;
        if (!File.Exists(_exe))
        {
            error = "missing " + _exe;
            return false;
        }
        var start = new ProcessStartInfo(_exe, _arguments)
        {
            WorkingDirectory = _workdir,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (KeyValuePair<string, string> pair in _environment)
        {
            // OWL's opt-ins test getenv != NULL, so empty/"0" is not off.
            if (pair.Value == null) start.EnvironmentVariables.Remove(pair.Key);
            else start.EnvironmentVariables[pair.Key] = pair.Value;
        }
        try
        {
            _process = Process.Start(start);
        }
        catch (Exception problem)
        {
            error = problem.Message;
            return false;
        }
        // The radio is NOT put in the kill-on-close job: that would TerminateProcess
        // it mid-transfer and wedge the chip. It watches our process handle
        // instead (AIRBRIDGE_PARENT_PID) and unwinds by itself; if it ever hangs,
        // ReclaimLeftovers() on the next start clears it.
        if (Name != "radio") ChildJob.Add(_process);
        _process.OutputDataReceived += (sender, line) => OnLine(line.Data);
        _process.ErrorDataReceived += (sender, line) => OnLine(line.Data);
        _process.BeginOutputReadLine();
        _process.BeginErrorReadLine();
        _log.Write(Name, "started pid " + _process.Id + " (" + _exe + " " + _arguments + ")");
        return true;
    }

    private void OnLine(string line)
    {
        if (line == null) return;
        Interlocked.Exchange(ref _lastOutputTicks, DateTime.UtcNow.Ticks);
        _log.Write(Name, line);
        if (_lineSeen != null) _lineSeen(line);
        if (_needle != null && line.IndexOf(_needle, StringComparison.Ordinal) >= 0)
        {
            Ready.Set();
        }
    }

    /// <summary>
    /// Send one JSON command line to the child's stdin. The Python receiver reads
    /// {"do": "set", "name": ...} from there, which renames it without touching
    /// the radio. Other children simply never read stdin.
    /// </summary>
    public bool SendLine(string line)
    {
        try
        {
            if (_process == null || _process.HasExited) return false;
            _process.StandardInput.WriteLine(line);
            _process.StandardInput.Flush();
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    public bool WaitReady(int timeoutMilliseconds, out string error)
    {
        error = null;
        if (Ready.Wait(timeoutMilliseconds)) return true;
        error = HasExited
            ? Name + " exited before it was ready"
            : Name + " not ready after " + (timeoutMilliseconds / 1000) + " s";
        return false;
    }

    public void Stop(int waitMilliseconds)
    {
        if (_process == null) return;
        try
        {
            if (!_process.HasExited)
            {
                // Ask the radio to unwind first (see the radio's stop event):
                // killing it mid-transfer wedges the chip until a replug.
                if (Name == "radio") Supervisor.SignalRadioStop();
                if (Name == "radio") _process.WaitForExit(15000);
                if (!_process.HasExited)
                {
                    _process.Kill();
                    _process.WaitForExit(waitMilliseconds);
                }
            }
            _log.Write(Name, "stopped (exit " + _process.ExitCode + ")");
        }
        catch (InvalidOperationException)
        {
        }
        catch (System.ComponentModel.Win32Exception)
        {
        }
    }
}

internal sealed class Supervisor
{
    // Shared with airbridge_radio.exe: signalling it closes the radio's listener,
    // which makes it take the normal teardown path instead of being killed.
    private static EventWaitHandle _radioStop;

    public static void EnsureRadioStopEvent()
    {
        if (_radioStop == null)
        {
            _radioStop = new EventWaitHandle(false, EventResetMode.ManualReset,
                @"Local\DropBridgeRadioStop");
        }
    }

    public static void SignalRadioStop()
    {
        try
        {
            EnsureRadioStopEvent();
            _radioStop.Reset();
            _radioStop.Set();
        }
        catch
        {
        }
    }

    private readonly int _channel;
    private readonly int _width;
    public readonly string ChannelSequence;
    public readonly bool TcpMerge;
    // Opt-in PC -> phone: netd then also listens on 127.0.0.1:8772.
    public static bool AllowSend;
    // Receive spatial streams advertised by awdld. The RTL8821CU is 1T1R, so 1
    // is the honest value; --streams 2 exists only to A/B whether the extra
    // advertisement changes what the phone transmits (it is not a fix).
    public static int AwdldStreams = 1;
    public string RadioArguments { get { return _channel + " " + _width; } }
    // RTL8821CU is 1T1R; announcing a second receive stream exceeds the hardware.
    public string AwdldArguments { get { return "-c " + _channel + " -s " + AwdldStreams + " -j " + (TcpMerge ? "400" : "0") + " -v"; } }
    // 20 MHz is the default: measured 202 KB/s against 40 MHz's 190 KB/s after a
    // cold start, and 40 MHz has a degraded mode that needs a USB replug to clear.
    public string BandText { get { return "ch" + _channel + " / " + _width + " MHz"; } }
    private readonly string _root;
    private readonly string _backend;
    private readonly string _data;
    private readonly LogSink _log;
    private readonly List<Child> _children = new List<Child>();
    public readonly Settings Prefs;
    public volatile bool Running;
    public string LastError = "";

    public Supervisor(string root, string sessionStamp, bool prepareDirectories,
                      int channel = 149, int width = 20, string dataDirectory = null,
                      string channelSequence = "social", bool tcpMerge = true)
    {
        if (channelSequence != "social" && channelSequence != "fixed")
            throw new ArgumentException("chanseq must be social or fixed");
        _root = root;
        ChannelSequence = channelSequence;
        TcpMerge = tcpMerge;
        _channel = channel;
        _width = width;
        _backend = Path.Combine(root, "backend");
        // Portable layout: the payload lives in <folder>\app\<build>, but the
        // data belongs to the folder itself (<folder>\data) so upgrades and
        // restarts keep the keys, the AirDrop instance id and received files.
        _data = string.IsNullOrEmpty(dataDirectory)
            ? Path.Combine(root, "data")
            : Path.GetFullPath(dataDirectory);
        _log = new LogSink(Path.Combine(_data, "logs", sessionStamp), prepareDirectories);
        Prefs = Settings.Load(_data);
        if (prepareDirectories)
        {
            Directory.CreateDirectory(ReceivedDirectory);
            Directory.CreateDirectory(Path.Combine(_data, "keys"));
            PruneOldSessions(Path.Combine(_data, "logs"), 10);
        }
    }

    public string ReceivedDirectory { get { return Path.Combine(_data, "received"); } }
    public string LogDirectory { get { return _log.DirectoryPath; } }
    public string Root { get { return _root; } }

    public Dictionary<string, string> AwdldEnvironment()
    {
        return new Dictionary<string, string>
        {
            { "AWDL_ELECTION_METRIC", "200" },
            { "AWDL_ELECTION_COUNTER", "100000" },
            { "AWDL_CHANSEQ", ChannelSequence == "social" ? "social" : null },
            { "AWDL_FOLLOW_MASTER_SEQ", null },
        };
    }

    /* Latest peer frame rate, parsed from the radio's 5 s "peer frames=N"
     * sample. A transfer at ~800 frames/s is the ~1 MB/s case; the 250-360
     * KB/s runs sit near 200. */
    public double Fps { get; private set; }
    public long FpsSamples { get; private set; }

    /* AWDL mastership: as the sync master the phone uses our all-149 MHz
     * sequence and transfers are ~3x faster. An iPhone's election counter
     * outranks ours if it ever wins, so the supervisor watches for the address
     * in "sync master is now X" drifting away from "AWDL address X". */
    public string SelfAddress { get; private set; }
    public string SyncMaster { get; private set; }

    public bool LostMastership
    {
        get
        {
            return !string.IsNullOrEmpty(SelfAddress) && !string.IsNullOrEmpty(SyncMaster) &&
                   !string.Equals(SelfAddress, SyncMaster, StringComparison.OrdinalIgnoreCase);
        }
    }

    private void OnChildLine(string line)
    {
        const string peerMarker = "peer frames=";
        int at = line.IndexOf(peerMarker, StringComparison.Ordinal);
        if (at >= 0)
        {
            int start = at + peerMarker.Length;
            int end = line.IndexOf(' ', start);
            string value = end < 0 ? line.Substring(start) : line.Substring(start, end - start);
            int frames;
            if (int.TryParse(value, out frames))
            {
                Fps = frames / 5.0; // Compatibility with old radio builds.
                const string fpsMarker = " fps=";
                int fpsAt = line.IndexOf(fpsMarker, StringComparison.Ordinal);
                if (fpsAt >= 0)
                {
                    int fpsStart = fpsAt + fpsMarker.Length;
                    int fpsEnd = line.IndexOf(' ', fpsStart);
                    string fpsValue = fpsEnd < 0 ? line.Substring(fpsStart)
                        : line.Substring(fpsStart, fpsEnd - fpsStart);
                    double measured;
                    if (double.TryParse(fpsValue, NumberStyles.Float,
                        CultureInfo.InvariantCulture, out measured)) Fps = measured;
                }
                FpsSamples++;
            }
            return;
        }
        const string selfMarker = "AWDL address ";
        at = line.IndexOf(selfMarker, StringComparison.Ordinal);
        if (at >= 0)
        {
            SelfAddress = AddressAfter(line, at + selfMarker.Length);
            return;
        }
        const string masterMarker = "sync master is now ";
        at = line.IndexOf(masterMarker, StringComparison.Ordinal);
        if (at >= 0)
        {
            SyncMaster = AddressAfter(line, at + masterMarker.Length);
        }
    }

    private static string AddressAfter(string line, int start)
    {
        if (start >= line.Length) return null;
        int end = line.IndexOf(' ', start);
        string value = end < 0 ? line.Substring(start) : line.Substring(start, end - start);
        value = value.Trim().TrimEnd(',');
        return value.Length == 17 ? value : null;
    }

    public void Log(string message)
    {
        _log.Write("supervisor", message);
    }

    public bool AllAlive
    {
        get
        {
            if (_children.Count == 0) return false;
            foreach (Child child in _children)
            {
                if (child.HasExited) return false;
            }
            return true;
        }
    }

    private static void PruneOldSessions(string logsRoot, int keep)
    {
        try
        {
            var directories = new List<DirectoryInfo>(new DirectoryInfo(logsRoot).GetDirectories());
            if (directories.Count <= keep) return;
            directories.Sort((left, right) => right.CreationTimeUtc.CompareTo(left.CreationTimeUtc));
            for (int index = keep; index < directories.Count; index++)
            {
                try { directories[index].Delete(true); } catch (IOException) { }
            }
        }
        catch (DirectoryNotFoundException)
        {
        }
    }

    private string RadioPath() { return Path.Combine(_backend, "airbridge_radio.exe"); }
    private string BridgePath() { return Path.Combine(_backend, "radio_pipe_bridge.exe"); }
    private string NetdPath() { return Path.Combine(_backend, "awdl_netd.exe"); }
    private string AwdldPath() { return Path.Combine(_backend, "awdld.exe"); }
    private string PythonPath() { return Path.Combine(_backend, "python", "python.exe"); }
    private string ReceiverPath() { return Path.Combine(_backend, "airdrop", "receiver.py"); }
    private string AddressFile() { return Path.Combine(_data, "awdl_netd.addr"); }

    public List<string> Validate()
    {
        var missing = new List<string>();
        foreach (string path in new[]
                 {
                     RadioPath(), BridgePath(), NetdPath(), AwdldPath(),
                     ReceiverPath(), PythonPath(),
                 })
        {
            if (!File.Exists(path)) missing.Add(path);
        }
        return missing;
    }

    public string Plan()
    {
        var text = new StringBuilder();
        text.AppendLine("root: " + _root);
        text.AppendLine("logs: " + LogDirectory);
        text.AppendLine("radio: " + RadioPath() + " " + RadioArguments);
        text.AppendLine("bridge: " + BridgePath());
        text.AppendLine("netd: " + NetdPath() + "  [AWDL_NETD_MDNS_PROXY=1]" +
                        (AllowSend ? "  [AWDL_NETD_OUTBOUND=8772: PC -> phone]" : ""));
        text.AppendLine("awdld: " + AwdldPath() + " " + AwdldArguments);
        text.AppendLine("tcp_merge: " + (TcpMerge ? "on (join_us=400)" : "off (join_us=0)"));
        text.AppendLine("streams: " + AwdldStreams + (AwdldStreams == 1 ? " (honest for 1T1R)" : " (A/B only)"));
        text.AppendLine("chanseq: " + ChannelSequence + " [slot 8=" +
            (ChannelSequence == "social" ? "6" : _channel.ToString()) + "]");
        foreach (var pair in AwdldEnvironment())
            text.AppendLine("  " + pair.Key + "=" + (pair.Value ?? "<unset>"));
        text.AppendLine("receiver: " + PythonPath() + " -u " + ReceiverPath());
        text.AppendLine("device: " + Prefs.DeviceName + " (beacon " + (Prefs.Beacon ? "on" : "off") + ")");
        foreach (string problem in Validate()) text.AppendLine("MISSING: " + problem);
        return text.ToString();
    }

    private Child NewReceiver()
    {
        return new Child("receiver", PythonPath(),
            "-u \"" + ReceiverPath() + "\" --name \"" + Prefs.DeviceName + "\" --listen 127.0.0.1" +
            " --mdns-address-file \"" + AddressFile() + "\"" +
            " --outdir \"" + ReceivedDirectory + "\"" +
            " --keys \"" + Path.Combine(_data, "keys") + "\" --trace",
            new Dictionary<string, string> { { "PYTHONUNBUFFERED", "1" } },
            _root, "receiving as", _log);
    }

    public List<string> DeadNames()
    {
        var names = new List<string>();
        foreach (Child child in _children)
        {
            if (child.HasExited) names.Add(child.Name);
        }
        return names;
    }

    /// <summary>Seconds since the named child last wrote a line, or -1 if unknown.</summary>
    public double SilentSeconds(string name)
    {
        foreach (Child child in _children)
        {
            if (child.Name != name) continue;
            if (child.HasExited) return -1;
            return (DateTime.UtcNow - child.LastOutputUtc).TotalSeconds;
        }
        return -1;
    }

    /// <summary>
    /// Rename the receiver in place. The Python receiver reads JSON commands from
    /// stdin ({"do": "set", "name": ...}) and re-announces itself, so the radio
    /// and the rest of the chain stay untouched. Returns false when the receiver
    /// is not running -- the name then takes effect on its next start.
    /// </summary>
    public bool RenameReceiver(string name, out string error)
    {
        error = null;
        foreach (Child child in _children)
        {
            if (child.Name != "receiver") continue;
            string json = "{\"do\": \"set\", \"name\": \"" +
                          name.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"}";
            if (!child.SendLine(json))
            {
                error = "receiver is not accepting commands";
                return false;
            }
            Log("receiver renamed to " + name);
            return true;
        }
        error = "receiver is not running";
        return false;
    }

    // The receiver is the only backend that can be replaced without touching the
    // radio (which needs ~40 s to re-initialise). Any other death still means a
    // full restart.
    public bool RestartReceiver(out string error)
    {
        for (int index = _children.Count - 1; index >= 0; index--)
        {
            if (_children[index].Name == "receiver") _children.RemoveAt(index);
        }
        Child receiver = NewReceiver();
        _children.Add(receiver);
        if (!receiver.Start(out error)) return false;
        if (!receiver.WaitReady(60000, out error)) return false;
        Running = true;
        return true;
    }

    // A stale instance holding a port is the most common start failure; report
    // it before the radio is touched instead of after a 40 s init.
    private static string PortProblem()
    {
        foreach (int port in new[] { 49876, 49877, 8771, 15353 })
        {
            try
            {
                var listener = new System.Net.Sockets.TcpListener(
                    System.Net.IPAddress.Loopback, port);
                listener.Start();
                listener.Stop();
            }
            catch (System.Net.Sockets.SocketException)
            {
                return "port " + port + " is already in use (another DropBridge instance?)";
            }
        }
        return null;
    }

    /// <summary>
    /// Kill leftover backends from a previous run that died abruptly. Only
    /// processes whose executable sits under our own package root qualify, so a
    /// user's unrelated program on the same port can never be touched.
    /// Returns how many were reclaimed.
    /// </summary>
    public int ReclaimLeftovers()
    {
        int killed = 0;
        try
        {
            using (var searcher = new ManagementObjectSearcher(
                       "SELECT ProcessId, ExecutablePath FROM Win32_Process WHERE Name IN " +
                       "('airbridge_radio.exe','radio_pipe_bridge.exe','awdl_netd.exe','awdld.exe','python.exe')"))
            {
                string root = Path.GetFullPath(_root).TrimEnd('\\') + "\\";
                foreach (ManagementObject item in searcher.Get())
                {
                    string path = item["ExecutablePath"] as string;
                    if (string.IsNullOrEmpty(path)) continue;
                    if (!path.StartsWith(root, StringComparison.OrdinalIgnoreCase)) continue;
                    int pid = Convert.ToInt32(item["ProcessId"], CultureInfo.InvariantCulture);
                    try
                    {
                        Process leftover = Process.GetProcessById(pid);
                        leftover.Kill();
                        leftover.WaitForExit(3000);
                        killed++;
                        Log("reclaimed leftover " + Path.GetFileName(path) + " pid " + pid);
                    }
                    catch
                    {
                    }
                }
            }
            if (killed > 0) Thread.Sleep(1500);
        }
        catch
        {
        }
        return killed;
    }

    public bool StartAll(int readyTimeoutSeconds, out string error)
    {
        error = null;
        EnsureRadioStopEvent();
        ChildJob.Ensure();
        string busy = PortProblem();
        if (busy != null)
        {
            // A previous run that was killed outright leaves backends holding
            // 49876/49877/8771/15353. Reclaim only our own package's processes,
            // then look again before declaring failure.
            int reclaimed = ReclaimLeftovers();
            if (reclaimed > 0) busy = PortProblem();
        }
        if (busy != null)
        {
            error = busy;
            return false;
        }
        List<string> missing = Validate();
        if (missing.Count > 0)
        {
            error = "missing files: " + string.Join(", ", missing.ToArray());
            return false;
        }
        if (File.Exists(AddressFile())) File.Delete(AddressFile());
        _log.Write("supervisor", "start chain root=" + _root + " args=" + RadioArguments +
            " chanseq=" + ChannelSequence + " join_us=" + (TcpMerge ? "400" : "0"));

        // warn keeps the radio log quiet; AIRBRIDGE_RADIO_LOG=info turns on the
        // per-5-s link counters (rx cb/crc) used when diagnosing a transfer.
        string radioLevel = Environment.GetEnvironmentVariable("AIRBRIDGE_RADIO_LOG");
        if (string.IsNullOrEmpty(radioLevel)) radioLevel = "warn";
        var radio = new Child("radio", RadioPath(), RadioArguments,
            new Dictionary<string, string>
            {
                { "AIRBRIDGE_LOG_LEVEL", radioLevel },
                // Keep the 5 s link counters at warn level so the rate watchdog
                // works without the per-frame noise info level produces.
                { "AIRBRIDGE_LINK_STATS", "1" },
                // The advertised sequence keeps the social channel (6) in slot
                // 8 so an iPhone accepts it; the firmware fast path makes that
                // cross-band hop ~2 ms instead of ~90 ms.
                { "AIRBRIDGE_FASTRETUNE_FW", "2" },
                // The radio watches this process and stops cleanly when it dies,
                // even when that death is a force kill (Q55).
                { "AIRBRIDGE_PARENT_PID",
                  System.Diagnostics.Process.GetCurrentProcess().Id.ToString() },
            },
            _root, "listening on 127.0.0.1:49876", _log, OnChildLine);
        var bridge = new Child("bridge", BridgePath(), "",
            null, _root, "waiting for awdld", _log);
        var netd = new Child("netd", NetdPath(), "",
            new Dictionary<string, string>
            {
                { "AWDL_NETD_MDNS_PROXY", "1" },
                { "AWDL_NETD_ADDR_FILE", AddressFile() },
                { "AWDL_NETD_OUTBOUND", AllowSend ? "8772" : null },
            }, _root, "waiting for awdld", _log);
        // As the AWDL sync master the phone keeps its data in our 149 MHz slots
        // and throughput roughly doubles; as a slave it only uses ~2/16 slots.
        // The counter decides the election before the metric (OWL never raises
        // its own counter, an iPhone's grows every 3.14 s of mastership), so a
        // fixed high value keeps us master for the whole session.
        var awdld = new Child("awdld", AwdldPath(), AwdldArguments, AwdldEnvironment(),
            _root, "host interface", _log, OnChildLine);
        var receiver = NewReceiver();

        _children.Clear();
        _children.Add(radio);
        _children.Add(bridge);
        _children.Add(netd);
        _children.Add(awdld);
        _children.Add(receiver);

        int timeout = readyTimeoutSeconds * 1000;
        foreach (Child child in _children)
        {
            if (!child.Start(out error))
            {
                StopAll();
                return false;
            }
            if (child == receiver)
            {
                DateTime deadline = DateTime.UtcNow.AddSeconds(readyTimeoutSeconds);
                while (!File.Exists(AddressFile()) && DateTime.UtcNow < deadline)
                {
                    Thread.Sleep(250);
                }
                if (!File.Exists(AddressFile()))
                {
                    error = "awdl_netd address file never appeared";
                    StopAll();
                    return false;
                }
            }
            int childTimeout = child == awdld ? Math.Max(timeout, 90000) : timeout;
            if (child == radio)
            {
                // A firmware boot failure retries for ~80 s before the next
                // attempt; 180 s ran out mid-retry once.
                childTimeout = Math.Max(childTimeout, 300000);
            }
            if (!child.WaitReady(childTimeout, out error))
            {
                StopAll();
                return false;
            }
        }
        Running = true;
        return true;
    }

    public void StopAll()
    {
        for (int index = _children.Count - 1; index >= 0; index--)
        {
            _children[index].Stop(5000);
        }
        _children.Clear();
        Running = false;
    }
}

internal static class RadioBinding
{
    // One interface only: the Wi-Fi half of the RTL8821CU. MI_00 (Bluetooth) and
    // the composite parent are never named here. WQL treats a backslash as an
    // escape, so the runtime string needs doubled backslashes.
    private const string Pattern = "USB\\\\VID_0BDA&PID_C820&MI_02";

    internal static bool IsAdmin()
    {
        try
        {
            var identity = System.Security.Principal.WindowsIdentity.GetCurrent();
            var principal = new System.Security.Principal.WindowsPrincipal(identity);
            return principal.IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);
        }
        catch
        {
            return false;
        }
    }

    /// <summary>Service of the MI_02 interface, "" when present without one, null when absent.</summary>
    internal static string Service()
    {
        try
        {
            using (var searcher = new ManagementObjectSearcher(
                       "SELECT DeviceID, Service FROM Win32_PnPEntity WHERE DeviceID LIKE '" + Pattern + "%'"))
            {
                foreach (ManagementObject item in searcher.Get())
                {
                    string service = item["Service"] as string;
                    return service ?? string.Empty;
                }
            }
        }
        catch
        {
        }
        return null;
    }

    internal static bool Bound(string service)
    {
        return string.Equals(service, "WinUSB", StringComparison.OrdinalIgnoreCase);
    }

    internal static string DriverDirectory(string root)
    {
        string[] candidates =
        {
            Path.Combine(root, "setup", "driver"),
            Path.Combine(root, "driver"),
            Path.Combine(root, "backend", "driver"),
        };
        foreach (string candidate in candidates)
        {
            if (File.Exists(Path.Combine(candidate, "airbridge-winusb.inf"))) return candidate;
        }
        return null;
    }

    /// <summary>
    /// Bind MI_02 to WinUSB, elevating once if needed. Idempotent: when the
    /// interface is already on WinUSB this returns 0 without touching anything.
    /// </summary>
    internal static int Install(string root, bool quiet, string logPath)
    {
        string service = Service();
        if (service == null)
        {
            Log(logPath, "install: MI_02 not present; plug the card in first");
            return 3;
        }
        if (Bound(service))
        {
            Log(logPath, "install: already bound to WinUSB; nothing to do");
            return 0;
        }
        if (!IsAdmin())
        {
            try
            {
                var start = new ProcessStartInfo(Application.ExecutablePath)
                {
                    UseShellExecute = true,
                    Verb = "runas",
                    Arguments = "--install-driver" + (quiet ? " --quiet" : string.Empty) +
                                " --root \"" + root + "\"",
                };
                using (Process child = Process.Start(start))
                {
                    child.WaitForExit();
                    Log(logPath, "install: elevated helper exit " + child.ExitCode);
                    return child.ExitCode;
                }
            }
            catch (Exception error)
            {
                Log(logPath, "install: elevation refused: " + error.Message);
                return 2;
            }
        }

        string directory = DriverDirectory(root);
        if (directory == null)
        {
            Log(logPath, "install: driver package missing under " + root);
            return 1;
        }
        string inf = Path.Combine(directory, "airbridge-winusb.inf");
        string cer = Path.Combine(directory, "dropbridge-driver.cer");
        if (!File.Exists(cer))
        {
            Log(logPath, "install: certificate missing next to the INF");
            return 1;
        }

        int code = Run("certutil.exe", "-addstore -f Root \"" + cer + "\"", logPath);
        if (code != 0) return 1;
        code = Run("certutil.exe", "-addstore -f TrustedPublisher \"" + cer + "\"", logPath);
        if (code != 0) return 1;
        code = Run("pnputil.exe", "/add-driver \"" + inf + "\" /install", logPath);
        if (code != 0) return 1;

        service = Service();
        bool bound = Bound(service);
        Log(logPath, "install: after pnputil, MI_02 service=" + (service ?? "<absent>"));
        return bound ? 0 : 1;
    }

    private static int Run(string exe, string arguments, string logPath)
    {
        try
        {
            var start = new ProcessStartInfo(exe, arguments)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
            };
            using (Process process = Process.Start(start))
            {
                string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
                process.WaitForExit(120000);
                Log(logPath, exe + " " + arguments + " -> exit " + process.ExitCode);
                foreach (string line in output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    Log(logPath, "  " + line.Trim());
                }
                return process.ExitCode;
            }
        }
        catch (Exception error)
        {
            Log(logPath, exe + " failed: " + error.Message);
            return 1;
        }
    }

    private static void Log(string logPath, string message)
    {
        try
        {
            if (string.IsNullOrEmpty(logPath)) return;
            Directory.CreateDirectory(Path.GetDirectoryName(logPath));
            File.AppendAllText(logPath,
                DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + " " + message + Environment.NewLine,
                Encoding.UTF8);
        }
        catch
        {
        }
    }
}

/* Q59: the notification area is light or dark depending on Settings >
 * Personalisation > Colours ("System mode"), and one set of pixels cannot stay
 * readable on both: black strokes vanish on a dark taskbar, white strokes vanish
 * on a light one. Both variants are embedded as manifest resources at build
 * time (tools\Build-Supervisor.ps1), so the exe stays self-contained and the
 * tray can swap icons when the user changes the system theme. A checkout that
 * built without the art falls back to the icon the exe carries. */
internal static class TrayIcons
{
    // Resources embedded by Build-Supervisor.ps1. The light-taskbar variant is
    // the black line art, and the same file is the exe icon.
    private const string ForLightTaskbar = "tray-light.ico";
    private const string ForDarkTaskbar = "tray-dark.ico";

    public static bool TaskbarIsLight()
    {
        try
        {
            using (RegistryKey key = Registry.CurrentUser.OpenSubKey(
                       @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
            {
                if (key != null)
                {
                    object value = key.GetValue("SystemUsesLightTheme");
                    if (value != null) return Convert.ToInt32(value) != 0;
                }
            }
        }
        catch (Exception)
        {
        }
        // Windows 11 ships with the light taskbar and that is also the variant
        // the exe carries, so an unreadable key is not a fatal condition.
        return true;
    }

    public static Icon ForTaskbar(bool lightTaskbar, Size size)
    {
        return Load(lightTaskbar ? ForLightTaskbar : ForDarkTaskbar, size);
    }

    /* Real display scale (1.75 on a 175 % monitor). The process is DPI-unaware on
     * purpose, so this has to be read on a temporarily aware thread. */
    public static double RealScale()
    {
        IntPtr previous = IntPtr.Zero;
        try
        {
            previous = SetThreadDpiAwarenessContext(new IntPtr(PerMonitorAwareV2));
            uint dpi = GetDpiForSystem();
            if (dpi > 0) return dpi / 96.0;
        }
        catch (Exception)
        {
        }
        finally
        {
            if (previous != IntPtr.Zero)
            {
                try { SetThreadDpiAwarenessContext(previous); }
                catch (Exception) { }
            }
        }
        return 1.0;
    }

    /* Q59c: the supervisor is a DPI-unaware process, so SystemInformation.SmallIconSize
     * reports the 96-DPI value (16) even on a 175 % display where the taskbar draws
     * 28 px. Windows then stretched our 16 px frame, which looked thin and blurry.
     * Asking for the real size first gives a frame that is drawn 1:1. The awareness
     * switch is thread-scoped and put back immediately, so the rest of the UI keeps
     * its current (unaware) layout. */
    public static int SmallIconPixels()
    {
        IntPtr previous = IntPtr.Zero;
        try
        {
            previous = SetThreadDpiAwarenessContext(new IntPtr(PerMonitorAwareV2));
            uint dpi = GetDpiForSystem();
            if (dpi == 0) dpi = 96;
            int pixels = GetSystemMetricsForDpi(SmCxSmIcon, dpi);
            if (pixels > 0) return pixels;
        }
        catch (Exception)
        {
        }
        finally
        {
            if (previous != IntPtr.Zero)
            {
                try { SetThreadDpiAwarenessContext(previous); }
                catch (Exception) { }
            }
        }
        return SystemInformation.SmallIconSize.Width;
    }

    private const int SmCxSmIcon = 49;
    private const int PerMonitorAwareV2 = -4;

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern uint GetDpiForSystem();

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern int GetSystemMetricsForDpi(int index, uint dpi);

    private static Icon Load(string name, Size size)
    {
        try
        {
            using (Stream stream = typeof(TrayIcons).Assembly.GetManifestResourceStream(name))
            {
                if (stream == null) return null;
                return new Icon(stream, size);
            }
        }
        catch (Exception)
        {
            return null;
        }
    }
}

/* Q60: small modal used by the tray's "设备名称…" entry. The validation mirrors
 * Settings.CleanName(), which in turn mirrors the receiver's valid_name(). */
internal sealed class DeviceNameDialog : Form
{
    public string Value { get; private set; }

    public DeviceNameDialog(string current)
    {
        Text = "设备名称";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterScreen;
        MaximizeBox = false;
        MinimizeBox = false;
        ClientSize = new Size(400, 132);
        var label = new Label
        {
            Text = "iPhone 隔空投送里显示的名称（1–63 个字符）：",
            AutoSize = true,
            Location = new Point(12, 12),
        };
        var box = new TextBox { Text = current ?? string.Empty, Location = new Point(12, 40), Width = 372 };
        var ok = new Button { Text = "保存", Location = new Point(220, 84), Width = 80 };
        var cancel = new Button
        {
            Text = "取消",
            DialogResult = DialogResult.Cancel,
            Location = new Point(310, 84),
            Width = 80,
        };
        ok.Click += delegate
        {
            string clean = Settings.CleanName(box.Text);
            if (clean == null)
            {
                MessageBox.Show(this,
                    "名称需要 1–63 个字符，且不能包含 \\ / : * ? \" < > | 或控制字符。",
                    "设备名称", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            Value = clean;
            DialogResult = DialogResult.OK;
        };
        AcceptButton = ok;
        CancelButton = cancel;
        Controls.Add(label);
        Controls.Add(box);
        Controls.Add(ok);
        Controls.Add(cancel);
        ActiveControl = box;
        box.SelectAll();
    }
}

/* Q61: a received file used to produce a plain balloon. Windows toasts cannot
 * carry custom buttons for a portable exe (that needs a registered COM
 * activator), so the picture arrives in this small no-activation popup instead,
 * with a one-click "copy to clipboard" that puts both the bitmap and the file
 * itself on the clipboard. It never takes focus from whatever the user is
 * doing. */
internal sealed class ReceivedPopup : Form
{
    private static readonly List<ReceivedPopup> Open = new List<ReceivedPopup>();
    private const int PopupWidth = 344;

    private readonly string _path;
    private readonly Image _preview;
    private readonly Action<string> _log;
    private readonly Action<string> _reveal;
    private readonly System.Windows.Forms.Timer _life;
    private readonly Button _copy;

    public ReceivedPopup(string deviceName, string path, string fileName, long bytes,
                         Image preview, Action<string> log, Action<string> reveal)
    {
        _path = path;
        _preview = preview;
        _log = log;
        _reveal = reveal;

        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        ShowInTaskbar = false;
        TopMost = true;
        BackColor = Color.FromArgb(250, 250, 250);
        Width = PopupWidth;
        Height = preview == null ? 120 : 216;

        var icon = new PictureBox
        {
            Image = AppIdentity.IconForPopup(),
            Location = new Point(12, 12),
            Size = new Size(28, 28),
            SizeMode = PictureBoxSizeMode.Zoom,
        };
        var title = new Label
        {
            Text = deviceName,
            Font = new Font(Font, FontStyle.Bold),
            AutoEllipsis = true,
            Location = new Point(48, 10),
            Size = new Size(230, 20),
        };
        var name = new Label
        {
            Text = fileName,
            AutoEllipsis = true,
            Location = new Point(48, 30),
            Size = new Size(230, 18),
        };
        var info = new Label
        {
            Text = FormatSize(bytes),
            ForeColor = Color.DimGray,
            Location = new Point(48, 48),
            Size = new Size(230, 18),
        };
        var close = new Button
        {
            Text = "×",
            FlatStyle = FlatStyle.Flat,
            Size = new Size(24, 24),
            Location = new Point(Width - 32, 8),
        };
        close.FlatAppearance.BorderSize = 0;
        close.Click += delegate { Dismiss(); };

        _copy = new Button
        {
            Text = preview == null ? "复制文件" : "复制图片",
            FlatStyle = FlatStyle.System,
            Size = new Size(96, 28),
            Location = new Point(12, Height - 40),
        };
        _copy.Click += delegate { CopyToClipboard(); };

        var revealButton = new Button
        {
            Text = "打开文件夹",
            FlatStyle = FlatStyle.System,
            Size = new Size(96, 28),
            Location = new Point(116, Height - 40),
        };
        revealButton.Click += delegate
        {
            _reveal(_path);
            Dismiss();
        };

        Controls.Add(icon);
        Controls.Add(title);
        Controls.Add(name);
        Controls.Add(info);
        Controls.Add(close);
        Controls.Add(_copy);
        Controls.Add(revealButton);
        if (preview != null)
        {
            Controls.Add(new PictureBox
            {
                Image = preview,
                Location = new Point(12, 70),
                Size = new Size(Width - 24, 100),
                SizeMode = PictureBoxSizeMode.Zoom,
            });
        }

        _life = new System.Windows.Forms.Timer { Interval = 12000 };
        _life.Tick += delegate { Dismiss(); };
        MouseEnter += delegate { _life.Stop(); };
        MouseLeave += delegate { if (!_life.Enabled) _life.Start(); };
        foreach (Control control in Controls)
        {
            control.MouseEnter += delegate { _life.Stop(); };
            control.MouseLeave += delegate { if (!_life.Enabled) _life.Start(); };
        }
    }

    /* A no-activation tool window: clicks work, but the popup never steals the
     * keyboard focus from the app the user is typing in. */
    protected override bool ShowWithoutActivation
    {
        get { return true; }
    }

    protected override CreateParams CreateParams
    {
        get
        {
            CreateParams parameters = base.CreateParams;
            parameters.ExStyle |= 0x08000000; // WS_EX_NOACTIVATE
            parameters.ExStyle |= 0x00000080; // WS_EX_TOOLWINDOW
            return parameters;
        }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        using (var pen = new Pen(Color.FromArgb(210, 210, 210)))
        {
            e.Graphics.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
        }
    }

    public static void ShowOne(ReceivedPopup popup)
    {
        while (Open.Count >= 3) Open[0].Dismiss();
        Rectangle area = Screen.PrimaryScreen.WorkingArea;
        // The process is DPI-unaware: WinForms reports the physical screen size,
        // while the shell positions windows in 96-dpi virtual coordinates. On a
        // 175 % display the difference put the popup off-screen, so convert.
        double scale = TrayIcons.RealScale();
        if (scale > 1.001)
        {
            area = new Rectangle(0, 0, (int)Math.Round(area.Width / scale),
                (int)Math.Round(area.Height / scale));
        }
        int top = area.Bottom - popup.Height - 16;
        foreach (ReceivedPopup other in Open) top -= other.Height + 8;
        if (top < area.Top + 16) top = area.Top + 16;
        popup.Location = new Point(area.Right - popup.Width - 16, top);
        Open.Add(popup);
        popup.Show();
        // A no-activation window is not raised by Show() alone, so ask for the top
        // of the topmost band explicitly; SWP_NOACTIVATE keeps the focus where it is.
        SetWindowPos(popup.Handle, HwndTopmost, 0, 0, 0, 0,
            SwpNoSize | SwpNoMove | SwpNoActivate);
        popup._life.Start();
    }

    private static readonly IntPtr HwndTopmost = new IntPtr(-1);
    private const uint SwpNoSize = 0x0001;
    private const uint SwpNoMove = 0x0002;
    private const uint SwpNoActivate = 0x0010;

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr hWnd, IntPtr insertAfter, int x, int y,
        int width, int height, uint flags);

    public static void CloseAll()
    {
        foreach (ReceivedPopup popup in Open.ToArray()) popup.Dismiss();
    }

    private void Dismiss()
    {
        _life.Stop();
        Open.Remove(this);
        if (IsDisposed) return;
        Hide();
        Dispose();
    }

    private void CopyToClipboard()
    {
        try
        {
            var data = new DataObject();
            Image image = null;
            try
            {
                using (Image loaded = Image.FromFile(_path)) image = new Bitmap(loaded);
            }
            catch (Exception)
            {
            }
            if (image != null) data.SetData(DataFormats.Bitmap, true, image);
            data.SetData(DataFormats.FileDrop, true, new[] { _path });
            Clipboard.SetDataObject(data, true);
            _copy.Text = "已复制";
            _copy.Enabled = false;
            _log("copied to clipboard: " + _path);
        }
        catch (Exception error)
        {
            _log("clipboard copy failed: " + error.Message);
            _copy.Text = "复制失败";
        }
    }

    private static string FormatSize(long bytes)
    {
        if (bytes <= 0) return "";
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return (bytes / 1024) + " KB";
        return (bytes / (1024 * 1024)) + " MB";
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            if (_preview != null) _preview.Dispose();
            if (_life != null) _life.Dispose();
        }
        base.Dispose(disposing);
    }
}

internal sealed class TrayApp : ApplicationContext
{
    private readonly Supervisor _supervisor;
    private readonly NotifyIcon _icon;
    private Icon _trayIcon;
    private bool _taskbarLight;
    private int _trayPixels;
    private readonly ToolStripMenuItem _status;
    private readonly ToolStripMenuItem _autostart;
    private readonly System.Windows.Forms.Timer _watchdog;
    private SynchronizationContext _ui;
    private FileSystemWatcher _watcher;
    private bool _reportedExit;
    private int _slowSamples;
    private bool _slowWarned;
    private bool _masterWarned;
    private int _starting;
    private readonly object _startGate = new object();
    private int _failures;
    private DateTime _windowStart = DateTime.UtcNow;
    private bool _gaveUp;
    private Process _beacon;
    private bool _beaconWanted = true;
    private ToolStripMenuItem _beaconItem;
    private ToolStripMenuItem _nameItem;
    private string _beaconLog;
    private readonly Settings _prefs;

    public TrayApp(Supervisor supervisor, int autoExitSeconds)
    {
        _supervisor = supervisor;
        _prefs = supervisor.Prefs;
        var menu = new ContextMenuStrip();
        _status = new ToolStripMenuItem("状态：启动中…") { Enabled = false };
        menu.Items.Add(_status);
        menu.Items.Add(new ToolStripMenuItem("打开接收文件夹", null,
            delegate { OpenFolder(_supervisor.ReceivedDirectory); }));
        menu.Items.Add(new ToolStripMenuItem("打开日志", null,
            delegate { OpenFolder(_supervisor.LogDirectory); }));
        menu.Items.Add(new ToolStripMenuItem("复制诊断信息", null,
            delegate { CopyDiagnostics(); }));
        _autostart = new ToolStripMenuItem("开机自启", null,
            delegate { ToggleAutostart(); }) { Checked = AutostartEnabled() };
        menu.Items.Add(_autostart);
        menu.Items.Add(new ToolStripMenuItem("安装驱动（首次使用）", null,
            delegate { InstallDriver(); }));
        _nameItem = new ToolStripMenuItem("设备名称：" + _prefs.DeviceName + "…", null,
            delegate { RenameDevice(); }) { ToolTipText = "iPhone 隔空投送里显示的名字；改完立即生效并记住" };
        menu.Items.Add(_nameItem);
        _beaconWanted = _prefs.Beacon;
        _beaconItem = new ToolStripMenuItem("蓝牙 beacon（帮 iPhone 发现这台电脑）", null,
            delegate { ToggleBeacon(); }) { Checked = _beaconWanted, ToolTipText = "周期性发送 Apple 的 AirDrop 蓝牙广播，让 iPhone 的 AWDL 保持唤醒；关掉后手机可能看不到本机" };
        menu.Items.Add(_beaconItem);
        menu.Items.Add(new ToolStripMenuItem("重新启动后端", null, delegate { StartBackends(); }));
        menu.Items.Add(new ToolStripMenuItem("重置链路", null, delegate
        {
            _slowSamples = 0;
            _slowWarned = false;
            RequestStart(false);
        }));
        menu.Items.Add(new ToolStripMenuItem("退出", null, delegate { Exit(); }));

        _taskbarLight = TrayIcons.TaskbarIsLight();
        _trayPixels = TrayIcons.SmallIconPixels();
        _trayIcon = TrayIcons.ForTaskbar(_taskbarLight, new Size(_trayPixels, _trayPixels)) ?? AppIcon();
        _icon = new NotifyIcon
        {
            Icon = _trayIcon,
            Text = _prefs.DeviceName,
            ContextMenuStrip = menu,
            Visible = true,
        };
        _icon.DoubleClick += delegate { OpenFolder(_supervisor.ReceivedDirectory); };
        SystemEvents.UserPreferenceChanged += OnUserPreferenceChanged;
        _icon.ShowBalloonTip(3000, _icon.Text, "正在启动后端…", ToolTipIcon.Info);

        Application.Idle += CaptureContext;
        StartWatcher();
        StartBackends();
        StartFirstRunCheck();
        OfferAutostartOnce();
        StartBeacon();

        _watchdog = new System.Windows.Forms.Timer { Interval = 5000 };
        _watchdog.Tick += delegate { CheckSilence(); CheckChildren(); CheckBeacon(); };
        _watchdog.Start();

        if (autoExitSeconds > 0)
        {
            var autoExit = new System.Windows.Forms.Timer { Interval = autoExitSeconds * 1000 };
            autoExit.Tick += delegate
            {
                autoExit.Stop();
                autoExit.Dispose();
                Exit();
            };
            autoExit.Start();
        }
    }

    private void CaptureContext(object sender, EventArgs e)
    {
        _ui = SynchronizationContext.Current;
        Application.Idle -= CaptureContext;
    }

    private void Ui(Action action)
    {
        SynchronizationContext context = _ui;
        if (context == null) return;
        context.Post(delegate { action(); }, null);
    }

    /* SystemEvents raises this for the WM_SETTINGCHANGE Windows broadcasts when
     * the user flips light/dark mode. It arrives on a broadcast thread, so the
     * actual icon swap is posted to the UI thread. */
    private void OnUserPreferenceChanged(object sender, UserPreferenceChangedEventArgs e)
    {
        if (e.Category != UserPreferenceCategory.General &&
            e.Category != UserPreferenceCategory.Color) return;
        bool light = TrayIcons.TaskbarIsLight();
        if (light == _taskbarLight) return;
        _taskbarLight = light;
        Ui(ApplyTrayIcon);
    }

    private void ApplyTrayIcon()
    {
        _trayPixels = TrayIcons.SmallIconPixels();
        Icon next = TrayIcons.ForTaskbar(_taskbarLight, new Size(_trayPixels, _trayPixels));
        if (next == null) return;
        Icon previous = _trayIcon;
        _trayIcon = next;
        _icon.Icon = next;
        if (previous != null && !ReferenceEquals(previous, SystemIcons.Application)) previous.Dispose();
    }

    private void StartWatcher()
    {
        try
        {
            _watcher = new FileSystemWatcher(_supervisor.ReceivedDirectory)
            {
                IncludeSubdirectories = false,
                EnableRaisingEvents = true,
            };
            _watcher.Created += delegate(object sender, FileSystemEventArgs args)
            {
                Thread.Sleep(300);
                string name = args.Name;
                long size = 0;
                try
                {
                    var info = new FileInfo(args.FullPath);
                    if (info.Exists) size = info.Length;
                }
                catch (IOException)
                {
                }
                Ui(delegate { ShowReceived(args.FullPath, name, size); });
            };
        }
        catch (Exception error)
        {
            _supervisor.LastError = "cannot watch received folder: " + error.Message;
        }
    }

    /* Pictures get the popup with the copy button; anything else stays a plain
     * balloon. HEIC and WebP often cannot be decoded by GDI+, so those still get
     * the popup but the button only offers the file itself. */
    private static readonly string[] PictureExtensions =
    {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".heic", ".webp",
    };

    private void ShowReceived(string path, string name, long bytes)
    {
        string extension = Path.GetExtension(name).ToLowerInvariant();
        if (Array.IndexOf(PictureExtensions, extension) < 0)
        {
            LogLine("received file (balloon): " + name);
            _icon.ShowBalloonTip(4000, _icon.Text,
                "收到文件：" + name + " (" + (bytes / 1024) + " KB)", ToolTipIcon.Info);
            return;
        }
        Image preview = null;
        try
        {
            using (Image loaded = Image.FromFile(path)) preview = new Bitmap(loaded);
        }
        catch (Exception)
        {
        }
        LogLine("received picture (popup, preview=" + (preview != null) + "): " + name);
        ReceivedPopup.ShowOne(new ReceivedPopup(_icon.Text, path, name, bytes, preview,
            LogLine, RevealFile));
    }

    private void LogLine(string message)
    {
        try { _supervisor.Log(message); }
        catch (Exception) { }
    }

    private static void RevealFile(string path)
    {
        try { Process.Start("explorer.exe", "/select,\"" + path + "\""); }
        catch (Exception) { }
    }

    private void StartBackends()
    {
        RequestStart(false);
    }

    private string DriverLog
    {
        get { return Path.Combine(_supervisor.Root, "data", "logs", "driver-install.txt"); }
    }

    // --- Bluetooth beacon ---------------------------------------------------
    // Apple devices only bring AWDL up while their AirDrop UI is active, unless
    // they hear the AirDrop BLE advertisement. Measured on this rig: with the
    // beacon the same 29 MB video ran 1.09-1.10 MB/s at ~900 fps and CRC 3-4%,
    // without it 0.32-0.88 MB/s at 300-800 fps and CRC 13-49%. So the tray keeps
    // it running by default; a marker file remembers a deliberate "off".
    private string BeaconScript()
    {
        string[] candidates =
        {
            Path.Combine(_supervisor.Root, "backend", "beacon.ps1"),
            Path.Combine(_supervisor.Root, "..", "upstream", "winawdl", "scripts", "airdrop-beacon.ps1"),
        };
        foreach (string candidate in candidates)
        {
            if (File.Exists(candidate)) return Path.GetFullPath(candidate);
        }
        return null;
    }

    private void StartBeacon()
    {
        if (!_beaconWanted || _beacon != null) return;
        string script = BeaconScript();
        if (script == null)
        {
            _supervisor.Log("beacon: script not found");
            return;
        }
        try
        {
            var start = new ProcessStartInfo("powershell.exe")
            {
                Arguments = "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
            };
            _beacon = Process.Start(start);
            ChildJob.Add(_beacon);
            _beaconLog = Path.Combine(_supervisor.LogDirectory, "beacon.log");
            StartBeaconReader(_beacon.StandardOutput);
            StartBeaconReader(_beacon.StandardError);
            _supervisor.Log("beacon: started pid " + _beacon.Id);
        }
        catch (Exception error)
        {
            _beacon = null;
            _supervisor.Log("beacon: start failed: " + error.Message);
        }
    }

    private void StartBeaconReader(System.IO.StreamReader reader)
    {
        var thread = new Thread(delegate()
        {
            try
            {
                string line;
                while ((line = reader.ReadLine()) != null)
                {
                    try { File.AppendAllText(_beaconLog, DateTime.Now.ToString("HH:mm:ss.fff") + " " + line + Environment.NewLine); }
                    catch { }
                }
            }
            catch
            {
            }
        });
        thread.IsBackground = true;
        thread.Start();
    }

    private void StopBeacon()
    {
        try
        {
            if (_beacon != null && !_beacon.HasExited) _beacon.Kill();
        }
        catch
        {
        }
        _beacon = null;
    }

    private void CheckBeacon()
    {
        if (!_beaconWanted) return;
        if (_beacon != null && !_beacon.HasExited) return;
        if (_beacon != null)
        {
            // A dead child must not block the restart: StartBeacon refuses while
            // the field is set, so clear it and record why we are here.
            int code = -1;
            try { code = _beacon.ExitCode; } catch { }
            _supervisor.Log("beacon: exited (code " + code + "), restarting");
            _beacon = null;
        }
        StartBeacon();
    }

    private void ToggleBeacon()
    {
        _beaconWanted = !_beaconWanted;
        if (_beaconItem != null) _beaconItem.Checked = _beaconWanted;
        _prefs.SetBeacon(_beaconWanted);
        try
        {
            if (_beaconWanted)
            {
                StartBeacon();
                _icon.ShowBalloonTip(4000, _icon.Text, "蓝牙 beacon 已开启，iPhone 更容易发现本机。", ToolTipIcon.Info);
            }
            else
            {
                StopBeacon();
                _icon.ShowBalloonTip(4000, _icon.Text, "蓝牙 beacon 已关闭（手机可能看不到本机，直到你打开隔空投送面板）。", ToolTipIcon.Warning);
            }
        }
        catch
        {
        }
    }

    /* Q60: the receiver reads {"do": "set", "name": ...} from stdin, so the new
     * name is announced without restarting the radio (~40 s). The choice is
     * written to data\settings.ini and reused on the next start. */
    private void RenameDevice()
    {
        string current = _prefs.DeviceName;
        using (var dialog = new DeviceNameDialog(current))
        {
            if (dialog.ShowDialog() != DialogResult.OK) return;
            string name = dialog.Value;
            if (name == current) return;
            string error;
            bool live = _supervisor.RenameReceiver(name, out error);
            _prefs.SetDeviceName(name);
            _nameItem.Text = "设备名称：" + name + "…";
            _icon.Text = name;
            AppIdentity.Ensure(name);
            _supervisor.Log("device name " + current + " -> " + name +
                            (live ? " (applied live)" : " (receiver restart needed: " + error + ")"));
            _icon.ShowBalloonTip(4000, _icon.Text,
                live ? "接收端名称已改为：" + name : "名称已保存，接收端下次启动时生效。",
                ToolTipIcon.Info);
        }
    }

    /// <summary>
    /// A receiver that is not running receives nothing, so the first successful
    /// start registers it for login once and tells the user (the tray menu can
    /// turn it back off). The marker file keeps a deliberate "off" from being
    /// re-offered on every start.
    /// </summary>
    private void OfferAutostartOnce()
    {
        string marker = Path.Combine(_supervisor.Root, "data", "autostart-offered");
        if (File.Exists(marker) || AutostartEnabled()) return;
        try
        {
            SetAutostart(true);
            _autostart.Checked = true;
            File.WriteAllText(marker, DateTime.Now.ToString("o"), Encoding.UTF8);
            _icon.ShowBalloonTip(6000, _icon.Text,
                "已设为开机自启：以后登录就在托盘里等着收文件。想关掉：右键图标 → 开机自启。",
                ToolTipIcon.Info);
        }
        catch
        {
        }
    }

    /// <summary>
    /// First run on a fresh machine: the Wi-Fi interface is still on the Realtek
    /// driver, so libusb cannot open it and the chain would fail with
    /// "no device". Ask once, install with a single UAC prompt, then restart.
    /// </summary>
    private void StartFirstRunCheck()
    {
        var timer = new System.Windows.Forms.Timer { Interval = 4000 };
        timer.Tick += delegate
        {
            timer.Stop();
            timer.Dispose();
            string service = RadioBinding.Service();
            if (service == null)
            {
                _icon.ShowBalloonTip(6000, _icon.Text,
                    "没找到 RTL8821CU 网卡：请直插机箱后面板的 USB 口。", ToolTipIcon.Warning);
                return;
            }
            if (RadioBinding.Bound(service)) return;

            DialogResult answer = MessageBox.Show(
                "首次使用需要一次性安装驱动（把网卡的 Wi-Fi 接口绑定到 WinUSB）。\n\n" +
                "点「是」后 Windows 会弹出一次管理员确认，安装完成后会自动继续。\n" +
                "如果不想现在做，可以从托盘菜单选「安装驱动（首次使用）」。",
                "DropBridge 首次使用", MessageBoxButtons.YesNo, MessageBoxIcon.Information);
            if (answer == DialogResult.Yes) InstallDriver();
        };
        timer.Start();
    }

    private void InstallDriver()
    {
        Cursor.Current = Cursors.WaitCursor;
        try
        {
            int code = RadioBinding.Install(_supervisor.Root, false, DriverLog);
            if (code == 0)
            {
                _icon.ShowBalloonTip(5000, _icon.Text, "驱动已就绪，正在重新启动后端…", ToolTipIcon.Info);
                RequestStart(false);
            }
            else if (code == 2)
            {
                MessageBox.Show("需要管理员权限才能安装驱动。\n可以右键 DropBridge.exe → 以管理员身份运行。",
                    "DropBridge", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            else if (code == 3)
            {
                MessageBox.Show("没找到 RTL8821CU 网卡。\n请直插机箱后面板的 USB 口后重试。",
                    "DropBridge", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            else
            {
                MessageBox.Show("驱动安装没有完成，详见：\n" + DriverLog,
                    "DropBridge", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        finally
        {
            Cursor.Current = Cursors.Default;
        }
    }

    private int NextBackoffSeconds()
    {
        if ((DateTime.UtcNow - _windowStart).TotalMinutes >= 10)
        {
            _failures = 0;
            _windowStart = DateTime.UtcNow;
        }
        _failures++;
        _supervisor.Log("restart attempt " + _failures);
        if (_failures > 5) return -1;
        if (_failures <= 1) return 1;
        if (_failures <= 2) return 5;
        return 30;
    }

    // One start at a time: menu clicks and watchdog restarts must not race.
    private void RequestStart(bool automatic)
    {
        if (Interlocked.CompareExchange(ref _starting, 1, 0) != 0) return;
        _status.Text = automatic ? "状态：正在重启后端…" : "状态：启动中…";
        var worker = new Thread(delegate()
        {
            try
            {
                int delay = automatic ? NextBackoffSeconds() : 0;
                if (delay < 0)
                {
                    Ui(delegate
                    {
                        _gaveUp = true;
                        _status.Text = "状态：多次失败，已停止";
                        _icon.ShowBalloonTip(10000, _icon.Text,
                            "后端反复失败，已停止自动重启。请查看日志后从托盘菜单重试。",
                            ToolTipIcon.Error);
                    });
                    return;
                }
                if (delay > 0) Thread.Sleep(delay * 1000);
                lock (_startGate)
                {
                    _supervisor.StopAll();
                    string error;
                    bool started = _supervisor.StartAll(180, out error);
                    Ui(delegate
                    {
                        if (started)
                        {
                            _reportedExit = false;
                            _gaveUp = false;
                            _failures = 0;
                            _windowStart = DateTime.UtcNow;
                            _status.Text = "状态：运行中（" + _supervisor.BandText + "）";
                            if (automatic)
                            {
                                _icon.ShowBalloonTip(4000, _icon.Text,
                                    "后端已自动恢复。", ToolTipIcon.Info);
                            }
                            else
                            {
                                _icon.ShowBalloonTip(3000, _icon.Text,
                                    "接收已就绪，可以在手机上发送了。", ToolTipIcon.Info);
                            }
                        }
                        else
                        {
                            _supervisor.LastError = error;
                            _supervisor.Log("start failed: " + error);
                            _status.Text = "状态：启动失败，稍后自动重试";
                            _icon.ShowBalloonTip(8000, "DropBridge 启动失败", error,
                                ToolTipIcon.Error);
                            // The radio sometimes fails several firmware boots
                            // in a row; retry with the usual backoff instead of
                            // waiting for the user to click 重新启动.
                            if (!_gaveUp) RequestStart(true);
                        }
                    });
                }
            }
            finally
            {
                Interlocked.Exchange(ref _starting, 0);
            }
        });
        worker.IsBackground = true;
        worker.Start();
    }

    /* Measured 2026-10-28: a healthy transfer sits near 700-850 frames/s,
     * the slow ones near 190-280 with the same loss. Two consecutive low
     * samples warn once; the tray's 重置链路 restarts the radio, which is the
     * only thing that recovered a degraded chip so far. */
    private void WatchLinkRate()
    {
        // Losing the AWDL mastership explains most slow transfers better than a
        // low frame rate does: with the phone as master only ~2/16 slots carry
        // our data.
        if (_supervisor.LostMastership && !_masterWarned)
        {
            _masterWarned = true;
            _supervisor.Log("AWDL mastership lost: sync master " + _supervisor.SyncMaster +
                            ", self " + _supervisor.SelfAddress);
            _icon.ShowBalloonTip(8000, _icon.Text,
                "同步主控被对方的手机接管，速度会明显下降。可从托盘菜单选“重置链路”后重试。",
                ToolTipIcon.Warning);
        }
        else if (!_supervisor.LostMastership)
        {
            _masterWarned = false;
        }
        if (_supervisor.FpsSamples == 0 || _supervisor.Fps <= 0) return;
        if (_supervisor.Fps < 300)
        {
            _slowSamples++;
        }
        else if (_supervisor.Fps >= 500)
        {
            _slowSamples = 0;
            _slowWarned = false;
        }
        if (_slowSamples >= 2 && !_slowWarned)
        {
            _slowWarned = true;
            _supervisor.Log("low link rate: " + (int)_supervisor.Fps + " frames/s");
            _icon.ShowBalloonTip(8000, _icon.Text,
                "链路速率偏低（约 " + (int)_supervisor.Fps +
                " 帧/s）。可从托盘菜单选“重置链路”后重试。", ToolTipIcon.Warning);
        }
    }

    // Q57: a radio stuck inside a USB call still looks alive to the process
    // table. It prints a stats line every ~5 s even when idle, so silence is the
    // signal. One silence restarts the chain; a second one within three minutes
    // tells the user to replug instead of looping.
    private int _silentHits;
    private DateTime _lastSilenceUtc = DateTime.MinValue;

    private void CheckSilence()
    {
        if (!_supervisor.Running || _gaveUp) return;
        double silent = _supervisor.SilentSeconds("radio");
        if (silent < 0 || silent < 30) return;
        if ((DateTime.UtcNow - _lastSilenceUtc).TotalMinutes > 3) _silentHits = 0;
        _lastSilenceUtc = DateTime.UtcNow;
        _silentHits++;
        _supervisor.Log("radio silent for " + (int)silent + " s (hit " + _silentHits + "); restarting the chain");
        _supervisor.Running = false;
        if (_silentHits >= 2)
        {
            _icon.ShowBalloonTip(10000, _icon.Text,
                "网卡没有响应，已经自动重启过两次。请把网卡拔下来、重新插上。", ToolTipIcon.Error);
        }
        else
        {
            _icon.ShowBalloonTip(5000, _icon.Text, "网卡卡住了，正在自动重启…", ToolTipIcon.Warning);
        }
        RequestStart(true);
    }

    private void CheckChildren()
    {
        if (!_supervisor.Running || _gaveUp) return;
        if (_supervisor.AllAlive)
        {
            _status.Text = "状态：运行中（" + _supervisor.BandText + "）";
            WatchLinkRate();
            return;
        }
        // A dead backend leaves a half-working system: restore it, loudly.
        List<string> dead = _supervisor.DeadNames();
        _supervisor.Running = false;
        _supervisor.Log("backends exited: " + string.Join(",", dead.ToArray()));
        _status.Text = "状态：后端退出，正在恢复…";
        if (!_reportedExit)
        {
            _reportedExit = true;
            _icon.ShowBalloonTip(5000, _icon.Text,
                "有后端进程退出，正在自动恢复。", ToolTipIcon.Warning);
        }
        if (dead.Count == 1 && dead[0] == "receiver" && RestartReceiverOnly())
        {
            return;
        }
        RequestStart(true);
    }

    // Only the Python side can be replaced without re-initialising the radio.
    private bool RestartReceiverOnly()
    {
        if (Interlocked.CompareExchange(ref _starting, 1, 0) != 0) return false;
        ThreadPool.QueueUserWorkItem(delegate(object state)
        {
            try
            {
                _supervisor.Log("restarting receiver only");
                string error;
                bool restarted = _supervisor.RestartReceiver(out error);
                if (!restarted) _supervisor.Log("receiver restart failed: " + error);
                Ui(delegate
                {
                    if (restarted)
                    {
                        _reportedExit = false;
                        _status.Text = "状态：运行中（" + _supervisor.BandText + "）";
                        _icon.ShowBalloonTip(3000, _icon.Text,
                            "接收端已恢复，无需重新初始化网卡。", ToolTipIcon.Info);
                    }
                    else
                    {
                        RequestStart(true);
                    }
                });
            }
            finally
            {
                Interlocked.Exchange(ref _starting, 0);
            }
        });
        return true;
    }

    private void CopyDiagnostics()
    {
        var text = new StringBuilder();
        text.AppendLine("DropBridge diagnostics");
        text.AppendLine("time: " + DateTime.Now.ToString("o"));
        text.AppendLine("exe: " + Application.ExecutablePath);
        text.AppendLine("root: " + _supervisor.Root);
        text.AppendLine("running: " + _supervisor.Running + "  allAlive: " + _supervisor.AllAlive);
        text.AppendLine("os: " + Environment.OSVersion.VersionString);
        text.AppendLine("logs: " + _supervisor.LogDirectory);
        try
        {
            foreach (string file in Directory.GetFiles(_supervisor.LogDirectory, "*.log"))
            {
                text.AppendLine("--- " + Path.GetFileName(file));
                string[] lines = File.ReadAllLines(file);
                int start = Math.Max(0, lines.Length - 40);
                for (int index = start; index < lines.Length; index++)
                {
                    text.AppendLine(lines[index]);
                }
            }
        }
        catch (IOException error)
        {
            text.AppendLine("cannot read logs: " + error.Message);
        }
        try
        {
            Clipboard.SetText(text.ToString());
            _icon.ShowBalloonTip(3000, _icon.Text,
                "诊断信息已复制到剪贴板。", ToolTipIcon.Info);
        }
        catch (Exception)
        {
            _icon.ShowBalloonTip(3000, _icon.Text,
                "复制失败，可在日志目录自行打包。", ToolTipIcon.Warning);
        }
    }

    /* The exe carries an icon (assets\dropbridge.ico at build time); the tray
     * reuses it so the notification area matches the file the user launched.
     * A checkout without the icon still runs with the stock application icon. */
    private static Icon AppIcon()
    {
        try
        {
            Icon icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            if (icon != null) return icon;
        }
        catch (Exception)
        {
        }
        return SystemIcons.Application;
    }

    private static void OpenFolder(string path)
    {
        try
        {
            Directory.CreateDirectory(path);
            Process.Start("explorer.exe", "\"" + path + "\"");
        }
        catch (Exception)
        {
        }
    }

    /* Autostart points at the stub in the folder root, not at this build's
     * supervisor: the build directory changes on every upgrade while the stub
     * path stays valid and re-launches the newest payload. In the folder
     * distribution the supervisor already sits at that root, so it registers
     * itself. */
    private static string AutostartValue()
    {
        try
        {
            string here = Path.GetDirectoryName(Application.ExecutablePath);
            if (Directory.Exists(Path.Combine(here, "backend")))
            {
                return "\"" + Application.ExecutablePath + "\"";
            }
            string build = Path.GetDirectoryName(here);
            string appRoot = Path.GetDirectoryName(build);
            string folder = Path.GetDirectoryName(appRoot);
            foreach (string name in new[] { "DropBridge.exe", "AirBridge32.exe" })
            {
                string stub = Path.Combine(folder, name);
                if (File.Exists(stub)) return "\"" + stub + "\"";
            }
        }
        catch (ArgumentException)
        {
        }
        return "\"" + Application.ExecutablePath + "\"";
    }

    private static bool AutostartEnabled()
    {
        using (RegistryKey key = Registry.CurrentUser.OpenSubKey(
                   @"Software\Microsoft\Windows\CurrentVersion\Run", false))
        {
            if (key == null) return false;
            return key.GetValue("DropBridge") as string == AutostartValue();
        }
    }

    private static void SetAutostart(bool enabled)
    {
        using (RegistryKey key = Registry.CurrentUser.CreateSubKey(
                   @"Software\Microsoft\Windows\CurrentVersion\Run"))
        {
            if (key == null) return;
            if (enabled) key.SetValue("DropBridge", AutostartValue());
            else key.DeleteValue("DropBridge", false);
        }
    }

    private void ToggleAutostart()
    {
        bool enable = !_autostart.Checked;
        SetAutostart(enable);
        _autostart.Checked = enable;
    }

    private void Exit()
    {
        _watchdog.Stop();
        ReceivedPopup.CloseAll();
        SystemEvents.UserPreferenceChanged -= OnUserPreferenceChanged;
        if (_watcher != null) _watcher.EnableRaisingEvents = false;
        _icon.Visible = false;
        StopBeacon();
        var worker = new Thread(delegate()
        {
            _supervisor.StopAll();
            Ui(delegate
            {
                _icon.Dispose();
                if (_trayIcon != null && !ReferenceEquals(_trayIcon, SystemIcons.Application))
                {
                    _trayIcon.Dispose();
                    _trayIcon = null;
                }
                ExitThread();
            });
        });
        worker.IsBackground = false;
        worker.Start();
    }
}

/* Q60: the tray used to remember only the beacon switch (a data\beacon-off
 * marker) and autostart (the Run key); everything else was forgotten on the
 * next start. The user-facing choices now live in <package>\data\settings.ini,
 * so a folder copy and a single-file copy each keep their own file and nothing
 * is written outside the package. The file is plain text on purpose: it can be
 * edited by hand and the next start picks it up. */
internal sealed class Settings
{
    public const string DefaultDeviceName = "DropBridge";
    private const string FileName = "settings.ini";
    private const int MaxNameChars = 63; // same limit as the receiver's --name
    private readonly string _path;

    public string DeviceName { get; private set; }
    public bool Beacon { get; private set; }

    private Settings(string path, string deviceName, bool beacon)
    {
        _path = path;
        DeviceName = deviceName;
        Beacon = beacon;
    }

    public static Settings Load(string dataDirectory)
    {
        string path = Path.Combine(dataDirectory, FileName);
        string deviceName = DefaultDeviceName;
        bool beacon = true;
        try
        {
            if (File.Exists(path))
            {
                foreach (string raw in File.ReadAllLines(path, Encoding.UTF8))
                {
                    string line = raw.Trim();
                    if (line.Length == 0 || line[0] == '#' || line[0] == ';') continue;
                    int at = line.IndexOf('=');
                    if (at <= 0) continue;
                    string key = line.Substring(0, at).Trim();
                    string value = line.Substring(at + 1).Trim();
                    if (key.Equals("deviceName", StringComparison.OrdinalIgnoreCase))
                    {
                        string clean = CleanName(value);
                        if (clean != null) deviceName = clean;
                    }
                    else if (key.Equals("beacon", StringComparison.OrdinalIgnoreCase))
                    {
                        bool on;
                        if (bool.TryParse(value, out on)) beacon = on;
                    }
                }
            }
            else if (File.Exists(Path.Combine(dataDirectory, "beacon-off")))
            {
                // Migration: builds before Q60 recorded "off" as a marker file.
                beacon = false;
            }
        }
        catch (Exception)
        {
        }
        return new Settings(path, deviceName, beacon);
    }

    public void SetDeviceName(string name)
    {
        DeviceName = name;
        Save();
    }

    public void SetBeacon(bool enabled)
    {
        Beacon = enabled;
        Save();
    }

    private void Save()
    {
        try
        {
            string directory = Path.GetDirectoryName(_path);
            if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
            var text = new StringBuilder();
            text.AppendLine("# DropBridge 设置（关闭托盘后可直接改，下一个启动生效）");
            text.AppendLine("# deviceName: iPhone 隔空投送里显示的名字，1-63 个字符");
            text.AppendLine("# beacon: 是否周期性发蓝牙 beacon 帮 iPhone 发现本机");
            text.AppendLine("deviceName=" + DeviceName);
            text.AppendLine("beacon=" + (Beacon ? "true" : "false"));
            File.WriteAllText(_path, text.ToString(), new UTF8Encoding(false));
        }
        catch (Exception)
        {
        }
    }

    /* Mirrors the receiver's valid_name(): 1-63 characters, no separators, no
     * control characters, no bidi overrides that could disguise the name. */
    public static string CleanName(string value)
    {
        if (value == null) return null;
        string name = value.Trim().TrimEnd('.');
        if (name.Length == 0 || name.Length > MaxNameChars) return null;
        foreach (char c in name)
        {
            if (c < 0x20 || (c >= 0x7f && c <= 0x9f)) return null;
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|') return null;
            if (c >= '\u200b' && c <= '\u200f') return null;
            if (c >= '\u202a' && c <= '\u202e') return null;
            if (c >= '\u2066' && c <= '\u2069') return null;
            if (c == '\ufeff') return null;
        }
        return name;
    }
}

/* Q59d: a portable exe has no application identity, so Windows renders the
 * notification header with its generic tile and the raw file name
 * ("DropBridge.exe"). An AppUserModelID plus the per-user registration under
 * Software\Classes\AppUserModelId gives toasts the app name and the icon that
 * sits in the exe, without installing anything. */
internal static class AppIdentity
{
    private const string AppId = "DropBridge.Tray";

    public static void Ensure(string displayName)
    {
        try
        {
            SetCurrentProcessExplicitAppUserModelID(AppId);
        }
        catch (Exception)
        {
        }

        if (string.IsNullOrEmpty(displayName)) displayName = Settings.DefaultDeviceName;
        try
        {
            // Windows resolves the toast icon from IconUri; it has to be an image
            // file, so prefer the .ico shipped next to the exe and fall back to
            // the exe itself only if the file is missing.
            string icon = Path.Combine(Path.GetDirectoryName(Application.ExecutablePath) ?? string.Empty,
                "DropBridge.ico");
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(
                       @"Software\Classes\AppUserModelId\" + AppId))
            {
                if (key == null) return;
                key.SetValue("DisplayName", displayName, RegistryValueKind.String);
                key.SetValue("IconUri", File.Exists(icon) ? icon : Application.ExecutablePath,
                    RegistryValueKind.String);
            }
        }
        catch (Exception)
        {
        }
    }

    [System.Runtime.InteropServices.DllImport("shell32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private static extern int SetCurrentProcessExplicitAppUserModelID(string appId);

    /* The 28 px tray icon, for the received-file popup. */
    public static Image IconForPopup()
    {
        try
        {
            using (Icon icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath))
            {
                if (icon != null) return icon.ToBitmap();
            }
        }
        catch (Exception)
        {
        }
        return SystemIcons.Application.ToBitmap();
    }
}

internal static class Program
{
    private static string ResolveRoot(string argument)
    {
        if (!string.IsNullOrEmpty(argument))
        {
            return Path.GetFullPath(argument);
        }
        string executableDirectory = Path.GetDirectoryName(Application.ExecutablePath);
        // Folder distribution: DropBridge.exe sits at the package root next to
        // backend\. The self-extracting layout keeps it inside backend\ and is
        // still handled by the parent-directory fallback.
        if (Directory.Exists(Path.Combine(executableDirectory, "backend")))
        {
            return executableDirectory;
        }
        string packageRoot = Path.GetDirectoryName(executableDirectory);
        return packageRoot;
    }

    [STAThread]
    private static int Main(string[] args)
    {
        string rootArgument = null;
        string output = null;
        bool check = false;
        int smokeSeconds = -1;
        int autoExitSeconds = -1;
        int autostart = 0;
        int channel = 149;
        int width = 20;
        string channelSequence = "social";
        bool tcpMerge = true;
        bool allowSend = false;
        int streams = 1;
        string dataArgument = null;
        for (int index = 0; index < args.Length; index++)
        {
            switch (args[index])
            {
                case "--root":
                    if (index + 1 < args.Length) rootArgument = args[++index];
                    break;
                case "--data":
                    if (index + 1 < args.Length) dataArgument = args[++index];
                    break;
                case "--check":
                    check = true;
                    break;
                case "--out":
                    if (index + 1 < args.Length) output = args[++index];
                    break;
                case "--smoke":
                    if (index + 1 < args.Length) int.TryParse(args[++index], out smokeSeconds);
                    break;
                case "--auto-exit":
                    if (index + 1 < args.Length) int.TryParse(args[++index], out autoExitSeconds);
                    break;
                case "--channel":
                    if (index + 1 < args.Length) int.TryParse(args[++index], out channel);
                    break;
                case "--width":
                    if (index + 1 < args.Length) int.TryParse(args[++index], out width);
                    break;
                case "--chanseq":
                    if (index + 1 >= args.Length) return 2;
                    channelSequence = args[++index];
                    if (channelSequence != "social" && channelSequence != "fixed") return 2;
                    break;
                case "--no-tcp-merge":
                    tcpMerge = false;
                    break;
                case "--allow-send":
                    allowSend = true;
                    break;
                case "--streams":
                    // Diagnostic A/B of what awdld advertises; 1 is the honest
                    // value for this 1T1R radio and stays the default.
                    if (index + 1 < args.Length && int.TryParse(args[++index], out streams) &&
                        (streams == 1 || streams == 2))
                    {
                        break;
                    }
                    return 2;
                case "--install-driver":
                    {
                        bool quiet = false;
                        string installRoot = null;
                        for (int extra = index + 1; extra < args.Length; extra++)
                        {
                            if (args[extra] == "--quiet") quiet = true;
                            else if (args[extra] == "--root" && extra + 1 < args.Length) installRoot = args[++extra];
                        }
                        string resolved = ResolveRoot(installRoot);
                        string log = Path.Combine(resolved, "data", "logs", "driver-install.txt");
                        return RadioBinding.Install(resolved, quiet, log);
                    }
                case "--autostart-on":
                    autostart = 1;
                    break;
                case "--autostart-off":
                    autostart = -1;
                    break;
            }
        }

        Supervisor.AllowSend = allowSend;
        Supervisor.AwdldStreams = streams;

        if (autostart != 0)
        {
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(
                       @"Software\Microsoft\Windows\CurrentVersion\Run"))
            {
                if (autostart > 0) key.SetValue("DropBridge", "\"" + Application.ExecutablePath + "\"");
                else key.DeleteValue("DropBridge", false);
            }
            return 0;
        }

        string root = ResolveRoot(rootArgument);
        string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");

        // A mistyped switch used to fall through to the tray and start a chain,
        // which looks like "the argument was ignored". Fail fast instead: the
        // first argument must be one of the switches below.
        string[] knownSwitches =
        {
            "--root", "--data", "--check", "--out", "--smoke", "--auto-exit",
            "--channel", "--width", "--chanseq", "--no-tcp-merge", "--allow-send",
            "--autostart-on", "--autostart-off", "--install-driver",
        };
        if (args.Length > 0 && Array.IndexOf(knownSwitches, args[0]) < 0)
        {
            try
            {
                Directory.CreateDirectory(Path.Combine(root, "data", "logs"));
                File.AppendAllText(Path.Combine(root, "data", "logs", "supervisor.log"),
                    DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + " unknown arguments: " +
                    string.Join(" ", args) + Environment.NewLine, Encoding.UTF8);
            }
            catch
            {
            }
            return 2;
        }

        if (check)
        {
            var supervisor = new Supervisor(root, stamp, false, channel, width, dataArgument, channelSequence, tcpMerge);
            string plan = supervisor.Plan();
            if (string.IsNullOrEmpty(output))
            {
                output = Path.Combine(root, "data", "check.txt");
            }
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output)));
            File.WriteAllText(output, plan, Encoding.UTF8);
            return supervisor.Validate().Count == 0 ? 0 : 1;
        }

        if (smokeSeconds > 0)
        {
            var supervisor = new Supervisor(root, stamp, true, channel, width, dataArgument, channelSequence, tcpMerge);
            string error;
            bool started = supervisor.StartAll(180, out error);
            var report = new StringBuilder();
            report.AppendLine("started=" + started);
            report.AppendLine("error=" + (error ?? ""));
            report.AppendLine("allAlive=" + supervisor.AllAlive);
            report.AppendLine("logs=" + supervisor.LogDirectory);
            Thread.Sleep(smokeSeconds * 1000);
            report.AppendLine("allAliveAfterWait=" + supervisor.AllAlive);
            supervisor.StopAll();
            File.WriteAllText(Path.Combine(supervisor.LogDirectory, "smoke.txt"),
                report.ToString(), Encoding.UTF8);
            return started ? 0 : 1;
        }

        bool created;
        using (var mutex = new Mutex(true, @"Local\DropBridge-tray", out created))
        {
            if (!created) return 0;
            Application.EnableVisualStyles();
            var supervisor = new Supervisor(root, stamp, true, channel, width, dataArgument, channelSequence, tcpMerge);
            // Q59d: only the tray instance owns the notification identity. Doing it
            // in Main would let "--check" from a scratch copy point IconUri at that
            // copy; the identity has to name the package the user actually runs.
            // Q60: the display name follows the configured device name.
            AppIdentity.Ensure(supervisor.Prefs.DeviceName);
            using (var context = new TrayApp(supervisor, autoExitSeconds))
            {
                Application.Run(context);
            }
        }
        return 0;
    }
}

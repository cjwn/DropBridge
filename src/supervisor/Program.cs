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
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

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
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (KeyValuePair<string, string> pair in _environment)
        {
            start.EnvironmentVariables[pair.Key] = pair.Value;
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
        _log.Write(Name, line);
        if (_lineSeen != null) _lineSeen(line);
        if (_needle != null && line.IndexOf(_needle, StringComparison.Ordinal) >= 0)
        {
            Ready.Set();
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
                _process.Kill();
                _process.WaitForExit(waitMilliseconds);
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
    private readonly int _channel;
    private readonly int _width;
    public string RadioArguments { get { return _channel + " " + _width; } }
    public string AwdldArguments { get { return "-c " + _channel + " -s 2 -j 400 -v"; } }
    // 20 MHz is the default: measured 202 KB/s against 40 MHz's 190 KB/s after a
    // cold start, and 40 MHz has a degraded mode that needs a USB replug to clear.
    public string BandText { get { return "ch" + _channel + " / " + _width + " MHz"; } }
    private readonly string _root;
    private readonly string _backend;
    private readonly string _data;
    private readonly LogSink _log;
    private readonly List<Child> _children = new List<Child>();
    public volatile bool Running;
    public string LastError = "";

    public Supervisor(string root, string sessionStamp, bool prepareDirectories,
                      int channel = 149, int width = 20, string dataDirectory = null)
    {
        _root = root;
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
                Fps = frames / 5.0;
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
        text.AppendLine("netd: " + NetdPath() + "  [AWDL_NETD_MDNS_PROXY=1]");
        text.AppendLine("awdld: " + AwdldPath() + " " + AwdldArguments);
        text.AppendLine("receiver: " + PythonPath() + " -u " + ReceiverPath());
        foreach (string problem in Validate()) text.AppendLine("MISSING: " + problem);
        return text.ToString();
    }

    private Child NewReceiver()
    {
        return new Child("receiver", PythonPath(),
            "-u \"" + ReceiverPath() + "\" --name AirBridge32 --listen 127.0.0.1" +
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
                return "port " + port + " is already in use (another AirBridge32 instance?)";
            }
        }
        return null;
    }

    public bool StartAll(int readyTimeoutSeconds, out string error)
    {
        error = null;
        string busy = PortProblem();
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
        _log.Write("supervisor", "start chain root=" + _root + " args=" + RadioArguments);

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
            },
            _root, "listening on 127.0.0.1:49876", _log, OnChildLine);
        var bridge = new Child("bridge", BridgePath(), "",
            null, _root, "waiting for awdld", _log);
        var netd = new Child("netd", NetdPath(), "",
            new Dictionary<string, string>
            {
                { "AWDL_NETD_MDNS_PROXY", "1" },
                { "AWDL_NETD_ADDR_FILE", AddressFile() },
            }, _root, "waiting for awdld", _log);
        // As the AWDL sync master the phone keeps its data in our 149 MHz slots
        // and throughput roughly doubles; as a slave it only uses ~2/16 slots.
        // The counter decides the election before the metric (OWL never raises
        // its own counter, an iPhone's grows every 3.14 s of mastership), so a
        // fixed high value keeps us master for the whole session.
        var awdld = new Child("awdld", AwdldPath(), AwdldArguments,
            new Dictionary<string, string>
            {
                { "AWDL_ELECTION_METRIC", "200" },
                { "AWDL_ELECTION_COUNTER", "100000" },
            },
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

internal sealed class TrayApp : ApplicationContext
{
    private readonly Supervisor _supervisor;
    private readonly NotifyIcon _icon;
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

    public TrayApp(Supervisor supervisor, int autoExitSeconds)
    {
        _supervisor = supervisor;
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
        menu.Items.Add(new ToolStripMenuItem("重新启动后端", null, delegate { StartBackends(); }));
        menu.Items.Add(new ToolStripMenuItem("重置链路", null, delegate
        {
            _slowSamples = 0;
            _slowWarned = false;
            RequestStart(false);
        }));
        menu.Items.Add(new ToolStripMenuItem("退出", null, delegate { Exit(); }));

        _icon = new NotifyIcon
        {
            Icon = SystemIcons.Application,
            Text = "AirBridge32",
            ContextMenuStrip = menu,
            Visible = true,
        };
        _icon.DoubleClick += delegate { OpenFolder(_supervisor.ReceivedDirectory); };
        _icon.ShowBalloonTip(3000, "AirBridge32", "正在启动后端…", ToolTipIcon.Info);

        Application.Idle += CaptureContext;
        StartWatcher();
        StartBackends();

        _watchdog = new System.Windows.Forms.Timer { Interval = 5000 };
        _watchdog.Tick += delegate { CheckChildren(); };
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
                string note = args.Name;
                try
                {
                    var info = new FileInfo(args.FullPath);
                    if (info.Exists) note += " (" + (info.Length / 1024) + " KB)";
                }
                catch (IOException)
                {
                }
                string text = "收到文件：" + note;
                Ui(delegate { _icon.ShowBalloonTip(4000, "AirBridge32", text, ToolTipIcon.Info); });
            };
        }
        catch (Exception error)
        {
            _supervisor.LastError = "cannot watch received folder: " + error.Message;
        }
    }

    private void StartBackends()
    {
        RequestStart(false);
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
                        _icon.ShowBalloonTip(10000, "AirBridge32",
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
                                _icon.ShowBalloonTip(4000, "AirBridge32",
                                    "后端已自动恢复。", ToolTipIcon.Info);
                            }
                            else
                            {
                                _icon.ShowBalloonTip(3000, "AirBridge32",
                                    "接收已就绪，可以在手机上发送了。", ToolTipIcon.Info);
                            }
                        }
                        else
                        {
                            _supervisor.LastError = error;
                            _supervisor.Log("start failed: " + error);
                            _status.Text = "状态：启动失败，稍后自动重试";
                            _icon.ShowBalloonTip(8000, "AirBridge32 启动失败", error,
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
            _icon.ShowBalloonTip(8000, "AirBridge32",
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
            _icon.ShowBalloonTip(8000, "AirBridge32",
                "链路速率偏低（约 " + (int)_supervisor.Fps +
                " 帧/s）。可从托盘菜单选“重置链路”后重试。", ToolTipIcon.Warning);
        }
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
            _icon.ShowBalloonTip(5000, "AirBridge32",
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
                        _icon.ShowBalloonTip(3000, "AirBridge32",
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
        text.AppendLine("AirBridge32 diagnostics");
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
            _icon.ShowBalloonTip(3000, "AirBridge32",
                "诊断信息已复制到剪贴板。", ToolTipIcon.Info);
        }
        catch (Exception)
        {
            _icon.ShowBalloonTip(3000, "AirBridge32",
                "复制失败，可在日志目录自行打包。", ToolTipIcon.Warning);
        }
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
     * path stays valid and re-launches the newest payload. */
    private static string AutostartValue()
    {
        try
        {
            string backend = Path.GetDirectoryName(Application.ExecutablePath);
            string build = Path.GetDirectoryName(backend);
            string appRoot = Path.GetDirectoryName(build);
            string folder = Path.GetDirectoryName(appRoot);
            string stub = Path.Combine(folder, "AirBridge32.exe");
            if (File.Exists(stub)) return "\"" + stub + "\"";
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
            return key.GetValue("AirBridge32") as string == AutostartValue();
        }
    }

    private static void SetAutostart(bool enabled)
    {
        using (RegistryKey key = Registry.CurrentUser.CreateSubKey(
                   @"Software\Microsoft\Windows\CurrentVersion\Run"))
        {
            if (key == null) return;
            if (enabled) key.SetValue("AirBridge32", AutostartValue());
            else key.DeleteValue("AirBridge32", false);
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
        if (_watcher != null) _watcher.EnableRaisingEvents = false;
        _icon.Visible = false;
        var worker = new Thread(delegate()
        {
            _supervisor.StopAll();
            Ui(delegate
            {
                _icon.Dispose();
                ExitThread();
            });
        });
        worker.IsBackground = false;
        worker.Start();
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
                case "--autostart-on":
                    autostart = 1;
                    break;
                case "--autostart-off":
                    autostart = -1;
                    break;
            }
        }

        if (autostart != 0)
        {
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(
                       @"Software\Microsoft\Windows\CurrentVersion\Run"))
            {
                if (autostart > 0) key.SetValue("AirBridge32", "\"" + Application.ExecutablePath + "\"");
                else key.DeleteValue("AirBridge32", false);
            }
            return 0;
        }

        string root = ResolveRoot(rootArgument);
        string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");

        if (check)
        {
            var supervisor = new Supervisor(root, stamp, false, channel, width, dataArgument);
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
            var supervisor = new Supervisor(root, stamp, true, channel, width, dataArgument);
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
        using (var mutex = new Mutex(true, @"Local\AirBridge32-tray", out created))
        {
            if (!created) return 0;
            Application.EnableVisualStyles();
            var supervisor = new Supervisor(root, stamp, true, channel, width, dataArgument);
            using (var context = new TrayApp(supervisor, autoExitSeconds))
            {
                Application.Run(context);
            }
        }
        return 0;
    }
}

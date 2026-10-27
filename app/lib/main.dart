import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';

void main() {
  runApp(const AirBridgeApp());
}

class AirBridgeApp extends StatelessWidget {
  const AirBridgeApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'AirBridge32',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF0078D4)),
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}

enum BackendState { stopped, starting, running, error }

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final List<String> _log = [];
  final List<String> _pendingLog = [];
  final List<Process> _processes = [];
  Timer? _flushTimer;
  BackendState _state = BackendState.stopped;
  String _status = '未启动';
  late final Directory _dataDir;
  late final String _addressFile;
  late final String _outputDir;
  late final File _logFile;

  @override
  void initState() {
    super.initState();
    final exeDir = File(Platform.resolvedExecutable).parent;
    final packaged = Directory('${exeDir.path}/backend');
    final cwd = Directory.current;
    final repoRoot = cwd.path.endsWith('app') ? cwd.parent : cwd;
    _dataDir = packaged.existsSync()
        ? Directory('${exeDir.path}/data')
        : Directory('${repoRoot.path}/build/app-data');
    _dataDir.createSync(recursive: true);
    _addressFile = '${_dataDir.path}/awdl_netd.addr';
    _outputDir = '${_dataDir.path}/received';
    _logFile = File('${_dataDir.path}/airbridge32.log');
    Directory(_outputDir).createSync(recursive: true);
    WidgetsBinding.instance.addPostFrameCallback((_) => _start());
  }

  String get _exeDir => File(Platform.resolvedExecutable).parent.path;

  bool get _packaged => Directory('$_exeDir/backend').existsSync();

  String get _repoRoot {
    final cwd = Directory.current;
    return cwd.path.endsWith('app') ? cwd.parent.path : cwd.path;
  }

  String _backendExe(String name) {
    if (_packaged) return '$_exeDir/backend/$name';
    switch (name) {
      case 'airbridge_radio.exe':
      case 'radio_pipe_bridge.exe':
        return '$_repoRoot/build/radio_usb/Release/$name';
      case 'awdld.exe':
        return '$_repoRoot/build/awdl/$name';
      case 'awdl_netd.exe':
        return '$_repoRoot/build/awdl_netd/Release/$name';
      default:
        return '$_exeDir/backend/$name';
    }
  }

  String get _receiverExe =>
      _packaged ? '$_exeDir/backend/airdrop_receiver.exe' : 'python';

  List<String> get _receiverArgs => _packaged
      ? [
          '--name', 'AirBridge32',
          '--listen', '127.0.0.1',
          '--mdns-address-file', _addressFile,
          '--outdir', _outputDir,
          '--keys', '${_dataDir.path}/keys',
          '--trace',
        ]
      : [
          '-u',
          '$_repoRoot/upstream/winawdl/airdrop/receiver.py',
          '--name', 'AirBridge32',
          '--listen', '127.0.0.1',
          '--mdns-address-file', _addressFile,
          '--outdir', _outputDir,
          '--keys', '${_dataDir.path}/keys',
          '--trace',
        ];

  void _append(String line) {
    _pendingLog.add(line);
    _flushTimer ??= Timer(const Duration(milliseconds: 200), _flushLogs);
  }

  void _flushLogs() {
    _flushTimer = null;
    if (_pendingLog.isEmpty) return;
    final batch = _pendingLog.join('\n');
    _pendingLog.clear();
    try {
      // Async, non-blocking: the radio's stdout pipe must never wait on UI/log IO.
      _logFile.writeAsString('$batch\n', mode: FileMode.append);
    } catch (_) {}
    if (!mounted) return;
    setState(() {
      _log.addAll(batch.split('\n'));
      if (_log.length > 300) _log.removeRange(0, _log.length - 300);
    });
  }

  Future<Process> _startProcess({
    required String label,
    required String exe,
    required List<String> args,
    String? workingDirectory,
    Map<String, String>? environment,
    String? readyNeedle,
    Duration timeout = const Duration(seconds: 180),
  }) async {
    _append('[$label] $exe ${args.join(' ')}');
    final process = await Process.start(
      exe,
      args,
      workingDirectory: workingDirectory,
      environment: environment,
      runInShell: false,
    );
    _processes.add(process);
    final ready = Completer<void>();
    var readySeen = false;

    void onLine(String line) {
      _append('[$label] $line');
      if (!readySeen && readyNeedle != null && line.contains(readyNeedle)) {
        readySeen = true;
        ready.complete();
      }
    }

    process.stdout
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen(onLine);
    process.stderr
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen(onLine);
    process.exitCode.then((code) {
      _append('[$label] exited ($code)');
      if (!ready.isCompleted) {
        ready.completeError(StateError('$label exited ($code)'));
      }
    });

    if (readyNeedle == null) {
      await Future<void>.delayed(const Duration(milliseconds: 300));
    } else {
      try {
        await ready.future.timeout(timeout);
      } on TimeoutException {
        throw StateError('$label not ready after ${timeout.inSeconds}s');
      }
    }
    return process;
  }

  Future<void> _start() async {
    if (_state == BackendState.starting || _state == BackendState.running) return;
    setState(() {
      _state = BackendState.starting;
      _status = '启动 radio…';
    });
    try {
      await _startProcess(
        label: 'radio',
        exe: _backendExe('airbridge_radio.exe'),
        args: ['149', '40'],
        environment: {
          ...Platform.environment,
          'AIRBRIDGE_LOG_LEVEL': 'warn',
        },
        readyNeedle: 'listening on 127.0.0.1:49876',
      );
      setState(() => _status = '启动桥…');
      await _startProcess(
        label: 'bridge',
        exe: _backendExe('radio_pipe_bridge.exe'),
        args: const [],
        readyNeedle: 'waiting for awdld',
        timeout: const Duration(seconds: 10),
      );
      setState(() => _status = '启动用户态网络栈…');
      await _startProcess(
        label: 'netd',
        exe: _backendExe('awdl_netd.exe'),
        args: const [],
        environment: {
          ...Platform.environment,
          'AWDL_NETD_MDNS_PROXY': '1',
          'AWDL_NETD_ADDR_FILE': _addressFile,
        },
        readyNeedle: 'waiting for awdld',
        timeout: const Duration(seconds: 10),
      );
      setState(() => _status = '启动 AWDL…');
      await _startProcess(
        label: 'awdld',
        exe: _backendExe('awdld.exe'),
        args: const ['-c', '149', '-s', '2', '-j', '400', '-v'],
        workingDirectory:
            _packaged ? '$_exeDir/backend' : '$_repoRoot/build/awdl',
        readyNeedle: 'host interface',
        timeout: const Duration(seconds: 30),
      );
      for (var i = 0; i < 60; i++) {
        if (File(_addressFile).existsSync()) break;
        await Future<void>.delayed(const Duration(milliseconds: 250));
      }
      setState(() => _status = '启动接收服务…');
      await _startProcess(
        label: 'receiver',
        exe: _receiverExe,
        args: _receiverArgs,
        workingDirectory: _packaged ? '$_exeDir/backend' : _repoRoot,
        environment: {...Platform.environment, 'PYTHONUNBUFFERED': '1'},
        readyNeedle: 'receiving as',
        timeout: const Duration(seconds: 30),
      );
      setState(() {
        _state = BackendState.running;
        _status = '运行中 · 5 GHz / ch149 / 40 MHz';
      });
    } catch (e) {
      _append('[supervisor] $e');
      setState(() {
        _state = BackendState.error;
        _status = '启动失败：$e';
      });
      await _stopProcesses();
    }
  }

  Future<void> _stopProcesses() async {
    for (final p in _processes.reversed) {
      try {
        p.kill();
      } catch (_) {}
    }
    _processes.clear();
  }

  Future<void> _stop() async {
    setState(() {
      _state = BackendState.stopped;
      _status = '已停止';
    });
    await _stopProcesses();
  }

  @override
  void dispose() {
    _flushTimer?.cancel();
    _flushLogs();
    for (final p in _processes.reversed) {
      try {
        p.kill();
      } catch (_) {}
    }
    super.dispose();
  }

  Color get _stateColor {
    switch (_state) {
      case BackendState.running:
        return Colors.green;
      case BackendState.starting:
        return Colors.orange;
      case BackendState.error:
        return Colors.red;
      case BackendState.stopped:
        return Colors.grey;
    }
  }

  @override
  Widget build(BuildContext context) {
    final canStart =
        _state == BackendState.stopped || _state == BackendState.error;
    return Scaffold(
      appBar: AppBar(
        title: const Text('AirBridge32'),
        actions: [
          IconButton(
            tooltip: '打开接收文件夹',
            onPressed: () {
              Process.start('explorer', [_outputDir]);
            },
            icon: const Icon(Icons.folder_open),
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Row(
                  children: [
                    Icon(Icons.circle, color: _stateColor, size: 14),
                    const SizedBox(width: 10),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(_status,
                              style: Theme.of(context).textTheme.titleMedium),
                          const SizedBox(height: 4),
                          const Text(
                            'iPhone 隔空投送 → AirBridge32 · 无需关闭 v2ray TUN',
                            style: TextStyle(fontSize: 12),
                          ),
                        ],
                      ),
                    ),
                    if (_state == BackendState.starting)
                      const SizedBox(
                        width: 20,
                        height: 20,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                FilledButton.icon(
                  onPressed: canStart ? _start : null,
                  icon: const Icon(Icons.play_arrow),
                  label: const Text('启动'),
                ),
                const SizedBox(width: 8),
                OutlinedButton.icon(
                  onPressed: _state == BackendState.stopped ? null : _stop,
                  icon: const Icon(Icons.stop),
                  label: const Text('停止'),
                ),
                const Spacer(),
                const Text('5 GHz · ch149 · 40 MHz',
                    style: TextStyle(fontSize: 12)),
              ],
            ),
            const SizedBox(height: 12),
            Expanded(
              child: Card(
                child: ListView.builder(
                  padding: const EdgeInsets.all(8),
                  itemCount: _log.length,
                  itemBuilder: (context, i) => SelectableText(
                    _log[i],
                    style:
                        const TextStyle(fontFamily: 'Consolas', fontSize: 11),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

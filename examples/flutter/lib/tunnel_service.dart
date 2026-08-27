import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

/// 隧道连接参数，对应 tunnel_client.exe 命令行。
class TunnelClientOptions {
  const TunnelClientOptions({
    required this.endpoint,
    this.port = 44333,
    this.useQuic = true,
    this.useWintun = false,
    this.insecure = false,
    this.globalRoute = false,
    this.relay = true,
    this.certHash,
  });

  final String endpoint;
  final int port;
  final bool useQuic;
  final bool useWintun;
  final bool insecure;
  final bool globalRoute;
  final bool relay;
  final String? certHash;

  List<String> toArguments() {
    final args = <String>[];
    if (useQuic) args.add('--quic');
    if (useWintun) args.add('--wintun');
    if (insecure) args.add('--insecure');
    if (globalRoute) args.add('--global-route');
    if (relay) args.add('--relay');
    args.add('--endpoint:$endpoint');
    args.add('--port:$port');
    if (certHash != null && certHash!.isNotEmpty) {
      args.add('--cert_hash:$certHash');
    }
    return args;
  }
}

/// 方式 A：直接启动 tunnel_client.exe（适合开发模式，无需 Wintun）。
class TunnelProcessService {
  TunnelProcessService({required this.workingDirectory});

  final String workingDirectory;
  Process? _process;
  final _outputController = StreamController<String>.broadcast();

  Stream<String> get output => _outputController.stream;
  bool get isRunning => _process != null;

  Future<void> start(TunnelClientOptions options) async {
    if (_process != null) {
      throw StateError('隧道进程已在运行');
    }

    final exePath = '$workingDirectory${Platform.pathSeparator}tunnel_client.exe';
    if (!File(exePath).existsSync()) {
      throw FileSystemException('找不到 tunnel_client.exe', exePath);
    }

    _process = await Process.start(
      exePath,
      options.toArguments(),
      workingDirectory: workingDirectory,
      runInShell: false,
    );

    _process!.stdout
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen(_outputController.add);
    _process!.stderr
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen(_outputController.add);
  }

  Future<void> stop() async {
    final process = _process;
    _process = null;
    if (process == null) return;
    process.kill(ProcessSignal.sigterm);
    await process.exitCode.timeout(
      const Duration(seconds: 5),
      onTimeout: () {
        process.kill(ProcessSignal.sigkill);
        return -1;
      },
    );
  }

  Future<void> dispose() async {
    await stop();
    await _outputController.close();
  }
}

/// 方式 B：Windows 上通过 MethodChannel 调用 C# / C++ 宿主（适合 Wintun + 管理员权限）。
class TunnelPlatformService {
  static const _channel = MethodChannel('sectunnel/control');

  Future<Map<String, dynamic>> getStatus() async {
    if (!Platform.isWindows) {
      return {'state': 'unsupported', 'connected': false};
    }
    final result = await _channel.invokeMethod<Map<Object?, Object?>>('getStatus');
    return Map<String, dynamic>.from(result ?? const {});
  }

  Future<void> connect(TunnelClientOptions options) async {
    if (!Platform.isWindows) {
      throw UnsupportedError('Platform channel 集成目前仅支持 Windows');
    }
    await _channel.invokeMethod<void>('connect', {
      'endpoint': options.endpoint,
      'port': options.port,
      'useQuic': options.useQuic,
      'useWintun': options.useWintun,
      'globalRoute': options.globalRoute,
      'insecure': options.insecure,
      'relay': options.relay,
      'certHash': options.certHash,
    });
  }

  Future<void> disconnect() async {
    await _channel.invokeMethod<void>('disconnect');
  }
}

/// Flutter UI 示例用的 ViewModel。
class TunnelViewModel extends ChangeNotifier {
  TunnelViewModel({
    required this.processService,
    required this.platformService,
  });

  final TunnelProcessService processService;
  final TunnelPlatformService platformService;

  String statusText = 'disconnected';
  String logText = '';
  bool busy = false;

  Future<void> connectDevMode({
    required String endpoint,
    int port = 44333,
  }) async {
    busy = true;
    notifyListeners();

    try {
      await processService.start(
        TunnelClientOptions(
          endpoint: endpoint,
          port: port,
          useQuic: true,
          insecure: endpoint == '127.0.0.1' || endpoint == 'localhost',
          relay: true,
        ),
      );
      processService.output.listen((line) {
        logText = '$logText\n$line';
        if (line.contains('连接已建立') || line.contains('established')) {
          statusText = 'connected';
        }
        notifyListeners();
      });
      statusText = 'connecting';
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  Future<void> connectProduction({
    required String endpoint,
    int port = 44333,
  }) async {
    busy = true;
    notifyListeners();

    try {
      await platformService.connect(
        TunnelClientOptions(
          endpoint: endpoint,
          port: port,
          useQuic: true,
          useWintun: true,
          globalRoute: true,
          relay: true,
        ),
      );
      final status = await platformService.getStatus();
      statusText = '${status['state']}';
    } catch (error) {
      statusText = 'error: $error';
    } finally {
      busy = false;
      notifyListeners();
    }
  }

  Future<void> disconnect() async {
    await processService.stop();
    await platformService.disconnect();
    statusText = 'disconnected';
    notifyListeners();
  }
}

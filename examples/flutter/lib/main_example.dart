import 'package:flutter/material.dart';

import 'tunnel_service.dart';

void main() {
  runApp(const SecureTunnelDemoApp());
}

class SecureTunnelDemoApp extends StatelessWidget {
  const SecureTunnelDemoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SecureTunnel Demo',
      home: TunnelDemoPage(
        viewModel: TunnelViewModel(
          processService: TunnelProcessService(
            workingDirectory: r'C:\Users\Admin\Desktop\SecureTunnelCpp\build-agent',
          ),
          platformService: TunnelPlatformService(),
        ),
      ),
    );
  }
}

class TunnelDemoPage extends StatefulWidget {
  const TunnelDemoPage({super.key, required this.viewModel});

  final TunnelViewModel viewModel;

  @override
  State<TunnelDemoPage> createState() => _TunnelDemoPageState();
}

class _TunnelDemoPageState extends State<TunnelDemoPage> {
  final _endpointController = TextEditingController(text: '127.0.0.1');
  final _portController = TextEditingController(text: '44333');

  @override
  void initState() {
    super.initState();
    widget.viewModel.addListener(_onChanged);
  }

  void _onChanged() => setState(() {});

  @override
  void dispose() {
    widget.viewModel.removeListener(_onChanged);
    _endpointController.dispose();
    _portController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final vm = widget.viewModel;
    return Scaffold(
      appBar: AppBar(title: const Text('SecureTunnel Flutter 示例')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            TextField(
              controller: _endpointController,
              decoration: const InputDecoration(labelText: '服务器 Endpoint'),
            ),
            TextField(
              controller: _portController,
              decoration: const InputDecoration(labelText: '端口'),
              keyboardType: TextInputType.number,
            ),
            const SizedBox(height: 12),
            Text('状态: ${vm.statusText}'),
            const SizedBox(height: 12),
            ElevatedButton(
              onPressed: vm.busy
                  ? null
                  : () => vm.connectDevMode(
                        endpoint: _endpointController.text.trim(),
                        port: int.tryParse(_portController.text.trim()) ?? 44333,
                      ),
              child: const Text('开发模式连接（Process）'),
            ),
            ElevatedButton(
              onPressed: vm.busy
                  ? null
                  : () => vm.connectProduction(
                        endpoint: _endpointController.text.trim(),
                        port: int.tryParse(_portController.text.trim()) ?? 44333,
                      ),
              child: const Text('生产模式连接（Platform Channel）'),
            ),
            ElevatedButton(
              onPressed: vm.busy ? null : vm.disconnect,
              child: const Text('断开'),
            ),
            const SizedBox(height: 12),
            Expanded(
              child: SingleChildScrollView(
                child: Text(vm.logText.isEmpty ? '等待日志...' : vm.logText),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

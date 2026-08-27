using SecureTunnel.Example;

// 示例 1：开发模式（无需管理员，本机 QUIC 联调）
await RunDevModeExample();

// 示例 2：生产模式（Wintun + 全局路由，需要管理员）
// await RunProductionExample();

static async Task RunDevModeExample()
{
    var buildDir = ResolveBuildDirectory();
    await using var runner = new TunnelProcessRunner();

    runner.OutputReceived += (_, line) => Console.WriteLine($"[stdout] {line}");
    runner.ErrorReceived += (_, line) => Console.WriteLine($"[stderr] {line}");

    var options = new TunnelClientOptions
    {
        Endpoint = "127.0.0.1",
        Port = 44333,
        UseQuic = true,
        Insecure = true,
        Relay = true,
    };

    Console.WriteLine($"启动命令: tunnel_client.exe {options.BuildArguments()}");
    await runner.StartAsync(options, buildDir, runAsAdmin: false);

    Console.WriteLine("运行 10 秒后停止...");
    await Task.Delay(TimeSpan.FromSeconds(10));
    await runner.StopAsync();
}

static async Task RunProductionExample()
{
    var buildDir = ResolveBuildDirectory();
    await using var runner = new TunnelProcessRunner();

    var options = new TunnelClientOptions
    {
        Endpoint = "203.0.113.10",
        Port = 44333,
        UseQuic = true,
        UseWintun = true,
        GlobalRoute = true,
        Relay = true,
        Insecure = false,
    };

    // Wintun + 改路由必须管理员；会弹出 UAC。
    await runner.StartAsync(options, buildDir, runAsAdmin: true);

    Console.WriteLine("隧道已启动。按 Enter 断开...");
    Console.ReadLine();
    await runner.StopAsync();
}

static string ResolveBuildDirectory()
{
    var candidates = new[]
    {
        Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "build-agent")),
        Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "build")),
        @"C:\Users\Admin\Desktop\SecureTunnelCpp\build-agent",
    };

    foreach (var path in candidates)
    {
        if (File.Exists(Path.Combine(path, "tunnel_client.exe")))
        {
            return path;
        }
    }

    throw new DirectoryNotFoundException(
        "找不到 build-agent 或 build 目录，请先编译 SecureTunnelCpp。");
}

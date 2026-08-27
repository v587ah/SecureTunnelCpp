using System.Diagnostics;
using System.Text;

namespace SecureTunnel.Example;

/// <summary>
/// 通过启动 tunnel_client.exe 控制隧道（当前项目推荐的第一种集成方式）。
/// </summary>
public sealed class TunnelProcessRunner : IAsyncDisposable
{
    private Process? _process;

    public bool IsRunning => _process is { HasExited: false };

    public event EventHandler<string>? OutputReceived;
    public event EventHandler<string>? ErrorReceived;

    /// <summary>
    /// 启动隧道客户端。
    /// </summary>
    /// <param name="options">连接与模式参数。</param>
    /// <param name="workingDirectory">
    /// 工作目录，应包含 tunnel_client.exe、msquic.dll、wintun.dll 及 certs/dev。
    /// </param>
    /// <param name="runAsAdmin">
    /// Wintun / 全局路由需要管理员权限。若为 true 且当前非管理员，将弹出 UAC。
    /// </param>
    public async Task StartAsync(
        TunnelClientOptions options,
        string workingDirectory,
        bool runAsAdmin = false,
        CancellationToken cancellationToken = default)
    {
        if (IsRunning)
        {
            throw new InvalidOperationException("隧道进程已在运行。");
        }

        var exePath = Path.Combine(workingDirectory, "tunnel_client.exe");
        if (!File.Exists(exePath))
        {
            throw new FileNotFoundException("找不到 tunnel_client.exe", exePath);
        }

        var args = options.BuildArguments();
        var startInfo = new ProcessStartInfo
        {
            FileName = exePath,
            Arguments = args,
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = false,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };

        if (runAsAdmin)
        {
            startInfo.Verb = "runas";
            startInfo.UseShellExecute = true;
            startInfo.RedirectStandardOutput = false;
            startInfo.RedirectStandardError = false;
        }

        _process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
        _process.Exited += (_, _) => OutputReceived?.Invoke(this, "进程已退出。");

        if (!runAsAdmin)
        {
            _process.OutputDataReceived += (_, e) =>
            {
                if (!string.IsNullOrEmpty(e.Data))
                {
                    OutputReceived?.Invoke(this, e.Data);
                }
            };
            _process.ErrorDataReceived += (_, e) =>
            {
                if (!string.IsNullOrEmpty(e.Data))
                {
                    ErrorReceived?.Invoke(this, e.Data);
                }
            };
        }

        if (!_process.Start())
        {
            throw new InvalidOperationException("启动 tunnel_client.exe 失败。");
        }

        if (!runAsAdmin)
        {
            _process.BeginOutputReadLine();
            _process.BeginErrorReadLine();
        }

        await Task.Delay(500, cancellationToken);
    }

    public async Task StopAsync(TimeSpan gracefulTimeout = default)
    {
        if (_process is null || _process.HasExited)
        {
            return;
        }

        if (gracefulTimeout == default)
        {
            gracefulTimeout = TimeSpan.FromSeconds(5);
        }

        try
        {
            if (!_process.CloseMainWindow())
            {
                _process.Kill(entireProcessTree: true);
            }

            using var cts = new CancellationTokenSource(gracefulTimeout);
            await _process.WaitForExitAsync(cts.Token);
        }
        catch (OperationCanceledException)
        {
            if (!_process.HasExited)
            {
                _process.Kill(entireProcessTree: true);
            }
        }
        finally
        {
            _process.Dispose();
            _process = null;
        }
    }

    public ValueTask DisposeAsync() => new(StopAsync());
}

public sealed class TunnelClientOptions
{
    public required string Endpoint { get; init; }
    public ushort Port { get; init; } = 44333;
    public bool UseQuic { get; init; } = true;
    public bool UseWintun { get; init; }
    public bool Insecure { get; init; }
    public bool GlobalRoute { get; init; }
    public bool Relay { get; init; } = true;
    public string? CertHash { get; init; }

    public string BuildArguments()
    {
        var parts = new List<string>();
        if (UseQuic)
        {
            parts.Add("--quic");
        }
        if (UseWintun)
        {
            parts.Add("--wintun");
        }
        if (Insecure)
        {
            parts.Add("--insecure");
        }
        if (GlobalRoute)
        {
            parts.Add("--global-route");
        }
        if (Relay)
        {
            parts.Add("--relay");
        }

        parts.Add($"--endpoint:{Endpoint}");
        parts.Add($"--port:{Port}");

        if (!string.IsNullOrWhiteSpace(CertHash))
        {
            parts.Add($"--cert_hash:{CertHash}");
        }

        return string.Join(' ', parts);
    }
}

using System.IO.Pipes;
using System.Text;
using System.Text.Json;

namespace SecureTunnel.Example;

/// <summary>
/// 与隧道核心服务通信的 Named Pipe 客户端（协议为规划中的 v1 草案）。
/// 当前仓库尚未内置 Pipe 服务端，此代码供 Windows Service 集成时参考。
/// </summary>
public sealed class TunnelNamedPipeClient : IAsyncDisposable
{
    private const string DefaultPipeName = @"\\.\pipe\SecureTunnel.Control";
    private NamedPipeClientStream? _pipe;

    public async Task ConnectAsync(
        string pipeName = DefaultPipeName,
        TimeSpan? timeout = null,
        CancellationToken cancellationToken = default)
    {
        timeout ??= TimeSpan.FromSeconds(5);
        _pipe = new NamedPipeClientStream(
            ".",
            pipeName.Replace(@"\\.\pipe\", string.Empty),
            PipeDirection.InOut,
            PipeOptions.Asynchronous);

        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(timeout.Value);
        await _pipe.ConnectAsync(cts.Token);
    }

    public async Task<TunnelStatusResponse> GetStatusAsync(CancellationToken cancellationToken = default)
    {
        var response = await SendAsync(
            new TunnelControlRequest { Command = "status" },
            cancellationToken);
        return JsonSerializer.Deserialize<TunnelStatusResponse>(response)
               ?? throw new InvalidOperationException("无效 status 响应。");
    }

    public Task ConnectTunnelAsync(TunnelClientOptions options, CancellationToken cancellationToken = default)
    {
        return SendAsync(
            new TunnelControlRequest
            {
                Command = "connect",
                Endpoint = options.Endpoint,
                Port = options.Port,
                UseQuic = options.UseQuic,
                UseWintun = options.UseWintun,
                GlobalRoute = options.GlobalRoute,
                Insecure = options.Insecure,
            },
            cancellationToken);
    }

    public Task DisconnectTunnelAsync(CancellationToken cancellationToken = default)
    {
        return SendAsync(new TunnelControlRequest { Command = "disconnect" }, cancellationToken);
    }

    private async Task<string> SendAsync(
        TunnelControlRequest request,
        CancellationToken cancellationToken)
    {
        if (_pipe is null || !_pipe.IsConnected)
        {
            throw new InvalidOperationException("Pipe 未连接。");
        }

        var payload = JsonSerializer.Serialize(request);
        var payloadBytes = Encoding.UTF8.GetBytes(payload);
        var lengthPrefix = BitConverter.GetBytes(payloadBytes.Length);

        await _pipe.WriteAsync(lengthPrefix, cancellationToken);
        await _pipe.WriteAsync(payloadBytes, cancellationToken);
        await _pipe.FlushAsync(cancellationToken);

        var responseLengthBytes = new byte[4];
        await _pipe.ReadExactlyAsync(responseLengthBytes, cancellationToken);
        var responseLength = BitConverter.ToInt32(responseLengthBytes, 0);
        if (responseLength <= 0 || responseLength > 1024 * 1024)
        {
            throw new InvalidOperationException("响应长度非法。");
        }

        var responseBytes = new byte[responseLength];
        await _pipe.ReadExactlyAsync(responseBytes, cancellationToken);
        return Encoding.UTF8.GetString(responseBytes);
    }

    public ValueTask DisposeAsync()
    {
        _pipe?.Dispose();
        _pipe = null;
        return ValueTask.CompletedTask;
    }
}

public sealed class TunnelControlRequest
{
    public required string Command { get; init; }
    public string? Endpoint { get; init; }
    public ushort Port { get; init; }
    public bool UseQuic { get; init; }
    public bool UseWintun { get; init; }
    public bool GlobalRoute { get; init; }
    public bool Insecure { get; init; }
}

public sealed class TunnelStatusResponse
{
    public string State { get; init; } = "unknown";
    public bool Connected { get; init; }
    public string? Endpoint { get; init; }
    public string? Error { get; init; }
}

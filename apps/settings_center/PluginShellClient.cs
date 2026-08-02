using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace OwO_Settings;

internal sealed record PluginVersionSnapshot(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("version")] string Version,
    [property: JsonPropertyName("active")] bool Active)
{
    public string Title => $"{Name}  {Version}";
    public string Detail => $"{Id} · {(Active ? "已启用" : "未启用")}";
    public string ActionLabel => Active ? "停用" : "启用此版本";
}

internal sealed record PluginRecoverySnapshot(
    [property: JsonPropertyName("index")] int Index,
    [property: JsonPropertyName("kind")] string Kind,
    [property: JsonPropertyName("action")] string Action,
    [property: JsonPropertyName("path")] string Path,
    [property: JsonPropertyName("plugin_id")] string PluginId,
    [property: JsonPropertyName("version")] string Version,
    [property: JsonPropertyName("diagnostic")] string Diagnostic)
{
    public string Title => Kind switch {
        "retained_staging" => "安装事务残留",
        "orphaned_version" => "孤立插件版本",
        "orphaned_record" => "孤立版本记录",
        "orphaned_authorization" => "无效授权记录",
        "inactive_version" => "可切换的未激活版本",
        "invalid_active_record" => "无效活动记录",
        _ => "需手工检查的不安全条目",
    };
    public string Detail => string.IsNullOrEmpty(Version)
        ? $"{Diagnostic}\n{Path}" : $"{PluginId} {Version} · {Diagnostic}\n{Path}";
    public string ActionLabel => Action switch {
        "activate" => "切换到此版本",
        "cleanup" => "清理",
        _ => "仅手工检查",
    };
    public bool CanApply => Action != "manual";
}

internal sealed record PluginSnapshot(
    [property: JsonPropertyName("schema_version")] int SchemaVersion,
    [property: JsonPropertyName("plugins")] List<PluginVersionSnapshot> Plugins,
    [property: JsonPropertyName("recovery")] List<PluginRecoverySnapshot> Recovery);

internal sealed class PluginShellClient
{
    private readonly string _shellPath = Environment.GetEnvironmentVariable("OWO_PLUGIN_SHELL_PATH")
        ?? Path.Combine(AppContext.BaseDirectory, "owo_plugin_shell.exe");
    private readonly string _storePath = Environment.GetEnvironmentVariable("OWO_PLUGIN_STORE_PATH")
        ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                        "OwO", "InputMethod", "plugins");

    internal string StorePath => _storePath;

    internal async Task<PluginSnapshot> LoadAsync(CancellationToken cancellationToken = default)
    {
        var json = await RunAsync([_storePath, "list"], cancellationToken);
        var value = JsonSerializer.Deserialize<PluginSnapshot>(json)
            ?? throw new InvalidOperationException("插件后端返回了空结果。");
        if (value.SchemaVersion != 1) throw new InvalidOperationException("插件管理协议版本不兼容。");
        return value;
    }

    internal Task ActivateAsync(string id, string version,
                                CancellationToken cancellationToken = default) =>
        RunAsync([_storePath, "activate", id, version], cancellationToken);

    internal Task DeactivateAsync(string id, string version,
                                  CancellationToken cancellationToken = default) =>
        RunAsync([_storePath, "deactivate", id, version], cancellationToken);

    internal Task CleanupAsync(PluginRecoverySnapshot item,
                               CancellationToken cancellationToken = default) =>
        RunAsync([_storePath, "cleanup", item.Index.ToString(), item.Kind, item.Path,
                  item.PluginId, item.Version], cancellationToken);

    private async Task<string> RunAsync(IEnumerable<string> arguments,
                                        CancellationToken cancellationToken)
    {
        if (!File.Exists(_shellPath))
            throw new FileNotFoundException("找不到 OwO 插件管理后端。", _shellPath);
        var start = new ProcessStartInfo(_shellPath) {
            UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true,
        };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        using var process = Process.Start(start) ?? throw new InvalidOperationException("无法启动插件管理后端。");
        var output = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var error = process.StandardError.ReadToEndAsync(cancellationToken);
        await process.WaitForExitAsync(cancellationToken);
        if (process.ExitCode != 0) {
            var message = (await error).Trim();
            throw new InvalidOperationException(message.Length > 0 ? message :
                $"插件管理后端退出码：{process.ExitCode}");
        }
        return await output;
    }
}

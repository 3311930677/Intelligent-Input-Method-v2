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
    public bool CanUninstall => !Active;
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
        "retained_uninstall" => "卸载事务残留",
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

internal sealed record PluginInstallSnapshot(
    [property: JsonPropertyName("schema_version")] int SchemaVersion,
    [property: JsonPropertyName("ok")] bool Ok,
    [property: JsonPropertyName("stage")] string Stage,
    [property: JsonPropertyName("version_published")] bool VersionPublished,
    [property: JsonPropertyName("activated")] bool Activated,
    [property: JsonPropertyName("plugin_id")] string PluginId,
    [property: JsonPropertyName("name")] string Name,
    [property: JsonPropertyName("version")] string Version,
    [property: JsonPropertyName("installed_path")] string InstalledPath,
    [property: JsonPropertyName("retained_staging_path")] string RetainedStagingPath,
    [property: JsonPropertyName("previous_version")] string PreviousVersion,
    [property: JsonPropertyName("inventory_sha256")] string InventorySha256,
    [property: JsonPropertyName("publisher_display_name")] string PublisherDisplayName,
    [property: JsonPropertyName("publisher_certificate_sha256")] string PublisherCertificateSha256,
    [property: JsonPropertyName("diagnostic")] string Diagnostic);

internal sealed class PluginShellClient
{
    private sealed record ShellRunResult(int ExitCode, string Output, string Error);

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

    internal async Task<PluginInstallSnapshot> InstallAsync(
        string packagePath, CancellationToken cancellationToken = default)
    {
        var run = await RunProcessAsync([_storePath, "install", packagePath], cancellationToken);
        PluginInstallSnapshot? value;
        try {
            value = JsonSerializer.Deserialize<PluginInstallSnapshot>(run.Output);
        } catch (JsonException) {
            value = null;
        }
        if (value is null) {
            var fallback = run.Error.Trim();
            throw new InvalidOperationException(fallback.Length > 0 ? fallback
                : "插件安装后端返回了无效结果。");
        }
        if (value.SchemaVersion != 1)
            throw new InvalidOperationException("插件安装协议版本不兼容。");
        if (run.ExitCode != 0 || !value.Ok) {
            var message = string.IsNullOrWhiteSpace(value.Diagnostic)
                ? run.Error.Trim() : value.Diagnostic;
            var retained = string.IsNullOrWhiteSpace(value.RetainedStagingPath)
                ? "" : $"；保留的暂存目录：{value.RetainedStagingPath}";
            throw new InvalidOperationException(
                $"阶段 {value.Stage}：{(message.Length > 0 ? message : "安装被拒绝")}{retained}");
        }
        if (value.Stage != "completed" || !value.VersionPublished ||
            !value.Activated || string.IsNullOrWhiteSpace(value.PluginId) ||
            string.IsNullOrWhiteSpace(value.Version) ||
            string.IsNullOrWhiteSpace(value.InstalledPath) ||
            string.IsNullOrWhiteSpace(value.InventorySha256) ||
            string.IsNullOrWhiteSpace(value.PublisherCertificateSha256)) {
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(value.Diagnostic)
                ? "插件安装后端返回了不完整的成功结果。" : value.Diagnostic);
        }
        return value;
    }

    internal Task ActivateAsync(string id, string version,
                                CancellationToken cancellationToken = default) =>
        RunAsync([_storePath, "activate", id, version], cancellationToken);

    internal Task DeactivateAsync(string id, string version,
                                  CancellationToken cancellationToken = default) =>
        RunAsync([_storePath, "deactivate", id, version], cancellationToken);

    internal Task UninstallAsync(string id, string version,
                                 CancellationToken cancellationToken = default) =>
        RunAsync([_storePath, "uninstall", id, version], cancellationToken);

    internal Task CleanupAsync(PluginRecoverySnapshot item,
                               CancellationToken cancellationToken = default) =>
        RunAsync([_storePath, "cleanup", item.Index.ToString(), item.Kind, item.Path,
                  item.PluginId, item.Version], cancellationToken);

    private async Task<string> RunAsync(IEnumerable<string> arguments,
                                        CancellationToken cancellationToken)
    {
        var result = await RunProcessAsync(arguments, cancellationToken);
        if (result.ExitCode != 0) {
            var message = result.Error.Trim();
            throw new InvalidOperationException(message.Length > 0 ? message :
                $"插件管理后端退出码：{result.ExitCode}");
        }
        return result.Output;
    }

    private async Task<ShellRunResult> RunProcessAsync(
        IEnumerable<string> arguments, CancellationToken cancellationToken)
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
        return new ShellRunResult(process.ExitCode, await output, await error);
    }
}

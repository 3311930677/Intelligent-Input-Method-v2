using Microsoft.UI.Xaml.Controls;

namespace OwO_Settings;

public sealed partial class MainPage : Page
{
    private readonly ConfigShellClient _client = new();
    private readonly PluginShellClient _pluginClient = new();

    public MainPage()
    {
        InitializeComponent();
        ConfigPath.Text = _client.ConfigPath;
        PluginPath.Text = _pluginClient.StorePath;
        Loaded += async (_, _) => {
            await LoadConfigAsync();
            await LoadPluginsAsync();
        };
    }

    private async Task LoadConfigAsync()
    {
        SetBusy(true);
        try {
            var value = await _client.LoadAsync();
            CandidatePageSize.Value = value.CandidatePageSize;
            UserLearning.IsOn = value.UserLearningEnabled;
            ModelRanking.IsOn = value.ModelRankingEnabled;
            ModelTimeout.Value = value.ModelTimeoutMs;
            ShowStatus("配置已加载", InfoBarSeverity.Success);
        } catch (Exception error) {
            ShowStatus(error.Message, InfoBarSeverity.Error);
        } finally {
            SetBusy(false);
        }
    }

    private async void SaveButton_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        SetBusy(true);
        try {
            var value = new SettingsSnapshot((uint)CandidatePageSize.Value,
                UserLearning.IsOn, ModelRanking.IsOn, (uint)ModelTimeout.Value);
            await _client.SaveAsync(value);
            ShowStatus("配置已保存，Core Service 将自动应用。", InfoBarSeverity.Success);
        } catch (Exception error) {
            ShowStatus(error.Message, InfoBarSeverity.Error);
        } finally {
            SetBusy(false);
        }
    }

    private async void ReloadButton_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e) =>
        await LoadConfigAsync();

    private async Task LoadPluginsAsync()
    {
        SetBusy(true);
        try {
            var value = await _pluginClient.LoadAsync();
            PluginVersions.ItemsSource = value.Plugins;
            RecoveryItems.ItemsSource = value.Recovery;
            NoPlugins.Visibility = value.Plugins.Count == 0
                ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;
            NoRecovery.Visibility = value.Recovery.Count == 0
                ? Microsoft.UI.Xaml.Visibility.Visible : Microsoft.UI.Xaml.Visibility.Collapsed;
            ShowStatus($"插件状态已刷新：{value.Plugins.Count} 个版本，{value.Recovery.Count} 个恢复项。",
                       InfoBarSeverity.Success);
        } catch (Exception error) {
            ShowStatus(error.Message, InfoBarSeverity.Error);
        } finally {
            SetBusy(false);
        }
    }

    private async void PluginReloadButton_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e) =>
        await LoadPluginsAsync();

    private async Task<bool> ConfirmAsync(string title, string message, string action)
    {
        var dialog = new ContentDialog {
            XamlRoot = XamlRoot,
            Title = title,
            Content = message,
            PrimaryButtonText = action,
            CloseButtonText = "取消",
            DefaultButton = ContentDialogButton.Close,
        };
        return await dialog.ShowAsync() == ContentDialogResult.Primary;
    }

    private async void PluginVersionAction_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        if (sender is not Button { Tag: PluginVersionSnapshot plugin }) return;
        var action = plugin.Active ? "停用" : "启用";
        if (!await ConfirmAsync($"{action}插件", $"{plugin.Name} {plugin.Version}\n{plugin.Id}", action))
            return;
        SetBusy(true);
        try {
            if (plugin.Active) await _pluginClient.DeactivateAsync(plugin.Id, plugin.Version);
            else await _pluginClient.ActivateAsync(plugin.Id, plugin.Version);
            await LoadPluginsAsync();
            ShowStatus($"插件已{action}。", InfoBarSeverity.Success);
        } catch (Exception error) {
            ShowStatus(error.Message, InfoBarSeverity.Error);
        } finally {
            SetBusy(false);
        }
    }

    private async void PluginVersionUninstall_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        if (sender is not Button { Tag: PluginVersionSnapshot plugin } || plugin.Active) return;
        var message = $"{plugin.Name} {plugin.Version}\n{plugin.Id}\n\n"
            + "此操作会删除该版本及其精确授权，无法撤销。插件用户数据会保留。";
        if (!await ConfirmAsync("卸载插件版本", message, "卸载")) return;
        SetBusy(true);
        try {
            await _pluginClient.UninstallAsync(plugin.Id, plugin.Version);
            await LoadPluginsAsync();
            ShowStatus("插件版本已卸载；用户数据已保留。", InfoBarSeverity.Success);
        } catch (Exception error) {
            await LoadPluginsAsync();
            ShowStatus(error.Message, InfoBarSeverity.Error);
        } finally {
            SetBusy(false);
        }
    }

    private async void RecoveryAction_Click(object sender, Microsoft.UI.Xaml.RoutedEventArgs e)
    {
        if (sender is not Button { Tag: PluginRecoverySnapshot item } || !item.CanApply) return;
        var activating = item.Action == "activate";
        var action = activating ? "切换版本" : "清理";
        if (!await ConfirmAsync(action, $"{item.Title}\n{item.Detail}", action)) return;
        SetBusy(true);
        try {
            if (activating) await _pluginClient.ActivateAsync(item.PluginId, item.Version);
            else await _pluginClient.CleanupAsync(item);
            await LoadPluginsAsync();
            ShowStatus($"恢复操作“{action}”已完成。", InfoBarSeverity.Success);
        } catch (Exception error) {
            ShowStatus(error.Message, InfoBarSeverity.Error);
        } finally {
            SetBusy(false);
        }
    }

    private void SetBusy(bool busy)
    {
        SaveButton.IsEnabled = !busy;
        PluginSection.IsEnabled = !busy;
        Status.IsOpen = true;
    }

    private void ShowStatus(string message, InfoBarSeverity severity)
    {
        Status.Title = message;
        Status.Severity = severity;
        Status.IsOpen = true;
    }
}

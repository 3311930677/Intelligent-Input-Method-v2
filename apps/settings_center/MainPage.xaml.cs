using Microsoft.UI.Xaml.Controls;

namespace OwO_Settings;

public sealed partial class MainPage : Page
{
    private readonly ConfigShellClient _client = new();

    public MainPage()
    {
        InitializeComponent();
        ConfigPath.Text = _client.ConfigPath;
        Loaded += async (_, _) => await LoadAsync();
    }

    private async Task LoadAsync()
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
        await LoadAsync();

    private void SetBusy(bool busy)
    {
        SaveButton.IsEnabled = !busy;
        Status.IsOpen = true;
    }

    private void ShowStatus(string message, InfoBarSeverity severity)
    {
        Status.Title = message;
        Status.Severity = severity;
        Status.IsOpen = true;
    }
}

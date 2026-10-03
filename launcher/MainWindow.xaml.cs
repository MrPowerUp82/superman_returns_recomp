using Microsoft.Win32;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using System.Windows.Controls;

namespace SupermanReturnsLauncher;

public partial class MainWindow : Window
{
    private readonly string _settingsPath;
    private LauncherSettings _settings;
    private bool _ready;
    private sealed record Choice(string Value, string Label) { public override string ToString() => Label; }

    public MainWindow()
    {
        InitializeComponent();
        var installId = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(AppContext.BaseDirectory.ToLowerInvariant())))[..12];
        _settingsPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SupermanReturns", "launcher", installId + ".ini");
        _settings = LauncherSettings.DetectDefaults(AppContext.BaseDirectory);
        string? loadWarning = null;
        try { if (File.Exists(_settingsPath)) _settings = LauncherSettings.Load(_settingsPath); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { loadWarning = "Não foi possível ler as preferências salvas. Usando os padrões."; }
        PopulateChoices();
        ApplySettingsToUi();
        foreach (var box in new[] { ResolutionBox, DisplayModeBox, InputBox, GpuBox, RenderScaleBox, AaBox, FilterBox, FpsBox, ShadowBox, MsaaBox })
            box.SelectionChanged += (_, _) => UpdateSummary();
        VSyncBox.Checked += (_, _) => UpdateSummary();
        VSyncBox.Unchecked += (_, _) => UpdateSummary();
        GameExeBox.TextChanged += (_, _) => UpdateSummary();
        GameDataBox.TextChanged += (_, _) => UpdateSummary();
        _ready = true;
        UpdateSummary();
        if (loadWarning is not null) StatusText.Text = loadWarning;
    }

    private static void SetChoices(ComboBox box, params Choice[] choices) => box.ItemsSource = choices;
    private void PopulateChoices()
    {
        SetChoices(ResolutionBox, new("1280x720", "1280 × 720"), new("1600x900", "1600 × 900"), new("1920x1080", "1920 × 1080"), new("2560x1440", "2560 × 1440"), new("3840x2160", "3840 × 2160"));
        SetChoices(DisplayModeBox, new("Fullscreen", "Tela cheia sem bordas"), new("Windowed", "Janela"));
        SetChoices(InputBox, new("KeyboardMouse", "Teclado e mouse"), new("Controller", "Controle Xbox / XInput"));
        SetChoices(RenderScaleBox, new("100", "100% — 720p original"), new("125", "125% — 900p"), new("150", "150% — 1080p"), new("200", "200% — 1440p"), new("300", "300% — 2160p"), new("400", "400% — 2880p"));
        SetChoices(AaBox, new("Off", "Desativado"), new("FXAA", "FXAA"));
        SetChoices(FilterBox, new("Original", "Original do jogo"), new("Anisotropic2x", "Anisotrópico 2×"), new("Anisotropic4x", "Anisotrópico 4×"), new("Anisotropic8x", "Anisotrópico 8×"), new("Anisotropic16x", "Anisotrópico 16×"));
        SetChoices(FpsBox, new("30", "30 FPS — original"), new("60", "60 FPS — experimental"), new("120", "120 FPS — experimental"), new("0", "Sem limite — experimental"));
        SetChoices(ShadowBox, new("1", "Original"), new("2", "2×"), new("4", "4×"));
        SetChoices(MsaaBox, new("1", "Desativado"), new("4", "4×"), new("8", "8×"));
        GpuBox.ItemsSource = GpuEnumerator.GetAdapters();
    }
    private static void Select(ComboBox box, string value) => box.SelectedItem = box.Items.Cast<Choice>().FirstOrDefault(x => x.Value == value) ?? box.Items[0];
    private void ApplySettingsToUi()
    {
        Select(ResolutionBox, _settings.Resolution); Select(DisplayModeBox, _settings.DisplayMode);
        Select(InputBox, _settings.InputMode); Select(RenderScaleBox, _settings.RenderScale);
        Select(AaBox, _settings.Antialiasing); Select(FilterBox, _settings.TextureFilter);
        Select(FpsBox, _settings.FpsLimit); Select(ShadowBox, _settings.ShadowQuality); Select(MsaaBox, _settings.Msaa);
        var adapters = GpuBox.Items.Cast<GpuAdapter>().ToArray();
        GpuBox.SelectedItem = adapters.FirstOrDefault(x => !string.IsNullOrEmpty(_settings.GpuLuid) && x.Luid == _settings.GpuLuid)
            ?? adapters.FirstOrDefault(x => x.Name == _settings.Gpu) ?? adapters[0];
        VSyncBox.IsChecked = _settings.VSync;
        GameExeBox.Text = _settings.GameExe; GameDataBox.Text = _settings.GameData;
    }
    private static string Value(ComboBox box) => ((Choice)box.SelectedItem).Value;
    private void ReadSettings()
    {
        _settings.Resolution = Value(ResolutionBox); _settings.DisplayMode = Value(DisplayModeBox);
        _settings.InputMode = Value(InputBox); _settings.RenderScale = Value(RenderScaleBox);
        _settings.Antialiasing = Value(AaBox); _settings.TextureFilter = Value(FilterBox);
        _settings.FpsLimit = Value(FpsBox); _settings.ShadowQuality = Value(ShadowBox); _settings.Msaa = Value(MsaaBox);
        var gpu = (GpuAdapter)GpuBox.SelectedItem; _settings.Gpu = gpu.Name; _settings.GpuIndex = gpu.Index; _settings.GpuLuid = gpu.Luid;
        _settings.VSync = VSyncBox.IsChecked == true;
        _settings.GameExe = GameExeBox.Text.Trim(); _settings.GameData = GameDataBox.Text.Trim();
    }
    private void UpdateSummary()
    {
        if (!_ready) return;
        ReadSettings();
        SummaryText.Text = $"Direct3D 12 · {_settings.Resolution.Replace("x", " × ")} · {_settings.RenderScale}% interno";
        FpsNote.Text = _settings.FpsLimit == "30" ? "30 FPS preserva o ritmo original do jogo." : "FPS acima de 30 é experimental e pode alterar o ritmo do jogo.";
        ArgumentsPreview.Text = _settings.BuildArguments();
    }
    private void Save_Click(object sender, RoutedEventArgs e)
    {
        try { ReadSettings(); _settings.Save(_settingsPath); StatusText.Text = "Preferências salvas para a próxima execução."; }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { StatusText.Text = "Não foi possível salvar: " + error.Message; }
    }
    private void Play_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            ReadSettings();
            var start = _settings.CreateLaunch(AppContext.BaseDirectory);
            _settings.Save(_settingsPath);
            Process.Start(start);
            Close();
        }
        catch (Exception error) when (error is InvalidOperationException or IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception or ArgumentException)
        { StatusText.Text = error.Message; }
    }
    private void Browse_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "Superman Returns|superman_returns.exe", Title = "Selecione a build do jogo" };
        if (dialog.ShowDialog() != true) return;
        GameExeBox.Text = dialog.FileName;
        var adjacent = Path.Combine(Path.GetDirectoryName(dialog.FileName)!, "game");
        if (File.Exists(Path.Combine(adjacent, "default.xex"))) GameDataBox.Text = adjacent;
    }
    private void BrowseData_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "Selecione a pasta game (default.xex e DATA)" };
        if (dialog.ShowDialog() == true) GameDataBox.Text = dialog.FolderName;
    }
    private void Defaults_Click(object sender, RoutedEventArgs e)
    {
        ReadSettings();
        _settings = new LauncherSettings { GameExe = _settings.GameExe, GameData = _settings.GameData, Gpu = _settings.Gpu, GpuLuid = _settings.GpuLuid };
        _ready = false; ApplySettingsToUi(); _ready = true; UpdateSummary();
        StatusText.Text = "Qualidade original restaurada. Clique em Salvar para guardar.";
    }
}

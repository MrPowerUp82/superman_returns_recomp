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
    private bool _coopOverridesDisplayed;
    private sealed record Choice(string Value, string Label) { public override string ToString() => Label; }

    public MainWindow()
    {
        InitializeComponent();
        var installId = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(AppContext.BaseDirectory.ToLowerInvariant())))[..12];
        _settingsPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SupermanReturns", "launcher", installId + ".ini");
        _settings = LauncherSettings.DetectDefaults(AppContext.BaseDirectory);
        string? loadWarning = null;
        try { if (File.Exists(_settingsPath)) _settings = LauncherSettings.Load(_settingsPath); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { loadWarning = T("LoadWarning"); }
        SetChoices(LanguageBox, new(LauncherText.Portuguese, "Português (Brasil)"), new(LauncherText.English, "English"));
        Select(LanguageBox, _settings.Language);
        ApplyLanguage();
        PopulateChoices();
        ApplySettingsToUi();
        foreach (var box in new[] { RendererBox, ApiBox, ResolutionBox, DisplayModeBox, InputBox, GpuBox, RenderScaleBox, AaBox, FilterBox, FpsBox, CostumeBox, ShadowBox, MsaaBox })
            box.SelectionChanged += (_, _) => UpdateSummary();
        VSyncBox.Checked += (_, _) => UpdateSummary();
        SkipIntroBox.Checked += (_, _) => UpdateSummary();
        SkipIntroBox.Unchecked += (_, _) => UpdateSummary();
        CoopBox.Checked += (_, _) => UpdateSummary();
        CoopBox.Unchecked += (_, _) => UpdateSummary();
        VSyncBox.Unchecked += (_, _) => UpdateSummary();
        GameExeBox.TextChanged += (_, _) => UpdateSummary();
        GameDataBox.TextChanged += (_, _) => UpdateSummary();
        FrameGenerationBox.Checked += (_, _) => UpdateSummary();
        FrameGenerationBox.Unchecked += (_, _) => UpdateSummary();
        FrameGenerationDllBox.TextChanged += (_, _) => UpdateSummary();
        LanguageBox.SelectionChanged += Language_Changed;
        _ready = true;
        UpdateSummary();
        if (loadWarning is not null) StatusText.Text = loadWarning;
    }

    private string T(string key) => LauncherText.Get(key, _settings.Language);

    private void ApplyLanguage()
    {
        Language = System.Windows.Markup.XmlLanguage.GetLanguage(_settings.Language);
        foreach (var key in LauncherText.Strings.Keys) Resources[key] = T(key);
    }

    private void Language_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_ready) return;
        ReadSettings();
        _settings.Language = Value(LanguageBox);
        _ready = false;
        ApplyLanguage();
        PopulateChoices();
        ApplySettingsToUi();
        _ready = true;
        UpdateSummary();
        try { _settings.Save(_settingsPath); StatusText.Text = T("Saved"); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        { StatusText.Text = T("SaveError") + error.Message; }
    }

    private static void SetChoices(ComboBox box, params Choice[] choices) => box.ItemsSource = choices;
    private void PopulateChoices()
    {
        SetChoices(RendererBox, new("native", T("NativeRecommended")), new("xenos", T("XenosCompatibility")));
        SetChoices(ApiBox, new("d3d12", T("D3dRecommended")), new("vulkan", "Vulkan (experimental)"));
        SetChoices(ResolutionBox, new("960x540", T("LowEndResolution")), new("1280x720", "1280 × 720"), new("1600x900", "1600 × 900"), new("1920x1080", "1920 × 1080"), new("2560x1440", "2560 × 1440"), new("3840x2160", "3840 × 2160"));
        SetChoices(DisplayModeBox, new("Fullscreen", T("Borderless")), new("Windowed", T("Windowed")));
        SetChoices(InputBox, new("KeyboardMouse", T("KeyboardMouse")), new("Controller", T("Controller")));
        SetChoices(CostumeBox, new("default", T("DefaultCostume")), new("2", "Golden Age"), new("kingdom_come", T("KingdomCome")));
        SetChoices(RenderScaleBox, new("50", T("Scale50")), new("75", T("Scale75")), new("100", "100% — 720p original"), new("125", "125% — 900p"), new("150", "150% — 1080p"), new("200", "200% — 1440p"), new("300", "300% — 2160p"), new("400", "400% — 2880p"));
        SetChoices(AaBox, new("Off", T("Off")), new("FXAA", "FXAA"));
        SetChoices(FilterBox, new("Original", T("OriginalFilter")), new("Anisotropic2x", T("Anisotropic2")), new("Anisotropic4x", T("Anisotropic4")), new("Anisotropic8x", T("Anisotropic8")), new("Anisotropic16x", T("Anisotropic16")));
        SetChoices(FpsBox, new("0", T("Uncapped")), new("30", "30 FPS — original"), new("60", "60 FPS — experimental"), new("120", "120 FPS — experimental"));
        SetChoices(ShadowBox, new("1", "Original"), new("2", "2×"), new("4", "4×"));
        SetChoices(MsaaBox, new("1", T("Off")), new("4", "4×"), new("8", "8×"));
        GpuBox.ItemsSource = GpuEnumerator.GetAdapters(_settings.Language);
    }
    private static void Select(ComboBox box, string value) => box.SelectedItem = box.Items.Cast<Choice>().FirstOrDefault(x => x.Value == value) ?? box.Items[0];
    private void ApplySettingsToUi()
    {
        _coopOverridesDisplayed = false;
        Select(RendererBox, _settings.Renderer); Select(ApiBox, _settings.GraphicsApi); SkipIntroBox.IsChecked = _settings.SkipIntro;
        Select(ResolutionBox, _settings.Resolution); Select(DisplayModeBox, _settings.DisplayMode);
        Select(InputBox, _settings.InputMode); Select(CostumeBox, _settings.Costume); Select(RenderScaleBox, _settings.RenderScale);
        Select(AaBox, _settings.Antialiasing); Select(FilterBox, _settings.TextureFilter);
        Select(FpsBox, _settings.FpsLimit); Select(ShadowBox, _settings.ShadowQuality); Select(MsaaBox, _settings.Msaa);
        var adapters = GpuBox.Items.Cast<GpuAdapter>().ToArray();
        GpuBox.SelectedItem = adapters.FirstOrDefault(x => !string.IsNullOrEmpty(_settings.GpuLuid) && x.Luid == _settings.GpuLuid)
            ?? adapters.FirstOrDefault(x => x.Name == _settings.Gpu) ?? adapters[0];
        VSyncBox.IsChecked = _settings.VSync;
        CoopBox.IsChecked = _settings.Coop;
        FrameGenerationBox.IsChecked = _settings.FrameGeneration;
        FrameGenerationDllBox.Text = string.IsNullOrWhiteSpace(_settings.FrameGenerationDll) ? LauncherSettings.FindFrameGenerationDll() : _settings.FrameGenerationDll;
        GameExeBox.Text = _settings.GameExe; GameDataBox.Text = _settings.GameData;
    }
    private static string Value(ComboBox box) => ((Choice)box.SelectedItem).Value;
    private void ReadSettings()
    {
        if (!_coopOverridesDisplayed) {
            _settings.Renderer = Value(RendererBox); _settings.GraphicsApi = Value(ApiBox);
            _settings.InputMode = Value(InputBox); _settings.RenderScale = Value(RenderScaleBox);
            _settings.FrameGeneration = FrameGenerationBox.IsChecked == true;
        }
        _settings.SkipIntro = SkipIntroBox.IsChecked == true;
        _settings.Resolution = Value(ResolutionBox); _settings.DisplayMode = Value(DisplayModeBox);
        _settings.Costume = Value(CostumeBox);
        _settings.Antialiasing = Value(AaBox); _settings.TextureFilter = Value(FilterBox);
        _settings.FpsLimit = Value(FpsBox); _settings.ShadowQuality = Value(ShadowBox); _settings.Msaa = Value(MsaaBox);
        var gpu = (GpuAdapter)GpuBox.SelectedItem; _settings.Gpu = gpu.Name; _settings.GpuIndex = gpu.Index; _settings.GpuLuid = gpu.Luid;
        _settings.VSync = VSyncBox.IsChecked == true;
        _settings.Coop = CoopBox.IsChecked == true;
        _settings.FrameGenerationDll = FrameGenerationDllBox.Text.Trim();
        _settings.GameExe = GameExeBox.Text.Trim(); _settings.GameData = GameDataBox.Text.Trim();
    }
    private void UpdateSummary()
    {
        if (!_ready) return;
        ReadSettings();
        // Show the effective co-op choices, retaining solo values in settings.
        // Suppress selection events while applying these temporary overrides.
        _ready = false;
        Select(RendererBox, _settings.Coop ? "native" : _settings.Renderer);
        Select(ApiBox, _settings.Coop ? "d3d12" : _settings.GraphicsApi);
        Select(RenderScaleBox, _settings.Coop ? "100" : _settings.RenderScale);
        Select(InputBox, _settings.Coop ? "Controller" : _settings.InputMode);
        FrameGenerationBox.IsChecked = !_settings.Coop && _settings.FrameGeneration;
        _coopOverridesDisplayed = _settings.Coop;
        _ready = true;
        CoopNote.Visibility = _settings.Coop ? Visibility.Visible : Visibility.Collapsed;
        foreach (var control in new Control[] { RendererBox, ApiBox, RenderScaleBox, InputBox, FrameGenerationBox })
            control.IsEnabled = !_settings.Coop;
        FrameGenerationPanel.Visibility = _settings.FrameGeneration && !_settings.Coop ? Visibility.Visible : Visibility.Collapsed;
        SummaryText.Text = _settings.Coop ? T("CoopSummary") : _settings.UsesVulkan
            ? $"Vulkan · {T("Native")} · {_settings.Resolution.Replace("x", " × ")} · 100% {T("Internal")}"
            : $"Direct3D 12 · {(_settings.Renderer == "xenos" ? "Xenos" : T("Native"))} · {_settings.Resolution.Replace("x", " × ")} · {_settings.RenderScale}% {T("Internal")}";
        FpsNote.Text = !_settings.Coop && !_settings.UsesVulkan && _settings.Renderer == "xenos"
            ? T("XenosFpsNote")
            : _settings.FpsLimit == "30"
                ? T("OriginalFpsNote")
                : _settings.FpsLimit == "0"
                    ? (_settings.VSync ? T("VSyncFpsNote") : T("UncappedFpsNote"))
                    : T("ExperimentalFpsNote");
        EngineNote.Text = _settings.Coop ? T("NativeNote") : _settings.UsesVulkan ? T("VulkanNote") : _settings.Renderer == "xenos" ? (_settings.RenderScale is "50" or "75" ? T("XenosScaleNote") : T("XenosNote")) : _settings.RenderScale is "50" or "75" ? T("NativeScaleNote") : T("NativeNote");
        ArgumentsPreview.Text = _settings.BuildArguments();
    }
    private void Save_Click(object sender, RoutedEventArgs e)
    {
        try { ReadSettings(); _settings.Save(_settingsPath); StatusText.Text = T("Saved"); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { StatusText.Text = T("SaveError") + error.Message; }
    }
    private void Play_Click(object sender, RoutedEventArgs e) => Launch(benchmark: false);
    private void Benchmark_Click(object sender, RoutedEventArgs e) => Launch(benchmark: true);
    private void Launch(bool benchmark)
    {
        try
        {
            ReadSettings();
            var start = _settings.CreateLaunch(AppContext.BaseDirectory, benchmark);
            _settings.Save(_settingsPath);
            Process.Start(start);
            Close();
        }
        catch (Exception error) when (error is InvalidOperationException or IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception or ArgumentException)
        { StatusText.Text = error.Message; }
    }
    private void Browse_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "Superman Returns|superman_returns.exe", Title = T("BrowseExecutable") };
        if (dialog.ShowDialog() != true) return;
        GameExeBox.Text = dialog.FileName;
        var adjacent = Path.Combine(Path.GetDirectoryName(dialog.FileName)!, "game");
        if (File.Exists(Path.Combine(adjacent, "default.xex"))) GameDataBox.Text = adjacent;
    }
    private void BrowseData_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = T("BrowseData") };
        if (dialog.ShowDialog() == true) GameDataBox.Text = dialog.FolderName;
    }
    private void BrowseFrameGeneration_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "Lossless Scaling|Lossless.dll", Title = T("BrowseDll") };
        if (dialog.ShowDialog() == true) FrameGenerationDllBox.Text = dialog.FileName;
    }
    private void Defaults_Click(object sender, RoutedEventArgs e)
    {
        ReadSettings();
        _settings = new LauncherSettings { Language = _settings.Language, Coop = _settings.Coop, GameExe = _settings.GameExe, GameData = _settings.GameData, Gpu = _settings.Gpu, GpuLuid = _settings.GpuLuid };
        _ready = false; ApplySettingsToUi(); _ready = true; UpdateSummary();
        StatusText.Text = T("DefaultsRestored");
    }
}

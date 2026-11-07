using System.Diagnostics;
using System.IO;
using System.Globalization;
using System.Text;
using System.Threading;

namespace SupermanReturnsLauncher;

public sealed class LauncherSettings
{
    private string _language = LauncherText.Portuguese;
    public string Language { get => _language; set => _language = LauncherText.NormalizeLanguage(value); }
    public string GameExe { get; set; } = "superman_returns.exe";
    public string GameData { get; set; } = "";
    public string Resolution { get; set; } = "1280x720";
    public string DisplayMode { get; set; } = "Fullscreen";
    public string Gpu { get; set; } = "Auto";
    public int GpuIndex { get; set; } = -1;
    public string GpuLuid { get; set; } = "";
    public bool VSync { get; set; } = false;
    public string FpsLimit { get; set; } = "0";
    public string RenderScale { get; set; } = "100";
    public string TextureFilter { get; set; } = "Original";
    public string Antialiasing { get; set; } = "Off";
    public string ShadowQuality { get; set; } = "1";
    public string Msaa { get; set; } = "1";
    public string InputMode { get; set; } = "KeyboardMouse";
    public string Renderer { get; set; } = "native";
    // Direct3D 12 is the validated and faster native backend; Vulkan stays selectable.
    public string GraphicsApi { get; set; } = "d3d12";
    public bool UsesVulkan => GraphicsApi == "vulkan";
    public bool SkipIntro { get; set; }
    public bool Coop { get; set; }
    // default (the game's choice), 2 (Golden Age, forced without unlocking) or
    // kingdom_come (skin painted over the Superman Returns suit).
    public string Costume { get; set; } = "default";
    private static readonly string[] Costumes = { "default", "2", "kingdom_come" };
    public int RtShadows { get; set; }
    public int ShadowSoftness { get; set; } = 1;
    public int CharacterShadows { get; set; }
    public int RtReflections { get; set; }
    public int Dlss { get; set; }
    private readonly HashSet<string> _loadedKeys = new(StringComparer.OrdinalIgnoreCase);
    private static readonly HashSet<string> MigratedKeys = new(new[] { "rt_shadows", "shadow_softness", "character_shadows", "rt_reflections", "dlss" }, StringComparer.OrdinalIgnoreCase);
    private Dictionary<string,string> _saved = new(StringComparer.OrdinalIgnoreCase);
    public bool FrameGeneration { get; set; }
    public string FrameGenerationDll { get; set; } = "";

    public static LauncherSettings Load(string path)
    {
        var s = new LauncherSettings();
        if (!File.Exists(path)) return s;
        foreach (var raw in File.ReadAllLines(path))
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith('#')) continue;
            var idx = line.IndexOf('=');
            if (idx <= 0) continue;
            var value = line[(idx + 1)..].Trim();
            s._loadedKeys.Add(line[..idx].Trim());
            switch (line[..idx].Trim().ToLowerInvariant())
            {
                case "rt_shadows": s.RtShadows = ValidChoice(value, 0, 0, 1); break;
                case "shadow_softness": s.ShadowSoftness = ValidChoice(value, 1, 1, 2); break;
                case "character_shadows": s.CharacterShadows = ValidChoice(value, 0, 0, 1); break;
                case "rt_reflections": s.RtReflections = ValidChoice(value, 0, 0, 1); break;
                case "dlss": s.Dlss = ValidChoice(value, 0, 0, 1, 2, 3, 4); break;
                case "language": s.Language = value; break;
                case "game_exe": s.GameExe = value; break;
                case "game_data": s.GameData = value; break;
                case "resolution": s.Resolution = value; break;
                case "display_mode": s.DisplayMode = value; break;
                case "gpu": s.Gpu = value; break;
                case "gpu_luid": s.GpuLuid = value; break;
                case "vsync": if (bool.TryParse(value, out var b)) s.VSync = b; break;
                case "fps_limit": s.FpsLimit = value; break;
                case "render_scale": s.RenderScale = value; break;
                case "texture_filter": s.TextureFilter = value; break;
                case "antialiasing": s.Antialiasing = value; break;
                case "shadow_quality": s.ShadowQuality = value; break;
                case "msaa": s.Msaa = value; break;
                case "input": s.InputMode = value; break;
                case "renderer": s.Renderer = value; break;
                case "graphics_api": s.GraphicsApi = value == "vulkan" ? "vulkan" : "d3d12"; break;
                case "costume": s.Costume = Costumes.Contains(value) ? value : "default"; break;
                case "skip_intro": if (bool.TryParse(value, out var si)) s.SkipIntro = si; break;
                case "coop": if (bool.TryParse(value, out var coop)) s.Coop = coop; break;
                case "frame_generation": if (bool.TryParse(value, out var fg)) s.FrameGeneration = fg; break;
                case "frame_generation_dll": s.FrameGenerationDll = value; break;
            }
        }
        s._saved = s.CurrentValues();
        return s;
    }

    private Dictionary<string,string> CurrentValues()
    {
        return SettingsLines().Select(line => line.Split('=', 2)).Where(parts => parts.Length == 2)
            .ToDictionary(parts => parts[0], parts => parts[1], StringComparer.OrdinalIgnoreCase);
    }

    private string[] SettingsLines()
    {
        return new[] { "# Superman Returns — launcher", $"language={Language}", $"game_exe={GameExe}", $"game_data={GameData}",
            $"resolution={Resolution}", $"display_mode={DisplayMode}", $"gpu={Gpu}", $"gpu_luid={GpuLuid}",
            $"vsync={VSync.ToString().ToLowerInvariant()}", $"fps_limit={FpsLimit}", $"render_scale={RenderScale}",
            $"texture_filter={TextureFilter}", $"antialiasing={Antialiasing}", $"shadow_quality={ShadowQuality}",
            $"msaa={Msaa}", $"input={InputMode}", $"renderer={Renderer}", $"graphics_api={GraphicsApi}",
            $"skip_intro={SkipIntro.ToString().ToLowerInvariant()}", $"costume={Costume}",
            $"coop={Coop.ToString().ToLowerInvariant()}", $"rt_shadows={RtShadows}", $"shadow_softness={ShadowSoftness}",
            $"character_shadows={CharacterShadows}", $"rt_reflections={RtReflections}", $"dlss={Dlss}",
            $"frame_generation={FrameGeneration.ToString().ToLowerInvariant()}", $"frame_generation_dll={FrameGenerationDll}" };
    }

    public void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        using var fileLock = AcquireSettingsLock(path + ".lock");
        var wanted = CurrentValues();
        var remaining = new Dictionary<string,string>(wanted, StringComparer.OrdinalIgnoreCase);
        var output = new List<string>();
        foreach (var raw in File.Exists(path) ? File.ReadAllLines(path) : Array.Empty<string>())
        {
            var equal = raw.IndexOf('=');
            var key = equal > 0 ? raw[..equal].Trim() : "";
            if (!wanted.TryGetValue(key, out var value)) { output.Add(raw); continue; }
            var dirty = !_saved.TryGetValue(key, out var previous) || previous != value;
            if (!dirty) output.Add(raw);
            else if (remaining.ContainsKey(key)) output.Add(key + "=" + value);
            remaining.Remove(key);
        }
        foreach (var (key, value) in remaining)
        {
            // Leave untouched migration fields absent until the game has read its legacy file.
            if (MigratedKeys.Contains(key) && !_loadedKeys.Contains(key) &&
                (!_saved.TryGetValue(key, out var old) || old == value) && IsMigrationDefault(key, value)) continue;
            output.Add(key + "=" + value);
        }
        var temporary = path + ".tmp." + Environment.ProcessId;
        File.WriteAllLines(temporary, output, new UTF8Encoding(false));
        File.Move(temporary, path, true);
        _saved = wanted;
        foreach (var line in output) { var equal = line.IndexOf('='); if (equal > 0) _loadedKeys.Add(line[..equal].Trim()); }
    }

    private static FileStream AcquireSettingsLock(string path)
    {
        for (var attempt = 0; ; ++attempt)
        {
            try { return new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
            catch (IOException) when (attempt < 40) { Thread.Sleep(10); }
        }
    }

    private static bool IsMigrationDefault(string key, string value) => value == (key == "shadow_softness" ? "1" : "0");
    private bool HasExplicitMigrationValue(string key, string value) => _loadedKeys.Contains(key) ||
        !IsMigrationDefault(key, value) || (_saved.TryGetValue(key, out var old) && old != value);

    private static int ValidChoice(string value, int fallback, params int[] choices) =>
        int.TryParse(value, out var number) && choices.Contains(number) ? number : fallback;

    // benchmark: title-screen benchmark (always skips the intro; the engine
    // rejects a flag given twice, so the intro value is replaced, not appended).
    public IReadOnlyList<string> BuildArgumentList(bool benchmark = false)
    {
        var coop = Coop && !benchmark;
        var resolution = Resolution.Split('x');
        int width = 1280, height = 720;
        if (resolution.Length == 2 && int.TryParse(resolution[0], out var w) && int.TryParse(resolution[1], out var h)
            && w >= 640 && w <= 8192 && h >= 480 && h <= 8192) { width = w; height = h; }
        int filter = TextureFilter switch { "Anisotropic2x" => 2, "Anisotropic4x" => 4,
            "Anisotropic8x" => 8, "Anisotropic16x" => 16, _ => -1 };
        // Vulkan does not support the enhancement options yet: they stay at the
        // game's defaults so the engine accepts the launch (backend_selection.cpp).
        var vulkan = !coop && UsesVulkan;
        var percent = coop || vulkan ? 100 : ValidChoice(RenderScale, 100, 50, 75, 100, 125, 150, 200, 300, 400);
        if (vulkan) filter = -1;
        // D3D12 scales every host surface and resolve in one coordinate system.
        // Shrinking the guest's 1280x720 present parameters instead truncates
        // post-process resolves and leaves black/stale regions on screen.
        var nativeD3d12 = !vulkan && (coop || Renderer != "xenos");
        var scale = (nativeD3d12 ? percent : (vulkan ? 100 : Math.Max(100, percent))) / 100.0;
        var result = new List<string> {
            $"--sr_renderer={(!coop && Renderer == "xenos" && !vulkan ? "xenos" : "native")}", "--sr_preset=custom",
            $"--sr_native_api={(vulkan ? "vulkan" : "d3d12")}",
            $"--sr_skip_intro={(SkipIntro || benchmark).ToString().ToLowerInvariant()}",
            "--sr_render_scale=100",
            $"--window_width={width}", $"--window_height={height}",
            $"--fullscreen={(DisplayMode != "Windowed").ToString().ToLowerInvariant()}",
            $"--vsync={VSync.ToString().ToLowerInvariant()}",
            "--d3d12_adapter=-1",
            $"--sr_native_fps_limit={ValidChoice(FpsLimit, 0, 0, 30, 60, 120)}",
            $"--sr_native_render_scale={scale.ToString("0.##", CultureInfo.InvariantCulture)}",
            $"--sr_native_anisotropic_filtering={filter}",
            $"--sr_native_fxaa={(!vulkan && Antialiasing == "FXAA").ToString().ToLowerInvariant()}",
            $"--sr_native_shadow_quality={(vulkan ? 1 : ValidChoice(ShadowQuality, 1, 1, 2, 4))}",
            $"--sr_native_msaa_samples={(vulkan ? 1 : ValidChoice(Msaa, 1, 1, 4, 8))}"
        };
        if (HasExplicitMigrationValue("rt_shadows", RtShadows.ToString())) result.Add($"--sr_rt_shadows={(RtShadows == 1).ToString().ToLowerInvariant()}");
        if (HasExplicitMigrationValue("shadow_softness", ShadowSoftness.ToString())) result.Add($"--sr_shadow_softness={(ShadowSoftness == 2 ? 2 : 1)}");
        if (HasExplicitMigrationValue("character_shadows", CharacterShadows.ToString())) result.Add($"--sr_character_shadows={(CharacterShadows == 1).ToString().ToLowerInvariant()}");
        if (HasExplicitMigrationValue("rt_reflections", RtReflections.ToString())) result.Add($"--sr_rt_reflections={(RtReflections == 1).ToString().ToLowerInvariant()}");
        if (!coop && HasExplicitMigrationValue("dlss", Dlss.ToString())) result.Add("--sr_dlss=" + (Dlss switch { 1 => "quality", 2 => "balanced", 3 => "performance", 4 => "dlaa", _ => "off" }));
        // An empty "--flag=" makes the engine's argument parser take the next argument
        // as its value, so the LUID is passed only when a GPU was chosen.
        if (!string.IsNullOrWhiteSpace(GpuLuid)) result.Add($"--sr_native_gpu_luid={GpuLuid.Trim()}");
        result.Add($"--sr_lsfg={(FrameGeneration && !coop).ToString().ToLowerInvariant()}");
        if (FrameGeneration && !coop) {
            result.Add("--sr_lsfg_mode=performance");
            if (!string.IsNullOrWhiteSpace(FrameGenerationDll)) result.Add("--sr_lsfg_dll=" + FrameGenerationDll.Trim());
        }
        if (Costume == "2") result.Add("--sr_costume=2");
        else if (Costume == "kingdom_come") result.Add("--sr_costume_skin=kingdom_come");
        if (benchmark) result.Add("--sr_benchmark=true");
        if (coop) {
            foreach (var flag in new[] { "input", "autojoin", "second_camera", "camera_probe", "camera_transport",
                "split_preview", "idle", "status_probe", "apt_probe", "stamina_preview" })
                result.Add($"--sr_coop_{flag}=true");
            result.Add("--sr_dlss=off");
        }
        if (!coop && InputMode == "KeyboardMouse") { result.Add("--mnk_mode=true"); result.Add("--mnk_mouse=true"); }
        else { result.Add("--mnk_mode=false"); result.Add("--mnk_mouse=false"); }
        return result;
    }

    public string BuildArguments() => string.Join(' ', BuildArgumentList());

    public ProcessStartInfo CreateLaunch(string baseDirectory, bool benchmark = false, string? settingsPath = null)
    {
        var coop = Coop && !benchmark;
        if (benchmark && !UsesVulkan && Renderer == "xenos")
            throw new InvalidOperationException(LauncherText.Get("BenchmarkRequiresNative", Language));
        if (!coop && !UsesVulkan && Renderer == "xenos" && ValidChoice(RenderScale, 100, 50, 75, 100, 125, 150, 200, 300, 400) < 100)
            throw new InvalidOperationException(LauncherText.Get("XenosScaleError", Language));
        var exe = Path.GetFullPath(GameExe, baseDirectory);
        if (!File.Exists(exe) || !Path.GetFileName(exe).Equals("superman_returns.exe", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException(LauncherText.Get("ExecutableError", Language));
        var data = Path.GetFullPath(string.IsNullOrWhiteSpace(GameData) ? Path.Combine(Path.GetDirectoryName(exe)!, "game") : GameData, baseDirectory);
        if (!File.Exists(Path.Combine(data, "default.xex")) || !Directory.Exists(Path.Combine(data, "DATA")))
            throw new InvalidOperationException(LauncherText.Get("DataError", Language));
        if (!coop && FrameGeneration && (string.IsNullOrWhiteSpace(FrameGenerationDll) || !File.Exists(FrameGenerationDll)))
            throw new InvalidOperationException(LauncherText.Get("DllError", Language));
        var logDir = Path.Combine(Path.GetDirectoryName(exe)!, "logs");
        Directory.CreateDirectory(logDir);
        var start = new ProcessStartInfo(exe) { WorkingDirectory = Path.GetDirectoryName(exe)!, UseShellExecute = false };
        if (!string.IsNullOrWhiteSpace(settingsPath)) start.Environment["SR_LAUNCHER_SETTINGS"] = Path.GetFullPath(settingsPath);
        if (coop) {
            // A launcher session always uses real controllers, even when started
            // from a developer shell that has the probe's environment enabled.
            start.Environment.Remove("SR_BENCH_INPUT");
            start.Environment.Remove("SR_BENCH_INPUT_P2");
        }
        foreach (var argument in BuildArgumentList(benchmark)) start.ArgumentList.Add(argument);
        start.ArgumentList.Add($"--game_data_root={data}");
        start.ArgumentList.Add($"--log_file={Path.Combine(logDir, "game.log")}");
        return start;
    }

    public static LauncherSettings DetectDefaults(string baseDirectory)
    {
        var s = new LauncherSettings { FrameGenerationDll = FindFrameGenerationDll() };
        for (var dir = new DirectoryInfo(baseDirectory); dir is not null; dir = dir.Parent)
        {
            foreach (var suffix in new[] { "superman_returns.exe", "port/out/build/win-amd64-release/superman_returns.exe", "port/out/build/win-amd64-dist/superman_returns.exe" })
            {
                var exe = Path.Combine(dir.FullName, suffix);
                if (!File.Exists(exe)) continue;
                s.GameExe = exe;
                var game = Path.Combine(Path.GetDirectoryName(exe)!, "game");
                if (!File.Exists(Path.Combine(game, "default.xex"))) game = Path.Combine(dir.FullName, "game");
                s.GameData = game;
                return s;
            }
        }
        return s;
    }

    public static string FindFrameGenerationDll()
    {
        var steam = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam", "steamapps", "common", "Lossless Scaling", "Lossless.dll");
        return File.Exists(steam) ? steam : "";
    }
}

using System.Diagnostics;
using System.IO;
using System.Globalization;
using System.Text;

namespace SupermanReturnsLauncher;

public sealed class LauncherSettings
{
    public string GameExe { get; set; } = "superman_returns.exe";
    public string GameData { get; set; } = "";
    public string Resolution { get; set; } = "1280x720";
    public string DisplayMode { get; set; } = "Fullscreen";
    public string Gpu { get; set; } = "Auto";
    public int GpuIndex { get; set; } = -1;
    public string GpuLuid { get; set; } = "";
    public bool VSync { get; set; } = true;
    public string FpsLimit { get; set; } = "30";
    public string RenderScale { get; set; } = "100";
    public string TextureFilter { get; set; } = "Original";
    public string Antialiasing { get; set; } = "Off";
    public string ShadowQuality { get; set; } = "1";
    public string Msaa { get; set; } = "1";
    public string InputMode { get; set; } = "KeyboardMouse";
    public string Renderer { get; set; } = "native";
    public string GraphicsApi { get; set; } = "d3d12";
    public bool SkipIntro { get; set; }

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
            switch (line[..idx].Trim().ToLowerInvariant())
            {
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
                case "graphics_api": s.GraphicsApi = value; break;
                case "skip_intro": if (bool.TryParse(value, out var si)) s.SkipIntro = si; break;
            }
        }
        return s;
    }

    public void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        var lines = new[] { "# Superman Returns — launcher", $"game_exe={GameExe}", $"game_data={GameData}",
            $"resolution={Resolution}", $"display_mode={DisplayMode}", $"gpu={Gpu}", $"gpu_luid={GpuLuid}",
            $"vsync={VSync.ToString().ToLowerInvariant()}", $"fps_limit={FpsLimit}", $"render_scale={RenderScale}",
            $"texture_filter={TextureFilter}", $"antialiasing={Antialiasing}", $"shadow_quality={ShadowQuality}",
            $"msaa={Msaa}", $"input={InputMode}", $"renderer={Renderer}", $"graphics_api={GraphicsApi}",
            $"skip_intro={SkipIntro.ToString().ToLowerInvariant()}" };
        var temporary = path + ".tmp";
        File.WriteAllLines(temporary, lines, new UTF8Encoding(false));
        File.Move(temporary, path, true);
    }

    private static int ValidChoice(string value, int fallback, params int[] choices) =>
        int.TryParse(value, out var number) && choices.Contains(number) ? number : fallback;

    public IReadOnlyList<string> BuildArgumentList()
    {
        var resolution = Resolution.Split('x');
        int width = 1280, height = 720;
        if (resolution.Length == 2 && int.TryParse(resolution[0], out var w) && int.TryParse(resolution[1], out var h)
            && w >= 640 && w <= 8192 && h >= 480 && h <= 8192) { width = w; height = h; }
        int filter = TextureFilter switch { "Anisotropic2x" => 2, "Anisotropic4x" => 4,
            "Anisotropic8x" => 8, "Anisotropic16x" => 16, _ => -1 };
        var percent = ValidChoice(RenderScale, 100, 50, 75, 100, 125, 150, 200, 300, 400);
        var lowScale = percent < 100;
        var scale = (lowScale ? 100 : percent) / 100.0;
        var result = new List<string> {
            $"--sr_renderer={(Renderer == "xenos" ? "xenos" : "native")}", "--sr_preset=custom",
            $"--sr_skip_intro={SkipIntro.ToString().ToLowerInvariant()}",
            $"--sr_render_scale={(lowScale ? percent : 100)}",
            $"--window_width={width}", $"--window_height={height}",
            $"--fullscreen={(DisplayMode != "Windowed").ToString().ToLowerInvariant()}",
            $"--vsync={VSync.ToString().ToLowerInvariant()}",
            "--d3d12_adapter=-1",
            $"--sr_native_gpu_luid={GpuLuid}",
            $"--sr_native_fps_limit={ValidChoice(FpsLimit, 30, 0, 30, 60, 120)}",
            $"--sr_native_render_scale={scale.ToString("0.##", CultureInfo.InvariantCulture)}",
            $"--sr_native_anisotropic_filtering={filter}",
            $"--sr_native_fxaa={(Antialiasing == "FXAA").ToString().ToLowerInvariant()}",
            $"--sr_native_shadow_quality={ValidChoice(ShadowQuality, 1, 1, 2, 4)}",
            $"--sr_native_msaa_samples={ValidChoice(Msaa, 1, 1, 4, 8)}"
        };
        if (InputMode == "KeyboardMouse") { result.Add("--mnk_mode=true"); result.Add("--mnk_mouse=true"); }
        else { result.Add("--mnk_mode=false"); result.Add("--mnk_mouse=false"); }
        return result;
    }

    public string BuildArguments() => string.Join(' ', BuildArgumentList());

    public ProcessStartInfo CreateLaunch(string baseDirectory)
    {
        if (GraphicsApi != "d3d12")
            throw new InvalidOperationException("Vulkan ainda não renderiza o jogo. Selecione Direct3D 12.");
        var exe = Path.GetFullPath(GameExe, baseDirectory);
        if (!File.Exists(exe) || !Path.GetFileName(exe).Equals("superman_returns.exe", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Selecione o executável superman_returns.exe da build do jogo.");
        var data = Path.GetFullPath(string.IsNullOrWhiteSpace(GameData) ? Path.Combine(Path.GetDirectoryName(exe)!, "game") : GameData, baseDirectory);
        if (!File.Exists(Path.Combine(data, "default.xex")) || !Directory.Exists(Path.Combine(data, "DATA")))
            throw new InvalidOperationException("Selecione a pasta game que contém default.xex e a pasta DATA.");
        var logDir = Path.Combine(Path.GetDirectoryName(exe)!, "logs");
        Directory.CreateDirectory(logDir);
        var start = new ProcessStartInfo(exe) { WorkingDirectory = Path.GetDirectoryName(exe)!, UseShellExecute = false };
        foreach (var argument in BuildArgumentList()) start.ArgumentList.Add(argument);
        start.ArgumentList.Add($"--game_data_root={data}");
        start.ArgumentList.Add($"--log_file={Path.Combine(logDir, "game.log")}");
        return start;
    }

    public static LauncherSettings DetectDefaults(string baseDirectory)
    {
        var s = new LauncherSettings();
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
}

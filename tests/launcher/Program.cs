using SupermanReturnsLauncher;

int failed = 0;
void Check(bool condition, string name) { Console.WriteLine($"{(condition ? "PASS" : "FAIL")} {name}"); if (!condition) failed++; }
var settings = new LauncherSettings { GraphicsApi = "d3d12", RenderScale = "150", FpsLimit = "60", Antialiasing = "FXAA", TextureFilter = "Anisotropic8x" };
var commandArgs = settings.BuildArguments();
Check(new LauncherSettings().BuildArgumentList().Contains("--sr_native_fps_limit=0"), "new installs launch without the native 30 FPS limit");
Check(new LauncherSettings().BuildArgumentList().Contains("--vsync=false"), "new installs select Canary's no-vsync unlock mode");
Check(new LauncherSettings { VSync = true }.BuildArgumentList().Contains("--vsync=true"), "explicit VSync remains selectable");
foreach (var api in new[] { "d3d12", "vulkan" })
foreach (var fps in new[] { "0", "30", "60", "120" }) {
    var fpsArgs = new LauncherSettings { GraphicsApi = api, FpsLimit = fps }.BuildArgumentList();
    Check(fpsArgs.Count(a => a.StartsWith("--sr_native_fps_limit=")) == 1 &&
        fpsArgs.Contains("--sr_native_fps_limit=" + fps), $"{api} forwards exactly one FPS choice: {fps}");
}
Check(commandArgs.Contains("--sr_native_render_scale=1.5"), "scale targets native renderer using invariant decimals");
Check(commandArgs.Contains("--sr_native_fps_limit=60"), "frame limit targets native presentation");
Check(commandArgs.Contains("--sr_native_fxaa=true"), "FXAA uses supported option");
Check(commandArgs.Contains("--sr_native_anisotropic_filtering=8"), "anisotropy uses numeric native option");
Check(commandArgs.Contains("--sr_native_api=d3d12") && !commandArgs.Contains("--borderless=") && !commandArgs.Contains("--show_fps="), "unsupported options are not sent");
System.Globalization.CultureInfo.CurrentCulture = new System.Globalization.CultureInfo("pt-BR");
Check(settings.BuildArgumentList().Contains("--sr_native_render_scale=1.5"), "Portuguese locale does not change numeric arguments");
settings.InputMode = "Controller";
Check(settings.BuildArgumentList().Contains("--mnk_mode=false") && settings.BuildArgumentList().Contains("--mnk_mouse=false"), "controller overrides keyboard settings");
var invalid = new LauncherSettings { GraphicsApi = "vulkan", Resolution = "0x0", RenderScale = "50", FpsLimit = "garbage", Msaa = "2" };
Check(invalid.BuildArgumentList().Contains("--window_width=1280") && invalid.BuildArgumentList().Contains("--sr_native_render_scale=1") && invalid.BuildArgumentList().Contains("--sr_native_fps_limit=0") && invalid.BuildArgumentList().Contains("--sr_native_msaa_samples=1"), "invalid preferences fall back to supported defaults");
var automatic = new LauncherSettings { GraphicsApi = "d3d12" }.BuildArgumentList();
Check(!automatic.Any(a => a.EndsWith('=')) && !automatic.Any(a => a.StartsWith("--sr_native_gpu_luid")), "automatic GPU sends no empty argument (the engine would read the next argument as its value)");
var fixture = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "fixture com espaço ç", Guid.NewGuid().ToString("N")));
Directory.CreateDirectory(fixture);
settings.GameExe = Path.Combine(fixture, "superman_returns.exe");
settings.GameData = Path.Combine(fixture, "game ç");
File.WriteAllBytes(settings.GameExe, Array.Empty<byte>());
bool rejected = false;
try { settings.CreateLaunch(fixture); } catch (InvalidOperationException) { rejected = true; }
Check(rejected, "missing game data prevents launch");
Directory.CreateDirectory(Path.Combine(settings.GameData, "DATA"));
File.WriteAllBytes(Path.Combine(settings.GameData, "default.xex"), Array.Empty<byte>());
var launch = settings.CreateLaunch(fixture);
Check(!new LauncherSettings().FrameGeneration && new LauncherSettings().BuildArgumentList().Contains("--sr_lsfg=false"), "frame generation defaults off explicitly");
settings.FrameGeneration = true;
settings.GraphicsApi = "vulkan";
settings.FrameGenerationDll = Path.Combine(fixture, "Lossless.dll");
bool missingFgDll = false;
try { settings.CreateLaunch(fixture); } catch (InvalidOperationException) { missingFgDll = true; }
Check(missingFgDll, "frame generation rejects missing DLL before launch");
File.WriteAllBytes(settings.FrameGenerationDll, new byte[]{1});
var fgLaunch = settings.CreateLaunch(fixture);
Check(fgLaunch.ArgumentList.Contains("--sr_lsfg=true") && fgLaunch.ArgumentList.Contains("--sr_native_api=vulkan"), "frame generation preserves Vulkan");
Check(fgLaunch.ArgumentList.Contains("--sr_lsfg_dll=" + settings.FrameGenerationDll) && fgLaunch.ArgumentList.Contains("--sr_lsfg_mode=performance"), "frame generation DLL path is one argument");
settings.Save(Path.Combine(fixture,"fg.ini"));
var restoredFg = LauncherSettings.Load(Path.Combine(fixture,"fg.ini"));
Check(restoredFg.FrameGeneration && restoredFg.FrameGenerationDll==settings.FrameGenerationDll,"frame generation preference and DLL round trip");
settings.FrameGeneration = false;
File.Delete(settings.FrameGenerationDll);
Check(settings.CreateLaunch(fixture).ArgumentList.Contains("--sr_lsfg=false") && !settings.BuildArgumentList().Any(x=>x.StartsWith("--sr_lsfg_dll=")), "frame generation off works without DLL");
settings.GraphicsApi = "d3d12";
Check(launch.ArgumentList.Contains("--game_data_root=" + settings.GameData), "Unicode path with spaces is one process argument");
Check(launch.WorkingDirectory == fixture && launch.ArgumentList.Contains("--log_file=" + Path.Combine(fixture, "logs", "game.log")), "working directory and log follow selected executable");
settings.Gpu = "NVIDIA GeForce RTX 2060";
settings.GpuLuid = "000000001234ABCD";
Check(settings.BuildArgumentList().Contains("--sr_native_gpu_luid=000000001234ABCD") && settings.BuildArgumentList().Contains("--d3d12_adapter=-1"), "GPU selection uses stable identity instead of another process's index");
var ini = Path.Combine(fixture, "launcher.ini");
settings.Save(ini);
var loaded = LauncherSettings.Load(ini);
Check(loaded.GameData == settings.GameData && loaded.Gpu == settings.Gpu && loaded.GpuLuid == settings.GpuLuid && loaded.InputMode == "Controller", "preferences round trip preserves paths and GPU identity");
var weak = new LauncherSettings { GraphicsApi = "d3d12", Renderer = "native", SkipIntro = true, RenderScale = "75" };
var weakArgs = weak.BuildArgumentList();
Check(weakArgs.Contains("--sr_renderer=native") && weakArgs.Contains("--sr_skip_intro=true"), "engine and intro skip are forwarded");
Check(weakArgs.Contains("--sr_render_scale=100") && weakArgs.Contains("--sr_native_render_scale=0.75"), "540p scales native targets, keeping guest resolves at 720p");
weak.RenderScale = "50";
weak.Resolution = "960x540";
Check(weak.BuildArgumentList().Contains("--sr_native_render_scale=0.5") && weak.BuildArgumentList().Contains("--window_width=960") && weak.BuildArgumentList().Contains("--window_height=540"), "360p renders at half resolution with independent window size");
weak.Renderer = "xenos";
weak.GameExe = settings.GameExe;
weak.GameData = settings.GameData;
bool rejectedXenosScale = false;
try { weak.CreateLaunch(fixture); } catch (InvalidOperationException) { rejectedXenosScale = true; }
Check(rejectedXenosScale, "Xenos does not start with a known-broken low guest resolution");
Check(settings.BuildArgumentList().Contains("--sr_render_scale=100") && settings.BuildArgumentList().Contains("--sr_skip_intro=false"), "defaults keep full quality");
Check(new LauncherSettings().GraphicsApi == "d3d12", "Direct3D 12 is the default (recommended) graphics API");
var vk = new LauncherSettings { GraphicsApi = "vulkan", GameExe = settings.GameExe, GameData = settings.GameData, Renderer = "xenos", RenderScale = "200",
    Antialiasing = "FXAA", Msaa = "4", ShadowQuality = "4", TextureFilter = "Anisotropic16x" };
var vkLaunch = vk.CreateLaunch(fixture);
Check(vkLaunch.ArgumentList.Contains("--sr_native_api=vulkan") && vkLaunch.ArgumentList.Contains("--sr_renderer=native"), "Vulkan launches the native renderer");
Check(vkLaunch.ArgumentList.Contains("--sr_native_render_scale=1") && vkLaunch.ArgumentList.Contains("--sr_native_fxaa=false") &&
    vkLaunch.ArgumentList.Contains("--sr_native_msaa_samples=1") && vkLaunch.ArgumentList.Contains("--sr_native_shadow_quality=1") &&
    vkLaunch.ArgumentList.Contains("--sr_native_anisotropic_filtering=-1"), "Vulkan forces options it does not support to game defaults");
var d3d = new LauncherSettings { GraphicsApi = "d3d12", Msaa = "4", Antialiasing = "FXAA" };
Check(d3d.BuildArgumentList().Contains("--sr_native_api=d3d12") && d3d.BuildArgumentList().Contains("--sr_native_msaa_samples=4") &&
    d3d.BuildArgumentList().Contains("--sr_native_fxaa=true"), "Direct3D 12 keeps its enhancement options");
var oldIni = Path.Combine(fixture, "old.ini");File.WriteAllText(oldIni, "graphics_api=d3d12\n");
Check(LauncherSettings.Load(oldIni).GraphicsApi == "d3d12", "a saved Direct3D 12 choice is kept");
Check(LauncherSettings.Load(oldIni).BuildArgumentList().Contains("--sr_native_fps_limit=0"), "old preferences without FPS use the uncapped default");
foreach (var fps in new[] { "0", "30", "60", "120" }) {
    var fpsPath = Path.Combine(fixture, "fps.ini");
    new LauncherSettings { FpsLimit = fps }.Save(fpsPath);
    Check(LauncherSettings.Load(fpsPath).BuildArgumentList().Contains("--sr_native_fps_limit=" + fps),
        $"saved FPS choice {fps} survives restart, including original 30 FPS preferences");
}
foreach (var invalidFps in new[] { "", "-1", "29", "240", "garbage" })
    Check(new LauncherSettings { FpsLimit = invalidFps }.BuildArgumentList().Contains("--sr_native_fps_limit=0"),
        $"invalid FPS {invalidFps} uses the uncapped default");
foreach (var vsync in new[] { false, true }) {
    var syncPath = Path.Combine(fixture, "vsync.ini");
    new LauncherSettings { VSync = vsync }.Save(syncPath);
    Check(LauncherSettings.Load(syncPath).VSync == vsync, $"saved VSync {vsync} survives restart");
}
Check(LauncherSettings.Load(ini).Renderer == "native", "renderer default survives round trip");
foreach (var api in new[] { "vulkan", "d3d12" }) {
    var apiPath = Path.Combine(fixture, "api.ini");
    new LauncherSettings { GraphicsApi = api }.Save(apiPath);
    Check(LauncherSettings.Load(apiPath).GraphicsApi == api, $"saved {api} preference survives the new default");
}
var bench = new LauncherSettings { GraphicsApi = "d3d12", SkipIntro = false }.BuildArgumentList(benchmark: true);
Check(bench.Count(a => a.StartsWith("--sr_skip_intro=")) == 1 && bench.Contains("--sr_skip_intro=true"), "benchmark skips the intro once even when the preference is off");
Check(bench.Count(a => a.StartsWith("--sr_benchmark=")) == 1 && bench.Contains("--sr_benchmark=true"), "benchmark flag sent once");
Check(!new LauncherSettings().BuildArgumentList().Any(a => a.StartsWith("--sr_benchmark")), "normal play sends no benchmark flag");
var xenosMessage = "";
try { new LauncherSettings { GraphicsApi = "d3d12", Renderer = "xenos" }.CreateLaunch(AppContext.BaseDirectory, benchmark: true); }
catch (InvalidOperationException error) { xenosMessage = error.Message; }
Check(xenosMessage == "O benchmark precisa do renderizador Nativo (Direct3D 12 ou Vulkan).", "benchmark refuses the Xenos renderer");
int Flags(IReadOnlyList<string> args, string prefix) => args.Count(a => a.StartsWith(prefix));
var kingdom = new LauncherSettings { Costume = "kingdom_come" }.BuildArgumentList();
Check(Flags(kingdom, "--sr_costume_skin=") == 1 && kingdom.Contains("--sr_costume_skin=kingdom_come") &&
      Flags(kingdom, "--sr_costume=") == 0, "Kingdom Come sends only the skin (it paints the Superman Returns suit)");
var golden = new LauncherSettings { Costume = "2" }.BuildArgumentList();
Check(Flags(golden, "--sr_costume=") == 1 && golden.Contains("--sr_costume=2") && Flags(golden, "--sr_costume_skin=") == 0,
      "Golden Age forces costume 2 once, without a skin");
var gameDefault = new LauncherSettings().BuildArgumentList();
Check(new LauncherSettings().Costume == "default" && Flags(gameDefault, "--sr_costume") == 0, "the game's own costume sends no costume flag");
var benchKingdom = new LauncherSettings { Costume = "kingdom_come" }.BuildArgumentList(benchmark: true);
Check(benchKingdom.Contains("--sr_costume_skin=kingdom_come"), "the benchmark keeps the costume choice");
var costumePath = Path.Combine(fixture, "costume.ini");
foreach (var costume in new[] { "kingdom_come", "2", "default" }) {
    new LauncherSettings { Costume = costume }.Save(costumePath);
    Check(LauncherSettings.Load(costumePath).Costume == costume, $"saved costume {costume} survives restart");
}
File.WriteAllText(costumePath, "costume=batman\n");
Check(LauncherSettings.Load(costumePath).Costume == "default", "an unknown saved costume falls back to the game's choice");
Check(new LauncherSettings().Language == "pt-BR", "new preferences retain Portuguese as the default");
Check(LauncherSettings.Load(oldIni).Language == "pt-BR", "existing preferences without a language retain Portuguese");
var languagePath = Path.Combine(fixture, "language.ini");
foreach (var language in new[] { "pt-BR", "en" }) {
    var localized = new LauncherSettings { Language = language, GraphicsApi = "d3d12", Renderer = "xenos" };
    localized.Save(languagePath);
    Check(LauncherSettings.Load(languagePath).Language == language, $"saved language {language} survives restart");
    var message = "";
    try { localized.CreateLaunch(fixture, benchmark: true); }
    catch (InvalidOperationException error) { message = error.Message; }
    Check(message == (language == "en"
        ? "The benchmark requires the Native renderer (Direct3D 12 or Vulkan)."
        : "O benchmark precisa do renderizador Nativo (Direct3D 12 ou Vulkan)."), $"launch validation uses {language}");
}
File.WriteAllText(languagePath, "language=unknown\n");
Check(LauncherSettings.Load(languagePath).Language == "pt-BR", "unknown saved languages fall back to Portuguese");
Check(new LauncherSettings { Language = "en", RenderScale = "150" }.BuildArgumentList()
    .SequenceEqual(new LauncherSettings { Language = "pt-BR", RenderScale = "150" }.BuildArgumentList()),
    "launcher language leaves game arguments unchanged");
Check(!new LauncherSettings().Coop && !LauncherSettings.Load(oldIni).Coop,
    "co-op defaults off for new and existing preferences");
var coopPath = Path.Combine(fixture, "coop.ini");
foreach (var enabled in new[] { true, false }) {
    new LauncherSettings { Coop = enabled }.Save(coopPath);
    Check(LauncherSettings.Load(coopPath).Coop == enabled, $"co-op preference {enabled} survives restart");
}
var coopSettings = new LauncherSettings { Coop = true, GraphicsApi = "vulkan", Renderer = "xenos", RenderScale = "50",
    InputMode = "KeyboardMouse", FrameGeneration = true, GameExe = settings.GameExe, GameData = settings.GameData };
var coopArgs = coopSettings.BuildArgumentList();
Check(coopArgs.Contains("--sr_renderer=native") && coopArgs.Contains("--sr_native_api=d3d12") &&
    coopArgs.Contains("--sr_native_render_scale=1") && coopArgs.Contains("--sr_render_scale=100") &&
    coopArgs.Contains("--sr_dlss=off") && coopArgs.Contains("--sr_lsfg=false"),
    "co-op selects the validated split renderer even with incompatible solo preferences");
Check(coopArgs.Contains("--mnk_mode=false") && coopArgs.Contains("--mnk_mouse=false"),
    "co-op uses two real controllers");
foreach (var flag in new[] { "input", "autojoin", "second_camera", "camera_probe", "camera_transport",
    "split_preview", "idle", "status_probe", "apt_probe", "stamina_preview" })
    Check(coopArgs.Contains($"--sr_coop_{flag}=true"), $"co-op enables {flag}");
Check(coopArgs.GroupBy(a => a.Split('=')[0]).All(group => group.Count() == 1),
    "co-op forwards every option once (duplicate arguments are rejected by the engine)");
var inheritedInput = Environment.GetEnvironmentVariable("SR_BENCH_INPUT");
var inheritedP2 = Environment.GetEnvironmentVariable("SR_BENCH_INPUT_P2");
try {
    Environment.SetEnvironmentVariable("SR_BENCH_INPUT", "developer-p1.txt");
    Environment.SetEnvironmentVariable("SR_BENCH_INPUT_P2", "developer-p2.txt");
    var coopLaunch = coopSettings.CreateLaunch(fixture);
    Check(!coopLaunch.Environment.ContainsKey("SR_BENCH_INPUT") && !coopLaunch.Environment.ContainsKey("SR_BENCH_INPUT_P2"),
        "co-op launch excludes inherited synthetic input files");
    Check(coopLaunch.ArgumentList.Contains("--sr_coop_autojoin=true") &&
        !coopLaunch.ArgumentList.Any(a => a.StartsWith("--sr_coop_script=") || a.StartsWith("--user_data_root=")),
        "co-op uses a built-in join with the normal save, without a development mailbox");
} finally {
    Environment.SetEnvironmentVariable("SR_BENCH_INPUT", inheritedInput);
    Environment.SetEnvironmentVariable("SR_BENCH_INPUT_P2", inheritedP2);
}
Check(coopSettings.Coop && coopSettings.Renderer == "xenos" && coopSettings.GraphicsApi == "vulkan" &&
    coopSettings.RenderScale == "50" && coopSettings.InputMode == "KeyboardMouse" && coopSettings.FrameGeneration,
    "co-op does not overwrite solo preferences");
var coopBenchmark = coopSettings.BuildArgumentList(benchmark: true);
Check(!coopBenchmark.Any(a => a.StartsWith("--sr_coop_")) && coopBenchmark.Contains("--sr_native_api=vulkan"),
    "benchmark ignores the co-op preference and retains solo graphics");
coopSettings.Coop = false;
var soloAgain = coopSettings.BuildArgumentList();
Check(!soloAgain.Any(a => a.StartsWith("--sr_coop_")) && soloAgain.Contains("--sr_native_api=vulkan") &&
    soloAgain.Contains("--mnk_mode=true") && soloAgain.Contains("--sr_lsfg=true"),
    "disabling co-op restores solo graphics and input");
Console.WriteLine($"{failed} failed checks");
return failed == 0 ? 0 : 1;

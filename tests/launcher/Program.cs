using SupermanReturnsLauncher;

int failed = 0;
void Check(bool condition, string name) { Console.WriteLine($"{(condition ? "PASS" : "FAIL")} {name}"); if (!condition) failed++; }
var settings = new LauncherSettings { GraphicsApi = "d3d12", RenderScale = "150", FpsLimit = "60", Antialiasing = "FXAA", TextureFilter = "Anisotropic8x" };
var commandArgs = settings.BuildArguments();
Check(commandArgs.Contains("--sr_native_render_scale=1.5"), "scale targets native renderer using invariant decimals");
Check(commandArgs.Contains("--sr_native_fps_limit=60"), "frame limit targets native presentation");
Check(commandArgs.Contains("--sr_native_fxaa=true"), "FXAA uses supported option");
Check(commandArgs.Contains("--sr_native_anisotropic_filtering=8"), "anisotropy uses numeric native option");
Check(commandArgs.Contains("--sr_native_api=d3d12") && !commandArgs.Contains("--borderless=") && !commandArgs.Contains("--show_fps="), "unsupported options are not sent");
System.Globalization.CultureInfo.CurrentCulture = new System.Globalization.CultureInfo("pt-BR");
Check(settings.BuildArgumentList().Contains("--sr_native_render_scale=1.5"), "Portuguese locale does not change numeric arguments");
settings.InputMode = "Controller";
Check(settings.BuildArgumentList().Contains("--mnk_mode=false") && settings.BuildArgumentList().Contains("--mnk_mouse=false"), "controller overrides keyboard settings");
var invalid = new LauncherSettings { Resolution = "0x0", RenderScale = "50", FpsLimit = "garbage", Msaa = "2" };
Check(invalid.BuildArgumentList().Contains("--window_width=1280") && invalid.BuildArgumentList().Contains("--sr_native_render_scale=1") && invalid.BuildArgumentList().Contains("--sr_native_fps_limit=30") && invalid.BuildArgumentList().Contains("--sr_native_msaa_samples=1"), "invalid preferences fall back to supported defaults");
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
Check(launch.ArgumentList.Contains("--game_data_root=" + settings.GameData), "Unicode path with spaces is one process argument");
Check(launch.WorkingDirectory == fixture && launch.ArgumentList.Contains("--log_file=" + Path.Combine(fixture, "logs", "game.log")), "working directory and log follow selected executable");
settings.Gpu = "NVIDIA GeForce RTX 2060";
settings.GpuLuid = "000000001234ABCD";
Check(settings.BuildArgumentList().Contains("--sr_native_gpu_luid=000000001234ABCD") && settings.BuildArgumentList().Contains("--d3d12_adapter=-1"), "GPU selection uses stable identity instead of another process's index");
var ini = Path.Combine(fixture, "launcher.ini");
settings.Save(ini);
var loaded = LauncherSettings.Load(ini);
Check(loaded.GameData == settings.GameData && loaded.Gpu == settings.Gpu && loaded.GpuLuid == settings.GpuLuid && loaded.InputMode == "Controller", "preferences round trip preserves paths and GPU identity");
var weak = new LauncherSettings { GraphicsApi = "d3d12", Renderer = "xenos", SkipIntro = true, RenderScale = "75" };
var weakArgs = weak.BuildArgumentList();
Check(weakArgs.Contains("--sr_renderer=xenos") && weakArgs.Contains("--sr_skip_intro=true"), "engine and intro skip are forwarded");
Check(weakArgs.Contains("--sr_render_scale=75") && weakArgs.Contains("--sr_native_render_scale=1"), "reduced scale uses guest render scale");
Check(settings.BuildArgumentList().Contains("--sr_render_scale=100") && settings.BuildArgumentList().Contains("--sr_skip_intro=false"), "defaults keep full quality");
Check(new LauncherSettings().UsesVulkan, "Vulkan is the default graphics API");
var vk = new LauncherSettings { GameExe = settings.GameExe, GameData = settings.GameData, Renderer = "xenos", RenderScale = "200",
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
Check(LauncherSettings.Load(ini).Renderer == "native", "renderer default survives round trip");
Console.WriteLine($"{failed} failed checks");
return failed == 0 ? 0 : 1;

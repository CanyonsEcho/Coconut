using System.Diagnostics;
using System.Net.Http;
using System.Text.Json;

namespace Coconut.RipperBridge;

/// <summary>
/// Headless companion CLI that bridges Coconut (C++ / RmlUi) with AssetRipper
/// (https://github.com/AssetRipper/AssetRipper), launching AssetRipper.GUI.Free in
/// --headless mode, driving its HTTP export pipeline, and streaming structured JSON
/// progress events to Coconut over stdout.
/// </summary>
public static class Program
{
    public static async Task<int> Main(string[] args)
    {
        string? inputPath = null;
        string? outputPath = null;
        string? ripperExe = null;
        string unityVersion = "2022.3.15f1";
        string scriptLevel = "Level2";
        int port = 52890;

        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--input" when i + 1 < args.Length: inputPath = args[++i]; break;
                case "--output" when i + 1 < args.Length: outputPath = args[++i]; break;
                case "--assetripper" when i + 1 < args.Length: ripperExe = args[++i]; break;
                case "--unity-version" when i + 1 < args.Length: unityVersion = args[++i]; break;
                case "--script-level" when i + 1 < args.Length: scriptLevel = args[++i]; break;
                case "--port" when i + 1 < args.Length && int.TryParse(args[++i], out var p): port = p; break;
                case "--help":
                case "-h":
                    PrintUsage();
                    return 0;
            }
        }

        if (string.IsNullOrWhiteSpace(inputPath) || string.IsNullOrWhiteSpace(outputPath))
        {
            EmitEvent("error", "Missing required --input or --output arguments.");
            PrintUsage();
            return 2;
        }

        ripperExe ??= Environment.GetEnvironmentVariable("ASSETRIPPER_PATH");
        Process? spawned = null;

        try
        {
            using var http = new HttpClient { BaseAddress = new Uri($"http://127.0.0.1:{port}"), Timeout = TimeSpan.FromMinutes(10) };

            bool serverReady = await WaitForServerAsync(http, TimeSpan.FromSeconds(1));
            if (!serverReady)
            {
                if (string.IsNullOrWhiteSpace(ripperExe) || !File.Exists(ripperExe))
                {
                    EmitEvent("error", "AssetRipper binary not found. Pass --assetripper <path/to/AssetRipper.GUI.Free> or set ASSETRIPPER_PATH.");
                    return 3;
                }

                EmitEvent("status", $"Launching AssetRipper headless on port {port}...");
                spawned = Process.Start(new ProcessStartInfo
                {
                    FileName = ripperExe,
                    Arguments = $"--headless --port {port}",
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                });

                serverReady = await WaitForServerAsync(http, TimeSpan.FromSeconds(20));
                if (!serverReady)
                {
                    EmitEvent("error", "Timed out waiting for AssetRipper headless HTTP server.");
                    return 4;
                }
            }

            EmitEvent("progress", $"Loading Unity build: {inputPath}");
            await http.PostAsync("/Reset", new FormUrlEncodedContent([]));

            var endpoint = Directory.Exists(inputPath) ? "/LoadFolder" : "/LoadFile";
            var loadResp = await http.PostAsync(endpoint, new FormUrlEncodedContent([new("path", inputPath)]));
            loadResp.EnsureSuccessStatusCode();

            EmitEvent("progress", $"Applying export settings (ScriptContentLevel={scriptLevel}, Target={unityVersion})...");
            await http.PostAsync("/Settings/Update", new FormUrlEncodedContent([
                new("ScriptContentLevel", scriptLevel),
                new("ImageExportFormat", "Png"),
                new("MeshExportFormat", "Native")
            ]));

            EmitEvent("progress", $"Exporting Unity project to {outputPath}...");
            Directory.CreateDirectory(outputPath);
            var exportResp = await http.PostAsync("/Export/UnityProject", new FormUrlEncodedContent([new("path", outputPath)]));
            exportResp.EnsureSuccessStatusCode();

            EmitEvent("done", $"AssetRipper export completed successfully -> {outputPath}");
            return 0;
        }
        catch (Exception ex)
        {
            EmitEvent("error", $"AssetRipper bridge error: {ex.Message}");
            return 1;
        }
        finally
        {
            if (spawned is { HasExited: false })
            {
                try { spawned.Kill(entireProcessTree: true); } catch { /* ignore */ }
            }
        }
    }

    private static async Task<bool> WaitForServerAsync(HttpClient http, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                using var resp = await http.GetAsync("/");
                if ((int)resp.StatusCode >= 200 && (int)resp.StatusCode < 500) return true;
            }
            catch { /* retry */ }
            await Task.Delay(250);
        }
        return false;
    }

    private static void EmitEvent(string kind, string message)
    {
        Console.WriteLine(JsonSerializer.Serialize(new { eventType = kind, message, timestamp = DateTime.UtcNow }));
    }

    private static void PrintUsage()
    {
        Console.WriteLine("Coconut.RipperBridge — AssetRipper Headless Automation Bridge");
        Console.WriteLine("Usage: Coconut.RipperBridge --input <gameDirOrApk> --output <unityProjectDir> [--assetripper <exe>] [--port 52890] [--unity-version 2022.3.15f1]");
    }
}

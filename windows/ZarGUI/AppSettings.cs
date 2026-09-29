using System.Text.Json;
using System.Text.Json.Serialization;

namespace ZarGUI;

internal enum ExistingArchive { Ask, Replace, KeepBoth }

internal enum ExtractDestination { Ask, NextToArchive }

internal enum Compression { Faster, Standard, Smaller }

/// <summary>User settings, stored as JSON in %LOCALAPPDATA%\ZarGUI (the app is unpackaged,
/// so ApplicationData is not available).</summary>
internal sealed class AppSettings
{
    private static readonly string FilePath =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ZarGUI", "settings.json");

    private static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() },
    };

    public static AppSettings Current { get; } = Load();

    /// <summary>Raised on the UI thread after <see cref="Save"/>.</summary>
    public static event Action? Changed;

    /// <summary>Null means next to the original folder.</summary>
    public string? OutputFolder { get; set; }
    public ExistingArchive ExistingArchive { get; set; } = ExistingArchive.Ask;
    public Compression Compression { get; set; } = Compression.Standard;
    public bool SkipSystemFiles { get; set; } = true;
    public ExtractDestination ExtractDestination { get; set; } = ExtractDestination.Ask;
    public bool RevealAfterExtract { get; set; }

    [JsonIgnore]
    public Packer.Options PackOptions => new(SkipSystemFiles, Compression switch
    {
        Compression.Faster => 1,
        Compression.Smaller => 12,
        _ => 0,
    });

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, Json));
        }
        catch (IOException)
        {
            // Settings still apply for this session.
        }
        catch (UnauthorizedAccessException)
        {
        }
        Changed?.Invoke();
    }

    private static AppSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(FilePath), Json) ?? new();
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            // Unreadable settings fall back to defaults.
        }
        return new();
    }
}

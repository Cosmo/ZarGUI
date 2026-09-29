using System.Globalization;

namespace ZarGUI;

internal static class Format
{
    private static readonly string[] Units = { "KB", "MB", "GB", "TB" };

    /// <summary>File sizes the way File Explorer shows them (1024-based).</summary>
    public static string Bytes(ulong bytes)
    {
        if (bytes < 1024) return bytes == 1 ? "1 byte" : $"{bytes} bytes";
        double value = bytes;
        int unit = -1;
        do
        {
            value /= 1024;
            unit++;
        } while (value >= 1024 && unit < Units.Length - 1);
        string number = value < 10 ? value.ToString("0.#", CultureInfo.CurrentCulture)
                                   : value.ToString("0", CultureInfo.CurrentCulture);
        return $"{number} {Units[unit]}";
    }
}

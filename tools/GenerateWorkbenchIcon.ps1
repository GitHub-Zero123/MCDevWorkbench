param(
    [string]$OutputPath = (Join-Path $PSScriptRoot "..\assets\MCDevWorkbench.ico")
)

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 20, 24, 32, 40, 48, 64, 256)
$frames = [System.Collections.Generic.List[byte[]]]::new()

foreach ($size in $sizes) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    $inset = [Math]::Max(1.0, $size * 0.008)
    $radius = $size * 0.1875
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $radius * 2.0
    $bounds = [System.Drawing.RectangleF]::new($inset, $inset, $size - 2.0 * $inset, $size - 2.0 * $inset)
    $path.AddArc($bounds.Left, $bounds.Top, $diameter, $diameter, 180, 90)
    $path.AddArc($bounds.Right - $diameter, $bounds.Top, $diameter, $diameter, 270, 90)
    $path.AddArc($bounds.Right - $diameter, $bounds.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($bounds.Left, $bounds.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()

    $background = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 33, 33, 33))
    $border = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 74, 74, 74), [Math]::Max(1.0, $size * 0.016))
    $graphics.FillPath($background, $path)
    $graphics.DrawPath($border, $path)

    $strokeWidth = [Math]::Max(1.6, $size * 0.086)
    $foreground = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 236, 236, 236), $strokeWidth)
    $foreground.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $foreground.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $foreground.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $graphics.DrawLines($foreground, @(
        [System.Drawing.PointF]::new($size * 0.281, $size * 0.297),
        [System.Drawing.PointF]::new($size * 0.477, $size * 0.500),
        [System.Drawing.PointF]::new($size * 0.281, $size * 0.703)
    ))
    $graphics.DrawLine(
        $foreground,
        [System.Drawing.PointF]::new($size * 0.516, $size * 0.703),
        [System.Drawing.PointF]::new($size * 0.742, $size * 0.703))

    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames.Add($stream.ToArray())

    $stream.Dispose()
    $foreground.Dispose()
    $border.Dispose()
    $background.Dispose()
    $path.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($resolvedOutput)
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$file = [System.IO.File]::Create($resolvedOutput)
$writer = [System.IO.BinaryWriter]::new($file)
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$frames.Count)

$offset = 6 + 16 * $frames.Count
for ($index = 0; $index -lt $frames.Count; ++$index) {
    $size = $sizes[$index]
    $encodedSize = if ($size -ge 256) { 0 } else { $size }
    $writer.Write([Byte]$encodedSize)
    $writer.Write([Byte]$encodedSize)
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$frames[$index].Length)
    $writer.Write([UInt32]$offset)
    $offset += $frames[$index].Length
}

foreach ($frame in $frames) {
    $writer.Write($frame)
}

$writer.Dispose()
$file.Dispose()

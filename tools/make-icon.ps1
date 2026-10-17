# Render the editable SVG geometry using Windows WPF; pack PNG frames into ICO.
# This is a build-time tool only. The application has no WPF/.NET dependency.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore, WindowsBase
$assetDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) 'assets'
[xml]$svg = Get-Content -LiteralPath (Join-Path $assetDirectory 'chatgpt-original.svg') -Raw
$path = @($svg.svg.path)[1].d
$blue = '#2563EB'
$uses = (0..5 | ForEach-Object { '<use href="#mark" transform="rotate(' + ($_ * 60) + ' 1203 1203)"/>' }) -join "`n"
$source = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2406 2406"><defs><path id="mark" fill="' + $blue + '" d="' + $path + '"/></defs>' + $uses + '</svg>'
[IO.File]::WriteAllText((Join-Path $assetDirectory 'chatgpt-blue.svg'), $source, [Text.Encoding]::UTF8)
$frames = @()
foreach ($size in @(16, 20, 24, 32, 40, 48, 64, 128, 256)) {
    $visual = New-Object Windows.Media.DrawingVisual
    $context = $visual.RenderOpen()
    $scale = $size / 2406.0
    $context.PushTransform((New-Object Windows.Media.ScaleTransform($scale, $scale)))
    foreach ($angle in @(0, 60, 120, 180, 240, 300)) {
        $geometry = [Windows.Media.Geometry]::Parse($path).Clone()
        $geometry.Transform = New-Object Windows.Media.RotateTransform($angle, 1203, 1203)
        $context.DrawGeometry([Windows.Media.BrushConverter]::new().ConvertFromString($blue), $null, $geometry)
    }
    $context.Pop()
    $context.Close()
    $bitmap = New-Object Windows.Media.Imaging.RenderTargetBitmap($size, $size, 96, 96, [Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $encoder = New-Object Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = New-Object IO.MemoryStream
    $encoder.Save($stream)
    $frames += ,@($size, $stream.ToArray())
    if ($size -eq 256) { [IO.File]::WriteAllBytes((Join-Path $assetDirectory 'chatgpt-blue.png'), $stream.ToArray()) }
    $stream.Dispose()
}
$output = [IO.File]::Create((Join-Path $assetDirectory 'chatgpt-blue.ico'))
$writer = New-Object IO.BinaryWriter($output)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$frames.Count)
    $offset = 6 + 16 * $frames.Count
    foreach ($frame in $frames) {
        $dimension = if ($frame[0] -eq 256) { 0 } else { $frame[0] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$frame[1].Length); $writer.Write([uint32]$offset)
        $offset += $frame[1].Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame[1]) }
} finally { $writer.Dispose(); $output.Dispose() }

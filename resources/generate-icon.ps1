# Rebuild the application icon from vector shapes; no external asset dependency.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$taskImages = @()
foreach ($taskSize in @(16, 24, 32, 48, 64, 128, 256)) {
    $taskBitmap = [Drawing.Bitmap]::new($taskSize, $taskSize)
    $taskGraphics = [Drawing.Graphics]::FromImage($taskBitmap)
    $taskGraphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $taskGraphics.ScaleTransform($taskSize / 256.0, $taskSize / 256.0)
    $taskPath = [Drawing.Drawing2D.GraphicsPath]::new()
    foreach ($taskArc in @(@(8,8,60,60,180), @(188,8,60,60,270), @(188,188,60,60,0), @(8,188,60,60,90))) {
        $taskPath.AddArc($taskArc[0], $taskArc[1], $taskArc[2], $taskArc[3], $taskArc[4], 90)
    }
    $taskPath.CloseFigure()
    $taskBackground = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(24, 35, 52))
    $taskWhite = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(240, 248, 255))
    $taskAccent = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(48, 210, 163))
    $taskPen = [Drawing.Pen]::new($taskAccent, 15)
    $taskPen.StartCap = [Drawing.Drawing2D.LineCap]::Round
    $taskPen.EndCap = [Drawing.Drawing2D.LineCap]::Round
    $taskGraphics.FillPath($taskBackground, $taskPath)
    $taskGraphics.FillEllipse($taskWhite, 100, 62, 56, 56)
    $taskGraphics.FillPie($taskWhite, 79, 123, 98, 96, 180, 180)
    $taskGraphics.DrawLine($taskPen, 53, 56, 181, 56)
    $taskGraphics.FillPolygon($taskAccent, [Drawing.PointF[]]@([Drawing.PointF]::new(176,34),[Drawing.PointF]::new(206,56),[Drawing.PointF]::new(176,78)))
    $taskGraphics.DrawLine($taskPen, 203, 200, 75, 200)
    $taskGraphics.FillPolygon($taskAccent, [Drawing.PointF[]]@([Drawing.PointF]::new(80,178),[Drawing.PointF]::new(50,200),[Drawing.PointF]::new(80,222)))
    $taskStream = [IO.MemoryStream]::new()
    $taskBitmap.Save($taskStream, [Drawing.Imaging.ImageFormat]::Png)
    $taskImages += ,@{ Size = $taskSize; Data = $taskStream.ToArray() }
    $taskStream.Dispose(); $taskPen.Dispose(); $taskAccent.Dispose(); $taskWhite.Dispose()
    $taskBackground.Dispose(); $taskPath.Dispose(); $taskGraphics.Dispose(); $taskBitmap.Dispose()
}
$taskOutput = [IO.File]::Create((Join-Path $PSScriptRoot 'app.ico'))
$taskWriter = [IO.BinaryWriter]::new($taskOutput)
try {
    $taskWriter.Write([uint16]0); $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]$taskImages.Count)
    $taskOffset = 6 + 16 * $taskImages.Count
    foreach ($taskImage in $taskImages) {
        $taskDimension = if ($taskImage.Size -eq 256) { 0 } else { $taskImage.Size }
        $taskWriter.Write([byte]$taskDimension); $taskWriter.Write([byte]$taskDimension)
        $taskWriter.Write([byte]0); $taskWriter.Write([byte]0)
        $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]32)
        $taskWriter.Write([uint32]$taskImage.Data.Length); $taskWriter.Write([uint32]$taskOffset)
        $taskOffset += $taskImage.Data.Length
    }
    foreach ($taskImage in $taskImages) { $taskWriter.Write([byte[]]$taskImage.Data) }
} finally { $taskWriter.Dispose(); $taskOutput.Dispose() }

param([Parameter(Mandatory=$true)][string]$Template,
      [Parameter(Mandatory=$true)][string]$LiveFile,
      [Parameter(Mandatory=$true)][string]$OutputFile,
      [Parameter(Mandatory=$true)][string]$StatusFile)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$utf8=[System.Text.UTF8Encoding]::new($false)
$ns='http://schemas.openxmlformats.org/spreadsheetml/2006/main'
$rns='http://schemas.openxmlformats.org/officeDocument/2006/relationships'
$pns='http://schemas.openxmlformats.org/package/2006/relationships'
$cns='http://schemas.openxmlformats.org/package/2006/content-types'
$culture=[Globalization.CultureInfo]::InvariantCulture
$temporary=$OutputFile+'.tasker-export.tmp'
$archive=$null

function Read-Snapshot([string]$path) {
    if(-not [IO.File]::Exists($path)) {return @()}
    $file=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {
        $reader=[IO.StreamReader]::new($file,[Text.Encoding]::UTF8,$true)
        try {$text=$reader.ReadToEnd()} finally {$reader.Dispose()}
    } finally {$file.Dispose()}
    # Writers flush complete records; omit an unfinished trailing record.
    $last=$text.LastIndexOf("`n")
    if($last -lt 0) {return @()}
    $lines=$text.Substring(0,$last).Split([char]10)
    return @($lines | ForEach-Object { $_.TrimEnd([char]13) } | Where-Object {$_ -match '\S'})
}
function Read-ZipXml([string]$path) {
    $entry=$archive.GetEntry($path)
    if($null -eq $entry) {throw "Template is missing $path"}
    $stream=$entry.Open()
    try {$doc=[Xml.XmlDocument]::new();$doc.PreserveWhitespace=$true;$doc.Load($stream);return ,$doc} finally {$stream.Dispose()}
}
function Write-ZipXml([string]$path,[Xml.XmlDocument]$doc) {
    $old=$archive.GetEntry($path);if($old){$old.Delete()}
    $entry=$archive.CreateEntry($path,[IO.Compression.CompressionLevel]::Optimal)
    $stream=$entry.Open()
    try {$settings=[Xml.XmlWriterSettings]::new();$settings.Encoding=$utf8;$writer=[Xml.XmlWriter]::Create($stream,$settings)
         try {$doc.Save($writer)} finally {$writer.Dispose()}} finally {$stream.Dispose()}
}
function Column-Name([int]$column) {
    $name='';while($column -gt 0){$column--;$name=[char](65+($column%26))+$name;$column=[int][Math]::Floor($column/26)};return $name
}
function Write-DataSheet([string]$path,[string[]]$lines) {
    $old=$archive.GetEntry($path);if($old){$old.Delete()}
    $entry=$archive.CreateEntry($path,[IO.Compression.CompressionLevel]::Optimal)
    $stream=$entry.Open();$settings=[Xml.XmlWriterSettings]::new();$settings.Encoding=$utf8
    $writer=[Xml.XmlWriter]::Create($stream,$settings)
    try {
        $writer.WriteStartDocument();$writer.WriteStartElement('worksheet',$ns)
        $writer.WriteStartElement('sheetViews',$ns);$writer.WriteStartElement('sheetView',$ns);$writer.WriteAttributeString('workbookViewId','0')
        $writer.WriteStartElement('pane',$ns);$writer.WriteAttributeString('ySplit','1');$writer.WriteAttributeString('topLeftCell','A2');$writer.WriteAttributeString('activePane','bottomLeft');$writer.WriteAttributeString('state','frozen');$writer.WriteEndElement();$writer.WriteEndElement();$writer.WriteEndElement()
        $headers=@();if($lines.Count){$headers=$lines[0].Split([char]9)}
        $writer.WriteStartElement('cols',$ns)
        for($c=0;$c -lt $headers.Count;$c++) {
            $width=16;if($headers[$c] -match 'timestamp|date|time'){$width=25};if($headers[$c] -match 'Comment|dataset'){$width=44}
            $writer.WriteStartElement('col',$ns);$writer.WriteAttributeString('min',([string]($c+1)));$writer.WriteAttributeString('max',([string]($c+1)));$writer.WriteAttributeString('width',([string]$width));$writer.WriteAttributeString('customWidth','1');$writer.WriteEndElement()
        }
        $writer.WriteEndElement();$writer.WriteStartElement('sheetData',$ns)
        if($lines.Count -gt 1048576){throw 'Too many rows for an Excel worksheet (maximum 1048576).'}
        for($row=0;$row -lt $lines.Count;$row++) {
            $cells=$lines[$row].Split([char]9)
            if($cells.Count -gt 16384){throw 'Too many columns for an Excel worksheet.'}
            $writer.WriteStartElement('row',$ns);$writer.WriteAttributeString('r',([string]($row+1)))
            for($column=0;$column -lt $cells.Count;$column++) {
                $text=$cells[$column];if($text.Length -eq 0){continue}
                if($text.Length -gt 32767){throw 'A text cell exceeds the Excel limit of 32767 characters.'}
                $writer.WriteStartElement('c',$ns);$writer.WriteAttributeString('r',((Column-Name ($column+1))+[string]($row+1)))
                $number=0.0;$date=[DateTime]::MinValue
                $isDate=$row -gt 0 -and $column -lt $headers.Count -and $headers[$column] -match '^timestamp_local$' -and [DateTime]::TryParseExact($text,[string[]]@('yyyy-MM-dd HH:mm:ss','yyyy-MM-dd HH:mm:ss.fff'),$culture,[Globalization.DateTimeStyles]::None,[ref]$date)
                $isNumber=$row -gt 0 -and [double]::TryParse($text,[Globalization.NumberStyles]::Float,$culture,[ref]$number) -and -not [double]::IsNaN($number) -and -not [double]::IsInfinity($number)
                if($isDate){$writer.WriteAttributeString('s',([string]$dateStyle));$writer.WriteElementString('v',$ns,$date.ToOADate().ToString('R',$culture))}
                elseif($isNumber){$writer.WriteElementString('v',$ns,$number.ToString('R',$culture))}
                else {
                    if($row -eq 0){$writer.WriteAttributeString('s',([string]$headerStyle))}
                    $writer.WriteAttributeString('t','inlineStr');$writer.WriteStartElement('is',$ns);$writer.WriteStartElement('t',$ns);$writer.WriteAttributeString('xml','space','http://www.w3.org/XML/1998/namespace','preserve');$writer.WriteString($text);$writer.WriteEndElement();$writer.WriteEndElement()
                }
                $writer.WriteEndElement()
            }
            $writer.WriteEndElement()
        }
        $writer.WriteEndElement();$writer.WriteEndElement();$writer.WriteEndDocument()
    } finally {$writer.Dispose();$stream.Dispose()}
}

try {
    if(-not [IO.File]::Exists($Template)){throw "Excel template not found: $Template"}
    if([IO.Path]::GetFullPath($Template).Equals([IO.Path]::GetFullPath($OutputFile),[StringComparison]::OrdinalIgnoreCase)){throw 'Choose a data filename different from Tasker_template so the template is preserved.'}
    $directory=[IO.Path]::GetDirectoryName($LiveFile);$base=[IO.Path]::GetFileNameWithoutExtension($LiveFile)
    $live=@(Read-Snapshot $LiveFile)
    $calibration=@(Read-Snapshot ([IO.Path]::Combine($directory,$base+'_calibration.txt')))
    $profiles=@(Read-Snapshot ([IO.Path]::Combine($directory,$base+'_profiles.txt')))
    if(-not $live.Count){$live=@("timestamp_local`tCH0_V`tCH1_V`tCH2_V`tCH3_V`tCH4_V`tCH5_V`tCH6_V`tCH7_V`tCH0_calibrated`tCH1_calibrated`tCH2_calibrated`tCH3_calibrated`tCH4_calibrated`tCH5_calibrated`tCH6_calibrated`tCH7_calibrated`tComment")}
    if(-not $calibration.Count){$calibration=@("timestamp_local`tchannel`tconcentration`tsignal_V")}
    if(-not $profiles.Count){$profiles=@("Profile dataset`ttimestamp_local`tPosition (um)`tCH0_V`tCH1_V`tCH2_V`tCH3_V`tCH4_V`tCH5_V`tCH6_V`tCH7_V`tCH0_calibrated`tCH1_calibrated`tCH2_calibrated`tCH3_calibrated`tCH4_calibrated`tCH5_calibrated`tCH6_calibrated`tCH7_calibrated`tComment")}
    $comments=[Collections.Generic.List[string]]::new();$comments.Add("Source`tProfile dataset`ttimestamp_local`tComment")
    foreach($source in @(@{Name='Live';Lines=$live},@{Name='Profiles';Lines=$profiles})) {
        $lines=$source.Lines;$headers=$lines[0].Split([char]9)
        $commentIndex=[Array]::IndexOf($headers,'Comment');$timeIndex=[Array]::IndexOf($headers,'timestamp_local');$profileIndex=[Array]::IndexOf($headers,'Profile dataset');$profile=''
        for($row=1;$row -lt $lines.Count;$row++) {
            $cells=$lines[$row].Split([char]9)
            if($profileIndex -ge 0 -and $profileIndex -lt $cells.Count -and $cells[$profileIndex]){$profile=$cells[$profileIndex]}
            if($commentIndex -ge 0 -and $commentIndex -lt $cells.Count -and $cells[$commentIndex] -and $timeIndex -ge 0){$comments.Add($source.Name+"`t"+$profile+"`t"+$cells[$timeIndex]+"`t"+$cells[$commentIndex])}
        }
    }
    [IO.File]::WriteAllLines([IO.Path]::Combine($directory,$base+'_comments.txt'),$comments,$utf8)
    [IO.File]::Copy($Template,$temporary,$true)
    $archive=[IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Update)
    $workbook=Read-ZipXml 'xl/workbook.xml';$relationships=Read-ZipXml 'xl/_rels/workbook.xml.rels';$types=Read-ZipXml '[Content_Types].xml';$styles=Read-ZipXml 'xl/styles.xml'
    $styleNs=[Xml.XmlNamespaceManager]::new($styles.NameTable);$styleNs.AddNamespace('s',$ns)
    $fonts=$styles.SelectSingleNode('/s:styleSheet/s:fonts',$styleNs);$fontId=$fonts.ChildNodes.Count
    $boldFont=$styles.CreateElement('font',$ns);$bold=$styles.CreateElement('b',$ns);[void]$boldFont.AppendChild($bold);[void]$fonts.AppendChild($boldFont);$fonts.SetAttribute('count',[string]$fonts.ChildNodes.Count)
    $formats=$styles.SelectSingleNode('/s:styleSheet/s:numFmts',$styleNs)
    if(-not $formats){$formats=$styles.CreateElement('numFmts',$ns);[void]$styles.DocumentElement.PrependChild($formats)}
    $formatId=164;foreach($format in $formats.ChildNodes){$formatId=[Math]::Max($formatId,[int]$format.GetAttribute('numFmtId')+1)}
    $format=$styles.CreateElement('numFmt',$ns);$format.SetAttribute('numFmtId',[string]$formatId);$format.SetAttribute('formatCode','yyyy-mm-dd hh:mm:ss.000');[void]$formats.AppendChild($format);$formats.SetAttribute('count',[string]$formats.ChildNodes.Count)
    $cellFormats=$styles.SelectSingleNode('/s:styleSheet/s:cellXfs',$styleNs);$headerStyle=$cellFormats.ChildNodes.Count
    foreach($spec in @(@{Font=$fontId;Format=0},@{Font=0;Format=$formatId})) {
        $xf=$styles.CreateElement('xf',$ns);$xf.SetAttribute('fontId',[string]$spec.Font);$xf.SetAttribute('numFmtId',[string]$spec.Format);$xf.SetAttribute('fillId','0');$xf.SetAttribute('borderId','0');$xf.SetAttribute('xfId','0');$xf.SetAttribute('applyFont','1');$xf.SetAttribute('applyNumberFormat','1');[void]$cellFormats.AppendChild($xf)
    }
    $dateStyle=$headerStyle+1;$cellFormats.SetAttribute('count',[string]$cellFormats.ChildNodes.Count);Write-ZipXml 'xl/styles.xml' $styles
    $wns=[Xml.XmlNamespaceManager]::new($workbook.NameTable);$wns.AddNamespace('s',$ns);$sheets=$workbook.SelectSingleNode('/s:workbook/s:sheets',$wns)
    $maxId=0;foreach($sheet in $sheets.ChildNodes){$maxId=[Math]::Max($maxId,[int]$sheet.GetAttribute('sheetId'))}
    foreach($item in @(@{Name='Live';Lines=$live},@{Name='Comment';Lines=$comments.ToArray()},@{Name='Calibration';Lines=$calibration},@{Name='Profiles';Lines=$profiles})) {
        $sheet=$null;foreach($candidate in $sheets.ChildNodes){if($candidate.GetAttribute('name') -eq $item.Name){$sheet=$candidate;break}}
        if($sheet) {
            $rid=$sheet.GetAttribute('id',$rns);$relationship=$null
            foreach($candidate in $relationships.DocumentElement.ChildNodes){if($candidate.GetAttribute('Id') -eq $rid){$relationship=$candidate;break}}
            if(-not $relationship){throw 'Invalid template worksheet relationship.'}
            $sheetPath='xl/'+$relationship.GetAttribute('Target').TrimStart('/')
            if($sheetPath.StartsWith('xl/xl/')){$sheetPath=$sheetPath.Substring(3)}
        } else {
            $maxId++;$sheetPath='xl/worksheets/tasker'+$maxId+'.xml';$rid='tasker'+$maxId
            $sheet=$workbook.CreateElement('sheet',$ns);$sheet.SetAttribute('name',$item.Name);$sheet.SetAttribute('sheetId',[string]$maxId);[void]$sheet.SetAttribute('id',$rns,$rid);[void]$sheets.AppendChild($sheet)
            $relationship=$relationships.CreateElement('Relationship',$pns);$relationship.SetAttribute('Id',$rid);$relationship.SetAttribute('Type',$rns+'/worksheet');$relationship.SetAttribute('Target',$sheetPath.Substring(3));[void]$relationships.DocumentElement.AppendChild($relationship)
            $override=$types.CreateElement('Override',$cns);$override.SetAttribute('PartName','/'+$sheetPath);$override.SetAttribute('ContentType','application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml');[void]$types.DocumentElement.AppendChild($override)
        }
        Write-DataSheet $sheetPath $item.Lines
    }
    # Remove obsolete calculation chain after replacing data sheets.
    foreach($relationship in @($relationships.DocumentElement.ChildNodes)){if($relationship.GetAttribute('Type').EndsWith('/calcChain')){[void]$relationships.DocumentElement.RemoveChild($relationship)}}
    $chain=$archive.GetEntry('xl/calcChain.xml');if($chain){$chain.Delete()}
    foreach($override in @($types.DocumentElement.ChildNodes)){if($override.GetAttribute('PartName') -eq '/xl/calcChain.xml'){[void]$types.DocumentElement.RemoveChild($override)}}
    $calc=$workbook.SelectSingleNode('/s:workbook/s:calcPr',$wns);if($calc){$calc.SetAttribute('fullCalcOnLoad','1')}
    $properties=$workbook.SelectSingleNode('/s:workbook/s:workbookPr',$wns);if($properties){$properties.SetAttribute('date1904','0')}
    Write-ZipXml 'xl/workbook.xml' $workbook;Write-ZipXml 'xl/_rels/workbook.xml.rels' $relationships;Write-ZipXml '[Content_Types].xml' $types
    $archive.Dispose();$archive=$null
    # Replace only after the complete workbook is closed successfully.
    if([IO.File]::Exists($OutputFile)){[IO.File]::Replace($temporary,$OutputFile,$null)}else{[IO.File]::Move($temporary,$OutputFile)}
    [IO.File]::WriteAllText($StatusFile,'OK',$utf8)
    exit 0
} catch {
    if($archive){$archive.Dispose()}
    if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}
    [IO.File]::WriteAllText($StatusFile,('ERROR: '+$_.Exception.Message),$utf8)
    exit 1
}

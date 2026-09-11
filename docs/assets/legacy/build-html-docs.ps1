[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$docsRoot = $PSScriptRoot

$pages = @(
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_SYSTEMHANDBUCH.md'
        Output = 'TATARUS_SYSTEMHANDBUCH.html'
        Title = 'Systemhandbuch'
        Eyebrow = 'Architektur und Funktionsweise'
        Summary = 'Das vollständige technische Gesamtbild von Nervensystem, Mehrorganphysiologie, Identität, Prospektion und verkörpertem Training.'
        SourceLink = 'TATARUS_SYSTEMHANDBUCH.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_ORGANISMUS.md'
        Output = 'TATARUS_ORGANISMUS.html'
        Title = 'Organismus, Biologie und 3D-Beobachtung'
        Eyebrow = 'Herz · Kreislauf · Lunge · zwei Nieren · Gehirn'
        Summary = 'Implementierter Organumfang, wissenschaftliche Aussagegrenzen, beidseitige Nieren, Gehirn-Körper-Kopplung und organfokussierte 3D-Ansichten.'
        SourceLink = 'TATARUS_ORGANISMUS.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_EXPLORER_KARTOGRAFIE.md'
        Output = 'TATARUS_EXPLORER_KARTOGRAFIE.html'
        Title = 'Explorer-Kartografie'
        Eyebrow = 'Rover · Drohne · 3D-Voxelkarte'
        Summary = 'Scannerfusion, persistente Umweltkarten, neuronale Explorationssignale, C++- und C-ABI-Integration.'
        SourceLink = 'TATARUS_EXPLORER_KARTOGRAFIE.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_IMAGINATIO.md'
        Output = 'TATARUS_IMAGINATIO.html'
        Title = 'TATARUS IMAGINATIO'
        Eyebrow = 'Visuelle Vorstellung · Neural Painting'
        Summary = '512×512-RGB24-Canvas, Kategorieformen, relationale Mehr-Objekt-Szenen und gelernte prospektive Zustandsfolgen.'
        SourceLink = 'TATARUS_IMAGINATIO.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_IMAGINATIO_UI_BEDIENUNGSANLEITUNG.md'
        Output = 'TATARUS_IMAGINATIO_UI_BEDIENUNGSANLEITUNG.html'
        Title = 'IMAGINATIO UI · Bedienungsanleitung'
        Eyebrow = 'Bebildertes Handbuch · Stufe 1–7'
        Summary = 'Ausführliche Bedienung des KI-Farblabors mit echten UI-Aufnahmen, vollständigen Arbeitsabläufen, Eingabeformaten, Archivierung und Fehlerbehebung.'
        SourceLink = 'TATARUS_IMAGINATIO_UI_BEDIENUNGSANLEITUNG.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_GEWEBEWACHSTUM.md'
        Output = 'TATARUS_GEWEBEWACHSTUM.html'
        Title = 'Gewebewachstum und Mechanik'
        Eyebrow = 'Material · Packung · Druck · Expansion'
        Summary = 'Kausale Umsetzung von synaptischem Materialbedarf, Extrazellulärraum, Gewebedruck, elastischer Expansion und Recycling.'
        SourceLink = 'TATARUS_GEWEBEWACHSTUM.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_UI_HANDBUCH.md'
        Output = 'TATARUS_UI_HANDBUCH.html'
        Title = 'UI-Handbuch'
        Eyebrow = 'Live-Oberfläche verstehen'
        Summary = 'Alle Anzeigen, Ansichten, Steuerungen und Zustände der gekoppelten Nervensystem- und Roboteroberfläche.'
        SourceLink = 'TATARUS_UI_HANDBUCH.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_API_REFERENZ.md'
        Output = 'TATARUS_API_REFERENZ.html'
        Title = 'API-Referenz'
        Eyebrow = 'C++, C ABI und Live-Dienst'
        Summary = 'Typen, Funktionen, Datenverträge, Telemetrie und Endpunkte für die programmatische Nutzung von TATARUS.'
        SourceLink = 'TATARUS_API_REFERENZ.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_INTEGRATION.md'
        Output = 'TATARUS_INTEGRATION.html'
        Title = 'Integrationsleitfaden'
        Eyebrow = 'TATARUS verkörpern'
        Summary = 'Sensorabbildung, Zeitmodell, Motorik, Belohnung, Persistenz und Abnahmekriterien für eigene Roboterumgebungen.'
        SourceLink = 'TATARUS_INTEGRATION.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_VALIDIERUNG.md'
        Output = 'TATARUS_VALIDIERUNG.html'
        Title = 'Validierung'
        Eyebrow = 'Nachweise und Systemgrenzen'
        Summary = 'Testebenen, Reproduzierbarkeit, geprüfte Eigenschaften und klar abgegrenzte Aussagen zum aktuellen Systemstand.'
        SourceLink = 'TATARUS_VALIDIERUNG.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'TATARUS_VISION_VERANTWORTUNG.md'
        Output = 'TATARUS_VISION_VERANTWORTUNG.html'
        Title = 'Vision und Verantwortung'
        Eyebrow = 'Forschung, Sicherheit und Einordnung'
        Summary = 'Technische Leitlinien für Forschung, Sicherheit, Verantwortung und eine sachliche öffentliche Einordnung von TATARUS.'
        SourceLink = 'TATARUS_VISION_VERANTWORTUNG.md'
        IsWiki = $false
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'Wiki.md'
        Output = 'Wiki.html'
        Title = 'System-Wiki'
        Eyebrow = 'Forschungsposition und Einzigartigkeit'
        Summary = 'Technische Verifikation der zentralen Thesen, Forschungsposition, Patentlandschaft und belastbare Abgrenzung der Systemaussagen.'
        SourceLink = 'Wiki.md'
        IsWiki = $true
    }
    [pscustomobject]@{
        Source = Join-Path $docsRoot 'benchmark.md'
        Output = 'benchmark.html'
        Title = 'Skalierungsbenchmark & Sechs-Stunden-Auswertung'
        Eyebrow = 'Leistung · Wachstum · Langzeitlauf'
        Summary = 'Gemessene Skalierung des gekoppelten Organismus und quantitative Einordnung des sechs­stündigen TATARUS-Laufs.'
        SourceLink = 'benchmark.md'
        IsWiki = $true
    }
)

$navigation = @(
    [pscustomobject]@{ Label = 'Start'; Href = 'index.html'; Output = 'index.html' }
    [pscustomobject]@{ Label = 'System'; Href = 'TATARUS_SYSTEMHANDBUCH.html'; Output = 'TATARUS_SYSTEMHANDBUCH.html' }
    [pscustomobject]@{ Label = 'UI'; Href = 'TATARUS_UI_HANDBUCH.html'; Output = 'TATARUS_UI_HANDBUCH.html' }
    [pscustomobject]@{ Label = 'API'; Href = 'TATARUS_API_REFERENZ.html'; Output = 'TATARUS_API_REFERENZ.html' }
    [pscustomobject]@{ Label = 'Karte'; Href = 'TATARUS_EXPLORER_KARTOGRAFIE.html'; Output = 'TATARUS_EXPLORER_KARTOGRAFIE.html' }
    [pscustomobject]@{ Label = 'Imaginatio'; Href = 'TATARUS_IMAGINATIO.html'; Output = 'TATARUS_IMAGINATIO.html' }
    [pscustomobject]@{ Label = 'Imaginatio-UI'; Href = 'TATARUS_IMAGINATIO_UI_BEDIENUNGSANLEITUNG.html'; Output = 'TATARUS_IMAGINATIO_UI_BEDIENUNGSANLEITUNG.html' }
    [pscustomobject]@{ Label = 'Einbau'; Href = 'TATARUS_INTEGRATION.html'; Output = 'TATARUS_INTEGRATION.html' }
    [pscustomobject]@{ Label = 'Tests'; Href = 'TATARUS_VALIDIERUNG.html'; Output = 'TATARUS_VALIDIERUNG.html' }
    [pscustomobject]@{ Label = 'Organe'; Href = 'TATARUS_ORGANISMUS.html'; Output = 'TATARUS_ORGANISMUS.html' }
    [pscustomobject]@{ Label = 'Gewebe'; Href = 'TATARUS_GEWEBEWACHSTUM.html'; Output = 'TATARUS_GEWEBEWACHSTUM.html' }
    [pscustomobject]@{ Label = 'Vision'; Href = 'TATARUS_VISION_VERANTWORTUNG.html'; Output = 'TATARUS_VISION_VERANTWORTUNG.html' }
    [pscustomobject]@{ Label = 'Wiki'; Href = 'Wiki.html'; Output = 'Wiki.html' }
    [pscustomobject]@{ Label = 'Benchmark'; Href = 'benchmark.html'; Output = 'benchmark.html' }
)

function Convert-ToPlainText {
    param([string]$Value)
    $withoutTags = [regex]::Replace($Value, '<[^>]+>', '')
    return [System.Net.WebUtility]::HtmlDecode($withoutTags).Trim()
}

function Get-DocumentNavigation {
    param([string]$CurrentOutput)
    $items = foreach ($item in $navigation) {
        $current = if ($item.Output -eq $CurrentOutput) { ' aria-current="page"' } else { '' }
        "<a href=`"$($item.Href)`"$current>$($item.Label)</a>"
    }
    return ($items -join "`n      ")
}

function Convert-LocalLinks {
    param(
        [string]$Html,
        [bool]$IsWiki
    )

    $result = [regex]::Replace(
        $Html,
        'href="([^"#?]+)\.md(#[^"]*)?"',
        { param($match) 'href="' + $match.Groups[1].Value + '.html' + $match.Groups[2].Value + '"' }
    )

    if ($IsWiki) {
        $result = $result.Replace('href="docs/', 'href="')
        foreach ($sourceFolder in @('tools/', 'include/', 'modules/', 'src/', 'tests/', 'output/')) {
            $result = $result.Replace('href="' + $sourceFolder, 'href="../' + $sourceFolder)
        }
    }

    return $result
}

foreach ($page in $pages) {
    if (-not (Test-Path -LiteralPath $page.Source)) {
        throw "Dokumentationsquelle fehlt: $($page.Source)"
    }

    $markdown = Get-Content -LiteralPath $page.Source -Raw

    if ($page.IsWiki) {
        $mermaidPattern = '(?s)```mermaid\s+.*?```'
        $architectureFigure = '<figure><img src="assets/tatarus-architecture.svg" alt="TATARUS Systemkopplung"><figcaption>Verkörperter Regelkreis zwischen Roboterwelt, Nervennetz, Gewebe, Physiologie, Prospektion und Identität.</figcaption></figure>'
        $markdown = [regex]::Replace($markdown, $mermaidPattern, $architectureFigure, 1)
    }

    $articleHtml = (ConvertFrom-Markdown -InputObject $markdown).Html
    $articleHtml = Convert-LocalLinks -Html $articleHtml -IsWiki $page.IsWiki

    $tocItems = foreach ($match in [regex]::Matches($articleHtml, '(?s)<h([23]) id="([^"]+)">(.*?)</h\1>')) {
        $level = $match.Groups[1].Value
        $id = $match.Groups[2].Value
        $label = [System.Net.WebUtility]::HtmlEncode((Convert-ToPlainText $match.Groups[3].Value))
        "<li class=`"level-$level`"><a href=`"#$id`">$label</a></li>"
    }
    $tocHtml = if ($tocItems.Count -gt 0) { $tocItems -join "`n          " } else { '<li>Keine Unterkapitel</li>' }
    $navHtml = Get-DocumentNavigation -CurrentOutput $page.Output
    $encodedTitle = [System.Net.WebUtility]::HtmlEncode($page.Title)
    $encodedEyebrow = [System.Net.WebUtility]::HtmlEncode($page.Eyebrow)
    $encodedSummary = [System.Net.WebUtility]::HtmlEncode($page.Summary)

    $document = @"
<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="description" content="$encodedSummary">
  <title>TATARUS · $encodedTitle</title>
  <link rel="stylesheet" href="assets/documentation.css">
</head>
<body>
  <a class="skip-link" href="#inhalt">Zum Inhalt springen</a>
  <nav class="site-nav" aria-label="Dokumentationsnavigation">
    <div class="site-nav__inner">
      <a class="brand" href="index.html">TATARUS</a>
      $navHtml
    </div>
  </nav>
  <div class="doc-shell">
    <header class="doc-hero">
      <div class="eyebrow">$encodedEyebrow</div>
      <h1>$encodedTitle</h1>
      <p>$encodedSummary</p>
      <div class="doc-meta"><span>TATARUS 5.0.0</span><span>Stand: 7. September 2026</span><span>GPL-3.0</span></div>
    </header>
    <div class="doc-layout">
      <aside class="toc" aria-label="Inhaltsverzeichnis">
        <strong>Auf dieser Seite</strong>
        <ol>
          $tocHtml
        </ol>
      </aside>
      <main id="inhalt" class="doc-content">
        $articleHtml
        <div class="document-actions">
          <a href="$($page.SourceLink)">Markdown-Quelle</a>
          <a href="index.html#dokumente">Alle Dokumente</a>
        </div>
      </main>
    </div>
    <footer class="doc-footer">TATARUS 5 · Synthetic Organism & Embodied Intelligence SDK · GPL-3.0</footer>
  </div>
</body>
</html>
"@

    Set-Content -LiteralPath (Join-Path $docsRoot $page.Output) -Value $document -Encoding utf8
}

Write-Output ("HTML-Dokumente erstellt: {0}" -f $pages.Count)

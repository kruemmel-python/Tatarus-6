#!/usr/bin/env python3
"""TATARUS Image Forensic Analyzer v3 für IMAGINATIO V14.

Forensischer Vergleich eines Originalbilds mit einer hochaufgelösten TATARUS-Ausgabe.
Das Werkzeug prüft, welcher Anteil der Ausgabe durch klassische Raster-Skalierung
(Nearest/Bilinear/Bicubic/Lanczos) erklärbar ist und wo zusätzliche Zielraumstruktur,
Textur oder stärkere strukturelle Abweichungen auftreten.

Wichtig: "neu" bedeutet in diesem Werkzeug nicht "semantisch aus dem Nichts erzeugt".
Es bedeutet: Die gemessene Zielraumstruktur ist durch die getesteten klassischen
Resampler des Originalrasters nicht hinreichend erklärbar.

Python 3.12+
Abhängigkeiten: numpy, Pillow, matplotlib, scikit-image

Version 3 ergänzt für TATARUS V14:
- Start ohne Positionsargumente: neuestes Galerieartefakt wird automatisch gewählt,
- automatische Zuordnung z. B. `Tatarus159` -> `512/Tatarus159.png`,
- Auswertung der V14-Galerie-Metadaten (Motortrace, Target-Space, Interpolation),
- automatische Berichtablage unter `imaginatio_output/forensics/<artifact-id>`,
- alte manuelle CLI `original generated` bleibt vollständig kompatibel,
- warning-clean auch bei `PYTHONWARNINGS=error`, inklusive unendlichem PSNR.

Version 2 ergänzte:
- automatische Selbstkalibrierung gegen bekannte Resampler-Kontrollen,
- synthetischen Nicht-Resampler-Sensitivitätstest,
- optionalen Legacy-/Clone-Kontrollvergleich,
- explizite Definition der Flächenklassifikation und Blockschwellen,
- INCONCLUSIVE statt harter Aussage, falls die Kalibrierung scheitert.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Final, Iterable, Literal

import numpy as np
from PIL import Image, ImageFilter, ImageOps

try:
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Fehlende Abhängigkeit: matplotlib") from exc

try:
    from skimage.metrics import structural_similarity as skimage_ssim
except ImportError:
    skimage_ssim = None


ResamplerName = Literal["nearest", "bilinear", "bicubic", "lanczos"]
Classification = Literal[
    "resampler_explainable",
    "reconstructed_detail",
    "structural_deviation",
]

RESAMPLERS: Final[tuple[ResamplerName, ...]] = (
    "nearest",
    "bilinear",
    "bicubic",
    "lanczos",
)


@dataclass(slots=True, frozen=True)
class GlobalMetrics:
    resampler: str
    mae: float
    rmse: float
    psnr_db: float
    correlation: float
    ssim: float
    edge_correlation: float
    high_frequency_energy: float
    high_frequency_energy_reference: float


@dataclass(slots=True, frozen=True)
class RegionMetrics:
    index: int
    x: int
    y: int
    width: int
    height: int
    best_resampler: str
    mae: float
    rmse: float
    ssim: float
    edge_correlation: float
    high_frequency_generated: float
    high_frequency_reference: float
    high_frequency_excess: float
    classification: str


@dataclass(slots=True, frozen=True)
class ReverseConsistency:
    mae: float
    rmse: float
    psnr_db: float
    correlation: float
    ssim: float


@dataclass(slots=True, frozen=True)
class AnalysisThresholds:
    explainable_mae_max: float = 1.5
    explainable_ssim_min: float = 0.994
    explainable_hf_excess_max: float = 1.0
    structural_mae_min: float = 7.0
    structural_ssim_max: float = 0.94
    clone_psnr_min: float = 48.0
    clone_ssim_min: float = 0.995
    clone_explainable_area_min: float = 85.0
    clone_hf_ratio_max: float = 1.25
    ambiguous_psnr_min: float = 41.0
    ambiguous_ssim_min: float = 0.985
    ambiguous_explainable_area_min: float = 60.0


@dataclass(slots=True, frozen=True)
class CalibrationCase:
    name: str
    expected: str
    explainable_area_percent: float
    reconstructed_detail_area_percent: float
    structural_deviation_area_percent: float
    verdict: str
    passed: bool


@dataclass(slots=True, frozen=True)
class CalibrationSummary:
    proxy_width: int
    proxy_height: int
    cases: tuple[CalibrationCase, ...]
    passed: bool


@dataclass(slots=True, frozen=True)
class LegacyControlSummary:
    path: str
    width: int
    height: int
    best_resampler: str
    psnr_db: float
    ssim: float
    explainable_area_percent: float
    verdict: str


@dataclass(slots=True, frozen=True)
class TatarusArtifactContext:
    artifact_id: str
    title: str
    metadata_path: str
    stage: str
    operation: str
    render_method: str
    motor_trace_replayed: bool
    executed_directly_on_target_canvas: bool
    raster_interpolation_used: bool
    synthesized_high_frequency_detail: bool
    detail_truth: str
    reference_visible: bool
    recalled_engrams: tuple[int, ...]
    active_assembly: int


@dataclass(slots=True, frozen=True)
class Verdict:
    label: str
    conclusive: bool
    calibration_passed: bool
    simple_upscale_consistent: bool
    best_resampler: str
    best_resampler_psnr_db: float
    best_resampler_ssim: float
    explainable_area_percent: float
    reconstructed_detail_area_percent: float
    structural_deviation_area_percent: float
    generated_high_frequency_ratio: float
    interpretation: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Forensische Original-vs-Ausgabe-Analyse für TATARUS IMAGINATIO V14. "
            "Ohne Positionsargumente wird automatisch das neueste Galerieartefakt "
            "unter imaginatio_output/gallery gewählt und die Referenz im 512-Korpus gesucht."
        )
    )
    parser.add_argument(
        "original",
        nargs="?",
        type=Path,
        help=(
            "Originalbild / sensorische Referenz. Optional: Bei V14-Galerieartefakten "
            "wird die Referenz automatisch aus --source-dir ermittelt."
        ),
    )
    parser.add_argument(
        "generated",
        nargs="?",
        type=Path,
        help=(
            "Neu erzeugtes / vergrößertes Bild. Optional: Ohne Angabe wird das neueste "
            "TATARUS-Galerieartefakt verwendet."
        ),
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path.cwd(),
        help="TATARUS-Projektwurzel (Standard: aktuelles Verzeichnis)",
    )
    parser.add_argument(
        "--artifact",
        type=str,
        default=None,
        help=(
            "Galerie-ID, Artefaktordner oder metadata.json. Ohne Angabe wird das neueste "
            "Artefakt verwendet."
        ),
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=None,
        help="Verzeichnis der Original-/Trainingsbilder (Standard: <project-root>/512)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help=(
            "Ausgabeordner. Standard bei Galerieanalyse: "
            "imaginatio_output/forensics/<artifact-id>; sonst tatarus_forensic_report"
        ),
    )
    parser.add_argument(
        "--block",
        type=int,
        default=64,
        help="Blockgröße der lokalen Analyse in Zielpixeln (Standard: 64)",
    )
    parser.add_argument(
        "--fft-size",
        type=int,
        default=1024,
        help="Maximale Kantenlänge für FFT-/Spektralanalyse (Standard: 1024)",
    )
    parser.add_argument(
        "--ssim-size",
        type=int,
        default=1600,
        help="Maximale Kantenlänge für globales SSIM (Standard: 1600)",
    )
    parser.add_argument(
        "--geometry",
        choices=("auto", "stretch", "fit", "crop"),
        default="auto",
        help=(
            "Geometrie der klassischen Referenz: auto wählt bei gleichem Seitenverhältnis "
            "stretch, sonst fit (Standard: auto)"
        ),
    )
    parser.add_argument(
        "--no-local-images",
        action="store_true",
        help="Keine zusätzlichen lokalen Analysebilder erzeugen",
    )
    parser.add_argument(
        "--skip-calibration",
        action="store_true",
        help=(
            "Selbstkalibrierung überspringen. Dann wird der Hauptbefund als methodisch "
            "nicht selbstkalibriert gekennzeichnet."
        ),
    )
    parser.add_argument(
        "--calibration-size",
        type=int,
        default=768,
        help="Maximale Kantenlänge der schnellen Selbstkalibrierung (Standard: 768)",
    )
    parser.add_argument(
        "--legacy-control",
        type=Path,
        default=None,
        help=(
            "Optionales bekanntes altes Clone/Upscale-Ergebnis. Wird als zusätzliche "
            "Kontrollprobe gegen dasselbe Original ausgewertet."
        ),
    )
    return parser.parse_args()


def _read_json_object(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"Metadaten konnten nicht gelesen werden: {path}") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"Metadaten sind kein JSON-Objekt: {path}")
    return value


def _artifact_created_key(directory: Path) -> tuple[float, str]:
    metadata_path = directory / "metadata.json"
    try:
        metadata = _read_json_object(metadata_path)
        created = str(metadata.get("created", ""))
        if created:
            parsed = datetime.fromisoformat(created.replace("Z", "+00:00"))
            return parsed.timestamp(), directory.name
    except (RuntimeError, ValueError):
        pass
    try:
        return directory.stat().st_mtime, directory.name
    except OSError:
        return 0.0, directory.name


def resolve_artifact_directory(project_root: Path, artifact: str | None) -> Path | None:
    gallery_root = (project_root / "imaginatio_output" / "gallery").resolve()
    if artifact:
        candidate = Path(artifact).expanduser()
        if candidate.exists():
            candidate = candidate.resolve()
            if candidate.is_file():
                if candidate.name.casefold() != "metadata.json":
                    raise RuntimeError(
                        "--artifact als Datei muss auf metadata.json eines Galerieartefakts zeigen"
                    )
                candidate = candidate.parent
            if not candidate.is_dir():
                raise RuntimeError(f"Artefaktpfad ist kein Verzeichnis: {candidate}")
            return candidate
        candidate = gallery_root / artifact
        if not candidate.is_dir():
            raise RuntimeError(f"TATARUS-Galerieartefakt nicht gefunden: {candidate}")
        return candidate.resolve()

    if not gallery_root.is_dir():
        return None
    candidates = [
        directory
        for directory in gallery_root.iterdir()
        if directory.is_dir()
        and not directory.name.startswith(".pending-")
        and (directory / "metadata.json").is_file()
        and (directory / "image.png").is_file()
    ]
    return max(candidates, key=_artifact_created_key) if candidates else None


def artifact_context(directory: Path) -> TatarusArtifactContext:
    metadata_path = directory / "metadata.json"
    metadata = _read_json_object(metadata_path)
    canvas = metadata.get("canvas", {})
    memory = metadata.get("memory", {})
    if not isinstance(canvas, dict):
        canvas = {}
    if not isinstance(memory, dict):
        memory = {}
    recalled = memory.get("recalled_engrams", [])
    if not isinstance(recalled, list):
        recalled = []
    return TatarusArtifactContext(
        artifact_id=str(metadata.get("id", directory.name)),
        title=str(metadata.get("title", directory.name)),
        metadata_path=str(metadata_path),
        stage=str(metadata.get("stage", "unknown")),
        operation=str(metadata.get("operation", "unknown")),
        render_method=str(canvas.get("render_method", "unknown")),
        motor_trace_replayed=bool(canvas.get("motor_trace_replayed", False)),
        executed_directly_on_target_canvas=bool(
            canvas.get("executed_directly_on_target_canvas", False)
        ),
        raster_interpolation_used=bool(canvas.get("raster_interpolation_used", False)),
        synthesized_high_frequency_detail=bool(
            canvas.get("synthesized_high_frequency_detail", False)
        ),
        detail_truth=str(canvas.get("detail_truth", "unknown")),
        reference_visible=bool(metadata.get("reference_visible", False)),
        recalled_engrams=tuple(int(value) for value in recalled),
        active_assembly=int(memory.get("active_assembly", 0) or 0),
    )


def generated_from_artifact(directory: Path) -> Path:
    metadata = _read_json_object(directory / "metadata.json")
    files = metadata.get("files", {})
    if isinstance(files, dict):
        raw = files.get("png")
        if isinstance(raw, str) and raw:
            # API-Pfade enden für lokale Galerieartefakte auf demselben Dateinamen.
            name = Path(raw.replace("\\", "/")).name
            candidate = directory / name
            if candidate.is_file():
                return candidate.resolve()
    candidate = directory / "image.png"
    if not candidate.is_file():
        raise RuntimeError(f"Galerieartefakt enthält kein image.png: {directory}")
    return candidate.resolve()


def _normalized_stem(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", Path(value).stem.casefold())


def original_from_artifact(
    context: TatarusArtifactContext,
    source_dir: Path,
) -> Path:
    if not source_dir.is_dir():
        raise RuntimeError(
            f"Originalverzeichnis nicht gefunden: {source_dir}. "
            "Mit --source-dir kann ein anderes Verzeichnis angegeben werden."
        )

    title = context.title.strip()
    direct_candidates = []
    title_path = Path(title)
    if title_path.suffix:
        direct_candidates.append(source_dir / title_path.name)
    else:
        direct_candidates.extend(
            source_dir / f"{title}{suffix}"
            for suffix in (".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff")
        )

    tatarus_number = re.search(r"(?i)\btatarus[\s_-]*(\d+)\b", title)
    if tatarus_number:
        number = int(tatarus_number.group(1))
        for stem in (f"Tatarus{number}", f"Tatarus{number:04d}"):
            direct_candidates.extend(
                source_dir / f"{stem}{suffix}"
                for suffix in (".png", ".jpg", ".jpeg", ".webp")
            )

    for candidate in direct_candidates:
        if candidate.is_file():
            return candidate.resolve()

    normalized_title = _normalized_stem(title)
    supported = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff"}
    matches = [
        path
        for path in source_dir.iterdir()
        if path.is_file()
        and path.suffix.casefold() in supported
        and _normalized_stem(path.name) == normalized_title
    ]
    if len(matches) == 1:
        return matches[0].resolve()

    raise RuntimeError(
        "Original konnte aus dem V14-Artefakttitel nicht automatisch bestimmt werden. "
        f"Artefakt: {context.artifact_id!r}, Titel: {context.title!r}, "
        f"gesucht in: {source_dir}. Starte alternativ mit: "
        "py .\\tatarus_image_forensic_analyzer.py <originalbild>"
    )


def resolve_inputs(
    args: argparse.Namespace,
) -> tuple[Path, Path, TatarusArtifactContext | None, Path]:
    project_root = args.project_root.expanduser().resolve()
    source_dir = (args.source_dir or project_root / "512").expanduser().resolve()
    artifact_dir = resolve_artifact_directory(project_root, args.artifact)
    context = artifact_context(artifact_dir) if artifact_dir is not None else None

    generated = args.generated.expanduser().resolve() if args.generated else None
    if generated is None and artifact_dir is not None:
        generated = generated_from_artifact(artifact_dir)

    original = args.original.expanduser().resolve() if args.original else None
    if original is None and context is not None:
        original = original_from_artifact(context, source_dir)

    if original is None or generated is None:
        raise RuntimeError(
            "Original und Ausgabe konnten nicht automatisch bestimmt werden. "
            "Direkter Aufruf: py .\\tatarus_image_forensic_analyzer.py "
            "<original.png> <generated.png>. Für V14-Automatik muss "
            "imaginatio_output\\gallery ein Artefakt und <project-root>\\512 "
            "das passende Original enthalten."
        )

    if args.out is not None:
        out_dir = args.out.expanduser().resolve()
    elif context is not None:
        out_dir = (project_root / "imaginatio_output" / "forensics" / context.artifact_id).resolve()
    else:
        out_dir = (project_root / "tatarus_forensic_report").resolve()
    return original, generated, context, out_dir


def ensure_image(path: Path) -> Image.Image:
    if not path.exists():
        raise FileNotFoundError(path)
    with Image.open(path) as image:
        return ImageOps.exif_transpose(image).convert("RGB")


def resampling_filter(name: ResamplerName) -> Image.Resampling:
    match name:
        case "nearest":
            return Image.Resampling.NEAREST
        case "bilinear":
            return Image.Resampling.BILINEAR
        case "bicubic":
            return Image.Resampling.BICUBIC
        case "lanczos":
            return Image.Resampling.LANCZOS
        case _:
            raise ValueError(f"Unbekannter Resampler: {name}")


def infer_background(generated: Image.Image) -> tuple[int, int, int]:
    array = np.asarray(generated)
    h, w = array.shape[:2]
    margin = max(1, min(h, w) // 64)
    samples = np.concatenate([
        array[:margin, :margin].reshape(-1, 3),
        array[:margin, w - margin :].reshape(-1, 3),
        array[h - margin :, :margin].reshape(-1, 3),
        array[h - margin :, w - margin :].reshape(-1, 3),
    ], axis=0)
    median = np.median(samples, axis=0)
    return tuple(int(round(value)) for value in median)


def resolve_geometry(original: Image.Image, generated: Image.Image, mode: str) -> str:
    if mode != "auto":
        return mode
    original_ratio = original.width / original.height
    generated_ratio = generated.width / generated.height
    relative = abs(original_ratio - generated_ratio) / max(original_ratio, generated_ratio)
    return "stretch" if relative <= 0.001 else "fit"


def resize_reference(
    original: Image.Image,
    size: tuple[int, int],
    name: ResamplerName,
    *,
    geometry: str = "stretch",
    background: tuple[int, int, int] = (0, 0, 0),
) -> Image.Image:
    target_w, target_h = size
    filter_ = resampling_filter(name)
    match geometry:
        case "stretch":
            return original.resize(size, filter_)
        case "fit":
            scale = min(target_w / original.width, target_h / original.height)
            new_size = (max(1, round(original.width * scale)), max(1, round(original.height * scale)))
            resized = original.resize(new_size, filter_)
            canvas = Image.new("RGB", size, background)
            canvas.paste(resized, ((target_w - new_size[0]) // 2, (target_h - new_size[1]) // 2))
            return canvas
        case "crop":
            scale = max(target_w / original.width, target_h / original.height)
            new_size = (max(1, round(original.width * scale)), max(1, round(original.height * scale)))
            resized = original.resize(new_size, filter_)
            left = max(0, (new_size[0] - target_w) // 2)
            top = max(0, (new_size[1] - target_h) // 2)
            return resized.crop((left, top, left + target_w, top + target_h))
        case _:
            raise ValueError(f"Unbekannte Geometrie: {geometry}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def json_compatible(value: object) -> object:
    """Konvertiert Analysewerte in strikt JSON-kompatible Daten.

    Exakte Bildübereinstimmung kann einen unendlichen PSNR erzeugen. JSON kennt
    keine IEEE-Infinity-Literale; deshalb wird sie explizit als String serialisiert.
    """
    match value:
        case float() as number if math.isnan(number):
            return None
        case float() as number if math.isinf(number):
            return "Infinity" if number > 0 else "-Infinity"
        case dict() as mapping:
            return {str(key): json_compatible(item) for key, item in mapping.items()}
        case list() as items:
            return [json_compatible(item) for item in items]
        case tuple() as items:
            return [json_compatible(item) for item in items]
        case _:
            return value


def rgb_to_gray(array: np.ndarray) -> np.ndarray:
    value = array.astype(np.float32)
    return value[..., 0] * 0.299 + value[..., 1] * 0.587 + value[..., 2] * 0.114


def finite_correlation(left: np.ndarray, right: np.ndarray) -> float:
    a = left.astype(np.float64, copy=False).reshape(-1)
    b = right.astype(np.float64, copy=False).reshape(-1)
    a -= a.mean()
    b -= b.mean()
    denominator = math.sqrt(float(np.dot(a, a)) * float(np.dot(b, b)))
    if denominator <= 1e-15:
        return 1.0 if np.allclose(left, right) else 0.0
    return float(np.dot(a, b) / denominator)


def basic_ssim(left: np.ndarray, right: np.ndarray) -> float:
    """Global SSIM fallback for installations without scikit-image."""
    x = rgb_to_gray(left).astype(np.float64)
    y = rgb_to_gray(right).astype(np.float64)
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    mx = float(x.mean())
    my = float(y.mean())
    vx = float(x.var())
    vy = float(y.var())
    covariance = float(((x - mx) * (y - my)).mean())
    numerator = (2.0 * mx * my + c1) * (2.0 * covariance + c2)
    denominator = (mx * mx + my * my + c1) * (vx + vy + c2)
    return 1.0 if denominator == 0 else float(numerator / denominator)


def local_ssim(left: np.ndarray, right: np.ndarray) -> float:
    # Für tausende Analyseblöcke verwenden wir absichtlich die globale
    # SSIM-Formel pro Block. Das ist deterministisch und erheblich schneller
    # als ein vollständiger Sliding-Window-SSIM-Aufruf pro Region.
    return basic_ssim(left, right)


def global_ssim(left: np.ndarray, right: np.ndarray, maximum_size: int) -> float:
    h, w = left.shape[:2]
    scale = min(1.0, maximum_size / max(h, w))
    if scale < 1.0:
        size = (max(8, round(w * scale)), max(8, round(h * scale)))
        l = np.asarray(Image.fromarray(left).resize(size, Image.Resampling.BICUBIC))
        r = np.asarray(Image.fromarray(right).resize(size, Image.Resampling.BICUBIC))
    else:
        l, r = left, right
    return local_ssim(l, r)


def gradient_magnitude(gray: np.ndarray) -> np.ndarray:
    gray = gray.astype(np.float32, copy=False)
    dx = np.empty_like(gray)
    dy = np.empty_like(gray)
    dx[:, 1:-1] = 0.5 * (gray[:, 2:] - gray[:, :-2])
    dx[:, 0] = gray[:, 1] - gray[:, 0]
    dx[:, -1] = gray[:, -1] - gray[:, -2]
    dy[1:-1, :] = 0.5 * (gray[2:, :] - gray[:-2, :])
    dy[0, :] = gray[1, :] - gray[0, :]
    dy[-1, :] = gray[-1, :] - gray[-2, :]
    return np.sqrt(dx * dx + dy * dy)


def high_pass_energy(rgb: np.ndarray, radius: float = 1.2) -> float:
    gray = np.clip(rgb_to_gray(rgb), 0, 255).astype(np.uint8)
    blurred = np.asarray(Image.fromarray(gray).filter(ImageFilter.GaussianBlur(radius=radius))).astype(np.float32)
    high = gray.astype(np.float32) - blurred
    return float(np.mean(np.abs(high)))


def full_basic_metrics(left: np.ndarray, right: np.ndarray) -> tuple[float, float, float, float]:
    if left.shape != right.shape:
        raise ValueError("Metriken benötigen gleiche Arraygrößen")
    height = left.shape[0]
    squared_sum = 0.0
    absolute_sum = 0.0
    count = 0
    # Zeilenblöcke verhindern große temporäre float64-Arrays bei 4K/8K.
    for y0 in range(0, height, 256):
        y1 = min(height, y0 + 256)
        a = left[y0:y1].astype(np.float32)
        b = right[y0:y1].astype(np.float32)
        diff = a - b
        squared_sum += float(np.square(diff, dtype=np.float32).sum(dtype=np.float64))
        absolute_sum += float(np.abs(diff).sum(dtype=np.float64))
        count += diff.size
    mse = squared_sum / count
    mae = absolute_sum / count
    rmse = math.sqrt(mse)
    psnr = math.inf if mse <= 0 else 20.0 * math.log10(255.0 / rmse)
    correlation = finite_correlation(rgb_to_gray(left), rgb_to_gray(right))
    return mae, rmse, psnr, correlation


def spectrum_curve(image: np.ndarray, maximum_size: int) -> tuple[np.ndarray, np.ndarray, float]:
    h, w = image.shape[:2]
    scale = min(1.0, maximum_size / max(h, w))
    if scale < 1.0:
        size = (max(64, round(w * scale)), max(64, round(h * scale)))
        reduced = np.asarray(Image.fromarray(image).resize(size, Image.Resampling.BICUBIC))
    else:
        reduced = image
    gray = rgb_to_gray(reduced)
    gray -= float(gray.mean())
    window_y = np.hanning(gray.shape[0]).astype(np.float32)
    window_x = np.hanning(gray.shape[1]).astype(np.float32)
    gray *= window_y[:, None] * window_x[None, :]
    spectrum = np.abs(np.fft.fftshift(np.fft.fft2(gray)))
    cy, cx = np.array(spectrum.shape) // 2
    yy, xx = np.indices(spectrum.shape)
    radius = np.sqrt((yy - cy) ** 2 + (xx - cx) ** 2)
    max_radius = float(radius.max())
    normalized = radius / max_radius
    bins = np.linspace(0.0, 1.0, 129)
    indices = np.digitize(normalized.ravel(), bins) - 1
    radial = np.zeros(len(bins) - 1, dtype=np.float64)
    counts = np.zeros_like(radial)
    flat = spectrum.ravel()
    valid = (indices >= 0) & (indices < radial.size)
    np.add.at(radial, indices[valid], flat[valid])
    np.add.at(counts, indices[valid], 1)
    radial = np.divide(radial, np.maximum(counts, 1))
    centers = 0.5 * (bins[:-1] + bins[1:])
    high_frequency_energy = float(spectrum[normalized > 0.35].sum() / max(spectrum.sum(), 1e-12))
    return centers, radial, high_frequency_energy


def compute_global_metrics(
    generated: np.ndarray,
    reference: np.ndarray,
    resampler: str,
    ssim_size: int,
    fft_size: int,
) -> GlobalMetrics:
    mae, rmse, psnr, corr = full_basic_metrics(generated, reference)
    ssim = global_ssim(generated, reference, ssim_size)
    edge_corr = finite_correlation(
        gradient_magnitude(rgb_to_gray(generated)),
        gradient_magnitude(rgb_to_gray(reference)),
    )
    _, _, hf_gen = spectrum_curve(generated, fft_size)
    _, _, hf_ref = spectrum_curve(reference, fft_size)
    return GlobalMetrics(
        resampler=resampler,
        mae=mae,
        rmse=rmse,
        psnr_db=psnr,
        correlation=corr,
        ssim=ssim,
        edge_correlation=edge_corr,
        high_frequency_energy=hf_gen,
        high_frequency_energy_reference=hf_ref,
    )


def block_score(mae: float, ssim: float, edge_corr: float, hf_excess: float) -> float:
    return (
        0.42 * min(mae / 12.0, 1.5)
        + 0.33 * max(0.0, 1.0 - ssim)
        + 0.15 * max(0.0, 1.0 - edge_corr)
        + 0.10 * min(hf_excess / 8.0, 1.5)
    )


def classify_region(
    mae: float,
    ssim: float,
    edge_corr: float,
    hf_excess: float,
    thresholds: AnalysisThresholds,
) -> Classification:
    """Klassifiziert einen lokalen Zielblock relativ zum besten Resampler.

    ``resampler_explainable`` bedeutet ausdrücklich nicht bloß "ähnlich". Der
    Block muss gleichzeitig alle dokumentierten Explainability-Schwellen erfüllen.
    """
    # Kantenkorrelation ist in nahezu homogenen Blöcken numerisch instabil und
    # wird deshalb nicht als hartes Ausschlusskriterium verwendet.
    if (
        mae <= thresholds.explainable_mae_max
        and ssim >= thresholds.explainable_ssim_min
        and hf_excess <= thresholds.explainable_hf_excess_max
    ):
        return "resampler_explainable"
    if ssim < thresholds.structural_ssim_max and mae > thresholds.structural_mae_min:
        return "structural_deviation"
    return "reconstructed_detail"


def high_pass_map(rgb: np.ndarray, radius: float = 1.2) -> np.ndarray:
    gray = np.clip(rgb_to_gray(rgb), 0, 255).astype(np.uint8)
    blurred = np.asarray(
        Image.fromarray(gray).filter(ImageFilter.GaussianBlur(radius=radius))
    ).astype(np.float32)
    return gray.astype(np.float32) - blurred


def high_frequency_map(rgb: np.ndarray) -> np.ndarray:
    """Schnelle lokale Hochfrequenzkarte über den 4-Nachbar-Laplacian."""
    gray = rgb_to_gray(rgb).astype(np.float32, copy=False)
    result = np.zeros_like(gray)
    result[1:-1, 1:-1] = (
        -4.0 * gray[1:-1, 1:-1]
        + gray[:-2, 1:-1]
        + gray[2:, 1:-1]
        + gray[1:-1, :-2]
        + gray[1:-1, 2:]
    )
    return result

def analyze_regions(
    generated: np.ndarray,
    original: Image.Image,
    target_size: tuple[int, int],
    block_size: int,
    *,
    geometry: str,
    background: tuple[int, int, int],
    thresholds: AnalysisThresholds,
) -> tuple[list[RegionMetrics], np.ndarray, np.ndarray, np.ndarray]:
    h, w = generated.shape[:2]
    rows = math.ceil(h / block_size)
    cols = math.ceil(w / block_size)
    block_count = rows * cols
    resampler_index = {name: index for index, name in enumerate(RESAMPLERS)}

    best_score = np.full(block_count, np.inf, dtype=np.float64)
    best_name: list[str] = ["nearest"] * block_count
    best_values: list[tuple[float, float, float, float, float, float, float] | None] = [None] * block_count

    gen_edges = gradient_magnitude(rgb_to_gray(generated))
    gen_high = high_frequency_map(generated)

    for name in RESAMPLERS:
        reference_image = resize_reference(
            original, target_size, name, geometry=geometry, background=background
        )
        reference = np.asarray(reference_image)
        ref_edges = gradient_magnitude(rgb_to_gray(reference))
        ref_high = high_frequency_map(reference)

        index = 0
        for y in range(0, h, block_size):
            y1 = min(h, y + block_size)
            for x in range(0, w, block_size):
                x1 = min(w, x + block_size)
                gen_block = generated[y:y1, x:x1]
                ref_block = reference[y:y1, x:x1]
                diff = gen_block.astype(np.float32) - ref_block.astype(np.float32)
                mae = float(np.mean(np.abs(diff)))
                rmse = float(np.sqrt(np.mean(diff * diff)))
                ssim = basic_ssim(gen_block, ref_block)
                edge_corr = finite_correlation(
                    gen_edges[y:y1, x:x1], ref_edges[y:y1, x:x1]
                )
                hf_gen = float(np.mean(np.abs(gen_high[y:y1, x:x1])))
                hf_ref = float(np.mean(np.abs(ref_high[y:y1, x:x1])))
                hf_excess = max(0.0, hf_gen - hf_ref)
                score = block_score(mae, ssim, edge_corr, hf_excess)
                if score < best_score[index]:
                    best_score[index] = score
                    best_name[index] = name
                    best_values[index] = (mae, rmse, ssim, edge_corr, hf_gen, hf_ref, hf_excess)
                index += 1

        del reference, ref_edges, ref_high

    class_map = np.zeros((rows, cols), dtype=np.uint8)
    best_map = np.zeros((rows, cols), dtype=np.uint8)
    score_map = best_score.reshape(rows, cols).astype(np.float32)
    results: list[RegionMetrics] = []
    index = 0
    for row, y in enumerate(range(0, h, block_size)):
        y1 = min(h, y + block_size)
        for col, x in enumerate(range(0, w, block_size)):
            x1 = min(w, x + block_size)
            values = best_values[index]
            if values is None:
                raise RuntimeError("Regionale Resampleranalyse blieb ohne Kandidaten")
            mae, rmse, ssim, edge_corr, hf_gen, hf_ref, hf_excess = values
            name = best_name[index]
            classification = classify_region(mae, ssim, edge_corr, hf_excess, thresholds)
            class_id = {
                "resampler_explainable": 0,
                "reconstructed_detail": 1,
                "structural_deviation": 2,
            }[classification]
            class_map[row, col] = class_id
            best_map[row, col] = resampler_index[name]
            results.append(
                RegionMetrics(
                    index=index, x=x, y=y, width=x1-x, height=y1-y,
                    best_resampler=name, mae=mae, rmse=rmse, ssim=ssim,
                    edge_correlation=edge_corr,
                    high_frequency_generated=hf_gen,
                    high_frequency_reference=hf_ref,
                    high_frequency_excess=hf_excess,
                    classification=classification,
                )
            )
            index += 1
    return results, class_map, best_map, score_map

def area_percentages(regions: Iterable[RegionMetrics]) -> dict[str, float]:
    totals = {
        "resampler_explainable": 0,
        "reconstructed_detail": 0,
        "structural_deviation": 0,
    }
    total_area = 0
    for region in regions:
        area = region.width * region.height
        totals[region.classification] += area
        total_area += area
    return {key: (100.0 * value / total_area if total_area else 0.0) for key, value in totals.items()}


def reverse_consistency(generated: Image.Image, original: Image.Image, ssim_size: int) -> ReverseConsistency:
    down = generated.resize(original.size, Image.Resampling.BICUBIC)
    a = np.asarray(down)
    b = np.asarray(original)
    mae, rmse, psnr, corr = full_basic_metrics(a, b)
    return ReverseConsistency(
        mae=mae,
        rmse=rmse,
        psnr_db=psnr,
        correlation=corr,
        ssim=global_ssim(a, b, ssim_size),
    )


def make_verdict(
    metrics: list[GlobalMetrics],
    regions: list[RegionMetrics],
    thresholds: AnalysisThresholds,
    *,
    calibration_passed: bool,
) -> Verdict:
    best = min(metrics, key=lambda item: item.mae)
    percentages = area_percentages(regions)
    hf_ratio = best.high_frequency_energy / max(best.high_frequency_energy_reference, 1e-12)

    clone_like = (
        best.psnr_db >= thresholds.clone_psnr_min
        and best.ssim >= thresholds.clone_ssim_min
        and percentages["resampler_explainable"] >= thresholds.clone_explainable_area_min
        and hf_ratio <= thresholds.clone_hf_ratio_max
    )
    ambiguous = (
        best.psnr_db >= thresholds.ambiguous_psnr_min
        and best.ssim >= thresholds.ambiguous_ssim_min
        and percentages["resampler_explainable"] >= thresholds.ambiguous_explainable_area_min
    )

    if not calibration_passed:
        label = "NICHT AUSWERTBAR – SELBSTKALIBRIERUNG FEHLGESCHLAGEN"
        interpretation = (
            "Mindestens eine bekannte Kontrollprobe wurde vom Detektor nicht wie erwartet "
            "klassifiziert. Der Hauptbefund wird deshalb bewusst nicht als belastbares "
            "Clone-/Nicht-Clone-Urteil ausgegeben."
        )
        conclusive = False
    elif clone_like:
        label = "MIT EINFACHEM UPSCALING VEREINBAR"
        interpretation = (
            "Die Ausgabe ist den getesteten klassischen Resamplern so ähnlich, dass ein "
            "einfacher Raster-Upscale als Erklärung nicht ausgeschlossen werden kann."
        )
        conclusive = True
    elif ambiguous:
        label = "GEMISCHTES / NICHT EINDEUTIGES VERHALTEN"
        interpretation = (
            "Die Ausgabe weicht messbar von klassischer Skalierung ab, bleibt aber in einem "
            "großen Flächenanteil durch die getesteten Resampler erklärbar."
        )
        conclusive = True
    else:
        label = "NICHT MIT EINFACHEM RASTER-UPSCALING VEREINBAR"
        interpretation = (
            "Die Ausgabe weicht lokal und global deutlich von Nearest, Bilinear, Bicubic und "
            "Lanczos ab. Zusätzliche Zielraumstruktur ist messbar. Das beweist keine unbekannte "
            "Originalwahrheit, spricht aber gegen einen bloßen klassischen Resize-Clone."
        )
        conclusive = True

    return Verdict(
        label=label,
        conclusive=conclusive,
        calibration_passed=calibration_passed,
        simple_upscale_consistent=clone_like if calibration_passed else False,
        best_resampler=best.resampler,
        best_resampler_psnr_db=best.psnr_db,
        best_resampler_ssim=best.ssim,
        explainable_area_percent=percentages["resampler_explainable"],
        reconstructed_detail_area_percent=percentages["reconstructed_detail"],
        structural_deviation_area_percent=percentages["structural_deviation"],
        generated_high_frequency_ratio=hf_ratio,
        interpretation=interpretation,
    )



def scaled_proxy_size(size: tuple[int, int], maximum_dimension: int) -> tuple[int, int]:
    if maximum_dimension < 128:
        raise ValueError("--calibration-size muss mindestens 128 sein")
    width, height = size
    scale = min(1.0, maximum_dimension / max(width, height))
    return (
        max(64, int(round(width * scale))),
        max(64, int(round(height * scale))),
    )


def deterministic_detail_control(
    reference: Image.Image,
    background: tuple[int, int, int],
) -> Image.Image:
    """Erzeugt eine absichtlich nicht durch Resampling erklärbare Kontrollprobe.

    Die Probe ist kein TATARUS-Simulator. Sie ist nur ein Sensitivitäts-Sanity-Check:
    Ein Detektor, der selbst diese deterministische Zusatzstruktur als reinen Resize
    klassifiziert, wäre für die Hauptanalyse zu tolerant.
    """
    array = np.asarray(reference).astype(np.float32)
    height, width = array.shape[:2]
    yy, xx = np.indices((height, width), dtype=np.float32)
    phase = 0.173 * xx + 0.117 * yy + 0.00091 * xx * yy
    fine = np.sin(phase) + 0.55 * np.sin(0.071 * xx - 0.193 * yy + 1.7)
    fine /= 1.55

    background_array = np.asarray(background, dtype=np.float32)
    foreground_distance = np.max(np.abs(array - background_array), axis=2)
    mask = np.clip((foreground_distance - 2.0) / 24.0, 0.50, 1.0)[..., None]
    channel_scale = np.asarray([1.0, 0.82, 0.67], dtype=np.float32)
    perturbation = fine[..., None] * channel_scale * 5.5 * mask
    result = np.clip(array + perturbation, 0.0, 255.0).astype(np.uint8)
    return Image.fromarray(result)


def metrics_for_all_resamplers(
    candidate: Image.Image,
    original: Image.Image,
    *,
    geometry: str,
    background: tuple[int, int, int],
    ssim_size: int,
    fft_size: int,
) -> list[GlobalMetrics]:
    candidate_array = np.asarray(candidate)
    metrics: list[GlobalMetrics] = []
    for name in RESAMPLERS:
        reference = resize_reference(
            original,
            candidate.size,
            name,
            geometry=geometry,
            background=background,
        )
        metrics.append(
            compute_global_metrics(
                candidate_array,
                np.asarray(reference),
                name,
                min(ssim_size, max(candidate.size)),
                min(fft_size, max(candidate.size)),
            )
        )
    return metrics


def evaluate_candidate(
    candidate: Image.Image,
    original: Image.Image,
    *,
    block_size: int,
    geometry: str,
    background: tuple[int, int, int],
    thresholds: AnalysisThresholds,
    ssim_size: int,
    fft_size: int,
    calibration_passed: bool = True,
) -> tuple[list[GlobalMetrics], list[RegionMetrics], Verdict]:
    metrics = metrics_for_all_resamplers(
        candidate,
        original,
        geometry=geometry,
        background=background,
        ssim_size=ssim_size,
        fft_size=fft_size,
    )
    regions, _, _, _ = analyze_regions(
        np.asarray(candidate),
        original,
        candidate.size,
        block_size,
        geometry=geometry,
        background=background,
        thresholds=thresholds,
    )
    verdict = make_verdict(
        metrics,
        regions,
        thresholds,
        calibration_passed=calibration_passed,
    )
    return metrics, regions, verdict


def run_self_calibration(
    original: Image.Image,
    generated_size: tuple[int, int],
    *,
    block_size: int,
    geometry: str,
    background: tuple[int, int, int],
    thresholds: AnalysisThresholds,
    maximum_dimension: int,
) -> CalibrationSummary:
    proxy_size = scaled_proxy_size(generated_size, maximum_dimension)
    calibration_block = max(16, min(block_size, max(16, min(proxy_size) // 8)))
    cases: list[CalibrationCase] = []

    for name in RESAMPLERS:
        exact = resize_reference(
            original,
            proxy_size,
            name,
            geometry=geometry,
            background=background,
        )
        _, regions, verdict = evaluate_candidate(
            exact,
            original,
            block_size=calibration_block,
            geometry=geometry,
            background=background,
            thresholds=thresholds,
            ssim_size=min(768, maximum_dimension),
            fft_size=min(512, maximum_dimension),
        )
        percentages = area_percentages(regions)
        passed = (
            percentages["resampler_explainable"] >= 99.0
            and verdict.simple_upscale_consistent
        )
        cases.append(
            CalibrationCase(
                name=f"exact_{name}",
                expected="known_resampler",
                explainable_area_percent=percentages["resampler_explainable"],
                reconstructed_detail_area_percent=percentages["reconstructed_detail"],
                structural_deviation_area_percent=percentages["structural_deviation"],
                verdict=verdict.label,
                passed=passed,
            )
        )

    base = resize_reference(
        original,
        proxy_size,
        "bicubic",
        geometry=geometry,
        background=background,
    )
    detail = deterministic_detail_control(base, background)
    _, detail_regions, detail_verdict = evaluate_candidate(
        detail,
        original,
        block_size=calibration_block,
        geometry=geometry,
        background=background,
        thresholds=thresholds,
        ssim_size=min(768, maximum_dimension),
        fft_size=min(512, maximum_dimension),
    )
    detail_percentages = area_percentages(detail_regions)
    non_resampler_area = (
        detail_percentages["reconstructed_detail"]
        + detail_percentages["structural_deviation"]
    )
    detail_passed = non_resampler_area >= 10.0 and not detail_verdict.simple_upscale_consistent
    cases.append(
        CalibrationCase(
            name="deterministic_added_detail",
            expected="non_resampler_sensitivity",
            explainable_area_percent=detail_percentages["resampler_explainable"],
            reconstructed_detail_area_percent=detail_percentages["reconstructed_detail"],
            structural_deviation_area_percent=detail_percentages["structural_deviation"],
            verdict=detail_verdict.label,
            passed=detail_passed,
        )
    )

    return CalibrationSummary(
        proxy_width=proxy_size[0],
        proxy_height=proxy_size[1],
        cases=tuple(cases),
        passed=all(case.passed for case in cases),
    )


def evaluate_legacy_control(
    path: Path,
    original: Image.Image,
    *,
    block_size: int,
    requested_geometry: str,
    thresholds: AnalysisThresholds,
    ssim_size: int,
    fft_size: int,
) -> LegacyControlSummary:
    control = ensure_image(path)
    geometry = resolve_geometry(original, control, requested_geometry)
    background = infer_background(control)
    metrics, regions, verdict = evaluate_candidate(
        control,
        original,
        block_size=block_size,
        geometry=geometry,
        background=background,
        thresholds=thresholds,
        ssim_size=ssim_size,
        fft_size=fft_size,
    )
    best = min(metrics, key=lambda item: item.mae)
    percentages = area_percentages(regions)
    return LegacyControlSummary(
        path=str(path),
        width=control.width,
        height=control.height,
        best_resampler=best.resampler,
        psnr_db=best.psnr_db,
        ssim=best.ssim,
        explainable_area_percent=percentages["resampler_explainable"],
        verdict=verdict.label,
    )


def save_comparison(original: Image.Image, generated: Image.Image, best_reference: Image.Image, out: Path) -> None:
    thumb_size = (640, 640)
    images = []
    for image in (original, generated, best_reference):
        copy = image.copy()
        copy.thumbnail(thumb_size, Image.Resampling.LANCZOS)
        canvas = Image.new("RGB", thumb_size, (0, 0, 0))
        x = (thumb_size[0] - copy.width) // 2
        y = (thumb_size[1] - copy.height) // 2
        canvas.paste(copy, (x, y))
        images.append(canvas)
    joined = Image.new("RGB", (thumb_size[0] * 3, thumb_size[1]))
    for index, image in enumerate(images):
        joined.paste(image, (index * thumb_size[0], 0))
    joined.save(out)


def expand_block_map(block_map: np.ndarray, target_size: tuple[int, int], block_size: int) -> np.ndarray:
    h, w = target_size[1], target_size[0]
    expanded = np.repeat(np.repeat(block_map, block_size, axis=0), block_size, axis=1)
    return expanded[:h, :w]


def save_classification_images(
    generated: Image.Image,
    class_map: np.ndarray,
    score_map: np.ndarray,
    block_size: int,
    out_dir: Path,
) -> None:
    width, height = generated.size
    expanded = expand_block_map(class_map, (width, height), block_size)
    grayscale = np.choose(expanded, [32, 160, 255]).astype(np.uint8)
    Image.fromarray(grayscale).save(out_dir / "08_classification_map.png")

    gen = np.asarray(generated)
    reconstructed_mask = expanded >= 1
    structural_mask = expanded >= 2
    detail_only = np.zeros_like(gen)
    detail_only[reconstructed_mask] = gen[reconstructed_mask]
    Image.fromarray(detail_only).save(out_dir / "09_non_resampler_detail_only.png")
    structural_only = np.zeros_like(gen)
    structural_only[structural_mask] = gen[structural_mask]
    Image.fromarray(structural_only).save(out_dir / "10_structural_deviation_only.png")

    score = expand_block_map(score_map, (width, height), block_size)
    plt.figure(figsize=(10, 8))
    plt.imshow(score)
    plt.colorbar(label="Lokaler Abweichungsscore")
    plt.title("Lokale Abweichung von getesteten Resamplern")
    plt.axis("off")
    plt.tight_layout()
    plt.savefig(out_dir / "07_resampler_explainability_heatmap.png", dpi=160)
    plt.close()


def save_difference_images(generated: Image.Image, reference: Image.Image, out_dir: Path) -> None:
    gen = np.asarray(generated).astype(np.int16)
    ref = np.asarray(reference).astype(np.int16)
    diff = np.abs(gen - ref).astype(np.uint8)
    amplified = np.clip(diff.astype(np.uint16) * 8, 0, 255).astype(np.uint8)
    Image.fromarray(diff).save(out_dir / "04_absolute_difference.png")
    Image.fromarray(amplified).save(out_dir / "05_difference_x8.png")

    gen_gray = rgb_to_gray(gen.astype(np.uint8))
    ref_gray = rgb_to_gray(ref.astype(np.uint8))
    edge_diff = np.abs(gradient_magnitude(gen_gray) - gradient_magnitude(ref_gray))
    edge_norm = np.clip(edge_diff / max(float(np.percentile(edge_diff, 99.5)), 1e-6) * 255.0, 0, 255).astype(np.uint8)
    Image.fromarray(edge_norm).save(out_dir / "06_edge_difference.png")


def save_plots(
    metrics: list[GlobalMetrics],
    generated: np.ndarray,
    original: Image.Image,
    target_size: tuple[int, int],
    geometry: str,
    background: tuple[int, int, int],
    fft_size: int,
    out_dir: Path,
    percentages: dict[str, float],
) -> None:
    names = [item.resampler for item in metrics]

    # Exakte Übereinstimmung ergibt mathematisch PSNR = +inf. Matplotlib kann
    # unendliche Balkenhöhen nicht sinnvoll transformieren und gibt sonst eine
    # RuntimeWarning aus. Für die reine Darstellung wird +inf daher auf eine
    # endliche Plot-Obergrenze abgebildet; die CSV-/JSON-Metrik bleibt +inf.
    finite_psnr = [item.psnr_db for item in metrics if math.isfinite(item.psnr_db)]
    psnr_cap = max(finite_psnr, default=60.0) + 6.0
    psnr_plot = [item.psnr_db if math.isfinite(item.psnr_db) else psnr_cap for item in metrics]
    plt.figure(figsize=(9, 5))
    bars = plt.bar(names, psnr_plot)
    for bar, metric in zip(bars, metrics, strict=True):
        if math.isinf(metric.psnr_db):
            plt.text(
                bar.get_x() + bar.get_width() / 2.0,
                bar.get_height(),
                "∞",
                ha="center",
                va="bottom",
            )
    plt.ylabel("PSNR [dB]")
    plt.title("TATARUS-Ausgabe gegen klassische Resampler")
    plt.tight_layout()
    plt.savefig(out_dir / "11_psnr_by_resampler.png", dpi=160)
    plt.close()

    plt.figure(figsize=(9, 5))
    plt.bar(names, [item.ssim for item in metrics])
    plt.ylabel("SSIM")
    plt.ylim(0.0, 1.0)
    plt.title("Strukturelle Ähnlichkeit zu klassischen Resamplern")
    plt.tight_layout()
    plt.savefig(out_dir / "12_ssim_by_resampler.png", dpi=160)
    plt.close()

    gx, gy, _ = spectrum_curve(generated, fft_size)
    plt.figure(figsize=(9, 5))
    plt.plot(gx, np.log1p(gy), label="generated")
    for name in RESAMPLERS:
        reference = np.asarray(resize_reference(
            original, target_size, name, geometry=geometry, background=background
        ))
        x, y, _ = spectrum_curve(reference, fft_size)
        plt.plot(x, np.log1p(y), label=name)
    plt.xlabel("Normierte räumliche Frequenz")
    plt.ylabel("log(1 + mittlere Spektralamplitude)")
    plt.title("Radiales Frequenzspektrum")
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_dir / "13_frequency_spectrum.png", dpi=160)
    plt.close()

    plt.figure(figsize=(9, 5))
    labels = ["Resampler-erklärbar", "Rekonstruiertes Detail", "Strukturelle Abweichung"]
    values = [
        percentages["resampler_explainable"],
        percentages["reconstructed_detail"],
        percentages["structural_deviation"],
    ]
    plt.bar(labels, values)
    plt.ylabel("Flächenanteil [%]")
    plt.ylim(0.0, 100.0)
    plt.title("Lokale Klassifikation der Zielausgabe")
    plt.xticks(rotation=15, ha="right")
    plt.tight_layout()
    plt.savefig(out_dir / "15_area_classification.png", dpi=160)
    plt.close()


def save_calibration_plot(calibration: CalibrationSummary, out_dir: Path) -> None:
    names = [case.name for case in calibration.cases]
    explainable = [case.explainable_area_percent for case in calibration.cases]
    plt.figure(figsize=(10, 5))
    plt.bar(names, explainable)
    plt.ylabel("Als Resampler erklärbare Zielfläche [%]")
    plt.ylim(0.0, 100.0)
    plt.title("Selbstkalibrierung des Resampler-Detektors")
    plt.xticks(rotation=18, ha="right")
    plt.tight_layout()
    plt.savefig(out_dir / "16_calibration_controls.png", dpi=160)
    plt.close()


def write_calibration_csv(calibration: CalibrationSummary, path: Path) -> None:
    fields = list(asdict(calibration.cases[0]).keys()) if calibration.cases else []
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for case in calibration.cases:
            writer.writerow(asdict(case))


def write_regions_csv(regions: list[RegionMetrics], path: Path) -> None:
    fields = list(asdict(regions[0]).keys()) if regions else []
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for region in regions:
            writer.writerow(asdict(region))


def write_global_csv(metrics: list[GlobalMetrics], path: Path) -> None:
    fields = list(asdict(metrics[0]).keys()) if metrics else []
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for metric in metrics:
            writer.writerow(asdict(metric))


def report_markdown(
    original_path: Path,
    generated_path: Path,
    original: Image.Image,
    generated: Image.Image,
    global_metrics: list[GlobalMetrics],
    reverse: ReverseConsistency,
    regions: list[RegionMetrics],
    verdict: Verdict,
    geometry: str,
    *,
    block_size: int,
    thresholds: AnalysisThresholds,
    calibration: CalibrationSummary | None,
    legacy_control: LegacyControlSummary | None,
    artifact: TatarusArtifactContext | None,
) -> str:
    percentages = area_percentages(regions)
    rows = "\n".join(
        f"| {m.resampler} | {m.mae:.3f} | {m.rmse:.3f} | {m.psnr_db:.2f} | "
        f"{m.ssim:.6f} | {m.correlation:.6f} | {m.edge_correlation:.6f} |"
        for m in global_metrics
    )
    top_regions = sorted(
        regions,
        key=lambda r: (r.classification == "structural_deviation", r.mae),
        reverse=True,
    )[:20]
    region_rows = "\n".join(
        f"| {r.index} | {r.x},{r.y} | {r.width}×{r.height} | {r.best_resampler} | "
        f"{r.mae:.2f} | {r.ssim:.4f} | {r.edge_correlation:.4f} | "
        f"{r.high_frequency_excess:.2f} | {r.classification} |"
        for r in top_regions
    )
    total_blocks = len(regions)
    class_counts = {
        key: sum(1 for region in regions if region.classification == key)
        for key in (
            "resampler_explainable",
            "reconstructed_detail",
            "structural_deviation",
        )
    }

    if calibration is None:
        calibration_section = """## Selbstkalibrierung

**ÜBERSPRUNGEN.** Die Analyse wurde mit `--skip-calibration` ausgeführt. Der Report dokumentiert
weiterhin die Messwerte, besitzt aber keinen eingebauten Kontrollnachweis für die aktuelle
Detektorkonfiguration.
"""
    else:
        calibration_rows = "\n".join(
            f"| {case.name} | {case.expected} | {case.explainable_area_percent:.2f} % | "
            f"{case.reconstructed_detail_area_percent:.2f} % | "
            f"{case.structural_deviation_area_percent:.2f} % | "
            f"{'PASS' if case.passed else 'FAIL'} |"
            for case in calibration.cases
        )
        calibration_section = f"""## Selbstkalibrierung

**Status:** {'PASS' if calibration.passed else 'FAIL'}  
**Proxy-Auflösung:** {calibration.proxy_width}×{calibration.proxy_height}

Vor dem Haupturteil prüft sich der Detektor selbst. Vier bekannte, direkt aus dem Original erzeugte
Kontrollen (Nearest, Bilinear, Bicubic, Lanczos) müssen als klassische Rastervergrößerung erkannt
werden. Zusätzlich muss eine absichtlich mit deterministischer Feinstruktur veränderte Kontrollprobe
als Nicht-Resampler-Abweichung erkannt werden. Diese fünfte Probe simuliert **nicht** TATARUS; sie ist
nur ein Sensitivitätstest gegen einen zu toleranten Detektor.

| Kontrolle | Erwartung | Resampler-erklärbar | Rekonstr. Detail | Strukt. Abweichung | Ergebnis |
|---|---|---:|---:|---:|---|
{calibration_rows}

Ein Haupturteil wird nur als belastbar markiert, wenn alle Kontrollproben bestanden wurden.
"""

    legacy_section = ""
    if legacy_control is not None:
        legacy_section = f"""## Optionale Legacy-/Clone-Kontrollprobe

**Datei:** `{Path(legacy_control.path).name}` — {legacy_control.width}×{legacy_control.height}  
**Bester Resampler:** {legacy_control.best_resampler}  
**PSNR:** {legacy_control.psnr_db:.2f} dB  
**SSIM:** {legacy_control.ssim:.6f}  
**Resampler-erklärbare Zielfläche:** {legacy_control.explainable_area_percent:.2f} %  
**Urteil:** **{legacy_control.verdict}**

Diese Probe ist besonders wertvoll, wenn sie aus einem bekannten älteren Clone-/Interpolationspfad
stammt. Sie zeigt dann unmittelbar, ob dieselbe Analyse den alten Pfad anders klassifiziert als die
aktuelle Ausgabe.
"""

    artifact_section = ""
    if artifact is not None:
        artifact_section = f"""## TATARUS-V14-Artefaktkontext

**Artefakt-ID:** `{artifact.artifact_id}`  
**Titel:** `{artifact.title}`  
**Stufe / Operation:** `{artifact.stage}` / `{artifact.operation}`  
**Renderverfahren:** `{artifact.render_method}`  
**Motortrace im Zielraum ausgeführt:** {'JA' if artifact.motor_trace_replayed else 'NEIN'}  
**Direkt auf Zielleinwand ausgeführt:** {'JA' if artifact.executed_directly_on_target_canvas else 'NEIN'}  
**Rasterinterpolation laut Artefaktmetadaten:** {'JA' if artifact.raster_interpolation_used else 'NEIN'}  
**Synthetisierte Hochfrequenzdetails:** {'JA' if artifact.synthesized_high_frequency_detail else 'NEIN'}  
**Referenz beim Recall sichtbar:** {'JA' if artifact.reference_visible else 'NEIN'}  
**Recalled Engrams:** {', '.join(str(value) for value in artifact.recalled_engrams) or 'keine'}  
**Aktive Assembly:** {artifact.active_assembly}

`detail_truth`: `{artifact.detail_truth}`

Diese Angaben stammen aus der TATARUS-Artefaktmetadatei. Die Bildforensik darunter ist davon
unabhängig und prüft ausschließlich, wie gut die erzeugten Pixel durch klassische Raster-Resampler
des Originalbildes erklärbar sind.
"""

    return f"""# TATARUS Image Forensic Report — v3 / V14

**Erzeugt:** {datetime.now(timezone.utc).isoformat()}  
**Original:** `{original_path.name}` — {original.width}×{original.height}  
**Ausgabe:** `{generated_path.name}` — {generated.width}×{generated.height}  
**Referenzgeometrie:** `{geometry}`  
**Analyseblock:** {block_size}×{block_size} Zielpixel  
**SHA-256 Original:** `{sha256_file(original_path)}`  
**SHA-256 Ausgabe:** `{sha256_file(generated_path)}`

{artifact_section}

## Gesamturteil

> **{verdict.label}**

{verdict.interpretation}

**Kalibrierung:** {'PASS' if verdict.calibration_passed else 'NICHT BESTANDEN / ÜBERSPRUNGEN'}  
**Urteil belastbar:** {'JA' if verdict.conclusive else 'NEIN'}

### Flächenklassifikation — präzise Definition

Die Prozentwerte beziehen sich auf die **Zielfläche**, gewichtet nach der Fläche der untersuchten
{block_size}×{block_size}-Blöcke. Ein Block gilt nur dann als `resampler_explainable`, wenn er gegen
mindestens eine der vier klassischen Referenzen **alle** Explainability-Schwellen erfüllt.

- Zielfläche in Blöcken, die alle Resampler-Erklärbarkeitsschwellen erfüllen: **{percentages['resampler_explainable']:.2f} %** ({class_counts['resampler_explainable']}/{total_blocks} Blöcke)
- Zielfläche mit zusätzlicher rekonstruierter Zielraumstruktur: **{percentages['reconstructed_detail']:.2f} %** ({class_counts['reconstructed_detail']}/{total_blocks} Blöcke)
- Zielfläche mit stärkerer struktureller Abweichung: **{percentages['structural_deviation']:.2f} %** ({class_counts['structural_deviation']}/{total_blocks} Blöcke)
- Verhältnis Hochfrequenzenergie Ausgabe / bester Resampler: **{verdict.generated_high_frequency_ratio:.3f}×**

**Wichtig:** `0,00 % resampler_explainable` bedeutet nicht „0 % Bildähnlichkeit“. Es bedeutet exakt:
Keiner der untersuchten Blöcke erfüllte gleichzeitig alle unten dokumentierten lokalen Schwellen.
Ein global hoher SSIM kann gleichzeitig bestehen, weil globale Motiv-/Geometrieähnlichkeit und lokale
vollständige Erklärbarkeit durch einen Resampler zwei verschiedene Fragestellungen sind.

### Dokumentierte Schwellen

| Kriterium | Schwelle |
|---|---:|
| Lokaler MAE für `resampler_explainable` | ≤ {thresholds.explainable_mae_max:.3f} |
| Lokaler SSIM für `resampler_explainable` | ≥ {thresholds.explainable_ssim_min:.6f} |
| Lokaler HF-Exzess für `resampler_explainable` | ≤ {thresholds.explainable_hf_excess_max:.3f} |
| MAE für `structural_deviation` | > {thresholds.structural_mae_min:.3f} |
| SSIM für `structural_deviation` | < {thresholds.structural_ssim_max:.6f} |
| Clone-Verdacht: globaler PSNR | ≥ {thresholds.clone_psnr_min:.2f} dB |
| Clone-Verdacht: globaler SSIM | ≥ {thresholds.clone_ssim_min:.6f} |
| Clone-Verdacht: erklärbare Zielfläche | ≥ {thresholds.clone_explainable_area_min:.2f} % |
| Clone-Verdacht: HF-Verhältnis | ≤ {thresholds.clone_hf_ratio_max:.3f}× |

{calibration_section}

## Globale Gegenprobe

| Referenz | MAE | RMSE | PSNR dB | SSIM | Korrelation | Kantenkorrelation |
|---|---:|---:|---:|---:|---:|---:|
{rows}

Der global ähnlichste klassische Resampler nach MAE ist **{verdict.best_resampler}**.

## Rückprojektion auf die Originalauflösung

Die Ausgabe wurde mit Bicubic wieder auf {original.width}×{original.height} zurückgeführt. Dieser Test
prüft nicht die Erzeugungsmethode, sondern ob die makroskopische Bildidentität trotz zusätzlicher
Zielraumstruktur erhalten bleibt.

- MAE: **{reverse.mae:.3f}**
- RMSE: **{reverse.rmse:.3f}**
- PSNR: **{reverse.psnr_db:.2f} dB**
- Korrelation: **{reverse.correlation:.6f}**
- SSIM: **{reverse.ssim:.6f}**

## Interpretation der drei Klassen

### `resampler_explainable`
Der lokale Block erfüllt gegen mindestens einen getesteten Resampler gleichzeitig die dokumentierten
MAE-, SSIM- und Hochfrequenz-Schwellen. Für diesen Block ist eine klassische Rastervergrößerung eine
hinreichende lokale Erklärung.

### `reconstructed_detail`
Die Grundgeometrie bleibt eng mit dem Original verbunden, aber lokale Textur-, Frequenz- oder
Pigmentinformation erfüllt die Resampler-Erklärbarkeitsschwellen nicht. Hier ist zusätzliche
Zielraumrekonstruktion messbar.

### `structural_deviation`
Der Block weicht zusätzlich stärker in Struktur bzw. Kantenverlauf von allen getesteten Resamplern ab.
Diese Klasse ist ein Hinweis auf neue oder veränderte Zielraumstruktur, aber **kein automatischer
Beweis für ein neues semantisches Objekt**.

{legacy_section}

## Stärkste lokale Abweichungen

| Block | Position | Größe | bester Resampler | MAE | SSIM | Kantenkorr. | HF-Exzess | Klasse |
|---:|---|---|---|---:|---:|---:|---:|---|
{region_rows}

## Erzeugte Beweisbilder

- `01_original.png` — Original
- `02_generated.png` — untersuchte Ausgabe
- `03_best_resampler_reference.png` — global beste klassische Upscale-Referenz
- `04_absolute_difference.png` — absolute RGB-Differenz
- `05_difference_x8.png` — achtfach verstärkte Differenz
- `06_edge_difference.png` — Kantenabweichung
- `07_resampler_explainability_heatmap.png` — lokale Abweichung von klassischen Resamplern
- `08_classification_map.png` — 3-Klassen-Karte
- `09_non_resampler_detail_only.png` — Bereiche mit zusätzlicher/abweichender Zielraumstruktur
- `10_structural_deviation_only.png` — nur starke strukturelle Abweichungen
- `11_psnr_by_resampler.png` — PSNR-Vergleich
- `12_ssim_by_resampler.png` — SSIM-Vergleich
- `13_frequency_spectrum.png` — Frequenzspektren
- `14_side_by_side.png` — Original / Ausgabe / beste Resize-Referenz
- `15_area_classification.png` — Flächenanteile der lokalen Klassen
- `16_calibration_controls.png` — Ergebnis der eingebauten Kontrollproben

## Methodische Grenze

Dieser Bericht prüft, ob die Ausgabe durch die klassischen Rasterverfahren **Nearest, Bilinear,
Bicubic und Lanczos** unter der gewählten Geometrie und den dokumentierten Schwellen erklärbar ist.
Die Selbstkalibrierung zeigt zusätzlich, dass die konkrete Detektorkonfiguration bekannte Resampler
als solche erkennt und absichtlich hinzugefügte Zielraum-Feinstruktur nicht einfach durchwinkt.

Der Bericht kann allein aus zwei Bildern trotzdem **nicht** beweisen,

- dass ein bestimmtes Detail semantisch vollständig neu erfunden wurde,
- dass die erzeugte Feinstruktur der unbekannten hochauflösenden Originalwahrheit entspricht,
- oder dass kein beliebiger anderer, nicht getesteter Bildalgorithmus dasselbe Resultat erzeugen könnte.

Für TATARUS ist die methodisch saubere Formulierung:

> **Die als rekonstruiert oder strukturell abweichend markierten Bereiche enthalten Zielraumstruktur,
> die unter den dokumentierten Schwellen in keiner der getesteten klassischen Rastervergrößerungen des
> Originalbildes vorhanden ist.**
"""


def report_html(markdown_text: str) -> str:
    """Kleine, dependency-freie HTML-Fassung des Berichts."""
    escaped = (
        markdown_text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )
    return f"""<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TATARUS Image Forensic Report</title>
<style>
body {{ max-width: 1200px; margin: 2rem auto; padding: 0 1rem; font-family: system-ui, sans-serif; line-height: 1.55; }}
pre {{ white-space: pre-wrap; word-break: break-word; }}
.gallery {{ display: grid; grid-template-columns: repeat(auto-fit,minmax(260px,1fr)); gap: 1rem; margin-top: 2rem; }}
.gallery figure {{ margin: 0; }}
.gallery img {{ width: 100%; height: auto; border: 1px solid #8884; }}
figcaption {{ font-size: .9rem; margin-top: .35rem; }}
</style>
</head>
<body>
<pre>{escaped}</pre>
<section class="gallery">
{''.join(f'<figure><img src="{name}"><figcaption>{name}</figcaption></figure>' for name in [
    '14_side_by_side.png', '04_absolute_difference.png', '06_edge_difference.png',
    '07_resampler_explainability_heatmap.png', '08_classification_map.png',
    '09_non_resampler_detail_only.png', '10_structural_deviation_only.png',
    '11_psnr_by_resampler.png', '12_ssim_by_resampler.png', '13_frequency_spectrum.png', '15_area_classification.png', '16_calibration_controls.png'
])}
</section>
</body>
</html>
"""


def main() -> int:
    args = parse_args()
    if args.block < 16:
        raise SystemExit("--block muss mindestens 16 sein")
    if args.calibration_size < 128:
        raise SystemExit("--calibration-size muss mindestens 128 sein")

    original_path, generated_path, artifact, out_dir = resolve_inputs(args)
    thresholds = AnalysisThresholds()
    out_dir.mkdir(parents=True, exist_ok=True)

    print("TATARUS Image Forensic Analyzer v3 / IMAGINATIO V14")
    if artifact is not None:
        print(f"Artefakt : {artifact.artifact_id} ({artifact.title})")
    print(f"Original : {original_path}")
    print(f"Ausgabe  : {generated_path}")
    print(f"Bericht  : {out_dir}")

    original = ensure_image(original_path)
    generated = ensure_image(generated_path)
    original.save(out_dir / "01_original.png")
    generated.save(out_dir / "02_generated.png")

    generated_array = np.asarray(generated)
    global_metrics_list: list[GlobalMetrics] = []
    geometry = resolve_geometry(original, generated, args.geometry)
    background = infer_background(generated)

    print(f"Original: {original.size[0]}x{original.size[1]}")
    print(f"Ausgabe : {generated.size[0]}x{generated.size[1]}")
    print(f"Geometrie: {geometry}; Hintergrundreferenz: {background}")
    print("Erzeuge klassische Resampler-Gegenproben ...")

    for name in RESAMPLERS:
        reference = resize_reference(
            original, generated.size, name, geometry=geometry, background=background
        )
        reference_array = np.asarray(reference)
        metric = compute_global_metrics(
            generated_array, reference_array, name, args.ssim_size, args.fft_size
        )
        global_metrics_list.append(metric)
        print(
            f"  {name:8s} MAE={metric.mae:.3f} PSNR={metric.psnr_db:.2f} dB "
            f"SSIM={metric.ssim:.6f}"
        )

    best_metric = min(global_metrics_list, key=lambda item: item.mae)
    best_reference = resize_reference(
        original, generated.size, best_metric.resampler, geometry=geometry, background=background
    )
    best_reference.save(out_dir / "03_best_resampler_reference.png")

    print("Lokale Blockanalyse ...")
    regions, class_map, best_map, score_map = analyze_regions(
        generated_array,
        original,
        generated.size,
        args.block,
        geometry=geometry,
        background=background,
        thresholds=thresholds,
    )

    calibration: CalibrationSummary | None
    if args.skip_calibration:
        calibration = None
        calibration_passed = False
        print("Selbstkalibrierung: ÜBERSPRUNGEN")
    else:
        print("Selbstkalibrierung mit bekannten Kontrollproben ...")
        calibration = run_self_calibration(
            original,
            generated.size,
            block_size=args.block,
            geometry=geometry,
            background=background,
            thresholds=thresholds,
            maximum_dimension=args.calibration_size,
        )
        calibration_passed = calibration.passed
        for case in calibration.cases:
            print(
                f"  {case.name:28s} {'PASS' if case.passed else 'FAIL'} "
                f"explainable={case.explainable_area_percent:6.2f}% "
                f"verdict={case.verdict}"
            )
        print(f"Selbstkalibrierung: {'PASS' if calibration_passed else 'FAIL'}")

    legacy_control: LegacyControlSummary | None = None
    if args.legacy_control is not None:
        print(f"Analysiere Legacy-/Clone-Kontrollprobe: {args.legacy_control}")
        legacy_control = evaluate_legacy_control(
            args.legacy_control,
            original,
            block_size=args.block,
            requested_geometry=args.geometry,
            thresholds=thresholds,
            ssim_size=args.ssim_size,
            fft_size=args.fft_size,
        )
        print(
            f"  Legacy: {legacy_control.verdict}; "
            f"explainable={legacy_control.explainable_area_percent:.2f}%"
        )

    reverse = reverse_consistency(generated, original, args.ssim_size)
    verdict = make_verdict(
        global_metrics_list,
        regions,
        thresholds,
        calibration_passed=calibration_passed,
    )

    save_difference_images(generated, best_reference, out_dir)
    if not args.no_local_images:
        save_classification_images(generated, class_map, score_map, args.block, out_dir)
    percentages = area_percentages(regions)
    save_plots(
        global_metrics_list,
        generated_array,
        original,
        generated.size,
        geometry,
        background,
        args.fft_size,
        out_dir,
        percentages,
    )
    save_comparison(original, generated, best_reference, out_dir / "14_side_by_side.png")

    write_regions_csv(regions, out_dir / "regions.csv")
    write_global_csv(global_metrics_list, out_dir / "global_metrics.csv")
    if calibration is not None:
        save_calibration_plot(calibration, out_dir)
        write_calibration_csv(calibration, out_dir / "calibration.csv")

    result = {
        "schema": "tatarus-image-forensic-analysis-v3",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "inputs": {
            "original": str(original_path),
            "generated": str(generated_path),
            "original_size": list(original.size),
            "generated_size": list(generated.size),
            "original_sha256": sha256_file(original_path),
            "generated_sha256": sha256_file(generated_path),
        },
        "tatarus_artifact": None if artifact is None else asdict(artifact),
        "parameters": {
            "block_size": args.block,
            "fft_size": args.fft_size,
            "ssim_size": args.ssim_size,
            "resamplers": list(RESAMPLERS),
            "geometry": geometry,
            "background_rgb": list(background),
            "thresholds": asdict(thresholds),
            "self_calibration_enabled": not args.skip_calibration,
            "calibration_maximum_dimension": args.calibration_size,
        },
        "calibration": None if calibration is None else asdict(calibration),
        "legacy_control": None if legacy_control is None else asdict(legacy_control),
        "global_metrics": [asdict(item) for item in global_metrics_list],
        "reverse_consistency": asdict(reverse),
        "area_percentages": area_percentages(regions),
        "verdict": asdict(verdict),
        "regions": [asdict(item) for item in regions],
    }
    (out_dir / "analysis.json").write_text(
        json.dumps(json_compatible(result), ensure_ascii=False, indent=2, allow_nan=False),
        encoding="utf-8",
    )

    if legacy_control is not None:
        (out_dir / "legacy_control.json").write_text(
            json.dumps(
                json_compatible(asdict(legacy_control)),
                ensure_ascii=False,
                indent=2,
                allow_nan=False,
            ),
            encoding="utf-8",
        )

    markdown = report_markdown(
        original_path,
        generated_path,
        original,
        generated,
        global_metrics_list,
        reverse,
        regions,
        verdict,
        geometry,
        block_size=args.block,
        thresholds=thresholds,
        calibration=calibration,
        legacy_control=legacy_control,
        artifact=artifact,
    )
    (out_dir / "REPORT.md").write_text(markdown, encoding="utf-8")
    (out_dir / "REPORT.html").write_text(report_html(markdown), encoding="utf-8")

    print()
    print(verdict.label)
    print(verdict.interpretation)
    print(f"Bericht: {out_dir / 'REPORT.md'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nAbgebrochen.", file=sys.stderr)
        raise SystemExit(130)
    except Exception as exc:
        print(f"FEHLER: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc

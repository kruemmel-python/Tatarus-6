#!/usr/bin/env python3
"""Categorize the local 512px corpus, teach it to TATARUS, and verify a snapshot."""

from __future__ import annotations

import argparse
import csv
import ctypes
import hashlib
import json
import shutil
import sys
import time
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw


SCHEMA = "tatarus-512-corpus-v1"
SEED = 7411
EXPECTED_SIDE = 512

# The corpus was reviewed as numbered contact sheets. Long contiguous runs are
# deliberate prompt/theme series; the overrides split conspicuous recurring
# subjects out of otherwise mixed runs. Every assignment is deterministic and
# auditable in catalog.json/catalog.csv.
RANGE_CATEGORIES: tuple[tuple[int, int, str], ...] = (
    (1, 83, "DUNKLE_FANTASY_PORTRAETS"),
    (84, 190, "GOTISCHE_BEZIEHUNGSSZENE"),
    (191, 220, "GEFLUEGELTER_DUNKLER_WAECHTER"),
    (221, 294, "MYTHOLOGIE_UND_FANTASYKAMPF"),
    (295, 400, "DUNKLE_FANTASY_SZENE"),
    (401, 430, "DAEMONISCHES_RITUAL_UND_NACHTLEBEN"),
    (431, 482, "WOLF_UND_WALD_BEI_MONDLICHT"),
    (483, 510, "DUNKLE_FANTASY_PORTRAETS"),
    (511, 545, "KOSMISCHER_SURREALISMUS"),
    (546, 565, "MENSCH_UND_TIER_PORTRAET"),
    (566, 641, "MENSCH_IM_FEUER"),
    (642, 654, "UEBERNATUERLICHER_FEUERKOERPER"),
    (655, 680, "FEUERWESEN_UND_FEUERPFERD"),
    (681, 726, "FEUERMUSIKER_UND_PERFORMANCE"),
)


def _numbers(spec: str) -> set[int]:
    result: set[int] = set()
    for part in spec.replace(" ", "").split(","):
        if not part:
            continue
        if "-" in part:
            start, end = (int(value) for value in part.split("-", 1))
            result.update(range(start, end + 1))
        else:
            result.add(int(part))
    return result


CATEGORY_OVERRIDES: dict[str, set[int]] = {
    "TIERPORTRAET": _numbers(
        "5,10,13,15,18,21,37,42-43,48-49,57,73-74,80,82-83,"
        "107,109-110,112-113,122-124,129,134,140,146,149-150,"
        "152-155,170,173,179,182,192,217,222-224,238-239,282,"
        "293,297,318-319,321-322,344,379,385,403,415,429,"
        "437-441,443-447,449-452,454,456-462,467,469-470,"
        "472,474-476,478-483,485,488-490,493,495,497,503-504,"
        "507-508,520-521,528,533,537,543,546-551,635-636,644-648"
    ),
    "MENSCHENPORTRAET": _numbers(
        "3,8-9,19,26-27,30,35,40-41,50,52,60,65-66,69-71,75-78,"
        "96,158-169,180,184-186,191,203,225,249,260,263,307,"
        "309,311,326,338,350,355-356,374,384,425,465,484,486,"
        "496,500-502,505,510,552-565"
    ),
    "SCIENCE_FICTION_UND_TECHNIK": _numbers(
        "4,14,17,22-24,28-29,36,39,41,53,60,78-79,490-492,"
        "498-499,506,509,511-519,522-524,526-527,532,534-535,540,542"
    ),
    "ABSTRAKT_UND_SYMBOLISCH": _numbers(
        "11,20,25,45,61,71,77,96,214,217,228-230,250,253-254,"
        "269-270,272,274-276,283-286,296,304,313-314,331,336,"
        "339,348,354,366-367,369,371,383,388,468,477,512,514-516,"
        "525,529,531-532,536,538-545"
    ),
    "NATUR_UND_OEKO_SCIFI": _numbers("23,37,39,50,53,204-205,337,410,432,508-509,516,523,526,537"),
    "MOND_UND_NACHTSZENE": _numbers(
        "102-106,108-110,112-113,121-124,134,146,149,152-157,"
        "170-183,193-200,211-215,217,223-226,250,253-254,263-268,"
        "296-306,313,318-324,331,339,344,348,351-354,366,369-372,"
        "378-379,383,403,415,429-430"
    ),
    "OKKULTES_STILLLEBEN_UND_RITUAL": _numbers(
        "227-235,258,295-306,313-314,327-328,335,345-347,357,"
        "367,369,371,380,390,405,408,413-414,416-421,427,430-436"
    ),
    "FANTASY_ILLUSTRATION_UND_ANIME": _numbers(
        "44-46,51-54,62-63,68-69,125-145,147-148,219-220,"
        "236,240-248,251-252,261-262,264-265,269-281,283-294,"
        "312,316,332-334,340-341,349,353,358-364,368,373,"
        "381-382,386-393"
    ),
    "MUSIK_UND_PERFORMANCE": _numbers("1,55-56,81,294-295,298-299,346,357,395,397-401,405,408,420-421,424,427"),
    "ACTION_HELDEN_UND_FAHRZEUGE": _numbers("14,27-29,38-39,41,46,53,60,63,78-81,161,271,281,300-301,337,364,387,389,391-393,432,442,448,453,468,489-492,498-499,506,509,534-535,540"),
    "DUNKLE_FANTASYFIGUR_UND_KREATUR": _numbers(
        "2,6-7,11-12,16-17,20,24-25,31-34,36,44-47,54,58-59,"
        "62-64,67-68,72,87,89-95,97,117-121,125-128,135-145,"
        "147-148,175-179,183,187-190,216,218-227,237-239,257-260,"
        "266-268,315-317,320,324-325,330,342-343,351-353,359-360,"
        "370,372,375-377,380-382,388,394,396-397,402,406-409,"
        "255-256,308,310,411-414,417-419,422-428,455,463-466,471,473,477,480,"
        "487,494,502,505,520-521,528-531,533,538-545"
    ),
    "GOTISCHE_BEZIEHUNGSSZENE": _numbers("84-106,111,114-116,151,308-310,325-330,365,396,399,404"),
}


def category_for(number: int) -> str:
    category = "NICHT_ZUGEORDNET"
    for start, end, candidate in RANGE_CATEGORIES:
        if start <= number <= end:
            category = candidate
            break
    for candidate, values in CATEGORY_OVERRIDES.items():
        if number in values:
            category = candidate
    return category


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, payload: Any) -> None:
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def discover(input_directory: Path) -> list[tuple[int, Path]]:
    numbered: list[tuple[int, Path]] = []
    for path in input_directory.glob("Tatarus*.png"):
        suffix = path.stem.removeprefix("Tatarus")
        if suffix.isdigit():
            numbered.append((int(suffix), path.resolve()))
    numbered.sort()
    if not numbered:
        raise RuntimeError(f"Keine Tatarus*.png-Dateien in {input_directory}")
    expected = list(range(1, numbered[-1][0] + 1))
    actual = [number for number, _ in numbered]
    if actual != expected:
        missing = sorted(set(expected) - set(actual))
        raise RuntimeError(f"Bildnummern sind nicht lückenlos; fehlen: {missing[:20]}")
    return numbered


def build_catalog(numbered: list[tuple[int, Path]]) -> tuple[list[dict[str, Any]], dict[str, int]]:
    records: list[dict[str, Any]] = []
    first_by_hash: dict[str, str] = {}
    counts: Counter[str] = Counter()
    for number, path in numbered:
        with Image.open(path) as image:
            if image.size != (EXPECTED_SIDE, EXPECTED_SIDE):
                raise RuntimeError(f"{path.name}: erwartet 512x512, gefunden {image.size}")
            if image.mode != "RGB":
                image.convert("RGB")
        digest = sha256(path)
        category = category_for(number)
        counts[category] += 1
        duplicate_of = first_by_hash.get(digest)
        first_by_hash.setdefault(digest, path.name)
        records.append(
            {
                "number": number,
                "file": path.name,
                "sha256": digest,
                "width": EXPECTED_SIDE,
                "height": EXPECTED_SIDE,
                "category": category,
                "concept": f"TATARUS_{number:04d}",
                "duplicate_of": duplicate_of,
            }
        )
    return records, dict(sorted(counts.items()))


def write_catalog(bundle: Path, records: list[dict[str, Any]], counts: dict[str, int]) -> None:
    write_json(
        bundle / "catalog.json",
        {"schema": SCHEMA, "images": len(records), "category_counts": counts, "records": records},
    )
    with (bundle / "catalog.csv").open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)


def make_category_sheets(
    bundle: Path, input_directory: Path, records: list[dict[str, Any]]
) -> None:
    destination = bundle / "category_overview"
    destination.mkdir(parents=True, exist_ok=True)
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        grouped[record["category"]].append(record)
    thumb, label_height, columns = 96, 18, 10
    for category, items in sorted(grouped.items()):
        rows = (len(items) + columns - 1) // columns
        sheet = Image.new("RGB", (columns * thumb, rows * (thumb + label_height)), (14, 16, 20))
        draw = ImageDraw.Draw(sheet)
        for index, record in enumerate(items):
            with Image.open(input_directory / record["file"]) as source:
                tile = source.convert("RGB")
                tile.thumbnail((thumb, thumb), Image.Resampling.LANCZOS)
            x = index % columns * thumb
            y = index // columns * (thumb + label_height)
            sheet.paste(tile, (x + (thumb - tile.width) // 2, y))
            draw.text((x + 3, y + thumb + 2), str(record["number"]), fill=(245, 245, 245))
        sheet.save(destination / f"{category}.jpg", quality=91)


class TatarusLibrary:
    def __init__(self, library_path: Path) -> None:
        self.library = ctypes.CDLL(str(library_path))
        rgb8_pointer = ctypes.POINTER(ctypes.c_uint8)
        self.library.tatarus_organism_create_sized.argtypes = [ctypes.c_uint64, ctypes.c_uint32]
        self.library.tatarus_organism_create_sized.restype = ctypes.c_void_p
        self.library.tatarus_organism_destroy.argtypes = [ctypes.c_void_p]
        self.library.tatarus_organism_destroy.restype = None
        self.library.tatarus_organism_imaginatio_ingest_category_rgb8.argtypes = [
            ctypes.c_void_p,
            rgb8_pointer,
            ctypes.c_uint64,
            ctypes.c_uint64,
            ctypes.c_uint64,
            ctypes.c_uint64,
            ctypes.c_char_p,
            ctypes.c_char_p,
        ]
        self.library.tatarus_organism_imaginatio_ingest_category_rgb8.restype = ctypes.c_int
        self.library.tatarus_organism_imaginatio_get_json.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_uint64,
        ]
        self.library.tatarus_organism_imaginatio_get_json.restype = ctypes.c_uint64
        self.library.tatarus_organism_save.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self.library.tatarus_organism_save.restype = ctypes.c_int
        self.library.tatarus_organism_load.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self.library.tatarus_organism_load.restype = ctypes.c_int
        self.library.tatarus_last_error.argtypes = []
        self.library.tatarus_last_error.restype = ctypes.c_char_p

    def error(self) -> str:
        raw = self.library.tatarus_last_error()
        return raw.decode("utf-8", errors="replace") if raw else "Unbekannter Tatarus-Fehler"

    def create(self) -> ctypes.c_void_p:
        handle = self.library.tatarus_organism_create_sized(SEED, 384)
        if not handle:
            raise RuntimeError(self.error())
        return ctypes.c_void_p(handle)

    def destroy(self, handle: ctypes.c_void_p | None) -> None:
        if handle:
            self.library.tatarus_organism_destroy(handle)

    def ingest(self, handle: ctypes.c_void_p, pixels: bytes, concept: str, category: str) -> None:
        buffer = (ctypes.c_uint8 * len(pixels)).from_buffer_copy(pixels)
        ok = self.library.tatarus_organism_imaginatio_ingest_category_rgb8(
            handle,
            buffer,
            len(pixels),
            EXPECTED_SIDE,
            EXPECTED_SIDE,
            8,
            concept.encode("utf-8"),
            category.encode("utf-8"),
        )
        if not ok:
            raise RuntimeError(self.error())

    def state(self, handle: ctypes.c_void_p) -> dict[str, Any]:
        required = int(self.library.tatarus_organism_imaginatio_get_json(handle, None, 0))
        if required <= 1:
            raise RuntimeError(self.error())
        buffer = ctypes.create_string_buffer(required)
        returned = int(
            self.library.tatarus_organism_imaginatio_get_json(handle, buffer, required)
        )
        if returned != required:
            raise RuntimeError(self.error())
        return json.loads(bytes(buffer.raw[: required - 1]))

    def save(self, handle: ctypes.c_void_p, snapshot: Path) -> None:
        if not self.library.tatarus_organism_save(handle, str(snapshot).encode("utf-8")):
            raise RuntimeError(self.error())

    def load(self, handle: ctypes.c_void_p, snapshot: Path) -> None:
        if not self.library.tatarus_organism_load(handle, str(snapshot).encode("utf-8")):
            raise RuntimeError(self.error())


def state_summary(state: dict[str, Any]) -> dict[str, Any]:
    visual = state.get("visual_engrams", [])
    categories = state.get("category_engrams", [])
    return {
        "schema": state.get("schema"),
        "snapshot_format_version": int(state.get("snapshot_format_version", 0)),
        "visual_memory_policy": state.get("visual_memory_policy", {}),
        "visual_engrams": len(visual),
        "visual_observations": sum(int(item.get("observations", 0)) for item in visual),
        "self_motor_engrams": sum(
            1 for item in visual if bool(item.get("self_motor_memory_valid", False))
        ),
        "self_motor_strokes": sum(int(item.get("self_motor_strokes", 0)) for item in visual),
        "self_consolidations": sum(int(item.get("self_consolidations", 0)) for item in visual),
        "categories": len(categories),
        "category_examples": {
            item.get("category", ""): int(item.get("examples", 0)) for item in categories
        },
        "object_engrams": len(state.get("object_engrams", [])),
        "organism_experiences": int(state.get("organism", {}).get("experiences", 0)),
        "maximum_engrams": int(state.get("analysis", {}).get("maximum_engrams", 0)),
    }


def verify_v14_runtime(summary: dict[str, Any]) -> None:
    """Bricht sofort ab, wenn versehentlich eine alte DLL geladen wurde."""
    if summary["schema"] != "tatarus-imaginatio-v14":
        raise RuntimeError(
            "Falsche TATARUS-Bibliothek geladen: "
            f"Schema {summary['schema']!r}, erwartet 'tatarus-imaginatio-v14'"
        )
    if summary["snapshot_format_version"] != 14:
        raise RuntimeError(
            "Falsche Snapshot-Version der geladenen Bibliothek: "
            f"{summary['snapshot_format_version']} (erwartet 14)"
        )

    policy = summary.get("visual_memory_policy", {})
    required_policy = {
        "source_raster_retained": False,
        "teacher_trace_retained": False,
        "working_canvas_persisted": False,
        "self_motor_trace_retained": True,
    }
    for key, expected in required_policy.items():
        actual = policy.get(key)
        if actual is not expected:
            raise RuntimeError(
                f"V14-Invariante verletzt: {key}={actual!r}, erwartet {expected!r}"
            )


def verify_summary(
    summary: dict[str, Any],
    records: list[dict[str, Any]],
    counts: dict[str, int],
) -> None:
    verify_v14_runtime(summary)
    unique_images = len({record["sha256"] for record in records})
    expected_categories = counts

    if summary["visual_engrams"] != unique_images:
        raise RuntimeError(
            f"Engrammzahl {summary['visual_engrams']} stimmt nicht mit {unique_images} unikalen Bildern überein"
        )
    if summary["visual_observations"] != len(records):
        raise RuntimeError(
            f"Beobachtungszahl {summary['visual_observations']} stimmt nicht mit {len(records)} Bildern überein"
        )
    if summary["self_motor_engrams"] != unique_images:
        raise RuntimeError(
            "Self-Motor-Engrammzahl "
            f"{summary['self_motor_engrams']} stimmt nicht mit {unique_images} unikalen Bildern überein"
        )
    if summary["self_consolidations"] < len(records):
        raise RuntimeError(
            "Nicht alle Bildbeobachtungen wurden als V14-Self-Imprint konsolidiert: "
            f"{summary['self_consolidations']} < {len(records)}"
        )
    if summary["category_examples"] != expected_categories:
        raise RuntimeError("Kategoriebeispiele im Tatarus-Zustand stimmen nicht mit dem Katalog überein")
    if summary["maximum_engrams"] < unique_images:
        raise RuntimeError("Tatarus-Speicherkapazität ist kleiner als der gelernte Bildbestand")


def checksums(bundle: Path) -> None:
    paths = [path for path in bundle.rglob("*") if path.is_file() and path.name != "CHECKSUMS.sha256"]
    lines = [f"{sha256(path)}  {path.relative_to(bundle).as_posix()}" for path in sorted(paths)]
    (bundle / "CHECKSUMS.sha256").write_text("\n".join(lines) + "\n", encoding="ascii")


def resolve_library_path(root: Path, requested: Path | None) -> Path:
    """Findet die zu BUILD_WINDOWS.bat passende TATARUS-C-DLL.

    V14 baut standardmäßig nach ``build/Release``. Ältere Spezial-Builds werden
    nur als Fallback akzeptiert. Ein explizites ``--library`` hat immer Vorrang.
    """
    if requested is not None:
        candidate = requested if requested.is_absolute() else root / requested
        candidate = candidate.resolve()
        if not candidate.is_file():
            raise FileNotFoundError(f"Tatarus-Bibliothek fehlt: {candidate}")
        return candidate

    preferred = (
        root / "build" / "Release" / "tatarus4_c.dll",
        root / "build" / "tatarus4_c.dll",
        root / "build-imaginatio-lab" / "Release" / "tatarus4_c.dll",
        root / "build-live-monitor" / "Release" / "tatarus4_c.dll",
        root / "build-imaginatio" / "Release" / "tatarus4_c.dll",
    )
    for candidate in preferred:
        if candidate.is_file():
            return candidate.resolve()

    # Letzter Fallback für benutzerdefinierte CMake-Buildverzeichnisse. Nur
    # Release-DLLs werden automatisch gewählt; Debug muss explizit angegeben
    # werden, damit nicht versehentlich ein falscher Runtime-Stand trainiert wird.
    discovered = sorted(
        (path.resolve() for path in root.glob("build*/Release/tatarus4_c.dll") if path.is_file()),
        key=lambda path: path.stat().st_mtime_ns,
        reverse=True,
    )
    if discovered:
        return discovered[0]

    searched = "\n  - ".join(str(path) for path in preferred)
    raise FileNotFoundError(
        "Keine TATARUS-V14-C-Bibliothek gefunden. Geprüft wurde:\n  - "
        f"{searched}\n"
        r"Führe zuerst .\BUILD_WINDOWS.bat aus oder gib die DLL explizit mit "
        r"--library .\build\Release\tatarus4_c.dll an."
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--input", type=Path)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--catalog-only", action="store_true")
    parser.add_argument("--limit", type=int, help="Nur für einen kurzen technischen Probelauf")
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    root = args.root.resolve()
    input_directory = (args.input or root / "512").resolve()
    library_path = resolve_library_path(root, args.library)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    bundle = (args.output or root / "output" / f"TATARUS_512_{stamp}").resolve()
    if bundle.exists():
        raise RuntimeError(f"Ausgabe existiert bereits: {bundle}")
    bundle.mkdir(parents=True)

    numbered = discover(input_directory)
    if args.limit is not None:
        if args.limit < 1:
            raise ValueError("--limit muss positiv sein")
        numbered = numbered[: args.limit]
    records, counts = build_catalog(numbered)
    write_catalog(bundle, records, counts)
    make_category_sheets(bundle, input_directory, records)
    if args.catalog_only:
        checksums(bundle)
        print(json.dumps({"bundle": str(bundle), "images": len(records), "categories": counts}, ensure_ascii=False))
        return 0
    print(f"TATARUS-Bibliothek: {library_path}", flush=True)
    api = TatarusLibrary(library_path)

    # Vor dem 726-Bilder-Lauf wird die geladene DLL selbst geprüft. So kann
    # weder ein alter V12/V13-Build noch eine falsche Snapshot-Policy unbemerkt
    # stundenlang trainiert werden.
    probe = api.create()
    try:
        runtime_summary = state_summary(api.state(probe))
        verify_v14_runtime(runtime_summary)
    finally:
        api.destroy(probe)
    print(
        "V14-Runtime geprüft: "
        f"schema={runtime_summary['schema']}, "
        f"snapshot={runtime_summary['snapshot_format_version']}",
        flush=True,
    )

    handle: ctypes.c_void_p | None = api.create()
    started = time.perf_counter()
    snapshot = bundle / "snapshot"
    try:
        for index, record in enumerate(records, start=1):
            with Image.open(input_directory / record["file"]) as image:
                pixels = image.convert("RGB").tobytes()
            image_started = time.perf_counter()
            api.ingest(handle, pixels, record["concept"], record["category"])
            elapsed = time.perf_counter() - started
            item_seconds = time.perf_counter() - image_started
            if index == 1 or index % 10 == 0 or index == len(records):
                rate = index / max(elapsed, 1e-9)
                remaining = (len(records) - index) / max(rate, 1e-9)
                print(
                    f"[{index:03d}/{len(records):03d}] {record['file']} -> {record['category']} "
                    f"({item_seconds:.2f}s; Rest ~{remaining / 60:.1f} min)",
                    flush=True,
                )
                write_json(
                    bundle / "progress.json",
                    {
                        "schema": SCHEMA,
                        "status": "learning",
                        "completed": index,
                        "total": len(records),
                        "elapsed_seconds": elapsed,
                    },
                )
        trained_summary = state_summary(api.state(handle))
        verify_summary(trained_summary, records, counts)
        print("Speichere vollständigen Tatarus-Snapshot ...", flush=True)
        api.save(handle, snapshot)
    finally:
        api.destroy(handle)
        handle = None

    print("Lade Snapshot in einen frischen Tatarus zur Endprüfung ...", flush=True)
    restored = api.create()
    try:
        api.load(restored, snapshot)
        restored_summary = state_summary(api.state(restored))
        verify_summary(restored_summary, records, counts)
    finally:
        api.destroy(restored)

    total_seconds = time.perf_counter() - started
    report = {
        "schema": SCHEMA,
        "status": "verified",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "source_directory": str(input_directory),
        "library": str(library_path),
        "seed": SEED,
        "files_seen": len(records),
        "unique_pixel_images": len({record["sha256"] for record in records}),
        "duplicate_files": sum(record["duplicate_of"] is not None for record in records),
        "category_counts": counts,
        "trained_state": trained_summary,
        "restored_state": restored_summary,
        "snapshot_relative_path": "snapshot",
        "snapshot_format_version": restored_summary["snapshot_format_version"],
        "visual_memory_policy": restored_summary["visual_memory_policy"],
        "elapsed_seconds": total_seconds,
        "verification": {
            "all_files_cataloged": True,
            "all_file_observations_retained": True,
            "category_evidence_matches_catalog": True,
            "fresh_organism_restore_passed": True,
            "source_raster_retention_disabled": True,
            "teacher_trace_retention_disabled": True,
            "working_canvas_not_persisted": True,
            "self_motor_engram_retained": True,
            "self_imprint_consolidation_passed": True,
        },
    }
    write_json(bundle / "training_report.json", report)
    progress = bundle / "progress.json"
    if progress.exists():
        progress.unlink()
    checksums(bundle)
    print(json.dumps({"bundle": str(bundle), "report": report}, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"FEHLER: {error}", file=sys.stderr, flush=True)
        raise

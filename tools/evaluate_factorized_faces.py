"""Reproducible real-image evaluation for IMAGINATIO v8 feature recombination."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import statistics
import time
import urllib.error
import urllib.request
from collections import Counter, defaultdict
from itertools import combinations
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


FACES = [
    (1, "FLAMING_SINGER"),
    (2, "FLAMING_GUITARIST"),
    (4, "COLOR_MAKEUP_WOMAN"),
    (9, "TATTOOED_WOMAN"),
    (10, "CRACKED_FACE"),
    (21, "GRAYSCALE_WOMAN"),
    (28, "BLUE_WATER_WOMAN"),
    (32, "MONOCHROME_WOMAN"),
    (36, "WARRIOR_MAN"),
]
FEATURES = [
    "BACKGROUND",
    "UPPER_LEFT", "UPPER_CENTER", "UPPER_RIGHT",
    "MIDDLE_LEFT", "CENTER", "MIDDLE_RIGHT",
    "LOWER_LEFT", "LOWER_CENTER", "LOWER_RIGHT",
]
FEATURE_LABELS = {
    "BACKGROUND": "Hintergrund",
    "UPPER_LEFT": "oben links (Gesicht: Haar/Stirn)",
    "UPPER_CENTER": "oben Mitte (Gesicht: Haar/Stirn)",
    "UPPER_RIGHT": "oben rechts (Gesicht: Haar/Stirn)",
    "MIDDLE_LEFT": "Mitte links (Gesicht: Auge/Wange)",
    "CENTER": "Zentrum (Gesicht: Nase/Mittelgesicht)",
    "MIDDLE_RIGHT": "Mitte rechts (Gesicht: Auge/Wange)",
    "LOWER_LEFT": "unten links (Gesicht: Wange/Kiefer)",
    "LOWER_CENTER": "unten Mitte (Gesicht: Mund/Kinn)",
    "LOWER_RIGHT": "unten rechts (Gesicht: Wange/Kiefer)",
}


def post(api: str, command: dict, timeout: int = 300) -> dict:
    request = urllib.request.Request(
        api,
        data=json.dumps(command, separators=(",", ":")).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP {exc.code}: {body}") from exc


def load_image(path: Path) -> tuple[str, np.ndarray]:
    image = Image.open(path).convert("RGB")
    if image.size != (512, 512):
        image = image.resize((512, 512), Image.Resampling.LANCZOS)
    array = np.asarray(image, dtype=np.uint8)
    return base64.b64encode(array.tobytes()).decode("ascii"), array


def canvas_array(response: dict) -> np.ndarray:
    canvas = response["imaginatio"]["canvas"]
    raw = base64.b64decode(canvas["rgb8_base64"], validate=True)
    width = int(canvas["width"])
    height = int(canvas["height"])
    if len(raw) != width * height * 3:
        raise RuntimeError("Invalid RGB8 canvas payload")
    return np.frombuffer(raw, dtype=np.uint8).reshape((height, width, 3)).copy()


def normalized_similarity(left: np.ndarray, right: np.ndarray) -> float:
    difference = np.abs(left.astype(np.int16) - right.astype(np.int16)).mean()
    return 1.0 - float(difference) / 255.0


def block_ssim(left: np.ndarray, right: np.ndarray, block: int = 8) -> float:
    x = left.astype(np.float64)
    y = right.astype(np.float64)
    scores: list[float] = []
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    for yy in range(0, x.shape[0], block):
        for xx in range(0, x.shape[1], block):
            xb = x[yy : yy + block, xx : xx + block]
            yb = y[yy : yy + block, xx : xx + block]
            mean_x = xb.mean(axis=(0, 1))
            mean_y = yb.mean(axis=(0, 1))
            variance_x = xb.var(axis=(0, 1))
            variance_y = yb.var(axis=(0, 1))
            covariance = ((xb - mean_x) * (yb - mean_y)).mean(axis=(0, 1))
            score = ((2 * mean_x * mean_y + c1) * (2 * covariance + c2)) / (
                (mean_x * mean_x + mean_y * mean_y + c1)
                * (variance_x + variance_y + c2)
            )
            scores.append(float(score.mean()))
    return float(np.mean(scores))


def entropy_bits(array: np.ndarray) -> float:
    values = []
    for channel in range(3):
        histogram = np.bincount(array[:, :, channel].ravel(), minlength=256).astype(float)
        probabilities = histogram[histogram > 0] / histogram.sum()
        values.append(float(-(probabilities * np.log2(probabilities)).sum()))
    return float(np.mean(values))


def feature_masks() -> dict[str, np.ndarray]:
    yy, xx = np.mgrid[0:512, 0:512]
    x = (xx + 0.5) / 512.0
    y = (yy + 0.5) / 512.0

    def gaussian(cx: float, cy: float, sx: float, sy: float) -> np.ndarray:
        return np.exp(-0.5 * (((x - cx) / sx) ** 2 + ((y - cy) / sy) ** 2))

    distance = np.sqrt(((x - 0.5) / 0.34) ** 2 + ((y - 0.52) / 0.43) ** 2)
    raw = [0.035 + 2.4 * np.clip((distance - 0.72) / 0.58, 0.0, 1.0) ** 2]
    for cy in (0.30, 0.52, 0.72):
        for cx in (0.31, 0.50, 0.69):
            raw.append(gaussian(cx, cy, 0.19, 0.16))
    raw = np.stack(raw, axis=0)
    normalized = raw / np.maximum(raw.sum(axis=0, keepdims=True), 1e-12)
    return {feature: normalized[index] for index, feature in enumerate(FEATURES)}


def weighted_mae(left: np.ndarray, right: np.ndarray, weights: np.ndarray) -> float:
    per_pixel = np.abs(left.astype(np.float64) - right.astype(np.float64)).mean(axis=2)
    return float((per_pixel * weights).sum() / max(1e-12, weights.sum()))


def make_contact_sheet(
    destination: Path,
    training: dict[int, np.ndarray],
    records: list[dict],
) -> None:
    items: list[tuple[str, np.ndarray, str]] = []
    label_by_number = dict(FACES)
    for number, array in training.items():
        items.append((f"TRAIN image{number}", array, label_by_number[number]))
    for record in records:
        donors = record["engine"]["feature_sources"]
        donor_line = " ".join(
            f"{item['feature'][0]}:{item['source_image']}" for item in donors
        )
        items.append(
            (
                f"NEW seed {record['seed']}",
                Image.open(destination.parent / record["file"]).convert("RGB"),
                donor_line,
            )
        )
    thumb = 256
    label_height = 55
    columns = 5
    rows = math.ceil(len(items) / columns)
    sheet = Image.new("RGB", (columns * thumb, rows * (thumb + label_height)), (20, 23, 30))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for index, (title, source, details) in enumerate(items):
        x = index % columns * thumb
        y = index // columns * (thumb + label_height)
        if isinstance(source, np.ndarray):
            source = Image.fromarray(source, "RGB")
        sheet.paste(source.resize((thumb, thumb), Image.Resampling.LANCZOS), (x, y))
        draw.multiline_text(
            (x + 5, y + thumb + 4),
            f"{title}\n{details}",
            fill=(238, 241, 248),
            font=font,
            spacing=2,
        )
    sheet.save(destination)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--port", type=int, default=18768)
    parser.add_argument("--seeds", type=int, nargs="*", default=list(range(101, 113)))
    args = parser.parse_args()
    root = args.root.resolve()
    api = f"http://127.0.0.1:{args.port}/api/imaginatio/control"
    evaluation = root / "Docs" / "evaluation"
    output = evaluation / "IMAGINATIO_FACTORIZED_FACE_OUTPUTS_2026-09-08"
    output.mkdir(parents=True, exist_ok=True)

    packed: dict[int, str] = {}
    training: dict[int, np.ndarray] = {}
    for number, _ in FACES:
        packed[number], training[number] = load_image(root / "512" / f"image{number}.png")

    started = time.perf_counter()
    learned = []
    print(f"Learning {len(FACES)} HUMAN_PORTRAIT examples", flush=True)
    for position, (number, label) in enumerate(FACES, start=1):
        response = post(
            api,
            {
                "action": "learn",
                "rgb8_base64": packed[number],
                "width": 512,
                "height": 512,
                "patch_side": 8,
                "text": label,
                "category": "HUMAN_PORTRAIT",
            },
        )
        last = response["imaginatio"].get("last_result", {})
        learned.append({"image": number, "label": label, "success": bool(last.get("success"))})
        print(f"  [{position}/{len(FACES)}] image{number}: {label}", flush=True)

    masks = feature_masks()
    face_by_anchor = {index: number for index, (number, _) in enumerate(FACES)}
    label_by_number = dict(FACES)
    records = []
    donor_usage: dict[str, Counter[int]] = defaultdict(Counter)
    generated_arrays = []
    print(f"Generating {len(args.seeds)} unseen variants", flush=True)
    for position, seed in enumerate(args.seeds, start=1):
        response = post(
            api,
            {"action": "draw_category", "category": "HUMAN_PORTRAIT", "variation_seed": seed},
        )
        array = canvas_array(response)
        generated_arrays.append(array)
        filename = f"human_portrait_seed_{seed}.png"
        Image.fromarray(array, "RGB").save(output / filename)
        last = response["imaginatio"].get("last_result", {})
        similarities = {
            number: normalized_similarity(array, source)
            for number, source in training.items()
        }
        nearest = max(similarities, key=similarities.get)
        exact = [number for number, source in training.items() if np.array_equal(array, source)]
        provenance = []
        assigned_top_three = 0
        for item in last.get("feature_sources", []):
            feature = item["feature"]
            source_index = int(item["source_index"])
            source_image = face_by_anchor[source_index]
            donor_usage[feature][source_image] += 1
            distances = {
                number: weighted_mae(array, source, masks[feature])
                for number, source in training.items()
            }
            rank = sorted(distances, key=distances.get).index(source_image) + 1
            if rank <= 3:
                assigned_top_three += 1
            provenance.append(
                {
                    "feature": feature,
                    "feature_label": FEATURE_LABELS[feature],
                    "source_index": source_index,
                    "source_image": source_image,
                    "source_label": label_by_number[source_image],
                    "regional_mae": distances[source_image],
                    "regional_similarity_rank": rank,
                }
            )
        record = {
            "seed": seed,
            "file": f"{output.name}/{filename}",
            "engine": {
                "success": bool(last.get("success")),
                "novelty": float(last.get("novelty", 0.0)),
                "similarity_to_category": float(last.get("similarity", 0.0)),
                "actions": int(last.get("actions", 0)),
                "feature_sources": provenance,
                "unique_feature_donors": len({item["source_index"] for item in provenance}),
            },
            "independent": {
                "sha256": hashlib.sha256(array.tobytes()).hexdigest(),
                "exact_training_matches": exact,
                "nearest_training_image": nearest,
                "nearest_training_label": label_by_number[nearest],
                "nearest_training_similarity": similarities[nearest],
                "nearest_training_block_ssim": block_ssim(array, training[nearest]),
                "assigned_donor_top3_regions": assigned_top_three,
                "entropy_bits": entropy_bits(array),
                "unique_rgb_colors": int(np.unique(array.reshape(-1, 3), axis=0).shape[0]),
            },
        }
        records.append(record)
        print(
            f"  [{position}/{len(args.seeds)}] seed {seed}: "
            f"donors={record['engine']['unique_feature_donors']} "
            f"nearest=image{nearest} sim={similarities[nearest]:.3f}",
            flush=True,
        )

    pairwise_mae = [
        float(np.abs(left.astype(np.int16) - right.astype(np.int16)).mean()) / 255.0
        for left, right in combinations(generated_arrays, 2)
    ]
    per_feature_variability = {}
    for feature, weights in masks.items():
        differences = [
            weighted_mae(left, right, weights) / 255.0
            for left, right in combinations(generated_arrays, 2)
        ]
        per_feature_variability[feature] = {
            "pairwise_normalized_mae_mean": statistics.fmean(differences),
            "pairwise_normalized_mae_min": min(differences),
            "pairwise_normalized_mae_max": max(differences),
        }

    result = {
        "schema": "tatarus-imaginatio-factorized-face-evaluation-v1",
        "date": "2026-09-08",
        "implementation": {
            "state_schema": "tatarus-imaginatio-v8",
            "feature_model": "hierarchical-object-part-running-statistics",
            "detail_policy": "factorized-soft-region-recombination",
            "feature_fields": FEATURES,
            "anchor_capacity": 16,
        },
        "method": {
            "training_category": "HUMAN_PORTRAIT",
            "training_images": [number for number, _ in FACES],
            "generated_seeds": args.seeds,
            "semantic_labels_are_human_assigned": True,
            "regional_donor_check_is_approximate": True,
            "external_generative_model_used": False,
        },
        "training": learned,
        "aggregate": {
            "outputs": len(records),
            "successful_outputs": sum(record["engine"]["success"] for record in records),
            "exact_training_copies": sum(bool(record["independent"]["exact_training_matches"]) for record in records),
            "unique_output_hashes": len({record["independent"]["sha256"] for record in records}),
            "unique_training_donors_used": len({
                image for counter in donor_usage.values() for image in counter
            }),
            "feature_donors_per_output_min": min(record["engine"]["unique_feature_donors"] for record in records),
            "feature_donors_per_output_mean": statistics.fmean(record["engine"]["unique_feature_donors"] for record in records),
            "engine_novelty_mean": statistics.fmean(record["engine"]["novelty"] for record in records),
            "nearest_training_similarity_mean": statistics.fmean(record["independent"]["nearest_training_similarity"] for record in records),
            "nearest_training_similarity_min": min(record["independent"]["nearest_training_similarity"] for record in records),
            "nearest_training_similarity_max": max(record["independent"]["nearest_training_similarity"] for record in records),
            "nearest_training_block_ssim_mean": statistics.fmean(record["independent"]["nearest_training_block_ssim"] for record in records),
            "pairwise_output_normalized_mae_mean": statistics.fmean(pairwise_mae),
            "pairwise_output_normalized_mae_min": min(pairwise_mae),
            "pairwise_output_normalized_mae_max": max(pairwise_mae),
            "assigned_donor_top3_region_rate": sum(record["independent"]["assigned_donor_top3_regions"] for record in records)
                / (len(records) * len(FEATURES)),
            "entropy_bits_mean": statistics.fmean(record["independent"]["entropy_bits"] for record in records),
        },
        "donor_usage": {
            feature: {str(number): count for number, count in sorted(counter.items())}
            for feature, counter in donor_usage.items()
        },
        "regional_output_variability": per_feature_variability,
        "records": records,
        "elapsed_seconds": time.perf_counter() - started,
    }
    json_path = evaluation / "IMAGINATIO_FACTORIZED_FACE_RESULTS_2026-09-08.json"
    json_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    contact_sheet = evaluation / "IMAGINATIO_FACTORIZED_FACE_CONTACT_SHEET_2026-09-08.png"
    make_contact_sheet(contact_sheet, training, records)
    print(json.dumps(result["aggregate"], indent=2), flush=True)
    print(f"Results: {json_path}", flush=True)
    print(f"Contact sheet: {contact_sheet}", flush=True)


if __name__ == "__main__":
    main()

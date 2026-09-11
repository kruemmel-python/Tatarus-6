"""End-to-end evaluation of the biological visual pathway on the 512 corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import statistics
import time
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from evaluate_factorized_faces import (
    canvas_array,
    load_image,
    normalized_similarity,
    post,
)


SEMANTICS = {
    1: ("FLAMING_SINGER", "HUMAN_PORTRAIT"),
    2: ("FLAMING_GUITARIST", "HUMAN_PORTRAIT"),
    3: ("BLACK_HORNED_DEMON", "CREATURE"),
    4: ("COLOR_MAKEUP_WOMAN", "HUMAN_PORTRAIT"),
    5: ("SCIFI_RIDER", "SCIFI_SCENE"),
    6: ("DOG", "ANIMAL"),
    7: ("HORNED_DEMON", "CREATURE"),
    8: ("BLUE_DEMON", "CREATURE"),
    9: ("TATTOOED_WOMAN", "HUMAN_PORTRAIT"),
    10: ("CRACKED_FACE", "HUMAN_PORTRAIT"),
    11: ("PURPLE_ALIEN", "CREATURE"),
    12: ("LION", "ANIMAL"),
    13: ("ABSTRACT_SWIRL_HEAD", "ABSTRACT"),
    14: ("BLUE_ALIEN", "CREATURE"),
    15: ("APE", "ANIMAL"),
    16: ("FANTASY_RIDER", "SCIFI_SCENE"),
    17: ("FURRY_CREATURE", "CREATURE"),
    18: ("SKULL_DEMON", "CREATURE"),
    19: ("FOREST_SOLDIER", "SCIFI_SCENE"),
    20: ("CATS", "ANIMAL"),
    21: ("GRAYSCALE_WOMAN", "HUMAN_PORTRAIT"),
    22: ("DRAGON", "CREATURE"),
    23: ("ELEPHANT", "ANIMAL"),
    24: ("ALIEN_ROBOT", "SCIFI_SCENE"),
    25: ("ECO_ANDROID_WORLD", "ECO_SCIFI"),
    26: ("VORTEX_HEAD", "ABSTRACT"),
    27: ("PALE_VILLAIN", "CREATURE"),
    28: ("BLUE_WATER_WOMAN", "HUMAN_PORTRAIT"),
    29: ("BATTLEFIELD_SOLDIERS", "SCIFI_SCENE"),
    30: ("SPACE_SOLDIER", "SCIFI_SCENE"),
    31: ("ROBOT_ANGEL", "SCIFI_SCENE"),
    32: ("MONOCHROME_WOMAN", "HUMAN_PORTRAIT"),
    33: ("BLUE_BEAST", "CREATURE"),
    36: ("WARRIOR_MAN", "HUMAN_PORTRAIT"),
    37: ("ALIEN_WARRIOR", "CREATURE"),
    38: ("ECO_ANDROID_LANDSCAPE", "ECO_SCIFI"),
    39: ("ANGRY_GORILLA", "ANIMAL"),
    40: ("DEMON_WOMAN", "CREATURE"),
    41: ("BUTTERFLY_CITY", "ECO_SCIFI"),
}

HOLDOUTS = {
    "HUMAN_PORTRAIT": 36,
    "CREATURE": 40,
    "ANIMAL": 39,
    "SCIFI_SCENE": 31,
    "ECO_SCIFI": 41,
    "ABSTRACT": 26,
}


def make_sheet(
    destination: Path,
    images: dict[int, np.ndarray],
    recognition: list[dict],
    generated: list[dict],
) -> None:
    categories = sorted(HOLDOUTS)
    thumb = 220
    label_height = 58
    columns = 5
    rows = len(categories)
    sheet = Image.new(
        "RGB",
        (columns * thumb, rows * (thumb + label_height)),
        (18, 21, 27),
    )
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    by_category = defaultdict(list)
    for item in generated:
        by_category[item["category"]].append(item)
    recognition_by_category = {item["expected_category"]: item for item in recognition}
    for row, category in enumerate(categories):
        holdout = HOLDOUTS[category]
        rec = recognition_by_category[category]
        train_ids = [
            number for number, (_, assigned) in SEMANTICS.items()
            if assigned == category and number != holdout
        ][:2]
        cells: list[tuple[np.ndarray, str]] = [
            (
                images[holdout],
                f"UNKNOWN image{holdout}\n"
                f"top1={rec['bottom_up']['top1']} "
                f"top3={'yes' if rec['bottom_up']['correct_top3'] else 'no'}",
            )
        ]
        for record in by_category[category]:
            cells.append(
                (
                    np.asarray(Image.open(destination.parent / record["file"]).convert("RGB")),
                    f"NEW seed {record['seed']}\n"
                    f"donors={record['unique_part_donors']} "
                    f"nearest={record['nearest_training_image']}",
                )
            )
        for number in train_ids:
            cells.append((images[number], f"LEARNED image{number}\n{SEMANTICS[number][0]}"))
        while len(cells) < columns:
            cells.append((np.zeros((512, 512, 3), dtype=np.uint8), ""))
        for column, (array, label) in enumerate(cells[:columns]):
            x = column * thumb
            y = row * (thumb + label_height)
            tile = Image.fromarray(array, "RGB").resize(
                (thumb, thumb), Image.Resampling.LANCZOS
            )
            sheet.paste(tile, (x, y))
            draw.multiline_text(
                (x + 5, y + thumb + 4),
                (category + "\n" + label) if column == 0 else label,
                fill=(238, 241, 248),
                font=font,
                spacing=2,
            )
    sheet.save(destination)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--port", type=int, default=18768)
    args = parser.parse_args()
    root = args.root.resolve()
    api = f"http://127.0.0.1:{args.port}/api/imaginatio/control"
    evaluation = root / "Docs" / "evaluation"
    output = evaluation / "IMAGINATIO_BIOLOGICAL_VISION_OUTPUTS_2026-09-08"
    output.mkdir(parents=True, exist_ok=True)

    packed: dict[int, str] = {}
    images: dict[int, np.ndarray] = {}
    for number in SEMANTICS:
        packed[number], images[number] = load_image(root / "512" / f"image{number}.png")

    started = time.perf_counter()
    insertion_order: dict[str, list[int]] = defaultdict(list)

    def learn(number: int) -> None:
        label, category = SEMANTICS[number]
        response = post(
            api,
            {
                "action": "learn",
                "rgb8_base64": packed[number],
                "width": 512,
                "height": 512,
                "patch_side": 8,
                "text": label,
                "category": category,
            },
        )
        if not response["imaginatio"].get("last_result", {}).get("success"):
            raise RuntimeError(f"Learning failed for image{number}")
        insertion_order[category].append(number)

    training_ids = [
        number for number, (_, category) in SEMANTICS.items()
        if HOLDOUTS[category] != number
    ]
    print(f"Learning {len(training_ids)} images; holding out {len(HOLDOUTS)}", flush=True)
    for index, number in enumerate(training_ids, 1):
        learn(number)
        print(f"  learn {index}/{len(training_ids)} image{number}", flush=True)

    recognition = []
    print("Recognizing previously unseen images", flush=True)
    for category, number in HOLDOUTS.items():
        base_command = {
            "action": "recognize",
            "rgb8_base64": packed[number],
            "width": 512,
            "height": 512,
            "maximum_matches": 3,
        }
        bottom_up = post(api, base_command)["recognition"]
        with_context = post(
            api, {**base_command, "category": category}
        )["recognition"]
        bottom_matches = bottom_up.get("matches", [])
        context_matches = with_context.get("matches", [])
        record = {
            "image": number,
            "label": SEMANTICS[number][0],
            "expected_category": category,
            "bottom_up": {
                **bottom_up,
                "top1": bottom_matches[0]["category"] if bottom_matches else None,
                "correct_top1": bool(bottom_matches and bottom_matches[0]["category"] == category),
                "correct_top3": any(item["category"] == category for item in bottom_matches),
            },
            "with_correct_context": {
                **with_context,
                "top1": context_matches[0]["category"] if context_matches else None,
                "correct_top1": bool(context_matches and context_matches[0]["category"] == category),
                "correct_top3": any(item["category"] == category for item in context_matches),
            },
        }
        recognition.append(record)
        print(
            f"  image{number} expected={category} "
            f"bottom-up={record['bottom_up']['top1']} "
            f"context={record['with_correct_context']['top1']}",
            flush=True,
        )

    print("Adding held-out experiences, then imagining new category variants", flush=True)
    for category, number in HOLDOUTS.items():
        learn(number)
    generated = []
    hashes = set()
    exact_copies = 0
    for category_index, category in enumerate(sorted(insertion_order)):
        category_ids = insertion_order[category]
        source_by_index = dict(enumerate(category_ids))
        for offset in range(2):
            seed = 1201 + category_index * 10 + offset
            response = post(
                api,
                {
                    "action": "draw_category",
                    "category": category,
                    "variation_seed": seed,
                },
            )
            array = canvas_array(response)
            filename = f"{category.lower()}_seed_{seed}.png"
            Image.fromarray(array, "RGB").save(output / filename)
            last = response["imaginatio"].get("last_result", {})
            similarities = {
                number: normalized_similarity(array, images[number])
                for number in category_ids
            }
            nearest = max(similarities, key=similarities.get)
            exact = [
                number for number in category_ids
                if np.array_equal(array, images[number])
            ]
            feature_sources = last.get("feature_sources", [])
            donors = {
                source_by_index.get(int(item["source_index"]), -1)
                for item in feature_sources
            }
            sha = hashlib.sha256(array.tobytes()).hexdigest()
            hashes.add(sha)
            exact_copies += bool(exact)
            generated.append(
                {
                    "category": category,
                    "seed": seed,
                    "file": f"{output.name}/{filename}",
                    "success": bool(last.get("success")),
                    "novelty": float(last.get("novelty", 0.0)),
                    "unique_part_donors": len(donors),
                    "source_images": sorted(donors),
                    "nearest_training_image": nearest,
                    "nearest_training_similarity": similarities[nearest],
                    "exact_training_matches": exact,
                    "sha256": sha,
                }
            )

    result = {
        "schema": "tatarus-biological-visual-evaluation-v1",
        "date": "2026-09-08",
        "implementation": {
            "visual_pathway": "tatarus-biological-vision-v1",
            "stages": [
                "retinal_photoreceptors",
                "retinal_ganglion_cells",
                "optic_nerve",
                "v1_oriented_edges",
                "v2_object_parts",
                "ventral_category_memory",
                "bounded_top_down",
            ],
            "category_specific_pixel_detectors_used": False,
            "external_generative_model_used": False,
        },
        "method": {
            "images": len(SEMANTICS),
            "categories": sorted(HOLDOUTS),
            "held_out_images": HOLDOUTS,
            "first_phase_training_images": training_ids,
            "generation_after_held_out_images_were_learned": True,
            "semantic_labels_are_human_assigned": True,
        },
        "recognition": recognition,
        "generation": generated,
        "aggregate": {
            "recognition_cases": len(recognition),
            "bottom_up_top1_correct": sum(
                item["bottom_up"]["correct_top1"] for item in recognition
            ),
            "bottom_up_top3_correct": sum(
                item["bottom_up"]["correct_top3"] for item in recognition
            ),
            "context_top1_correct": sum(
                item["with_correct_context"]["correct_top1"] for item in recognition
            ),
            "context_top3_correct": sum(
                item["with_correct_context"]["correct_top3"] for item in recognition
            ),
            "maximum_observed_top_down_boost": max(
                match.get("top_down_boost", 0.0)
                for item in recognition
                for match in item["with_correct_context"].get("matches", [])
            ),
            "generated_outputs": len(generated),
            "successful_generated_outputs": sum(
                item["success"] for item in generated
            ),
            "exact_training_copies": exact_copies,
            "unique_generated_hashes": len(hashes),
            "part_donors_per_output_mean": statistics.fmean(
                item["unique_part_donors"] for item in generated
            ),
            "nearest_training_similarity_mean": statistics.fmean(
                item["nearest_training_similarity"] for item in generated
            ),
        },
        "elapsed_seconds": time.perf_counter() - started,
    }
    json_path = evaluation / "IMAGINATIO_BIOLOGICAL_VISION_RESULTS_2026-09-08.json"
    json_path.write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    contact_sheet = evaluation / "IMAGINATIO_BIOLOGICAL_VISION_CONTACT_SHEET_2026-09-08.png"
    make_sheet(contact_sheet, images, recognition, generated)
    print(json.dumps(result["aggregate"], indent=2), flush=True)
    print(f"Results: {json_path}", flush=True)
    print(f"Contact sheet: {contact_sheet}", flush=True)


if __name__ == "__main__":
    main()

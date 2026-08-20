#!/usr/bin/env python3

import csv
import sys
from collections import defaultdict


def read_rows(path):
    with open(path, newline="", encoding="utf-8") as source:
        return [row for row in csv.DictReader(source) if row.get("seed") != "seed"]


def number(row, field):
    return float(row[field])


def summarize_dictionary(rows):
    scans = {}
    equalities = {}
    for row in rows:
        key = (
            row["type"],
            row["distribution"],
            int(row["actual_cardinality"]),
            row["representation"],
        )
        if row["operation"] == "decoded_scan":
            scans[key] = row
        elif row["operation"] in {"equality_decoded", "equality_encoded"}:
            equalities[key] = row

    print("## Dictionary cardinality crossover")
    print("| type | distribution | cardinality | C/N | raw B/value | u32 B/value | packed B/value | raw scan ns | u32 decode ns | packed decode ns |")
    print("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    groups = sorted({key[:3] for key in scans})
    for value_type, distribution, cardinality in groups:
        raw = scans[(value_type, distribution, cardinality, "raw")]
        fixed = scans[(value_type, distribution, cardinality, "dictionary_u32")]
        packed = scans[(value_type, distribution, cardinality, "dictionary_packed")]
        print(
            f"| {value_type} | {distribution} | {cardinality} | {number(raw, 'cardinality_ratio'):.6g} "
            f"| {number(raw, 'bytes_per_value'):.4f} | {number(fixed, 'bytes_per_value'):.4f} "
            f"| {number(packed, 'bytes_per_value'):.4f} | {number(raw, 'ns_per_value'):.4f} "
            f"| {number(fixed, 'ns_per_value'):.4f} | {number(packed, 'ns_per_value'):.4f} |"
        )

    print("\n## Encoded equality speed ratio")
    print("| type | distribution | cardinality | selectivity | u32/raw time | packed/raw time |")
    print("|---|---|---:|---:|---:|---:|")
    equality_groups = sorted({key[:3] for key in equalities})
    for value_type, distribution, cardinality in equality_groups:
        raw = equalities[(value_type, distribution, cardinality, "raw")]
        fixed = equalities[(value_type, distribution, cardinality, "dictionary_u32")]
        packed = equalities[(value_type, distribution, cardinality, "dictionary_packed")]
        raw_ns = number(raw, "ns_per_value")
        print(
            f"| {value_type} | {distribution} | {cardinality} "
            f"| {number(raw, 'predicate_selectivity'):.6g} | {number(fixed, 'ns_per_value') / raw_ns:.4f} "
            f"| {number(packed, 'ns_per_value') / raw_ns:.4f} |"
        )

    print("\n## First measured packed size loss")
    for value_type, distribution in sorted({key[:2] for key in scans}):
        losses = []
        for candidate_type, candidate_distribution, cardinality in groups:
            if candidate_type != value_type or candidate_distribution != distribution:
                continue
            raw = scans[(value_type, distribution, cardinality, "raw")]
            packed = scans[(value_type, distribution, cardinality, "dictionary_packed")]
            if number(packed, "total_bytes") >= number(raw, "total_bytes"):
                losses.append((number(raw, "cardinality_ratio"), cardinality))
        if losses:
            ratio, cardinality = min(losses)
            print(f"- {value_type}/{distribution}: C={cardinality}, C/N={ratio:.6g}")
        else:
            print(f"- {value_type}/{distribution}: no size loss in measured sweep")


def summarize_packed(rows):
    scans = defaultdict(dict)
    lookups = defaultdict(dict)
    for row in rows:
        target = scans if row["operation"] == "sequential_scan" else lookups
        target[int(row["bit_width"])][row["representation"]] = row

    print("\n## Packed versus uint32 codes")
    print("| width | uint32 B/value | packed B/value | uint32 scan ns | packed scan ns | packed/u32 scan | packed/u32 random |")
    print("|---:|---:|---:|---:|---:|---:|---:|")
    for width in sorted(scans):
        fixed = scans[width]["uint32"]
        packed = scans[width]["packed"]
        fixed_random = lookups[width]["uint32"]
        packed_random = lookups[width]["packed"]
        fixed_ns = number(fixed, "ns_per_value")
        print(
            f"| {width} | {number(fixed, 'bytes_per_value'):.4f} | {number(packed, 'bytes_per_value'):.4f} "
            f"| {fixed_ns:.4f} | {number(packed, 'ns_per_value'):.4f} "
            f"| {number(packed, 'ns_per_value') / fixed_ns:.4f} "
            f"| {number(packed_random, 'ns_per_value') / number(fixed_random, 'ns_per_value'):.4f} |"
        )


if len(sys.argv) != 3:
    raise SystemExit("usage: summarize_dictionary.py DICTIONARY.csv PACKED_CODES.csv")

dictionary_rows = read_rows(sys.argv[1])
packed_rows = read_rows(sys.argv[2])
if not dictionary_rows or not packed_rows:
    raise SystemExit("both input files must contain benchmark rows")

summarize_dictionary(dictionary_rows)
summarize_packed(packed_rows)

#!/usr/bin/env python3
"""Inspect and clean invalid historical sensor rows in aquaponics.db.

Default behavior is report-only. Use --apply to update the DB in place.
The script creates a timestamped backup before applying any mutation.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sqlite3
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


@dataclass(frozen=True)
class SensorRule:
    name: str
    sql_invalid: str
    sql_zero: str | None = None
    note: str = ""


RULES = {
    "water_temp": SensorRule(
        name="water_temp",
        sql_invalid="water_temp IS NOT NULL AND (water_temp IN (0, 85.0, -127.0) OR water_temp < 0 OR water_temp > 50)",
        sql_zero="water_temp = 0",
        note="DS18B20 invalid values often show as 85.0, -127.0, or legacy 0 fallback",
    ),
    "air_temp": SensorRule(
        name="air_temp",
        sql_invalid="air_temp IS NOT NULL AND (air_temp = 0 OR air_temp < 0 OR air_temp > 60)",
        sql_zero="air_temp = 0",
        note="DHT failures were historically persisted as 0 by the Pi fallback",
    ),
    "humidity": SensorRule(
        name="humidity",
        sql_invalid="humidity IS NOT NULL AND (humidity = 0 OR humidity < 0 OR humidity > 100)",
        sql_zero="humidity = 0",
        note="DHT failures were historically persisted as 0 by the Pi fallback",
    ),
    "tds": SensorRule(
        name="tds",
        sql_invalid="tds IS NOT NULL AND tds <= 0",
        sql_zero="tds = 0",
        note="TDS missing payloads were historically persisted as 0 by the Pi fallback",
    ),
    "ph": SensorRule(
        name="ph",
        sql_invalid="ph IS NOT NULL AND (ph <= 0 OR ph > 14)",
        sql_zero="ph = 0",
        note="pH missing payloads were historically persisted as 0 by the Pi fallback",
    ),
    "light": SensorRule(
        name="light",
        sql_invalid="light IS NOT NULL AND light < 0",
        sql_zero=None,
        note="Light sensor invalid payloads are usually omitted, not stored as 0",
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Inspect and clean invalid historical sensor values")
    parser.add_argument(
        "--db",
        type=Path,
        default=Path(__file__).with_name("aquaponics.db"),
        help="Path to aquaponics.db (default: pi_server/aquaponics.db next to this script)",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Apply cleanup in place. Without this flag the script only prints a report.",
    )
    parser.add_argument(
        "--backup-dir",
        type=Path,
        default=Path(__file__).with_name("db_backups"),
        help="Directory for database backups before cleanup",
    )
    parser.add_argument(
        "--skip-sensors",
        nargs="*",
        default=[],
        metavar="SENSOR",
        help="Sensor columns to ignore, e.g. --skip-sensors ph light",
    )
    parser.add_argument(
        "--settings",
        type=Path,
        default=Path(__file__).with_name("settings.json"),
        help="Optional settings.json used to auto-skip disabled sensors when sensor_config exists",
    )
    return parser.parse_args()


def query_scalar(cursor: sqlite3.Cursor, sql: str) -> int:
    cursor.execute(sql)
    row = cursor.fetchone()
    return int(row[0] or 0)


def load_disabled_sensors(settings_path: Path) -> set[str]:
    if not settings_path.exists():
        return set()

    try:
        with settings_path.open("r", encoding="utf-8") as handle:
            settings = json.load(handle)
    except Exception:
        return set()

    sensor_config = settings.get("sensor_config")
    if not isinstance(sensor_config, dict):
        return set()

    mapping = {
        "water": "water_temp",
        "air": "air_temp",
        "humidity": "humidity",
        "tds": "tds",
        "ph": "ph",
        "light": "light",
    }

    disabled = set()
    for settings_key, column in mapping.items():
        if sensor_config.get(settings_key) is False:
            disabled.add(column)
    return disabled


def resolve_active_rules(args: argparse.Namespace) -> tuple[dict[str, SensorRule], set[str]]:
    skipped = {sensor.strip() for sensor in args.skip_sensors if sensor.strip()}
    skipped.update(load_disabled_sensors(args.settings.resolve()))

    unknown = skipped.difference(RULES.keys())
    if unknown:
        raise ValueError(f"Unknown sensor name(s): {', '.join(sorted(unknown))}")

    active_rules = {name: rule for name, rule in RULES.items() if name not in skipped}
    return active_rules, skipped


def build_report(cursor: sqlite3.Cursor, active_rules: dict[str, SensorRule]) -> tuple[int, dict[str, dict[str, int]]]:
    total_rows = query_scalar(cursor, "SELECT COUNT(*) FROM sensors")
    report: dict[str, dict[str, int]] = {}

    for column, rule in active_rules.items():
        report[column] = {
            "invalid_rows": query_scalar(cursor, f"SELECT COUNT(*) FROM sensors WHERE {rule.sql_invalid}"),
            "zero_rows": query_scalar(cursor, f"SELECT COUNT(*) FROM sensors WHERE {rule.sql_zero}") if rule.sql_zero else 0,
            "null_rows": query_scalar(cursor, f"SELECT COUNT(*) FROM sensors WHERE {column} IS NULL"),
        }

    return total_rows, report


def print_report(total_rows: int, report: dict[str, dict[str, int]], skipped: set[str]) -> None:
    print(f"DB rows in sensors: {total_rows}")
    print("Invalid history summary:")
    for column, stats in report.items():
        rule = RULES[column]
        print(
            f"- {column}: invalid={stats['invalid_rows']}, zero={stats['zero_rows']}, null={stats['null_rows']}"
            f" | {rule.note}"
        )
    if skipped:
        print("Skipped sensors:", ", ".join(sorted(skipped)))


def make_backup(db_path: Path, backup_dir: Path) -> Path:
    backup_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_path = backup_dir / f"{db_path.stem}-{timestamp}{db_path.suffix}"
    shutil.copy2(db_path, backup_path)
    return backup_path


def apply_cleanup(cursor: sqlite3.Cursor, active_rules: dict[str, SensorRule]) -> dict[str, int]:
    updated_counts: dict[str, int] = {}
    for column, rule in active_rules.items():
        cursor.execute(f"UPDATE sensors SET {column} = NULL WHERE {rule.sql_invalid}")
        updated_counts[column] = cursor.rowcount if cursor.rowcount != -1 else 0
    return updated_counts


def main() -> int:
    args = parse_args()
    db_path = args.db.resolve()

    try:
        active_rules, skipped = resolve_active_rules(args)
    except ValueError as exc:
        print(exc)
        return 2

    if not db_path.exists():
        print(f"Database not found: {db_path}")
        print("Tip: copy this script next to aquaponics.db on the Pi or pass --db <full-path>.")
        return 1

    conn = sqlite3.connect(db_path)
    try:
        cursor = conn.cursor()
        total_rows, report = build_report(cursor, active_rules)
        print_report(total_rows, report, skipped)

        if not args.apply:
            print("\nReport only. Re-run with --apply to replace invalid historical values with NULL.")
            return 0

        backup_path = make_backup(db_path, args.backup_dir)
        print(f"\nBackup created: {backup_path}")

        updated_counts = apply_cleanup(cursor, active_rules)
        conn.commit()
        print("Cleanup applied:")
        for column, count in updated_counts.items():
            print(f"- {column}: set {count} row(s) to NULL")

        print("\nDone. Refresh the Graphs page to see cleaned historical gaps instead of false drops.")
        return 0
    finally:
        conn.close()


if __name__ == "__main__":
    raise SystemExit(main())
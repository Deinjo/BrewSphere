#!/usr/bin/env python3
"""Test the Brewfather API and print the latest fermentation temperature.

Credentials are read from config_local.py or from BREWFATHER_USER_ID and
BREWFATHER_API_KEY. They are never printed. The API key only needs the
batches.read scope.
"""

import argparse
import base64
import html
import json
import os
import sys
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

try:
    import config_local
except ImportError:
    config_local = None


DEFAULT_BASE_URL = "https://api.brewfather.app/v2"


def request_json(url: str, user_id: str, api_key: str, timeout: float, request_callback=None):
    if request_callback is not None:
        request_callback("GET", url)
    credentials = f"{user_id}:{api_key}".encode("utf-8")
    authorization = base64.b64encode(credentials).decode("ascii")
    request = Request(
        url,
        headers={
            "Accept": "application/json",
            "Authorization": f"Basic {authorization}",
            "User-Agent": "BrewSphere-Brewfather-Test/1.0",
        },
    )
    with urlopen(request, timeout=timeout) as response:
        return json.load(response)


def get_batches(
    base_url: str,
    status: str | None,
    user_id: str,
    api_key: str,
    timeout: float,
    complete: bool = False,
    start_after: str | None = None,
    request_callback=None,
):
    parameters = {"limit": 50, "complete": str(complete).lower()}
    if status:
        parameters["status"] = status
    if start_after:
        parameters["start_after"] = start_after
    query = urlencode(parameters)
    return request_json(
        f"{base_url.rstrip('/')}/batches?{query}", user_id, api_key, timeout,
        request_callback=request_callback,
    )


def get_all_batches(
    base_url: str,
    user_id: str,
    api_key: str,
    timeout: float,
    complete: bool = False,
    request_callback=None,
):
    """Fetch all batches using Brewfather API v2 start_after paging."""
    batches = []
    start_after = None
    while True:
        page = get_batches(
            base_url, None, user_id, api_key, timeout,
            complete=complete, start_after=start_after,
            request_callback=request_callback,
        )
        if not isinstance(page, list):
            raise ValueError("Unerwartetes Format der Batch-Antwort (kein Array)")
        batches.extend(page)
        if len(page) < 50:
            return batches
        last_id = page[-1].get("_id") if isinstance(page[-1], dict) else None
        if not last_id or last_id == start_after:
            raise ValueError("Die Batch-Paginierung lieferte keine neue ID")
        start_after = last_id


def get_last_reading(
    base_url: str, batch_id: str, user_id: str, api_key: str, timeout: float,
    request_callback=None,
):
    return request_json(
        f"{base_url.rstrip('/')}/batches/{batch_id}/readings/last",
        user_id,
        api_key,
        timeout, request_callback=request_callback,
    )


def get_batch(
    base_url: str, batch_id: str, user_id: str, api_key: str, timeout: float,
    request_callback=None,
):
    return request_json(
        f"{base_url.rstrip('/')}/batches/{batch_id}", user_id, api_key, timeout,
        request_callback=request_callback,
    )


def get_all_readings(
    base_url: str, batch_id: str, user_id: str, api_key: str, timeout: float,
    request_callback=None,
):
    return request_json(
        f"{base_url.rstrip('/')}/batches/{batch_id}/readings",
        user_id, api_key, timeout, request_callback=request_callback,
    )


def get_brew_tracker(
    base_url: str, batch_id: str, user_id: str, api_key: str, timeout: float,
    request_callback=None,
):
    return request_json(
        f"{base_url.rstrip('/')}/batches/{batch_id}/brewtracker",
        user_id, api_key, timeout, request_callback=request_callback,
    )


def save_redacted_response(path: str, batch: dict, reading: dict) -> None:
    """Save only relevant response data with identifying values masked."""
    batch_data = {
        key: value
        for key, value in batch.items()
        if key in {"name", "status", "batchNo", "brewDate", "recipe"}
    }
    batch_data["_id"] = "REDACTED"

    reading_data = dict(reading)
    for key in ("id", "_id", "_uid"):
        if key in reading_data:
            reading_data[key] = "REDACTED"

    output = {"batch": batch_data, "last_reading": reading_data}
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(output, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
    )


def save_full_json(path: str, response_data: dict) -> None:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(response_data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def save_html(path: str, response_data: dict) -> None:
    sections = []
    for title, value in response_data.items():
        formatted = json.dumps(value, indent=2, ensure_ascii=False)
        sections.append(
            "<details open>"
            f"<summary>{html.escape(title)}</summary>"
            f"<pre>{html.escape(formatted)}</pre>"
            "</details>"
        )

    document = """<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Brewfather API Response</title>
  <style>
    body { background: #202124; color: #e8eaed; font: 15px/1.5 Consolas, monospace; margin: 2rem; }
    h1 { font: 22px sans-serif; }
    p { color: #bdc1c6; font: 14px sans-serif; }
    details { background: #292a2d; border: 1px solid #5f6368; border-radius: 6px; margin: 1rem 0; }
    summary { cursor: pointer; font: bold 16px sans-serif; padding: .75rem 1rem; }
    pre { overflow: auto; padding: 0 1rem 1rem; }
  </style>
</head>
<body>
  <h1>Brewfather API Response</h1>
  <p>Lokaler Diagnoseexport. Diese Datei kann private Brau- und Sensordaten enthalten.</p>
  {sections}
</body>
</html>
""".replace("{sections}", "\n".join(sections))

    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(document, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Test Brewfather batch and latest-reading API calls."
    )
    parser.add_argument(
        "--status",
        default="Fermenting",
        help="Batch status to list (default: Fermenting)",
    )
    parser.add_argument(
        "--batch-id",
        help="Use this batch ID directly instead of selecting the first listed batch",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help=f"Brewfather API base URL (default: {DEFAULT_BASE_URL})",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="HTTP timeout in seconds (default: 10)",
    )
    parser.add_argument(
        "--save-json",
        metavar="PATH",
        help="Save a redacted diagnostic JSON to this path",
    )
    parser.add_argument(
        "--save-full-json",
        metavar="PATH",
        help="Save the complete API response as formatted JSON",
    )
    parser.add_argument(
        "--save-html",
        metavar="PATH",
        help="Save the complete API response as a navigable HTML file",
    )
    args = parser.parse_args()

    user_id = os.environ.get("BREWFATHER_USER_ID")
    api_key = os.environ.get("BREWFATHER_API_KEY")
    if config_local is not None:
        user_id = user_id or getattr(config_local, "BREWFATHER_USER_ID", "")
        api_key = api_key or getattr(config_local, "BREWFATHER_API_KEY", "")

    if not user_id or not api_key:
        print(
            "Fehler: Bitte config_local.py ausfuellen oder "
            "BREWFATHER_USER_ID und BREWFATHER_API_KEY setzen.",
            file=sys.stderr,
        )
        return 2

    try:
        batches_response = None
        full_response_requested = bool(args.save_full_json or args.save_html)
        if args.batch_id:
            batch = {"_id": args.batch_id, "name": "(direct batch ID)"}
            if full_response_requested:
                batches_response = [
                    get_batch(
                        args.base_url,
                        args.batch_id,
                        user_id,
                        api_key,
                        args.timeout,
                    )
                ]
                batch = batches_response[0]
        else:
            batches_response = get_batches(
                args.base_url,
                args.status,
                user_id,
                api_key,
                args.timeout,
                complete=full_response_requested,
            )
            if not isinstance(batches_response, list):
                raise ValueError("Unerwartetes Format der Batch-Antwort (kein Array)")
            if not batches_response:
                print(f"Keine Batches mit Status '{args.status}' gefunden.")
                return 1
            batch = batches_response[0]

        batch_id = batch.get("_id")
        if not batch_id:
            raise ValueError("Die Batch-Antwort enthaelt keine _id")

        reading = get_last_reading(
            args.base_url, batch_id, user_id, api_key, args.timeout
        )
        temperature = reading.get("temp") if isinstance(reading, dict) else None
        if not isinstance(temperature, (int, float)):
            raise ValueError("Die Messwert-Antwort enthaelt kein numerisches temp-Feld")

        print(f"Batch: {batch.get('name', '<ohne Namen>')} ({batch_id})")
        if "status" in batch:
            print(f"Status: {batch['status']}")
        print(f"Temperatur: {temperature:.1f} C")
        if isinstance(reading, dict) and "sg" in reading:
            print(f"SG: {reading['sg']}")
        if isinstance(reading, dict) and "type" in reading:
            print(f"Sensor: {reading['type']}")
        if args.save_json:
            save_redacted_response(args.save_json, batch, reading)
            print(f"Redigiertes JSON gespeichert: {args.save_json}")
        if args.save_full_json or args.save_html:
            full_response = {
                "batches_response": batches_response,
                "last_reading_response": reading,
            }
            if args.save_full_json:
                save_full_json(args.save_full_json, full_response)
                print(f"Vollstaendiges JSON gespeichert: {args.save_full_json}")
            if args.save_html:
                save_html(args.save_html, full_response)
                print(f"Navigierbares HTML gespeichert: {args.save_html}")
        return 0
    except HTTPError as error:
        print(f"HTTP-Fehler: {error.code} {error.reason}", file=sys.stderr)
        if error.code == 401:
            print("Bitte User-ID und API-Key pruefen.", file=sys.stderr)
        elif error.code == 403:
            print("Der API-Key benoetigt den Scope batches.read.", file=sys.stderr)
        return 1
    except (URLError, TimeoutError) as error:
        print(f"Netzwerkfehler: {error}", file=sys.stderr)
        return 1
    except (ValueError, json.JSONDecodeError) as error:
        print(f"Antwortfehler: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

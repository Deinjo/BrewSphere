#!/usr/bin/env python3
"""Local browser mockup and safe Brewfather API proxy."""

import base64
import json
import mimetypes
import os
import webbrowser
from datetime import datetime, timezone
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

try:
    import config_local
except ImportError:
    config_local = None

ROOT = Path(__file__).resolve().parent
API_BASE = "https://api.brewfather.app/v2"


def plato_from_sg(specific_gravity):
    return (-616.868 + 1111.14 * specific_gravity -
            630.272 * specific_gravity ** 2 +
             135.997 * specific_gravity ** 3)


def display_name(title):
    """Use the text after the first # as the compact display title."""
    title = str(title or "")
    marker = title.rfind("#")
    if marker >= 0:
        compact = title[marker + 1:].split(")", 1)[0].strip()
        if compact:
            return compact
    return title


def display_name_from_batch(detail, batch):
    candidates = [detail.get("description", "")]
    candidates.extend(event.get("description", "") for event in detail.get("events", []) if isinstance(event, dict))
    candidates.extend([
        detail.get("name", ""),
        (detail.get("recipe") or {}).get("name", ""),
        batch.get("name", ""),
    ])
    for candidate in candidates:
        if "#" in str(candidate or "") and display_name(candidate) != str(candidate or ""):
            return display_name(candidate)
    return str(candidates[-1] or "")


def api_json(path):
    user_id = os.environ.get("BREWFATHER_USER_ID") or getattr(config_local, "BREWFATHER_USER_ID", "")
    api_key = os.environ.get("BREWFATHER_API_KEY") or getattr(config_local, "BREWFATHER_API_KEY", "")
    if not user_id or not api_key or "HIER_DEINE" in user_id or "HIER_DEIN" in api_key:
        raise RuntimeError("Brewfather-Zugangsdaten fehlen in config_local.py")
    token = base64.b64encode(f"{user_id}:{api_key}".encode()).decode()
    request = Request(f"{API_BASE}{path}", headers={"Authorization": f"Basic {token}", "Accept": "application/json"})
    with urlopen(request, timeout=10) as response:
        return json.load(response)


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?", 1)[0] == "/api/brewfather":
            try:
                batches = api_json("/batches?status=Fermenting&limit=50")
                if not batches:
                    raise RuntimeError("Kein fermentierender Batch gefunden")
                batch = batches[0]
                batch_id = batch["_id"]
                detail = api_json(f"/batches/{batch_id}?include=estimatedFg,measuredOg,measuredFg,measuredAttenuation")
                reading = api_json(f"/batches/{batch_id}/readings/last")
                sg = float(reading.get("sg") or 0)
                estimated_fg = float(detail.get("estimatedFg") or 0)
                brew_day = 0
                if detail.get("brewDate"):
                    start = datetime.fromtimestamp(detail["brewDate"] / 1000, timezone.utc)
                    brew_day = (datetime.now(timezone.utc).date() - start.date()).days + 1
                response = {
                    "batch_name": display_name_from_batch(detail, batch),
                    "recipe_name": (detail.get("recipe") or {}).get("name", (batch.get("recipe") or {}).get("name", "")),
                    "batch_number": detail.get("batchNo", batch.get("batchNo", 0)),
                    "status": detail.get("status", batch.get("status", "")),
                    "brew_day": max(0, brew_day),
                    "temperature": reading.get("temp", 0),
                    "target_temperature": reading.get("temp_target", 0),
                    "fridge_temperature": reading.get("fridgeTemp", 0),
                    "plato": round(plato_from_sg(sg), 1) if sg else 0,
                    "target_plato": round(plato_from_sg(estimated_fg), 1) if estimated_fg else 0,
                    "attenuation": detail.get("measuredAttenuation", 0),
                    "end_attenuation": detail.get("measuredAttenuation", 0),
                }
                self.send_json(response)
            except (HTTPError, URLError, RuntimeError, KeyError) as error:
                self.send_error(502, str(error))
            return
        super().do_GET()

    def send_json(self, value):
        payload = json.dumps(value).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


if __name__ == "__main__":
    os.chdir(ROOT)
    server = ThreadingHTTPServer(("127.0.0.1", 8765), Handler)
    url = "http://127.0.0.1:8765"
    print(f"BrewSphere Mockup: {url}")
    webbrowser.open(url)
    server.serve_forever()

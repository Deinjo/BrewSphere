#!/usr/bin/env python3
"""Simple graphical front end for test_brewfather_api.py."""

import json
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from pathlib import Path
import re
from urllib.error import HTTPError
from PIL import Image, ImageTk

import test_brewfather_api as api

TOOL_VERSION = "1.4.0"
BRAND_IMAGE_NAME = "brewsphere-emblem-512.png"

STATUS_COLORS = {
    "Planning": ("#dbeafe", "#17324d"),
    "Brewing": ("#ffedd5", "#5c2b08"),
    "Fermenting": ("#dcfce7", "#14532d"),
    "Conditioning": ("#f3e8ff", "#4a1d66"),
    "Completed": ("#e5e7eb", "#1f2937"),
    "Archived": ("#cbd5e1", "#334155"),
}
STATUS_ORDER = {
    "Brewing": 0,
    "Fermenting": 1,
    "Conditioning": 2,
    "Planning": 3,
    "Completed": 4,
    "Archived": 5,
}


CONFIG_MACROS = {
    "user_id": "BREWFATHER_USER_ID",
    "api_key": "BREWFATHER_API_KEY",
}


def config_header_path() -> Path:
    """Use a tool-local header first, then the firmware include header."""
    if getattr(sys, "frozen", False):
        # In a one-file build __file__ points into a temporary extraction folder.
        return Path(sys.executable).resolve().parent / "config_local.h"
    application_dir = Path(__file__).resolve().parent
    tool_header = application_dir / "config_local.h"
    if tool_header.exists():
        return tool_header
    return Path(__file__).resolve().parents[2] / "include" / "config_local.h"


def resource_path(name: str) -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / name
    return Path(__file__).resolve().parents[2] / "docs" / "assets" / "brand" / name


def read_config_header() -> dict[str, str]:
    path = config_header_path()
    if not path.is_file():
        return {}
    content = path.read_text(encoding="utf-8")
    values = {}
    for key, macro in CONFIG_MACROS.items():
        match = re.search(
            rf"^\s*#define\s+{re.escape(macro)}\s+\"([^\"]*)\"\s*$",
            content,
            re.MULTILINE,
        )
        if match:
            values[key] = match.group(1)
    return values


def save_config_value(key: str, value: str) -> Path:
    path = config_header_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    content = path.read_text(encoding="utf-8") if path.exists() else "#pragma once\n"
    macro = CONFIG_MACROS[key]
    safe_value = value.replace('"', "")
    line = f'#define {macro} "{safe_value}"'
    pattern = rf"^\s*#define\s+{re.escape(macro)}\s+.*$"
    if re.search(pattern, content, re.MULTILINE):
        content = re.sub(pattern, line, content, count=1, flags=re.MULTILINE)
    else:
        content = content.rstrip() + "\n\n" + line + "\n"
    path.write_text(content, encoding="utf-8")
    return path


class BrewfatherGui(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"Brewfather API Test v{TOOL_VERSION}")
        self.minsize(1100, 650)
        self.resizable(True, True)
        self._build_form()
        config_values = read_config_header()
        self.user_id.set(config_values.get("user_id", ""))
        self.api_key.set(config_values.get("api_key", ""))

    def _build_form(self):
        self.columnconfigure(0, weight=1)
        self.columnconfigure(1, weight=2)
        self.rowconfigure(1, weight=1)
        self.rowconfigure(2, weight=0)

        form = ttk.LabelFrame(self, text="Brewfather-Zugang und Abfrage")
        form.grid(row=0, column=0, columnspan=2, padx=12, pady=12, sticky="ew")
        form.columnconfigure(1, weight=1)

        self.user_id = tk.StringVar()
        self.api_key = tk.StringVar()
        self.status = tk.StringVar(value="Fermenting")
        self.batch_id = tk.StringVar()
        self.timeout = tk.StringVar(value="10")
        self.base_url = tk.StringVar(value=api.DEFAULT_BASE_URL)
        self.save_json = tk.BooleanVar()
        self.save_full_json = tk.BooleanVar()
        self.save_html = tk.BooleanVar()
        self.show_key = tk.BooleanVar()

        self._entry(form, 0, "User-ID:", self.user_id)
        self.key_entry = self._entry(form, 1, "API-Key:", self.api_key, show="*")
        ttk.Button(
            form, text="Speichern", command=lambda: self._save_credential("api_key"),
        ).grid(row=1, column=2, padx=6, sticky="w")
        ttk.Button(
            form, text="Speichern", command=lambda: self._save_credential("user_id"),
        ).grid(row=0, column=2, padx=6, sticky="w")
        ttk.Checkbutton(
            form, text="API-Key anzeigen", variable=self.show_key,
            command=self._toggle_key,
        ).grid(row=1, column=3, padx=6, sticky="w")
        brand_path = resource_path(BRAND_IMAGE_NAME)
        if brand_path.is_file():
            brand_image = Image.open(brand_path).convert("RGBA")
            brand_image.thumbnail((192, 192), Image.Resampling.LANCZOS)
            self.brand_image = ImageTk.PhotoImage(brand_image)
            self.iconphoto(True, self.brand_image)
            ttk.Label(form, image=self.brand_image).grid(
                row=2, column=2, rowspan=4, padx=(24, 16), pady=8, sticky="nsew",
            )
        ttk.Label(form, text="Batch-Status:").grid(
            row=2, column=0, padx=8, pady=4, sticky="w",
        )
        ttk.Combobox(
            form, textvariable=self.status, state="readonly",
            values=("Brewing", "Fermenting", "Conditioning", "Planning", "Completed", "Archived"),
        ).grid(row=2, column=1, padx=8, pady=4, sticky="ew")
        self._entry(form, 3, "Batch-ID (optional):", self.batch_id)
        self._entry(form, 4, "Basis-URL:", self.base_url)
        self._entry(form, 5, "Timeout (Sekunden):", self.timeout)

        exports = ttk.Frame(form)
        exports.grid(row=6, column=1, columnspan=2, pady=(6, 0), sticky="w")
        ttk.Label(exports, text="Exporte:").pack(side="left", padx=(0, 8))
        ttk.Checkbutton(exports, text="reduziertes JSON", variable=self.save_json).pack(side="left")
        ttk.Checkbutton(exports, text="vollständiges JSON", variable=self.save_full_json).pack(side="left")
        ttk.Checkbutton(exports, text="HTML", variable=self.save_html).pack(side="left")

        buttons = ttk.Frame(form)
        buttons.grid(row=7, column=1, columnspan=2, pady=10, sticky="w")
        self.query_button = ttk.Button(
            buttons, text="Get Batch", command=lambda: self._start_endpoint_query("batch"),
        )
        self.query_button.pack(side="left")
        self.reading_button = ttk.Button(
            buttons, text="Get Batch Last Reading",
            command=lambda: self._start_endpoint_query("last_reading"),
        )
        self.reading_button.pack(side="left", padx=(8, 0))
        self.all_readings_button = ttk.Button(
            buttons, text="Get Batch All Readings",
            command=lambda: self._start_endpoint_query("all_readings"),
        )
        self.all_readings_button.pack(side="left", padx=(8, 0))
        self.tracker_button = ttk.Button(
            buttons, text="Get Batch Brew Tracker",
            command=lambda: self._start_endpoint_query("brew_tracker"),
        )
        self.tracker_button.pack(side="left", padx=(8, 0))
        self.legacy_query_button = ttk.Button(
            buttons, text="API abfragen (Batch + Reading)", command=self._start_query,
        )
        self.legacy_query_button.pack(side="left", padx=(8, 0))
        self.action_buttons = (
            self.query_button, self.reading_button,
            self.all_readings_button, self.tracker_button, self.legacy_query_button,
        )
        self.list_button = ttk.Button(
            buttons, text="Alle Batches laden", command=self._start_list_batches,
        )
        self.list_button.pack(side="left", padx=8)
        ttk.Button(buttons, text="Eingaben löschen", command=self._clear).pack(side="left", padx=8)

        list_frame = ttk.LabelFrame(self, text="Batches")
        list_frame.grid(row=1, column=0, padx=12, pady=(0, 8), sticky="nsew")
        list_frame.columnconfigure(0, weight=1)
        list_frame.rowconfigure(0, weight=1)
        self.batch_table = ttk.Treeview(
            list_frame, columns=("recipe", "name", "id", "status"), show="headings",
            selectmode="browse",
        )
        self.batch_table.heading("recipe", text="Recipe Name")
        self.batch_table.heading("name", text="Name")
        self.batch_table.heading("id", text="ID")
        self.batch_table.heading("status", text="Status")
        self.batch_table.column("recipe", width=280, anchor="w")
        self.batch_table.column("name", width=260, anchor="w")
        self.batch_table.column("id", width=260, anchor="w")
        self.batch_table.column("status", width=140, anchor="w")
        for status, (background, foreground) in STATUS_COLORS.items():
            self.batch_table.tag_configure(
                status, background=background, foreground=foreground,
            )
        self.batch_table.tag_configure(
            "unknown", background="#f8fafc", foreground="#475569",
        )
        self.batch_table.grid(row=0, column=0, sticky="nsew")
        self.batch_table.bind("<Double-1>", self._copy_batch_id)
        table_scrollbar = ttk.Scrollbar(list_frame, command=self.batch_table.yview)
        table_scrollbar.grid(row=0, column=1, sticky="ns")
        self.batch_table.configure(yscrollcommand=table_scrollbar.set)

        result_frame = ttk.LabelFrame(self, text="Ergebnis")
        result_frame.grid(row=2, column=0, padx=12, pady=(0, 12), sticky="ew")
        result_frame.columnconfigure(0, weight=1)
        result_frame.rowconfigure(0, weight=1)
        self.output = tk.Text(result_frame, wrap="word", state="disabled", height=10)
        self.output.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(result_frame, command=self.output.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.output.configure(yscrollcommand=scrollbar.set)
        self.progress = ttk.Label(result_frame, text="Bereit", anchor="w")
        self.progress.grid(row=1, column=0, columnspan=2, padx=8, pady=(6, 8), sticky="ew")

        right_panel = ttk.Frame(self)
        right_panel.grid(row=1, column=1, rowspan=2, padx=(0, 12), pady=(0, 12), sticky="nsew")
        right_panel.columnconfigure(0, weight=1)
        right_panel.rowconfigure(1, weight=1)

        request_frame = ttk.LabelFrame(right_panel, text="API Requests")
        request_frame.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        request_frame.columnconfigure(0, weight=1)
        request_frame.rowconfigure(0, weight=1)
        self.request_output = tk.Text(
            request_frame, wrap="none", state="disabled", height=5,
            background="#1e1e1e", foreground="#d4d4d4",
            insertbackground="#ffffff", padx=8, pady=6,
        )
        self.request_output.grid(row=0, column=0, sticky="nsew")
        request_scrollbar = ttk.Scrollbar(request_frame, command=self.request_output.yview)
        request_scrollbar.grid(row=0, column=1, sticky="ns")
        self.request_output.configure(yscrollcommand=request_scrollbar.set)
        self.request_output.tag_configure("method", foreground="#4ec9b0")
        self.request_output.tag_configure("url", foreground="#9cdcfe")

        json_frame = ttk.LabelFrame(right_panel, text="JSON Response")
        json_frame.grid(row=1, column=0, sticky="nsew")
        json_frame.columnconfigure(0, weight=1)
        json_frame.rowconfigure(0, weight=1)
        self.json_output = tk.Text(
            json_frame, wrap="none", state="disabled", undo=False,
            background="#1e1e1e", foreground="#d4d4d4",
            insertbackground="#ffffff", padx=10, pady=8,
        )
        self.json_output.grid(row=0, column=0, sticky="nsew")
        json_y_scrollbar = ttk.Scrollbar(json_frame, command=self.json_output.yview)
        json_y_scrollbar.grid(row=0, column=1, sticky="ns")
        json_x_scrollbar = ttk.Scrollbar(
            json_frame, orient="horizontal", command=self.json_output.xview,
        )
        json_x_scrollbar.grid(row=1, column=0, sticky="ew")
        self.json_output.configure(
            yscrollcommand=json_y_scrollbar.set, xscrollcommand=json_x_scrollbar.set,
        )
        self.json_output.tag_configure("key", foreground="#9cdcfe")
        self.json_output.tag_configure("string", foreground="#ce9178")
        self.json_output.tag_configure("number", foreground="#b5cea8")
        self.json_output.tag_configure("literal", foreground="#569cd6")

    @staticmethod
    def _entry(parent, row, label, variable, show=None, save_key=None):
        ttk.Label(parent, text=label).grid(row=row, column=0, padx=8, pady=4, sticky="w")
        entry = ttk.Entry(parent, textvariable=variable, show=show or "")
        entry.grid(row=row, column=1, columnspan=1, padx=8, pady=4, sticky="ew")
        return entry

    def _save_credential(self, key):
        value = self.user_id.get().strip() if key == "user_id" else self.api_key.get().strip()
        if not value:
            messagebox.showerror("Eingabe fehlt", "Bitte zuerst einen Wert eintragen.")
            return
        try:
            path = save_config_value(key, value)
        except OSError as error:
            messagebox.showerror("Speichern fehlgeschlagen", str(error))
            return
        messagebox.showinfo("Gespeichert", f"Der Wert wurde in\n{path}\ngespeichert.")

    def _toggle_key(self):
        self.key_entry.configure(show="" if self.show_key.get() else "*")

    def _clear(self):
        self.user_id.set("")
        self.api_key.set("")
        self.batch_id.set("")
        self._write_output("")
        self._write_requests("")
        self._write_json("")
        for item in self.batch_table.get_children():
            self.batch_table.delete(item)
        self.progress.configure(text="Bereit")

    def _copy_batch_id(self, event):
        item_id = self.batch_table.identify_row(event.y)
        if not item_id:
            return
        values = self.batch_table.item(item_id, "values")
        if len(values) > 2 and values[2]:
            self.batch_id.set(values[2])
            self.progress.configure(text="Batch-ID übernommen")

    def _write_output(self, text):
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.insert("1.0", text)
        self.output.configure(state="disabled")

    def _write_requests(self, text):
        self.request_output.configure(state="normal")
        self.request_output.delete("1.0", "end")
        self.request_output.insert("1.0", text)
        self.request_output.tag_remove("method", "1.0", "end")
        self.request_output.tag_remove("url", "1.0", "end")
        for line_number, line in enumerate(text.splitlines(), start=1):
            self.request_output.tag_add("method", f"{line_number}.0", f"{line_number}.3")
            self.request_output.tag_add(
                "url", f"{line_number}.4", f"{line_number}.end",
            )
        self.request_output.configure(state="disabled")

    def _append_request(self, method, url):
        current = self.request_output.get("1.0", "end-1c")
        self._write_requests(f"{current + chr(10) if current else ''}{method} {url}")

    def _request_callback(self, method, url):
        self.after(0, self._append_request, method, url)

    def _write_json(self, text):
        self.json_output.configure(state="normal")
        self.json_output.delete("1.0", "end")
        self.json_output.insert("1.0", text)
        for tag in ("key", "string", "number", "literal"):
            self.json_output.tag_remove(tag, "1.0", "end")
        if text:
            self._highlight_json(text)
        self.json_output.configure(state="disabled")

    def _highlight_json(self, text):
        token_pattern = re.compile(
            r'(?P<string>"(?:\\.|[^"\\])*")|'
            r"(?P<number>-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)|"
            r"(?P<literal>\b(?:true|false|null)\b)"
        )
        for match in token_pattern.finditer(text):
            tag = match.lastgroup
            start = f"1.0 + {match.start()} chars"
            end = f"1.0 + {match.end()} chars"
            if tag == "string" and re.match(r"\s*:", text[match.end():]):
                tag = "key"
            self.json_output.tag_add(tag, start, end)

    def _start_query(self):
        if not self.user_id.get().strip() or not self.api_key.get().strip():
            messagebox.showerror("Zugangsdaten fehlen", "Bitte User-ID und API-Key eintragen.")
            return
        try:
            timeout = float(self.timeout.get())
            if timeout <= 0:
                raise ValueError
        except ValueError:
            messagebox.showerror("Ungültiger Timeout", "Der Timeout muss eine positive Zahl sein.")
            return

        values = {
            "user_id": self.user_id.get().strip(),
            "api_key": self.api_key.get().strip(),
            "status": self.status.get().strip() or "Fermenting",
            "batch_id": self.batch_id.get().strip(),
            "base_url": self.base_url.get().strip() or api.DEFAULT_BASE_URL,
            "timeout": timeout,
            "save_json": self.save_json.get(),
            "save_full_json": self.save_full_json.get(),
            "save_html": self.save_html.get(),
        }
        for key, title, extension, filetypes in (
            ("save_json", "Reduziertes JSON speichern", ".json", [("JSON-Datei", "*.json")]),
            ("save_full_json", "Vollständiges JSON speichern", ".json", [("JSON-Datei", "*.json")]),
            ("save_html", "HTML speichern", ".html", [("HTML-Datei", "*.html")]),
        ):
            if values[key]:
                path = filedialog.asksaveasfilename(
                    title=title, defaultextension=extension,
                    filetypes=filetypes + [("Alle Dateien", "*.*")],
                )
                if not path:
                    values[key] = False
                else:
                    values[f"{key}_path"] = path
        for button in self.action_buttons:
            button.configure(state="disabled")
        self.list_button.configure(state="disabled")
        self.progress.configure(text="API-Abfrage läuft ...")
        self._write_output("")
        self._write_requests("")
        self._write_json("")
        threading.Thread(target=self._query_worker, args=(values,), daemon=True).start()

    def _start_endpoint_query(self, endpoint):
        if not self.user_id.get().strip() or not self.api_key.get().strip():
            messagebox.showerror("Zugangsdaten fehlen", "Bitte User-ID und API-Key eintragen.")
            return
        batch_id = self.batch_id.get().strip()
        if not batch_id:
            messagebox.showerror(
                "Batch-ID fehlt",
                "Bitte eine Batch-ID eintragen oder per Doppelklick aus der Liste übernehmen.",
            )
            return
        try:
            timeout = float(self.timeout.get())
            if timeout <= 0:
                raise ValueError
        except ValueError:
            messagebox.showerror("Ungültiger Timeout", "Der Timeout muss eine positive Zahl sein.")
            return
        values = {
            "endpoint": endpoint,
            "base_url": self.base_url.get().strip() or api.DEFAULT_BASE_URL,
            "batch_id": batch_id,
            "user_id": self.user_id.get().strip(),
            "api_key": self.api_key.get().strip(),
            "timeout": timeout,
        }
        for button in self.action_buttons:
            button.configure(state="disabled")
        self.list_button.configure(state="disabled")
        self.progress.configure(text="API-Request läuft ...")
        self._write_output("")
        self._write_requests("")
        self._write_json("")
        threading.Thread(target=self._endpoint_worker, args=(values,), daemon=True).start()

    def _endpoint_worker(self, values):
        functions = {
            "batch": api.get_batch,
            "last_reading": api.get_last_reading,
            "all_readings": api.get_all_readings,
            "brew_tracker": api.get_brew_tracker,
        }
        labels = {
            "batch": "Get Batch",
            "last_reading": "Get Batch Last Reading",
            "all_readings": "Get Batch All Readings",
            "brew_tracker": "Get Batch Brew Tracker",
        }
        try:
            response = functions[values["endpoint"]](
                values["base_url"], values["batch_id"], values["user_id"],
                values["api_key"], values["timeout"],
                request_callback=self._request_callback,
            )
            self.after(
                0, self._finish,
                f"{labels[values['endpoint']]} erfolgreich abgefragt.",
                False, json.dumps(response, indent=2, ensure_ascii=False),
            )
        except Exception as error:
            self.after(0, self._finish, f"Fehler: {error}", True)

    def _start_list_batches(self):
        if not self.user_id.get().strip() or not self.api_key.get().strip():
            messagebox.showerror("Zugangsdaten fehlen", "Bitte User-ID und API-Key eintragen.")
            return
        try:
            timeout = float(self.timeout.get())
            if timeout <= 0:
                raise ValueError
        except ValueError:
            messagebox.showerror("Ungültiger Timeout", "Der Timeout muss eine positive Zahl sein.")
            return
        values = (
            self.base_url.get().strip() or api.DEFAULT_BASE_URL,
            self.user_id.get().strip(), self.api_key.get().strip(), timeout,
        )
        for button in self.action_buttons:
            button.configure(state="disabled")
        self.list_button.configure(state="disabled")
        self.progress.configure(text="Alle Batches werden geladen ...")
        self._write_output("")
        self._write_requests("")
        self._write_json("")
        threading.Thread(target=self._list_batches_worker, args=(values,), daemon=True).start()

    def _list_batches_worker(self, values):
        try:
            batches = api.get_all_batches(*values, request_callback=self._request_callback)
            self.after(0, self._show_batches, batches)
        except Exception as error:
            self.after(0, self._finish, f"Fehler: {error}", True)

    def _show_batches(self, batches):
        for item in self.batch_table.get_children():
            self.batch_table.delete(item)
        sorted_batches = sorted(
            enumerate(batches),
            key=lambda item: self._batch_sort_key(item[1]),
        )
        for _, batch in sorted_batches:
            if isinstance(batch, dict):
                status = batch.get("status", "")
                recipe = batch.get("recipe")
                recipe_name = recipe.get("name", "") if isinstance(recipe, dict) else ""
                self.batch_table.insert(
                    "", "end",
                    values=(recipe_name, batch.get("name", ""), batch.get("_id", ""), status),
                    tags=(status if status in STATUS_COLORS else "unknown",),
                )
        self._finish(
            f"{len(batches)} Batches geladen.",
            False,
            json.dumps({"batches_response": batches}, indent=2, ensure_ascii=False),
        )

    @staticmethod
    def _batch_sort_key(batch):
        if not isinstance(batch, dict):
            return (99, "")
        status = batch.get("status", "")
        recipe = batch.get("recipe")
        recipe_name = recipe.get("name", "") if isinstance(recipe, dict) else ""
        return (STATUS_ORDER.get(status, 99), recipe_name.casefold())

    def _query_worker(self, values):
        try:
            batches_response = None
            full_requested = values["save_full_json"] or values["save_html"]
            if values["batch_id"]:
                batch = {"_id": values["batch_id"], "name": "(direkte Batch-ID)"}
                if full_requested:
                    batch = api.get_batch(
                        values["base_url"], values["batch_id"], values["user_id"],
                        values["api_key"], values["timeout"],
                        request_callback=self._request_callback,
                    )
                    batches_response = [batch]
            else:
                batches_response = api.get_batches(
                    values["base_url"], values["status"], values["user_id"],
                    values["api_key"], values["timeout"], complete=full_requested,
                    request_callback=self._request_callback,
                )
                if not isinstance(batches_response, list) or not batches_response:
                    raise LookupError(f"Keine Batches mit Status '{values['status']}' gefunden.")
                batch = batches_response[0]

            batch_id = batch.get("_id")
            if not batch_id:
                raise ValueError("Die Batch-Antwort enthält keine _id.")
            try:
                reading = api.get_last_reading(
                    values["base_url"], batch_id, values["user_id"],
                    values["api_key"], values["timeout"],
                    request_callback=self._request_callback,
                )
                reading_missing = False
            except HTTPError as error:
                if error.code != 404:
                    raise
                reading = {}
                reading_missing = True
            temperature = reading.get("temp") if isinstance(reading, dict) else None
            if not reading_missing and not isinstance(temperature, (int, float)):
                raise ValueError("Die Messwert-Antwort enthält kein numerisches temp-Feld.")

            lines = [
                f"Batch: {batch.get('name', '<ohne Namen>')} ({batch_id})",
                f"Status: {batch['status']}" if "status" in batch else None,
                "Kein letzter Messwert vorhanden." if reading_missing else f"Temperatur: {temperature:.1f} C",
                f"SG: {reading['sg']}" if "sg" in reading else None,
                f"Sensor: {reading['type']}" if "type" in reading else None,
            ]
            exports = self._save_exports(values, batch, reading, batches_response)
            lines.extend(exports)
            response = {"batch": batch, "last_reading_response": reading}
            if batches_response is not None:
                response["batches_response"] = batches_response
            self.after(
                0, self._finish, "\n".join(line for line in lines if line),
                False, json.dumps(response, indent=2, ensure_ascii=False),
            )
        except Exception as error:
            self.after(0, self._finish, f"Fehler: {error}", True)

    @staticmethod
    def _save_exports(values, batch, reading, batches_response):
        lines = []
        if values["save_json"]:
            path = values["save_json_path"]
            api.save_redacted_response(path, batch, reading)
            lines.append(f"Redigiertes JSON gespeichert: {path}")
        full = {"batches_response": batches_response, "last_reading_response": reading}
        if values["save_full_json"]:
            path = values["save_full_json_path"]
            api.save_full_json(path, full)
            lines.append(f"Vollständiges JSON gespeichert: {path}")
        if values["save_html"]:
            path = values["save_html_path"]
            api.save_html(path, full)
            lines.append(f"Navigierbares HTML gespeichert: {path}")
        return lines

    def _finish(self, text, failed=False, json_text=None):
        self._write_output(text)
        if json_text is not None:
            self._write_json(json_text)
        self.progress.configure(text="Fehler" if failed else "Abfrage abgeschlossen")
        for button in self.action_buttons:
            button.configure(state="normal")
        self.list_button.configure(state="normal")


if __name__ == "__main__":
    BrewfatherGui().mainloop()

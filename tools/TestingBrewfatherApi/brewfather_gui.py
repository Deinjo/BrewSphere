#!/usr/bin/env python3
"""Simple graphical front end for test_brewfather_api.py."""

import json
import math
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from datetime import datetime, timedelta, timezone
from pathlib import Path
import re
from urllib.error import HTTPError
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
from PIL import Image, ImageTk

import test_brewfather_api as api

TOOL_VERSION = "1.19.0"
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
        self.batch_table.bind("<<TreeviewSelect>>", self._select_batch_id)
        self.batch_table.bind("<Double-1>", self._get_selected_batch)
        table_scrollbar = ttk.Scrollbar(list_frame, command=self.batch_table.yview)
        table_scrollbar.grid(row=0, column=1, sticky="ns")
        self.batch_table.configure(yscrollcommand=table_scrollbar.set)

        right_panel = ttk.Notebook(self)
        right_panel.grid(row=1, column=1, rowspan=2, padx=(0, 12), pady=(0, 12), sticky="nsew")
        api_tab = ttk.Frame(right_panel)
        chart_tab = ttk.Frame(right_panel)
        mash_tab = ttk.Frame(right_panel)
        right_panel.add(api_tab, text="API-Request/-Response")
        right_panel.add(chart_tab, text="Diagramm")
        api_tab.columnconfigure(0, weight=1)
        api_tab.rowconfigure(1, weight=1)
        chart_tab.columnconfigure(0, weight=1)
        chart_tab.rowconfigure(0, weight=1)
        profile_tabs = ttk.Notebook(chart_tab)
        profile_tabs.grid(row=0, column=0, sticky="nsew")
        fermentation_tab = ttk.Frame(profile_tabs)
        mash_tab = ttk.Frame(profile_tabs)
        profile_tabs.add(fermentation_tab, text="Fermentation")
        profile_tabs.add(mash_tab, text="Maischen")
        fermentation_tab.columnconfigure(0, weight=1)
        fermentation_tab.rowconfigure(0, weight=1)
        mash_tab.columnconfigure(0, weight=1)
        mash_tab.rowconfigure(0, weight=1)

        chart_frame = ttk.LabelFrame(fermentation_tab, text="Fermentationsprofil")
        chart_frame.grid(row=0, column=0, sticky="nsew")
        chart_frame.columnconfigure(0, weight=1)
        chart_frame.rowconfigure(0, weight=1)
        self.chart_canvas = tk.Canvas(
            chart_frame, background="#ffffff", highlightthickness=1,
            highlightbackground="#b7c1cc", height=260,
        )
        self.chart_canvas.grid(row=0, column=0, sticky="nsew")
        self.chart_steps = []
        self.chart_canvas.bind("<Configure>", lambda _event: self._draw_chart())
        self.step_table = ttk.Treeview(
            chart_frame,
            columns=("actual", "time", "cumulative", "temperature", "name", "pressure", "type"),
            show="headings", height=8,
        )
        step_columns = (
            ("actual", "ActualTime (Berlin)", 150),
            ("time", "StepTime", 80),
            ("cumulative", "StepTimeKumiliert", 130),
            ("temperature", "StepTemp", 80),
            ("name", "Name", 420),
            ("pressure", "displayPressure", 120),
            ("type", "Type", 120),
        )
        for column, heading, width in step_columns:
            self.step_table.heading(column, text=heading)
            self.step_table.column(column, width=width, anchor="w")
        self.step_table.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        step_scrollbar = ttk.Scrollbar(chart_frame, command=self.step_table.yview)
        step_scrollbar.grid(row=1, column=1, sticky="ns", pady=(8, 0))
        self.step_table.configure(yscrollcommand=step_scrollbar.set)

        mash_frame = ttk.LabelFrame(mash_tab, text="Maischprofil")
        mash_frame.grid(row=0, column=0, sticky="nsew")
        mash_frame.columnconfigure(0, weight=1)
        mash_frame.rowconfigure(0, weight=1)
        self.mash_canvas = tk.Canvas(
            mash_frame, background="#ffffff", highlightthickness=1,
            highlightbackground="#b7c1cc", height=260,
        )
        self.mash_canvas.grid(row=0, column=0, sticky="nsew")
        self.mash_steps = []
        self.mash_canvas.bind("<Configure>", lambda _event: self._draw_mash_chart())
        self.mash_table = ttk.Treeview(
            mash_frame,
            columns=("time", "cumulative", "temperature", "name"),
            show="headings", height=8,
        )
        mash_columns = (
            ("time", "StepTime", 80),
            ("cumulative", "StepTimeKumiliert", 130),
            ("temperature", "StepTemp", 80),
            ("name", "Name", 420),
        )
        for column, heading, width in mash_columns:
            self.mash_table.heading(column, text=heading)
            self.mash_table.column(column, width=width, anchor="w")
        self.mash_table.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        mash_scrollbar = ttk.Scrollbar(mash_frame, command=self.mash_table.yview)
        mash_scrollbar.grid(row=1, column=1, sticky="ns", pady=(8, 0))
        self.mash_table.configure(yscrollcommand=mash_scrollbar.set)

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

        request_frame = ttk.LabelFrame(api_tab, text="API Requests")
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

        json_frame = ttk.LabelFrame(api_tab, text="JSON Response")
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
        for table in (self.step_table, self.mash_table):
            for item in table.get_children():
                table.delete(item)
        self.chart_steps = []
        self.mash_steps = []
        self.progress.configure(text="Bereit")

    def _select_batch_id(self, event=None):
        item_id = self.batch_table.identify_row(event.y) if event is not None else ""
        if not item_id:
            selection = self.batch_table.selection()
            item_id = selection[0] if selection else ""
        if not item_id:
            return
        values = self.batch_table.item(item_id, "values")
        if len(values) > 2 and values[2]:
            self.batch_id.set(values[2])
            self.progress.configure(text="Batch-ID übernommen")

    def _get_selected_batch(self, event):
        self._select_batch_id(event)
        if self.batch_id.get().strip():
            self._start_endpoint_query("batch")
        return "break"

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

    def _draw_chart(
        self, canvas=None, steps=None, x_label="Tage", special_99=True,
        staircase=False, y_min=0.0, temperature_bands=None,
    ):
        canvas = self.chart_canvas if canvas is None else canvas
        steps = self.chart_steps if steps is None else steps
        canvas.delete("all")
        width = canvas.winfo_width()
        height = canvas.winfo_height()
        if width < 120 or height < 100:
            return

        left, right, top, bottom = 58, 18, 18, 42
        plot_width = width - left - right
        plot_height = height - top - bottom
        max_temperature = max((step["temperature"] for step in steps), default=y_min)
        y_max = max(y_min + 2.0, math.ceil((max_temperature + 3.0) / 2.0) * 2.0)
        x_max = max(
            1.0,
            math.ceil(max((step["cumulative"] for step in steps), default=1.0)),
        )
        special_last_step = bool(special_99 and
            steps and math.isclose(steps[-1]["duration"], 99.0)
        )
        if special_last_step:
            last_step = steps[-1]
            x_max = last_step["cumulative"] - last_step["duration"] + 6.0

        def x_position(day):
            return left + (day / x_max) * plot_width

        def y_position(temperature):
            return top + ((y_max - temperature) / (y_max - y_min)) * plot_height

        for band in temperature_bands or ():
            lower, upper, background, label_color, label = band
            canvas.create_rectangle(
                left, y_position(upper), width - right, y_position(lower),
                fill=background, outline="",
            )
            canvas.create_text(
                left + plot_width / 2,
                (y_position(lower) + y_position(upper)) / 2,
                text=label,
                fill=label_color,
                font=("Segoe UI", 10, "bold"),
            )

        for temperature in range(int(y_min), int(y_max) + 1, 2):
            y = y_position(temperature)
            canvas.create_line(left, y, width - right, y, fill="#d8dee6")
            canvas.create_text(
                left - 8, y, text=str(temperature), anchor="e",
                fill="#334155", font=("Segoe UI", 9),
            )

        canvas.create_line(left, top, left, height - bottom, fill="#334155", width=1)
        canvas.create_line(
            left, height - bottom, width - right, height - bottom,
            fill="#334155", width=1,
        )
        x_tick_step = max(1, math.ceil(x_max / 8))
        for day in range(0, int(x_max) + 1, x_tick_step):
            x = x_position(day)
            canvas.create_line(
                x, height - bottom, x, height - bottom + 5, fill="#334155",
            )
            canvas.create_text(
                x, height - bottom + 17, text=str(day), anchor="n",
                fill="#334155", font=("Segoe UI", 9),
            )

        canvas.create_text(
            width / 2, height - 8, text=x_label, anchor="s",
            fill="#1f2937", font=("Segoe UI", 10, "bold"),
        )
        canvas.create_text(
            13, top + plot_height / 2, text="Temperatur [°C]", angle=90,
            fill="#1f2937", font=("Segoe UI", 10, "bold"),
        )

        if len(steps) >= 2 or (staircase and steps):
            if special_last_step:
                last_step = steps[-1]
                split_day = last_step["cumulative"] - last_step["duration"] + 5.0
                line_steps = steps[:-1]
                points = [
                    coordinate
                    for step in line_steps
                    for coordinate in (x_position(step["cumulative"]), y_position(step["temperature"]))
                ]
                points.extend((x_position(split_day), y_position(last_step["temperature"])))
                canvas.create_line(*points, fill="#d62828", width=3)
                canvas.create_line(
                    x_position(split_day), y_position(last_step["temperature"]),
                    x_position(x_max), y_position(last_step["temperature"]),
                    fill="#d62828", width=3, dash=(7, 4),
                )
            else:
                if staircase:
                    points = []
                    phase_boundaries = []
                    previous_temperature = None
                    for step in steps:
                        phase_end = step["cumulative"]
                        phase_start = phase_end - step["duration"]
                        temperature = step["temperature"]
                        if previous_temperature is None:
                            points.extend((x_position(phase_start), y_position(temperature)))
                        else:
                            points.extend((x_position(phase_start), y_position(previous_temperature)))
                            points.extend((x_position(phase_start), y_position(temperature)))
                        points.extend((x_position(phase_end), y_position(temperature)))
                        phase_boundaries.append((phase_start, phase_end, temperature, step["name"]))
                        previous_temperature = temperature
                else:
                    points = [
                        coordinate
                        for step in steps
                        for coordinate in (x_position(step["cumulative"]), y_position(step["temperature"]))
                    ]
                canvas.create_line(*points, fill="#d62828", width=3)
                if staircase:
                    for phase_start, phase_end, temperature, name in phase_boundaries:
                        if name and phase_end > phase_start:
                            canvas.create_text(
                                x_position((phase_start + phase_end) / 2),
                                y_position(temperature) - 10,
                                text=name,
                                fill="#7f1d1d",
                                font=("Segoe UI", 8),
                            )
            marker_steps = steps[:-1] if special_last_step else steps
            for step in marker_steps:
                x, y = x_position(step["cumulative"]), y_position(step["temperature"])
                canvas.create_oval(x - 4, y - 4, x + 4, y + 4, fill="#d62828", outline="#ffffff")
            if special_last_step:
                last_step = steps[-1]
                x, y = x_position(x_max), y_position(last_step["temperature"])
                canvas.create_oval(x - 4, y - 4, x + 4, y + 4, fill="#d62828", outline="#ffffff")
        elif steps:
            step = steps[0]
            x, y = x_position(step["cumulative"]), y_position(step["temperature"])
            canvas.create_oval(x - 4, y - 4, x + 4, y + 4, fill="#d62828", outline="")

    def _draw_mash_chart(self):
        self._draw_chart(
            self.mash_canvas, self.mash_steps,
            x_label="Minuten", special_99=False, staircase=True, y_min=50.0,
            temperature_bands=(
                (60.0, 65.0, "#fff8d8", "#9a7600", "Beta-Amylase"),
                (70.0, 75.0, "#eaf4ff", "#24527a", "Alpha-Amylase"),
            ),
        )

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
                "Bitte eine Batch-ID eintragen oder per Klick aus der Liste übernehmen.",
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
            chart_steps = self._fermentation_steps(response) if values["endpoint"] == "batch" else None
            mash_steps = self._mash_steps(response) if values["endpoint"] == "batch" else None
            result_text = f"{labels[values['endpoint']]} erfolgreich abgefragt."
            if values["endpoint"] == "batch":
                result_text += f" Fermentationsschritte erkannt: {len(chart_steps)}."
            self.after(
                0, self._finish,
                result_text,
                False, json.dumps(response, indent=2, ensure_ascii=False), chart_steps, mash_steps,
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
                self._fermentation_steps(batch),
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

    @staticmethod
    def _fermentation_steps(response):
        if not isinstance(response, dict):
            return []
        fermentation = response.get("fermentation")
        if not isinstance(fermentation, dict):
            recipe = response.get("recipe")
            fermentation = recipe.get("fermentation") if isinstance(recipe, dict) else None
        steps = fermentation.get("steps") if isinstance(fermentation, dict) else None
        return BrewfatherGui._profile_steps(steps)

    @staticmethod
    def _mash_steps(response):
        if not isinstance(response, dict):
            return []
        recipe = response.get("recipe")
        mash = recipe.get("mash") if isinstance(recipe, dict) else None
        steps = mash.get("steps") if isinstance(mash, dict) else None
        return BrewfatherGui._profile_steps(steps)

    @staticmethod
    def _profile_steps(steps):
        if not isinstance(steps, list):
            return []
        values = []
        elapsed_time = 0.0
        for step in steps:
            if not isinstance(step, dict):
                continue
            try:
                duration = float(step["stepTime"])
                temperature = float(step.get("stepTemp", step.get("displayStepTemp")))
            except (KeyError, TypeError, ValueError):
                continue
            if math.isfinite(duration) and math.isfinite(temperature):
                elapsed_time += max(0.0, duration)
                values.append(
                    {
                        "duration": max(0.0, duration),
                        "cumulative": elapsed_time,
                        "temperature": max(0.0, temperature),
                        "actual_time": step.get("actualTime"),
                        "name": str(step.get("name", "")),
                        "pressure": step.get("displayPressure"),
                        "type": str(step.get("type", "")),
                    }
                )
        return values

    @staticmethod
    def _format_number(value):
        if value is None:
            return ""
        try:
            number = float(value)
        except (TypeError, ValueError):
            return str(value)
        return f"{number:g}"

    @staticmethod
    def _format_actual_time(value):
        if value is None:
            return ""
        try:
            utc_time = datetime.fromtimestamp(float(value) / 1000.0, tz=timezone.utc)
        except (TypeError, ValueError, OverflowError, OSError):
            return ""
        try:
            berlin_time = utc_time.astimezone(ZoneInfo("Europe/Berlin"))
        except ZoneInfoNotFoundError:
            year = utc_time.year
            march_end = datetime(year, 3, 31, tzinfo=timezone.utc)
            october_end = datetime(year, 10, 31, tzinfo=timezone.utc)
            dst_start = march_end - timedelta(days=(march_end.weekday() + 1) % 7)
            dst_end = october_end - timedelta(days=(october_end.weekday() + 1) % 7)
            dst_start = dst_start.replace(hour=1)
            dst_end = dst_end.replace(hour=1)
            offset = 2 if dst_start <= utc_time < dst_end else 1
            berlin_time = utc_time + timedelta(hours=offset)
        return berlin_time.strftime("%d.%m.%Y %H:%M:%S")

    def _finish(self, text, failed=False, json_text=None, chart_steps=None, mash_steps=None):
        self._write_output(text)
        if json_text is not None:
            self._write_json(json_text)
        if chart_steps is not None:
            self.chart_steps = chart_steps
            for item in self.step_table.get_children():
                self.step_table.delete(item)
            for step in chart_steps:
                self.step_table.insert(
                    "", "end",
                    values=(
                        self._format_actual_time(step["actual_time"]),
                        self._format_number(step["duration"]),
                        self._format_number(step["cumulative"]),
                        self._format_number(step["temperature"]),
                        step["name"],
                        self._format_number(step["pressure"]),
                        step["type"],
                    ),
                )
            self._draw_chart_when_ready()
        if mash_steps is not None:
            self.mash_steps = mash_steps
            for item in self.mash_table.get_children():
                self.mash_table.delete(item)
            for step in mash_steps:
                self.mash_table.insert(
                    "", "end",
                    values=(
                        self._format_number(step["duration"]),
                        self._format_number(step["cumulative"]),
                        self._format_number(step["temperature"]),
                        step["name"],
                    ),
                )
            self.after_idle(self._draw_mash_chart)
        self.progress.configure(text="Fehler" if failed else "Abfrage abgeschlossen")
        for button in self.action_buttons:
            button.configure(state="normal")
        self.list_button.configure(state="normal")

    def _draw_chart_when_ready(self, attempt=0):
        if (
            (self.chart_canvas.winfo_width() < 120 or self.chart_canvas.winfo_height() < 100)
            and attempt < 20
        ):
            self.after(50, self._draw_chart_when_ready, attempt + 1)
            return
        self._draw_chart()


if __name__ == "__main__":
    BrewfatherGui().mainloop()

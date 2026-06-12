import csv
import concurrent.futures
import ipaddress
import json
import os
import platform
import queue
import shlex
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from tkinter import filedialog, messagebox

try:
    import customtkinter as ctk
    from PIL import Image
except ModuleNotFoundError as exc:
    missing = exc.name or "GUI dependency"
    print(f"Missing dependency: {missing}. Install with: pip install -r requirements.txt", file=sys.stderr)
    raise SystemExit(1) from exc


ROOT_DIR = Path(__file__).resolve().parents[1]


def executable_path(name: str) -> Path:
    base = ROOT_DIR / "build" / name
    if os.name == "nt":
        exe = base.with_suffix(".exe")
        if exe.exists():
            return exe
    return base


def open_path(path: Path) -> None:
    if not path.exists():
        messagebox.showwarning("File not found", f"File does not exist:\n{path}")
        return
    if os.name == "nt":
        os.startfile(str(path))  # type: ignore[attr-defined]
    elif sys.platform == "darwin":
        subprocess.Popen(["open", str(path)])
    else:
        subprocess.Popen(["xdg-open", str(path)])


def shell_join(args) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline([str(a) for a in args])
    return " ".join(shlex.quote(str(a)) for a in args)


TIER_ORDER = ["all", "starter", "special"] + [f"tier{i}" for i in range(1, 16)]


def local_ipv4_subnets():
    networks = []
    seen = set()
    try:
        _, _, addresses = socket.gethostbyname_ex(socket.gethostname())
    except OSError:
        addresses = []
    for address in addresses:
        try:
            ip = ipaddress.ip_address(address)
        except ValueError:
            continue
        if ip.version != 4 or ip.is_loopback or ip.is_link_local:
            continue
        network = ipaddress.ip_network(f"{ip}/24", strict=False)
        key = str(network)
        if key not in seen:
            seen.add(key)
            networks.append(network)
    return networks


def ping_host(host, timeout_ms=300):
    if os.name == "nt":
        command = ["ping", "-n", "1", "-w", str(timeout_ms), str(host)]
    else:
        seconds = max(1, int((timeout_ms + 999) / 1000))
        command = ["ping", "-c", "1", "-W", str(seconds), str(host)]
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=max(1, timeout_ms / 1000.0 + 1),
        )
        return result.returncode == 0
    except Exception:
        return False


def scan_lan_hosts(timeout_ms=300, workers=64):
    networks = local_ipv4_subnets()
    targets = []
    for network in networks:
        targets.extend(str(host) for host in network.hosts())
    found = []
    if targets:
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            future_to_host = {executor.submit(ping_host, host, timeout_ms): host for host in targets}
            for future in concurrent.futures.as_completed(future_to_host):
                host = future_to_host[future]
                if future.result():
                    found.append(host)
    found = sorted(set(found), key=lambda value: tuple(int(part) for part in value.split(".")))
    return [("localhost", str(os.cpu_count() or 1), "manual")] + [(host, "1", "reachable") for host in found if host != "127.0.0.1"]


def load_recipe_names(data_path: Path):
    with data_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    names = {"Air", "Earth", "Fire", "Water"}
    if isinstance(data, dict):
        names.update(str(name) for name in data.keys())
        for pairs in data.values():
            if isinstance(pairs, list):
                for pair in pairs:
                    if isinstance(pair, list):
                        names.update(str(x) for x in pair if isinstance(x, str))
    elif isinstance(data, list):
        for entry in data:
            if not isinstance(entry, dict):
                continue
            if isinstance(entry.get("name"), str):
                names.add(entry["name"])
                for recipe in entry.get("recipes", []):
                    for element in recipe.get("elements", []):
                        if isinstance(element, str):
                            names.add(element)
            if isinstance(entry.get("result"), str):
                names.add(entry["result"])
                for element in entry.get("ingredients", []):
                    if isinstance(element, str):
                        names.add(element)
    return names


def load_tier_catalog(tiers_path: Path, recipe_names):
    with tiers_path.open("r", encoding="utf-8") as handle:
        catalog = json.load(handle)

    if not isinstance(catalog, dict):
        raise ValueError("tier catalog root must be an object")

    recipe_lower = {name.lower() for name in recipe_names}
    result = {}
    seen = set()
    for tier in TIER_ORDER[1:]:
        values = catalog.get(tier)
        if not isinstance(values, list):
            raise ValueError(f"tier catalog missing list '{tier}'")
        result[tier] = []
        for value in values:
            if not isinstance(value, str):
                raise ValueError(f"tier '{tier}' contains a non-string item")
            key = value.lower()
            if key in seen:
                raise ValueError(f"duplicate tier element: {value}")
            if key not in recipe_lower:
                raise ValueError(f"tier element not found in recipes.json: {value}")
            seen.add(key)
            result[tier].append(value)

    if len(seen) != 720:
        raise ValueError(f"tier catalog must contain 720 unique elements, got {len(seen)}")

    all_values = []
    for tier in TIER_ORDER[1:]:
        all_values.extend(result[tier])
    result["all"] = all_values
    return result


class AlchemyGui(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("Little Alchemy HPC MPI")
        self.geometry("1180x760")
        self.minsize(900, 600)

        ctk.set_appearance_mode("System")
        ctk.set_default_color_theme("blue")

        self.proc = None
        self.log_queue = queue.Queue()
        self.worker_thread = None
        self.last_output_prefix = ROOT_DIR / "results" / "gui_run"
        self.last_format = "png"
        self.preview_pil_image = None
        self.preview_image = None
        self.run_started_at = None

        self._build_vars()
        self._build_layout()
        self._load_targets_silent()
        self._refresh_mode_state()
        self.after(100, self._drain_log_queue)

    def _build_vars(self):
        self.run_kind = ctk.StringVar(value="target")
        self.engine = ctk.StringVar(value="serial")
        self.data_path = ctk.StringVar(value=str(ROOT_DIR / "data" / "recipes.json"))
        self.tiers_path = ctk.StringVar(value=str(ROOT_DIR / "data" / "tiers.json"))
        self.tier_filter = ctk.StringVar(value="all")
        self.target_search = ctk.StringVar(value="")
        self.target = ctk.StringVar(value="Brick")
        self.benchmark_path = ctk.StringVar(value=str(ROOT_DIR / "benchmarks" / "targets.txt"))
        self.algorithm = ctk.StringVar(value="bfs")
        self.search_mode = ctk.StringVar(value="multiple")
        self.limit = ctk.StringVar(value="5")
        self.trace_mode = ctk.StringVar(value="memo")
        self.visual_mode = ctk.StringVar(value="shared")
        self.output_prefix = ctk.StringVar(value=str(ROOT_DIR / "results" / "gui_run"))
        self.image_format = ctk.StringVar(value="png")
        self.max_visual_depth_enabled = ctk.BooleanVar(value=False)
        self.max_visual_depth = ctk.StringVar(value="6")
        self.mpi_np = ctk.StringVar(value="2")
        self.split_depth = ctk.StringVar(value="1")
        self.multi_node_enabled = ctk.BooleanVar(value=False)
        self.manual_host = ctk.StringVar(value="")
        self.baseline_enabled = ctk.BooleanVar(value=False)
        self.baseline_ms = ctk.StringVar(value="")
        self.status = ctk.StringVar(value="Idle")
        self.target_catalog = {"all": ["Brick"]}
        self.filtered_targets = ["Brick"]
        self.host_rows = []

    def _build_layout(self):
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        self.controls = ctk.CTkScrollableFrame(self, width=340, label_text="Controls")
        self.controls.grid(row=0, column=0, sticky="nsew", padx=(12, 6), pady=12)
        self.controls.grid_columnconfigure(0, weight=1)

        self.tabs = ctk.CTkTabview(self)
        self.tabs.grid(row=0, column=1, sticky="nsew", padx=(6, 12), pady=12)
        for tab in ("Log", "Results", "Image", "CSV", "Command"):
            self.tabs.add(tab)
            self.tabs.tab(tab).grid_columnconfigure(0, weight=1)
            self.tabs.tab(tab).grid_rowconfigure(0, weight=1)

        self.log_box = ctk.CTkTextbox(self.tabs.tab("Log"), wrap="word")
        self.log_box.grid(row=0, column=0, sticky="nsew")

        self.result_box = ctk.CTkTextbox(self.tabs.tab("Results"), wrap="word")
        self.result_box.grid(row=0, column=0, sticky="nsew")

        self.image_label = ctk.CTkLabel(self.tabs.tab("Image"), text="Run a search, then open or preview the rendered image.")
        self.image_label.grid(row=0, column=0, sticky="nsew", padx=12, pady=12)

        self.csv_box = ctk.CTkTextbox(self.tabs.tab("CSV"), wrap="none")
        self.csv_box.grid(row=0, column=0, sticky="nsew")

        self.command_box = ctk.CTkTextbox(self.tabs.tab("Command"), wrap="word", height=120)
        self.command_box.grid(row=0, column=0, sticky="nsew")

        row = 0
        row = self._section(row, "Run")
        self._segmented(row, "Run type", self.run_kind, ["target", "benchmark"], self._refresh_mode_state)
        row += 1
        self._segmented(row, "Engine", self.engine, ["serial", "mpi"], self._refresh_mode_state)
        row += 1

        row = self._section(row, "Input")
        self._path_row(row, "Data JSON", self.data_path, self._browse_data, self._load_targets_silent)
        row += 1
        self._path_row(row, "Tier JSON", self.tiers_path, self._browse_tiers, self._load_targets_silent)
        row += 1
        self._option_row(row, "Tier", self.tier_filter, TIER_ORDER, self._apply_target_filter)
        row += 1
        self.search_entry = self._entry_row(row, "Search target", self.target_search)
        self.target_search.trace_add("write", lambda *_: self._apply_target_filter())
        row += 1
        self.target_menu = self._option_row(row, "Target", self.target, ["Brick"])
        row += 1
        self._path_row(row, "Benchmark", self.benchmark_path, self._browse_benchmark, None)
        row += 1

        row = self._section(row, "Search")
        self._option_row(row, "Algorithm", self.algorithm, ["bfs", "dfs"])
        row += 1
        self._option_row(row, "Mode", self.search_mode, ["single", "multiple", "all"], self._refresh_mode_state)
        row += 1
        ctk.CTkLabel(
            self.controls,
            text="Mode all = all direct recipes for the selected element.",
            anchor="w",
            wraplength=300,
        ).grid(row=row, column=0, sticky="ew", padx=8, pady=(0, 4))
        row += 1
        self.limit_entry = self._entry_row(row, "Limit", self.limit)
        row += 1
        self._option_row(row, "Trace mode", self.trace_mode, ["memo", "full"])
        row += 1

        row = self._section(row, "Output")
        self._option_row(row, "Visual mode", self.visual_mode, ["shared", "full"])
        row += 1
        self._option_row(row, "Image format", self.image_format, ["png", "svg"])
        row += 1
        self._path_row(row, "Output prefix", self.output_prefix, self._browse_output_prefix, None)
        row += 1
        depth_frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        depth_frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        depth_frame.grid_columnconfigure(1, weight=1)
        ctk.CTkCheckBox(depth_frame, text="Max visual depth", variable=self.max_visual_depth_enabled).grid(row=0, column=0, sticky="w")
        ctk.CTkEntry(depth_frame, textvariable=self.max_visual_depth, width=80).grid(row=0, column=1, sticky="e")
        row += 1

        row = self._section(row, "MPI")
        self.mpi_np_entry = self._entry_row(row, "Processes", self.mpi_np)
        row += 1
        self.split_depth_entry = self._entry_row(row, "Split depth", self.split_depth)
        row += 1
        ctk.CTkLabel(
            self.controls,
            text="Split depth expands MPI tasks for single/multiple. Mode all ignores it.",
            anchor="w",
            wraplength=300,
        ).grid(row=row, column=0, sticky="ew", padx=8, pady=(0, 4))
        row += 1
        ctk.CTkCheckBox(
            self.controls,
            text="Use multi-node MS-MPI host list",
            variable=self.multi_node_enabled,
            command=self._refresh_mode_state,
        ).grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        row += 1
        manual_frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        manual_frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        manual_frame.grid_columnconfigure(0, weight=1)
        ctk.CTkEntry(manual_frame, textvariable=self.manual_host, placeholder_text="hostname or IP").grid(row=0, column=0, sticky="ew", padx=(0, 4))
        ctk.CTkButton(manual_frame, text="Add", width=54, command=self._add_manual_host).grid(row=0, column=1)
        row += 1
        host_button_grid = ctk.CTkFrame(self.controls, fg_color="transparent")
        host_button_grid.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        host_button_grid.grid_columnconfigure((0, 1, 2), weight=1)
        ctk.CTkButton(host_button_grid, text="Scan LAN", command=self.scan_hosts).grid(row=0, column=0, sticky="ew", padx=(0, 3))
        ctk.CTkButton(host_button_grid, text="Test/Connect", command=self.test_hosts).grid(row=0, column=1, sticky="ew", padx=3)
        ctk.CTkButton(host_button_grid, text="Clear Hosts", command=self.clear_hosts).grid(row=0, column=2, sticky="ew", padx=(3, 0))
        row += 1
        self.hosts_frame = ctk.CTkFrame(self.controls)
        self.hosts_frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        self.hosts_frame.grid_columnconfigure(0, weight=1)
        self._replace_hosts([("localhost", str(os.cpu_count() or 1), "manual")])
        row += 1
        baseline_frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        baseline_frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        baseline_frame.grid_columnconfigure(1, weight=1)
        ctk.CTkCheckBox(baseline_frame, text="Baseline ms", variable=self.baseline_enabled).grid(row=0, column=0, sticky="w")
        ctk.CTkEntry(baseline_frame, textvariable=self.baseline_ms, width=100).grid(row=0, column=1, sticky="e")
        row += 1

        row = self._section(row, "Actions")
        button_grid = ctk.CTkFrame(self.controls, fg_color="transparent")
        button_grid.grid(row=row, column=0, sticky="ew", padx=8, pady=6)
        button_grid.grid_columnconfigure((0, 1), weight=1)
        ctk.CTkButton(button_grid, text="Build", command=self.build_project).grid(row=0, column=0, sticky="ew", padx=(0, 4), pady=4)
        ctk.CTkButton(button_grid, text="Run", command=self.run_search).grid(row=0, column=1, sticky="ew", padx=(4, 0), pady=4)
        ctk.CTkButton(button_grid, text="Stop", command=self.stop_process, fg_color="#b02a37").grid(row=1, column=0, sticky="ew", padx=(0, 4), pady=4)
        ctk.CTkButton(button_grid, text="Reload targets", command=self._load_targets_silent).grid(row=1, column=1, sticky="ew", padx=(4, 0), pady=4)
        row += 1

        open_grid = ctk.CTkFrame(self.controls, fg_color="transparent")
        open_grid.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        open_grid.grid_columnconfigure((0, 1), weight=1)
        ctk.CTkButton(open_grid, text="Open JSON", command=lambda: self._open_output(".json")).grid(row=0, column=0, sticky="ew", padx=(0, 4), pady=3)
        ctk.CTkButton(open_grid, text="Open DOT", command=lambda: self._open_output(".dot")).grid(row=0, column=1, sticky="ew", padx=(4, 0), pady=3)
        ctk.CTkButton(open_grid, text="Open Image", command=self._open_image).grid(row=1, column=0, sticky="ew", padx=(0, 4), pady=3)
        ctk.CTkButton(open_grid, text="Open CSV", command=lambda: self._open_output(".csv")).grid(row=1, column=1, sticky="ew", padx=(4, 0), pady=3)
        row += 1
        ctk.CTkLabel(self.controls, textvariable=self.status, anchor="w").grid(row=row, column=0, sticky="ew", padx=8, pady=(8, 4))

    def _section(self, row, title):
        label = ctk.CTkLabel(self.controls, text=title, font=ctk.CTkFont(weight="bold"))
        label.grid(row=row, column=0, sticky="w", padx=8, pady=(14, 4))
        return row + 1

    def _segmented(self, row, label, variable, values, command=None):
        frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        frame.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(frame, text=label).grid(row=0, column=0, sticky="w", padx=(0, 8))
        ctk.CTkSegmentedButton(frame, values=values, variable=variable, command=lambda _=None: command() if command else None).grid(row=0, column=1, sticky="ew")

    def _option_row(self, row, label, variable, values, command=None):
        frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        frame.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(frame, text=label).grid(row=0, column=0, sticky="w", padx=(0, 8))
        menu = ctk.CTkOptionMenu(frame, values=values, variable=variable, command=lambda _=None: command() if command else None)
        menu.grid(row=0, column=1, sticky="ew")
        return menu

    def _entry_row(self, row, label, variable):
        frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        frame.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(frame, text=label).grid(row=0, column=0, sticky="w", padx=(0, 8))
        entry = ctk.CTkEntry(frame, textvariable=variable)
        entry.grid(row=0, column=1, sticky="ew")
        return entry

    def _path_row(self, row, label, variable, browse_command, extra_command):
        frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        frame.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(frame, text=label).grid(row=0, column=0, sticky="w", padx=(0, 8))
        ctk.CTkEntry(frame, textvariable=variable).grid(row=0, column=1, sticky="ew", padx=(0, 4))
        ctk.CTkButton(frame, text="...", width=34, command=browse_command).grid(row=0, column=2)
        if extra_command:
            ctk.CTkButton(frame, text="Load", width=48, command=extra_command).grid(row=0, column=3, padx=(4, 0))

    def _replace_hosts(self, hosts):
        for child in self.hosts_frame.winfo_children():
            child.destroy()
        self.host_rows = []
        header = ctk.CTkFrame(self.hosts_frame, fg_color="transparent")
        header.grid(row=0, column=0, sticky="ew", padx=4, pady=(4, 0))
        header.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(header, text="Host/IP", anchor="w").grid(row=0, column=0, sticky="ew")
        ctk.CTkLabel(header, text="Slots", width=54).grid(row=0, column=1, padx=4)
        ctk.CTkLabel(header, text="Status", width=82).grid(row=0, column=2, padx=4)
        for host, slots, status in hosts:
            self._append_host_row(host, slots, status)

    def _append_host_row(self, host, slots="1", status="manual"):
        row_index = len(self.host_rows) + 1
        frame = ctk.CTkFrame(self.hosts_frame, fg_color="transparent")
        frame.grid(row=row_index, column=0, sticky="ew", padx=4, pady=2)
        frame.grid_columnconfigure(0, weight=1)
        host_var = ctk.StringVar(value=host)
        slots_var = ctk.StringVar(value=str(slots))
        status_var = ctk.StringVar(value=status)
        ctk.CTkEntry(frame, textvariable=host_var).grid(row=0, column=0, sticky="ew", padx=(0, 4))
        ctk.CTkEntry(frame, textvariable=slots_var, width=54).grid(row=0, column=1, padx=4)
        ctk.CTkLabel(frame, textvariable=status_var, width=82).grid(row=0, column=2, padx=4)
        ctk.CTkButton(frame, text="x", width=28, command=lambda: self._remove_host_row(frame)).grid(row=0, column=3)
        self.host_rows.append({"frame": frame, "host": host_var, "slots": slots_var, "status": status_var})

    def _remove_host_row(self, frame):
        self.host_rows = [row for row in self.host_rows if row["frame"] is not frame]
        frame.destroy()

    def _host_entries(self, connected_only=False):
        entries = []
        for row in self.host_rows:
            host = row["host"].get().strip()
            if not host:
                continue
            status = row["status"].get().strip().lower()
            if connected_only and status != "connected":
                continue
            try:
                slots = int(row["slots"].get().strip())
            except ValueError as exc:
                raise ValueError(f"Invalid slot count for host {host}") from exc
            if slots < 1:
                raise ValueError(f"Invalid slot count for host {host}")
            entries.append((host, slots, status))
        return entries

    def _set_host_status(self, host, status):
        key = host.strip().lower()
        for row in self.host_rows:
            if row["host"].get().strip().lower() == key:
                row["status"].set(status)
                return

    def _add_manual_host(self):
        host = self.manual_host.get().strip()
        if not host:
            return
        for existing, _, _ in self._host_entries(False):
            if existing.lower() == host.lower():
                self.manual_host.set("")
                return
        self._append_host_row(host, "1", "manual")
        self.manual_host.set("")

    def clear_hosts(self):
        self._replace_hosts([])

    def scan_hosts(self):
        self.multi_node_enabled.set(True)
        self.status.set("Scanning LAN...")
        self._log("Scanning LAN for reachable hosts...\n")

        def worker():
            try:
                hosts = scan_lan_hosts()
                self.log_queue.put(("hosts_replace", hosts))
                self.log_queue.put(("log", f"Scan found {len(hosts)} host entries.\n"))
            except Exception as exc:
                self.log_queue.put(("log", f"Scan failed: {exc}\n"))
            finally:
                self.log_queue.put(("status", "Idle"))

        threading.Thread(target=worker, daemon=True).start()

    def _test_single_host(self, host, slots):
        command = ["mpiexec", "-hosts", "1", host, str(slots), "hostname"]
        try:
            result = subprocess.run(
                command,
                cwd=str(ROOT_DIR),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=15,
            )
            return result.returncode == 0, result.stdout.strip()
        except Exception as exc:
            return False, str(exc)

    def test_hosts(self):
        try:
            entries = self._host_entries(False)
        except Exception as exc:
            messagebox.showerror("Invalid host list", str(exc))
            return
        if not entries:
            messagebox.showwarning("No hosts", "Add or scan at least one host first.")
            return
        self.multi_node_enabled.set(True)
        self.status.set("Testing hosts...")
        self._log("Testing MS-MPI host connectivity...\n")

        def worker():
            for host, slots, _ in entries:
                self.log_queue.put(("host_status", (host, "testing")))
                ok, output = self._test_single_host(host, slots)
                self.log_queue.put(("host_status", (host, "connected" if ok else "failed")))
                detail = output.replace("\n", " ").strip()
                self.log_queue.put(("log", f"{host} slots={slots}: {'connected' if ok else 'failed'} {detail}\n"))
            self.log_queue.put(("status", "Idle"))

        threading.Thread(target=worker, daemon=True).start()

    def _browse_data(self):
        path = filedialog.askopenfilename(filetypes=[("JSON files", "*.json"), ("All files", "*.*")])
        if path:
            self.data_path.set(path)
            self._load_targets_silent()

    def _browse_tiers(self):
        path = filedialog.askopenfilename(filetypes=[("JSON files", "*.json"), ("All files", "*.*")])
        if path:
            self.tiers_path.set(path)
            self._load_targets_silent()

    def _browse_benchmark(self):
        path = filedialog.askopenfilename(filetypes=[("Text files", "*.txt"), ("All files", "*.*")])
        if path:
            self.benchmark_path.set(path)

    def _browse_output_prefix(self):
        path = filedialog.asksaveasfilename(initialfile=Path(self.output_prefix.get()).name)
        if path:
            suffix = Path(path).suffix
            if suffix in {".json", ".dot", ".png", ".svg", ".csv"}:
                path = str(Path(path).with_suffix(""))
            self.output_prefix.set(path)

    def _load_targets_silent(self):
        try:
            recipe_names = load_recipe_names(Path(self.data_path.get()))
            self.target_catalog = load_tier_catalog(Path(self.tiers_path.get()), recipe_names)
            self._apply_target_filter()
            self._log(f"Loaded tier catalog with {len(self.target_catalog['all'])} target names.\n")
        except Exception as exc:
            self._log(f"Could not load targets: {exc}\n")

    def _apply_target_filter(self):
        tier = self.tier_filter.get()
        needle = self.target_search.get().strip().lower()
        source = self.target_catalog.get(tier, self.target_catalog.get("all", []))
        targets = [name for name in source if needle in name.lower()]
        if not targets:
            targets = ["<no match>"]
        self.filtered_targets = targets
        self.target_menu.configure(values=targets)
        if self.target.get() not in targets:
            self.target.set(targets[0])

    def _refresh_mode_state(self):
        limit_state = "disabled" if self.search_mode.get() == "all" else "normal"
        self.limit_entry.configure(state=limit_state)
        mpi_state = "normal" if self.engine.get() == "mpi" else "disabled"
        self.mpi_np_entry.configure(state=mpi_state)
        self.split_depth_entry.configure(state=mpi_state)

    def _base_args(self):
        args = [
            "--data", self.data_path.get(),
            "--tiers", self.tiers_path.get(),
            "--algorithm", self.algorithm.get(),
            "--mode", self.search_mode.get(),
            "--trace-mode", self.trace_mode.get(),
            "--visual-mode", self.visual_mode.get(),
            "--output", self.output_prefix.get(),
            "--format", self.image_format.get(),
        ]
        if self.run_kind.get() == "benchmark":
            args.extend(["--benchmark", self.benchmark_path.get()])
        else:
            if self.target.get() == "<no match>":
                raise ValueError("No target matches the current tier/search filter")
            args.extend(["--target", self.target.get()])

        if self.search_mode.get() != "all":
            args.extend(["--limit", self.limit.get()])
        if self.max_visual_depth_enabled.get():
            args.extend(["--max-visual-depth", self.max_visual_depth.get()])
        if self.engine.get() == "mpi":
            args.extend(["--split-depth", self.split_depth.get()])
            if self.baseline_enabled.get() and self.baseline_ms.get().strip():
                args.extend(["--baseline-ms", self.baseline_ms.get().strip()])
        return args

    def _mpi_prefix(self):
        np_value = self.mpi_np.get().strip() or "2"
        if os.name == "nt" and self.multi_node_enabled.get():
            entries = self._host_entries(connected_only=True)
            if not entries:
                raise ValueError("Multi-node is enabled but no host is connected. Run Test/Connect first.")
            args = ["mpiexec", "-hosts", str(len(entries))]
            for host, slots, _ in entries:
                args.extend([host, str(slots)])
            return args
        if os.name == "nt":
            return ["mpiexec", "-n", np_value]
        if shutil.which("mpirun"):
            return ["mpirun", "-np", np_value]
        return ["mpiexec", "-n", np_value]

    def _command_for_run(self):
        if self.engine.get() == "mpi":
            exe = executable_path("alchemy_mpi")
            return self._mpi_prefix() + [str(exe)] + self._base_args()
        exe = executable_path("alchemy_serial")
        return [str(exe)] + self._base_args()

    def _command_for_build(self):
        return [["cmake", "-S", str(ROOT_DIR), "-B", str(ROOT_DIR / "build")], ["cmake", "--build", str(ROOT_DIR / "build")]]

    def build_project(self):
        commands = self._command_for_build()
        self._start_commands(commands, clear=True)

    def run_search(self):
        self.last_output_prefix = Path(self.output_prefix.get())
        self.last_format = self.image_format.get()
        try:
            command = self._command_for_run()
        except Exception as exc:
            messagebox.showerror("Invalid command", str(exc))
            return
        self._start_commands([command], clear=True)

    def _start_commands(self, commands, clear=False):
        if self.proc is not None:
            messagebox.showwarning("Process running", "Stop the current process before starting another one.")
            return
        if clear:
            self._clear_outputs()
        self.run_started_at = time.monotonic()
        self.status.set("Running...")
        self.command_box.delete("1.0", "end")
        for command in commands:
            self.command_box.insert("end", shell_join(command) + "\n")
        self.tabs.set("Log")
        self.worker_thread = threading.Thread(target=self._run_commands_worker, args=(commands,), daemon=True)
        self.worker_thread.start()

    def _run_commands_worker(self, commands):
        try:
            for command in commands:
                self.log_queue.put(("log", f"$ {shell_join(command)}\n"))
                self.proc = subprocess.Popen(
                    command,
                    cwd=str(ROOT_DIR),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                )
                assert self.proc.stdout is not None
                for line in self.proc.stdout:
                    self.log_queue.put(("log", line))
                rc = self.proc.wait()
                self.proc = None
                self.log_queue.put(("log", f"\nProcess exited with code {rc}\n"))
                if rc != 0:
                    self.log_queue.put(("done", False))
                    return
            self.log_queue.put(("done", True))
        except Exception as exc:
            self.proc = None
            self.log_queue.put(("log", f"Error: {exc}\n"))
            self.log_queue.put(("done", False))

    def stop_process(self):
        if self.proc is None:
            return
        try:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(self.proc.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                self.proc.terminate()
        except Exception as exc:
            self._log(f"Stop failed: {exc}\n")

    def _drain_log_queue(self):
        try:
            while True:
                kind, payload = self.log_queue.get_nowait()
                if kind == "log":
                    self._log(payload)
                elif kind == "hosts_replace":
                    self._replace_hosts(payload)
                elif kind == "host_status":
                    host, status = payload
                    self._set_host_status(host, status)
                elif kind == "status":
                    self.status.set(payload)
                elif kind == "done":
                    self.run_started_at = None
                    self.status.set("Done" if payload else "Failed or stopped")
                    if payload:
                        self._load_outputs()
        except queue.Empty:
            pass
        if self.run_started_at is not None:
            elapsed = time.monotonic() - self.run_started_at
            self.status.set(f"Running... {elapsed:.1f}s")
        self.after(100, self._drain_log_queue)

    def _clear_outputs(self):
        for textbox in (self.log_box, self.result_box, self.csv_box, self.command_box):
            textbox.delete("1.0", "end")
        self._reset_image_preview("Run completed outputs will appear here.")

    def _log(self, text):
        self.log_box.insert("end", text)
        self.log_box.see("end")

    def _load_outputs(self):
        prefix = Path(self.output_prefix.get())
        if self.run_kind.get() == "benchmark":
            csv_path = prefix.with_suffix(".csv")
            if csv_path.exists():
                self._load_csv(csv_path)
                self.tabs.set("CSV")
            return

        json_path = prefix.with_suffix(".json")
        if json_path.exists():
            try:
                data = json.loads(json_path.read_text(encoding="utf-8"))
                self.result_box.delete("1.0", "end")
                self.result_box.insert("end", json.dumps(data, indent=2))
                self.tabs.set("Results")
            except Exception as exc:
                self._log(f"Could not parse JSON output: {exc}\n")

        image_path = prefix.with_suffix("." + self.image_format.get())
        if image_path.exists():
            if self.image_format.get() in {"png", "jpg", "jpeg"}:
                self._preview_image(image_path)
            else:
                self._reset_image_preview(f"Image output is ready:\n{image_path}\n\nUse Open Image to view this format.")

    def _load_csv(self, path: Path):
        self.csv_box.delete("1.0", "end")
        try:
            with path.open("r", encoding="utf-8", newline="") as handle:
                rows = list(csv.reader(handle))
            preview = rows[:80]
            widths = [0] * max((len(row) for row in preview), default=0)
            for row in preview:
                for index, value in enumerate(row):
                    widths[index] = max(widths[index], min(len(value), 32))
            for row in preview:
                cells = []
                for index, value in enumerate(row):
                    value = value[:32]
                    cells.append(value.ljust(widths[index]))
                self.csv_box.insert("end", " | ".join(cells) + "\n")
            if len(rows) > len(preview):
                self.csv_box.insert("end", f"\n... {len(rows) - len(preview)} more rows\n")
        except Exception as exc:
            self.csv_box.insert("end", f"Could not load CSV: {exc}\n")

    def _reset_image_preview(self, text):
        self.preview_image = None
        self.preview_pil_image = None
        try:
            self.image_label.destroy()
        except Exception:
            pass
        self.image_label = ctk.CTkLabel(self.tabs.tab("Image"), text=text)
        self.image_label.grid(row=0, column=0, sticky="nsew", padx=12, pady=12)

    def _set_image_preview(self, image):
        self.preview_pil_image = image
        max_w, max_h = 840, 560
        ratio = min(max_w / image.width, max_h / image.height, 1.0)
        size = (max(1, int(image.width * ratio)), max(1, int(image.height * ratio)))
        self.preview_image = ctk.CTkImage(light_image=image, dark_image=image, size=size)
        try:
            self.image_label.destroy()
        except Exception:
            pass
        self.image_label = ctk.CTkLabel(self.tabs.tab("Image"), text="", image=self.preview_image)
        self.image_label.grid(row=0, column=0, sticky="nsew", padx=12, pady=12)

    def _preview_image(self, path: Path):
        try:
            with Image.open(path) as image_file:
                image = image_file.copy()
            self._set_image_preview(image)
        except Exception as exc:
            self._reset_image_preview(f"Could not preview image:\n{exc}")

    def _open_output(self, suffix):
        open_path(Path(self.output_prefix.get()).with_suffix(suffix))

    def _open_image(self):
        open_path(Path(self.output_prefix.get()).with_suffix("." + self.image_format.get()))


if __name__ == "__main__":
    app = AlchemyGui()
    app.mainloop()

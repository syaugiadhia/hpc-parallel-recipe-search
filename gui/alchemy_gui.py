import csv
import concurrent.futures
import ipaddress
import json
import os
import queue
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
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
SLAVE_AGENT_PORT = 50555
SLAVE_DISCOVERY_PORT = 50556
INVITE_TIMEOUT_SECONDS = 30
DISCOVERY_MESSAGE = b"ALCHEMY_HPC_DISCOVER_V1"

# Batas tampilan UI supaya GUI tidak freeze/crash saat output sangat besar
# (program mencetak full tree tiap resep ke stdout, dan JSON hasil bisa multi-MB).
LOG_MAX_LINES = 2000          # baris terakhir yang dipertahankan di kotak Log
RESULT_MAX_CHARS = 200_000    # karakter JSON yang ditampilkan di tab Results (sisanya dipotong)


def local_ipv4_subnets():
    networks = []
    seen = set()
    for address in local_ipv4_addresses():
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


def local_ipv4_addresses():
    addresses = set()
    try:
        _, _, values = socket.gethostbyname_ex(socket.gethostname())
    except OSError:
        values = []
    for value in values:
        try:
            ip = ipaddress.ip_address(value)
        except ValueError:
            continue
        if ip.version == 4 and not ip.is_loopback and not ip.is_link_local:
            addresses.add(value)
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            value = info[4][0]
            ip = ipaddress.ip_address(value)
            if ip.version == 4 and not ip.is_loopback and not ip.is_link_local:
                addresses.add(value)
    except OSError:
        pass
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            value = sock.getsockname()[0]
            ip = ipaddress.ip_address(value)
            if ip.version == 4 and not ip.is_loopback and not ip.is_link_local:
                addresses.add(value)
    except OSError:
        pass
    if os.name == "nt":
        try:
            output = subprocess.check_output(
                ["ipconfig"],
                text=True,
                encoding="utf-8",
                errors="ignore",
                timeout=3,
            )
            for value in re.findall(r"IPv4[^:\n]*:\s*([0-9]+(?:\.[0-9]+){3})", output):
                ip = ipaddress.ip_address(value)
                if ip.version == 4 and not ip.is_loopback and not ip.is_link_local:
                    addresses.add(value)
        except Exception:
            pass
    return sorted(addresses)


def primary_local_ip():
    addresses = local_ipv4_addresses()
    if addresses:
        return addresses[0]
    return "127.0.0.1"


def send_json_line(sock, payload):
    data = (json.dumps(payload) + "\n").encode("utf-8")
    sock.sendall(data)


def recv_json_line(sock, timeout=None):
    if timeout is not None:
        sock.settimeout(timeout)
    chunks = []
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        chunks.append(chunk)
        if b"\n" in chunk:
            break
    if not chunks:
        raise ConnectionError("empty response")
    line = b"".join(chunks).split(b"\n", 1)[0]
    return json.loads(line.decode("utf-8"))


def discover_slave_agents(timeout=1.2):
    discovered = {}
    broadcasts = {"255.255.255.255"}
    for network in local_ipv4_subnets():
        broadcasts.add(str(network.broadcast_address))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(0.2)
        for broadcast in broadcasts:
            try:
                sock.sendto(DISCOVERY_MESSAGE, (broadcast, SLAVE_DISCOVERY_PORT))
            except OSError:
                pass
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                payload = json.loads(data.decode("utf-8"))
            except Exception:
                continue
            if payload.get("type") != "alchemy_slave":
                continue
            host = addr[0]
            discovered[host] = (host, str(payload.get("slots", 1)), "candidate")
    return list(discovered.values())


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
    discovered = {host: (host, slots, status) for host, slots, status in discover_slave_agents()}
    for host in found:
        if host != "127.0.0.1" and host not in discovered:
            discovered[host] = (host, "1", "candidate")
    return list(discovered.values())


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
    def __init__(self, preset=None):
        super().__init__()
        self.title("Little Alchemy HPC MPI")
        self.geometry("1180x760")
        self.minsize(900, 600)

        ctk.set_appearance_mode("System")
        ctk.set_default_color_theme("blue")

        self.proc = None
        self.log_queue = queue.Queue()
        self.worker_thread = None
        self.stop_requested = False
        self.last_output_prefix = ROOT_DIR / "results" / "gui_run"
        self.last_format = "png"
        self.preview_pil_image = None
        self.preview_image = None
        self.preview_json_data = None
        self.preview_recipes = []
        self.preview_page_index = 0
        self.run_started_at = None
        self.slave_stop_event = threading.Event()
        self.slave_server_socket = None
        self.slave_threads = []
        self.pending_invite = None
        self.pending_invite_active = False
        self.pending_invite_lock = threading.Lock()
        self.slave_advertised_slots = str(os.cpu_count() or 1)

        self._build_vars()
        self._build_layout()
        self._load_targets_silent()
        self._refresh_mode_state()
        self._apply_cli_preset(preset)
        self.after(100, self._drain_log_queue)

    def _apply_cli_preset(self, preset):
        """Terapkan preset dari argumen CLI (dipakai oleh run_master/run_slaveN.bat)."""
        if not preset:
            return
        role = (preset.get("role") or "").lower()
        if role == "master":
            self.cluster_role.set("Master")
            self.engine.set("mpi")
            self.multi_node_enabled.set(True)
        elif role == "slave":
            self.cluster_role.set("Slave")
        if preset.get("engine"):
            self.engine.set(preset["engine"])
        if preset.get("slots"):
            self.slave_slots.set(str(preset["slots"]))
        self._refresh_mode_state()
        if preset.get("autostart") and self.cluster_role.get() == "Slave":
            self.after(600, self.start_slave_agent)

    def _build_vars(self):
        self.cluster_role = ctk.StringVar(value="Master")
        self.run_kind = ctk.StringVar(value="target")
        self.engine = ctk.StringVar(value="serial")
        self.data_path = ctk.StringVar(value=str(ROOT_DIR / "data" / "recipes.json"))
        self.tiers_path = ctk.StringVar(value=str(ROOT_DIR / "data" / "tiers.json"))
        self.tier_filter = ctk.StringVar(value="all")
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
        self.openmp_threads = ctk.StringVar(value=str(min(max(os.cpu_count() or 2, 2), 4)))
        self.split_depth = ctk.StringVar(value="1")
        self.multi_node_enabled = ctk.BooleanVar(value=False)
        self.manual_host = ctk.StringVar(value="")
        self.slave_slots = ctk.StringVar(value=str(os.cpu_count() or 1))
        self.slave_status = ctk.StringVar(value="Slave stopped")
        self.incoming_invite_text = ctk.StringVar(value="No incoming master request.")
        self.baseline_enabled = ctk.BooleanVar(value=False)
        self.baseline_ms = ctk.StringVar(value="")
        self.compare_host_order_text = ctk.StringVar(value="Compare host order:\nMaster: not loaded yet")
        self.status = ctk.StringVar(value="Idle")
        self.image_page = ctk.StringVar(value="1")
        self.image_page_status = ctk.StringVar(value="No recipe preview.")
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
        for tab in ("Log", "Results", "Image", "Compare", "CSV", "Command"):
            self.tabs.add(tab)
            self.tabs.tab(tab).grid_columnconfigure(0, weight=1)
            self.tabs.tab(tab).grid_rowconfigure(0, weight=1)

        self.log_box = ctk.CTkTextbox(self.tabs.tab("Log"), wrap="word")
        self.log_box.grid(row=0, column=0, sticky="nsew")

        self.result_box = ctk.CTkTextbox(self.tabs.tab("Results"), wrap="word")
        self.result_box.grid(row=0, column=0, sticky="nsew")

        self.tabs.tab("Image").grid_rowconfigure(0, weight=0)
        self.tabs.tab("Image").grid_rowconfigure(1, weight=1)
        self._build_image_tab()

        self.compare_box = ctk.CTkTextbox(self.tabs.tab("Compare"), wrap="none")
        self.compare_box.grid(row=0, column=0, sticky="nsew")

        self.csv_box = ctk.CTkTextbox(self.tabs.tab("CSV"), wrap="none")
        self.csv_box.grid(row=0, column=0, sticky="nsew")

        self.command_box = ctk.CTkTextbox(self.tabs.tab("Command"), wrap="word", height=120)
        self.command_box.grid(row=0, column=0, sticky="nsew")

        row = 0
        row = self._section(row, "Run")
        self._segmented(row, "Role", self.cluster_role, ["Master", "Slave"], self._refresh_mode_state)
        row += 1
        self._segmented(row, "Run type", self.run_kind, ["target", "benchmark"], self._refresh_mode_state)
        row += 1
        self._segmented(row, "Engine", self.engine, ["serial", "openmp", "mpi"], self._refresh_mode_state)
        row += 1

        row = self._section(row, "Input")
        self._path_row(row, "Data JSON", self.data_path, self._browse_data, self._load_targets_silent)
        row += 1
        self._path_row(row, "Tier JSON", self.tiers_path, self._browse_tiers, self._load_targets_silent)
        row += 1
        self._option_row(row, "Tier", self.tier_filter, TIER_ORDER, self._apply_target_filter)
        row += 1
        self.target_combo = self._combo_row(row, "Target", self.target, ["Brick"])
        self.target_combo.bind("<KeyRelease>", lambda _event: self._apply_target_filter(keep_text=True))
        row += 1
        self._path_row(row, "Benchmark", self.benchmark_path, self._browse_benchmark, None)
        row += 1

        row = self._section(row, "Search")
        self.algorithm_menu = self._option_row(row, "Algorithm", self.algorithm, ["bfs", "dfs"])
        row += 1
        self._option_row(row, "Mode", self.search_mode, ["single", "multiple", "all"], self._refresh_mode_state)
        row += 1
        ctk.CTkLabel(
            self.controls,
            text="Mode all = unique direct recipes + shortest subtree; not all possible combinations.",
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
        self.openmp_threads_entry = self._entry_row(row, "OpenMP threads", self.openmp_threads)
        row += 1
        self.split_depth_entry = self._entry_row(row, "Split depth", self.split_depth)
        row += 1
        ctk.CTkLabel(
            self.controls,
            text="Split depth expands MPI tasks for single/multiple. Mode all uses direct recipes and ignores it.",
            anchor="w",
            wraplength=300,
        ).grid(row=row, column=0, sticky="ew", padx=8, pady=(0, 4))
        row += 1
        ctk.CTkCheckBox(
            self.controls,
            text="Use accepted master/slave host list",
            variable=self.multi_node_enabled,
            command=self._refresh_mode_state,
        ).grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        row += 1
        ctk.CTkLabel(
            self.controls,
            text="Master invites specific slaves. Slave must accept within 30 seconds.",
            anchor="w",
            wraplength=300,
        ).grid(row=row, column=0, sticky="ew", padx=8, pady=(0, 4))
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
        ctk.CTkButton(host_button_grid, text="Connect Listed", command=self.test_hosts).grid(row=0, column=1, sticky="ew", padx=3)
        ctk.CTkButton(host_button_grid, text="Clear Hosts", command=self.clear_hosts).grid(row=0, column=2, sticky="ew", padx=(3, 0))
        row += 1
        self.hosts_frame = ctk.CTkFrame(self.controls)
        self.hosts_frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        self.hosts_frame.grid_columnconfigure(0, weight=1)
        self._replace_hosts([(socket.gethostname(), str(os.cpu_count() or 1), "master")])
        row += 1
        slave_panel = ctk.CTkFrame(self.controls)
        slave_panel.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        slave_panel.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(slave_panel, text="Slave Agent", font=ctk.CTkFont(weight="bold")).grid(row=0, column=0, sticky="w", padx=8, pady=(6, 2))
        ctk.CTkLabel(slave_panel, textvariable=self.slave_status, anchor="w").grid(row=1, column=0, sticky="ew", padx=8, pady=2)
        ctk.CTkLabel(slave_panel, text=f"IP: {primary_local_ip()}  Port: {SLAVE_AGENT_PORT}", anchor="w").grid(row=2, column=0, sticky="ew", padx=8, pady=2)
        slot_frame = ctk.CTkFrame(slave_panel, fg_color="transparent")
        slot_frame.grid(row=3, column=0, sticky="ew", padx=8, pady=2)
        slot_frame.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(slot_frame, text="Offered slots").grid(row=0, column=0, sticky="w", padx=(0, 8))
        ctk.CTkEntry(slot_frame, textvariable=self.slave_slots, width=80).grid(row=0, column=1, sticky="e")
        slave_buttons = ctk.CTkFrame(slave_panel, fg_color="transparent")
        slave_buttons.grid(row=4, column=0, sticky="ew", padx=8, pady=4)
        slave_buttons.grid_columnconfigure((0, 1), weight=1)
        ctk.CTkButton(slave_buttons, text="Start Slave", command=self.start_slave_agent).grid(row=0, column=0, sticky="ew", padx=(0, 4))
        ctk.CTkButton(slave_buttons, text="Stop Slave", command=self.stop_slave_agent).grid(row=0, column=1, sticky="ew", padx=(4, 0))
        ctk.CTkLabel(slave_panel, textvariable=self.incoming_invite_text, anchor="w", wraplength=285).grid(row=5, column=0, sticky="ew", padx=8, pady=(4, 2))
        invite_buttons = ctk.CTkFrame(slave_panel, fg_color="transparent")
        invite_buttons.grid(row=6, column=0, sticky="ew", padx=8, pady=(2, 8))
        invite_buttons.grid_columnconfigure((0, 1), weight=1)
        ctk.CTkButton(invite_buttons, text="Accept", command=self.accept_invite).grid(row=0, column=0, sticky="ew", padx=(0, 4))
        ctk.CTkButton(invite_buttons, text="Reject", command=self.reject_invite).grid(row=0, column=1, sticky="ew", padx=(4, 0))
        row += 1
        baseline_frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        baseline_frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        baseline_frame.grid_columnconfigure(1, weight=1)
        ctk.CTkCheckBox(baseline_frame, text="Baseline ms", variable=self.baseline_enabled).grid(row=0, column=0, sticky="w")
        ctk.CTkEntry(baseline_frame, textvariable=self.baseline_ms, width=100).grid(row=0, column=1, sticky="e")
        row += 1

        row = self._section(row, "Compare Runs")
        ctk.CTkLabel(
            self.controls,
            textvariable=self.compare_host_order_text,
            anchor="w",
            justify="left",
            wraplength=300,
        ).grid(row=row, column=0, sticky="ew", padx=8, pady=(0, 4))
        row += 1
        ctk.CTkLabel(
            self.controls,
            text="One variant per line: serial, openmp N, mpi N, hybrid-local PxT, multi a,b,c, or hybrid axT,bxT. multi/hybrid values include the master first. Master is not a slave. Example: hybrid 1x2,2x4,2x4 = Master 1 rank x 2 threads + each slave 2 ranks x 4 threads.",
            anchor="w",
            wraplength=300,
        ).grid(row=row, column=0, sticky="ew", padx=8, pady=(0, 4))
        row += 1
        self.compare_variants_box = ctk.CTkTextbox(self.controls, height=82, wrap="none")
        self.compare_variants_box.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        self.compare_variants_box.insert("1.0", "serial\nopenmp 4\nmpi 2\nhybrid-local 2x2\n")
        row += 1
        ctk.CTkButton(self.controls, text="Run Compare", command=self.run_compare).grid(row=row, column=0, sticky="ew", padx=8, pady=4)
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
        ctk.CTkButton(open_grid, text="Open Compare CSV", command=self._open_compare_csv).grid(row=2, column=0, sticky="ew", padx=(0, 4), pady=3)
        ctk.CTkButton(open_grid, text="Render Full Graph", command=self.render_full_graph).grid(row=2, column=1, sticky="ew", padx=(4, 0), pady=3)
        row += 1
        ctk.CTkLabel(self.controls, textvariable=self.status, anchor="w").grid(row=row, column=0, sticky="ew", padx=8, pady=(8, 4))

    def _build_image_tab(self):
        image_tab = self.tabs.tab("Image")
        controls = ctk.CTkFrame(image_tab, fg_color="transparent")
        controls.grid(row=0, column=0, sticky="ew", padx=12, pady=(12, 0))
        controls.grid_columnconfigure(6, weight=1)

        ctk.CTkLabel(controls, text="Recipe #").grid(row=0, column=0, sticky="w", padx=(0, 6))
        page_entry = ctk.CTkEntry(controls, textvariable=self.image_page, width=58)
        page_entry.grid(row=0, column=1, sticky="w", padx=(0, 8))
        page_entry.bind("<Return>", lambda _event: self._go_preview_page())
        ctk.CTkButton(controls, text="Prev", width=58, command=lambda: self._change_preview_page(-1)).grid(row=0, column=2, padx=3)
        ctk.CTkButton(controls, text="Next", width=58, command=lambda: self._change_preview_page(1)).grid(row=0, column=3, padx=3)
        ctk.CTkButton(controls, text="Go", width=46, command=self._go_preview_page).grid(row=0, column=4, padx=(8, 3))
        ctk.CTkButton(controls, text="Render Preview", width=116, command=self._render_preview_page).grid(row=0, column=5, padx=3)
        ctk.CTkLabel(controls, textvariable=self.image_page_status, anchor="e").grid(row=0, column=6, sticky="e")

        self.image_label = ctk.CTkLabel(image_tab, text="Run a search, then preview recipe pages here.")
        self.image_label.grid(row=1, column=0, sticky="nsew", padx=12, pady=12)

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

    def _combo_row(self, row, label, variable, values, command=None):
        frame = ctk.CTkFrame(self.controls, fg_color="transparent")
        frame.grid(row=row, column=0, sticky="ew", padx=8, pady=4)
        frame.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(frame, text=label).grid(row=0, column=0, sticky="w", padx=(0, 8))
        combo = ctk.CTkComboBox(frame, values=values, variable=variable, command=lambda _=None: command() if command else None)
        combo.grid(row=0, column=1, sticky="ew")
        return combo

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
        self._refresh_compare_host_order()

    def _append_host_row(self, host, slots="1", status="manual"):
        row_index = len(self.host_rows) + 1
        frame = ctk.CTkFrame(self.hosts_frame, fg_color="transparent")
        frame.grid(row=row_index, column=0, sticky="ew", padx=4, pady=2)
        frame.grid_columnconfigure(0, weight=1)
        host_var = ctk.StringVar(value=host)
        slots_var = ctk.StringVar(value=str(slots))
        status_var = ctk.StringVar(value=status)
        host_entry = ctk.CTkEntry(frame, textvariable=host_var)
        host_entry.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        host_entry.bind("<KeyRelease>", lambda _event: self._refresh_compare_host_order())
        ctk.CTkEntry(frame, textvariable=slots_var, width=54).grid(row=0, column=1, padx=4)
        ctk.CTkLabel(frame, textvariable=status_var, width=82).grid(row=0, column=2, padx=4)
        ctk.CTkButton(frame, text="Connect", width=70, command=lambda: self.connect_host_row(frame)).grid(row=0, column=3, padx=(0, 4))
        ctk.CTkButton(frame, text="x", width=28, command=lambda: self._remove_host_row(frame)).grid(row=0, column=4)
        self.host_rows.append({"frame": frame, "host": host_var, "slots": slots_var, "status": status_var})
        self._refresh_compare_host_order()

    def _remove_host_row(self, frame):
        self.host_rows = [row for row in self.host_rows if row["frame"] is not frame]
        frame.destroy()
        self._refresh_compare_host_order()

    def _host_entries(self, connected_only=False):
        entries = []
        for row in self.host_rows:
            host = row["host"].get().strip()
            if not host:
                continue
            status = row["status"].get().strip().lower()
            if connected_only and status not in {"connected", "master"}:
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
                self._refresh_compare_host_order()
                return

    def _set_host_slots(self, host, slots):
        key = host.strip().lower()
        for row in self.host_rows:
            if row["host"].get().strip().lower() == key:
                row["slots"].set(str(slots))
                self._refresh_compare_host_order()
                return

    def _merge_hosts(self, hosts):
        existing = {row["host"].get().strip().lower() for row in self.host_rows}
        for host, slots, status in hosts:
            key = host.strip().lower()
            if not key or key in existing:
                continue
            self._append_host_row(host, slots, status)
            existing.add(key)
        self._refresh_compare_host_order()

    def _add_manual_host(self):
        host = self.manual_host.get().strip()
        if not host:
            return
        for existing, _, _ in self._host_entries(False):
            if existing.lower() == host.lower():
                self.manual_host.set("")
                return
        self._append_host_row(host, "1", "candidate")
        self.manual_host.set("")

    def clear_hosts(self):
        self._replace_hosts([(socket.gethostname(), str(os.cpu_count() or 1), "master")])

    def scan_hosts(self):
        self.multi_node_enabled.set(True)
        self.status.set("Scanning LAN...")
        self._log("Scanning LAN for slave agents and reachable hosts...\n")

        def worker():
            try:
                hosts = scan_lan_hosts()
                self.log_queue.put(("hosts_merge", hosts))
                self.log_queue.put(("log", f"Scan found {len(hosts)} host entries.\n"))
            except Exception as exc:
                self.log_queue.put(("log", f"Scan failed: {exc}\n"))
            finally:
                self.log_queue.put(("status", "Idle"))

        threading.Thread(target=worker, daemon=True).start()

    def _invite_slave(self, host, slots):
        payload = {
            "type": "invite",
            "master_hostname": socket.gethostname(),
            "master_ip": primary_local_ip(),
            "requested_slots": slots,
            "project_path": str(ROOT_DIR),
            "timeout_seconds": INVITE_TIMEOUT_SECONDS,
        }
        try:
            with socket.create_connection((host, SLAVE_AGENT_PORT), timeout=5) as sock:
                send_json_line(sock, payload)
                return recv_json_line(sock, timeout=INVITE_TIMEOUT_SECONDS + 5)
        except socket.timeout:
            return {"status": "timeout", "message": "No response before timeout"}
        except Exception as exc:
            return {"status": "failed", "message": str(exc)}

    def connect_host_row(self, frame):
        row = next((item for item in self.host_rows if item["frame"] is frame), None)
        if row is None:
            return
        host = row["host"].get().strip()
        status = row["status"].get().strip().lower()
        if status == "master":
            self._log("This computer is already the master host.\n")
            return
        try:
            slots = int(row["slots"].get().strip())
            if slots < 1:
                raise ValueError
        except ValueError:
            messagebox.showerror("Invalid slots", f"Invalid slot count for host {host}")
            return
        self._connect_hosts([(host, slots)])

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
        targets = [(host, slots) for host, slots, status in entries if status not in {"master", "connected"}]
        self._connect_hosts(targets)

    def _connect_hosts(self, targets):
        if not targets:
            self._log("No candidate slave hosts to invite.\n")
            return
        self.multi_node_enabled.set(True)
        self.status.set("Waiting slave approval...")
        self._log("Sending master invite. Slave must accept within 30 seconds.\n")

        def worker():
            for host, slots in targets:
                self.log_queue.put(("host_status", (host, "waiting approval")))
                response = self._invite_slave(host, slots)
                status = response.get("status", "failed")
                if status == "accepted":
                    actual_slots = response.get("slots", slots)
                    self.log_queue.put(("host_slots", (host, str(actual_slots))))
                    self.log_queue.put(("host_status", (host, "connected")))
                    self.log_queue.put(("log", f"{host}: connected ({response.get('hostname', 'unknown')}, slots={actual_slots})\n"))
                elif status == "rejected":
                    self.log_queue.put(("host_status", (host, "rejected")))
                    self.log_queue.put(("log", f"{host}: rejected by slave\n"))
                elif status == "timeout":
                    self.log_queue.put(("host_status", (host, "timeout")))
                    self.log_queue.put(("log", f"{host}: invite timed out\n"))
                elif status == "busy":
                    self.log_queue.put(("host_status", (host, "busy")))
                    self.log_queue.put(("log", f"{host}: slave is busy with another invite\n"))
                else:
                    self.log_queue.put(("host_status", (host, "failed")))
                    self.log_queue.put(("log", f"{host}: failed {response.get('message', '')}\n"))
            self.log_queue.put(("status", "Idle"))

        threading.Thread(target=worker, daemon=True).start()

    def start_slave_agent(self):
        if self.slave_threads:
            self._log("Slave agent is already running.\n")
            return
        try:
            slots = int(self.slave_slots.get().strip())
            if slots < 1:
                raise ValueError
        except ValueError:
            messagebox.showerror("Invalid slots", "Offered slots must be a positive integer.")
            return

        self.cluster_role.set("Slave")
        self.slave_advertised_slots = str(slots)
        self.slave_stop_event.clear()
        self.slave_status.set(f"Slave waiting on {primary_local_ip()}:{SLAVE_AGENT_PORT}")
        self._log(f"Slave agent started at {primary_local_ip()}:{SLAVE_AGENT_PORT}. Waiting for master invite.\n")

        tcp_thread = threading.Thread(target=self._slave_tcp_server, daemon=True)
        udp_thread = threading.Thread(target=self._slave_discovery_server, daemon=True)
        self.slave_threads = [tcp_thread, udp_thread]
        tcp_thread.start()
        udp_thread.start()

    def stop_slave_agent(self):
        self.slave_stop_event.set()
        try:
            if self.slave_server_socket is not None:
                self.slave_server_socket.close()
        except Exception:
            pass
        self.slave_server_socket = None
        self.slave_threads = []
        if self.pending_invite:
            self._respond_to_invite({"status": "rejected", "message": "slave stopped"})
        self._clear_pending_invite("Slave stopped.")
        self.slave_status.set("Slave stopped")
        self._log("Slave agent stopped.\n")

    def _slave_tcp_server(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
                self.slave_server_socket = server
                server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                server.bind(("", SLAVE_AGENT_PORT))
                server.listen()
                server.settimeout(0.5)
                while not self.slave_stop_event.is_set():
                    try:
                        client, addr = server.accept()
                    except socket.timeout:
                        continue
                    except OSError:
                        break
                    threading.Thread(target=self._handle_slave_client, args=(client, addr), daemon=True).start()
        except Exception as exc:
            self.log_queue.put(("log", f"Slave TCP server failed: {exc}\n"))
            self.log_queue.put(("slave_status", "Slave stopped"))

    def _slave_discovery_server(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                sock.bind(("", SLAVE_DISCOVERY_PORT))
                sock.settimeout(0.5)
                while not self.slave_stop_event.is_set():
                    try:
                        data, addr = sock.recvfrom(4096)
                    except socket.timeout:
                        continue
                    except OSError:
                        break
                    if data != DISCOVERY_MESSAGE:
                        continue
                    payload = {
                        "type": "alchemy_slave",
                        "hostname": socket.gethostname(),
                        "ip": primary_local_ip(),
                        "port": SLAVE_AGENT_PORT,
                        "slots": self.slave_advertised_slots,
                        "project_path": str(ROOT_DIR),
                    }
                    try:
                        sock.sendto(json.dumps(payload).encode("utf-8"), addr)
                    except OSError:
                        pass
        except Exception as exc:
            self.log_queue.put(("log", f"Slave discovery server failed: {exc}\n"))

    def _handle_slave_client(self, client, addr):
        with client:
            try:
                request = recv_json_line(client, timeout=5)
                if request.get("type") != "invite":
                    send_json_line(client, {"status": "failed", "message": "unknown request"})
                    return

                with self.pending_invite_lock:
                    if self.pending_invite_active:
                        send_json_line(client, {"status": "busy", "message": "another invite is pending"})
                        return
                    self.pending_invite_active = True

                response_queue = queue.Queue(maxsize=1)
                invite = {
                    "master_hostname": request.get("master_hostname", "unknown"),
                    "master_ip": request.get("master_ip", addr[0]),
                    "requested_slots": request.get("requested_slots", 1),
                    "deadline": time.monotonic() + INVITE_TIMEOUT_SECONDS,
                    "response_queue": response_queue,
                }
                self.log_queue.put(("incoming_invite", invite))
                try:
                    response = response_queue.get(timeout=INVITE_TIMEOUT_SECONDS)
                except queue.Empty:
                    response = {"status": "timeout", "message": "slave approval timed out"}
                    self.log_queue.put(("invite_expired", None))
                send_json_line(client, response)
            except Exception as exc:
                try:
                    send_json_line(client, {"status": "failed", "message": str(exc)})
                except Exception:
                    pass
            finally:
                with self.pending_invite_lock:
                    self.pending_invite_active = False

    def _handle_incoming_invite(self, invite):
        self.cluster_role.set("Slave")
        self.pending_invite = invite
        self._tick_invite_countdown()
        self._log(f"Incoming invite from {invite['master_hostname']} ({invite['master_ip']}). Accept within 30 seconds.\n")

    def _tick_invite_countdown(self):
        if not self.pending_invite:
            return
        remaining = int(max(0, self.pending_invite["deadline"] - time.monotonic()))
        self.incoming_invite_text.set(
            f"Incoming master request:\n"
            f"{self.pending_invite['master_hostname']} ({self.pending_invite['master_ip']})\n"
            f"Requested slots: {self.pending_invite['requested_slots']}\n"
            f"Accept within: {remaining}s"
        )
        if remaining <= 0:
            self._clear_pending_invite("Invite timed out.")
            return
        self.after(1000, self._tick_invite_countdown)

    def _slave_ready_payload(self):
        try:
            slots = int(self.slave_slots.get().strip())
            if slots < 1:
                raise ValueError
        except ValueError:
            slots = 1
        exe = executable_path("alchemy_mpi")
        return {
            "status": "accepted",
            "hostname": socket.gethostname(),
            "ip": primary_local_ip(),
            "slots": slots,
            "project_path": str(ROOT_DIR),
            "executable_exists": exe.exists(),
        }

    def accept_invite(self):
        if not self.pending_invite:
            self._log("No pending invite to accept.\n")
            return
        self._respond_to_invite(self._slave_ready_payload())
        self._clear_pending_invite("Connected to master. Waiting for MPI launch.")
        self.slave_status.set("Slave accepted master. Waiting for MPI launch.")

    def reject_invite(self):
        if not self.pending_invite:
            self._log("No pending invite to reject.\n")
            return
        self._respond_to_invite({"status": "rejected"})
        self._clear_pending_invite("Invite rejected.")

    def _respond_to_invite(self, response):
        try:
            self.pending_invite["response_queue"].put_nowait(response)
        except Exception:
            pass

    def _clear_pending_invite(self, text="No incoming master request."):
        self.pending_invite = None
        self.incoming_invite_text.set(text)

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

    def _apply_target_filter(self, keep_text=False):
        tier = self.tier_filter.get()
        current = self.target.get()
        needle = current.strip().lower() if keep_text else ""
        source = self.target_catalog.get(tier, self.target_catalog.get("all", []))
        targets = [name for name in source if not needle or name.lower().startswith(needle)]
        if not targets:
            targets = ["<no match>"]
        self.filtered_targets = targets
        self.target_combo.configure(values=targets)
        if keep_text:
            self._open_target_dropdown()
            return
        if current not in targets:
            self.target.set(targets[0])

    def _open_target_dropdown(self):
        open_dropdown = getattr(self.target_combo, "_open_dropdown_menu", None)
        if callable(open_dropdown):
            try:
                open_dropdown()
            except Exception:
                pass

    def _resolve_target(self):
        current = self.target.get().strip()
        if not current or current == "<no match>":
            raise ValueError("No target matches the current tier/search filter")

        source = self.target_catalog.get(self.tier_filter.get(), self.target_catalog.get("all", []))
        current_lower = current.lower()
        for name in source:
            if name.lower() == current_lower:
                if self.target.get() != name:
                    self.target.set(name)
                return name

        matches = [name for name in self.filtered_targets if name != "<no match>"]
        if matches:
            self.target.set(matches[0])
            return matches[0]
        raise ValueError("No target matches the current tier/search filter")

    def _refresh_mode_state(self):
        limit_state = "disabled" if self.search_mode.get() == "all" else "normal"
        self.limit_entry.configure(state=limit_state)
        if self.search_mode.get() == "all":
            self.algorithm.set("bfs")
            self.algorithm_menu.configure(state="disabled")
        else:
            self.algorithm_menu.configure(state="normal")
        mpi_state = "normal" if self.engine.get() == "mpi" else "disabled"
        self.mpi_np_entry.configure(state=mpi_state)
        self.split_depth_entry.configure(state=mpi_state)
        threads_state = "normal" if self.engine.get() in {"openmp", "mpi"} else "disabled"
        self.openmp_threads_entry.configure(state=threads_state)

    def _base_args(self, render_mode="json", output_prefix=None, include_mpi_options=None, force_target=False):
        output_prefix = self.output_prefix.get() if output_prefix is None else str(output_prefix)
        include_mpi_options = self.engine.get() == "mpi" if include_mpi_options is None else include_mpi_options
        args = [
            "--data", self.data_path.get(),
            "--tiers", self.tiers_path.get(),
            "--algorithm", self.algorithm.get(),
            "--mode", self.search_mode.get(),
            "--trace-mode", self.trace_mode.get(),
            "--visual-mode", self.visual_mode.get(),
            "--render", render_mode,
            "--output", output_prefix,
            "--format", self.image_format.get(),
        ]
        if self.run_kind.get() == "benchmark" and not force_target:
            args.extend(["--benchmark", self.benchmark_path.get()])
        else:
            args.extend(["--target", self._resolve_target()])

        if self.search_mode.get() != "all":
            args.extend(["--limit", self.limit.get()])
        if self.max_visual_depth_enabled.get():
            args.extend(["--max-visual-depth", self.max_visual_depth.get()])
        if include_mpi_options:
            args.extend(["--split-depth", self.split_depth.get()])
            if self.baseline_enabled.get() and self.baseline_ms.get().strip():
                args.extend(["--baseline-ms", self.baseline_ms.get().strip()])
        return args

    def _mpi_host_token(self, host, status, index):
        """Kembalikan token host untuk baris `-hosts` mpiexec.

        Node master/lokal SELALU di-address `localhost` (persis pola reference
        `tugas_lain_bisa/HPC` yang terbukti jalan). Meng-address rank-0 lewat
        hostname asli di mesin multi-homed (Tailscale, 169.254 APIPA, dsb) bikin
        smpd manager bind/connect ke adapter salah -> error 1726 / hang.
        Slave tetap pakai hostname/IP apa adanya supaya cocok dgn cmdkey."""
        if status == "master" or index == 0:
            return "localhost"
        key = (host or "").strip().lower()
        if key in {"localhost", "127.0.0.1", "::1"}:
            return "localhost"
        if key == socket.gethostname().strip().lower():
            return "localhost"
        if key in {addr.lower() for addr in local_ipv4_addresses()}:
            return "localhost"
        return host

    def _mpi_prefix(self):
        np_value = self.mpi_np.get().strip() or "2"
        if os.name == "nt" and self.multi_node_enabled.get():
            entries = self._host_entries(connected_only=True)
            if not entries:
                raise ValueError("Multi-node is enabled but no host is connected. Run Test/Connect first.")
            args = ["mpiexec", "-hosts", str(len(entries))]
            for index, (host, slots, status) in enumerate(entries):
                args.extend([self._mpi_host_token(host, status, index), str(slots)])
            args.extend(["-wdir", str(ROOT_DIR)])
            return args
        if os.name == "nt":
            return ["mpiexec", "-n", np_value]
        if shutil.which("mpirun"):
            return ["mpirun", "-np", np_value]
        return ["mpiexec", "-n", np_value]

    def _require_positive_int(self, value, label):
        try:
            parsed = int(str(value).strip())
        except ValueError as exc:
            raise ValueError(f"{label} must be a positive integer") from exc
        if parsed < 1:
            raise ValueError(f"{label} must be a positive integer")
        return parsed

    def _openmp_threads_value(self):
        return self._require_positive_int(self.openmp_threads.get() or "1", "OpenMP threads")

    def _mpi_process_count_for_current_layout(self):
        if os.name == "nt" and self.multi_node_enabled.get():
            entries = self._host_entries(connected_only=True)
            if not entries:
                raise ValueError("Multi-node is enabled but no host is connected. Run Test/Connect first.")
            host_profile = ", ".join(f"{host}:{slots}" for host, slots, _status in entries)
            return sum(slots for _host, slots, _status in entries), host_profile
        processes = self._require_positive_int(self.mpi_np.get().strip() or "2", "Processes")
        return processes, f"local:{processes}"

    def _command_for_run(self, render_mode="json"):
        engine = self.engine.get()
        if engine == "mpi":
            exe = executable_path("alchemy_mpi")
            command = self._mpi_prefix() + [str(exe)] + self._base_args(render_mode)
            threads = self._openmp_threads_value()
            if threads > 1:
                command.extend(["--threads", str(threads)])
            return command
        if engine == "openmp":
            exe = executable_path("alchemy_openmp")
            return [str(exe)] + self._base_args(render_mode) + ["--threads", str(self._openmp_threads_value())]
        exe = executable_path("alchemy_serial")
        return [str(exe)] + self._base_args(render_mode)

    def _command_for_build(self):
        return [["cmake", "-S", str(ROOT_DIR), "-B", str(ROOT_DIR / "build")], ["cmake", "--build", str(ROOT_DIR / "build")]]

    def build_project(self):
        commands = self._command_for_build()
        self._start_commands(commands, clear=True)

    def run_search(self):
        self.last_output_prefix = Path(self.output_prefix.get())
        self.last_format = self.image_format.get()
        try:
            command = self._command_for_run("json")
        except Exception as exc:
            messagebox.showerror("Invalid command", str(exc))
            return
        self._start_commands([command], clear=True)

    def render_full_graph(self):
        if self.run_kind.get() == "benchmark":
            messagebox.showinfo("Render Full Graph", "Full graph render is available for target runs. Benchmark runs stay JSON/CSV-first.")
            return
        self.last_output_prefix = Path(self.output_prefix.get())
        self.last_format = self.image_format.get()
        try:
            command = self._command_for_run("full")
        except Exception as exc:
            messagebox.showerror("Invalid command", str(exc))
            return
        self._start_commands([command], clear=False)

    def _local_mpi_prefix(self, process_count):
        process_count = str(process_count)
        if os.name == "nt":
            return ["mpiexec", "-n", process_count]
        if shutil.which("mpirun"):
            return ["mpirun", "-np", process_count]
        return ["mpiexec", "-n", process_count]

    def _compare_host_order(self):
        entries = self._host_entries(connected_only=True)
        order = []
        slave_index = 1
        for index, (host, slots, status) in enumerate(entries):
            if status == "master" or index == 0:
                label = "Master"
            else:
                label = f"Slave {slave_index}"
                slave_index += 1
            order.append({"label": label, "host": host, "slots": slots, "status": status})
        return order

    def _refresh_compare_host_order(self):
        try:
            order = self._compare_host_order()
            if not order:
                self.compare_host_order_text.set("Compare host order:\nMaster: no connected/master host")
                return
            lines = ["Compare host order:"]
            for item in order:
                lines.append(f"{item['label']}: {item['host']}")
            self.compare_host_order_text.set("\n".join(lines))
        except Exception as exc:
            self.compare_host_order_text.set(f"Compare host order unavailable: {exc}")

    def _multi_mpi_prefix_for_slots(self, slot_values):
        order = self._compare_host_order()
        if len(order) != len(slot_values):
            labels = ", ".join(item["label"] for item in order) or "none"
            variant_text = "multi " + ",".join(str(value) for value in slot_values)
            raise ValueError(
                f"{variant_text} has {len(slot_values)} values, but current compare host order has {len(order)} hosts: {labels}."
            )
        args = ["mpiexec", "-hosts", str(len(order))]
        host_profile = []
        for index, (item, slots) in enumerate(zip(order, slot_values)):
            token = self._mpi_host_token(item["host"], item["status"], index)
            args.extend([token, str(slots)])
            host_profile.append(f"{item['label']} {token}:{slots}")
        args.extend(["-wdir", str(ROOT_DIR)])
        return args, ", ".join(host_profile)

    def _parse_hybrid_profile(self, text, keyword):
        order = self._compare_host_order()
        parts = [part.strip() for part in text.split(",") if part.strip()]
        if len(order) != len(parts):
            labels = ", ".join(item["label"] for item in order) or "none"
            raise ValueError(
                f"{keyword} {text} has {len(parts)} values, but current compare host order has {len(order)} hosts: {labels}."
            )
        slot_values = []
        thread_values = []
        expanded_threads = []
        for part in parts:
            match = re.fullmatch(r"(\d+)\s*[xX]\s*(\d+)", part)
            if not match:
                raise ValueError(f"Invalid {keyword} token '{part}'. Expected processesxthreads, e.g. 2x4.")
            slots = int(match.group(1))
            threads = int(match.group(2))
            if slots < 1 or threads < 1:
                raise ValueError(f"Invalid {keyword} token '{part}'. Values must be positive.")
            slot_values.append(slots)
            thread_values.append(threads)
            expanded_threads.extend([threads] * slots)
        return slot_values, thread_values, ",".join(str(value) for value in expanded_threads)

    def _append_threads_arg(self, command, threads):
        return list(command) + ["--threads", str(threads)]

    def _variant_slug(self, value):
        slug = re.sub(r"[^A-Za-z0-9]+", "_", value.strip().lower()).strip("_")
        return slug or "run"

    def _compare_variants(self):
        raw_lines = self.compare_variants_box.get("1.0", "end").splitlines()
        lines = []
        for raw in raw_lines:
            line = raw.split("#", 1)[0].strip()
            if line:
                lines.append(line)
        if not lines:
            raise ValueError("Compare variants is empty")

        specs = []
        used_slugs = {}
        base_prefix = str(Path(self.output_prefix.get()))
        serial_exe = executable_path("alchemy_serial")
        openmp_exe = executable_path("alchemy_openmp")
        mpi_exe = executable_path("alchemy_mpi")

        for line in lines:
            keyword, _space, rest = line.partition(" ")
            keyword = keyword.lower()
            rest = rest.strip()
            if keyword == "serial" and not rest:
                slug = "serial"
                output_prefix = f"{base_prefix}_compare_{slug}"
                command = [str(serial_exe)] + self._base_args("json", output_prefix, include_mpi_options=False, force_target=True)
                spec = {
                    "variant": "serial",
                    "engine": "serial",
                    "processes": 1,
                    "threads_per_rank": "1",
                    "total_workers": 1,
                    "host_profile": "single process",
                    "output_prefix": output_prefix,
                    "command": command,
                }
            elif keyword == "openmp":
                try:
                    thread_count = int(rest)
                except ValueError as exc:
                    raise ValueError(f"Invalid openmp variant '{line}'. Expected: openmp N") from exc
                if thread_count < 1:
                    raise ValueError(f"Invalid OpenMP thread count in '{line}'")
                slug = f"openmp_{thread_count}"
                output_prefix = f"{base_prefix}_compare_{slug}"
                command = (
                    [str(openmp_exe)]
                    + self._base_args("json", output_prefix, include_mpi_options=False, force_target=True)
                    + ["--threads", str(thread_count)]
                )
                spec = {
                    "variant": f"openmp {thread_count}",
                    "engine": "openmp",
                    "processes": 1,
                    "threads_per_rank": str(thread_count),
                    "total_workers": thread_count,
                    "host_profile": f"local-threads:{thread_count}",
                    "output_prefix": output_prefix,
                    "command": command,
                }
            elif keyword in ("mpi", "local"):
                try:
                    process_count = int(rest)
                except ValueError as exc:
                    raise ValueError(f"Invalid mpi variant '{line}'. Expected: mpi N") from exc
                if process_count < 1:
                    raise ValueError(f"Invalid mpi process count in '{line}'")
                slug = f"mpi_{process_count}"
                output_prefix = f"{base_prefix}_compare_{slug}"
                command = (
                    self._local_mpi_prefix(process_count)
                    + [str(mpi_exe)]
                    + self._base_args("json", output_prefix, include_mpi_options=True, force_target=True)
                )
                spec = {
                    "variant": f"mpi {process_count}",
                    "engine": "mpi",
                    "processes": process_count,
                    "threads_per_rank": "1",
                    "total_workers": process_count,
                    "host_profile": f"local:{process_count}",
                    "output_prefix": output_prefix,
                    "command": command,
                }
            elif keyword == "hybrid-local":
                match = re.fullmatch(r"(\d+)\s*[xX]\s*(\d+)", rest)
                if not match:
                    raise ValueError(f"Invalid hybrid-local variant '{line}'. Expected: hybrid-local PxT")
                process_count = int(match.group(1))
                thread_count = int(match.group(2))
                if process_count < 1 or thread_count < 1:
                    raise ValueError(f"Invalid hybrid-local values in '{line}'")
                slug = f"hybrid_local_{process_count}x{thread_count}"
                output_prefix = f"{base_prefix}_compare_{slug}"
                command = (
                    self._local_mpi_prefix(process_count)
                    + [str(mpi_exe)]
                    + self._base_args("json", output_prefix, include_mpi_options=True, force_target=True)
                    + ["--threads", str(thread_count)]
                )
                spec = {
                    "variant": f"hybrid-local {process_count}x{thread_count}",
                    "engine": "mpi+openmp-local",
                    "processes": process_count,
                    "threads_per_rank": str(thread_count),
                    "total_workers": process_count * thread_count,
                    "host_profile": f"local:{process_count}",
                    "output_prefix": output_prefix,
                    "command": command,
                }
            elif keyword == "multi":
                try:
                    slot_values = [int(value.strip()) for value in rest.split(",") if value.strip()]
                except ValueError as exc:
                    raise ValueError(f"Invalid multi variant '{line}'. Expected: multi a,b,c") from exc
                if not slot_values or any(slots < 1 for slots in slot_values):
                    raise ValueError(f"Invalid multi slots in '{line}'")
                mpi_prefix, host_profile = self._multi_mpi_prefix_for_slots(slot_values)
                slug = "multi_" + "x".join(str(slots) for slots in slot_values)
                output_prefix = f"{base_prefix}_compare_{slug}"
                command = (
                    mpi_prefix
                    + [str(mpi_exe)]
                    + self._base_args("json", output_prefix, include_mpi_options=True, force_target=True)
                )
                spec = {
                    "variant": "multi " + ",".join(str(slots) for slots in slot_values),
                    "engine": "mpi-multi",
                    "processes": sum(slot_values),
                    "threads_per_rank": "1",
                    "total_workers": sum(slot_values),
                    "host_profile": host_profile,
                    "output_prefix": output_prefix,
                    "command": command,
                }
            elif keyword == "hybrid":
                if re.fullmatch(r"\d+", rest or ""):
                    thread_count = int(rest)
                    if thread_count < 1:
                        raise ValueError(f"Invalid hybrid thread count in '{line}'")
                    process_count, host_profile = self._mpi_process_count_for_current_layout()
                    slug = f"hybrid_{process_count}x{thread_count}"
                    output_prefix = f"{base_prefix}_compare_{slug}"
                    command = (
                        self._mpi_prefix()
                        + [str(mpi_exe)]
                        + self._base_args("json", output_prefix, include_mpi_options=True, force_target=True)
                        + ["--threads", str(thread_count)]
                    )
                    spec = {
                        "variant": f"hybrid {thread_count}",
                        "engine": "mpi+openmp",
                        "processes": process_count,
                        "threads_per_rank": str(thread_count),
                        "total_workers": process_count * thread_count,
                        "host_profile": host_profile,
                        "output_prefix": output_prefix,
                        "command": command,
                    }
                else:
                    slot_values, _thread_values, expanded_threads = self._parse_hybrid_profile(rest, "hybrid")
                    mpi_prefix, host_profile = self._multi_mpi_prefix_for_slots(slot_values)
                    profile = ",".join(part.strip().replace("X", "x") for part in rest.split(",") if part.strip())
                    slug = "hybrid_" + "_".join(profile.split(","))
                    output_prefix = f"{base_prefix}_compare_{slug}"
                    command = (
                        mpi_prefix
                        + [str(mpi_exe)]
                        + self._base_args("json", output_prefix, include_mpi_options=True, force_target=True)
                        + ["--thread-profile", profile]
                    )
                    total_workers = sum(
                        int(slots) * int(threads)
                        for slots, threads in (part.lower().split("x", 1) for part in profile.split(","))
                    )
                    spec = {
                        "variant": "hybrid " + profile,
                        "engine": "mpi+openmp",
                        "processes": sum(slot_values),
                        "threads_per_rank": expanded_threads,
                        "total_workers": total_workers,
                        "host_profile": host_profile,
                        "output_prefix": output_prefix,
                        "command": command,
                    }
            else:
                raise ValueError(
                    f"Unknown compare variant '{line}'. Use serial, openmp N, mpi N, hybrid-local PxT, multi a,b,c, or hybrid axT,bxT."
                )

            base_slug = self._variant_slug(slug)
            seen_count = used_slugs.get(base_slug, 0)
            used_slugs[base_slug] = seen_count + 1
            if seen_count:
                unique_slug = f"{base_slug}_{seen_count + 1}"
                spec["output_prefix"] = f"{base_prefix}_compare_{unique_slug}"
                spec["command"] = self._replace_output_prefix(spec["command"], spec["output_prefix"])
            specs.append(spec)
        return specs

    def _replace_output_prefix(self, command, output_prefix):
        replaced = list(command)
        for index, value in enumerate(replaced[:-1]):
            if value == "--output":
                replaced[index + 1] = output_prefix
                break
        return replaced

    def run_compare(self):
        if self.run_kind.get() == "benchmark":
            messagebox.showinfo("Run Compare", "Compare runs use the current target. Switch Run type to target first.")
            return
        self.last_format = self.image_format.get()
        try:
            specs = self._compare_variants()
        except Exception as exc:
            messagebox.showerror("Invalid compare variants", str(exc))
            return
        csv_path = Path(f"{self.output_prefix.get()}_compare.csv")
        self._start_compare(specs, csv_path)

    def _start_compare(self, specs, csv_path):
        if self.proc is not None:
            messagebox.showwarning("Process running", "Stop the current process before starting another one.")
            return
        self._clear_outputs()
        self.stop_requested = False
        self.run_started_at = time.monotonic()
        self.status.set("Running compare...")
        self.command_box.delete("1.0", "end")
        for spec in specs:
            self.command_box.insert("end", f"# {spec['variant']}\n{shell_join(spec['command'])}\n")
        self.tabs.set("Log")
        self.worker_thread = threading.Thread(target=self._run_compare_worker, args=(specs, csv_path), daemon=True)
        self.worker_thread.start()

    def _run_compare_worker(self, specs, csv_path):
        rows = []
        stopped = False
        try:
            for spec in specs:
                command = spec["command"]
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
                row = self._compare_row_from_json(spec, rc)
                rows.append(row)
                self.log_queue.put(("log", f"\nCompare variant '{spec['variant']}' exited with code {rc}\n"))
                if self.stop_requested:
                    stopped = True
                    break

            self._write_compare_csv(csv_path, rows)
            self.log_queue.put(("compare_done", (not stopped, str(csv_path))))
        except Exception as exc:
            self.proc = None
            self.log_queue.put(("log", f"Compare error: {exc}\n"))
            if rows:
                try:
                    self._write_compare_csv(csv_path, rows)
                    self.log_queue.put(("compare_done", (False, str(csv_path))))
                    return
                except Exception as write_exc:
                    self.log_queue.put(("log", f"Could not write partial compare CSV: {write_exc}\n"))
            self.log_queue.put(("compare_done", (False, "")))

    def _compare_row_from_json(self, spec, return_code):
        json_path = Path(spec["output_prefix"]).with_suffix(".json")
        row = {
            "variant": spec["variant"],
            "engine": spec["engine"],
            "processes": spec["processes"],
            "threads_per_rank": spec.get("threads_per_rank", "1"),
            "total_workers": spec.get("total_workers", spec["processes"]),
            "host_profile": spec["host_profile"],
            "recipes_found": "",
            "time_ms": "",
            "communication_ms": "",
            "speedup_vs_serial": "",
            "efficiency": "",
            "output_json": str(json_path),
            "status": "ok" if return_code == 0 else f"failed:{return_code}",
        }
        if json_path.exists():
            data = json.loads(json_path.read_text(encoding="utf-8"))
            stats = data.get("statistics", {})
            row["recipes_found"] = data.get("recipes_found", "")
            row["time_ms"] = stats.get("time_ms", "")
            row["communication_ms"] = stats.get("communication_ms", "")
            row["processes"] = stats.get("processes", row["processes"])
            threads_by_rank = stats.get("threads_by_rank", [])
            if isinstance(threads_by_rank, list) and threads_by_rank:
                row["threads_per_rank"] = ",".join(str(value) for value in threads_by_rank)
            else:
                row["threads_per_rank"] = str(stats.get("threads_per_process", row["threads_per_rank"]))
            row["total_workers"] = stats.get("total_workers", row["total_workers"])
        elif return_code == 0:
            row["status"] = "missing-json"
        return row

    def _write_compare_csv(self, csv_path, rows):
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        baseline = None
        for row in rows:
            if row["engine"] == "serial" and row["status"] == "ok" and row["time_ms"] not in {"", None}:
                baseline = float(row["time_ms"])
                break
        if baseline and baseline > 0:
            for row in rows:
                if row["status"] != "ok" or row["time_ms"] in {"", None}:
                    continue
                time_ms = float(row["time_ms"])
                if time_ms <= 0:
                    continue
                speedup = baseline / time_ms
                row["speedup_vs_serial"] = f"{speedup:.3f}"
                try:
                    workers = int(row.get("total_workers", row["processes"]))
                except (TypeError, ValueError):
                    workers = 0
                if workers > 0:
                    row["efficiency"] = f"{(speedup / workers):.3f}"

        fieldnames = [
            "variant",
            "engine",
            "processes",
            "threads_per_rank",
            "total_workers",
            "host_profile",
            "recipes_found",
            "time_ms",
            "communication_ms",
            "speedup_vs_serial",
            "efficiency",
            "output_json",
            "status",
        ]
        with csv_path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)

    def _start_commands(self, commands, clear=False):
        if self.proc is not None:
            messagebox.showwarning("Process running", "Stop the current process before starting another one.")
            return
        if clear:
            self._clear_outputs()
        self.stop_requested = False
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
        self.stop_requested = True
        try:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(self.proc.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                self.proc.terminate()
        except Exception as exc:
            self._log(f"Stop failed: {exc}\n")

    def _drain_log_queue(self):
        pending_log = []

        def flush_log():
            if pending_log:
                self._append_log("".join(pending_log))
                pending_log.clear()

        try:
            while True:
                kind, payload = self.log_queue.get_nowait()
                if kind == "log":
                    # Kumpulkan dulu; di-insert sekali (hindari ribuan insert+see per baris).
                    pending_log.append(payload)
                    continue
                # Event non-log: pastikan urutan log tetap benar.
                flush_log()
                if kind == "hosts_replace":
                    self._replace_hosts(payload)
                elif kind == "hosts_merge":
                    self._merge_hosts(payload)
                elif kind == "host_status":
                    host, status = payload
                    self._set_host_status(host, status)
                elif kind == "host_slots":
                    host, slots = payload
                    self._set_host_slots(host, slots)
                elif kind == "incoming_invite":
                    self._handle_incoming_invite(payload)
                elif kind == "invite_expired":
                    self._clear_pending_invite("Invite timed out.")
                elif kind == "slave_status":
                    self.slave_status.set(payload)
                elif kind == "status":
                    self.status.set(payload)
                elif kind == "done":
                    self.run_started_at = None
                    if payload:
                        self._load_outputs()  # baca/parse JSON di worker thread (anti-freeze)
                    else:
                        self.status.set("Failed or stopped")
                elif kind == "result_ready":
                    self._apply_loaded_outputs(payload)
                elif kind == "compare_done":
                    success, csv_path = payload
                    self.run_started_at = None
                    self.status.set("Compare done" if success else "Compare failed or partial")
                    if csv_path:
                        path = Path(csv_path)
                        if path.exists():
                            self._load_compare_csv(path)
                            self.tabs.set("Compare")
        except queue.Empty:
            pass
        flush_log()
        if self.run_started_at is not None:
            elapsed = time.monotonic() - self.run_started_at
            self.status.set(f"Running... {elapsed:.1f}s")
        self.after(100, self._drain_log_queue)

    def _clear_outputs(self):
        for textbox in (self.log_box, self.result_box, self.compare_box, self.csv_box, self.command_box):
            textbox.delete("1.0", "end")
        self.preview_json_data = None
        self.preview_recipes = []
        self.preview_page_index = 0
        self.image_page.set("1")
        self.image_page_status.set("No recipe preview.")
        self._reset_image_preview("Run completed outputs will appear here.")

    def _append_log(self, text):
        """Insert teks log sekali jalan, dengan batas ukuran supaya GUI tak freeze.
        Output program bisa sangat besar (full tree tiap resep), jadi kita simpan
        hanya LOG_MAX_LINES baris terakhir."""
        if not text:
            return
        box = self.log_box
        # Kalau satu batch saja sudah lebih besar dari kapasitas, ambil ekornya.
        if text.count("\n") > LOG_MAX_LINES:
            lines = text.splitlines(keepends=True)
            text = "".join(lines[-LOG_MAX_LINES:])
        box.insert("end", text)
        try:
            line_count = int(box.index("end-1c").split(".")[0])
            if line_count > LOG_MAX_LINES:
                box.delete("1.0", f"{line_count - LOG_MAX_LINES + 1}.0")
        except Exception:
            pass
        box.see("end")

    def _log(self, text):
        self._append_log(text)

    def _load_outputs(self):
        prefix = Path(self.output_prefix.get())
        if self.run_kind.get() == "benchmark":
            csv_path = prefix.with_suffix(".csv")
            if csv_path.exists():
                self._load_csv(csv_path)
                self.tabs.set("CSV")
            return

        json_path = prefix.with_suffix(".json")
        image_path = prefix.with_suffix("." + self.image_format.get())
        # Baca + parse + serialisasi JSON di worker thread supaya UI tidak freeze;
        # hasilnya dikirim balik lewat antrian (event "result_ready").
        self.status.set("Loading results...")
        threading.Thread(
            target=self._load_outputs_worker,
            args=(json_path, image_path),
            daemon=True,
        ).start()

    def _load_outputs_worker(self, json_path, image_path):
        payload = {"image_path": str(image_path), "image_format": self.image_format.get()}
        try:
            if json_path.exists():
                data = json.loads(json_path.read_text(encoding="utf-8"))
                recipes = list(data.get("recipes", []))
                full = json.dumps(data, indent=2)
                if len(full) > RESULT_MAX_CHARS:
                    display = (
                        full[:RESULT_MAX_CHARS]
                        + f"\n\n... (output dipotong di {RESULT_MAX_CHARS:,} dari {len(full):,} karakter. "
                        f"Buka file JSON untuk lengkap, atau pakai Render Preview per resep.)\n"
                    )
                else:
                    display = full
                payload.update({"ok": True, "data": data, "recipes": recipes, "display": display})
            else:
                payload.update({"ok": False})
        except Exception as exc:
            payload.update({"ok": False, "error": str(exc)})
        self.log_queue.put(("result_ready", payload))

    def _apply_loaded_outputs(self, payload):
        if not payload.get("ok"):
            if payload.get("error"):
                self._append_log(f"Could not parse JSON output: {payload['error']}\n")
            self._maybe_preview_image(Path(payload["image_path"]), payload.get("image_format", ""))
            self.status.set("Done")
            return
        self.result_box.delete("1.0", "end")
        self.result_box.insert("end", payload["display"])
        self.tabs.set("Results")
        self.preview_json_data = payload["data"]
        self.preview_recipes = payload["recipes"]
        self.preview_page_index = 0
        self.image_page.set("1")
        if self.preview_recipes:
            self.image_page_status.set(f"JSON loaded; choose recipe to preview. {len(self.preview_recipes)} recipes available.")
            self._reset_image_preview("JSON loaded. Choose a recipe number, then click Render Preview.")
        else:
            self.image_page_status.set("No recipes found.")
            self._reset_image_preview("No recipes to preview.")
            self._maybe_preview_image(Path(payload["image_path"]), payload.get("image_format", ""))
        self.status.set("Done")

    def _maybe_preview_image(self, image_path, image_format):
        if image_path.exists() and not self.preview_recipes:
            if image_format in {"png", "jpg", "jpeg"}:
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

    def _load_compare_csv(self, path: Path):
        self.compare_box.delete("1.0", "end")
        try:
            with path.open("r", encoding="utf-8", newline="") as handle:
                rows = list(csv.DictReader(handle))
            self.compare_box.insert("end", f"Compare CSV: {path}\n\n")
            if not rows:
                self.compare_box.insert("end", "No compare rows found.\n")
                return

            columns = [
                ("variant", "Variant"),
                ("engine", "Engine"),
                ("processes", "Proc"),
                ("threads_per_rank", "Threads/rank"),
                ("total_workers", "Total workers"),
                ("host_profile", "Hosts"),
                ("recipes_found", "Recipes"),
                ("time_ms", "Time ms"),
                ("communication_ms", "Comm ms"),
                ("speedup_vs_serial", "Speedup"),
                ("efficiency", "Eff"),
                ("status", "Status"),
            ]
            headers = [label for _key, label in columns]
            table_rows = []
            for row in rows:
                table_rows.append([str(row.get(key, ""))[:42] for key, _label in columns])
            widths = []
            for index, header in enumerate(headers):
                max_width = max(len(header), *(len(row[index]) for row in table_rows))
                widths.append(min(max_width, 42))

            header_line = " | ".join(headers[index].ljust(widths[index]) for index in range(len(headers)))
            separator = "-+-".join("-" * widths[index] for index in range(len(headers)))
            self.compare_box.insert("end", header_line + "\n")
            self.compare_box.insert("end", separator + "\n")
            for row in table_rows:
                cells = [row[index][:widths[index]].ljust(widths[index]) for index in range(len(widths))]
                self.compare_box.insert("end", " | ".join(cells) + "\n")

            self.compare_box.insert("end", "\nOutput JSON files:\n")
            for row in rows:
                self.compare_box.insert("end", f"- {row.get('variant', '')}: {row.get('output_json', '')}\n")
        except Exception as exc:
            self.compare_box.insert("end", f"Could not load compare CSV: {exc}\n")

    def _positive_int(self, value, default):
        try:
            parsed = int(str(value).strip())
            if parsed > 0:
                return parsed
        except ValueError:
            pass
        return default

    def _preview_page_count(self):
        if not self.preview_recipes:
            return 0
        return len(self.preview_recipes)

    def _change_preview_page(self, delta):
        if not self.preview_recipes:
            return
        self._show_preview_page(self.preview_page_index + delta)

    def _go_preview_page(self):
        if not self.preview_recipes:
            return
        page = self._positive_int(self.image_page.get(), self.preview_page_index + 1)
        self._show_preview_page(page - 1)

    def _show_preview_page(self, page_index):
        total_pages = self._preview_page_count()
        if total_pages <= 0:
            self.image_page_status.set("No recipes found.")
            self._reset_image_preview("No recipes to preview.")
            return
        self.preview_page_index = min(max(0, page_index), total_pages - 1)
        self.image_page.set(str(self.preview_page_index + 1))
        self._render_preview_page()

    def _render_preview_page(self):
        if not self.preview_recipes or not self.preview_json_data:
            self.image_page_status.set("No recipes found.")
            self._reset_image_preview("No recipes to preview.")
            return

        total = len(self.preview_recipes)
        total_pages = self._preview_page_count()
        self.preview_page_index = min(max(0, self.preview_page_index), max(0, total_pages - 1))
        self.image_page.set(str(self.preview_page_index + 1))
        recipe = self.preview_recipes[self.preview_page_index]
        if total_pages == 0 or not recipe:
            self.image_page_status.set("No recipes found.")
            self._reset_image_preview("No recipes to preview.")
            return

        if shutil.which("dot") is None:
            self.image_page_status.set(f"Recipe {self.preview_page_index + 1}/{total_pages}")
            self._reset_image_preview("Graphviz 'dot' is not available; cannot render recipe preview.")
            return

        try:
            dot_text = self._build_preview_dot([recipe])
            with tempfile.TemporaryDirectory(prefix="alchemy_preview_") as temp_dir:
                temp_dir = Path(temp_dir)
                dot_path = temp_dir / "page.dot"
                png_path = temp_dir / "page.png"
                dot_path.write_text(dot_text, encoding="utf-8")
                subprocess.run(
                    ["dot", "-Tpng", str(dot_path), "-o", str(png_path)],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                with Image.open(png_path) as image_file:
                    image = image_file.copy()
            self._set_image_preview(image)
            recipe_id = recipe.get("id", self.preview_page_index + 1) if isinstance(recipe, dict) else self.preview_page_index + 1
            self.image_page_status.set(f"Recipe {recipe_id} ({self.preview_page_index + 1}/{total})")
        except Exception as exc:
            self.image_page_status.set("Preview render failed.")
            self._reset_image_preview(f"Could not render recipe preview:\n{exc}")

    def _dot_escape(self, value):
        return str(value).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")

    def _tree_signature(self, tree):
        children = tree.get("children", []) if isinstance(tree, dict) else []
        child_signatures = sorted(self._tree_signature(child) for child in children)
        return f"{tree.get('name', '')}({','.join(child_signatures)})"

    def _build_preview_dot(self, page_recipes):
        visual_mode = self.preview_json_data.get("visual_mode", self.visual_mode.get()) if self.preview_json_data else self.visual_mode.get()
        shared_seen = {}
        next_id = 0
        lines = [
            "digraph RecipeResults {",
            "  graph [rankdir=TB, labelloc=t, fontsize=20, fontname=\"Arial\"];",
            "  node [fontname=\"Arial\"];",
            "  edge [fontname=\"Arial\"];",
            "  label=\"Little Alchemy Recipe Search\";",
            "",
        ]

        def node_style(tree, shared, truncated):
            if shared:
                return 'shape=box, style="dashed,filled", fillcolor="#fff3cd", color="#b8860b"'
            if truncated:
                return 'shape=box, style="rounded,filled", fillcolor="#f8d7da", color="#b02a37"'
            if tree.get("basic", False):
                return 'shape=ellipse, style=filled, fillcolor="#d1e7dd", color="#0f5132"'
            return 'shape=box, style="rounded,filled", fillcolor="#e7f1ff", color="#084298"'

        max_depth = -1
        if self.max_visual_depth_enabled.get():
            try:
                parsed_depth = int(self.max_visual_depth.get().strip())
                if parsed_depth >= 0:
                    max_depth = parsed_depth
            except ValueError:
                max_depth = -1

        def add_node(tree, recipe_id, depth):
            nonlocal next_id
            children = tree.get("children", []) if isinstance(tree, dict) else []
            depth_limited = max_depth >= 0 and depth >= max_depth and bool(children)
            signature = self._tree_signature(tree)
            can_share = visual_mode == "shared" and not tree.get("basic", False) and not depth_limited and depth > 0
            if can_share and signature in shared_seen:
                node_id = f"n{next_id}"
                next_id += 1
                _, seen_recipe_id = shared_seen[signature]
                label = f"{tree.get('name', '')}\n(shared, see Recipe {seen_recipe_id})"
                lines.append(f"    {node_id} [label=\"{self._dot_escape(label)}\", {node_style(tree, True, False)}];")
                return node_id, True

            node_id = f"n{next_id}"
            next_id += 1
            label = str(tree.get("name", ""))
            if depth_limited:
                label += "\n(truncated)"
            lines.append(f"    {node_id} [label=\"{self._dot_escape(label)}\", {node_style(tree, False, depth_limited)}];")

            if can_share:
                shared_seen[signature] = (node_id, recipe_id)
            if not depth_limited:
                for child in children:
                    child_id, child_shared = add_node(child, recipe_id, depth + 1)
                    style = " [style=dashed]" if child_shared else ""
                    lines.append(f"    {node_id} -> {child_id}{style};")
            return node_id, False

        for fallback_index, recipe in enumerate(page_recipes, start=1):
            recipe_id = recipe.get("id", fallback_index)
            lines.append(f"  subgraph cluster_recipe_{recipe_id} {{")
            lines.append(f"    label=\"Recipe {recipe_id}\";")
            lines.append("    color=\"#adb5bd\";")
            add_node(recipe.get("tree", {}), recipe_id, 0)
            lines.append("  }")
            lines.append("")

        lines.append("}")
        return "\n".join(lines) + "\n"

    def _reset_image_preview(self, text):
        self.preview_image = None
        self.preview_pil_image = None
        try:
            self.image_label.destroy()
        except Exception:
            pass
        self.image_label = ctk.CTkLabel(self.tabs.tab("Image"), text=text)
        self.image_label.grid(row=1, column=0, sticky="nsew", padx=12, pady=12)

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
        self.image_label.grid(row=1, column=0, sticky="nsew", padx=12, pady=12)

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

    def _open_compare_csv(self):
        open_path(Path(f"{self.output_prefix.get()}_compare.csv"))


def _parse_cli_args(argv):
    import argparse

    parser = argparse.ArgumentParser(description="Little Alchemy HPC MPI GUI")
    parser.add_argument("--role", choices=["master", "slave"], help="Preset cluster role.")
    parser.add_argument("--engine", choices=["serial", "openmp", "mpi"], help="Preset engine.")
    parser.add_argument("--slots", type=int, help="Offered slots untuk role slave.")
    parser.add_argument("--autostart", action="store_true", help="Langsung Start Slave (role slave).")
    args = parser.parse_args(argv)
    return {
        "role": args.role,
        "engine": args.engine,
        "slots": args.slots,
        "autostart": args.autostart,
    }


if __name__ == "__main__":
    preset = _parse_cli_args(sys.argv[1:])
    app = AlchemyGui(preset=preset)
    app.mainloop()

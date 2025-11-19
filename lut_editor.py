#!/usr/bin/env python3
"""
SSD1677 LUT Editor with GUI
Interactive tool for creating and editing custom LUTs for e-paper displays

Architecture:
- Each transition (B->B, B->W, W->B, W->W) has a voltage pattern that cycles
- ALL transitions share the same timing groups (TP/RP)
- During Group 0 Phase A for X frames, each transition applies its own voltage pattern
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, filedialog
import json
from typing import List, Dict, Tuple

VS_MAP = {
    0b00: "VSS",
    0b01: "VSH1",
    0b10: "VSL",
    0b11: "VSH2",
}

VS_REVERSE = {v: k for k, v in VS_MAP.items()}

TRANSITION_NAMES = ["Black → Black", "Black → White", "White → Black", "White → White"]
VOLTAGE_OPTIONS = ["VSS", "VSH1", "VSL", "VSH2"]


class LUTEditor:
    def __init__(self, root):
        self.root = root
        self.root.title("SSD1677 LUT Editor")
        self.root.geometry("1500x900")

        # Voltage patterns for each transition (4 steps per byte, up to 10 bytes = 40 steps max)
        # Decoded from the original LUT hex values
        self.voltage_patterns = {
            0: ["VSS", "VSH1", "VSS", "VSH1"],  # B->B: 0x11 = 00,01,00,01
            1: ["VSL", "VSL", "VSL", "VSS"],  # B->W: 0xA8 = 10,10,10,00
            2: ["VSH1", "VSS", "VSH1", "VSS"],  # W->B: 0x44 = 01,00,01,00
            3: ["VSS", "VSL", "VSS", "VSL"],  # W->W: 0x22 = 00,10,00,10
        }

        # Global timing groups (shared by all transitions)
        # Each group: [TP_A, TP_B, TP_C, TP_D, RP]
        self.timing_groups = [
            [4, 4, 0, 0, 0],  # Group 0: A=4, B=4
            [2, 2, 0, 0, 0],  # Group 1: A=2, B=2
            [0, 0, 0, 0, 0],  # Group 2
            [0, 0, 0, 0, 0],  # Group 3
            [0, 0, 0, 0, 0],  # Group 4
            [0, 0, 0, 0, 0],  # Group 5
            [0, 0, 0, 0, 0],  # Group 6
            [0, 0, 0, 0, 0],  # Group 7
            [0, 0, 0, 0, 0],  # Group 8
            [0, 0, 0, 0, 0],  # Group 9
        ]

        # Frame rate and voltages
        self.frame_rate = 0x88
        self.voltages = {
            "VGH": 0x17,
            "VSH1": 0x41,
            "VSH2": 0xA8,
            "VSL": 0x32,
            "VCOM": 0x30,
        }

        self.create_widgets()
        self.update_preview()

    def create_widgets(self):
        # Create main paned window
        paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Left panel
        left_frame = ttk.Frame(paned)
        paned.add(left_frame, weight=2)

        # Right panel
        right_frame = ttk.Frame(paned)
        paned.add(right_frame, weight=1)

        self.create_left_panel(left_frame)
        self.create_right_panel(right_frame)

    def create_left_panel(self, parent):
        # Scrollable container
        canvas = tk.Canvas(parent)
        scrollbar = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
        scrollable_frame = ttk.Frame(canvas)

        scrollable_frame.bind(
            "<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )

        canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        # Title
        title_frame = ttk.Frame(scrollable_frame)
        title_frame.pack(fill=tk.X, padx=10, pady=10)
        ttk.Label(title_frame, text="SSD1677 LUT Editor", font=("", 14, "bold")).pack()
        ttk.Label(
            title_frame,
            text="Voltage patterns are per-transition, Timing groups are global",
            font=("", 9, "italic"),
        ).pack()

        # Voltage patterns section
        voltage_frame = ttk.LabelFrame(
            scrollable_frame, text="Voltage Patterns (per transition)", padding=10
        )
        voltage_frame.pack(fill=tk.X, padx=10, pady=5)

        self.voltage_widgets = {}
        for trans_idx in range(4):
            self.create_voltage_pattern_editor(voltage_frame, trans_idx)

        # Timing groups section
        timing_frame = ttk.LabelFrame(
            scrollable_frame,
            text="Timing Groups (global - used by all transitions)",
            padding=10,
        )
        timing_frame.pack(fill=tk.X, padx=10, pady=5)

        self.timing_widgets = []
        for group_idx in range(10):
            self.create_timing_group_editor(timing_frame, group_idx)

        # Global settings
        settings_frame = ttk.LabelFrame(
            scrollable_frame, text="Global Settings", padding=10
        )
        settings_frame.pack(fill=tk.X, padx=10, pady=10)
        self.create_settings_editor(settings_frame)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

    def create_voltage_pattern_editor(self, parent, trans_idx):
        """Create voltage pattern editor for one transition"""
        frame = ttk.Frame(parent)
        frame.pack(fill=tk.X, pady=3)

        ttk.Label(frame, text=f"{TRANSITION_NAMES[trans_idx]}:", width=18).pack(
            side=tk.LEFT
        )

        container = ttk.Frame(frame)
        container.pack(side=tk.LEFT, fill=tk.X, expand=True)

        self.voltage_widgets[trans_idx] = {"container": container}

        add_btn = ttk.Button(
            frame, text="+ Add", width=6, command=lambda: self.add_voltage(trans_idx)
        )
        add_btn.pack(side=tk.LEFT, padx=2)
        self.voltage_widgets[trans_idx]["add_btn"] = add_btn

        self.reload_voltage_pattern(trans_idx)

    def reload_voltage_pattern(self, trans_idx):
        """Reload voltage pattern widgets"""
        container = self.voltage_widgets[trans_idx]["container"]

        for widget in container.winfo_children():
            widget.destroy()

        pattern = self.voltage_patterns[trans_idx]
        for volt_idx, voltage in enumerate(pattern):
            v_frame = ttk.Frame(container)
            v_frame.pack(side=tk.LEFT, padx=2)

            combo = ttk.Combobox(
                v_frame, values=VOLTAGE_OPTIONS, width=7, state="readonly"
            )
            combo.set(voltage)
            combo.pack(side=tk.LEFT)
            combo.bind(
                "<<ComboboxSelected>>",
                lambda e, t=trans_idx, v=volt_idx: self.on_voltage_change(t, v),
            )

            if len(pattern) > 1:
                ttk.Button(
                    v_frame,
                    text="×",
                    width=2,
                    command=lambda t=trans_idx, v=volt_idx: self.remove_voltage(t, v),
                ).pack(side=tk.LEFT)

        # Enable/disable add button based on pattern length
        if "add_btn" in self.voltage_widgets[trans_idx]:
            add_btn = self.voltage_widgets[trans_idx]["add_btn"]
            if len(pattern) >= 40:  # Max 40 steps (10 bytes × 4 steps)
                add_btn.config(state="disabled")
            else:
                add_btn.config(state="normal")

    def add_voltage(self, trans_idx):
        """Add voltage to pattern"""
        if (
            len(self.voltage_patterns[trans_idx]) < 40
        ):  # Max 40 steps (10 bytes × 4 steps)
            self.voltage_patterns[trans_idx].append("VSS")
            self.reload_voltage_pattern(trans_idx)
            self.update_preview()

    def remove_voltage(self, trans_idx, volt_idx):
        """Remove voltage from pattern"""
        if len(self.voltage_patterns[trans_idx]) > 1:
            self.voltage_patterns[trans_idx].pop(volt_idx)
            self.reload_voltage_pattern(trans_idx)
            self.update_preview()

    def on_voltage_change(self, trans_idx, volt_idx):
        """Handle voltage change"""
        container = self.voltage_widgets[trans_idx]["container"]
        widgets = container.winfo_children()
        if volt_idx < len(widgets):
            combo = widgets[volt_idx].winfo_children()[0]
            self.voltage_patterns[trans_idx][volt_idx] = combo.get()
            self.update_preview()

    def create_timing_group_editor(self, parent, group_idx):
        """Create timing group editor"""
        frame = ttk.Frame(parent)
        frame.pack(fill=tk.X, pady=2)

        ttk.Label(frame, text=f"Group {group_idx}:", width=10).pack(side=tk.LEFT)

        entries = []
        for phase_name in ["A", "B", "C", "D"]:
            ttk.Label(frame, text=f"{phase_name}:").pack(side=tk.LEFT, padx=2)
            spin = ttk.Spinbox(frame, from_=0, to=255, width=5)
            spin.set(
                str(
                    self.timing_groups[group_idx][
                        ["A", "B", "C", "D"].index(phase_name)
                    ]
                )
            )
            spin.pack(side=tk.LEFT, padx=2)
            spin.bind("<KeyRelease>", lambda e, g=group_idx: self.on_timing_change(g))
            spin.bind("<<Increment>>", lambda e, g=group_idx: self.on_timing_change(g))
            spin.bind("<<Decrement>>", lambda e, g=group_idx: self.on_timing_change(g))
            entries.append(spin)

        ttk.Label(frame, text="RP:").pack(side=tk.LEFT, padx=5)
        rp_spin = ttk.Spinbox(frame, from_=0, to=255, width=5)
        rp_spin.set(str(self.timing_groups[group_idx][4]))
        rp_spin.pack(side=tk.LEFT, padx=2)
        rp_spin.bind("<KeyRelease>", lambda e, g=group_idx: self.on_timing_change(g))
        rp_spin.bind("<<Increment>>", lambda e, g=group_idx: self.on_timing_change(g))
        rp_spin.bind("<<Decrement>>", lambda e, g=group_idx: self.on_timing_change(g))
        entries.append(rp_spin)

        self.timing_widgets.append(entries)

    def on_timing_change(self, group_idx):
        """Handle timing change"""
        entries = self.timing_widgets[group_idx]
        for i in range(5):
            try:
                val = int(entries[i].get() or 0)
                self.timing_groups[group_idx][i] = val
            except ValueError:
                pass
        self.update_preview()

    def create_settings_editor(self, parent):
        """Create global settings editor"""
        # Frame rate
        fr_frame = ttk.Frame(parent)
        fr_frame.pack(fill=tk.X, pady=5)

        ttk.Label(fr_frame, text="Frame Rate:", font=("", 9, "bold")).pack(side=tk.LEFT)
        self.frame_rate_spin = ttk.Spinbox(fr_frame, from_=1, to=255, width=6)
        self.frame_rate_spin.set(str(self.frame_rate))
        self.frame_rate_spin.pack(side=tk.LEFT, padx=5)
        self.frame_rate_spin.bind("<KeyRelease>", lambda e: self.on_framerate_change())
        self.frame_rate_spin.bind("<<Increment>>", lambda e: self.on_framerate_change())
        self.frame_rate_spin.bind("<<Decrement>>", lambda e: self.on_framerate_change())
        ttk.Label(fr_frame, text="(lower = slower/stable)").pack(side=tk.LEFT)

        # Voltages
        v_frame = ttk.Frame(parent)
        v_frame.pack(fill=tk.X, pady=5)
        ttk.Label(v_frame, text="Voltages:", font=("", 9, "bold")).pack(anchor=tk.W)

        v_grid = ttk.Frame(v_frame)
        v_grid.pack(fill=tk.X, pady=2)

        self.voltage_entries = {}
        for i, name in enumerate(["VGH", "VSH1", "VSH2", "VSL", "VCOM"]):
            ttk.Label(v_grid, text=f"{name}:").grid(
                row=i, column=0, sticky=tk.W, padx=2, pady=2
            )
            entry = ttk.Entry(v_grid, width=8)
            entry.insert(0, f"0x{self.voltages[name]:02X}")
            entry.grid(row=i, column=1, padx=2, pady=2)
            entry.bind("<KeyRelease>", lambda e: self.on_voltage_setting_change())
            self.voltage_entries[name] = entry

    def on_framerate_change(self):
        """Handle frame rate change"""
        try:
            self.frame_rate = int(self.frame_rate_spin.get() or 0x44)
        except ValueError:
            pass
        self.update_preview()

    def on_voltage_setting_change(self):
        """Handle voltage setting change"""
        for name, entry in self.voltage_entries.items():
            try:
                text = entry.get().strip()
                if text.startswith("0x") or text.startswith("0X"):
                    val = int(text, 16)
                else:
                    val = int(text)
                self.voltages[name] = val & 0xFF
            except ValueError:
                pass
        self.update_preview()

    def create_right_panel(self, parent):
        """Create preview panel"""
        # Buttons
        button_frame = ttk.Frame(parent)
        button_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Button(button_frame, text="Load", command=self.load_from_file).pack(
            side=tk.LEFT, padx=2
        )
        ttk.Button(button_frame, text="Save", command=self.save_to_file).pack(
            side=tk.LEFT, padx=2
        )
        ttk.Button(button_frame, text="Reset", command=self.reset_to_default).pack(
            side=tk.LEFT, padx=2
        )

        # Timing info
        info_frame = ttk.LabelFrame(parent, text="Timing Info", padding=5)
        info_frame.pack(fill=tk.X, padx=5, pady=5)

        self.timing_label = ttk.Label(info_frame, text="Calculating...", font=("", 9))
        self.timing_label.pack()

        # Preview
        preview_frame = ttk.LabelFrame(parent, text="C Array Output", padding=5)
        preview_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.preview_text = scrolledtext.ScrolledText(
            preview_frame, wrap=tk.WORD, font=("Consolas", 9)
        )
        self.preview_text.pack(fill=tk.BOTH, expand=True)

        # Copy button
        ttk.Button(
            parent, text="Copy to Clipboard", command=self.copy_to_clipboard
        ).pack(fill=tk.X, padx=5, pady=5)

    def voltage_pattern_to_lut(self) -> List[int]:
        """Convert voltage patterns and timing to 112-byte LUT"""
        lut = [0x00] * 112

        # Generate VS blocks (L0-L3)
        for trans_idx in range(4):
            pattern = self.voltage_patterns[trans_idx]
            vs_start = trans_idx * 10

            # Encode pattern into bytes (4 steps per byte, 2 bits per step)
            # Remaining bytes stay 0x00
            byte_idx = 0
            step_idx = 0
            while step_idx < len(pattern) and byte_idx < 10:
                byte_val = 0
                for phase_in_byte in range(4):
                    if step_idx < len(pattern):
                        phase = pattern[step_idx]
                        shift = 6 - (phase_in_byte * 2)
                        byte_val |= VS_REVERSE.get(phase, 0) << shift
                        step_idx += 1
                    else:
                        break
                lut[vs_start + byte_idx] = byte_val
                byte_idx += 1

        # L4 (VCOM) stays 0x00

        # TP/RP groups (10 groups × 5 bytes)
        for group_idx in range(10):
            base = 50 + group_idx * 5
            for i in range(5):
                lut[base + i] = self.timing_groups[group_idx][i]

        # Frame rate (5 bytes, all same)
        for i in range(5):
            lut[100 + i] = self.frame_rate

        # Voltages
        lut[105] = self.voltages["VGH"]
        lut[106] = self.voltages["VSH1"]
        lut[107] = self.voltages["VSH2"]
        lut[108] = self.voltages["VSL"]
        lut[109] = self.voltages["VCOM"]

        return lut

    def calculate_timing(self) -> Tuple[int, int]:
        """Calculate total frames and estimated time"""
        total_frames = 0

        for group in self.timing_groups:
            tp_sum = sum(group[:4])  # A + B + C + D
            rp = group[4]
            total_frames += tp_sum * (rp + 1)  # RP=0 means once

        if self.frame_rate > 0:
            ms_per_frame = 2500 / self.frame_rate
        else:
            ms_per_frame = 50

        if ms_per_frame < 10:
            ms_per_frame = 10

        refresh_time = int(total_frames * ms_per_frame * 1.1)

        return total_frames, refresh_time

    def update_preview(self):
        """Update preview"""
        lut = self.voltage_pattern_to_lut()
        total_frames, refresh_time = self.calculate_timing()

        self.timing_label.config(
            text=f"Total frames: {total_frames} | Est. time: ~{refresh_time}ms"
        )

        # Generate C code
        output = "const unsigned char lut_custom[] PROGMEM = {\n"

        # VS blocks
        output += "  // VS L0-L3 (voltage patterns per transition)\n"
        for trans_idx in range(4):
            pattern_str = "→".join(self.voltage_patterns[trans_idx])
            output += f"  // {TRANSITION_NAMES[trans_idx]}: [{pattern_str}]\n  "
            for byte_idx in range(10):
                output += f"0x{lut[trans_idx * 10 + byte_idx]:02X},"
            output += "\n"

        output += "  // L4 (VCOM)\n  "
        for i in range(10):
            output += f"0x{lut[40 + i]:02X},"
        output += "\n"

        output += "\n  // TP/RP groups (global timing)\n"
        for group_idx in range(10):
            base = 50 + group_idx * 5
            timing = self.timing_groups[group_idx]
            output += f"  "
            for i in range(5):
                output += f"0x{lut[base + i]:02X},"
            output += f"  // G{group_idx}: A={timing[0]} B={timing[1]} C={timing[2]} D={timing[3]} RP={timing[4]}"
            if sum(timing[:4]) > 0:
                output += f" ({sum(timing[:4]) * (timing[4] + 1)} frames)"
            output += "\n"

        output += "\n  // Frame rate\n  "
        for i in range(5):
            output += f"0x{lut[100 + i]:02X},"
        output += "\n"

        output += "\n  // Voltages (VGH, VSH1, VSH2, VSL, VCOM)\n  "
        for i in range(5):
            output += f"0x{lut[105 + i]:02X},"
        output += "\n"

        output += "\n  // Reserved\n  0x00,0x00\n};\n"

        self.preview_text.delete(1.0, tk.END)
        self.preview_text.insert(1.0, output)

    def copy_to_clipboard(self):
        """Copy to clipboard"""
        text = self.preview_text.get(1.0, tk.END)
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        messagebox.showinfo("Copied", "LUT copied to clipboard!")

    def save_to_file(self):
        """Save to JSON"""
        filename = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
        )
        if filename:
            data = {
                "voltage_patterns": self.voltage_patterns,
                "timing_groups": self.timing_groups,
                "frame_rate": self.frame_rate,
                "voltages": self.voltages,
            }
            with open(filename, "w") as f:
                json.dump(data, f, indent=2)
            messagebox.showinfo("Saved", f"Saved to {filename}")

    def load_from_file(self):
        """Load from JSON"""
        filename = filedialog.askopenfilename(
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
        )
        if filename:
            try:
                with open(filename, "r") as f:
                    data = json.load(f)
                    self.voltage_patterns = {
                        int(k): v for k, v in data["voltage_patterns"].items()
                    }
                    self.timing_groups = data["timing_groups"]
                    self.frame_rate = data["frame_rate"]
                    self.voltages = data["voltages"]

                    # Reload UI
                    for i in range(4):
                        self.reload_voltage_pattern(i)

                    for g in range(10):
                        for t in range(5):
                            self.timing_widgets[g][t].set(str(self.timing_groups[g][t]))

                    self.frame_rate_spin.set(str(self.frame_rate))

                    for name in self.voltages:
                        self.voltage_entries[name].delete(0, tk.END)
                        self.voltage_entries[name].insert(
                            0, f"0x{self.voltages[name]:02X}"
                        )

                    self.update_preview()
                    messagebox.showinfo("Loaded", f"Loaded from {filename}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to load: {e}")

    def reset_to_default(self):
        """Reset to defaults"""
        if messagebox.askyesno("Reset", "Reset to default?"):
            self.voltage_patterns = {
                0: ["VSL", "VSH1"],
                1: ["VSH2", "VSH1"],
                2: ["VSL", "VSL"],
                3: ["VSH1", "VSS"],
            }
            self.timing_groups = [
                [10, 10, 0, 0, 0],
                [8, 8, 0, 0, 0],
                [0, 0, 0, 0, 0],
                [0, 0, 0, 0, 0],
                [0, 0, 0, 0, 0],
                [0, 0, 0, 0, 0],
                [0, 0, 0, 0, 0],
                [0, 0, 0, 0, 0],
                [0, 0, 0, 0, 0],
                [0, 0, 0, 0, 0],
            ]

            for i in range(4):
                self.reload_voltage_pattern(i)

            for g in range(10):
                for t in range(5):
                    self.timing_widgets[g][t].set(str(self.timing_groups[g][t]))

            self.update_preview()


def main():
    root = tk.Tk()
    app = LUTEditor(root)
    root.mainloop()


if __name__ == "__main__":
    main()

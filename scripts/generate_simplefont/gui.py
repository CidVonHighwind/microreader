#!/usr/bin/env python3
"""
Simple GUI for previewing glyphs from TTF fonts using render_glyph_from_ttf.
"""

import tkinter as tk
from tkinter import filedialog, messagebox
from PIL import Image, ImageTk, ImageDraw
import os
import sys
from fontTools.ttLib import TTFont
import freetype

# Ensure the repository root is on sys.path
if __package__ is None:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    if repo_root not in sys.path:
        sys.path.insert(0, repo_root)

from scripts.generate_simplefont.render import render_glyph_from_ttf
from scripts.generate_simplefont import cli as gen_cli


class GlyphPreviewGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Glyph Preview GUI")

        # Variables
        default_ttf = os.path.join(repo_root, "data", "NotoSans.ttf")
        print(f"Default TTF path: {default_ttf}")
        self.ttf_path = tk.StringVar(value=default_ttf)
        self.size = tk.IntVar(value=24)
        # Use hex string input for character codes (e.g., 0x41)
        self.char_code = tk.StringVar(value="0x41")

        # Trace for auto-render
        self.ttf_path.trace_add("write", self.on_ttf_change)
        self.size.trace_add("write", self.on_value_change)
        self.char_code.trace_add("write", self.on_value_change)

        # TTF Path
        tk.Label(root, text="TTF Path:").grid(row=0, column=0, sticky="w")
        tk.Entry(root, textvariable=self.ttf_path, width=50).grid(row=0, column=1)
        tk.Button(root, text="Browse", command=self.browse_ttf).grid(row=0, column=2)
        # Export name
        tk.Label(root, text="Export name:").grid(row=0, column=3, sticky="w")
        self.export_name = tk.StringVar(value="NotoSans26")
        tk.Entry(root, textvariable=self.export_name).grid(row=0, column=4)

        # Size
        tk.Label(root, text="Size:").grid(row=1, column=0, sticky="w")
        tk.Entry(root, textvariable=self.size).grid(row=1, column=1, sticky="w")

        # Variations
        tk.Label(root, text="Variations:").grid(row=2, column=0, sticky="w")
        self.variations_frame = tk.Frame(root)
        self.variations_frame.grid(row=3, column=0, columnspan=5, sticky="w")
        self.variation_vars = {}

        # (debug UI removed) - simplified preview display

        # Trace for auto-render
        self.ttf_path.trace_add("write", self.on_ttf_change)
        self.size.trace_add("write", self.on_value_change)
        self.char_code.trace_add("write", self.on_value_change)

        # Char Code
        tk.Label(root, text="Char Code (decimal):").grid(row=4, column=0, sticky="w")
        tk.Entry(root, textvariable=self.char_code).grid(row=4, column=1, sticky="w")
        self.char_label = tk.Label(root, text="Char: A")
        self.char_label.grid(row=4, column=2)
        tk.Button(root, text="Prev", command=self.prev_char).grid(row=4, column=3)
        tk.Button(root, text="Next", command=self.next_char).grid(row=4, column=4)

        # Image Display
        self.image_label = tk.Label(root)
        self.image_label.grid(row=5, column=0, columnspan=5)

        # Metrics
        tk.Label(root, text="Width:").grid(row=6, column=0, sticky="w")
        self.width_label = tk.Label(root, text="")
        self.width_label.grid(row=6, column=1, sticky="w")

        tk.Label(root, text="Height:").grid(row=7, column=0, sticky="w")
        self.height_label = tk.Label(root, text="")
        self.height_label.grid(row=7, column=1, sticky="w")

        tk.Label(root, text="X Advance:").grid(row=8, column=0, sticky="w")
        self.xadvance_label = tk.Label(root, text="")
        self.xadvance_label.grid(row=8, column=1, sticky="w")

        tk.Label(root, text="X Offset:").grid(row=9, column=0, sticky="w")
        self.xoffset_label = tk.Label(root, text="")
        self.xoffset_label.grid(row=9, column=1, sticky="w")

        tk.Label(root, text="Y Offset:").grid(row=10, column=0, sticky="w")
        self.yoffset_label = tk.Label(root, text="")
        self.yoffset_label.grid(row=10, column=1, sticky="w")

        # Initial render
        self.on_ttf_change()
        # Save button
        tk.Button(root, text="Save", command=self.save_font).grid(row=11, column=4)

    def browse_ttf(self):
        path = filedialog.askopenfilename(
            filetypes=[("TTF files", "*.ttf"), ("OTF files", "*.otf")]
        )
        if path:
            self.ttf_path.set(path)

    def update_variations_ui(self, ttf_path):
        # Clearing existing variation controls
        # Clear existing variation controls
        for widget in self.variations_frame.winfo_children():
            widget.destroy()
        self.variation_vars.clear()

        try:
            # Loading font
            font = TTFont(ttf_path)
            # Prefer to detect variable axes via freetype, if available
            freetype_axes = None
            try:
                fface = freetype.Face(ttf_path)
                try:
                    vsi = fface.get_variation_info()
                    freetype_axes = vsi.axes
                except Exception as e:
                    freetype_axes = None
                    print(
                        f"update_variations_ui: freetype.get_variation_info failed: {e}"
                    )
            except Exception as e:
                freetype_axes = None
                print(f"update_variations_ui: freetype.Face failed: {e}")
            if "fvar" in font:
                axes = font["fvar"].axes
                axis_tags = [axis.axisTag for axis in axes]
                self.axes = axes
                row = 0
                for axis in axes:
                    tag = axis.axisTag
                    # Prefer freetype axis defaults if they exist
                    ft_axis = None
                    if freetype_axes is not None:
                        ft_axis = next(
                            (ax for ax in freetype_axes if ax.tag == tag), None
                        )
                    default_val = axis.defaultValue
                    if ft_axis is not None:
                        default_val = ft_axis.default
                    # add field for axis {tag}
                    tk.Label(self.variations_frame, text=f"{tag}:").grid(
                        row=row, column=0, sticky="w"
                    )
                    var = tk.StringVar(value=str(default_val))
                    self.variation_vars[tag] = var
                    tk.Entry(self.variations_frame, textvariable=var).grid(
                        row=row, column=1
                    )
                    var.trace_add("write", self.on_value_change)
                    row += 1
            else:
                # Font is not variable
                self.axes = []
        except Exception as e:
            print(f"update_variations_ui: Error loading font: {e}")
            self.axes = []

    def on_ttf_change(self, *args):
        ttf = self.ttf_path.get()
        if os.path.exists(ttf):
            self.update_variations_ui(ttf)
            self.render_glyph()
        else:
            self.clear_display()

    def on_value_change(self, *args):
        if os.path.exists(self.ttf_path.get()):
            self.render_glyph()

    def parse_char_code(self, s: str):
        """Parse a character code string (accepts single chars, hex '0xNN', bare hex 'NN', or decimal) and return integer or None."""
        if s is None:
            return None
        s = s.strip()
        if s == "":
            return None
        # Single character -> ord
        if len(s) == 1:
            return ord(s)
        import string

        try:
            # 0xNN hex form
            if s.lower().startswith("0x"):
                return int(s, 16)
            # bare hex digits (e.g., '41', '00A5') -> treat as hex
            if all(c in string.hexdigits for c in s):
                return int(s, 16)
            # else try decimal
            return int(s, 10)
        except Exception:
            return None

    def render_glyph(self):
        try:
            ttf = self.ttf_path.get()
            size = self.size.get()
            ch = self.parse_char_code(self.char_code.get())
            if ch is None or ch < 0 or ch > 0x10FFFF:
                messagebox.showwarning(
                    "Invalid character code",
                    f"Character code not valid: '{self.char_code.get()}'. Use '0x41', '41', '65', or a single char.",
                )
                return
            variations = {}
            for k, v in self.variation_vars.items():
                try:
                    variations[k] = float(v.get() or "0")
                except ValueError:
                    # If invalid (e.g., empty), use default
                    axis = next(
                        (a for a in getattr(self, "axes", []) if a.axisTag == k), None
                    )
                    variations[k] = axis.defaultValue if axis else 0.0
            # Clamp variations to axis ranges
            for tag, value in variations.items():
                axis = next(
                    (a for a in getattr(self, "axes", []) if a.axisTag == tag), None
                )
                if axis:
                    variations[tag] = max(min(value, axis.maxValue), axis.minValue)
            print(f"Rendering with variations: {variations}")

            width, height, grayscale_pixels, xadvance, xoffset, yoffset = (
                render_glyph_from_ttf(ch, ttf, size, variations=variations)
            )

            # Create PIL image
            glyph_img = Image.new("L", (width, height))
            glyph_img.putdata(grayscale_pixels)

            # Convert to RGBA with transparency for non-black parts
            glyph_rgba = Image.new("RGBA", (width, height))
            for y in range(height):
                for x in range(width):
                    gray = grayscale_pixels[y * width + x]
                    glyph_rgba.putpixel(
                        (x, y), (0, 0, 0, 255 - gray)
                    )  # Black with alpha = gray value

            # Scale up for better visibility
            zoom = 4
            glyph_rgba = glyph_rgba.resize((width * zoom, height * zoom), Image.NEAREST)

            # Create fixed size canvas
            canvas_size = 256
            img = Image.new("RGB", (canvas_size, canvas_size), (255, 255, 255))
            draw = ImageDraw.Draw(img)

            # Draw baseline and center lines (1px)
            baseline_y = canvas_size // 2
            center_x = canvas_size // 2
            draw.line(
                (0, baseline_y, canvas_size - 1, baseline_y), fill=(0, 0, 0)
            )  # Black baseline
            draw.line(
                (center_x, 0, center_x, canvas_size - 1), fill=(255, 0, 0)
            )  # Red center

            # Position the glyph
            glyph_x = center_x + xoffset * zoom
            glyph_y = baseline_y + yoffset * zoom
            img.paste(glyph_rgba, (int(glyph_x), int(glyph_y)), glyph_rgba)

            # Draw box around glyph
            box_left = glyph_x
            box_top = glyph_y
            box_right = glyph_x + width * zoom
            box_bottom = glyph_y + height * zoom
            draw.rectangle(
                [box_left, box_top, box_right, box_bottom], outline=(0, 255, 0)
            )  # Green box

            # Draw x advance line
            advance_x = glyph_x + xadvance * zoom
            draw.line(
                (advance_x, 0, advance_x, canvas_size - 1), fill=(0, 0, 255)
            )  # Blue advance

            # Convert to PhotoImage
            self.photo = ImageTk.PhotoImage(img)
            self.image_label.config(image=self.photo)
            self.root.update_idletasks()  # Force GUI update

            # Update labels
            self.width_label.config(text=str(width))
            self.height_label.config(text=str(height))
            self.xadvance_label.config(text=str(xadvance))
            self.xoffset_label.config(text=str(xoffset))
            self.yoffset_label.config(text=str(yoffset))
            self.char_label.config(text=f"Char: {chr(ch)}")
            # Debug area removed; nothing to show here

        except Exception as e:
            messagebox.showerror("Error", str(e))
            self.clear_display()

    def on_value_change(self, *args):
        if os.path.exists(self.ttf_path.get()):
            self.render_glyph()

    def clear_display(self):
        self.image_label.config(image="")
        self.width_label.config(text="")
        self.height_label.config(text="")
        self.xadvance_label.config(text="")
        self.xoffset_label.config(text="")
        self.yoffset_label.config(text="")
        self.char_label.config(text="")
        # Clear variations
        for widget in self.variations_frame.winfo_children():
            widget.destroy()
        self.variation_vars.clear()

    def prev_char(self):
        cur = self.parse_char_code(self.char_code.get())
        if cur is None:
            cur = 0x20
        cur = max(0, cur - 1)
        self.char_code.set(hex(cur))
        self.render_glyph()

    def next_char(self):
        cur = self.parse_char_code(self.char_code.get())
        if cur is None:
            cur = 0x20
        cur = min(0x10FFFF, cur + 1)
        self.char_code.set(hex(cur))
        self.render_glyph()

    def save_font(self):
        """Export the current font with applied variations using the CLI functionality."""
        # gather inputs
        ttf = self.ttf_path.get()
        if not os.path.exists(ttf):
            messagebox.showerror("Save Error", f"TTF not found: {ttf}")
            return
        size = self.size.get()
        name = self.export_name.get() or os.path.splitext(os.path.basename(ttf))[0]
        # default chars file
        chars_file = os.path.join(repo_root, "data", "chars_input.txt")
        if not os.path.exists(chars_file):
            # ask user for file
            chars_file = filedialog.askopenfilename(
                title="Choose chars file",
                filetypes=[("Text files", "*.txt"), ("All files", "*")],
            )
            if not chars_file:
                messagebox.showerror("Save Error", "Characters file not provided")
                return
        # default output path under repo src/Fonts
        default_out = os.path.join(repo_root, "src", "Fonts", f"{name}.h")
        out_path = filedialog.asksaveasfilename(
            title="Save Font Header",
            defaultextension=".h",
            initialfile=os.path.basename(default_out),
            initialdir=os.path.dirname(default_out),
            filetypes=[("C Header", "*.h"), ("All files", "*")],
        )
        if not out_path:
            return
        # Build variations args
        var_args = []
        for tag, var in self.variation_vars.items():
            try:
                val = float(var.get())
            except Exception:
                continue
            var_args.extend(["--var", f"{tag}={val}"])
        # Build argv as CLI expects
        argv = [
            "--name",
            name,
            "--size",
            str(size),
            "--ttf",
            ttf,
            "--chars-file",
            chars_file,
            "--out",
            out_path,
        ]
        argv.extend(var_args)

        # Run the CLI main() in a try/catch to avoid exiting the GUI
        try:
            gen_cli.main(argv)
            messagebox.showinfo("Export complete", f"Saved font header to: {out_path}")
        except SystemExit as e:
            # main may call sys.exit; treat exit code 0 as success
            if getattr(e, "code", None) == 0:
                messagebox.showinfo(
                    "Export complete", f"Saved font header to: {out_path}"
                )
            else:
                messagebox.showerror("Export failed", f"CLI exited with code {e.code}")
        except Exception as e:
            messagebox.showerror("Export failed", str(e))


if __name__ == "__main__":
    root = tk.Tk()
    app = GlyphPreviewGUI(root)
    root.mainloop()

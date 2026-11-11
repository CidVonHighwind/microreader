Import("env")
import hashlib
import os
import sys

_TOOLS_DIR = os.path.join(env.subst("$PROJECT_DIR"), "tools")
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)
import build_assets  # noqa: E402

project_dir = env.subst("$PROJECT_DIR")
esp32_dir = os.path.join(project_dir, "platforms", "esp32")
bin_path = os.path.join(esp32_dir, "assets.bin")
asm_path = os.path.join(esp32_dir, "assets_embedded.S")

is_x3 = "x3" in env["PIOENV"]
build_assets.build(project_dir, bin_path, build_assets.X3_ASSETS if is_x3 else build_assets.DEFAULT_ASSETS)
print(f"[generate_assets] assets.bin written ({os.path.getsize(bin_path):,} bytes)")

# Embed an MD5 of assets.bin as a comment so CMake detects content changes
# and recompiles the .S file whenever assets.bin is updated.
with open(bin_path, "rb") as bf:
    blob_hash = hashlib.md5(bf.read()).hexdigest()

bin_path_escaped = bin_path.replace("\\", "/")
asm = f"""\
    /* assets_md5: {blob_hash} */
    .section .rodata
    .global _binary_assets_bin_start
    .global _binary_assets_bin_end
_binary_assets_bin_start:
    .incbin \"{bin_path_escaped}\"
_binary_assets_bin_end:
"""
with open(asm_path, "w") as f:
    f.write(asm)
print(f"[generate_assets] assets_embedded.S written (blob md5={blob_hash})")

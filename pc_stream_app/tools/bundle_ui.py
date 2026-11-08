"""
Bundle UI - Assembler for PC Streamer HTML/JS Interface
Combines shell template, tab views, and scripts into a standalone index.html
"""

import os
import sys
import argparse
import re
from pathlib import Path

# Paths relative to this script: pc_stream_app/tools/bundle_ui.py
TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_DIR = TOOLS_DIR.parent
UI_DIR = PROJECT_DIR / "ui"
VIEWS_DIR = UI_DIR / "views"

TAB_FILES = [
    "tab_dashboard.html",
    "tab_mixer.html",
    "tab_routing.html",
    "tab_profiles.html",
    "tab_test.html",
    "tab_log.html",
    "tab_settings.html",
]

JS_DIR = UI_DIR / "js"
MODULAR_SCRIPT_FILES = [
    "ipc.js",
    "telemetry.js",
    "mixer.js",
    "routing.js",
    "profiles.js",
    "test.js",
    "settings.js",
    "app.js",
]

MODULAR_SCRIPT_TAGS = """  <!-- Application Modular Scripts -->
  <script src="js/ipc.js"></script>
  <script src="js/telemetry.js"></script>
  <script src="js/mixer.js"></script>
  <script src="js/routing.js"></script>
  <script src="js/profiles.js"></script>
  <script src="js/test.js"></script>
  <script src="js/settings.js"></script>
  <script src="js/app.js"></script>
"""

TAB_RANGES = [
    ("tab_dashboard.html", 223, 653),
    ("tab_mixer.html", 654, 1130),
    ("tab_routing.html", 1131, 1452),
    ("tab_profiles.html", 1453, 2218),
    ("tab_test.html", 2219, 2814),
    ("tab_log.html", 2815, 3229),
    ("tab_settings.html", 3230, 3647),
]


def extract_views(index_path=None, force=False):
    """Extracts 7 view tabs and shell from index.html"""
    if index_path is None:
        index_path = UI_DIR / "index.html"
    index_path = Path(index_path)

    if not index_path.exists():
        print(f"Error: Source file {index_path} not found.")
        sys.exit(1)

    VIEWS_DIR.mkdir(parents=True, exist_ok=True)

    with open(index_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    print(f"Reading {index_path} ({len(lines)} lines)...")

    # 1. Extract shell
    shell_path = UI_DIR / "shell.html"
    if not shell_path.exists() or force:
        part_a = "".join(lines[:222])
        part_b = "".join(lines[3647:3652])
        scripts = "".join(lines[3652:5254])
        part_c = "".join(lines[5254:])

        shell_content = (
            part_a
            + "<!-- {{TAB_PANELS}} -->\n"
            + part_b
            + "<!-- {{SCRIPTS}} -->\n"
            + part_c
        )
        with open(shell_path, "w", encoding="utf-8", newline="") as sf:
            sf.write(shell_content)
        print(f"Created shell template: {shell_path}")

        # Also extract script if not already present
        script_path = UI_DIR / "app_script.js"
        if not script_path.exists() or force:
            with open(script_path, "w", encoding="utf-8", newline="") as scf:
                scf.write(scripts)
            print(f"Extracted script to: {script_path}")

    # 2. Extract tab views
    for filename, start, end in TAB_RANGES:
        out_path = VIEWS_DIR / filename
        if out_path.exists() and not force:
            print(f"Skipping existing view (use --force to overwrite): {filename}")
            continue

        tab_lines = lines[start - 1 : end]
        with open(out_path, "w", encoding="utf-8", newline="") as vf:
            vf.writelines(tab_lines)
        print(f"Extracted {filename}: lines {start}-{end} ({len(tab_lines)} lines)")


def bundle_ui(output_path=None, shell_path=None, script_path=None, inline_scripts=False):
    """Bundles shell + 7 views + scripts into single output HTML file"""
    if shell_path is None:
        shell_path = UI_DIR / "shell.html"
    shell_path = Path(shell_path)

    if not shell_path.exists():
        print(f"Warning: {shell_path} not found. Attempting to generate from index.html...")
        extract_views(force=False)

    with open(shell_path, "r", encoding="utf-8") as sf:
        shell_content = sf.read()

    # Load 7 views
    views_combined = []
    missing_views = []
    for tab_file in TAB_FILES:
        p = VIEWS_DIR / tab_file
        if not p.exists():
            missing_views.append(str(p))
            continue
        with open(p, "r", encoding="utf-8") as vf:
            views_combined.append(vf.read())

    if missing_views:
        print(f"Error: Missing view files:\n" + "\n".join(f" - {v}" for v in missing_views))
        sys.exit(1)

    all_views_text = "".join(views_combined)

    # Determine scripts content
    if script_path:
        script_file = Path(script_path)
        if script_file.exists():
            with open(script_file, "r", encoding="utf-8") as scf:
                scripts_code = scf.read()
            scripts_block = f"  <script>\n{scripts_code}\n  </script>\n" if not scripts_code.strip().startswith("<script") else scripts_code
        else:
            scripts_block = MODULAR_SCRIPT_TAGS
    elif inline_scripts:
        # Inlined modular scripts concatenation
        inlined = []
        for sf in MODULAR_SCRIPT_FILES:
            sp = JS_DIR / sf
            if sp.exists():
                with open(sp, "r", encoding="utf-8") as f:
                    inlined.append(f"// === {sf} ===\n" + f.read())
        scripts_block = "  <script>\n" + "\n\n".join(inlined) + "\n  </script>\n"
    else:
        # Default: cleanly include modular scripts
        scripts_block = MODULAR_SCRIPT_TAGS

    # Replace placeholders in shell
    result = shell_content

    # Views placeholder (handle with or without trailing newline)
    views_placeholder = None
    for ph in ["<!-- {{TAB_PANELS}} -->\n", "<!-- {{TAB_PANELS}} -->", "<!-- {{VIEWS}} -->\n", "<!-- {{VIEWS}} -->", "<!-- TAB_VIEWS_PLACEHOLDER -->\n", "<!-- TAB_VIEWS_PLACEHOLDER -->"]:
        if ph in result:
            views_placeholder = ph
            break

    if views_placeholder:
        result = result.replace(views_placeholder, all_views_text)
    else:
        print("Error: Could not find views placeholder in shell.html (expected '<!-- {{TAB_PANELS}} -->')")
        sys.exit(1)

    # Scripts placeholder (handle with or without trailing newline)
    scripts_placeholder = None
    for ph in ["<!-- {{SCRIPTS}} -->\n", "<!-- {{SCRIPTS}} -->"]:
        if ph in result:
            scripts_placeholder = ph
            break

    if scripts_placeholder:
        result = result.replace(scripts_placeholder, scripts_block)
    else:
        print("Warning: Could not find scripts placeholder in shell.html (expected '<!-- {{SCRIPTS}} -->')")

    # Output path
    if output_path is None:
        output_path = UI_DIR / "index.html"
    output_path = Path(output_path)

    with open(output_path, "w", encoding="utf-8", newline="") as out:
        out.write(result)

    print(f"Successfully bundled UI to: {output_path} ({os.path.getsize(output_path):,} bytes)")
    return output_path


def verify_ui():
    """Verifies that all views and shell exist, IDs are valid, and bundle matches requirements"""
    print("=== VERIFYING UI VIEWS & BUNDLE ===")
    all_ok = True

    id_regex = re.compile(r'id=["\']([^"\']+)["\']')

    total_ids = 0
    for filename in TAB_FILES:
        path = VIEWS_DIR / filename
        if not path.exists():
            print(f"[FAIL] Missing view: {filename}")
            all_ok = False
            continue

        with open(path, "r", encoding="utf-8") as f:
            content = f.read()

        ids = id_regex.findall(content)
        total_ids += len(ids)

        # Check root element
        lines = content.strip().splitlines()
        first_line = lines[0] if lines else ""
        last_line = lines[-1] if lines else ""

        has_root_id = any(t in first_line for t in ["tab-dashboard", "tab-mixer", "tab-routing", "tab-profiles", "tab-test", "tab-log", "tab-settings"])
        closes_properly = last_line.strip() == "</div>"

        status = "[OK]" if has_root_id and closes_properly else "[WARN]"
        if not (has_root_id and closes_properly):
            all_ok = False

        print(f" {status} {filename:20s}: {len(lines):4d} lines, {len(content):6d} bytes, {len(ids):3d} element IDs (Root: {ids[0] if ids else 'None'})")

    print(f"Total element IDs across all 7 views: {total_ids}")

    # Test bundling to a temporary file
    temp_bundle = UI_DIR / "bundle_test.tmp.html"
    try:
        bundled_path = bundle_ui(output_path=temp_bundle)
        with open(bundled_path, "r", encoding="utf-8") as bf:
            b_text = bf.read()
        b_ids = id_regex.findall(b_text)
        print(f" [OK] Test bundle generated successfully: {len(b_text):,} bytes, {len(b_ids)} total IDs")
        if temp_bundle.exists():
            temp_bundle.unlink()
    except Exception as e:
        print(f"[FAIL] Bundle test failed: {e}")
        all_ok = False
        if temp_bundle.exists():
            temp_bundle.unlink()

    return all_ok


def main():
    parser = argparse.ArgumentParser(description="PC Streamer UI View Assembler & Extractor")
    parser.add_argument("--extract", action="store_true", help="Extract tab views and shell from index.html")
    parser.add_argument("--bundle", action="store_true", help="Bundle shell + views + scripts into index.html")
    parser.add_argument("--verify", action="store_true", help="Verify all views and bundle integrity")
    parser.add_argument("--force", action="store_true", help="Force overwrite existing files when extracting")
    parser.add_argument("--inline-scripts", action="store_true", help="Inline all JS modules into a single <script> block")
    parser.add_argument("--output", type=str, default=None, help="Output file path for bundled HTML")
    parser.add_argument("--shell", type=str, default=None, help="Custom shell HTML file path")
    parser.add_argument("--script", type=str, default=None, help="Custom script JS/HTML file path")

    args = parser.parse_args()

    if args.extract:
        extract_views(force=args.force)
    elif args.verify:
        success = verify_ui()
        sys.exit(0 if success else 1)
    else:
        # Default behavior: if views exist, verify and bundle, else extract
        if not (VIEWS_DIR / "tab_dashboard.html").exists():
            print("Views not detected. Extracting first...")
            extract_views()
        bundle_ui(
            output_path=args.output,
            shell_path=args.shell,
            script_path=args.script,
            inline_scripts=args.inline_scripts,
        )
        verify_ui()


if __name__ == "__main__":
    main()

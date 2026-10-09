#!/usr/bin/env python3
"""The gate a game must pass to ship in PlaySuite.

paths   The source limit for every game a change touches. For a pull request
        from a fork, also: the change stays inside one games/<id>/ folder, carries
        no prebuilt binaries, its build.cmake only declares ordinary library and
        test targets, its C++ keeps the house style's mechanical rules (no auto,
        no ->, no lambdas, no coroutines, no ranges pipelines, no defaulted
        comparisons, no std::any), which nothing can waive, and its shipped code
        avoids constructs that only make sense when working around the language
        (raw pointer casts, pointer-sized integers, platform headers, assembly,
        setjmp, compiler attributes and the like) unless a maintainer has
        reviewed that use for that game.
prepared
        The shipped limit: each game's transcoded audio and copied resources in
        a prepared runtime folder.
symbols What each game's shipped code can reach. Every library object compiled
        from a game's folder is read with llvm-nm and llvm-objdump; the symbols it
        needs from outside must be in the base allowlist or in a capability
        tools/game_approvals.json grants that game, and no object may contain a
        system-call instruction. A game without approvals gets the base set only.

The approvals file is maintained by PlaySuite, outside every game folder, so a
contribution cannot approve itself. Symbols show what code can call; together
with the scan for code that looks like it is reaching around the language, they
leave deliberate memory corruption as the remaining route, which review covers.
"""
import argparse
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APPROVALS = ROOT / "tools/game_approvals.json"

# Plain C functions every game may use: arithmetic, memory and strings, number
# conversion, character classes, the C++ runtime, stack protection, thread-local
# storage, diagnostics on stderr, and static destructors (__cxa_atexit, or atexit
# where MinGW registers them).
BASE_C = set("""
sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh exp exp2 exp10 expm1
log log2 log10 log1p pow sqrt cbrt hypot fmod remainder floor ceil round trunc lround
llround lrint llrint lrintf llrintf rint nearbyint ldexp frexp modf fabs fmin fmax fma copysign nan
sinf cosf tanf asinf acosf atanf atan2f sinhf coshf tanhf expf exp2f logf log2f log10f
powf sqrtf cbrtf hypotf fmodf floorf ceilf roundf truncf lroundf ldexpf fabsf fminf fmaxf
__sincos_stret __sincosf_stret __exp10 sincos sincosf
memcpy memmove memset memcmp memchr wmemchr wmemcpy wmemmove wmemset bzero strlen
strcmp strncmp strchr strrchr strstr strcpy strncpy strcat memset_pattern4
memset_pattern8 memset_pattern16 __memcpy_chk __memmove_chk __memset_chk
atof atoi atol atoll strtod strtof strtold strtol strtoul strtoll strtoull
snprintf vsnprintf sscanf __snprintf_chk __vsnprintf_chk
__maskrune __tolower __toupper __DefaultRuneLocale _DefaultRuneLocale tolower toupper isalpha isdigit
isspace isalnum isupper islower ispunct isxdigit __ctype_b_loc __ctype_tolower_loc
__ctype_toupper_loc rand srand abs labs llabs div qsort bsearch
__cxa_allocate_exception __cxa_free_exception __cxa_throw __cxa_rethrow
__cxa_begin_catch __cxa_end_catch __cxa_guard_acquire __cxa_guard_release
__cxa_guard_abort __cxa_pure_virtual __cxa_deleted_virtual __cxa_atexit
__cxa_thread_atexit __cxa_thread_atexit_impl __cxa_finalize atexit __dynamic_cast
__dso_handle __gxx_personality_v0 __gxx_personality_seh0 _Unwind_Resume
__stack_chk_fail __stack_chk_guard __tlv_atexit __tlv_bootstrap _tlv_atexit _tlv_bootstrap __tls_get_addr
__chkstk __chkstk_ms ___chkstk_ms __chkstk_darwin __emutls_get_address _tls_index
fprintf fputs fputc __stderrp stderr __acrt_iob_func __iob_func
""".split())

# Capabilities a game may be granted. Each lists the plain symbols and the
# demangled C++ patterns that need it.
CAPABILITIES = {
    "files": {
        # The 64-bit seeks and setbuf are what libc++'s file streams compile to on Windows.
        "c": set("fopen fclose fread fwrite fflush fseek ftell rewind fgets fgetc fputs "
                 "remove rename _wfopen tmpfile _fseeki64 _ftelli64 fseeko ftello setbuf".split()),
        "cxx": [r"std::(__1::)?__fs::", r"std::(__1::)?basic_(i|o)?fstream", r"std::(__1::)?basic_filebuf",
                r"std::filesystem::"],
    },
    "threads": {
        "c": set("pthread_create pthread_join pthread_detach pthread_setspecific pthread_getspecific "
                 "pthread_key_create _beginthreadex CreateThread".split()),
        "cxx": [r"std::(__1::)?thread::", r"std::(__1::)?__thread_struct", r"std::(__1::)?__thread_local_data",
                r"std::(__1::)?__thread_specific_ptr"],
    },
    "environment": {"c": set("getenv secure_getenv _wgetenv _dupenv_s".split()), "cxx": []},
    "scripting": {"c": set(), "cxx": [], "prefix": "JS_"},
}

# PlaySuite's own C++ namespaces: GUI.Forms, the shell's game API and the shared
# engines in shared/.
NAMESPACES = ("gui_forms::", "games::", "ambient::", "coverage::", "grass::", "paint::", "soil::",
              "felt::", "cards::", "puzzles::", "render::", "__cxxabiv1::")

# The standard library, apart from what the capabilities above cover.
STD = re.compile(r"(^|[\s(<,*&])std::")

# An instruction line from llvm-objdump: address, then the mnemonic and operands.
INSTRUCTION = re.compile(r"^\s*[0-9a-f]+:\s+([a-z][a-z0-9.]*)\s*(.*)$")


def system_call(line):
    match = INSTRUCTION.match(line)
    if not match:
        return False
    mnemonic, operands = match.group(1), match.group(2)
    return mnemonic in ("syscall", "sysenter", "svc") or (mnemonic == "int" and "0x80" in operands)


def load_approvals():
    data = json.loads(APPROVALS.read_text(encoding="utf-8"))
    return data["limits"], data["games"]


def run(arguments, text_input=None):
    return subprocess.run(arguments, input=text_input, capture_output=True, text=True, check=True).stdout


# ---------------------------------------------------------------- paths

BINARY_MAGIC = (b"\x7fELF", b"MZ", b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe",
                b"!<arch>", b"\xfe\xed\xfa\xcf", b"\x00asm")
BUILD_FORBIDDEN = re.compile(
    r"\b(execute_process|add_custom_command|add_custom_target|FetchContent\w*|ExternalProject\w*|"
    r"include|find_package|find_library|find_program|link_directories|target_link_options|"
    r"target_link_directories|add_link_options|set_property|set_target_properties|install|"
    r"try_compile|try_run|IMPORTED|file)\s*\(", re.IGNORECASE)
LINK = re.compile(r"target_link_libraries\s*\(([^)]*)\)", re.IGNORECASE)
# What a game's own targets may link besides each other.
ALLOWED_LINKS = {"PRIVATE", "PUBLIC", "INTERFACE", "vendor_game_ui", "game_ui", "game_rules",
                 "game_raster", "game_felt", "game_audio_adapters", "game_paths", "GUIForms::Audio",
                 "GUIForms::Application", "GUIForms::Threading"}
# Constructs a game has no ordinary use for. Each is a common step in turning
# memory into code or reaching the platform directly, so contributed code that
# uses one waits for a maintainer instead of guessing.
SUSPICIOUS = [
    (re.compile(r"\b(asm|__asm__|__asm)\b"), "inline assembly"),
    (re.compile(r"\breinterpret_cast\b"), "reinterpret_cast"),
    (re.compile(r"\bconst_cast\b"), "const_cast"),
    (re.compile(r"\b(u?intptr_t)\b"), "pointer-sized integers"),
    (re.compile(r"\bunion\b"), "unions (type punning)"),
    (re.compile(r"\b(setjmp|longjmp|sigsetjmp|siglongjmp)\b"), "setjmp/longjmp"),
    (re.compile(r'\bextern\s+"C"'), 'extern "C" declarations'),
    (re.compile(r"\b(__attribute__|__declspec)\b|\[\[\s*(gnu|clang|msvc)::"), "compiler attributes"),
    (re.compile(r"^\s*#\s*pragma\b(?!\s+once\s*$)", re.M), "#pragma (other than #pragma once)"),
    (re.compile(r"\b__builtin_\w+"), "compiler builtins"),
    (re.compile(r"\bvolatile\b"), "volatile"),
    (re.compile(r"\bgoto\b"), "goto"),
    (re.compile(r"\b_?alloca\b"), "alloca"),
    (re.compile(r"\(\s*(?:const\s+)?[A-Za-z_][\w:]*(?:\s*<[^()]*>)?\s*\*+\s*(?:const\s*)?\)\s*[A-Za-z_(&]"),
     "C-style pointer casts"),
    (re.compile(r'^\s*#\s*include\s*<(windows\.h|winsock\w*\.h|unistd\.h|dlfcn\.h|pthread\.h|spawn\.h|'
                r'signal\.h|csignal|csetjmp|setjmp\.h|sys/[^>]+|mach/[^>]+|mach-o/[^>]+|linux/[^>]+|'
                r'arpa/[^>]+|netinet/[^>]+|netdb\.h|objc/[^>]+|fstream|filesystem|thread)>', re.M),
     "platform or file/thread headers (use PlaySuite's game API)"),
]
SOURCE_SUFFIXES = (".cpp", ".hpp", ".h", ".c", ".cc", ".cxx", ".inl", ".ipp")


def house_style():
    """scripts/check-style.py: the house style's mechanical rules, which are absolute."""
    spec = importlib.util.spec_from_file_location("check_style", ROOT / "scripts/check-style.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Beyond scripts/check-style.py, the house style also excludes std::any.
HOUSE_EXTRA = [(re.compile(r"\bstd\s*::\s*any\b"), "std::any")]
# Folders that never ship: tests and tools run only in CI and development.
UNSHIPPED = ("tests", "tools", "dev")
CODE_NOISE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)


def code_only(text):
    """Blanks comments and literals, keeping line breaks so line numbers stay true."""
    def blank(match):
        piece = match.group(0)
        if piece.startswith('"') and "include" in text[max(0, match.start() - 12):match.start()]:
            return piece  # keep #include "file" names
        return re.sub(r"[^\n]", " ", piece)
    return CODE_NOISE.sub(blank, text)
FORBIDDEN_SUFFIXES = (".m", ".mm", ".s", ".S", ".asm")


def changed_files(base, head):
    out = run(["git", "-C", str(ROOT), "diff", "--name-only", "--no-renames", base + "..." + head])
    return [line for line in out.splitlines() if line]


def game_of(path):
    parts = Path(path).parts
    return parts[1] if len(parts) > 2 and parts[0] == "games" else None


def check_build_cmake(folder, problems):
    script = folder / "build.cmake"
    if not script.exists():
        return
    text = script.read_text(encoding="utf-8")
    own = set(re.findall(r"add_(?:library|executable)\s*\(\s*([A-Za-z0-9_]+)", text))
    for match in BUILD_FORBIDDEN.finditer(text):
        problems.append(f"{script.relative_to(ROOT)}: {match.group(1)}() is not allowed in a game's build")
    for match in LINK.finditer(text):
        for item in match.group(1).split()[1:]:
            if item not in ALLOWED_LINKS and item not in own:
                problems.append(f"{script.relative_to(ROOT)}: links {item}, which a game may not link")


def check_game_folder(folder, game, fork, limits, approvals, style):
    """One game folder's rules: the source limit, and for a contribution the build,
    house-style and code rules. The submission check (new-games/tools/check_game.py)
    runs this too, so a contributor sees the same results before opening a PR."""
    problems = []
    approved = approvals.get(game, {})
    folder_limit = approved.get("source_mb", limits["source_mb"]) * 1024 * 1024
    total = 0
    for path in sorted(folder.rglob("*")):
        if not path.is_file() or path.is_symlink():
            if path.is_symlink():
                problems.append(f"{path.relative_to(ROOT)}: symbolic links are not allowed")
            continue
        total += path.stat().st_size
        with path.open("rb") as stream:
            head_bytes = stream.read(8)
        if any(head_bytes.startswith(magic) for magic in BINARY_MAGIC):
            problems.append(f"{path.relative_to(ROOT)}: prebuilt binaries are not allowed; ship source")
        if fork and path.suffix in FORBIDDEN_SUFFIXES:
            problems.append(f"{path.relative_to(ROOT)}: Objective-C and assembly sources are not allowed")
        shipped = path.relative_to(folder).parts[0] not in UNSHIPPED
        if fork and path.suffix in SOURCE_SUFFIXES:
            raw = path.read_text(encoding="utf-8", errors="replace")
            text = code_only(raw)
            for finding in style.inspect(raw):
                problems.append(f"{path.relative_to(ROOT)}:{finding.line}: breaks the house style "
                                f"({finding.rule}); this cannot be waived")
            for pattern, what in HOUSE_EXTRA:
                match = pattern.search(text)
                if match:
                    line = text.count("\n", 0, match.start()) + 1
                    problems.append(f"{path.relative_to(ROOT)}:{line}: breaks the house style ({what}); "
                                    "this cannot be waived")
            reviewed = set(approved.get("reviewed", []))
            for pattern, what in SUSPICIOUS if shipped else []:
                match = pattern.search(text)
                if match and what not in reviewed:
                    line = text.count("\n", 0, match.start()) + 1
                    problems.append(f"{path.relative_to(ROOT)}:{line}: uses {what}, which needs a maintainer's review")
    if total > folder_limit:
        problems.append(f"games/{game}: {total / 1048576:.1f} MB of source exceeds the "
                        f"{folder_limit / 1048576:.0f} MB limit")
    if fork:
        check_build_cmake(folder, problems)
    return problems


def check_paths(base, head, fork):
    limits, approvals = load_approvals()
    files = changed_files(base, head)
    games = sorted({game_of(f) for f in files if game_of(f)})
    problems = []
    style = house_style()
    if fork:
        outside = [f for f in files if game_of(f) is None]
        if outside:
            problems.append("a contributed game changes only its own games/<id>/ folder; also changed: "
                            + ", ".join(outside[:10]))
        if len(games) > 1:
            problems.append("a contribution adds or changes one game; changed: " + ", ".join(games))
    for game in games:
        folder = ROOT / "games" / game
        if folder.is_dir():
            problems.extend(check_game_folder(folder, game, fork, limits, approvals, style))
    return games, problems


# ---------------------------------------------------------------- prepared

def check_prepared(runtime):
    limits, approvals = load_approvals()
    manifest = json.loads((runtime / "audio/portable_manifest.json").read_text(encoding="utf-8"))
    sizes = {}
    for record in manifest["files"]:
        game = record["source"].split("/")[0] if "/" in record["source"] else None
        if game:
            sizes[game] = sizes.get(game, 0) + (runtime / "audio" / record["file"]).stat().st_size
    for record in manifest["module_resources"]:
        game = record["file"].split("/")[0]
        sizes[game] = sizes.get(game, 0) + (runtime / record["file"]).stat().st_size
    problems = []
    for game, size in sorted(sizes.items()):
        if not (ROOT / "games" / game).is_dir():
            continue  # PlaySuite's shared assets, not a game
        limit = approvals.get(game, {}).get("prepared_mb", limits["prepared_mb"]) * 1024 * 1024
        print(f"games/{game}: {size / 1048576:.1f} MB prepared")
        if size > limit:
            problems.append(f"games/{game}: {size / 1048576:.1f} MB prepared exceeds the "
                            f"{limit / 1048576:.0f} MB limit")
    return problems


# ---------------------------------------------------------------- symbols

def game_objects(build):
    """Objects compiled from each game's folder into a library (what ships)."""
    result = {}
    for directory in build.glob("CMakeFiles/*.dir"):
        target = directory.name[:-4]
        is_library = any(build.glob(f"lib{target}.a")) or any(build.glob(f"{target}.lib")) or \
            target in ("vendor_game_ui", "game_audio_adapters", "game_ui")
        if not is_library:
            continue
        for obj in directory.rglob("*.o*"):
            if obj.suffix not in (".o", ".obj"):
                continue
            match = re.search(r"[/\\]games[/\\]([a-z0-9_]+)[/\\]", str(obj))
            if match:
                result.setdefault(match.group(1), []).append(obj)
    return result


def normalise(name, macho):
    if name.startswith("__imp_"):
        name = name[len("__imp_"):]
    if macho and name.startswith("_"):
        name = name[1:]
    return name


def external_symbols(objects, nm, filt):
    defined, undefined = set(), set()
    macho = False
    for obj in objects:
        with obj.open("rb") as stream:
            macho = macho or stream.read(4) in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe")
        for line in run([nm, "-P", str(obj)]).splitlines():
            parts = line.split()
            if len(parts) >= 2:
                (undefined if parts[1] == "U" else defined).add(parts[0])
    raw = sorted(undefined - defined)
    plain = [normalise(n, macho) for n in raw]
    demangled = run([filt], "\n".join(plain)).splitlines() if plain else []
    return list(zip(plain, demangled))


def classify(plain, demangled):
    """Returns 'base', a capability name, or None for an unknown symbol."""
    if plain in BASE_C:
        return "base"
    for name, capability in CAPABILITIES.items():
        if plain in capability["c"] or (capability.get("prefix") and plain.startswith(capability["prefix"])):
            return name
        for pattern in capability["cxx"]:
            if re.search(pattern, demangled):
                return name
    head = demangled.split("(")[0]
    if demangled.startswith("operator new") or demangled.startswith("operator delete"):
        return "base"
    if any(namespace in head for namespace in NAMESPACES):
        return "base"
    if STD.search(head) or head.startswith("typeinfo for std::") or head.startswith("vtable for std::"):
        return "base"
    if re.match(r"(typeinfo|vtable|typeinfo name|VTT) for (__cxxabiv1|std)::", demangled):
        return "base"
    return None


def check_symbols(build, nm, filt, objdump, suggest, only=None):
    limits, approvals = load_approvals()
    problems, warnings, found = [], [], {}
    objects = game_objects(build)
    if only is not None:
        objects = {game: objs for game, objs in objects.items() if game in only}
    if not objects:
        return ["no game library objects found under " + str(build)], [], {}
    for game, objs in sorted(objects.items()):
        approved = approvals.get(game, {})
        granted = set(approved.get("capabilities", []))
        first_party = approved.get("first_party", False)
        used = set()
        for plain, demangled in external_symbols(objs, nm, filt):
            kind = classify(plain, demangled)
            if kind == "base":
                continue
            if kind is None:
                message = f"games/{game}: calls {demangled}, which no game capability covers"
                (warnings if first_party else problems).append(message)
                continue
            used.add(kind)
            if kind not in granted:
                problems.append(f"games/{game}: needs the '{kind}' capability ({demangled}), "
                                "which is not approved for it")
        found[game] = sorted(used)
        for obj in objs:
            listing = run([objdump, "-d", "--no-show-raw-insn", str(obj)])
            if any(system_call(line) for line in listing.splitlines()):
                problems.append(f"games/{game}: {obj.name} contains a system-call instruction")
    if suggest:
        print(json.dumps(found, indent=2))
    return problems, warnings, found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    paths = commands.add_parser("paths")
    paths.add_argument("--base", required=True)
    paths.add_argument("--head", default="HEAD")
    paths.add_argument("--fork", action="store_true")
    prepared = commands.add_parser("prepared")
    prepared.add_argument("--runtime", type=Path, required=True)
    symbols = commands.add_parser("symbols")
    symbols.add_argument("--build", type=Path, required=True)
    symbols.add_argument("--nm", default="llvm-nm")
    symbols.add_argument("--cxxfilt", default="llvm-cxxfilt")
    symbols.add_argument("--objdump", default="llvm-objdump")
    symbols.add_argument("--suggest", action="store_true", help="print the capabilities each game uses")
    args = parser.parse_args()
    if args.command == "paths":
        games, problems = check_paths(args.base, args.head, args.fork)
        print("Checked games: " + (", ".join(games) if games else "none changed"))
        warnings = []
    elif args.command == "prepared":
        problems, warnings = check_prepared(args.runtime), []
    else:
        problems, warnings, found = check_symbols(args.build, args.nm, args.cxxfilt, args.objdump, args.suggest)
        print(f"Checked {len(found)} games' shipped code")
    for warning in warnings:
        print("warning: " + warning)
    for problem in problems:
        print("error: " + problem)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()

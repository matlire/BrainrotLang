#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  ./compile.sh [options] <input.rot>

Options:
  --backend <tasm|nasm>      Backend target. Default: nasm
  --syntax  <sane|tiktok>    Syntax mode. Default: sane
  --build   <debug|release>  Build mode. Default: debug

  --stage-dir <dir>          Put .east/.opt.east/.asm files here. Default: source dir
  --elf-dir <dir>            Put NASM .o and executable here. Default: source dir

  --graphics                 Enable NASM simulated screen runtime
  --no-graphics              Disable NASM simulated screen runtime. Default
  --graphics                 Enable NASM simulated screen runtime
  --screen <cols>x<rows>     Set simulated screen size. Default: current stty size

  --make                     Force rebuild compiler tools
  --no-make                  Do not rebuild compiler tools
  --no-middleend             Skip middleend optimization
  --no-asm                   Stop after backend assembly generation

  --run                      Run final executable after build, NASM only
  --out <file>               Final executable/output path
  --help                     Show this help

Examples:
  ./compile.sh examples/1.rot
  ./compile.sh --backend nasm --run examples/1.rot
  ./compile.sh --backend nasm --graphics --make --run examples/1.rot
  ./compile.sh --backend tasm examples/1.rot
  ./compile.sh --backend nasm --no-make --out examples/1 examples/1.rot
EOF
}

backend="nasm"
syntax="sane"
build="debug"
graphics=0

do_make=0
do_middleend=1
do_asm=1
do_run=0
graphics=0
screen_width=""
screen_height=""
stage_dir=""
elf_dir=""
out_file=""
input=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backend)
            backend="${2:?--backend needs value}"
            shift 2
            ;;
        --syntax)
            syntax="${2:?--syntax needs value}"
            shift 2
            ;;
        --build)
            build="${2:?--build needs value}"
            shift 2
            ;;
        --graphics)
            graphics=1
            shift
            ;;
        --screen)
            screen="${2:?--screen needs COLSxROWS}"
            if [[ ! "$screen" =~ ^[0-9]+x[0-9]+$ ]]; then
                echo "Bad --screen value: $screen. Expected COLSxROWS, e.g. 120x40" >&2
                exit 2
            fi
            screen_width="${screen%x*}"
            screen_height="${screen#*x}"
            shift 2
            ;;
        --no-graphics)
            graphics=0
            shift
            ;;
        --make)
            do_make=1
            shift
            ;;
        --no-make)
            do_make=0
            shift
            ;;
        --no-middleend)
            do_middleend=0
            shift
            ;;
        --no-asm)
            do_asm=0
            shift
            ;;
        --run)
            do_run=1
            shift
            ;;
        --out)
            out_file="${2:?--out needs value}"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --stage-dir|--dist-dir)
            stage_dir="${2:?--stage-dir needs value}"
            shift 2
            ;;
        --elf-dir|--bin-dir)
            elf_dir="${2:?--elf-dir needs value}"
            shift 2
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            if [[ -n "$input" ]]; then
                echo "Only one input file is supported" >&2
                exit 2
            fi
            input="$1"
            shift
            ;;
    esac
done

if [[ -z "$input" ]]; then
    usage >&2
    exit 2
fi

case "$backend" in
    nasm|tasm) ;;
    *)
        echo "Bad backend: $backend. Expected nasm or tasm." >&2
        exit 2
        ;;
esac

case "$syntax" in
    sane|tiktok|am-tiktok|am_tiktok) ;;
    *)
        echo "Bad syntax: $syntax. Expected sane or tiktok." >&2
        exit 2
        ;;
esac

case "$build" in
    debug|release) ;;
    *)
        echo "Bad build: $build. Expected debug or release." >&2
        exit 2
        ;;
esac

case "$graphics" in
    0|1) ;;
    *)
        echo "Bad graphics value: $graphics. Expected 0 or 1." >&2
        exit 2
        ;;
esac

if [[ "$graphics" -eq 1 && "$backend" != "nasm" ]]; then
    echo "--graphics is supported only for NASM backend." >&2
    exit 2
fi

if [[ "$graphics" -eq 1 ]]; then
    if [[ -z "$screen_width" || -z "$screen_height" ]]; then
        if [[ -t 1 ]] && command -v stty >/dev/null 2>&1; then
            read -r rows cols < <(stty size 2>/dev/null || true)

            if [[ "${rows:-}" =~ ^[0-9]+$ && "${cols:-}" =~ ^[0-9]+$ &&
                  "$rows" -gt 0 && "$cols" -gt 0 ]]; then
                screen_width="$cols"
                screen_height=$(( rows - 1 ))
            fi
        fi
    fi

    : "${screen_width:=128}"
    : "${screen_height:=32}"
else
    screen_width=128
    screen_height=32
fi

if [[ ! -f "$input" ]]; then
    echo "Input file does not exist: $input" >&2
    exit 1
fi

syntax_dir="$syntax"
case "$syntax_dir" in
    tiktok|am-tiktok)
        syntax_dir="am_tiktok"
        ;;
esac

dist_dir="dist/${backend}-${syntax_dir}-${build}-gfx${graphics}"
frontend="${dist_dir}/frontend"
middleend="${dist_dir}/middleend"
backend_bin="${dist_dir}/backend"

src_dir="$(dirname "$input")"
src_base="$(basename "$input")"
src_stem="${src_base%.*}"

if [[ -z "$stage_dir" ]]; then
    stage_dir="${PWD}/examples/dist"
fi

if [[ -z "$elf_dir" ]]; then
    elf_dir="${PWD}/examples/build"
fi

mkdir -p "$stage_dir" "$elf_dir"

east="${stage_dir}/${src_stem}.east"
opt_east="${stage_dir}/${src_stem}.opt.east"
asm="${stage_dir}/${src_stem}.opt.asm"

if [[ -z "$out_file" ]]; then
    out_file="${elf_dir}/${src_stem}"
fi

if [[ "$do_make" -eq 1 ]]; then
    echo "[1/6] Building compiler tools: BACKEND=$backend SYNTAX=$syntax_dir BUILD=$build GRAPHICS=$graphics SCREEN=${screen_width}x${screen_height}"

    make_args=(
        "ASM_TARGET=$backend"
        "SYNTAX=$syntax_dir"
        "BUILD=$build"
        "NASM_GRAPHICS=$graphics"
        "NASM_SCREEN_WIDTH=$screen_width"
        "NASM_SCREEN_HEIGHT=$screen_height"
    )

    make clean "${make_args[@]}"
    make "${make_args[@]}"
fi

echo "[2/6] Frontend: $input -> $east"
"$frontend" --infile "$input" --outfile "$east"

if [[ "$do_middleend" -eq 1 ]]; then
    echo "[3/6] Middleend: $east -> $opt_east"
    "$middleend" --infile "$east" --outfile "$opt_east"
    backend_input="$opt_east"
else
    echo "[3/6] Middleend skipped"
    backend_input="$east"
fi

echo "[4/6] Backend: $backend_input -> $asm"
"$backend_bin" --infile "$backend_input" --outfile "$asm"
if [[ "$do_asm" -eq 0 ]]; then
    echo "[done] Assembly generated: $asm"
    exit 0
fi

if [[ "$backend" == "tasm" ]]; then
    echo "[done] TASM output generated: $asm"
    echo "Run it through your toy assembler/VM pipeline."
    exit 0
fi

obj="${elf_dir}/${src_stem}.o"

echo "[5/6] NASM: $asm -> $obj"
nasm -f elf64 "$asm" -o "$obj"

echo "[6/6] Link: $obj -> $out_file"
ld "$obj" -o "$out_file"
echo "[done] Executable: $out_file"

if [[ "$do_run" -eq 1 ]]; then
    echo "[run] $out_file"
    "$out_file"
fi

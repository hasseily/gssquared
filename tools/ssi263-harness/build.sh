#!/bin/zsh
# Build the renderers and tools into WORK/bin (README.md, "Commands").
#   ./build.sh [all]         card_rtl and card_gss
#   ./build.sh gss | rtl     one of them
#   ./build.sh dbg           card_gss_dbg (per-sample state dump of the tree under test)
#   ./build.sh exp TAG FLAG...      card_gss_TAG: d15bdbf9's SSI263.cpp with EXP_* flags
#                                   (gss/make_exp.py), the attribution builds
#   ./build.sh dbgexp TAG FLAG...   card_gss_dbg_TAG: the same with the state dump
#   ./build.sh tone          tone_check and tone_rtl (tone/)
#   card_rtl  appletini-one's Phasor RTL (mockingboard.sv) under Verilator
#   card_gss  GSSquared's SSI263/W6522/PhasorLogic/PhasorAudio from GSQ
# Every binary gets two records next to it, which run_suite.py --strict uses
# to refuse (or redo) a stale build:
#   NAME.deps   every file it is built from, one absolute path a line
#               (the compiler's include closure, the Verilog sources,
#               this script)
#   NAME.build  the build.sh arguments that rebuild it, then # comments
# Environment (README.md, "Environment"):
#   APPLETINI_ONE  the appletini-one checkout (read only); needed by rtl, tone
#   GSQ            the GSSquared tree; default: the repository this script is in
#   SSI_WORK       outputs; default: work/ next to this script
#   EXP_BASE       the commit the exp builds patch (default d15bdbf9)
#   SDL3_CFLAGS    where SDL3's headers are (GSSquared's headers include
#                  <SDL3/SDL.h>; nothing of SDL is linked); default:
#                  pkg-config --cflags sdl3, else vendored/SDL/include
set -e
SELF=${0:A}
H=${SELF:h}
GSQ=${GSQ:-${H:h:h}}
GSQ=${GSQ:A}
WORK=${SSI_WORK:-$H/work}
mkdir -p $WORK
WORK=${WORK:A}
B=$WORK/bin
O=$WORK/obj
mkdir -p $B $O
what=${1:-all}
if [[ -z $SDL3_CFLAGS ]]; then
  if SDL3_CFLAGS=$(pkg-config --cflags sdl3 2> /dev/null); then :
  elif [[ -f $GSQ/vendored/SDL/include/SDL3/SDL.h ]]; then SDL3_CFLAGS=-I$GSQ/vendored/SDL/include
  else
    echo "SDL3 headers not found: install SDL3 (brew install sdl3), run git submodule update --init vendored/SDL, or set SDL3_CFLAGS=-I<dir with SDL3/SDL.h>" >&2
    exit 2
  fi
fi
CXX=(clang++ -std=c++20 -O2 -w -I$GSQ/src ${=SDL3_CFLAGS})

need_fw() {
  if [[ -z $APPLETINI_ONE ]]; then
    echo "APPLETINI_ONE is not set: export APPLETINI_ONE=<the appletini-one checkout> (README.md, \"Environment\")" >&2
    exit 2
  fi
  FW=${APPLETINI_ONE:A}
  if [[ ! -f $FW/hdl/apple/mockingboard.sv ]]; then
    echo "APPLETINI_ONE=$APPLETINI_ONE has no hdl/apple/mockingboard.sv" >&2
    exit 2
  fi
  A=$FW/hdl/apple
}
[[ -f $GSQ/src/devices/mockingboard/SSI263.cpp ]] || { echo "GSQ=$GSQ has no src/devices/mockingboard/SSI263.cpp" >&2; exit 2; }

gsq_info() { echo "# ${GSQ:t} $(git -C $GSQ rev-parse --abbrev-ref HEAD) $(git -C $GSQ rev-parse --short HEAD), $(git -C $GSQ status --porcelain src | wc -l | tr -d ' ') local changes in src"; }
fw_info() { [[ -n $FW ]] && echo "# appletini-one $(git -C $FW rev-parse --short HEAD) ($FW)"; true; }

# record NAME "BUILD ARGS" FILE...: writes NAME.build and NAME.deps.
record() {
  local name=$1 args=$2; shift 2
  { echo "$args"; gsq_info; fw_info; echo "# built $(date '+%Y-%m-%d %H:%M:%S')"; } > $B/$name.build
  # build.sh itself is a source of every binary (flags, source lists).
  print -l -- "$@" $SELF | /usr/bin/grep -v '^$' | sort -u > $B/$name.deps
}
# cxx_deps FLAG... -- SOURCE...: the include closure (non-system headers).
cxx_deps() {
  local flags=() srcs=()
  while [[ $1 != -- ]]; do flags+=($1); shift; done; shift
  srcs=("$@")
  "${CXX[@]}" $flags -MM $srcs | tr -d '\\' | tr ' ' '\n' | /usr/bin/grep -v ':$' | /usr/bin/grep -v '^$' \
    | while read -r f; do print -r -- ${f:A}; done
}
# exp_tree: EXP_BASE's src/ (the code before the fix), extracted from GSQ's
# history into WORK/gsq-EXP_BASE; prints the tree.
exp_tree() {
  local base=${EXP_BASE:-d15bdbf9} dst
  dst=$WORK/gsq-$base
  if [[ ! -f $dst/src/devices/mockingboard/SSI263.cpp ]]; then
    rm -rf $dst.tmp && mkdir -p $dst.tmp
    git -C $GSQ archive $base src | tar -x -C $dst.tmp
    rm -rf $dst && mv $dst.tmp $dst
  fi
  print -r -- $dst
}

if [[ $what == all || $what == gss ]]; then
  srcs=($H/gss/card_gss.cpp $GSQ/src/devices/mockingboard/SSI263.cpp)
  $CXX $srcs -o $B/.card_gss.new || exit 1
  mv -f $B/.card_gss.new $B/card_gss
  record card_gss "gss" $(cxx_deps -- $srcs)
  echo "built $B/card_gss from $GSQ $(git -C $GSQ rev-parse --short HEAD) ($(git -C $GSQ status --porcelain src | wc -l | tr -d ' ') local changes in src)"
fi

if [[ $what == all || $what == rtl ]]; then
  need_fw
  command -v verilator > /dev/null || { echo "verilator not found (README.md, \"Requirements\")" >&2; exit 2; }
  # The PSG under a renamed copy (the checkout is not touched); the bench's
  # rtl/ym2149_mute.sv takes the YM2149 name and mutes its audio only.
  sed 's/^module YM2149$/module YM2149_real/' $A/YM2149.sv > $O/YM2149_real.sv
  /usr/bin/grep -q '^module YM2149_real$' $O/YM2149_real.sv || { echo "YM2149 rename failed"; exit 1; }
  sv=($FW/hdl/globals.sv $A/ssi263_formant_pkg.sv $A/sc01a_digital_core.sv
      $A/ssi263_formant_backend.sv $A/ssi263_bus_wrapper.sv $A/ssi263_voice.sv
      $A/ssi263_xck_ce.sv $A/via6522.v $O/YM2149_real.sv $H/rtl/ym2149_mute.sv $A/mockingboard.sv
      $H/rtl/tb_card.sv)
  ( cd $FW && verilator --cc --exe --build -O3 -j 4 \
      -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-TIMESCALEMOD -Wno-MULTIDRIVEN \
      --top-module tb_card -Ihdl -Ihdl/apple --Mdir $O/rtl \
      -CFLAGS "-O2 -std=c++17" \
      $sv $H/rtl/card_rtl.cpp > $O/rtl_build.log 2>&1 ) || { tail -40 $O/rtl_build.log; exit 1; }
  cp $O/rtl/Vtb_card $B/.card_rtl.new || exit 1
  mv -f $B/.card_rtl.new $B/card_rtl
  # obj/YM2149_real.sv is generated: the record names its source instead.
  # (Nothing in these sources $readmem's a file; the SC-02 ROM is compiled
  # into ssi263_formant_pkg.sv.)
  record card_rtl "rtl" ${sv:#$O/YM2149_real.sv} $A/YM2149.sv \
    $H/rtl/card_rtl.cpp $H/rtl/rtl_card.hpp $H/common/driver.hpp
  echo "built $B/card_rtl from appletini-one $(git -C $FW rev-parse --short HEAD)"
fi

if [[ $what == dbg ]]; then
  $CXX $H/gss/card_gss_dbg.cpp -o $B/.card_gss_dbg.new || exit 1
  mv -f $B/.card_gss_dbg.new $B/card_gss_dbg
  record card_gss_dbg "dbg" $(cxx_deps -- $H/gss/card_gss_dbg.cpp)
  echo "built $B/card_gss_dbg"
fi

# Attribution builds: ./build.sh exp TAG FLAG... (FLAGS from gss/make_exp.py)
# and ./build.sh dbgexp TAG FLAG... (the same with the state dump).
if [[ $what == exp || $what == dbgexp ]]; then
  tag=$2; shift 2
  [[ -n $tag ]] || { echo "usage: ./build.sh $what TAG FLAG..." >&2; exit 2; }
  # make_exp.py patches the code before the fix (EXP_BASE, d15bdbf9), taken
  # from this repository's history; GSQ itself is not touched.
  GSQ=$(exp_tree)
  CXX=(clang++ -std=c++20 -O2 -w -I$GSQ/src ${=SDL3_CFLAGS} -I$O)
  python3 $H/gss/make_exp.py $GSQ $O/exp/SSI263_exp.cpp > /dev/null || exit 1
  defs=(); for f in "$@"; do defs+=(-D$f); done
  if [[ $what == exp ]]; then
    name=card_gss_$tag
    srcs=($H/gss/card_gss.cpp $O/exp/SSI263_exp.cpp)
  else
    name=card_gss_dbg_$tag
    srcs=($H/gss/card_gss_dbg_exp.cpp)
  fi
  $CXX $defs $srcs -o $B/.$name.new || exit 1
  mv -f $B/.$name.new $B/$name
  # obj/exp/SSI263_exp.cpp is generated from EXP_BASE's SSI263.cpp by
  # make_exp.py: both are sources of this build.
  record $name "$what $tag $*" $(cxx_deps $defs -- $srcs) $H/gss/make_exp.py $GSQ/src/devices/mockingboard/SSI263.cpp
  echo "built $B/$name ($*) from ${EXP_BASE:-d15bdbf9}"
fi

# Tone-stage check (README.md, "The tone stage"): the per-clock C++ model
# against GSSquared's WarmthChannel, and the RTL block itself under Verilator.
if [[ $what == tone ]]; then
  need_fw
  $CXX $H/tone/tone_check.cpp -o $B/.tone_check.new || exit 1
  mv -f $B/.tone_check.new $B/tone_check
  record tone_check "tone" $(cxx_deps -- $H/tone/tone_check.cpp)
  echo "built $B/tone_check"
  python3 $H/tone/extract_tone.py $A/mockingboard.sv $O/tone_stage.sv || exit 1
  ( verilator --cc --exe --build -O3 -j 4 -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH \
      --top-module tone_stage --Mdir $O/tone \
      -CFLAGS "-O2 -std=c++17" $O/tone_stage.sv $H/tone/tone_rtl.cpp > $O/tone_build.log 2>&1 ) \
    || { tail -40 $O/tone_build.log; exit 1; }
  cp $O/tone/Vtone_stage $B/.tone_rtl.new || exit 1
  mv -f $B/.tone_rtl.new $B/tone_rtl
  record tone_rtl "tone" $A/mockingboard.sv $H/tone/extract_tone.py $H/tone/tone_rtl.cpp \
    $H/tone/schedule.hpp $H/tone/tone_model.hpp
  echo "built $B/tone_rtl from appletini-one $(git -C $FW rev-parse --short HEAD) mockingboard.sv"
fi

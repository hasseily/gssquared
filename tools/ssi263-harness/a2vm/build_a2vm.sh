#!/bin/zsh
# Build the capture machine: a scratch copy of appletini-software's a2vm
# (demos/doom_gs/tools/a2vm at the commit in a2vm/BASE) with a2vm.patch
# applied and the harness's ssi_ext.c/ssi_ext.h added, linked with the RTL
# Phasor (the harness's tb_card, so ./build.sh rtl must have run).
#   WORK/a2vm/src             the patched copy (recreated on every build)
#   WORK/a2vm/a2vm_ssi        the RTL card as the Phasor (what capture.py runs)
#   WORK/a2vm/a2vm_ssi_gss    the same machine with GSSquared's card from GSQ
#                             (exploration only; ./build_a2vm.sh gss)
# Environment: APPLETINI_SOFTWARE (read only; the commit is taken with git
# archive, so its working tree and later commits do not matter), SSI_WORK,
# GSQ (README.md, "Environment").
set -e
D=${0:A:h}
H=${D:h}
GSQ=${GSQ:-${H:h:h}}
WORK=${SSI_WORK:-$H/work}
mkdir -p $WORK
WORK=${WORK:A}
O=$WORK/obj
B=$WORK/a2vm
BASE=$(<$D/BASE)
if [[ -z $APPLETINI_SOFTWARE ]]; then
  echo "APPLETINI_SOFTWARE is not set: export APPLETINI_SOFTWARE=<the appletini-software checkout> (README.md, \"Environment\")" >&2
  exit 2
fi
git -C $APPLETINI_SOFTWARE cat-file -e $BASE^{commit} 2>/dev/null \
  || { echo "APPLETINI_SOFTWARE=$APPLETINI_SOFTWARE does not have commit $BASE (a2vm/BASE)" >&2; exit 2; }
[[ -f $O/rtl/Vtb_card__ALL.a ]] || { echo "no $O/rtl/Vtb_card__ALL.a: run ./build.sh rtl first" >&2; exit 2; }
VI=$(verilator --getenv VERILATOR_ROOT)/include
[[ -f $VI/verilated.h ]] || { echo "verilator include directory not found ($VI)" >&2; exit 2; }

rm -rf $B/src $B/obj
mkdir -p $B/src $B/obj
git -C $APPLETINI_SOFTWARE archive $BASE demos/doom_gs/tools/a2vm | tar -x -C $B/src --strip-components 4
( cd $B/src && patch -p1 -s --forward < $D/a2vm.patch )
cp $D/ssi_ext.c $D/ssi_ext.h $B/src/
for f in main a2vm cost prodos cpu65c02 ssi_ext; do
  cc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -c -o $B/obj/$f.o $B/src/$f.c
done
c++ -std=c++17 -O2 -w -I$O/rtl -I$VI -I$VI/vltstd -c -o $B/obj/rtlcard_shim.o $H/rtl/rtlcard_shim.cpp
# -U: Verilator's runtime refers to these SystemC/VPI hooks, unused here
# (macOS ld; on Linux drop the -Wl,-U,... list).
c++ -o $B/.a2vm_ssi.new $B/obj/{main,a2vm,cost,prodos,cpu65c02,ssi_ext,rtlcard_shim}.o \
    $O/rtl/Vtb_card__ALL.a $O/rtl/verilated.o $O/rtl/verilated_threads.o -lpthread \
    -Wl,-U,__Z15vl_time_stamp64v,-U,__Z13sc_time_stampv,-U,_vlog_startup_routines
mv -f $B/.a2vm_ssi.new $B/a2vm_ssi
{ echo "# a2vm $BASE + a2vm.patch + ssi_ext.c; RTL from $O/rtl"; echo "# built $(date '+%Y-%m-%d %H:%M:%S')"; } > $B/a2vm_ssi.build
echo "built $B/a2vm_ssi (a2vm ${BASE[1,8]} + a2vm.patch)"

if [[ $1 == gss ]]; then
  # The same machine with GSSquared's card (gss/card_gss.cpp) as the Phasor.
  c++ -std=c++20 -O2 -w -I$GSQ/src ${=SDL3_CFLAGS:-$(pkg-config --cflags sdl3)} -c -o $B/obj/gsscard_shim.o $H/gss/gsscard_shim.cpp
  c++ -std=c++20 -O2 -w -I$GSQ/src -c -o $B/obj/SSI263.o $GSQ/src/devices/mockingboard/SSI263.cpp
  c++ -o $B/.a2vm_ssi_gss.new $B/obj/{main,a2vm,cost,prodos,cpu65c02,ssi_ext,gsscard_shim,SSI263}.o
  mv -f $B/.a2vm_ssi_gss.new $B/a2vm_ssi_gss
  echo "built $B/a2vm_ssi_gss (GSSquared's card from $GSQ)"
fi

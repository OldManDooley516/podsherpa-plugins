#!/bin/bash
# cmp.sh <jsfx dir> <jsfx file> <module> <in.f32> "<rig --set args>" "<ps_ref params>" [latency]
D=$1; J=$2; M=$3; IN=$4; SETS=$5; PAR=$6; LAT=${7:-0}
SECS=$(python3 -c "import os;print(os.path.getsize('$IN')/8/48000)")
(cd $D && /home/claude/alt/tools/rig $J --in $IN --out /tmp/cmp_j.f32 $SETS --reinit --audio $SECS >/dev/null 2>&1)
/home/claude/podsherpa-plugins/build/ps_ref_artefacts/Release/ps_ref $M $IN /tmp/cmp_c.f32 512 $PAR >/dev/null || echo "ps_ref failed"
python3 - "$IN" "$LAT" "$M $PAR" <<'P'
import numpy as np,sys
x=np.fromfile(sys.argv[1],dtype=np.float32).reshape(-1,2);j=np.fromfile('/tmp/cmp_j.f32',dtype=np.float32).reshape(-1,2);c=np.fromfile('/tmp/cmp_c.f32',dtype=np.float32).reshape(-1,2)
n=min(len(j),len(c));j=j[:n].astype(float);c=c[:n].astype(float);d=j-c
ref=np.sqrt((j**2).mean())+1e-20
ch=np.sqrt(((j-x[:n])**2).mean())
print('%-60s max diff %.2e (%.0f dB below the JSFX output) | plugin changes the input by %.1f dB rel. output'%(sys.argv[3][:60],abs(d).max(),-20*np.log10(np.sqrt((d**2).mean())/ref+1e-20),20*np.log10(ch/ref+1e-20)))
P

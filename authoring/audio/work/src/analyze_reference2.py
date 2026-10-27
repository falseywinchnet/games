"""Refined tempo, swing and bass-register pitch histogram of the reference (measurement only)."""
import subprocess,sys,numpy as np,json
SR=22050;H=256;N=2048
raw=subprocess.run(["ffmpeg","-v","error","-i",sys.argv[1],"-f","f32le","-ac","1","-ar",str(SR),"-"],capture_output=True).stdout
x=np.frombuffer(raw,np.float32)
fr=np.lib.stride_tricks.sliding_window_view(x,N)[::H]*np.hanning(N);S=np.abs(np.fft.rfft(fr,axis=1));f=np.fft.rfftfreq(N,1/SR)
fl=np.maximum(np.diff(np.log1p(S),axis=0),0).sum(1);fl-=fl.mean();fps=SR/H
best=max(((np.sum(fl*np.cos(2*np.pi*(b/60)*np.arange(len(fl))/fps))**2+np.sum(fl*np.sin(2*np.pi*(b/60)*np.arange(len(fl))/fps))**2,b) for b in np.arange(100,140,0.05)))
bpm=best[1]
# phase-fold onsets over 1 beat into 12 bins
t=np.arange(len(fl))/fps; ph=((t*bpm/60)%1)
# find beat phase offset maximizing
hist=np.array([fl[(ph>=i/24)&(ph<(i+1)/24)].mean() for i in range(24)])
off=np.argmax(hist); hist=np.roll(hist,-off)
# bass pitch histogram 40-200 Hz
v=(f>40)&(f<200);m=np.round(69+12*np.log2(f[v]/440)).astype(int)%12
names="C C# D D# E F F# G G# A A# B".split()
bh=np.array([(S[:,v][:,m==k]**2).sum() for k in range(12)]);bh/=bh.max()
# low-band (<150Hz) onset share vs high-band (>2k) onset
lo=np.maximum(np.diff(np.log1p(S[:,f<150].sum(1))),0);hi=np.maximum(np.diff(np.log1p(S[:,f>2000].sum(1))),0)
hl=np.array([hi[(ph[:len(hi)]>=i/24)&(ph[:len(hi)]<(i+1)/24)].mean() for i in range(24)]);hl=np.roll(hl,-off)
out={"tempo_bpm_refined":round(bpm,2),"onset_strength_by_beat_phase_24bins":[round(a,2) for a in hist/hist.max()],
"hi_band_onset_by_beat_phase":[round(a,2) for a in hl/hl.max()],"bass_pitch_class":{n:round(b,2) for n,b in zip(names,bh)}}
print(json.dumps(out));json.dump(out,open(sys.argv[2],"w"),indent=1)

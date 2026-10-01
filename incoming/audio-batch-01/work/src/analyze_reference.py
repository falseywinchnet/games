"""Measure objective features of the reference track (analysis only; no audio is reused)."""
import subprocess, sys, json, numpy as np
SR=22050
def load(p,ch=2):
    raw=subprocess.run(["ffmpeg","-v","error","-i",p,"-f","f32le","-ac",str(ch),"-ar",str(SR),"-"],capture_output=True).stdout
    return np.frombuffer(raw,np.float32).reshape(-1,ch)
p=sys.argv[1]; st=load(p); x=st.mean(1)
r={}
r["duration_s"]=len(x)/SR
r["peak_dbfs"]=20*np.log10(np.abs(st).max())
r["rms_dbfs"]=20*np.log10(np.sqrt((x**2).mean()))
m,s=(st[:,0]+st[:,1])/2,(st[:,0]-st[:,1])/2
r["side_to_mid_db"]=10*np.log10((s**2).mean()/(m**2).mean())
N=2048;H=512;win=np.hanning(N)
fr=np.lib.stride_tricks.sliding_window_view(x,N)[::H]*win
S=np.abs(np.fft.rfft(fr,axis=1)); f=np.fft.rfftfreq(N,1/SR)
r["spectral_centroid_hz_median"]=float(np.median((S*f).sum(1)/(S.sum(1)+1e-9)))
# band energy
P=(S**2).sum(0); bands=[(20,120),(120,400),(400,1500),(1500,5000),(5000,11000)]
tot=P.sum(); r["band_energy_pct"]={f"{a}-{b}":round(100*P[(f>=a)&(f<b)].sum()/tot,1) for a,b in bands}
# onset envelope / tempo
flux=np.maximum(np.diff(np.log1p(S),axis=0),0).sum(1); flux-=flux.mean()
fps=SR/H; ac=np.correlate(flux,flux,"full")[len(flux)-1:]
lags=np.arange(len(ac)); bpm=60*fps/np.maximum(lags,1)
sel=(bpm>60)&(bpm<200); cand=sorted(zip(ac[sel],bpm[sel]),reverse=True)[:8]
r["tempo_candidates_bpm"]=[round(b,1) for a,b in cand]
# chroma/key
pc=np.zeros(12); valid=(f>60)&(f<2000)
midi=np.round(69+12*np.log2(f[valid]/440)).astype(int)%12
for k in range(12): pc[k]=(S[:,valid][:,midi==k]**2).sum()
pc/=pc.max(); names="C C# D D# E F F# G G# A A# B".split()
r["chroma"]={n:round(v,2) for n,v in zip(names,pc)}
maj=np.array([6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88]);mn=np.array([6.33,2.68,3.52,5.38,2.6,3.53,2.54,4.75,3.98,2.69,3.34,3.17])
sc=[(np.corrcoef(np.roll(maj,k),pc)[0,1],names[k]+" major") for k in range(12)]+[(np.corrcoef(np.roll(mn,k),pc)[0,1],names[k]+" minor") for k in range(12)]
r["key_estimates"]=[(n,round(c,3)) for c,n in sorted(sc,reverse=True)[:3]]
# loudness over time (4s windows) for arrangement sections
w=4*SR; r["rms_db_per_4s"]=[round(20*np.log10(np.sqrt((x[i:i+w]**2).mean())+1e-9),1) for i in range(0,len(x)-w,w)]
cent=(S*f).sum(1)/(S.sum(1)+1e-9); step=int(4*fps)
r["centroid_hz_per_4s"]=[int(cent[i:i+step].mean()) for i in range(0,len(cent)-step,step)]
# onset density
thr=np.percentile(flux,90); on=np.where((flux[1:-1]>thr)&(flux[1:-1]>flux[:-2])&(flux[1:-1]>flux[2:]))[0]
r["strong_onsets_per_s"]=round(len(on)/r["duration_s"],2)
# self-similarity of chroma in 4s blocks to find repeats
json.dump(r,open(sys.argv[2],"w"),indent=1,default=float); print(json.dumps(r,default=float)[:4000])

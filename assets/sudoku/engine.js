const performance = {now: () => Date.now()};
// No dependencies, I/O, or network. Shared by Node and the offline browser worker.
const VERSION = '1.0.0';
const ALL = 511;
const POP = Uint8Array.from({length:512}, (_,i) => i.toString(2).replaceAll('0','').length);
const DIGIT = Uint8Array.from({length:512}, (_,i) => i ? 32-Math.clz32(i) : 0);
const ROW = Uint8Array.from({length:81}, (_,i) => Math.floor(i/9));
const COL = Uint8Array.from({length:81}, (_,i) => i%9);
const BOX = Uint8Array.from({length:81}, (_,i) => Math.floor(i/27)*3+Math.floor(i%9/3));
const units = Array.from({length:27}, () => []);
for(let i=0;i<81;i++) { units[ROW[i]].push(i); units[9+COL[i]].push(i); units[18+BOX[i]].push(i); }
const peers = Array.from({length:81}, (_,i) => [...new Set([...units[ROW[i]],...units[9+COL[i]],...units[18+BOX[i]]])].filter(j=>j!==i));
const GRAPH = Object.freeze({
  units:Object.freeze(units.map(u=>Object.freeze(u))),
  peers:Object.freeze(peers.map(p=>Object.freeze(p))), vertices:81, edges:810
});

function parseBoard(input) {
  if(typeof input === 'string') {
    input = input.replace(/\s/g,'');
    if(!/^[0-9.]{81}$/.test(input)) throw new TypeError('Board must contain exactly 81 digits or dots.');
    input = [...input].map(x=>x==='.'?0:Number(x));
  }
  if(!(Array.isArray(input) || ArrayBuffer.isView(input)) || input.length!==81)
    throw new TypeError('Board must contain exactly 81 cells.');
  for(const v of input) if(!Number.isInteger(v) || v<0 || v>9) throw new TypeError('Cells must be integers from 0 to 9.');
  return Uint8Array.from(input);
}
function formatBoard(input) { return [...parseBoard(input)].join(''); }
function masks(board) {
  const r=new Uint16Array(9),c=new Uint16Array(9),b=new Uint16Array(9);
  for(let i=0;i<81;i++) if(board[i]) {
    const bit=1<<(board[i]-1), rr=ROW[i],cc=COL[i],bb=BOX[i];
    if((r[rr]|c[cc]|b[bb])&bit) return null;
    r[rr]|=bit;c[cc]|=bit;b[bb]|=bit;
  }
  return {r,c,b};
}
function isValid(input, {complete=false}={}) {
  const board=parseBoard(input);
  return masks(board)!==null && (!complete || board.every(Boolean));
}
function createRng(seed) {
  if(typeof seed!=='string' && !(typeof seed==='number' && Number.isFinite(seed)))
    throw new TypeError('Seed must be a string or finite number.');
  let a=2166136261;
  for(const ch of String(seed)) a=Math.imul(a^ch.charCodeAt(0),16777619)>>>0;
  return () => {
    a=(a+0x6D2B79F5)>>>0;
    let t=Math.imul(a^(a>>>15),1|a);t^=t+Math.imul(t^(t>>>7),61|t);
    return ((t^(t>>>14))>>>0)/4294967296;
  };
}
function shuffle(a,rng) { for(let i=a.length-1;i>0;i--) {const j=Math.floor(rng()*(i+1));[a[i],a[j]]=[a[j],a[i]];}return a; }
class SearchLimitError extends Error { constructor(){super('Exact search node budget exhausted; uniqueness is unproved.');this.name='SearchLimitError';} }

// Exact list-coloring search. Minimum remaining colors; stop after a second coloring.
function solveExact(input,{limit=2,maxNodes=2_000_000,rng=null}={}) {
  if(!Number.isInteger(limit)||limit<1||!Number.isInteger(maxNodes)||maxNodes<1) throw new RangeError('Positive integer search limits required.');
  const board=parseBoard(input),state=masks(board);
  if(!state) return {count:0,solution:null,nodes:0};
  const {r,c,b}=state;let count=0,solution=null,nodes=0;
  function visit() {
    if(++nodes>maxNodes) throw new SearchLimitError();
    let cell=-1,choices=0,best=10,ties=0;
    for(let i=0;i<81;i++) if(!board[i]) {
      const bits=ALL&~(r[ROW[i]]|c[COL[i]]|b[BOX[i]]),n=POP[bits];
      if(!n)return;
      if(n<best) {best=n;cell=i;choices=bits;ties=1;if(n===1)break;}
      else if(rng && n===best && rng()<1/(++ties)){cell=i;choices=bits;}
    }
    if(cell<0) {count++;if(!solution)solution=board.slice();return;}
    const rr=ROW[cell],cc=COL[cell],bb=BOX[cell];
    let options=[];
    while(choices) {const bit=choices&-choices;choices^=bit;options.push(bit);}
    if(rng)shuffle(options,rng);
    for(const bit of options) {
      board[cell]=DIGIT[bit];r[rr]|=bit;c[cc]|=bit;b[bb]|=bit;
      visit();
      board[cell]=0;r[rr]^=bit;c[cc]^=bit;b[bb]^=bit;
      if(count>=limit)return;
    }
  }
  visit();return {count,solution,nodes};
}
function countSolutions(input,options={}) {return solveExact(input,{...options,limit:2}).count;}
function generateSolution(seed) {return solveExact(new Uint8Array(81),{limit:1,rng:createRng(seed)}).solution;}

const TECHNIQUES = Object.freeze({
  nakedSingle:{tier:1,weight:1}, hiddenSingle:{tier:1,weight:2},
  lockedCandidate:{tier:2,weight:12}, nakedPair:{tier:3,weight:24},
  hiddenPair:{tier:3,weight:30}, nakedTriple:{tier:3,weight:36}, xWing:{tier:3,weight:48}
});

// Deterministic, explainable deductions; this solver NEVER guesses or backtracks.
function analyze(input,{trace=false,maxTier=3}={}) {
  if(![1,2,3].includes(maxTier))throw new RangeError('maxTier must be 1, 2, or 3.');
  const board=parseBoard(input),initial=masks(board),candidates=new Uint16Array(81);
  if(!initial)return {solved:false,valid:false,band:null,score:null,reason:'Conflicting clues',steps:[]};
  for(let i=0;i<81;i++)if(!board[i])candidates[i]=ALL&~(initial.r[ROW[i]]|initial.c[COL[i]]|initial.b[BOX[i]]);
  const counts=Object.fromEntries(Object.keys(TECHNIQUES).map(k=>[k,0])),steps=[];
  let tier=1,effort=0,eliminations=0,contradiction=false;
  const entropy=Array.from(candidates).reduce((s,m)=>s+(m?Math.log2(POP[m]):0),0);
  function record(technique,detail) {
    counts[technique]++;tier=Math.max(tier,TECHNIQUES[technique].tier);effort+=TECHNIQUES[technique].weight;
    if(trace)steps.push({technique,...detail});
  }
  function place(cell,digit,technique,unit=null) {
    board[cell]=digit;candidates[cell]=0;
    const bit=1<<(digit-1);
    for(const p of peers[cell]) if(!board[p]) {candidates[p]&=~bit;if(!candidates[p])contradiction=true;}
    record(technique,{cell,digit,unit});
  }
  function remove(technique,removals,detail) {
    const changes=[];
    for(const [cell,bits] of removals) {
      const actual=candidates[cell]&bits;
      if(actual) {candidates[cell]&=~actual;eliminations+=POP[actual];changes.push({cell,mask:actual});if(!candidates[cell])contradiction=true;}
    }
    if(!changes.length)return false;
    record(technique,{...detail,removals:changes});return true;
  }
  function single() {
    for(let i=0;i<81;i++) if(!board[i]) {
      if(!candidates[i]){contradiction=true;return false;}
      if(POP[candidates[i]]===1){place(i,DIGIT[candidates[i]],'nakedSingle');return true;}
    }
    for(let u=0;u<27;u++) {
      let used=0;for(const i of units[u])if(board[i])used|=1<<(board[i]-1);
      for(let bit=1;bit<=256;bit<<=1) if(!(used&bit)) {
        let cell=-1,n=0;
        for(const i of units[u])if(candidates[i]&bit){cell=i;n++;}
        if(!n){contradiction=true;return false;}
        if(n===1){place(cell,DIGIT[bit],'hiddenSingle',u);return true;}
      }
    }
    return false;
  }
  function locked() {
    for(let u=0;u<27;u++)for(let bit=1;bit<=256;bit<<=1) {
      const cells=units[u].filter(i=>candidates[i]&bit);
      if(cells.length<2)continue;
      const destinations = u>=18 ? [ROW[cells[0]],9+COL[cells[0]]] : [18+BOX[cells[0]]];
      for(const v of destinations)if(cells.every(i=>units[v].includes(i))) {
        if(remove('lockedCandidate',units[v].filter(i=>!units[u].includes(i)).map(i=>[i,bit]),{unit:u,targetUnit:v,cells,digit:DIGIT[bit]}))return true;
      }
    }
    return false;
  }
  function naked(size) {
    for(let u=0;u<27;u++) {
      const cells=units[u].filter(i=>POP[candidates[i]]>=2&&POP[candidates[i]]<=size);
      function choose(start,selected,bits) {
        if(selected.length===size) {
          if(POP[bits]!==size)return false;
          return remove(size===2?'nakedPair':'nakedTriple',units[u].filter(i=>!selected.includes(i)).map(i=>[i,bits]),{unit:u,cells:[...selected],mask:bits});
        }
        for(let k=start;k<cells.length;k++) {
          const next=bits|candidates[cells[k]];if(POP[next]>size)continue;
          if(choose(k+1,[...selected,cells[k]],next))return true;
        }
        return false;
      }
      if(choose(0,[],0))return true;
    }
    return false;
  }
  function hiddenPair() {
    for(let u=0;u<27;u++) {
      const positions=new Uint16Array(9);
      for(let j=0;j<9;j++)for(let d=0;d<9;d++)if(candidates[units[u][j]]&(1<<d))positions[d]|=1<<j;
      for(let a=0;a<8;a++)if(POP[positions[a]]===2)for(let b=a+1;b<9;b++)if(positions[a]===positions[b]) {
        const cells=units[u].filter((_,j)=>positions[a]&(1<<j)),bits=(1<<a)|(1<<b);
        if(remove('hiddenPair',cells.map(i=>[i,ALL^bits]),{unit:u,cells,mask:bits}))return true;
      }
    }
    return false;
  }
  function xwing() {
    for(const offset of [0,9])for(let bit=1;bit<=256;bit<<=1) {
      const positions=new Uint16Array(9);
      for(let u=0;u<9;u++)for(let j=0;j<9;j++)if(candidates[units[offset+u][j]]&bit)positions[u]|=1<<j;
      for(let a=0;a<8;a++)if(POP[positions[a]]===2)for(let b=a+1;b<9;b++)if(positions[a]===positions[b]) {
        const removals=[],cells=[];
        for(let j=0;j<9;j++)if(positions[a]&(1<<j))for(let u=0;u<9;u++) {
          const i=units[offset+u][j];if(u!==a&&u!==b)removals.push([i,bit]);else cells.push(i);
        }
        if(remove('xWing',removals,{cells,digit:DIGIT[bit],orientation:offset===0?'rows':'columns'}))return true;
      }
    }
    return false;
  }
  while(!contradiction && board.some(v=>v===0)) {
    if(single())continue;
    if(contradiction)break;
    if(maxTier>=2&&locked())continue;
    if(maxTier>=3&&(naked(2)||hiddenPair()||naked(3)||xwing()))continue;
    break;
  }
  const solved=!contradiction&&board.every(Boolean)&&isValid(board,{complete:true});
  const band=solved?['easy','medium','hard'][tier-1]:null;
  // Tier is the primary ordering. Effort and initial candidate entropy order within a tier.
  const score=solved ? tier*1000+effort+entropy*0.2+eliminations*0.5 : null;
  return {solved,valid:!contradiction,band,tier,score,counts,effort,entropy,eliminations,
    clues:parseBoard(input).filter(Boolean).length,remaining:board.filter(v=>!v).length,
    solution:solved?[...board]:null,steps,reason:contradiction?'Contradiction':solved?'Solved by logic':'Beyond supported techniques'};
}

function empiricalQuantile(sorted,p) {
  if(!sorted.length || !Number.isFinite(p)||p<0||p>1)throw new RangeError('Quantile needs samples and p in [0,1].');
  // Generalized inverse of the empirical CDF (discrete optimal transport).
  return sorted[Math.max(0,Math.ceil(p*sorted.length)-1)];
}
function percentile(sorted,value) {
  if(!sorted.length||!Number.isFinite(value))throw new RangeError('Percentile needs samples and a finite value.');
  let lo=0,hi=sorted.length;
  while(lo<hi){const m=(lo+hi)>>>1;if(sorted[m]<value)lo=m+1;else hi=m;}
  const left=lo;hi=sorted.length;
  while(lo<hi){const m=(lo+hi)>>>1;if(sorted[m]<=value)lo=m+1;else hi=m;}
  return (left+lo)/(2*sorted.length);
}
class GenerationError extends Error {constructor(message){super(message);this.name='GenerationError';}}
function randomSeed() {
  const bytes=new Uint32Array(4);
  if(!globalThis.crypto?.getRandomValues)throw new Error('Supply a seed: secure system randomness unavailable.');
  crypto.getRandomValues(bytes);return [...bytes].map(x=>x.toString(16).padStart(8,'0')).join('');
}

// Produces one complete carving trajectory. Used both for generation and calibration.
function carve(seed,{symmetry='none',onCandidate=()=>false,maxNodes=200_000}={}) {
  if(!['none','rotational'].includes(symmetry))throw new RangeError('symmetry must be none or rotational.');
  const rng=createRng(seed),solution=solveExact(new Uint8Array(81),{limit:1,rng,maxNodes}).solution;
  const board=solution.slice(),order=shuffle(Array.from({length:symmetry==='none'?81:41},(_,i)=>i),rng);
  let checks=0,rejected=0,clues=81;
  for(const i of order) {
    const group=symmetry==='rotational'&&i!==40?[i,80-i]:[i];
    if(clues-group.length<23)continue;
    const saved=group.map(j=>board[j]);for(const j of group)board[j]=0;
    let unique=false;checks++;
    try {unique=countSolutions(board,{maxNodes})===1;} catch(e) {if(!(e instanceof SearchLimitError))throw e;}
    if(!unique){for(let k=0;k<group.length;k++)board[group[k]]=saved[k];rejected++;continue;}
    clues-=group.length;
    if(clues<=46) {
      const rating=analyze(board);
      if(rating.solved&&onCandidate({puzzle:[...board],solution:[...solution],rating,checks,rejected}))break;
    }
  }
  return {checks,rejected};
}

function generate({difficulty='medium',seed=randomSeed(),calibration=null,symmetry='none',maxAttempts=256,candidateBudget=6,tolerance=0.12}={}) {
  const bands=['easy','medium','hard'];let band,quantile;
  if(typeof difficulty==='number') {
    if(!Number.isFinite(difficulty)||difficulty<0||difficulty>1)throw new RangeError('Difficulty must be in [0,1].');
    const index=Math.min(2,Math.floor(difficulty*3));band=bands[index];quantile=difficulty*3-index;
  } else {if(!bands.includes(difficulty))throw new RangeError('Difficulty must be easy, medium, hard, or [0,1].');band=difficulty;quantile=0.5;}
  createRng(seed); // Validate before doing work.
  if(!Number.isInteger(maxAttempts)||maxAttempts<1||maxAttempts>10000||!Number.isInteger(candidateBudget)||candidateBudget<1||candidateBudget>10000)
    throw new RangeError('Attempt and candidate budgets must be integers in [1,10000].');
  if(!Number.isFinite(tolerance)||tolerance<0||tolerance>1)throw new RangeError('Tolerance must be in [0,1].');
  let samples=null;
  if(calibration!==null) {
    if(calibration.version!==VERSION)throw new RangeError('Calibration version does not match engine.');
    samples=calibration.scores?.[band];
    if(!Array.isArray(samples)||!samples.length||samples.some((x,i)=>!Number.isFinite(x)||(i&&x<samples[i-1])))throw new TypeError('Calibration requires sorted finite score samples.');
  }
  if(typeof difficulty==='number'&&!samples)throw new Error('Numeric difficulty requires an offline calibration profile.');
  const target=samples?empiricalQuantile(samples,quantile):null;
  let best=null,bestDistance=Infinity,accepted=0,matchedTrajectories=0,attempts=0,checks=0;
  const start=performance.now();
  for(;attempts<maxAttempts;attempts++) {
    let done=false,matched=false;
    const result=carve(`${seed}:trajectory:${attempts}`,{symmetry,onCandidate: candidate=>{
      if(candidate.rating.band!==band)return false;
      accepted++;matched=true;
      const distance=samples?Math.abs(candidate.rating.score-target):0;
      if(distance<bestDistance){best=candidate;bestDistance=distance;}
      const actual=samples?percentile(samples,candidate.rating.score):null;
      // One trajectory can provide a ladder of easier/harder puzzles; no cache needed.
      if(!samples || Math.abs(actual-quantile)<=tolerance){done=true;return true;}
      return false;
    }});
    checks+=result.checks;
    if(matched)matchedTrajectories++;
    if(matchedTrajectories>=candidateBudget)done=true;
    if(done){attempts++;break;}
  }
  if(!best)throw new GenerationError(`No ${band} puzzle found in ${maxAttempts} trajectories. Increase maxAttempts or use another seed.`);
  // Recheck the exact returned board; failed searches are never interpreted as unique.
  if(countSolutions(best.puzzle)!==1)throw new Error('Internal uniqueness invariant failed.');
  const rating=analyze(best.puzzle,{trace:true});
  if(!rating.solved||rating.band!==band)throw new Error('Internal difficulty invariant failed.');
  const actualQuantile=samples?percentile(samples,rating.score):null;
  return {version:VERSION,seed,puzzle:best.puzzle,solution:best.solution,rating,
    transport:{requested:difficulty,band,requestedQuantile:quantile,targetScore:target,actualQuantile,
      actualDifficulty:samples?(bands.indexOf(band)+actualQuantile)/3:null,
      quantileError:samples?Math.abs(quantile-actualQuantile):null,
      withinTolerance:samples?Math.abs(quantile-actualQuantile)<=tolerance:null},
    stats:{attempts,candidates:accepted,matchedTrajectories,uniquenessChecks:checks+1,elapsedMs:performance.now()-start},symmetry};
}

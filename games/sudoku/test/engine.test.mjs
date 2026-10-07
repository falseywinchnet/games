import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {GRAPH,parseBoard,isValid,generateSolution,solveExact,countSolutions,analyze,generate,createRng,empiricalQuantile,percentile,SearchLimitError,GenerationError} from '../src/engine.mjs';
import {referenceSolve,referenceValid,verifyTrace} from './reference.mjs';
const calibration=JSON.parse(await readFile(new URL('../src/calibration.json',import.meta.url)));
const classic='530070000600195000098000060800060003400803001700020006060000280000419005000080079';
test('constraint graph has 81 vertices, 810 edges, 27 cliques and 20 symmetric peers per cell',()=>{
  assert.equal(GRAPH.peers.length,81);assert.equal(GRAPH.units.length,27);
  assert.equal(GRAPH.peers.reduce((s,p)=>s+p.length,0)/2,810);
  GRAPH.peers.forEach((p,i)=>{assert.equal(p.length,20);assert(!p.includes(i));for(const j of p)assert(GRAPH.peers[j].includes(i));});
  for(const u of GRAPH.units){assert.equal(new Set(u).size,9);for(const i of u)for(const j of u)if(i!==j)assert(GRAPH.peers[i].includes(j));}
  assert.throws(()=>GRAPH.peers[0].push(0));
});
test('input validation rejects malformed boards and budgets',()=>{
  for(const x of ['',Array(80).fill(0),Array(81).fill(-1),Array(81).fill(1.5),Array(81).fill('0'),null,'x'.repeat(81)])assert.throws(()=>parseBoard(x));
  assert.deepEqual(parseBoard('.'.repeat(81)),new Uint8Array(81));
  for(const d of [-1,1.1,NaN,Infinity,'expert'])assert.throws(()=>generate({difficulty:d,seed:0,calibration}));
  assert.throws(()=>generate({difficulty:0.4,seed:0}));
  assert.throws(()=>generate({seed:0,maxAttempts:0}));assert.throws(()=>generate({seed:0,candidateBudget:0}));
  assert.throws(()=>generate({seed:0,symmetry:'diagonal'}));assert.throws(()=>createRng(NaN));
  assert.throws(()=>generate({seed:0,calibration:{version:'other'}}));
});
test('exact oracle agrees on unique, multiple, duplicate, complete and unsatisfiable boards',()=>{
  const solved=[...generateSolution('oracle')],duplicate=[...solved];duplicate[0]=duplicate[1];
  const unsat=[...parseBoard(classic)];unsat[2]=1;
  for(const [board,expected] of [[parseBoard(classic),1],[Array(81).fill(0),2],[duplicate,0],[solved,1],[unsat,0]]) {
    const copy=[...board],actual=solveExact(board),reference=referenceSolve(board);
    assert.equal(actual.count,expected);assert.equal(actual.count,reference.count);assert.deepEqual([...board],copy);
    if(actual.count===1)assert.deepEqual([...actual.solution],reference.solution);
  }
  assert.throws(()=>solveExact(Array(81).fill(0),{maxNodes:1}),SearchLimitError);
});
test('randomized full colorings are valid, deterministic and diverse',()=>{
  const grids=new Set();for(let n=0;n<100;n++){const s=generateSolution(`grid-${n}`);assert(referenceValid([...s]));grids.add(s.join(''));}
  assert.equal(grids.size,100);assert.deepEqual(generateSolution('repeat'),generateSolution('repeat'));
});
test('fuzz production exact solver against independent Algorithm X',()=>{
  const rng=createRng('oracle-fuzz');
  for(let n=0;n<150;n++){
    const p=[...generateSolution(`oracle-${n}`)];for(let i=0;i<81;i++)if(rng()<0.3+(n%7)*0.1)p[i]=0;
    if(n%5===0)p[Math.floor(rng()*81)]=1+Math.floor(rng()*9);
    assert.equal(countSolutions(p),referenceSolve(p).count,`case ${n}`);
  }
});
test('discrete quantile transport and ties have defined behavior',()=>{
  const s=[1,2,2,4];assert.equal(empiricalQuantile(s,0),1);assert.equal(empiricalQuantile(s,1),4);
  assert.equal(empiricalQuantile(s,.5),2);assert.equal(percentile(s,2),.5);
  assert.equal(percentile(s,0),0);assert.equal(percentile(s,5),1);
  assert.throws(()=>empiricalQuantile([],0));
  let prev=-Infinity;for(let i=0;i<=100;i++){const q=empiricalQuantile(calibration.scores.hard,i/100);assert(q>=prev);prev=q;}
});
test('generated bands are exact, independently unique and have replayable logic proofs',()=>{
  const observed=new Set();
  for(const difficulty of ['easy','medium','hard'])for(let n=0;n<45;n++) {
    const p=generate({difficulty,seed:`test-${difficulty}-${n}`,calibration,symmetry:n%2?'rotational':'none'});
    assert.equal(p.rating.band,difficulty);assert(p.rating.solved);assert(referenceValid(p.solution));
    assert(p.puzzle.every((v,i)=>v===0||v===p.solution[i]));
    const reference=referenceSolve(p.puzzle);assert.equal(reference.count,1);assert.deepEqual(reference.solution,p.solution);
    verifyTrace(p.puzzle,p.solution,p.rating.steps);
    p.rating.steps.forEach(s=>observed.add(s.technique));
    if(difficulty!=='easy')assert.equal(analyze(p.puzzle,{maxTier:difficulty==='medium'?1:2}).solved,false);
    if(n%2)assert(p.puzzle.every((v,i)=>Boolean(v)===Boolean(p.puzzle[80-i])));
  }
  assert(observed.has('nakedPair'));assert(observed.has('hiddenPair'));assert(observed.has('lockedCandidate'));
});
test('continuous endpoints, repeatability, honest budget failures and immutable inputs',()=>{
  for(const difficulty of [0,.2,1/3,.5,2/3,.8,1]){
    const p=generate({difficulty,seed:`slider-${difficulty}`,calibration});
    assert.equal(p.rating.band,['easy','medium','hard'][Math.min(2,Math.floor(difficulty*3))]);
    assert(p.transport.actualDifficulty>=0&&p.transport.actualDifficulty<=1);
    assert.equal(p.transport.withinTolerance,p.transport.quantileError<=.12);
  }
  const a=generate({difficulty:'hard',seed:'repeat',calibration}),b=generate({difficulty:'hard',seed:'repeat',calibration});
  assert.deepEqual(a.puzzle,b.puzzle);assert.deepEqual(a.rating,b.rating);assert.deepEqual(a.transport,b.transport);
  assert.throws(()=>generate({difficulty:'hard',seed:'probe',maxAttempts:1}),GenerationError);
  const input=parseBoard(classic),copy=input.slice();assert(analyze(input).solved);assert.deepEqual(input,copy);
  assert.equal(analyze(Array(81).fill(0)).solved,false);assert(isValid(input));
});

// Independent Algorithm X oracle. No production imports, bit masks, or peer graph.
export function referenceSolve(board,limit=2) {
  const rows=new Map(),columns=new Map();
  for(let c=0;c<324;c++)columns.set(c,new Set());
  for(let r=0;r<9;r++)for(let c=0;c<9;c++)for(let d=1;d<=9;d++) {
    if(board[r*9+c]&&board[r*9+c]!==d)continue;
    const key=(r*9+c)*9+d-1;
    const constraints=[r*9+c,81+r*9+d-1,162+c*9+d-1,243+(Math.floor(r/3)*3+Math.floor(c/3))*9+d-1];
    rows.set(key,constraints);for(const col of constraints)columns.get(col).add(key);
  }
  let count=0,solution=null;const chosen=[];
  function select(row) {
    const removed=[];
    for(const col of rows.get(row)) {
      const entries=columns.get(col);
      for(const entry of entries)for(const other of rows.get(entry))if(other!==col)columns.get(other)?.delete(entry);
      removed.push(entries);columns.delete(col);
    }
    return removed;
  }
  function deselect(row,removed) {
    for(const col of [...rows.get(row)].reverse()) {
      const entries=removed.pop();columns.set(col,entries);
      for(const entry of entries)for(const other of rows.get(entry))if(other!==col)columns.get(other)?.add(entry);
    }
  }
  function visit() {
    if(!columns.size) {
      count++;
      if(!solution){solution=Array(81).fill(0);for(const row of chosen)solution[Math.floor(row/9)]=row%9+1;}
      return;
    }
    let best=null;for(const entries of columns.values())if(best===null||entries.size<best.size)best=entries;
    if(!best.size)return;
    for(const row of [...best]) {
      chosen.push(row);const removed=select(row);visit();deselect(row,removed);chosen.pop();
      if(count>=limit)return;
    }
  }
  visit();return {count,solution};
}
export function referenceValid(board) {
  if(board.length!==81||board.some(x=>!Number.isInteger(x)||x<1||x>9))return false;
  for(let j=0;j<9;j++) {
    const row=[],col=[],box=[];
    for(let k=0;k<9;k++) {
      row.push(board[j*9+k]);col.push(board[k*9+j]);
      box.push(board[(Math.floor(j/3)*3+Math.floor(k/3))*9+(j%3*3+k%3)]);
    }
    if([row,col,box].some(a=>new Set(a).size!==9))return false;
  }
  return true;
}

// Replay each deduction against independently computed candidates and witnesses.
export function verifyTrace(puzzle,solution,steps) {
  const board=[...puzzle];
  const same=(a,b)=>Math.floor(a/9)===Math.floor(b/9)||a%9===b%9||
    (Math.floor(a/27)===Math.floor(b/27)&&Math.floor(a%9/3)===Math.floor(b%9/3));
  const unit=u=>Array.from({length:81},(_,i)=>i).filter(i=>u<9?Math.floor(i/9)===u:u<18?i%9===u-9:Math.floor(i/27)*3+Math.floor(i%9/3)===u-18);
  const digits=mask=>Array.from({length:9},(_,i)=>i+1).filter(d=>mask&(1<<(d-1)));
  const cs=board.map((v,i)=>new Set(v?[]:Array.from({length:9},(_,j)=>j+1).filter(d=>!board.some((n,k)=>n===d&&same(i,k)))));
  function ensure(ok,message){if(!ok)throw new Error(`Invalid trace: ${message}`);}
  for(const step of steps) {
    const {technique,cell,digit}=step;
    if('cell' in step) {
      ensure(!board[cell]&&cs[cell].has(digit)&&solution[cell]===digit,'placement');
      if(technique==='nakedSingle')ensure(cs[cell].size===1,'naked single');
      else if(technique==='hiddenSingle')ensure(unit(step.unit).filter(i=>cs[i].has(digit)).length===1,'hidden single');
      else ensure(false,'unknown placement');
      board[cell]=digit;cs[cell].clear();for(let i=0;i<81;i++)if(!board[i]&&same(cell,i))cs[i].delete(digit);
    }else {
      const selected=step.cells;
      ensure(selected.length>0,'missing witness');
      if(technique==='lockedCandidate') {
        const locations=unit(step.unit).filter(i=>cs[i].has(digit));
        ensure(locations.length>=2&&locations.every(i=>unit(step.targetUnit).includes(i)),'locked witness');
        for(const r of step.removals)ensure(unit(step.targetUnit).includes(r.cell)&&!unit(step.unit).includes(r.cell)&&r.mask===(1<<(digit-1)),'locked target');
      } else if(technique==='nakedPair'||technique==='nakedTriple') {
        const union=new Set(selected.flatMap(i=>[...cs[i]]));
        ensure(selected.length===(technique==='nakedPair'?2:3)&&union.size===selected.length&&selected.every(i=>unit(step.unit).includes(i)&&cs[i].size>=2),'naked subset');
        for(const r of step.removals)ensure(unit(step.unit).includes(r.cell)&&!selected.includes(r.cell)&&digits(r.mask).every(d=>union.has(d)),'subset target');
      } else if(technique==='hiddenPair') {
        const ds=digits(step.mask);ensure(ds.length===2&&selected.length===2,'hidden pair size');
        for(const d of ds){const locations=unit(step.unit).filter(i=>cs[i].has(d));ensure(locations.length===2&&locations.every(i=>selected.includes(i)),'hidden pair witness');}
        for(const r of step.removals)ensure(selected.includes(r.cell)&&digits(r.mask).every(d=>!ds.includes(d)),'hidden target');
      } else if(technique==='xWing') {
        const rows=step.orientation==='rows';
        const base=i=>rows?Math.floor(i/9):i%9,cross=i=>rows?i%9:Math.floor(i/9);
        const bases=new Set(selected.map(base)),crosses=new Set(selected.map(cross));
        ensure(selected.length===4&&bases.size===2&&crosses.size===2,'xwing shape');
        for(const b of bases){const locations=Array.from({length:81},(_,i)=>i).filter(i=>base(i)===b&&cs[i].has(digit));ensure(locations.length===2&&locations.every(i=>selected.includes(i)),'xwing witness');}
        for(const r of step.removals)ensure(!bases.has(base(r.cell))&&crosses.has(cross(r.cell))&&r.mask===(1<<(digit-1)),'xwing target');
      } else ensure(false,'unknown elimination');
      for(const {cell,mask} of step.removals)for(const d of digits(mask)) {
        ensure(cs[cell].has(d)&&solution[cell]!==d,'elimination removes solution or absent candidate');cs[cell].delete(d);
      }
    }
  }
  ensure(board.every((v,i)=>v===solution[i]),'incomplete proof');return true;
}

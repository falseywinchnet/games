import {readFileSync} from 'node:fs';
import {generate,analyze} from '../vendor/sudoku/src/engine.mjs';
import {referenceSolve,verifyTrace} from '../vendor/sudoku/test/reference.mjs';
const calibration=JSON.parse(readFileSync(new URL('../vendor/sudoku/src/calibration.json',import.meta.url)));
const directory=process.argv[2] ?? '/tmp/games-native-sudoku-validation';
for(let level=0;level<3;level++) {
 const lines=readFileSync(`${directory}/native-${level}.txt`,'utf8').split('\n');
 const rows=lines.slice(3,84).map(x=>x.split(' ').map(Number));
 const puzzle=rows.map(x=>x[0]),solution=rows.map(x=>x[1]);
 const oracle=referenceSolve(puzzle);
 if(oracle.count!==1 || String(oracle.solution)!==String(solution))throw Error('Independent native uniqueness failure');
 const band=['easy','medium','hard'][level];
 const node=generate({difficulty:band,seed:`native-integration-${level}`,calibration});
 if(String(node.puzzle)!==String(puzzle))throw Error('Native/Node seeded behavior differs');
 const rating=analyze(puzzle,{trace:true});
 if(rating.band!==band || !rating.solved)throw Error('Wrong difficulty');
 verifyTrace(puzzle,solution,rating.steps);
 if(level>0 && analyze(puzzle,{maxTier:level}).solved)throw Error('Lower tier unexpectedly solves');
 console.log(`${band}: native/Node exact match, independent unique solution, ${rating.steps.length} verified deductions.`);
}


import { BASICParser, DIALECTS, BASICOptions } from "./compiler";
import { BASICRuntime } from "./runtime";
import { lpad, rpad } from "../util";

function dumpDialectInfo() {
    var dialects = new Set<BASICOptions>();
    var array = {};
    var SELECTED_DIALECTS = ['TINY','ECMA55','HP','ALTAIR41','BASIC80','MODERN'];
    SELECTED_DIALECTS.forEach((dkey) => {
        dialects.add(DIALECTS[dkey]);
    });
    dialects.forEach((dialect) => {
        Object.entries(dialect).forEach(([key, value]) => {
            if (value === null) value = "all";
            else if (value === true) value = "Y";
            else if (value === false) value = "-";
            else if (Array.isArray(value))
                value = value.length;
            if (!array[key]) array[key] = [];
            array[key].push(value);
        });
    });
    Object.entries(array).forEach(([key, arr]) => {
        var s = rpad(key, 30) + "|";
        s += (arr as []).map((val) => rpad(val, 9)).join('|');
        console.log(s);
    });
    process.exit(0);
}

var readline = require('readline');
var rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    terminal: false,
    crlfDelay: Infinity,
});
var inputlines = [];
rl.on('line', (line) => {
    //console.log(`Line from file: ${line}`);
    inputlines.push(line);
});

var fs = require('fs');

var parser = new BASICParser();
var runtime = new BASICRuntime();

// parse args
var filename = '/dev/stdin';
var args = process.argv.slice(2);
for (var i=0; i<args.length; i++) {
    if (args[i] == '-v')
        runtime.trace = true;
    else if (args[i] == '-d')
        parser.opts = DIALECTS[args[++i]] || Error('no such dialect');
    else if (args[i] == '--dialects')
        dumpDialectInfo();
    else
        filename = args[i];
}

// parse file
var data = fs.readFileSync(filename, 'utf-8');
try {
    var pgm = parser.parseFile(data, filename);
} catch (e) {
    if (parser.errors.length == 0)
        console.log(`@@@ ${e}`);
    else
        console.log(e);
}
parser.errors.forEach((err) => console.log(`@@@ ${err.msg} (line ${err.label})`));
if (parser.errors.length) process.exit(2);

// run program
runtime.load(pgm);
runtime.reset();
runtime.print = (s:string) => {
    fs.writeSync(1, s+"");
}
runtime.input = async (prompt:string) => {
    return new Promise( (resolve, reject) => {
        function answered(answer) {
            var vals = answer.toUpperCase().split(',');
            //console.log(">>>",vals);
            resolve(vals);
        }
        prompt += ' ?';
        if (inputlines.length) {
            fs.writeSync(1, prompt);
            fs.writeSync(1, '\n');
            answered(inputlines.shift());
        } else rl.question(prompt, (answer) => {
            fs.writeSync(1, '\n');
            answered(answer);
        });
    });
}
runtime.resume = function() {
    process.nextTick(() => {
        try {
            if (runtime.step()) {
                if (runtime.running) runtime.resume();
            } else if (runtime.exited) {
                //console.log("*** PROGRAM EXITED ***");
                process.exit(0);
            }
        } catch (e) {
            console.log(`### ${e.message} (line ${runtime.getCurrentSourceLocation().label})`);
            process.exit(1);
        }
    });
}
runtime.resume();

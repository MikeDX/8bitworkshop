import { WorkerError, WorkerResult } from "../../common/workertypes";
import {
  BuildStep,
  gatherFiles,
  staleFiles,
  populateFiles,
  putWorkFile,
  populateExtraFiles,
  store,
} from "../builder";
import { makeErrorMatcher } from "../listingutils";
import { loadNative, moduleInstFn, print_fn, execMain, emglobal, EmscriptenModule } from "../wasmutils";

function tccErrorMatcher(errors: WorkerError[], mainpath: string) {
  return makeErrorMatcher(errors, /([^:]+|tcc):(\d+|\s*error): (.+)/, 2, 3, mainpath, 1);
}

function runTool(step: BuildStep, mod: EmscriptenModule, args: string[], label: string) {
  try {
    execMain(step, mod, args);
  } catch (e: any) {
    if (e && (e.status === 0 || (e.name === "ExitStatus" && e.status === 0))) return;
    if (e && typeof e.status === "number" && e.status !== 0) {
      throw new Error(`${label} exited ${e.status}`);
    }
    throw e;
  }
}

async function loadSnesTool(name: string, printErr?: (s: string) => void): Promise<EmscriptenModule> {
  loadNative(name);
  const factory = emglobal[name];
  if (!factory) throw new Error(`SNES tool ${name} not loaded`);
  return factory({
    instantiateWasm: moduleInstFn(name),
    noInitialRun: true,
    print: print_fn,
    printErr: printErr || print_fn,
  });
}

function ensureDir(FS: any, p: string) {
  try {
    FS.mkdir(p);
  } catch (_) {
    /* exists */
  }
}

function readText(FS: any, path: string): string {
  return FS.readFile(path, { encoding: "utf8" });
}

function readBin(FS: any, path: string): Uint8Array {
  return FS.readFile(path, { encoding: "binary" }) as Uint8Array;
}

/**
 * Compile SNES C via tcc816 → wla-65816 → wlalink.
 * Always links crt0 + snes lib. Links tiles.asm when hello_tiles.chr is present.
 */
export async function compileSnesC(step: BuildStep): Promise<WorkerResult> {
  const params = step.params;
  const errors: WorkerError[] = [];
  gatherFiles(step, { mainFilePath: "main.c" });
  const destpath = step.prefix + (params.rom_ext || ".sfc");
  if (!staleFiles(step, [destpath])) {
    return;
  }

  const base = step.prefix;
  const extra = params.extra_compile_files || [
    "hdr.asm",
    "crt0.asm",
    "snes.asm",
    "snes.h",
    "tiles.asm",
    "hello_tiles.chr",
  ];

  let genAsm: string;
  {
    const tcc = await loadSnesTool("tcc816", tccErrorMatcher(errors, step.path));
    const FS = tcc.FS;
    ensureDir(FS, "/work");
    FS.chdir("/work");
    populateExtraFiles(step, FS, extra.filter((f: string) => f.endsWith(".h") || f.endsWith(".asm") || f.endsWith(".chr")));
    populateFiles(step, FS, { mainFilePath: step.path });
    runTool(step, tcc, ["-c", "-I", "/work", "-o", `${base}.asm`, step.path], "tcc816");
    if (errors.length) return { errors };
    genAsm = readText(FS, `${base}.asm`);
  }

  async function assemble(
    objName: string,
    srcName: string,
    files: Record<string, string | Uint8Array>,
    extraArgs: string[] = [],
  ): Promise<Uint8Array> {
    const wlaErrs: WorkerError[] = [];
    const wla = await loadSnesTool(
      "wla65816",
      makeErrorMatcher(wlaErrs, /([^:\s]+):(\d+):\s*(.+)/, 2, 3, srcName, 1),
    );
    const FS = wla.FS;
    ensureDir(FS, "/work");
    FS.chdir("/work");
    for (const [name, data] of Object.entries(files)) {
      if (typeof data === "string") FS.writeFile(name, data);
      else FS.writeFile(name, data, { encoding: "binary" });
    }
    runTool(step, wla, ["-q", "-I", "/work", ...extraArgs, "-o", objName, srcName], `wla ${srcName}`);
    if (wlaErrs.length) errors.push(...wlaErrs);
    return readBin(FS, objName);
  }

  const boot = await loadSnesTool("tcc816");
  populateExtraFiles(step, boot.FS, extra);
  populateFiles(step, boot.FS);

  const hdr = readText(boot.FS, "hdr.asm");
  const crt0 = readText(boot.FS, "crt0.asm");
  const snesAsm = readText(boot.FS, "snes.asm");
  const wantTiles = !!(store.workfs["hello_tiles.chr"] || boot.FS.analyzePath("hello_tiles.chr").exists);

  const objs: [string, Uint8Array][] = [];
  objs.push(["crt0.obj", await assemble("crt0.obj", "crt0.asm", { "hdr.asm": hdr, "crt0.asm": crt0 })]);
  if (errors.length) return { errors };
  objs.push(["snes.obj", await assemble("snes.obj", "snes.asm", { "hdr.asm": hdr, "snes.asm": snesAsm })]);
  if (errors.length) return { errors };

  if (wantTiles) {
    try {
      const tilesAsm = readText(boot.FS, "tiles.asm");
      const chr = readBin(boot.FS, "hello_tiles.chr");
      objs.push([
        "tiles.obj",
        await assemble("tiles.obj", "tiles.asm", {
          "hdr.asm": hdr,
          "tiles.asm": tilesAsm,
          "hello_tiles.chr": chr,
        }),
      ]);
      if (errors.length) return { errors };
    } catch (_) {
      /* optional */
    }
  }

  objs.push([
    `${base}.obj`,
    await assemble(
      `${base}.obj`,
      `${base}.asm`,
      { "hdr.asm": hdr, [`${base}.asm`]: genAsm },
      ["-D", `WLA_FILENAME=${base}`],
    ),
  ]);
  if (errors.length) return { errors };

  const link = await loadSnesTool("wlalink");
  const FS = link.FS;
  ensureDir(FS, "/work");
  FS.chdir("/work");
  let linkfile = "[objects]\n";
  for (const [name, data] of objs) {
    FS.writeFile(name, data, { encoding: "binary" });
    linkfile += name + "\n";
  }
  FS.writeFile("linkfile", linkfile);
  runTool(step, link, ["-d", "-s", "-A", "-c", "linkfile", destpath], "wlalink");

  const rom = readBin(FS, destpath);
  putWorkFile(destpath, rom);
  return { output: rom, errors };
}

// Runs one of OpenVSP's external programs (vspaero, vsploads; compiled to WebAssembly, built by
// web/wasm/build_wasm.ps1) for the in-browser engine, the way a process would run: a fresh
// instance per run, with copies of the working files. Started by src/api/wasmEngine.ts.
//
//   in:  { cmd, args, cwd, dirs, files: [{ path, data }] }
//   out: { type: 'out', text }                       the program's output, as it runs
//        { type: 'exit', code, files: [{ path, data }] }   files it created or changed in dirs
const PROGRAMS = new Set(['vspaero', 'vsploads'])

function mkdirs(FS, dir) {
  let path = ''
  for (const part of dir.split('/').filter(Boolean)) {
    path += '/' + part
    if (!FS.analyzePath(path).exists) FS.mkdir(path)
  }
}

function filesIn(FS, dir) {
  if (!FS.analyzePath(dir).exists) return []
  return FS.readdir(dir)
    .filter((name) => name !== '.' && name !== '..')
    .map((name) => (dir === '/' ? '' : dir) + '/' + name)
    .filter((path) => FS.isFile(FS.stat(path).mode))
}

self.onmessage = async ({ data }) => {
  const { cmd, args, cwd, dirs, files } = data
  const out = (text) => self.postMessage({ type: 'out', text: text + '\n' })
  let code = 1
  let changed = []
  try {
    if (!PROGRAMS.has(cmd)) throw new Error(`${cmd} is not available in the browser`)
    const { default: createProgram } = await import(`./wasm/${cmd}.js`)
    const program = await createProgram({ noInitialRun: true, print: out, printErr: out })
    const FS = program.FS
    for (const f of files) {
      mkdirs(FS, f.path.slice(0, f.path.lastIndexOf('/')) || '/')
      FS.writeFile(f.path, f.data)
    }
    mkdirs(FS, cwd)
    FS.chdir(cwd)
    const before = new Map()
    for (const dir of dirs) for (const path of filesIn(FS, dir)) before.set(path, FS.stat(path).mtime.getTime())

    try {
      code = program.callMain(args)
    } catch (e) {
      // exit() ends the program by throwing.
      if (e && typeof e.status === 'number') code = e.status
      else {
        out(`${cmd}: ${e && e.message ? e.message : e}`)
        code = 1
      }
    }

    for (const dir of dirs) {
      for (const path of filesIn(FS, dir)) {
        if (before.get(path) !== FS.stat(path).mtime.getTime()) changed.push({ path, data: FS.readFile(path) })
      }
    }
  } catch (e) {
    out(`${cmd}: ${e && e.message ? e.message : e}`)
    code = 127
    changed = []
  }
  self.postMessage({ type: 'exit', code, files: changed }, changed.map((f) => f.data.buffer))
}

import { disassemble, DEVS_DBG_FILE } from "@devicescript/compiler"
import { BINDIR, CmdOptions, log } from "./command"
import { readCompiled } from "./run"

export interface DisAsmOptions {
    detailed?: boolean
}

export async function disasm(fn: string, options: DisAsmOptions & CmdOptions) {
    if (!fn) fn = BINDIR + "/" + DEVS_DBG_FILE
    const tmp = await readCompiled(fn)
    log(disassemble(tmp.dbg ?? tmp.binary, options.detailed))
}

// Export Ghidra decompilation for the addresses given as script arguments:
//   ExportDecomp.java <out_dir> <hex_addr> [<hex_addr> ...]
// Xbox 360 code saves/restores registers through shared __savegprlr_N /
// __restgprlr_N helpers (0x8279E000-0x8279F000 here). Auto-analysis can treat
// them as non-returning, which cuts every caller after its first `bl`. This
// script first clears no-return on functions in that range and on the
// targets' callees, re-disassembles and re-creates each target's body, then
// writes <out_dir>/<addr>.c with the decompiled C and a header noting the
// function name, body range and any decompiler warnings.
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;

import java.io.File;
import java.io.PrintWriter;

public class ExportDecomp extends GhidraScript {
  private int fixedNoReturn = 0;

  private void clearNoReturn(Function f) throws Exception {
    if (f != null && f.hasNoReturn()) {
      f.setNoReturn(false);
      fixedNoReturn++;
    }
  }

  @Override
  protected void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 2) {
      printerr("usage: ExportDecomp.java <out_dir> <hex_addr>...");
      return;
    }
    File out = new File(args[0]);
    out.mkdirs();

    AddressSet helpers = new AddressSet(toAddr(0x8279E000L), toAddr(0x8279EFFFL));
    FunctionIterator it = currentProgram.getFunctionManager().getFunctions(helpers, true);
    while (it.hasNext()) clearNoReturn(it.next());

    for (int pass = 0; pass < 3; pass++) {
      for (int i = 1; i < args.length; i++) {
        Address a = toAddr(Long.parseLong(args[i], 16));
        new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
        Function fn = getFunctionAt(a);
        if (fn == null) {
          new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
          fn = getFunctionAt(a);
        }
        if (fn == null) continue;
        clearNoReturn(fn);
        CreateFunctionCmd.fixupFunctionBody(currentProgram, fn, monitor);
        for (Instruction ins : currentProgram.getListing().getInstructions(fn.getBody(), true)) {
          if (!ins.getFlowType().isCall()) continue;
          for (Address t : ins.getFlows()) {
            Function callee = getFunctionAt(t);
            if (callee != null) clearNoReturn(callee);
          }
          ins.setFallThrough(ins.getDefaultFallThrough());
        }
        CreateFunctionCmd.fixupFunctionBody(currentProgram, fn, monitor);
      }
    }
    println("cleared no-return on " + fixedNoReturn + " functions");

    DecompInterface ifc = new DecompInterface();
    ifc.openProgram(currentProgram);
    for (int i = 1; i < args.length; i++) {
      String hex = args[i];
      Address a = toAddr(Long.parseLong(hex, 16));
      Function fn = getFunctionAt(a);
      if (fn == null) fn = getFunctionContaining(a);
      File file = new File(out, hex.toUpperCase() + ".c");
      try (PrintWriter w = new PrintWriter(file, "UTF-8")) {
        if (fn == null) {
          w.println("// no function at " + hex);
          continue;
        }
        DecompileResults r = ifc.decompileFunction(fn, 120, monitor);
        w.println("// " + fn.getName() + " entry " + fn.getEntryPoint() + " body " + fn.getBody());
        if (r.getErrorMessage() != null && !r.getErrorMessage().isEmpty())
          w.println("// decompiler: " + r.getErrorMessage().replace('\n', ' '));
        if (r.getDecompiledFunction() != null) w.println(r.getDecompiledFunction().getC());
        else w.println("// no decompilation");
      }
      println("exported " + hex);
    }
    ifc.dispose();
  }
}

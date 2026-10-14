// Post-analysis for ALIFE.EXE:
//  1. Create functions for orphan code: code reached only through
//     function-pointer tables (e.g. compbun's fileSystem[] / codecList[]) that
//     Ghidra disassembled but never turned into functions, and unreferenced
//     functions sitting undefined right after a RET.
//  2. Only for programs with the __watcall extension (ALIFEDOS.EXE, see
//     AfterlifeSetup.java): give every internal (non-thunk, non-external) function whose calling
//     convention is still unknown/default the __watcall convention. Imports and
//     thunks to imports keep their Win32 __stdcall prototypes.
//@category Afterlife

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.listing.Data;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class AfterlifePost extends GhidraScript {
	@Override
	protected void run() throws Exception {
		int created = 0;
		for (int pass = 0; pass < 5; pass++) {
			int n = createOrphanFunctions() + functionsAfterRet();
			created += n;
			if (n == 0) {
				break;
			}
		}
		println("created " + created + " functions for orphan code");

		if (currentProgram.getCompilerSpec().getCallingConvention("__watcall") == null) {
			return; // MSVC-built ALIFE.EXE: keep Ghidra's __cdecl/__stdcall defaults
		}
		int changed = 0, kept = 0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			if (f.isThunk() || f.isExternal()) {
				kept++;
				continue;
			}
			String cc = f.getCallingConventionName();
			if (cc == null || cc.equals("unknown") || cc.equals("default") ||
				(cc.equals("__stdcall") && f.getSignatureSource() == SourceType.DEFAULT)) {
				f.setCallingConvention("__watcall");
				changed++;
			}
			else {
				kept++;
			}
		}
		println("set __watcall on " + changed + " functions, kept " + kept);
	}

	/** Unreferenced functions: undefined bytes right after a RET (skipping
	 *  00/90/CC fill) are almost always the start of a function nobody calls
	 *  directly. Gaps after a JMP are left alone: in this unoptimized code they
	 *  are usually dead blocks inside a function. */
	private int functionsAfterRet() throws Exception {
		Listing listing = currentProgram.getListing();
		int created = 0;
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			if (!b.isExecute() || !b.isInitialized()) {
				continue;
			}
			Address a = b.getStart();
			while (!monitor.isCancelled()) {
				Data d = listing.getUndefinedDataAfter(a, monitor);
				if (d == null || !b.contains(d.getAddress())) {
					break;
				}
				Address s = d.getAddress();
				a = s;
				Instruction prev = listing.getInstructionBefore(s);
				if (prev == null || !prev.getFlowType().isTerminal() ||
					prev.getFlowType().isJump()) {
					continue;
				}
				// prev must end right before s, possibly followed by fill bytes
				Address q = prev.getMaxAddress().next();
				boolean fillOnly = true;
				for (; q.compareTo(s) < 0; q = q.next()) {
					int v = getByte(q) & 0xff;
					if (v != 0x00 && v != 0x90 && v != 0xcc) {
						fillOnly = false;
						break;
					}
				}
				while (fillOnly && b.contains(s) && listing.getUndefinedDataAt(s) != null) {
					int v = getByte(s) & 0xff;
					if (v != 0x00 && v != 0x90 && v != 0xcc) {
						break;
					}
					s = s.next();
				}
				if (!fillOnly || !b.contains(s) || listing.getUndefinedDataAt(s) == null) {
					continue;
				}
				if (disassemble(s) && createFunction(s, null) != null) {
					created++;
				}
			}
		}
		return created;
	}

	/** Create a function at every instruction that is outside any function and
	 *  is not reached by fall-through from the previous instruction. */
	private int createOrphanFunctions() throws Exception {
		Listing listing = currentProgram.getListing();
		FunctionManager fm = currentProgram.getFunctionManager();
		int created = 0;
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			if (!b.isExecute()) {
				continue;
			}
			InstructionIterator it = listing.getInstructions(b.getStart(), true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Instruction insn = it.next();
				Address a = insn.getAddress();
				if (!b.contains(a)) {
					break;
				}
				if (fm.getFunctionContaining(a) != null) {
					continue;
				}
				Instruction prev = listing.getInstructionBefore(a);
				if (prev != null && a.equals(prev.getFallThrough())) {
					continue;
				}
				Function f = createFunction(a, null);
				if (f != null) {
					created++;
					it = listing.getInstructions(f.getBody().getMaxAddress().next(), true);
				}
			}
		}
		return created;
	}
}

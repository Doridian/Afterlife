// Pre-analysis setup: install the __watcall prototype as a compiler-spec
// extension and make it the prototype-evaluation model, so analysis treats
// unknown internal functions as Watcom register-convention functions.
// Run as -preScript right after import (see tools/ghidra/setup.sh).
//@category Afterlife

import java.nio.file.Files;
import java.nio.file.Path;

import ghidra.app.script.GhidraScript;
import ghidra.framework.options.Options;
import ghidra.program.database.ProgramCompilerSpec;
import ghidra.program.database.SpecExtension;

public class AfterlifeSetup extends GhidraScript {
	@Override
	protected void run() throws Exception {
		Path xml = getSourceFile().getParentFile().getParentFile().getFile(false).toPath()
				.resolve("watcall.xml");
		String doc = Files.readString(xml);
		SpecExtension ext = new SpecExtension(currentProgram);
		ext.addReplaceCompilerSpecExtension(doc, monitor);
		Options opts = currentProgram.getOptions(ProgramCompilerSpec.DECOMPILER_PROPERTY_LIST_NAME);
		opts.setString(ProgramCompilerSpec.EVALUATION_MODEL_PROPERTY_NAME, "__watcall");
		StringBuilder names = new StringBuilder();
		for (var m : currentProgram.getCompilerSpec().getCallingConventions()) {
			names.append(' ').append(m.getName());
		}
		println("installed __watcall; calling conventions:" + names);
	}
}

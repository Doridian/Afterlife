// Command dispatcher behind tools/gh. Not meant to be run by hand.
//
// Script args: <outfile> <command> [args...]
//   or         <outfile> batch <cmdfile>   (one command per line, args tab-separated)
// Output goes to <outfile>; "<outfile>.rc" gets 0 on success, 1 if any command failed.
//@category Afterlife

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.regex.Pattern;

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.parser.FunctionSignatureParser;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import ghidra.util.data.DataTypeParser;

public class GhCli extends GhidraScript {

	private PrintWriter out;
	private DecompInterface decomp;
	private boolean failed;

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 2) {
			throw new IllegalArgumentException("usage: GhCli <outfile> <command> [args]");
		}
		String outPath = args[0];
		try (PrintWriter pw = new PrintWriter(new OutputStreamWriter(
			new FileOutputStream(outPath), StandardCharsets.UTF_8))) {
			out = pw;
			List<String[]> cmds = new ArrayList<>();
			if (args[1].equals("batch")) {
				for (String line : Files.readAllLines(Path.of(args[2]), StandardCharsets.UTF_8)) {
					if (!line.isBlank()) {
						cmds.add(line.split("\t", -1));
					}
				}
			}
			else {
				cmds.add(Arrays.copyOfRange(args, 1, args.length));
			}
			boolean headers = cmds.size() > 1;
			for (String[] c : cmds) {
				if (headers) {
					out.println("=== " + String.join(" ", c));
				}
				try {
					dispatch(c);
				}
				catch (Exception e) {
					failed = true;
					out.println("error: " + e.getMessage());
				}
				out.flush();
			}
		}
		finally {
			if (decomp != null) {
				decomp.dispose();
			}
			Files.writeString(Path.of(outPath + ".rc"), failed ? "1\n" : "0\n");
		}
	}

	private void dispatch(String[] c) throws Exception {
		String cmd = c[0];
		String[] a = Arrays.copyOfRange(c, 1, c.length);
		switch (cmd) {
			case "decompile": need(a, 1); cmdDecompile(a[0]); break;
			case "disasm": need(a, 1); cmdDisasm(a[0]); break;
			case "xrefs": need(a, 1); cmdXrefs(a[0]); break;
			case "listing": need(a, 1); cmdListing(a[0], a.length > 1 ? Integer.parseInt(a[1]) : 32); break;
			case "strings": cmdStrings(a); break;
			case "funcs": cmdFuncs(a); break;
			case "apply": cmdApply(a.length > 0 ? a[0] : null); break;
			case "rename": need(a, 2); cmdRename(a[0], a[1], a.length > 2 ? a[2] : null); break;
			case "export": cmdExport(a.length > 0 ? a[0] : null); break;
			default: throw new IllegalArgumentException("unknown command: " + cmd);
		}
	}

	private static void need(String[] a, int n) {
		if (a.length < n) {
			throw new IllegalArgumentException("missing argument");
		}
	}

	// ---------------------------------------------------------------- helpers

	private Address resolve(String s) {
		SymbolTable st = currentProgram.getSymbolTable();
		Symbol best = null;
		for (Symbol sym : st.getSymbols(s)) {
			if (sym.isExternal()) {
				continue;
			}
			if (best == null || sym.getSymbolType() == SymbolType.FUNCTION) {
				best = sym;
			}
		}
		if (best != null) {
			return best.getAddress();
		}
		String h = s;
		int us = h.lastIndexOf('_');
		if (us >= 0 && h.matches("[A-Za-z]+_[0-9A-Fa-f]+")) {
			h = h.substring(us + 1); // FUN_004012a0, DAT_..., LAB_...
		}
		if (h.startsWith("0x") || h.startsWith("0X")) {
			h = h.substring(2);
		}
		if (!h.matches("[0-9A-Fa-f]{1,8}")) {
			throw new IllegalArgumentException("no symbol or address: " + s);
		}
		Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace()
				.getAddress(Long.parseUnsignedLong(h, 16));
		if (!currentProgram.getMemory().contains(addr)) {
			throw new IllegalArgumentException("address not in memory: " + addr);
		}
		return addr;
	}

	private Function funcAt(String s) {
		Address a = resolve(s);
		Function f = currentProgram.getFunctionManager().getFunctionContaining(a);
		if (f == null) {
			throw new IllegalArgumentException("no function contains " + a);
		}
		return f;
	}

	private static String hex(Address a) {
		return String.format("%08x", a.getOffset());
	}

	private String where(Address a) {
		Function f = currentProgram.getFunctionManager().getFunctionContaining(a);
		if (f == null) {
			MemoryBlock b = currentProgram.getMemory().getBlock(a);
			return b == null ? "?" : b.getName();
		}
		long off = a.subtract(f.getEntryPoint());
		return f.getName() + (off == 0 ? "" : String.format("+0x%x", off));
	}

	private static String escape(String s) {
		StringBuilder b = new StringBuilder();
		for (char ch : s.toCharArray()) {
			switch (ch) {
				case '\n': b.append("\\n"); break;
				case '\r': b.append("\\r"); break;
				case '\t': b.append("\\t"); break;
				case '"': b.append("\\\""); break;
				case '\\': b.append("\\\\"); break;
				default:
					if (ch < 0x20 || ch == 0x7f) {
						b.append(String.format("\\x%02x", (int) ch));
					}
					else {
						b.append(ch);
					}
			}
		}
		return b.toString();
	}

	private static Pattern pattern(String[] a, int i) {
		return i < a.length ? Pattern.compile(a[i], Pattern.CASE_INSENSITIVE) : null;
	}

	// --------------------------------------------------------------- commands

	private void cmdDecompile(String s) throws Exception {
		Function f = funcAt(s);
		if (decomp == null) {
			decomp = new DecompInterface();
			DecompileOptions o = new DecompileOptions();
			o.grabFromProgram(currentProgram);
			decomp.setOptions(o);
			decomp.toggleCCode(true);
			decomp.setSimplificationStyle("decompile");
			if (!decomp.openProgram(currentProgram)) {
				throw new IllegalStateException("decompiler: " + decomp.getLastMessage());
			}
		}
		DecompileResults r = decomp.decompileFunction(f, 120, monitor);
		if (r == null || !r.decompileCompleted()) {
			throw new IllegalStateException("decompile failed: " +
				(r == null ? "?" : r.getErrorMessage()));
		}
		out.println("/* " + hex(f.getEntryPoint()) + " " + f.getName() + " (" +
			f.getCallingConventionName() + ", " + f.getBody().getNumAddresses() + " bytes) */");
		out.print(r.getDecompiledFunction().getC());
	}

	private void cmdDisasm(String s) {
		Function f = funcAt(s);
		Listing listing = currentProgram.getListing();
		SymbolTable st = currentProgram.getSymbolTable();
		CodeUnitFormat fmt = new CodeUnitFormat(new CodeUnitFormatOptions());
		out.println("; " + f.getPrototypeString(false, true));
		for (CodeUnit cu : listing.getCodeUnits(f.getBody(), true)) {
			printCodeUnit(cu, fmt, st);
		}
	}

	private void cmdListing(String s, int count) {
		Address a = resolve(s);
		Listing listing = currentProgram.getListing();
		CodeUnitFormat fmt = new CodeUnitFormat(new CodeUnitFormatOptions());
		CodeUnit cu = listing.getCodeUnitContaining(a);
		CodeUnitIterator it = listing.getCodeUnits(cu != null ? cu.getAddress() : a, true);
		for (int i = 0; i < count && it.hasNext(); i++) {
			printCodeUnit(it.next(), fmt, currentProgram.getSymbolTable());
		}
	}

	private void printCodeUnit(CodeUnit cu, CodeUnitFormat fmt, SymbolTable st) {
		Address a = cu.getAddress();
		for (Symbol sym : st.getSymbols(a)) {
			out.println(sym.getName() + ":");
		}
		String pre = cu.getComment(CommentType.PRE);
		if (pre != null) {
			for (String l : pre.split("\n")) {
				out.println("        ; " + l);
			}
		}
		StringBuilder bytes = new StringBuilder();
		try {
			for (byte b : cu.getBytes()) {
				if (bytes.length() >= 24) {
					bytes.append("..");
					break;
				}
				bytes.append(String.format("%02x", b & 0xff));
			}
		}
		catch (Exception e) {
			bytes.append("??");
		}
		String line = String.format("  %s  %-26s %s", hex(a), bytes, fmt.getRepresentationString(cu));
		String eol = cu.getComment(CommentType.EOL);
		out.println(eol == null ? line : line + "    ; " + eol.replace("\n", " "));
	}

	private void cmdXrefs(String s) {
		Address a = resolve(s);
		Function f = currentProgram.getFunctionManager().getFunctionAt(a);
		out.println("; refs to " + hex(a) + " " + (f != null ? f.getName() : where(a)));
		int n = 0;
		for (Reference r : currentProgram.getReferenceManager().getReferencesTo(a)) {
			out.println(String.format("%s  %-16s %s", hex(r.getFromAddress()),
				r.getReferenceType().getName(), where(r.getFromAddress())));
			n++;
		}
		if (n == 0) {
			out.println("; (none)");
		}
	}

	private void cmdStrings(String[] a) {
		boolean withXrefs = false;
		List<String> rest = new ArrayList<>();
		for (String x : a) {
			if (x.equals("-x")) {
				withXrefs = true;
			}
			else {
				rest.add(x);
			}
		}
		Pattern p = pattern(rest.toArray(new String[0]), 0);
		ReferenceManager rm = currentProgram.getReferenceManager();
		for (Data d : currentProgram.getListing().getDefinedData(true)) {
			if (!d.hasStringValue()) {
				continue;
			}
			StringDataInstance sdi = StringDataInstance.getStringDataInstance(d);
			String v = sdi.getStringValue();
			if (v == null || (p != null && !p.matcher(v).find())) {
				continue;
			}
			StringBuilder line = new StringBuilder(hex(d.getAddress()) + "  \"" + escape(v) + "\"");
			if (withXrefs) {
				Set<String> fs = new LinkedHashSet<>();
				for (Reference r : rm.getReferencesTo(d.getAddress())) {
					fs.add(where(r.getFromAddress()));
				}
				line.append("  <- ").append(fs.isEmpty() ? "(none)" : String.join(", ", fs));
			}
			out.println(line);
		}
	}

	private void cmdFuncs(String[] a) {
		boolean userOnly = false;
		List<String> rest = new ArrayList<>();
		for (String x : a) {
			if (x.equals("-u")) {
				userOnly = true;
			}
			else {
				rest.add(x);
			}
		}
		Pattern p = pattern(rest.toArray(new String[0]), 0);
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			if (userOnly && f.getSymbol().getSource() != SourceType.USER_DEFINED) {
				continue;
			}
			if (p != null && !p.matcher(f.getName()).find()) {
				continue;
			}
			out.println(String.format("%s  %6d  %s", hex(f.getEntryPoint()),
				f.getBody().getNumAddresses(), f.getName()));
		}
	}

	// ----------------------------------------------------------- symbol file

	private static class Entry {
		long addr;
		String kind, name, sig, comment;
		List<String> pre = new ArrayList<>(); // standalone comment lines above it

		String format() {
			StringBuilder b = new StringBuilder(String.format("0x%08x %-4s %s", addr, kind, name));
			if (sig != null && !sig.isEmpty()) {
				b.append(" | ").append(sig);
			}
			if (comment != null && !comment.isEmpty()) {
				b.append("  # ").append(comment);
			}
			return b.toString();
		}
	}

	private static class SymFile {
		List<String> header = new ArrayList<>();
		TreeMap<Long, Entry> entries = new TreeMap<>();
		List<String> trailer = new ArrayList<>();
	}

	private Path symPath(String override) {
		if (override != null) {
			return Path.of(override);
		}
		Path root = getSourceFile().getParentFile().getParentFile().getParentFile().getParentFile()
				.getFile(false).toPath();
		return root.resolve("symbols").resolve(currentProgram.getName() + ".txt");
	}

	private static SymFile readSyms(Path p) throws IOException {
		SymFile sf = new SymFile();
		if (!Files.exists(p)) {
			return sf;
		}
		List<String> pending = new ArrayList<>();
		int lineNo = 0;
		for (String raw : Files.readAllLines(p, StandardCharsets.UTF_8)) {
			lineNo++;
			String line = raw.strip();
			if (line.isEmpty() || line.startsWith("#")) {
				if (sf.entries.isEmpty()) {
					sf.header.add(raw);
				}
				else {
					pending.add(raw);
				}
				continue;
			}
			Entry e = new Entry();
			int hash = line.indexOf('#');
			if (hash >= 0) {
				e.comment = line.substring(hash + 1).strip();
				line = line.substring(0, hash).strip();
			}
			int bar = line.indexOf('|');
			if (bar >= 0) {
				e.sig = line.substring(bar + 1).strip();
				line = line.substring(0, bar).strip();
			}
			String[] f = line.split("\\s+");
			if (f.length != 3 || !(f[1].equals("func") || f[1].equals("data"))) {
				throw new IOException(p + ":" + lineNo + ": expected 'ADDR func|data NAME'");
			}
			String h = f[0].toLowerCase().startsWith("0x") ? f[0].substring(2) : f[0];
			e.addr = Long.parseUnsignedLong(h, 16);
			e.kind = f[1];
			e.name = f[2];
			e.pre.addAll(pending);
			pending.clear();
			if (sf.entries.put(e.addr, e) != null) {
				throw new IOException(p + ":" + lineNo + ": duplicate address " + f[0]);
			}
		}
		sf.trailer.addAll(pending);
		return sf;
	}

	private static void writeSyms(Path p, SymFile sf) throws IOException {
		List<String> lines = new ArrayList<>(sf.header);
		for (Entry e : sf.entries.values()) {
			lines.addAll(e.pre);
			lines.add(e.format());
		}
		lines.addAll(sf.trailer);
		Path tmp = p.resolveSibling(p.getFileName() + ".tmp");
		Files.write(tmp, lines, StandardCharsets.UTF_8);
		Files.move(tmp, p, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
	}

	private static boolean hasConvention(String sig) {
		return sig.matches(".*\\b__(watcall|stdcall|cdecl|fastcall|thiscall)\\b.*");
	}

	/** Convention assumed when a signature names none: __watcall for the
	 *  Watcom-built DOS exe (if its extension is installed), else __cdecl. */
	private String defaultCC() {
		return currentProgram.getCompilerSpec().getCallingConvention("__watcall") != null
				? "__watcall" : "__cdecl";
	}

	private Address addr(long off) {
		return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(off);
	}

	/** Apply one entry to the DB. Returns a short status or throws. */
	private String applyEntry(Entry e) throws Exception {
		Address a = addr(e.addr);
		if (!currentProgram.getMemory().contains(a)) {
			throw new IllegalArgumentException("address not in memory");
		}
		FunctionManager fm = currentProgram.getFunctionManager();
		SymbolTable st = currentProgram.getSymbolTable();
		boolean changed = false;
		if (e.kind.equals("func")) {
			Function f = fm.getFunctionAt(a);
			if (f == null) {
				if (currentProgram.getListing().getInstructionAt(a) == null) {
					disassemble(a);
				}
				f = createFunction(a, e.name);
				if (f == null) {
					throw new IllegalStateException("could not create function");
				}
				f.setCallingConvention(defaultCC());
				changed = true;
			}
			if (e.sig != null && !e.sig.isEmpty()) {
				FunctionSignatureParser parser =
					new FunctionSignatureParser(currentProgram.getDataTypeManager(), null);
				FunctionDefinitionDataType fdt = parser.parse(f.getSignature(), e.sig);
				fdt.setName(e.name);
				String before = f.getPrototypeString(false, true);
				ApplyFunctionSignatureCmd cmd =
					new ApplyFunctionSignatureCmd(a, fdt, SourceType.USER_DEFINED);
				if (!cmd.applyTo(currentProgram)) {
					throw new IllegalStateException("signature: " + cmd.getStatusMsg());
				}
				if (!hasConvention(e.sig)) {
					f.setCallingConvention(defaultCC());
				}
				changed |= !before.equals(f.getPrototypeString(false, true));
			}
			if (!f.getName().equals(e.name)) {
				f.setName(e.name, SourceType.USER_DEFINED);
				changed = true;
			}
		}
		else {
			Symbol prim = st.getPrimarySymbol(a);
			if (prim == null || !prim.getName().equals(e.name)) {
				Symbol s = st.createLabel(a, e.name, SourceType.USER_DEFINED);
				s.setPrimary();
				changed = true;
			}
			for (Symbol s : st.getSymbols(a)) {
				if (s.getSymbolType() == SymbolType.LABEL && !s.getName().equals(e.name) &&
					s.getSource() == SourceType.USER_DEFINED) {
					s.delete(); // stale name from an earlier apply
				}
			}
			if (e.sig != null && !e.sig.isEmpty()) {
				DataTypeManager dtm = currentProgram.getDataTypeManager();
				DataType dt = new DataTypeParser(dtm, dtm, null,
					DataTypeParser.AllowedDataTypes.FIXED_LENGTH).parse(e.sig);
				Data d = currentProgram.getListing().getDataAt(a);
				if (d == null || !d.getDataType().isEquivalent(dt)) {
					DataUtilities.createData(currentProgram, a, dt, -1,
						DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
					changed = true;
				}
			}
		}
		return changed ? "changed" : "same";
	}

	private void cmdApply(String file) throws Exception {
		Path p = symPath(file);
		SymFile sf = readSyms(p);
		int changed = 0, same = 0, errors = 0;
		for (Entry e : sf.entries.values()) {
			try {
				if (applyEntry(e).equals("changed")) {
					changed++;
				}
				else {
					same++;
				}
			}
			catch (Exception ex) {
				errors++;
				failed = true;
				out.println(String.format("error: 0x%08x %s: %s", e.addr, e.name, ex.getMessage()));
			}
		}
		out.println("applied " + p.getFileName() + ": " + changed + " changed, " + same +
			" unchanged, " + errors + " errors");
	}

	private void cmdRename(String target, String newName, String kind) throws Exception {
		Address a = resolve(target);
		Function f = currentProgram.getFunctionManager().getFunctionAt(a);
		if (kind == null) {
			kind = f != null ? "func" : "data";
		}
		if (!kind.equals("func") && !kind.equals("data")) {
			throw new IllegalArgumentException("kind must be func or data");
		}
		Path p = symPath(null);
		SymFile sf = readSyms(p);
		Entry e = sf.entries.get(a.getOffset());
		if (e == null) {
			e = new Entry();
			e.addr = a.getOffset();
			sf.entries.put(e.addr, e);
		}
		e.kind = kind;
		e.name = newName;
		applyEntry(e);
		writeSyms(p, sf);
		out.println(e.format());
	}

	private void cmdExport(String file) throws Exception {
		Path p = symPath(file);
		SymFile sf = readSyms(p);
		int added = 0, updated = 0;
		SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
		while (it.hasNext()) {
			Symbol s = it.next();
			if (s.getSource() != SourceType.USER_DEFINED || s.isExternal() || !s.isPrimary() ||
				!s.getParentNamespace().isGlobal()) {
				continue;
			}
			String kind;
			String sig = null;
			if (s.getSymbolType() == SymbolType.FUNCTION) {
				kind = "func";
				Function f = (Function) s.getObject();
				if (f.getSignatureSource() == SourceType.USER_DEFINED) {
					sig = f.getPrototypeString(false, !defaultCC().equals(f.getCallingConventionName()));
				}
			}
			else if (s.getSymbolType() == SymbolType.LABEL) {
				kind = "data";
			}
			else {
				continue;
			}
			long off = s.getAddress().getOffset();
			Entry e = sf.entries.get(off);
			if (e == null) {
				e = new Entry();
				e.addr = off;
				e.kind = kind;
				e.name = s.getName();
				e.sig = sig;
				sf.entries.put(off, e);
				added++;
			}
			else if (!e.name.equals(s.getName()) || !e.kind.equals(kind) ||
				(sig != null && !sig.equals(e.sig))) {
				e.name = s.getName();
				e.kind = kind;
				if (sig != null) {
					e.sig = sig;
				}
				updated++;
			}
		}
		writeSyms(p, sf);
		out.println("exported to " + p + ": " + added + " added, " + updated + " updated, " +
			sf.entries.size() + " total");
	}
}

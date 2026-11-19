#include "decompiler/CilDecompiler.h"
#include "core/BinaryReader.h"

#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace coconut::decompiler {

namespace {

enum class OpArg { None, I8, U8, I32, I64, F32, F64, BrS, Br, Token, Switch, U16 };

struct OpDesc {
    const char* name;
    OpArg arg;
};

OpDesc LookupSingleOp(u8 op) {
    switch (op) {
        case 0x00: return {"nop", OpArg::None};
        case 0x01: return {"break", OpArg::None};
        case 0x02: return {"ldarg.0", OpArg::None};
        case 0x03: return {"ldarg.1", OpArg::None};
        case 0x04: return {"ldarg.2", OpArg::None};
        case 0x05: return {"ldarg.3", OpArg::None};
        case 0x06: return {"ldloc.0", OpArg::None};
        case 0x07: return {"ldloc.1", OpArg::None};
        case 0x08: return {"ldloc.2", OpArg::None};
        case 0x09: return {"ldloc.3", OpArg::None};
        case 0x0A: return {"stloc.0", OpArg::None};
        case 0x0B: return {"stloc.1", OpArg::None};
        case 0x0C: return {"stloc.2", OpArg::None};
        case 0x0D: return {"stloc.3", OpArg::None};
        case 0x0E: return {"ldarg.s", OpArg::U8};
        case 0x0F: return {"ldarga.s", OpArg::U8};
        case 0x10: return {"starg.s", OpArg::U8};
        case 0x11: return {"ldloc.s", OpArg::U8};
        case 0x12: return {"ldloca.s", OpArg::U8};
        case 0x13: return {"stloc.s", OpArg::U8};
        case 0x14: return {"ldnull", OpArg::None};
        case 0x15: return {"ldc.i4.m1", OpArg::None};
        case 0x16: return {"ldc.i4.0", OpArg::None};
        case 0x17: return {"ldc.i4.1", OpArg::None};
        case 0x18: return {"ldc.i4.2", OpArg::None};
        case 0x19: return {"ldc.i4.3", OpArg::None};
        case 0x1A: return {"ldc.i4.4", OpArg::None};
        case 0x1B: return {"ldc.i4.5", OpArg::None};
        case 0x1C: return {"ldc.i4.6", OpArg::None};
        case 0x1D: return {"ldc.i4.7", OpArg::None};
        case 0x1E: return {"ldc.i4.8", OpArg::None};
        case 0x1F: return {"ldc.i4.s", OpArg::I8};
        case 0x20: return {"ldc.i4", OpArg::I32};
        case 0x21: return {"ldc.i8", OpArg::I64};
        case 0x22: return {"ldc.r4", OpArg::F32};
        case 0x23: return {"ldc.r8", OpArg::F64};
        case 0x25: return {"dup", OpArg::None};
        case 0x26: return {"pop", OpArg::None};
        case 0x28: return {"call", OpArg::Token};
        case 0x2A: return {"ret", OpArg::None};
        case 0x2B: return {"br.s", OpArg::BrS};
        case 0x2C: return {"brfalse.s", OpArg::BrS};
        case 0x2D: return {"brtrue.s", OpArg::BrS};
        case 0x2E: return {"beq.s", OpArg::BrS};
        case 0x2F: return {"bge.s", OpArg::BrS};
        case 0x30: return {"bgt.s", OpArg::BrS};
        case 0x31: return {"ble.s", OpArg::BrS};
        case 0x32: return {"blt.s", OpArg::BrS};
        case 0x33: return {"bne.un.s", OpArg::BrS};
        case 0x38: return {"br", OpArg::Br};
        case 0x39: return {"brfalse", OpArg::Br};
        case 0x3A: return {"brtrue", OpArg::Br};
        case 0x3B: return {"beq", OpArg::Br};
        case 0x3C: return {"bge", OpArg::Br};
        case 0x3D: return {"bgt", OpArg::Br};
        case 0x3E: return {"ble", OpArg::Br};
        case 0x3F: return {"blt", OpArg::Br};
        case 0x40: return {"bne.un", OpArg::Br};
        case 0x45: return {"switch", OpArg::Switch};
        case 0x58: return {"add", OpArg::None};
        case 0x59: return {"sub", OpArg::None};
        case 0x5A: return {"mul", OpArg::None};
        case 0x5B: return {"div", OpArg::None};
        case 0x5D: return {"rem", OpArg::None};
        case 0x5F: return {"and", OpArg::None};
        case 0x60: return {"or", OpArg::None};
        case 0x61: return {"xor", OpArg::None};
        case 0x62: return {"shl", OpArg::None};
        case 0x63: return {"shr", OpArg::None};
        case 0x65: return {"neg", OpArg::None};
        case 0x66: return {"not", OpArg::None};
        case 0x69: return {"conv.i4", OpArg::None};
        case 0x6A: return {"conv.i8", OpArg::None};
        case 0x6B: return {"conv.r4", OpArg::None};
        case 0x6C: return {"conv.r8", OpArg::None};
        case 0x6F: return {"callvirt", OpArg::Token};
        case 0x72: return {"ldstr", OpArg::Token};
        case 0x73: return {"newobj", OpArg::Token};
        case 0x74: return {"castclass", OpArg::Token};
        case 0x75: return {"isinst", OpArg::Token};
        case 0x7A: return {"throw", OpArg::None};
        case 0x7B: return {"ldfld", OpArg::Token};
        case 0x7C: return {"ldflda", OpArg::Token};
        case 0x7D: return {"stfld", OpArg::Token};
        case 0x7E: return {"ldsfld", OpArg::Token};
        case 0x80: return {"stsfld", OpArg::Token};
        case 0x8C: return {"box", OpArg::Token};
        case 0x8D: return {"newarr", OpArg::Token};
        case 0x8E: return {"ldlen", OpArg::None};
        case 0xD0: return {"ldtoken", OpArg::Token};
        case 0xDC: return {"endfinally", OpArg::None};
        case 0xDD: return {"leave", OpArg::Br};
        case 0xDE: return {"leave.s", OpArg::BrS};
        default:   return {"unk", OpArg::None};
    }
}

OpDesc LookupExtendedOp(u8 op2) {
    switch (op2) {
        case 0x01: return {"ceq", OpArg::None};
        case 0x02: return {"cgt", OpArg::None};
        case 0x03: return {"cgt.un", OpArg::None};
        case 0x04: return {"clt", OpArg::None};
        case 0x05: return {"clt.un", OpArg::None};
        case 0x06: return {"ldftn", OpArg::Token};
        case 0x09: return {"ldarg", OpArg::U16};
        case 0x0B: return {"starg", OpArg::U16};
        case 0x0C: return {"ldloc", OpArg::U16};
        case 0x0E: return {"stloc", OpArg::U16};
        case 0x15: return {"initobj", OpArg::Token};
        default:   return {"fe.unk", OpArg::None};
    }
}

std::string FormatILOffset(u32 off) {
    std::ostringstream oss;
    oss << "IL_" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << off;
    return oss.str();
}

std::string ShortMemberName(const std::string& resolved) {
    size_t dot = resolved.rfind('.');
    return dot == std::string::npos ? resolved : resolved.substr(dot + 1);
}

std::string ArgName(const MonoMethodInfo& method, u32 idx) {
    if (!method.isStatic) {
        if (idx == 0) return "this";
        size_t pi = idx - 1;
        if (pi < method.params.size()) return method.params[pi].name;
        return "arg" + std::to_string(pi);
    }
    if (idx < method.params.size()) return method.params[idx].name;
    return "arg" + std::to_string(idx);
}

} // namespace

std::vector<CilInstruction> CilDecompiler::Disassemble(const MonoAssembly& assembly, const MonoMethodInfo& method) {
    std::vector<CilInstruction> out;
    if (method.ilCode.empty()) return out;

    BinaryReader r(method.ilCode, Endian::Little);
    while (!r.IsEof() && !r.HasError()) {
        u32 startOff = static_cast<u32>(r.Position());
        u8 op = r.ReadU8();
        OpDesc desc = (op == 0xFE && !r.IsEof()) ? LookupExtendedOp(r.ReadU8()) : LookupSingleOp(op);

        CilInstruction insn;
        insn.offset = startOff;
        insn.mnemonic = desc.name;

        std::ostringstream opStr;
        switch (desc.arg) {
            case OpArg::None: break;
            case OpArg::I8:   opStr << static_cast<int>(r.ReadI8()); break;
            case OpArg::U8:   opStr << static_cast<unsigned>(r.ReadU8()); break;
            case OpArg::U16:  opStr << r.ReadU16(); break;
            case OpArg::I32:  opStr << r.ReadI32(); break;
            case OpArg::I64:  opStr << r.ReadI64(); break;
            case OpArg::F32:  opStr << r.ReadF32() << "f"; break;
            case OpArg::F64:  opStr << r.ReadF64(); break;
            case OpArg::BrS: {
                i8 rel = r.ReadI8();
                i32 target = static_cast<i32>(r.Position()) + rel;
                insn.branchTarget = target;
                opStr << FormatILOffset(static_cast<u32>(std::max(0, target)));
                break;
            }
            case OpArg::Br: {
                i32 rel = r.ReadI32();
                i32 target = static_cast<i32>(r.Position()) + rel;
                insn.branchTarget = target;
                opStr << FormatILOffset(static_cast<u32>(std::max(0, target)));
                break;
            }
            case OpArg::Token: {
                u32 tok = r.ReadU32();
                insn.tokenOperand = tok;
                opStr << assembly.ResolveToken(tok);
                break;
            }
            case OpArg::Switch: {
                u32 count = r.ReadU32();
                opStr << "(" << count << " targets)";
                r.Skip(static_cast<size_t>(count) * 4);
                break;
            }
        }

        size_t endOff = r.Position();
        insn.rawBytes.assign(
            method.ilCode.begin() + static_cast<std::ptrdiff_t>(startOff),
            method.ilCode.begin() + static_cast<std::ptrdiff_t>(endOff));
        insn.operandText = opStr.str();
        out.push_back(std::move(insn));
    }
    return out;
}

DecompiledMethodResult CilDecompiler::DecompileMethod(
    const MonoAssembly& assembly,
    const MonoTypeInfo& ownerType,
    const MonoMethodInfo& method,
    const ProjectDatabase* db) {
    DecompiledMethodResult res;
    res.instructions = Disassemble(assembly, method);

    std::string fullMethodSym = ownerType.FullName() + "::" + method.name;
    std::string displayMethodName = method.name;
    if (db) {
        if (auto ren = db->GetSymbolRename(fullMethodSym)) {
            displayMethodName = *ren;
        }
    }

    // 1. Build Ghidra-style IL Disassembly Listing
    std::ostringstream dis;
    dis << "; Method: " << fullMethodSym << " (RVA: 0x" << std::hex << method.rva << std::dec
        << ", Token: 0x" << std::hex << method.token << std::dec << ")\n";
    for (const auto& ins : res.instructions) {
        dis << FormatILOffset(ins.offset) << " | ";
        std::ostringstream hexCols;
        for (size_t i = 0; i < ins.rawBytes.size(); ++i) {
            if (i > 0) hexCols << ' ';
            hexCols << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                    << static_cast<int>(ins.rawBytes[i]);
        }
        std::string hexStr = hexCols.str();
        dis << std::left << std::setw(18) << std::setfill(' ') << hexStr << " | " << ins.mnemonic;
        if (!ins.operandText.empty()) {
            dis << " " << ins.operandText;
        }
        if (db) {
            if (auto cmt = db->GetComment(fullMethodSym + "@" + FormatILOffset(ins.offset))) {
                dis << " // " << *cmt;
            }
        }
        dis << '\n';
    }
    res.disassemblyListing = dis.str();

    // 2. Stack-simulation AST -> Structured C# Reconstruction + XRef Extraction
    std::vector<std::string> stack;
    std::vector<std::string> stmts;
    std::unordered_map<u32, int> closeIfAtOffset;
    auto pop = [&]() -> std::string {
        if (stack.empty()) return "/*stack*/";
        std::string v = std::move(stack.back());
        stack.pop_back();
        return v;
    };

    int indent = 2;
    auto pad = [&]() { return std::string(static_cast<size_t>(indent) * 4, ' '); };

    for (size_t i = 0; i < res.instructions.size(); ++i) {
        const auto& ins = res.instructions[i];

        auto closeIt = closeIfAtOffset.find(ins.offset);
        if (closeIt != closeIfAtOffset.end()) {
            for (int c = 0; c < closeIt->second; ++c) {
                indent = std::max(2, indent - 1);
                stmts.push_back(pad() + "}");
            }
            closeIfAtOffset.erase(closeIt);
        }

        const std::string& m = ins.mnemonic;
        if (m == "nop") continue;
        if (m == "ldarg.0") stack.push_back(ArgName(method, 0));
        else if (m == "ldarg.1") stack.push_back(ArgName(method, 1));
        else if (m == "ldarg.2") stack.push_back(ArgName(method, 2));
        else if (m == "ldarg.3") stack.push_back(ArgName(method, 3));
        else if (m == "ldarg.s" || m == "ldarg") stack.push_back(ArgName(method, static_cast<u32>(std::stoul(ins.operandText))));
        else if (m == "ldloc.0") stack.push_back("loc0");
        else if (m == "ldloc.1") stack.push_back("loc1");
        else if (m == "ldloc.2") stack.push_back("loc2");
        else if (m == "ldloc.3") stack.push_back("loc3");
        else if (m == "ldloc.s" || m == "ldloc") stack.push_back("loc" + ins.operandText);
        else if (m == "stloc.0" || m == "stloc.1" || m == "stloc.2" || m == "stloc.3") {
            std::string loc = "loc" + m.substr(6);
            stmts.push_back(pad() + "var " + loc + " = " + pop() + ";");
        } else if (m == "stloc.s" || m == "stloc") {
            stmts.push_back(pad() + "var loc" + ins.operandText + " = " + pop() + ";");
        } else if (m == "ldnull") stack.push_back("null");
        else if (m == "ldc.i4.m1") stack.push_back("-1");
        else if (m.rfind("ldc.i4.", 0) == 0 && m.size() == 8 && std::isdigit(static_cast<unsigned char>(m[7]))) {
            stack.push_back(m.substr(7));
        } else if (m == "ldc.i4.s" || m == "ldc.i4" || m == "ldc.i8" || m == "ldc.r4" || m == "ldc.r8") {
            stack.push_back(ins.operandText);
        } else if (m == "ldstr") {
            stack.push_back(ins.operandText);
            res.xrefs.push_back({fullMethodSym, ins.offset, ins.operandText, "STRING"});
        } else if (m == "ldfld") {
            std::string obj = pop();
            std::string field = ShortMemberName(ins.operandText);
            stack.push_back(obj + "." + field);
            res.xrefs.push_back({fullMethodSym, ins.offset, ins.operandText, "READ"});
        } else if (m == "ldsfld") {
            stack.push_back(ins.operandText);
            res.xrefs.push_back({fullMethodSym, ins.offset, ins.operandText, "READ"});
        } else if (m == "stfld") {
            std::string val = pop();
            std::string obj = pop();
            std::string field = ShortMemberName(ins.operandText);
            // Convert bool literals cleanly for known bool fields
            for (const auto& f : ownerType.fields) {
                if (f.name == field && f.typeName == "bool") {
                    if (val == "1") val = "true";
                    else if (val == "0") val = "false";
                }
            }
            stmts.push_back(pad() + obj + "." + field + " = " + val + ";");
            res.xrefs.push_back({fullMethodSym, ins.offset, ins.operandText, "WRITE"});
        } else if (m == "stsfld") {
            std::string val = pop();
            stmts.push_back(pad() + ins.operandText + " = " + val + ";");
            res.xrefs.push_back({fullMethodSym, ins.offset, ins.operandText, "WRITE"});
        } else if (m == "add" || m == "sub" || m == "mul" || m == "div" || m == "rem" ||
                   m == "and" || m == "or" || m == "xor" || m == "ceq" || m == "cgt" || m == "clt") {
            std::string rhs = pop();
            std::string lhs = pop();
            const char* opSym = "+";
            if (m == "sub") opSym = "-";
            else if (m == "mul") opSym = "*";
            else if (m == "div") opSym = "/";
            else if (m == "rem") opSym = "%";
            else if (m == "and") opSym = "&";
            else if (m == "or")  opSym = "|";
            else if (m == "xor") opSym = "^";
            else if (m == "ceq") opSym = "==";
            else if (m == "cgt") opSym = ">";
            else if (m == "clt") opSym = "<";
            stack.push_back("(" + lhs + " " + opSym + " " + rhs + ")");
        } else if (m == "neg") {
            stack.push_back("(-" + pop() + ")");
        } else if (m == "dup") {
            if (!stack.empty()) stack.push_back(stack.back());
        } else if (m == "pop") {
            if (!stack.empty()) stmts.push_back(pad() + pop() + ";");
        } else if (m == "bgt.s" || m == "bgt" || m == "bge.s" || m == "bge" ||
                   m == "blt.s" || m == "blt" || m == "ble.s" || m == "ble" ||
                   m == "beq.s" || m == "beq" || m == "bne.un.s" || m == "bne.un") {
            // Invert jump condition to form structured `if (...) { ... }` block when branching forward!
            std::string rhs = pop();
            std::string lhs = pop();
            const char* invOp = "==";
            if (m.rfind("bgt", 0) == 0) invOp = "<=";
            else if (m.rfind("bge", 0) == 0) invOp = "<";
            else if (m.rfind("blt", 0) == 0) invOp = ">=";
            else if (m.rfind("ble", 0) == 0) invOp = ">";
            else if (m.rfind("beq", 0) == 0) invOp = "!=";
            else if (m.rfind("bne", 0) == 0) invOp = "==";

            if (ins.branchTarget > static_cast<i32>(ins.offset)) {
                stmts.push_back(pad() + "if (" + lhs + " " + invOp + " " + rhs + ")");
                stmts.push_back(pad() + "{");
                ++indent;
                closeIfAtOffset[static_cast<u32>(ins.branchTarget)]++;
            } else {
                stmts.push_back(pad() + "// branch to " + ins.operandText);
            }
        } else if (m == "brfalse.s" || m == "brfalse" || m == "brtrue.s" || m == "brtrue") {
            std::string cond = pop();
            bool isFalse = m.rfind("brfalse", 0) == 0;
            if (ins.branchTarget > static_cast<i32>(ins.offset)) {
                stmts.push_back(pad() + "if (" + (isFalse ? cond : ("!" + cond)) + ")");
                stmts.push_back(pad() + "{");
                ++indent;
                closeIfAtOffset[static_cast<u32>(ins.branchTarget)]++;
            }
        } else if (m == "call" || m == "callvirt" || m == "newobj") {
            res.xrefs.push_back({fullMethodSym, ins.offset, ins.operandText, "CALL"});
            // Pop available argument(s) heuristically for clean C# call expression
            std::string argStr;
            if (!stack.empty()) {
                argStr = pop();
            }
            if (m == "newobj") {
                stack.push_back("new " + ins.operandText + "(" + argStr + ")");
            } else {
                stmts.push_back(pad() + ins.operandText + "(" + argStr + ");");
            }
        } else if (m == "ret") {
            if (method.returnType != "void" && !stack.empty()) {
                stmts.push_back(pad() + "return " + pop() + ";");
            } else if (i + 1 < res.instructions.size()) {
                stmts.push_back(pad() + "return;");
            }
        }
    }

    while (indent > 2) {
        --indent;
        stmts.push_back(pad() + "}");
    }

    // Format complete C# method declaration
    std::ostringstream cs;
    cs << "        " << method.visibility << ' ';
    if (method.isStatic) cs << "static ";
    if (method.isAbstract) cs << "abstract ";
    else if (method.isVirtual) cs << "virtual ";
    cs << method.returnType << ' ' << displayMethodName << '(';
    for (size_t pi = 0; pi < method.params.size(); ++pi) {
        if (pi > 0) cs << ", ";
        cs << method.params[pi].typeName << ' ' << method.params[pi].name;
    }
    cs << ")\n        {\n";
    if (stmts.empty() && !method.ilCode.empty()) {
        cs << "            // Empty method body\n";
    } else if (method.ilCode.empty()) {
        cs << "            // External or abstract method (RVA 0x0)\n";
    } else {
        for (const auto& s : stmts) {
            cs << s << '\n';
        }
    }
    cs << "        }\n";
    res.decompiledCSharp = cs.str();
    return res;
}

std::string CilDecompiler::DecompileType(
    const MonoAssembly& assembly,
    const MonoTypeInfo& type,
    const ProjectDatabase* db,
    std::vector<CodeXRef>* outXRefs) {
    if (db) {
        if (auto overrideSrc = db->GetScriptOverride(type.FullName())) {
            return *overrideSrc;
        }
    }

    std::ostringstream cs;
    cs << "// Decompiled by Coconut from " << assembly.fileName << "\n";
    cs << "using System;\n";
    cs << "using UnityEngine;\n\n";

    bool hasNs = !type.nameSpace.empty();
    if (hasNs) {
        cs << "namespace " << type.nameSpace << "\n{\n";
    }

    std::string ind = hasNs ? "    " : "";
    cs << ind << type.visibility << ' ';
    if (type.isAbstract && type.kind == "class") cs << "abstract ";
    else if (type.isSealed && type.kind == "class") cs << "sealed ";
    cs << type.kind << ' ' << type.name;
    if (!type.baseType.empty() && type.baseType != "System.Object" &&
        type.baseType != "System.ValueType" && type.baseType != "System.Enum") {
        std::string shortBase = ShortMemberName(type.baseType);
        cs << " : " << shortBase;
    }
    cs << "\n" << ind << "{\n";

    for (const auto& f : type.fields) {
        cs << ind << "    " << f.visibility << ' ';
        if (f.isStatic) cs << "static ";
        if (f.isReadOnly) cs << "readonly ";
        cs << f.typeName << ' ' << f.name << ";\n";
    }
    if (!type.fields.empty() && !type.methods.empty()) cs << '\n';

    for (const auto& m : type.methods) {
        auto res = DecompileMethod(assembly, type, m, db);
        cs << res.decompiledCSharp << '\n';
        if (outXRefs) {
            outXRefs->insert(outXRefs->end(), res.xrefs.begin(), res.xrefs.end());
        }
    }

    cs << ind << "}\n";
    if (hasNs) {
        cs << "}\n";
    }
    return cs.str();
}

} // namespace coconut::decompiler

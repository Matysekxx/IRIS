const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "iris",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .link_libcpp = true,
        }),
    });

    exe.root_module.addIncludePath(b.path("."));
    exe.root_module.addIncludePath(b.path("asmjit"));

    const cpp_flags = [_][]const u8{
        "-std=c++20",
        "-DNDEBUG",
        "-O3",
        "-ffast-math",
        "-march=native",
        "-DASMJIT_STATIC",
        "-fno-sanitize=all",
    };

    const iris_sources = [_][]const u8{
        "src/main.cpp",
        "src/bytecode/Compiler.cpp",
        "src/bytecode/JITCompiler.cpp",
        "src/bytecode/JITHelpers.cpp",
        "src/bytecode/VM.cpp",
        "src/core/ArrayData.cpp",
        "src/core/Native.cpp",
        "src/core/Value.cpp",
        "src/device/Win32Driver.cpp",
        "src/execute/Executor.cpp",
        "src/log/Logger.cpp",
        "src/parser/NodeFactory.cpp",
        "src/parser/Parser.cpp",
    };

    const asmjit_sources = [_][]const u8{
        "asmjit/asmjit/arm/a64assembler.cpp",
        "asmjit/asmjit/arm/a64builder.cpp",
        "asmjit/asmjit/arm/a64compiler.cpp",
        "asmjit/asmjit/arm/a64emithelper.cpp",
        "asmjit/asmjit/arm/a64formatter.cpp",
        "asmjit/asmjit/arm/a64func.cpp",
        "asmjit/asmjit/arm/a64instapi.cpp",
        "asmjit/asmjit/arm/a64instdb.cpp",
        "asmjit/asmjit/arm/a64operand.cpp",
        "asmjit/asmjit/arm/a64rapass.cpp",
        "asmjit/asmjit/arm/armformatter.cpp",
        "asmjit/asmjit/core/archtraits.cpp",
        "asmjit/asmjit/core/assembler.cpp",
        "asmjit/asmjit/core/builder.cpp",
        "asmjit/asmjit/core/codeholder.cpp",
        "asmjit/asmjit/core/codewriter.cpp",
        "asmjit/asmjit/core/compiler.cpp",
        "asmjit/asmjit/core/constpool.cpp",
        "asmjit/asmjit/core/cpuinfo.cpp",
        "asmjit/asmjit/core/emithelper.cpp",
        "asmjit/asmjit/core/emitter.cpp",
        "asmjit/asmjit/core/emitterutils.cpp",
        "asmjit/asmjit/core/environment.cpp",
        "asmjit/asmjit/core/errorhandler.cpp",
        "asmjit/asmjit/core/formatter.cpp",
        "asmjit/asmjit/core/func.cpp",
        "asmjit/asmjit/core/funcargscontext.cpp",
        "asmjit/asmjit/core/globals.cpp",
        "asmjit/asmjit/core/inst.cpp",
        "asmjit/asmjit/core/instdb.cpp",
        "asmjit/asmjit/core/jitallocator.cpp",
        "asmjit/asmjit/core/jitruntime.cpp",
        "asmjit/asmjit/core/logger.cpp",
        "asmjit/asmjit/core/operand.cpp",
        "asmjit/asmjit/core/osutils.cpp",
        "asmjit/asmjit/core/ralocal.cpp",
        "asmjit/asmjit/core/rapass.cpp",
        "asmjit/asmjit/core/rastack.cpp",
        "asmjit/asmjit/core/string.cpp",
        "asmjit/asmjit/core/target.cpp",
        "asmjit/asmjit/core/type.cpp",
        "asmjit/asmjit/core/virtmem.cpp",
        "asmjit/asmjit/support/arena.cpp",
        "asmjit/asmjit/support/arenabitset.cpp",
        "asmjit/asmjit/support/arenahash.cpp",
        "asmjit/asmjit/support/arenalist.cpp",
        "asmjit/asmjit/support/arenatree.cpp",
        "asmjit/asmjit/support/arenavector.cpp",
        "asmjit/asmjit/support/support.cpp",
        "asmjit/asmjit/ujit/unicompiler_a64.cpp",
        "asmjit/asmjit/ujit/unicompiler_x86.cpp",
        "asmjit/asmjit/ujit/vecconsttable.cpp",
        "asmjit/asmjit/x86/x86assembler.cpp",
        "asmjit/asmjit/x86/x86builder.cpp",
        "asmjit/asmjit/x86/x86compiler.cpp",
        "asmjit/asmjit/x86/x86emithelper.cpp",
        "asmjit/asmjit/x86/x86formatter.cpp",
        "asmjit/asmjit/x86/x86func.cpp",
        "asmjit/asmjit/x86/x86instapi.cpp",
        "asmjit/asmjit/x86/x86instdb.cpp",
        "asmjit/asmjit/x86/x86operand.cpp",
        "asmjit/asmjit/x86/x86rapass.cpp"
    };

    exe.root_module.addCSourceFiles(.{
        .files = &iris_sources,
        .flags = &cpp_flags,
    });

    exe.root_module.addCSourceFiles(.{
        .files = &asmjit_sources,
        .flags = &cpp_flags,
    });

    if (target.result.os.tag == .windows) {
        exe.root_module.linkSystemLibrary("user32", .{});
    }

    b.installArtifact(exe);
}

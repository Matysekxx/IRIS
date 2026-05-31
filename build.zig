const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "iris",
        .target = target,
        .optimize = optimize,
    });

    exe.addIncludePath(b.path("."));
    exe.addIncludePath(b.path("asmjit/src"));

    const cpp_flags = [_][]const u8{
        "-std=c++20",
        "-DNDEBUG",
        "-march=native",
    };

    const sources = [_][]const u8{
        "lang/main.cpp",
        "lang/bytecode/Compiler.cpp",
        "lang/bytecode/JITCompiler.cpp",
        "lang/bytecode/JITHelpers.cpp",
        "lang/bytecode/VM.cpp",
        "lang/core/ArrayData.cpp",
        "lang/core/Native.cpp",
        "lang/core/Value.cpp",
        "lang/device/Win32Driver.cpp",
        "lang/execute/Executor.cpp",
        "lang/log/Logger.cpp",
        "lang/parser/NodeFactory.cpp",
        "lang/parser/Parser.cpp",
    };

    exe.addCSourceFiles(.{
        .files = &sources,
        .flags = &cpp_flags,
    });

    exe.linkLibC();
    exe.linkLibCpp();
    
    if (target.result.os.tag == .windows) {
        exe.linkSystemLibrary("user32");
    }

    b.installArtifact(exe);
}

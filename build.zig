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

    // Add all .cpp files
    exe.addCSourceFiles(.{
        .files = &.{
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
        },
        .flags = &cpp_flags,
    });

    // Add asmjit (as a static library or directly)
    // For simplicity, we can just add its sources or use its cmake output
    // But since we want "zig cc" to handle it, let's try to add it properly
    
    exe.linkLibC();
    exe.linkLibCpp();
    
    // On Windows, link user32
    if (target.result.os.tag == .windows) {
        exe.linkSystemLibrary("user32");
    }

    b.installArtifact(exe);
}

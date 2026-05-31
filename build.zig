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
        "-march=native",
        "-DASMJIT_STATIC",
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

    exe.root_module.addCSourceFiles(.{
        .files = &sources,
        .flags = &cpp_flags,
    });

    const lib_path = if (optimize == .Debug)
        "build/asmjit/Debug/asmjit.lib"
    else
        "build/asmjit/Release/asmjit.lib";
    exe.root_module.addObjectFile(b.path(lib_path));

    if (target.result.os.tag == .windows) {
        exe.root_module.linkSystemLibrary("user32", .{});
    }

    b.installArtifact(exe);
}

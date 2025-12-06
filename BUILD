load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load(":refresh_compile_commands.bzl", "refresh_compile_commands")

# See README.md for interface.

# But if you aren't doing any cross-compiling for other platforms, the following can be a good default:
# bazel run @hedron_compile_commands//:refresh_all
refresh_compile_commands(
    name = "refresh_all",
)


# Stardoc users only: Depend on "@hedron_compile_commands//:bzl_srcs_for_stardoc" as needed.
# Why? Stardoc requires all loaded files to be listed as deps; without this we'd prevent users from running Stardoc on their code when they load from this tool in, e.g., their own workspace.bzl or wrapping macros.
filegroup(
    name = "bzl_srcs_for_stardoc",
    visibility = ["//visibility:public"],
    srcs = glob(["**/*.bzl"]) + [
        "@bazel_tools//tools:bzl_srcs",
    ],
)



########################################
# Implementation:
# If you are looking into the implementation, start with the overview in ImplementationReadme.md.

exports_files([
    "refresh.cc",
])

cc_library(
    name = "refresh_lib",
    srcs = ["json_utils.cc", "subprocess.cc"],
    hdrs = ["json_utils.h", "subprocess.h", "refresh.h"],
    strip_include_prefix = ".",
    visibility = ["//visibility:public"],
    copts = select({
        "@bazel_tools//src/conditions:windows": ["/std:c++17"],
        "//conditions:default": ["-std=c++17"],
    }),
)

cc_binary(
    name = "nvcc_clang_diff",
    srcs = ["nvcc_clang_diff.cc"],
    deps = [":refresh_lib"],
    copts = select({
        "@bazel_tools//src/conditions:windows": ["/std:c++17"],
        "//conditions:default": ["-std=c++17"],
    }),
)

cc_binary(
    name = "print_args",
    srcs = ["print_args.cpp"],
    visibility = ["//visibility:public"],
)
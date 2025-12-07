load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "main",
    srcs = ["main.cc"],
)

cc_library(
    name = "lib1",
    srcs = ["lib1.cc"],
    hdrs = ["lib1.h"],
)

cc_library(
    name = "lib2",
    srcs = ["lib2.cc"],
    hdrs = ["lib2.h"],
    deps = [":lib1"],
)

# Target with error to test robustness

cc_library(
    name = "error_target",
    srcs = ["error.cc"],
    copts = ["-Xclang"],  # This flag causes D9002 warning on MSVC
)

refresh_compile_commands(
    name = "refresh_my_targets",
    targets = {
        "//...": "",
    },
)

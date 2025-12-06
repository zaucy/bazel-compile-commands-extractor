load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")
load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "main",
    srcs = ["main.cc"],
)

# We can also define a local refresh target to test specific targets
refresh_compile_commands(
    name = "refresh_my_targets",
    targets = {
        "//:main": "",
    },
)

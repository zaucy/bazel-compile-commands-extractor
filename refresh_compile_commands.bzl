""" refresh_compile_commands rule

When `bazel run`, these rules refresh the compile_commands.json in the root of your Bazel workspace
    [creating compile_commands.json if it doesn't already exist.]
"""

load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")
load("@rules_cc//cc:defs.bzl", "cc_binary")

def refresh_compile_commands(
        name,
        targets = None,
        exclude_headers = None,
        exclude_external_sources = False,
        **kwargs):
    if not targets:
        targets = {"@//...": ""}
    elif type(targets) == "select":
        pass
    elif type(targets) == "list":
        targets = {target: "" for target in targets}
    elif type(targets) != "dict":
        targets = {targets: ""}

    targets = {
        target if target.startswith("/") or target.startswith("@") else "{}//{}:{}".format(native.repository_name(), native.package_name(), target.removeprefix(":")): flags
        for target, flags in targets.items()
    }

    config_name = name + "_config"
    config_file = config_name + ".cc"

    _gen_refresh_config(
        name = config_name,
        out = config_file,
        labels_to_flags = targets,
        exclude_headers = exclude_headers,
        exclude_external_sources = exclude_external_sources,
        **kwargs
    )

    cc_binary(
        name = name,
        srcs = [
            "@hedron_compile_commands//:refresh.cc",
            config_file,
        ],
        deps = [
            "@hedron_compile_commands//:refresh_lib",
        ],
        data = ["@hedron_compile_commands//:print_args"],
        copts = select({
            "@bazel_tools//src/conditions:windows": ["/std:c++17"],
            "//conditions:default": ["-std=c++17"],
        }),
        **kwargs
    )

def _gen_refresh_config_impl(ctx):
    toolchain_includes = find_cpp_toolchain(ctx).built_in_include_directories

    content = '#include "refresh.h"\n'
    content += "#include <vector>\n"
    content += "#include <string>\n"
    content += "#include <utility>\n\n"

    content += "const std::vector<std::string> windows_default_include_paths_vec = {\n"
    for path in toolchain_includes:
        content += '    "{}",\n'.format(path.replace("\\", "\\\\"))
    content += "};\n"
    content += "const std::vector<std::string>& RefreshConfig::GetWindowsDefaultIncludePaths() { return windows_default_include_paths_vec; }\n\n"

    content += 'const std::string exclude_headers_str = "{}";\n'.format(ctx.attr.exclude_headers or "")
    content += "const std::string& RefreshConfig::GetExcludeHeaders() { return exclude_headers_str; }\n\n"

    content += "bool exclude_external_sources_val = {};\n".format("true" if ctx.attr.exclude_external_sources else "false")
    content += "bool RefreshConfig::GetExcludeExternalSources() { return exclude_external_sources_val; }\n\n"

    content += "const std::vector<std::pair<std::string, std::string>> target_flag_pairs_vec = {\n"
    for target, flags in ctx.attr.labels_to_flags.items():
        content += '    {{"{}", "{}"}},\n'.format(target, flags)
    content += "};\n"
    content += "const std::vector<std::pair<std::string, std::string>>& RefreshConfig::GetTargetFlagPairs() { return target_flag_pairs_vec; }\n\n"

    content += 'const std::string print_args_executable_str = "{}";\n'.format(ctx.executable._print_args_executable.path.replace("\\", "/"))
    content += "const std::string& RefreshConfig::GetPrintArgsExecutable() { return print_args_executable_str; }\n"

    ctx.actions.write(output = ctx.outputs.out, content = content)

_gen_refresh_config = rule(
    attrs = {
        "out": attr.output(mandatory = True),
        "labels_to_flags": attr.string_dict(mandatory = True),
        "exclude_external_sources": attr.bool(default = False),
        "exclude_headers": attr.string(values = ["all", "external", ""]),
        "_print_args_executable": attr.label(executable = True, cfg = "target", default = "//:print_args"),
        "_cc_toolchain": attr.label(default = "@bazel_tools//tools/cpp:current_cc_toolchain"),
    },
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
    implementation = _gen_refresh_config_impl,
)

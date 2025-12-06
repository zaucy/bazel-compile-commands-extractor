#include "subprocess.h"
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <sstream>
#include <filesystem>

// nvcc_clang_diff.cc
// Reimplementation of nvcc_clang_diff.py in C++

struct Flag {
    std::string long_name;
    std::string short_name;
    bool has_args;

    bool operator<(const Flag& other) const {
        if (long_name != other.long_name) return long_name < other.long_name;
        return short_name < other.short_name;
    }
};

std::string flag_key(const std::string& flag) {
    auto pos = flag.find('=');
    if (pos != std::string::npos) {
        return flag.substr(0, pos);
    }
    return flag;
}

std::vector<Flag> get_nvcc_flags() {
    // Find nvcc
    std::string nvcc = "nvcc"; // Assume in path
    // In C++ we might want to check standard paths like /usr/local/cuda/bin/nvcc if not found? 
    // For simplicity, just assume it's in PATH or standard location.
    
    std::string help_output;
    try {
        auto result = subprocess::Run({nvcc, "--help"});
        help_output = result.stdout_output + result.stderr_output;
    } catch (...) {
        // Fallback
        try {
             auto result = subprocess::Run({"/usr/local/cuda/bin/nvcc", "--help"});
             help_output = result.stdout_output + result.stderr_output;
        } catch (...) {
            std::cerr << "Could not find nvcc" << std::endl;
            return {};
        }
    }

    std::vector<Flag> flags;
    std::stringstream ss(help_output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.find("--") != 0) continue;
        
        std::stringstream ls(line);
        std::string long_arg;
        ls >> long_arg;
        
        // Find short arg
        std::string short_arg;
        std::string temp;
        while (ls >> temp) short_arg = temp; // Last token
        
        if (short_arg.size() >= 2 && short_arg.front() == '(' && short_arg.back() == ')') {
            short_arg = short_arg.substr(1, short_arg.size() - 2);
        }
        
        // Check if has args. Python: len(line_parts) > 2
        // We tokenized line. Let's count tokens.
        std::stringstream ls2(line);
        int count = 0;
        while (ls2 >> temp) count++;
        
        flags.push_back({long_arg, short_arg, count > 2});
    }
    return flags;
}

std::set<std::string> get_clang_flags() {
    std::string clang = "clang";
    std::string help_output;
    try {
        auto result = subprocess::Run({clang, "--help"});
        help_output = result.stdout_output;
    } catch (...) {
         try {
             auto result = subprocess::Run({"/usr/bin/clang", "--help"});
             help_output = result.stdout_output;
        } catch (...) {
            std::cerr << "Could not find clang" << std::endl;
            return {};
        }
    }

    std::set<std::string> flags;
    std::stringstream ss(help_output);
    std::string token;
    while (ss >> token) {
        if (token.find("-") == 0) {
            flags.insert(flag_key(token));
        }
    }
    
    std::vector<std::string> manual = {
        "-Wreorder", "-Wno-deprecated-declarations", "-Werror", "-O", "--help", "-l", "-m64", "--shared", "-shared"
    };
    for (const auto& f : manual) flags.insert(f);
    
    return flags;
}

int main() {
    auto nvcc_flags = get_nvcc_flags();
    auto clang_flags = get_clang_flags();

    std::vector<Flag> nvcc_flags_no_arg;
    std::vector<Flag> nvcc_flags_with_arg;
    std::map<std::string, std::string> nvcc_rewrite_flags;

    for (const auto& nvcc_flag : nvcc_flags) {
        if (clang_flags.count(nvcc_flag.long_name) && clang_flags.count(nvcc_flag.short_name)) continue;

        if (clang_flags.count(nvcc_flag.short_name)) {
            nvcc_rewrite_flags[nvcc_flag.long_name] = nvcc_flag.short_name;
            continue;
        }
        if (clang_flags.count(nvcc_flag.long_name)) {
            nvcc_rewrite_flags[nvcc_flag.short_name] = nvcc_flag.long_name;
            continue;
        }

        if (nvcc_flag.has_args) {
            nvcc_flags_with_arg.push_back(nvcc_flag);
        } else {
            nvcc_flags_no_arg.push_back(nvcc_flag);
        }
    }

    std::sort(nvcc_flags_no_arg.begin(), nvcc_flags_no_arg.end());
    std::sort(nvcc_flags_with_arg.begin(), nvcc_flags_with_arg.end());

    std::cout << "namespace nvcc_diff {\n";
    
    std::cout << "const char* _nvcc_flags_to_skip_no_arg[] = {\n";
    std::cout << "    // long name, short name\n";
    for (const auto& f : nvcc_flags_no_arg) {
        std::cout << "    \"" << f.long_name << "\", \"" << f.short_name << "\",\n";
    }
    std::cout << "    nullptr\n";
    std::cout << "};\n\n";

    std::cout << "const char* _nvcc_flags_to_skip_with_arg[] = {\n";
    std::cout << "    // long name, short name\n";
    for (const auto& f : nvcc_flags_with_arg) {
        std::cout << "    \"" << f.long_name << "\", \"" << f.short_name << "\",\n";
    }
    std::cout << "    nullptr\n";
    std::cout << "};\n\n";

    std::cout << "struct Pair { const char* from; const char* to; };\n";
    std::cout << "const Pair _nvcc_rewrite_flags[] = {\n";
    for (const auto& kv : nvcc_rewrite_flags) {
         std::cout << "    {\"" << kv.first << "\", \"" << kv.second << "\"},\n";
    }
    std::cout << "    {nullptr, nullptr}\n";
    std::cout << "};\n";
    
    std::cout << "} // namespace nvcc_diff\n";

    return 0;
}

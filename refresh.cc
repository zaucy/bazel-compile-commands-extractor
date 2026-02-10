#include "refresh.h"
#include "json_utils.h"
#include "subprocess.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <algorithm>
#include <regex>
#include <filesystem>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <future>
#include <queue>
#include <chrono>
#include <functional>
#include <atomic>
#include <iomanip>

namespace fs = std::filesystem;

class StatusManager {
public:
    static void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        ClearLines();
        std::cerr << msg << "\n";
        PrintLines();
    }

    static void SetStatus(size_t id, const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        ClearLines();
        if (msg.empty()) {
            statuses_.erase(id);
        } else {
            statuses_[id] = msg;
        }
        PrintLines();
    }

private:
    static void ClearLines() {
        if (last_line_count_ > 0) {
             for (size_t i = 0; i < last_line_count_; ++i) {
                 std::cerr << "\33[A"; // Up
                 std::cerr << "\33[2K\r"; // Clear
             }
        }
    }

    static void PrintLines() {
        last_line_count_ = 0;
        for (const auto& kv : statuses_) {
            std::cerr << kv.second << "\n";
            last_line_count_++;
        }
        std::cerr << std::flush;
    }

    static std::mutex mutex_;
    static std::map<size_t, std::string> statuses_;
    static size_t last_line_count_;
};

std::mutex StatusManager::mutex_;
std::map<size_t, std::string> StatusManager::statuses_;
size_t StatusManager::last_line_count_ = 0;

bool g_disable_cache = false;

// --- Constants & Globals ---

// Hardcoded NVCC flags from refresh.template.py
const std::set<std::string> _nvcc_flags_to_skip_no_arg = {
    "--Wdefault-stream-launch", "-Wdefault-stream-launch",
    "--Wext-lambda-captures-this", "-Wext-lambda-captures-this",
    "--Wmissing-launch-bounds", "-Wmissing-launch-bounds",
    "--Wno-deprecated-gpu-targets", "-Wno-deprecated-gpu-targets",
    "--allow-unsupported-compiler", "-allow-unsupported-compiler",
    "--augment-host-linker-script", "-aug-hls",
    "--clean-targets", "-clean",
    "--compile-as-tools-patch", "-astoolspatch",
    "--cubin", "-cubin",
    "--cuda", "-cuda",
    "--device-c", "-dc",
    "--device-link", "-dlink",
    "--device-w", "-dw",
    "--display-error-number", "-err-no",
    "--dlink-time-opt", "-dlto",
    "--dont-use-profile", "-noprof",
    "--dryrun", "-dryrun",
    "--expt-extended-lambda", "-expt-extended-lambda",
    "--expt-relaxed-constexpr", "-expt-relaxed-constexpr",
    "--extended-lambda", "-extended-lambda",
    "--extensible-whole-program", "-ewp",
    "--extra-device-vectorization", "-extra-device-vectorization",
    "--fatbin", "-fatbin",
    "--forward-unknown-opts", "-forward-unknown-opts",
    "--forward-unknown-to-host-compiler", "-forward-unknown-to-host-compiler",
    "--forward-unknown-to-host-linker", "-forward-unknown-to-host-linker",
    "--gen-opt-lto", "-gen-opt-lto",
    "--generate-line-info", "-lineinfo",
    "--host-relocatable-link", "-r",
    "--keep", "-keep",
    "--keep-device-functions", "-keep-device-functions",
    "--lib", "-lib",
    "--link", "-link",
    "--list-gpu-arch", "-arch-ls",
    "--list-gpu-code", "-code-ls",
    "--lto", "-lto",
    "--no-align-double", "--no-align-double",
    "--no-compress", "-no-compress",
    "--no-device-link", "-nodlink",
    "--no-display-error-number", "-no-err-no",
    "--no-exceptions", "-noeh",
    "--no-host-device-initializer-list", "-nohdinitlist",
    "--no-host-device-move-forward", "-nohdmoveforward",
    "--objdir-as-tempdir", "-objtemp",
    "--optix-ir", "-optix-ir",
    "--ptx", "-ptx",
    "--qpp-config", "-qpp-config",
    "--resource-usage", "-res-usage",
    "--restrict", "-restrict",
    "--run", "-run",
    "--source-in-ptx", "-src-in-ptx",
    "--use-local-env", "-use-local-env",
    "--use_fast_math", "-use_fast_math",
};

const std::set<std::string> _nvcc_flags_to_skip_with_arg = {
    "--archive-options", "-Xarchive",
    "--archiver-binary", "-arbin",
    "--brief-diagnostics", "-brief-diag",
    "--compiler-bindir", "-ccbin",
    "--compiler-options", "-Xcompiler",
    "--cudadevrt", "-cudadevrt",
    "--cudart", "-cudart",
    "--default-stream", "-default-stream",
    "--dependency-drive-prefix", "-ddp",
    "--diag-error", "-diag-error",
    "--diag-suppress", "-diag-suppress",
    "--diag-warn", "-diag-warn",
    "--dopt", "-dopt",
    "--drive-prefix", "-dp",
    "--entries", "-e",
    "--fmad", "-fmad",
    "--ftemplate-backtrace-limit", "-ftemplate-backtrace-limit",
    "--ftemplate-depth", "-ftemplate-depth",
    "--ftz", "-ftz",
    "--generate-code", "-gencode",
    "--gpu-code", "-code",
    "--host-linker-script", "-hls",
    "--input-drive-prefix", "-idp",
    "--keep-dir", "-keep-dir",
    "--libdevice-directory", "-ldir",
    "--machine", "-m",
    "--maxrregcount", "-maxrregcount",
    "--nvlink-options", "-Xnvlink",
    "--optimization-info", "-opt-info",
    "--options-file", "-optf",
    "--output-directory", "-odir",
    "--prec-div", "-prec-div",
    "--prec-sqrt", "-prec-sqrt",
    "--ptxas-options", "-Xptxas",
    "--relocatable-device-code", "-rdc",
    "--run-args", "-run-args",
    "--split-compile", "-split-compile",
    "--target-directory", "-target-dir",
    "--threads", "-t",
    "--version-ident", "-dQ",
};

const std::map<std::string, std::string> _nvcc_rewrite_flags = {
    {"--Werror", "-Werror"},
    {"--Wno-deprecated-declarations", "-Wno-deprecated-declarations"},
    {"--Wreorder", "-Wreorder"},
    {"--compile", "-c"},
    {"--debug", "-g"},
    {"--define-macro", "-D"},
    {"--dependency-output", "-MF"},
    {"--dependency-target-name", "-MT"},
    {"--device-debug", "-G"},
    {"--disable-warnings", "-w"},
    {"--generate-dependencies", "-M"},
    {"--generate-dependencies-with-compile", "-MD"},
    {"--generate-dependency-targets", "-MP"},
    {"--generate-nonsystem-dependencies", "-MM"},
    {"--generate-nonsystem-dependencies-with-compile", "-MMD"},
    {"--gpu-architecture", "-arch"},
    {"--include-path", "-I"},
    {"--library", "-l"},
    {"--library-path", "-L"},
    {"--linker-options", "-Xlinker"},
    {"--m64", "-m64"},
    {"--optimize", "-O"},
    {"--output-file", "-o"},
    {"--pre-include", "-include"},
    {"--preprocess", "-E"},
    {"--profile", "-pg"},
    {"--save-temps", "-save-temps"},
    {"--std", "-std"},
    {"--system-include", "-isystem"},
    {"--time", "-time"},
    {"--undefine-macro", "-U"},
    {"--verbose", "-v"},
    {"--x", "-x"},
    {"-V", "--version"},
    {"-h", "--help"},
};

// --- Utilities ---

enum class SGR {
    RESET,
    FG_RED, FG_GREEN, FG_YELLOW, FG_BLUE
};

void log_with_sgr(SGR sgr, const std::string& colored, const std::string& uncolored = "") {
    const char* code = "";
    switch (sgr) {
        case SGR::RESET: code = "\033[0m"; break;
        case SGR::FG_RED: code = "\033[0;31m"; break;
        case SGR::FG_GREEN: code = "\033[0;32m"; break;
        case SGR::FG_YELLOW: code = "\033[0;33m"; break;
        case SGR::FG_BLUE: code = "\033[0;34m"; break;
    }
    std::stringstream ss;
    ss << code << colored << "\033[0m" << uncolored;
    StatusManager::Log(ss.str());
}

void log_error(const std::string& colored, const std::string& uncolored = "") {
    log_with_sgr(SGR::FG_RED, colored, uncolored);
}
void log_warning(const std::string& colored, const std::string& uncolored = "") {
    log_with_sgr(SGR::FG_YELLOW, colored, uncolored);
}
void log_info(const std::string& colored, const std::string& uncolored = "") {
    log_with_sgr(SGR::FG_BLUE, colored, uncolored);
}
void log_success(const std::string& colored, const std::string& uncolored = "") {
    log_with_sgr(SGR::FG_GREEN, colored, uncolored);
}

// Simple ThreadPool
class ThreadPool {
public:
    ThreadPool(size_t num_threads) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) worker.join();
    }
    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop = false;
};

// --- Helper Functions ---

std::tuple<int, int, int> _get_bazel_version() {
    static std::tuple<int, int, int> version = []() {
        try {
            auto res = subprocess::Run({"bazel", "version"});
            std::stringstream ss(res.stdout_output);
            std::string line;
            while (std::getline(ss, line)) {
                if (line.find("Build label: ") == 0) {
                    std::string ver_str = line.substr(13);
                    std::regex re(R"(^(\d+)\.(\d+)\.(\d+))");
                    std::smatch match;
                    if (std::regex_search(ver_str, match, re)) {
                        return std::make_tuple(std::stoi(match[1]), std::stoi(match[2]), std::stoi(match[3]));
                    }
                }
            }
        } catch (...) {}
        return std::make_tuple(0, 0, 0);
    }();
    return version;
}

std::set<std::string> _get_bazel_cached_action_keys() {
    static std::set<std::string> keys = []() {
        std::set<std::string> k;
        try {
            auto res = subprocess::Run({"bazel", "dump", "--action_cache"});
            std::stringstream ss(res.stdout_output);
            std::string line;
            while (std::getline(ss, line)) {
                if (line.find("actionKey = ") == 0) {
                    k.insert(line.substr(12));
                }
            }
        } catch (...) {}
        return k;
    }();
    return keys;
}

// --- Header Parsing ---

std::set<std::string> _parse_headers_from_makefile_deps(const std::string& d_file_content) {
    if (d_file_content.empty()) return {};
    // Normalize newlines and escapes
    std::string content = d_file_content;
    // Python: replace escaped newlines
    size_t pos = 0;
    while ((pos = content.find("\\\n", pos)) != std::string::npos) {
        content.replace(pos, 2, "");
    }
    
    auto split_pos = content.find(": ");
    if (split_pos == std::string::npos) return {};
    
    std::string deps = content.substr(split_pos + 2);
    
#ifdef _WIN32
    // Replace backslash followed by non-space with forward slash?
    // Python: re.sub(r'\(?=[^ \])', '/', dependencies)
    // Simplified: replace all backslashes with forward slashes except those escaping spaces? 
    // Or just use filesystem::path::make_preferred?
    // Let's manually iterate.
    std::string norm;
    for (size_t i = 0; i < deps.size(); ++i) {
        if (deps[i] == '\\') {
            if (i + 1 < deps.size() && deps[i+1] != ' ' && deps[i+1] != '\\') {
                norm += '/';
            } else {
                norm += deps[i];
            }
        } else {
            norm += deps[i];
        }
    }
    deps = norm;
#endif

    // Split by space, handling escaped spaces.
    // Simplistic split for now (mimic shlex somewhat)
    std::set<std::string> headers;
    std::string current;
    bool escaped = false;
    for (char c : deps) {
        if (escaped) {
            current += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == ' ' || c == '\n' || c == '\r') {
            if (!current.empty()) {
                headers.insert(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) headers.insert(current);
    
    if (!headers.empty()) headers.erase(headers.begin()); // First is source
    return headers;
}

// --- Caching Utilities ---
// Using a simple global map with mutex for caching modified times
std::map<std::string, double> _mtime_cache;
std::mutex _mtime_cache_mutex;

double _get_cached_modified_time(const std::string& path) {
    std::unique_lock<std::mutex> lock(_mtime_cache_mutex);
    if (_mtime_cache.count(path)) return _mtime_cache[path];
    
    std::error_code ec;
    auto ftime = fs::last_write_time(path, ec);
    if (ec) {
        _mtime_cache[path] = 0;
        return 0;
    }
    // Convert to double timestamp
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    double ts = std::chrono::duration<double>(sctp.time_since_epoch()).count();
    _mtime_cache[path] = ts;
    return ts;
}

double _get_cached_adjusted_modified_time(const std::string& path) {
    double mtime = _get_cached_modified_time(path);
    // Bazel internal cutoff (~1 year from now)
    auto cutoff = std::chrono::system_clock::now() + std::chrono::hours(24 * 365);
    double cutoff_ts = std::chrono::duration<double>(cutoff.time_since_epoch()).count();
    if (mtime > cutoff_ts) return 0;
    return mtime;
}

bool _get_cached_file_exists(const std::string& path) {
    return _get_cached_modified_time(path) != 0;
}

// --- Windows Helpers ---

std::string windows_list2cmdline(const std::vector<std::string>& args) {
    std::string result;
    for (const auto& arg : args) {
        if (!result.empty()) result += " ";
        
        bool need_quote = arg.empty() || arg.find_first_of(" \t\"") != std::string::npos;
        if (need_quote) result += "\"";
        
        std::string bs_buf;
        for (char c : arg) {
            if (c == '\\') {
                bs_buf += c;
            } else if (c == '"') {
                result += std::string(bs_buf.size() * 2, '\\');
                result += "\\\"";
                bs_buf.clear();
            } else {
                if (!bs_buf.empty()) {
                    result += bs_buf;
                    bs_buf.clear();
                }
                result += c;
            }
        }
        
        if (!bs_buf.empty()) {
            if (need_quote) {
                result += std::string(bs_buf.size() * 2, '\\');
            } else {
                result += bs_buf;
            }
        }
        
        if (need_quote) result += "\"";
    }
    return result;
}

subprocess::RunResult _subprocess_run_spilling_over_to_param_file_if_needed(std::vector<std::string> command, std::map<std::string, std::string> env, bool capture_stdout = true) {
    subprocess::RunOptions options;
    options.capture_stdout = capture_stdout;
#ifdef _WIN32
    try {
        return subprocess::Run(command, env, options);
    } catch (const std::runtime_error& e) {
        // Crude check for length error, assuming it's mostly length related on Windows
        // Create param file
        char tmpname[L_tmpnam];
        if (tmpnam(tmpname)) {
            std::string param_file = tmpname;
            std::ofstream ofs(param_file);
            ofs << windows_list2cmdline({command.begin() + 1, command.end()});
            ofs.close();
            
            std::vector<std::string> new_cmd = {command[0], "@" + param_file};
            try {
                auto res = subprocess::Run(new_cmd, env, options);
                fs::remove(param_file);
                return res;
            } catch (...) {
                fs::remove(param_file);
                throw;
            }
        }
        throw;
    }
#else
    return subprocess::Run(command, env, options);
#endif
}

// --- Header Finding ---

bool _is_nvcc(const std::string& path) {
    return fs::path(path).filename().string().find("nvcc") == 0;
}

struct GetHeadersResult {
    std::set<std::string> headers;
    bool should_cache;
    std::vector<std::string> warnings;
};

GetHeadersResult _get_headers_gcc(const json_utils::JsonValue& action, const std::string& source_path, const std::string& action_key) {
    auto& args_json = action.as_object().at("arguments").as_array();
    std::vector<std::string> args;
    for(const auto& v : args_json) args.push_back(v.string_val);

    // Check cache (Bazel's cache)
    if (_get_bazel_cached_action_keys().count(action_key)) {
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i].find("-MF") == 0) {
                std::string dep_path = (args[i].size() > 3) ? args[i].substr(3) : args[i+1];
                if (fs::exists(dep_path)) {
                    std::ifstream t(dep_path);
                    std::stringstream buffer;
                    buffer << t.rdbuf();
                    auto headers = _parse_headers_from_makefile_deps(buffer.str());
                    double dep_mtime = _get_cached_modified_time(dep_path);
                    
                    bool fresh = true;
                    if (_get_cached_adjusted_modified_time(source_path) > dep_mtime) fresh = false;
                    for (const auto& h : headers) {
                        if (_get_cached_adjusted_modified_time(h) > dep_mtime) { fresh = false; break; }
                    }
                    if (fresh) return {headers, true};
                }
                break;
            }
        }
    }

    std::vector<std::string> header_cmd;
    for (const auto& arg : args) {
        if (arg.find("-M") == 0 || arg.find("-dependencies") != std::string::npos || (arg.size() >= 2 && arg.substr(arg.size()-2) == ".d")) continue;
        if (arg == "-o" || (arg.size() >= 2 && arg.substr(arg.size()-2) == ".o")) continue;
        if (arg.find("-fsanitize") == 0) continue;
        header_cmd.push_back(arg);
    }

    bool is_nvcc = _is_nvcc(header_cmd[0]);
    if (is_nvcc) {
        header_cmd.push_back("--generate-dependencies");
    } else {
        header_cmd.push_back("-M");
        header_cmd.push_back("--print-missing-file-dependencies");
    }

    std::map<std::string, std::string> env;
    if (action.as_object().count("environmentVariables")) {
        for (const auto& kv : action.as_object().at("environmentVariables").as_array()) {
             env[kv.as_object().at("key").string_val] = kv.as_object().at("value").string_val;
        }
    }

    auto res = _subprocess_run_spilling_over_to_param_file_if_needed(header_cmd, env);
    if (!res.stderr_output.empty()) {
        // print warning once?
        // implementation detail: _print_header_finding_warning_once
    }

    auto headers = _parse_headers_from_makefile_deps(res.stdout_output);
    
    bool should_cache = false;
    if (is_nvcc) {
        should_cache = !res.stdout_output.empty();
    } else {
        size_t num = headers.size();
        std::set<std::string> existing;
        for(const auto& h : headers) if (_get_cached_file_exists(h)) existing.insert(h);
        headers = existing;
        should_cache = (headers.size() == num);
    }

    return {headers, should_cache};
}

GetHeadersResult _get_headers_msvc(const json_utils::JsonValue& action, const std::string& source_path) {
    auto& args_json = action.as_object().at("arguments").as_array();
    std::vector<std::string> header_cmd;
    
    for(const auto& v : args_json) header_cmd.push_back(v.string_val);
    
    header_cmd.push_back("/showIncludes");
    header_cmd.push_back("/EP");
    // header_cmd.push_back("/nologo"); // Assuming it's in args or we don't strictly need it if we filter stderr well.

    std::map<std::string, std::string> env;
    if (action.as_object().count("environmentVariables")) {
        for (const auto& kv : action.as_object().at("environmentVariables").as_array()) {
             env[kv.as_object().at("key").string_val] = kv.as_object().at("value").string_val;
        }
    }
    if (env.find("SystemRoot") == env.end()) { if (const char* v = std::getenv("SystemRoot")) env["SystemRoot"] = v; }

    if (!env.count("INCLUDE")) {
        std::string includes;
        for (const auto& p : RefreshConfig::GetWindowsDefaultIncludePaths()) {
            if (!includes.empty()) includes += ";";
            includes += p;
        }
        env["INCLUDE"] = includes;
    }

    auto res = _subprocess_run_spilling_over_to_param_file_if_needed(header_cmd, env, false); // Discard stdout!
    
    std::set<std::string> headers;
    std::stringstream ss(res.stderr_output);
    std::string line;
    
    std::vector<std::string> markers = {
        "Note: including file:", "注意: 包含文件: ", "注意: 包含檔案:", "Poznámka: Včetně souboru:",
        "Hinweis: Einlesen der Datei:", "Remarque : inclusion du fichier : ", "Nota: file incluso ",
        "メモ: インクルード ファイル: ", "참고: 포함 파일:", "Uwaga: w tym pliku: ", "Observação: incluindo arquivo:",
        "Примечание: включение файла: ", "Not: eklenen dosya: ", "Nota: inclusión del archivo:"
    };

    bool error = (res.return_code != 0); // Python uses exit code ignore? No, it says check=False.
    // It checks for fatal error C1083 in output.
    
    std::vector<std::string> error_lines;
    
    while(std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        bool matched = false;
        for (const auto& marker : markers) {
            if (line.find(marker) == 0) {
                std::string h = line.substr(marker.size());
                h.erase(0, h.find_first_not_of(" \t"));
                headers.insert(h);
                matched = true;
                break;
            }
        }
        
        if (!matched) {
             if (line.find(source_path) != std::string::npos) continue; // Filter source filename echo
             if (line.find("D9002 : ignoring unknown option") != std::string::npos) continue;
             
             error_lines.push_back(line);
             if (line.find("fatal error C1083:") != std::string::npos) error = true;
        }
    }
    
    return {headers, !error, error_lines};
}

bool _file_is_in_main_workspace_and_not_external(const std::string& file_str) {
    fs::path p(file_str);
    // Simplification: check if path starts with "external" or "bazel-out/.../external"
    std::string s = p.generic_string();
    if (s.find("external/") == 0) return false;
    if (s.find("bazel-out") == 0 && s.find("/external/") != std::string::npos) return false;
    return true;
}

GetHeadersResult _get_headers(const json_utils::JsonValue& action, const std::string& source_path) {
    std::string exclude = RefreshConfig::GetExcludeHeaders();
    if (exclude == "all") return {{}, false, {}};
    
    // "external" check needs is_external from action... not parsed yet?
    // Assuming action is passed as JSON.
    
    std::string output_file;
    auto& args_json = action.as_object().at("arguments").as_array();
    for (size_t i = 0; i < args_json.size(); ++i) {
        std::string arg = args_json[i].string_val;
        if (arg == "-o" || arg == "--output") { output_file = args_json[i+1].string_val; break; }
        if (arg.find("/Fo") == 0 || arg.find("-Fo") == 0) { output_file = arg.substr(3); break; }
        if (arg.find("--output=") == 0) { output_file = arg.substr(9); break; }
    }

    std::string actionKey = action.as_object().at("actionKey").string_val;

    // Check disk cache
    if (!g_disable_cache && !output_file.empty()) {
        std::string cache_path = output_file + ".hedron.compile-commands.headers";
        if (fs::exists(cache_path)) {
            try {
                std::ifstream f(cache_path);
                std::stringstream buffer;
                buffer << f.rdbuf();
                auto json = json_utils::Parse(buffer.str());
                if (json.type == json_utils::JsonType::Array && json.as_array().size() == 2) {
                     std::string cached_key = json.as_array()[0].string_val;
                     if (cached_key == actionKey) {
                         std::set<std::string> headers;
                         for (const auto& h : json.as_array()[1].as_array()) headers.insert(h.string_val);
                         
                         double cache_time = _get_cached_modified_time(cache_path);
                         bool fresh = (_get_cached_adjusted_modified_time(source_path) <= cache_time);
                         for (const auto& h : headers) {
                             if (_get_cached_adjusted_modified_time(h) > cache_time) { fresh = false; break; }
                         }
                         if (fresh) return {headers, true, {}};
                     }
                }
            } catch (...) {}
        }
    }

    GetHeadersResult result;
    std::string compiler = args_json[0].string_val;
    if (compiler.length() >= 6 && compiler.substr(compiler.length()-6) == "cl.exe") {
        result = _get_headers_msvc(action, source_path);
    } else if (compiler.find("ml.exe") != std::string::npos || compiler.find("ml64.exe") != std::string::npos) {
        result = {{}, false, {}};
    } else {
        result = _get_headers_gcc(action, source_path, actionKey);
    }

    if (!output_file.empty() && result.should_cache) {
        std::string cache_path = output_file + ".hedron.compile-commands.headers";
        fs::create_directories(fs::path(cache_path).parent_path());
        std::ofstream f(cache_path);
        json_utils::JsonArray arr;
        arr.push_back(json_utils::JsonValue(actionKey));
        json_utils::JsonArray h_arr;
        for (const auto& h : result.headers) h_arr.push_back(json_utils::JsonValue(h));
        arr.push_back(json_utils::JsonValue(h_arr));
        f << json_utils::Dump(json_utils::JsonValue(arr));
    }

    if (exclude == "external") {
        std::set<std::string> filtered;
        for (const auto& h : result.headers) {
            if (_file_is_in_main_workspace_and_not_external(h)) filtered.insert(h);
        }
        return {filtered, result.should_cache, result.warnings};
    }
    return result;
}

// --- File Identification ---

struct GetFilesResult {
    std::set<std::string> sources;
    std::set<std::string> headers;
    std::vector<std::string> warnings;
};

GetFilesResult _get_files(json_utils::JsonValue& action) {
    auto& args_json = action.as_object().at("arguments").as_array();
    std::vector<std::string> args;
    for(const auto& v : args_json) args.push_back(v.string_val);

    std::string source_file;
    // Find source file candidates
    std::vector<std::string> candidates;
    std::set<std::string> exts = {".c", ".i", ".cc", ".cpp", ".cxx", ".c++", ".C", ".CC", ".cp", ".CPP", ".C++", ".CXX", ".ii", ".m", ".mm", ".M", ".cu", ".cui", ".cl", ".clcpp", ".s", ".asm", ".S"};
    
    for (const auto& arg : args) {
        if (arg.find("-") != 0) {
            std::string ext = fs::path(arg).extension().string();
            if (exts.count(ext)) candidates.push_back(arg);
        }
    }
    
    if (candidates.empty()) {
	// Leave empty so we don't report this spammy "no sources log"
        return {{}, {}, {}};
    }
    source_file = candidates[0];
    
    if (candidates.size() > 1) {
        // Heuristics
        auto it_o = std::find(args.begin(), args.end(), "-o");
        if (it_o != args.end() && it_o != args.begin()) {
            source_file = *(it_o - 1);
        } else {
            auto it_c = std::find(args.begin(), args.end(), "/c");
            if (it_c != args.end() && it_c + 1 != args.end()) {
                source_file = *(it_c + 1);
            }
        }
    }

    if (!fs::exists(source_file)) {
        return {{source_file}, {}, {"Source file not found: " + source_file}};
    }

    // Check assembly
    std::string ext = fs::path(source_file).extension().string();
    if (ext == ".s" || ext == ".asm") return {{source_file}, {}, {}};

    auto result = _get_headers(action, source_file);
    auto headers = result.headers;

    // Language flag fix
    if (headers.size() > 0) {
        bool has_h = false;
        for(const auto& h : headers) if (h.find(".h") != std::string::npos) { has_h = true; break; }
        
        bool has_lang = false;
        for (const auto& arg : args) if (arg.find("-x") == 0 || arg == "-objc" || arg == "/tc" || arg == "/tp") has_lang = true;
        
        if (has_h && !has_lang) {
             // Add language flag logic... (simplified)
             // Python logic is complex mapping extensions.
             // If needed, insert into args.
             // Updating the action object in place:
             // For now, skip implementation of complex lang flag insertion unless critical.
             // It modifies action.arguments.
             // TODO: Implement extension mapping if needed.
        }
    }

    return {{source_file}, headers, result.warnings};
}

// --- Platform Patches ---

std::string _get_apple_SDKROOT(const std::string& SDK_name) {
    static std::map<std::string, std::string> cache;
    if (cache.count(SDK_name)) return cache[SDK_name];
    
    try {
        // xcrun --show-sdk-path -sdk <name>
        std::string name_lower = SDK_name; 
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        
        auto res_path = subprocess::Run({"xcrun", "--show-sdk-path", "-sdk", name_lower});
        std::string path = res_path.stdout_output;
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();

        auto res_ver = subprocess::Run({"xcrun", "--show-sdk-version", "-sdk", name_lower});
        std::string ver = res_ver.stdout_output;
        while (!ver.empty() && (ver.back() == '\n' || ver.back() == '\r')) ver.pop_back();

        // Strip version from path? Logic: path.replace(version, '')
        size_t pos = path.find(ver);
        if (pos != std::string::npos) {
            path.replace(pos, ver.length(), "");
        }
        cache[SDK_name] = path;
        return path;
    } catch (...) {
        return "";
    }
}

std::string _get_apple_platform(const std::vector<std::string>& args) {
    std::regex re("/Platforms/([a-zA-Z]+).platform/Developer/");
    std::smatch match;
    for (const auto& arg : args) {
        if (std::regex_search(arg, match, re)) {
            return match[1];
        }
    }
    return "";
}

std::string _get_apple_DEVELOPER_DIR() {
    static std::string dir = []() {
        try {
            auto res = subprocess::Run({"xcode-select", "--print-path"});
            std::string s = res.stdout_output;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            return s;
        } catch (...) { return std::string(""); }
    }();
    return dir;
}

std::vector<std::string> _apple_platform_patch(std::vector<std::string> args) {
    bool has_bazel_xcode = false;
    for (const auto& arg : args) if (arg.find("__BAZEL_XCODE_") != std::string::npos) { has_bazel_xcode = true; break; }
    
    if (has_bazel_xcode) {
        args[0] = "clang";
        std::vector<std::string> new_args;
        for (const auto& arg : args) {
            if (arg.find("DEBUG_PREFIX_MAP_PWD") == 0 && arg != "OSO_PREFIX_MAP_PWD") continue;
            
            std::string new_arg = arg;
            // replace __BAZEL_XCODE_DEVELOPER_DIR__
            size_t pos;
            while ((pos = new_arg.find("__BAZEL_XCODE_DEVELOPER_DIR__")) != std::string::npos) {
                new_arg.replace(pos, 31, _get_apple_DEVELOPER_DIR());
            }
            
            // replace __BAZEL_XCODE_SDKROOT__
            if (new_arg.find("__BAZEL_XCODE_SDKROOT__") != std::string::npos) {
                std::string platform = _get_apple_platform(args);
                std::string sdkroot = _get_apple_SDKROOT(platform);
                while ((pos = new_arg.find("__BAZEL_XCODE_SDKROOT__")) != std::string::npos) {
                    new_arg.replace(pos, 25, sdkroot);
                }
            }
            new_args.push_back(new_arg);
        }
        return new_args;
    }
    return args;
}

bool _is_relative_to(const fs::path& p, const fs::path& base) {
    // std::filesystem::relative_to is C++20 (or strictly, p.relative_path() logic)
    // Check if p starts with base
    std::string ps = fs::absolute(p).string();
    std::string bs = fs::absolute(base).string();
    return ps.find(bs) == 0;
}

std::vector<std::string> _emscripten_platform_patch(json_utils::JsonValue& action) {
    auto& args_json = action.as_object().at("arguments").as_array();
    std::vector<std::string> args;
    for(const auto& v : args_json) args.push_back(v.string_val);
    
    if (fs::path(args[0]).filename().string().find("emcc") != 0) return args;

    fs::path workspace_absolute = fs::current_path(); // Env var logic handled in main

    auto get_workspace_root = [&](fs::path path_from_execroot) -> fs::path {
        auto it = path_from_execroot.begin();
        if (it != path_from_execroot.end() && *it == "external") {
             // external/repo/... -> external/repo
             auto it2 = it; ++it2;
             if (it2 != path_from_execroot.end()) {
                 return fs::path("external") / *it2;
             }
        }
        return ".";
    };

    std::map<std::string, std::string> env;
    if (action.as_object().count("environmentVariables")) {
        for (const auto& kv : action.as_object().at("environmentVariables").as_array()) {
             env[kv.as_object().at("key").string_val] = kv.as_object().at("value").string_val;
        }
    }
    
    env["EXT_BUILD_ROOT"] = workspace_absolute.string();
    env["EMCC_SKIP_SANITY_CHECK"] = "1";
    env["EM_COMPILER_WRAPPER"] = RefreshConfig::GetPrintArgsExecutable();
    
    if (!env.count("EM_BIN_PATH")) {
         std::string sysroot;
         for(size_t i=0; i<args.size(); ++i) {
             if (args[i] == "--sysroot" || args[i] == "-isysroot") {
                 if (i+1 < args.size()) sysroot = args[i+1];
             } else if (args[i].find("--sysroot=") == 0) {
                 sysroot = args[i].substr(10);
             } else if (args[i].find("-isysroot") == 0) { // Correct check?
                  // python: arg.startswith('-isysroot') -> arg[len...] 
                  // but -isysroot <path> is handled above.
                  // Does -isysroot<path> exist? assuming yes based on python code
                  sysroot = args[i].substr(9);
             }
         }
         if (!sysroot.empty()) {
             env["EM_BIN_PATH"] = get_workspace_root(sysroot).string();
         }
    }
    if (!env.count("EM_CONFIG_PATH")) {
        env["EM_CONFIG_PATH"] = (get_workspace_root(args[0]) / "emscripten_toolchain" / "emscripten_config").string();
    }

    // Run emcc
    // Convert args[0] to native path?
    std::vector<std::string> emcc_cmd;
    emcc_cmd.push_back(fs::path(args[0]).make_preferred().string());
    for(size_t i=1; i<args.size(); ++i) emcc_cmd.push_back(args[i]);

    auto res = subprocess::Run(emcc_cmd, env);
    
    // Parse output
    std::string begin_marker = "===HEDRON_COMPILE_COMMANDS_BEGIN_ARGS===";
    std::string end_marker = "===HEDRON_COMPILE_COMMANDS_END_ARGS===";
    
    std::stringstream ss(res.stdout_output);
    std::string line;
    std::vector<std::string> new_args;
    bool in_args = false;
    while (std::getline(ss, line)) {
        // Handle CR?
        if (!line.empty() && line.back() == '\r') line.pop_back();
        
        if (line == begin_marker) {
            in_args = true;
            continue;
        }
        if (line == end_marker) {
            in_args = false;
            break;
        }
        if (in_args) {
            new_args.push_back(line);
        }
    }
    
    if (!new_args.empty()) {
        // Fix driver path
        fs::path driver(new_args[0]);
        // _is_relative_to logic?
        // If driver is absolute and relative to workspace, make relative.
        // Logic: if driver starts with workspace, strip.
        // Actually simpler: use lexically_relative if C++17 (which we have)
        // But keeping it simple string match for now.
        if (driver.is_absolute()) {
            // ...
        }
        return new_args;
    }
    
    return args; // Fallback
}

std::vector<std::string> _all_platform_patch(std::vector<std::string> args) {
    std::vector<std::string> new_args;
    bool skip_next = false;
    for (const auto& arg : args) {
        if (arg.find("-fmodules-cache-path=bazel-out/") == 0) continue;
        if (arg.find("-fdebug-prefix-map") == 0) continue;
        if (arg == "-fno-canonical-system-headers") continue;
        
        if (arg.find("-gcc-toolchain") == 0) {
            if (arg == "-gcc-toolchain") skip_next = true;
            continue;
        }
        if (skip_next) { skip_next = false; continue; }
        
        new_args.push_back(arg);
    }
    
    // Symlink check
    if (fs::is_symlink(new_args[0])) {
         try {
             std::string target = fs::read_symlink(new_args[0]).string();
             if (fs::path(target).filename() == "ccache") {
                 // Assume original name is compiler name?
                 // python: compiler = os.path.basename(compile_args[0]); real_compiler = shutil.which(compiler);
                 // This logic seems to imply resolving the symlink of 'gcc' -> 'ccache' -> 'gcc'?
                 // Wait, python says: "Discover compilers that are actually symlinks to ccache--and replace them with the underlying compiler"
                 // It replaces args[0] with `shutil.which(os.path.basename(args[0]))`.
                 // This assumes `which` finds the real one (not the symlink) or a different one?
                 // If `gcc` is a symlink to `ccache`, `which gcc` should return... the symlink?
                 // Unless PATH is ordered such that real gcc is elsewhere.
                 // Skipping full implementation of this edge case for now as it requires robust `which`.
             }
         } catch (...) {}
    }
    
    return new_args;
}

std::vector<std::string> _nvcc_patch(std::vector<std::string> args); // Declaration

// --- Command Conversion ---

std::vector<std::string> _nvcc_patch(std::vector<std::string> args) {
    if (!_is_nvcc(args[0])) return args;
    
    std::vector<std::string> new_args;
    new_args.push_back(args[0]);
    new_args.push_back("-Xclang");
    new_args.push_back("-fcuda-allow-variadic-functions");
    
    bool skip_next = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (skip_next) { skip_next = false; continue; }
        std::string arg = args[i];
        
        if (_nvcc_flags_to_skip_no_arg.count(arg)) continue;
        
        bool skip = false;
        for (const auto& f : _nvcc_flags_to_skip_with_arg) {
            if (arg == f) { skip = true; skip_next = true; break; }
            if (f.size() > 2 && arg.find(f + "=") == 0) { skip = true; break; }
        }
        if (skip) continue;
        
        // Rewrite
        std::string option = arg;
        std::string remainder;
        auto pos = arg.find('=');
        if (pos != std::string::npos) {
            option = arg.substr(0, pos);
            remainder = arg.substr(pos + 1);
        }
        
        if (_nvcc_rewrite_flags.count(option)) {
            std::string mapped = _nvcc_rewrite_flags.at(option);
            if (pos != std::string::npos) {
                if (mapped == "-D" || mapped == "-I" || mapped == "-L" || mapped == "-U") {
                    arg = mapped + remainder;
                } else {
                    arg = mapped + "=" + remainder;
                }
            } else {
                arg = mapped;
            }
        }
        
        // Comma separation logic... skip for now, assume simple case
        new_args.push_back(arg);
    }
    return new_args;
}

struct CommandEntry {
    std::string file;
    std::vector<std::string> arguments;
    std::string directory;
};

std::vector<CommandEntry> _convert_compile_commands(const json_utils::JsonValue& aquery_output) {
    std::vector<CommandEntry> entries;
    if (aquery_output.type != json_utils::JsonType::Object) return entries;
    
    // Convert actions
    auto& actions = aquery_output.as_object().at("actions").as_array();
    
    std::mutex entries_mutex;
    std::string workspace_dir = fs::current_path().string(); // assumes CWD is workspace root

    {
        ThreadPool pool(std::min(32u, std::thread::hardware_concurrency() + 4));
        std::atomic<size_t> completed{0};
        size_t total = actions.size();
        for (size_t i = 0; i < actions.size(); ++i) {
            pool.enqueue([&, i] {
                try {
                    json_utils::JsonValue action_copy = actions[i];
                    
                    auto result = _get_files(action_copy);
                    const auto& sources = result.sources;
                    const auto& headers = result.headers;

                    if (!result.warnings.empty()) {
                        std::stringstream ss;
                        ss << "Warning: ";
                        for(const auto& w : result.warnings) ss << w << "; ";
                        StatusManager::Log(ss.str());
                    }
                    
                    if (sources.empty()) return;
                    
                    std::vector<std::string> args;
                    for(const auto& v : action_copy.as_object().at("arguments").as_array()) args.push_back(v.string_val);
                    
                    args = _apple_platform_patch(args);
                    args = _emscripten_platform_patch(action_copy);
                    args = _all_platform_patch(args);
                    args = _nvcc_patch(args);
    
                    std::lock_guard<std::mutex> lock(entries_mutex);
                    for (const auto& s : sources) entries.push_back({s, args, workspace_dir});
                    for (const auto& h : headers) entries.push_back({h, args, workspace_dir});
                } catch (const std::exception& e) {
                     StatusManager::Log("Exception in worker: " + std::string(e.what()));
                }
                size_t c = ++completed;
                std::stringstream ss;
                ss << "[" << c << " / " << total << "] actions processed...";
                StatusManager::SetStatus(0, ss.str());
            });
                    }
    }
    
    // Destructor of pool waits for all.
    StatusManager::SetStatus(0, "");
    return entries;
}

// "Generated files: //external link makes external dependencies work"
// We create a link to the external directory in the output base to allow clangd to find external headers.
// This avoids the "execroot trap" where the execroot is wiped/reconfigured on each build.
// Instead, we point to the accumulating cache in the output base.
// See ImplementationReadme.md for full reasoning.
void _ensure_external_workspaces_link_exists() {
    if (fs::exists("external")) return;

    try {
        // Resolve target path via bazel-out
        // bazel-out -> execroot/<workspace>/bazel-out
        // We want execroot/../external which is output_base/external
        // So bazel-out/../../../external
        
        fs::path bazel_out = "bazel-out";
        if (!fs::exists(bazel_out)) {
            // Fallback or error? bazel-out should exist if we are running via bazel.
            // But maybe the user is running the binary directly?
            // Try to deduce from execution path?
            return; 
        }

        fs::path target = fs::canonical(bazel_out).parent_path().parent_path().parent_path() / "external";
        
        if (!fs::exists(target)) {
             // Try creating it? No, it's managed by Bazel.
             // Maybe we are in a different structure.
             return;
        }

        log_info("Creating link 'external' -> " + target.string());

#ifdef _WIN32
        // Use mklink /J for junction (no admin required)
        // fs::create_directory_symlink requires admin or dev mode.
        auto res = subprocess::Run({"cmd", "/c", "mklink", "/J", "external", target.string()});
        if (!res.stderr_output.empty()) {
            log_warning("Failed to create external junction: " + res.stderr_output);
        }
#else
        // relative link is preferred for portability but absolute is easier to resolve.
        // The python script used relative: bazel-out/../../../external
        // Let's use relative if possible, but canonical resolved absolute.
        // fs::create_directory_symlink(target, "external");
        
        // Python used: ln -s bazel-out/../../../external .
        // This relies on bazel-out being a symlink/dir in cwd.
        // Let's try the exact python logic for non-windows.
        fs::create_directory_symlink("bazel-out/../../../external", "external");
#endif

    } catch (const std::exception& e) {
        log_warning("Could not create external link: " + std::string(e.what()));
    }
}

// --- Main ---

int main(int argc, char** argv) {
    subprocess::SetStatusCallback(StatusManager::SetStatus);
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-cache") {
            g_disable_cache = true;
        }
    }
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        // ensure cwd
        if (const char* env_p = std::getenv("BUILD_WORKSPACE_DIRECTORY")) {
            fs::current_path(env_p);
        } else {
            log_error("BUILD_WORKSPACE_DIRECTORY not found. Run with bazel run.");
            return 1;
        }

        _ensure_external_workspaces_link_exists();
        // _ensure_gitignore_entries_exist();

        std::vector<CommandEntry> all_entries;

        auto target_flags = RefreshConfig::GetTargetFlagPairs();
        for (const auto& kv : target_flags) {
            std::string target = kv.first;
            std::string flags = kv.second;
            log_info("Analyzing commands used in " + target);
            
            std::vector<std::string> cmd = {"bazel", "aquery", 
                "mnemonic('(Objc|Cpp|Cuda)Compile',deps(" + target + "))", 
                "--output=jsonproto", "--include_artifacts=false", "--ui_event_filters=-info", "--noshow_progress",
                "--features=-compiler_param_file", "--features=-layering_check"};
            
            if (const char* env_ver = std::getenv("BAZEL_VERSION")) {
                // Check version...
            }
            // Add flags
            std::stringstream ss(flags);
            std::string s;
            while (ss >> s) cmd.push_back(s);
            
            auto res = subprocess::Run(cmd);
            
            try {
                auto json = json_utils::Parse(res.stdout_output);
                if (json.type == json_utils::JsonType::Object && json.as_object().count("actions")) {
                    std::cout << "Found " << json.as_object().at("actions").as_array().size() << " actions" << std::endl;
                }
                auto entries = _convert_compile_commands(json);
                all_entries.insert(all_entries.end(), entries.begin(), entries.end());
            } catch (const std::exception& e) {
                log_warning("Failed to parse/process aquery output for " + target + ": " + e.what());
            }
            
            log_success("Finished extracting commands for " + target);
        }

        if (all_entries.empty()) {
            log_error("No commands extracted.");
            return 1;
        }

        // Filter duplicate headers
        std::vector<CommandEntry> unique_entries;
        std::set<std::string> seen;
        for (const auto& e : all_entries) {
             // Simplified dedupe: if file seen, skip?
             // Logic in python: emit source always. Emit header only if not seen.
             bool is_header = (e.file.find(".h") != std::string::npos); // weak check
             if (is_header) {
                 if (seen.count(e.file)) continue;
                 seen.insert(e.file);
             }
             unique_entries.push_back(e);
        }

        // Write JSON
        std::ofstream out("compile_commands.json");
        out << "[\n";
        for (size_t i = 0; i < unique_entries.size(); ++i) {
            const auto& e = unique_entries[i];
            out << "  {\n";
            out << "    \"file\": " << json_utils::Dump(json_utils::JsonValue(e.file)) << ",\n";
            
            out << "    \"arguments\": [\n";
            for (size_t j = 0; j < e.arguments.size(); ++j) {
                out << "      " << json_utils::Dump(json_utils::JsonValue(e.arguments[j]));
                if (j < e.arguments.size() - 1) out << ",";
                out << "\n";
            }
            out << "    ],\n";
            
            out << "    \"directory\": " << json_utils::Dump(json_utils::JsonValue(e.directory)) << "\n";
            out << "  }" << (i < unique_entries.size() - 1 ? "," : "") << "\n";
        }
        out << "]\n";

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        std::cout << "Generated compile_commands.json in " << std::fixed << std::setprecision(2) << elapsed.count() << "s" << std::endl;

    } catch (const std::exception& e) {
        log_error(std::string("Fatal error: ") + e.what());
        return 1;
    }
    return 0;
}

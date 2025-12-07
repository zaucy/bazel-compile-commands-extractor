#ifndef SUBPROCESS_H
#define SUBPROCESS_H

#include <string>
#include <vector>
#include <map>
#include <functional>

namespace subprocess {

// Callback for status updates: (id, status_text). 
// If status_text is empty, the status for id should be cleared.
using StatusCallback = std::function<void(size_t id, const std::string&)>;

void SetStatusCallback(StatusCallback callback);

struct RunResult {
    int return_code;
    std::string stdout_output;
    std::string stderr_output;
};

// Runs a command.
// If env is empty, inherits current environment.
// If check is true, throws runtime_error on non-zero return code.
RunResult Run(const std::vector<std::string>& command, 
              const std::map<std::string, std::string>& env = {}, 
              bool check = false);

} // namespace subprocess

#endif // SUBPROCESS_H

#ifndef REFRESH_H
#define REFRESH_H

#include <vector>
#include <string>
#include <utility>

struct RefreshConfig {
    static const std::vector<std::string>& GetWindowsDefaultIncludePaths();
    static const std::string& GetExcludeHeaders(); // "all", "external", or ""
    static bool GetExcludeExternalSources();
    static const std::vector<std::pair<std::string, std::string>>& GetTargetFlagPairs();
    static const std::string& GetPrintArgsExecutable();
};

#endif // REFRESH_H

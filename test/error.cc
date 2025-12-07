// This file contains an error but we only extract commands so it should be fine unless header parsing fails.
// To trigger a header parsing error (which runs the compiler), we need to include something that doesn't exist?
// But header parsing uses /EP /showIncludes. It might fail if syntax is totally broken?
// No, /EP usually runs fine even with some errors, but if #include fails it prints error.
// Let's try a missing include.
#include "missing_header.h"

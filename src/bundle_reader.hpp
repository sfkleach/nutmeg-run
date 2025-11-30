#ifndef BUNDLE_READER_HPP
#define BUNDLE_READER_HPP

#include "function_object.hpp"
#include "instruction.hpp"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace nutmeg {

class BundleReaderError : public std::runtime_error {
public:
    explicit BundleReaderError(const std::string& msg) : std::runtime_error(msg) {}
};

// Represents a binding from the Bindings table.
struct Binding {
    std::string idname;
    bool lazy;
    std::string value;  // JSON-encoded FunctionObject.
    std::string filename;
};

class BundleReader {
private:
    sqlite3* db_;
    std::string bundle_path_;

    // Helper to execute queries and handle errors.
    void check_sqlite_result(int result, const std::string& operation);

public:
    explicit BundleReader(const std::string& bundle_path);
    ~BundleReader();

    // Disable copy (defensive check, as we're managing SQLite resources).
    BundleReader(const BundleReader&) = delete;
    BundleReader& operator=(const BundleReader&) = delete;

    // Get all entry points.
    std::vector<std::string> get_entry_points();

    // Get binding by IdName.
    Binding get_binding(const std::string& idname);

    // Get dependencies for a given IdName.
    std::vector<std::string> get_dependencies(const std::string& idname);

    // Parse the JSON value field into a FunctionObject, compiling to threaded code.
    FunctionObject parse_function_object(const std::string& json, const std::unordered_map<Opcode, void*>& opcode_map);
};

} // namespace nutmeg

#endif // BUNDLE_READER_HPP

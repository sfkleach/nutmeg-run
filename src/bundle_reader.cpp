#include "bundle_reader.hpp"
#include <nlohmann/json.hpp>
#include <fmt/core.h>

using json = nlohmann::json;

namespace nutmeg {

BundleReader::BundleReader(const std::string& bundle_path)
    : db_(nullptr), bundle_path_(bundle_path) {
    int result = sqlite3_open(bundle_path.c_str(), &db_);
    if (result != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw BundleReaderError(fmt::format("Failed to open bundle file '{}': {}", bundle_path, error));
    }
}

BundleReader::~BundleReader() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void BundleReader::check_sqlite_result(int result, const std::string& operation) {
    if (result != SQLITE_OK && result != SQLITE_ROW && result != SQLITE_DONE) {
        std::string error = sqlite3_errmsg(db_);
        throw BundleReaderError(fmt::format("{}: {}", operation, error));
    }
}

std::vector<std::string> BundleReader::get_entry_points() {
    std::vector<std::string> entry_points;
    sqlite3_stmt* stmt = nullptr;

    const char* sql = "SELECT id_name FROM entry_points";
    int result = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    check_sqlite_result(result, "Failed to prepare entry_points query");

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* idname = sqlite3_column_text(stmt, 0);
        if (idname) {
            entry_points.emplace_back(reinterpret_cast<const char*>(idname));
        }
    }

    check_sqlite_result(result, "Failed to execute EntryPoints query");
    sqlite3_finalize(stmt);

    return entry_points;
}

Binding BundleReader::get_binding(const std::string& idname) {
    sqlite3_stmt* stmt = nullptr;

    const char* sql = "SELECT id_name, lazy, value, file_name FROM bindings WHERE id_name = ?";
    int result = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    check_sqlite_result(result, "Failed to prepare bindings query");

    result = sqlite3_bind_text(stmt, 1, idname.c_str(), -1, SQLITE_TRANSIENT);
    check_sqlite_result(result, "Failed to bind parameter");

    result = sqlite3_step(stmt);
    if (result != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw BundleReaderError(fmt::format("Binding not found: {}", idname));
    }

    Binding binding;
    const unsigned char* text;

    text = sqlite3_column_text(stmt, 0);
    binding.idname = text ? reinterpret_cast<const char*>(text) : "";

    binding.lazy = sqlite3_column_int(stmt, 1) != 0;

    text = sqlite3_column_text(stmt, 2);
    binding.value = text ? reinterpret_cast<const char*>(text) : "";

    text = sqlite3_column_text(stmt, 3);
    binding.filename = text ? reinterpret_cast<const char*>(text) : "";

    sqlite3_finalize(stmt);
    return binding;
}

std::unordered_map<std::string, bool> BundleReader::get_dependencies(const std::string& idname) {
    std::unordered_map<std::string, bool> dependencies;
    get_dependencies_recursive(idname, dependencies);

    fmt::print("Dependencies for '{}' [\n", idname);
    for (const auto& [dep, is_lazy] : dependencies) {
        fmt::print("  {} (lazy: {})\n", dep, is_lazy);
    }
    fmt::print("]   \n");
    return dependencies;
}

void BundleReader::get_dependencies_recursive(
    const std::string& idname,
    std::unordered_map<std::string, bool>& dependencies) {

    // If we've already processed this dependency, skip it (prevents cycles).
    if (dependencies.count(idname)) {
        return;
    }

    // Look up the binding to determine if it's lazy.
    Binding binding = get_binding(idname);
    dependencies[idname] = binding.lazy;

    sqlite3_stmt* stmt = nullptr;

    const char* sql = "SELECT needs FROM depends_ons WHERE id_name = ?";
    int result = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    check_sqlite_result(result, "Failed to prepare depends_ons query");

    result = sqlite3_bind_text(stmt, 1, idname.c_str(), -1, SQLITE_TRANSIENT);
    check_sqlite_result(result, "Failed to bind parameter");

    std::vector<std::string> direct_deps;
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* needs = sqlite3_column_text(stmt, 0);
        if (needs) {
            direct_deps.emplace_back(reinterpret_cast<const char*>(needs));
        }
    }

    check_sqlite_result(result, "Failed to execute DependsOn query");
    sqlite3_finalize(stmt);

    // Recursively process each direct dependency.
    for (const auto& dep : direct_deps) {
        get_dependencies_recursive(dep, dependencies);
    }
}



} // namespace nutmeg

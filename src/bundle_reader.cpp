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

std::vector<std::string> BundleReader::get_dependencies(const std::string& idname) {
    std::vector<std::string> dependencies;
    sqlite3_stmt* stmt = nullptr;

    const char* sql = "SELECT needs FROM depends_ons WHERE id_name = ?";
    int result = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    check_sqlite_result(result, "Failed to prepare depends_ons query");

    result = sqlite3_bind_text(stmt, 1, idname.c_str(), -1, SQLITE_TRANSIENT);
    check_sqlite_result(result, "Failed to bind parameter");

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char* needs = sqlite3_column_text(stmt, 0);
        if (needs) {
            dependencies.emplace_back(reinterpret_cast<const char*>(needs));
        }
    }

    check_sqlite_result(result, "Failed to execute DependsOn query");
    sqlite3_finalize(stmt);

    return dependencies;
}

FunctionObject BundleReader::parse_function_object(const std::string& json_str) {
    try {
        json j = json::parse(json_str);

        FunctionObject func;
        func.nlocals = j.at("nlocals").get<int>();
        func.nparams = j.at("nparams").get<int>();

        for (const auto& inst_json : j.at("instructions")) {
            Instruction inst;
            inst.type = inst_json.at("type").get<std::string>();
            inst.opcode = string_to_opcode(inst.type);  // Map to opcode.

            // Optional fields.
            if (inst_json.contains("index")) {
                inst.index = inst_json.at("index").get<int>();
            }
            if (inst_json.contains("value")) {
                inst.value = inst_json.at("value").get<std::string>();
            }
            if (inst_json.contains("name")) {
                inst.name = inst_json.at("name").get<std::string>();
            }
            if (inst_json.contains("nargs")) {
                inst.nargs = inst_json.at("nargs").get<int>();
            }

            func.instructions.push_back(inst);
        }

        return func;
    } catch (const json::exception& e) {
        throw BundleReaderError(fmt::format("JSON parsing error: {}", e.what()));
    }
}

} // namespace nutmeg

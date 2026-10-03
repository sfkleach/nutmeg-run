#include <fmt/core.h>
#include <string>
#include <vector>
#include <optional>
#include <cstring>
#include <cstdlib>
#include <unordered_set>
#include <chrono>
#include "bundle_reader.hpp"
#include "machine.hpp"
#include "heap.hpp"
#include "trace.hpp"
#include "event_log.hpp"

struct CommandLineArgs {
    std::optional<std::string> entry_point;
    std::string bundle_file;
    std::vector<std::string> program_args;
};

// Display help information and exit successfully.
void print_help() {
    fmt::print("Usage: nutmeg-run [OPTIONS] BUNDLE_FILE [ARGUMENTS...]\n");
    fmt::print("\n");
    fmt::print("Execute a Nutmeg bundle file.\n");
    fmt::print("\n");
    fmt::print("Options:\n");
    fmt::print("  -h, --help              Display this help message and exit\n");
    fmt::print("  -e NAME, -e=NAME        Specify the entry point to invoke\n");
    fmt::print("  --entry-point NAME      Specify the entry point to invoke\n");
    fmt::print("  --entry-point=NAME      Specify the entry point to invoke\n");
    fmt::print("\n");
    fmt::print("Arguments:\n");
    fmt::print("  BUNDLE_FILE             The Nutmeg bundle file to execute (required)\n");
    fmt::print("  ARGUMENTS...            Arguments passed to the program\n");
    std::exit(EXIT_SUCCESS);
}

// Parse command-line arguments according to: nutmeg-run [OPTIONS] BUNDLE_FILE [ARGUMENTS...].
CommandLineArgs parse_args(int argc, char* argv[]) {
    CommandLineArgs args;
    int i = 1;

    // Parse options.
    while (i < argc) {
        std::string arg = argv[i];

        // Check for help option.
        if (arg == "-h" || arg == "--help") {
            print_help();
        }
        // Check for --entry-point=NAME (optional form).
        else if (arg.rfind("--entry-point=", 0) == 0) {
            args.entry_point = arg.substr(14);  // Length of "--entry-point=".
            i++;
        }
        // Check for --entry-point NAME (required form).
        else if (arg == "--entry-point") {
            if (i + 1 >= argc) {
                fmt::print(stderr, "Error: --entry-point option requires an argument\n");
                std::exit(EXIT_FAILURE);
            }
            args.entry_point = argv[i + 1];
            i += 2;
        }
        // Check for -e NAME (short form, must take a value).
        else if (arg == "-e") {
            if (i + 1 >= argc) {
                fmt::print(stderr, "Error: -e option requires an argument\n");
                std::exit(EXIT_FAILURE);
            }
            args.entry_point = argv[i + 1];
            i += 2;
        }
        // Check for -e=NAME (short form, may take a value).
        else if (arg.rfind("-e=", 0) == 0) {
            args.entry_point = arg.substr(3);  // Length of "-e=".
            i++;
        }
        // Stop at first non-option argument (the bundle file).
        else if (arg[0] != '-') {
            break;
        }
        else {
            fmt::print(stderr, "Error: Unknown option '{}'\n", arg);
            std::exit(EXIT_FAILURE);
        }
    }

    // Next argument is the bundle file (required).
    if (i >= argc) {
        fmt::print(stderr, "Error: Missing BUNDLE_FILE argument\n");
        fmt::print(stderr, "Usage: nutmeg-run [OPTIONS] BUNDLE_FILE [ARGUMENTS...]\n");
        fmt::print(stderr, "Options:\n");
        fmt::print(stderr, "  -e NAME, -e=NAME, --entry-point NAME, --entry-point=NAME\n");
        fmt::print(stderr, "                          Specify the entry point to invoke\n");
        std::exit(EXIT_FAILURE);
    }
    args.bundle_file = argv[i++];

    // Remaining arguments are passed to the program.
    while (i < argc) {
        args.program_args.push_back(argv[i++]);
    }

    return args;
}

int main(int argc, char* argv[]) {
    try {
        CommandLineArgs args = parse_args(argc, argv);

        // Open the bundle file.
        nutmeg::BundleReader reader(args.bundle_file);
        LOG_EVENT("bundle.open", nutmeg::ev_str("file", args.bundle_file));

        // Determine which entry point to use.
        std::string entry_point_name;
        if (args.entry_point) {
            entry_point_name = *args.entry_point;
        } else {
            // No entry point specified - get all and ensure there's exactly one.
            auto entry_points = reader.get_entry_points();
            if (entry_points.empty()) {
                fmt::print(stderr, "Error: No entry points found in bundle\n");
                return EXIT_FAILURE;
            }
            if (entry_points.size() > 1) {
                fmt::print(stderr, "Error: Multiple entry points found, please specify one with --entry-point:\n");
                for (const auto& ep : entry_points) {
                    fmt::print(stderr, "  {}\n", ep);
                }
                return EXIT_FAILURE;
            }
            entry_point_name = entry_points[0];
        }
        LOG_EVENT("entry.point", nutmeg::ev_str("name", entry_point_name),
                  nutmeg::ev_str("source", args.entry_point ? "option" : "bundle"));

        // Create the machine (initializes threaded interpreter).
        nutmeg::Machine machine;

        // Load all bindings transitively from the entry point.
        if constexpr (nutmeg::TRACE_MAIN) {
            fmt::print("Loading entry point: {}\n", entry_point_name);
        }
        std::unordered_map<std::string, bool> deps = reader.get_dependencies(entry_point_name);
        nutmeg::Cell undef = nutmeg::SPECIAL_UNDEF;
        for (const auto& id_lazy : deps) {
            // Each dependency should be declared as a global variable with an undefined value.
            machine.define_global(id_lazy.first, undef, false);
            if constexpr (nutmeg::TRACE_MAIN) {
                fmt::print("  Found dependency: {}\n", id_lazy.first);
            }
        }

        for (const auto& id_lazy : deps) {
            if constexpr (nutmeg::TRACE_MAIN) {
                fmt::print("  Dependency: {}\n", id_lazy.first);
            }
            nutmeg::Binding binding = reader.get_binding(id_lazy.first);
            nutmeg::FunctionObject func = machine.parse_function_object(id_lazy.first, deps, binding.value);
            nutmeg::Cell* func_obj = machine.allocate_function(func.code, func.nlocals, func.nparams);
            machine.define_global(id_lazy.first, make_tagged_ptr(func_obj), binding.lazy);
            LOG_EVENT("load.binding", nutmeg::ev_str("name", id_lazy.first),
                      nutmeg::ev_bool("lazy", binding.lazy),
                      nutmeg::ev_int("instructions", func.code.size()),
                      nutmeg::ev_int("nlocals", func.nlocals), nutmeg::ev_int("nparams", func.nparams),
                      nutmeg::ev_ptr("address", func_obj));
            if constexpr (nutmeg::TRACE_MAIN) {
                fmt::print("  Loaded func_object {}\n", static_cast<void*>(func_obj));
                fmt::print("  Recovering func object: {}\n", as_detagged_ptr(make_tagged_ptr(func_obj)));
            }
        }
        if constexpr (nutmeg::TRACE_MAIN) {
            fmt::print("All dependencies loaded.\n");
        }

        // Get the entry point function and execute it.
        nutmeg::Cell* entry_func_ptr = machine.get_global_cell_ptr(entry_point_name);
        if constexpr (nutmeg::TRACE_MAIN) {
            fmt::print("Recovered func_object {}\n", static_cast<void*>(entry_func_ptr));
        }

        // Measure execution time.
        auto start_time = std::chrono::high_resolution_clock::now();
        machine.execute(entry_func_ptr);
        auto end_time = std::chrono::high_resolution_clock::now();

        if constexpr (nutmeg::TRACE_TIMES) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            fmt::print("Execution time: {}.{:06d} seconds\n",
                       duration.count() / 1000000,
                       duration.count() % 1000000);
        }

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        fmt::print(stderr, "Error: {}\n", e.what());
        return EXIT_FAILURE;
    }
}

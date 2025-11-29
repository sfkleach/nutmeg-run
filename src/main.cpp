#include <fmt/core.h>
#include <string>
#include <vector>
#include <optional>
#include <cstring>

struct CommandLineArgs {
    std::optional<std::string> entry_point;
    std::string bundle_file;
    std::vector<std::string> program_args;
};

// Parse command-line arguments according to: nutmeg-run [OPTIONS] BUNDLE_FILE [ARGUMENTS...].
CommandLineArgs parse_args(int argc, char* argv[]) {
    CommandLineArgs args;
    int i = 1;

    // Parse options.
    while (i < argc) {
        std::string arg = argv[i];
        
        // Check for --entry-point=NAME.
        if (arg.rfind("--entry-point=", 0) == 0) {
            args.entry_point = arg.substr(14);  // Length of "--entry-point=".
            i++;
        }
        // Check for -e NAME (short form).
        else if (arg == "-e") {
            if (i + 1 >= argc) {
                fmt::print(stderr, "Error: -e option requires an argument\n");
                std::exit(1);
            }
            args.entry_point = argv[i + 1];
            i += 2;
        }
        // Stop at first non-option argument (the bundle file).
        else if (arg[0] != '-') {
            break;
        }
        else {
            fmt::print(stderr, "Error: Unknown option '{}'\n", arg);
            std::exit(1);
        }
    }

    // Next argument is the bundle file (required).
    if (i >= argc) {
        fmt::print(stderr, "Error: Missing BUNDLE_FILE argument\n");
        fmt::print(stderr, "Usage: nutmeg-run [OPTIONS] BUNDLE_FILE [ARGUMENTS...]\n");
        fmt::print(stderr, "Options:\n");
        fmt::print(stderr, "  -e NAME, --entry-point=NAME  Specify the entry point to invoke\n");
        std::exit(1);
    }
    args.bundle_file = argv[i++];

    // Remaining arguments are passed to the program.
    while (i < argc) {
        args.program_args.push_back(argv[i++]);
    }

    return args;
}

int main(int argc, char* argv[]) {
    CommandLineArgs args = parse_args(argc, argv);
    
    fmt::print("Bundle file: {}\n", args.bundle_file);
    if (args.entry_point) {
        fmt::print("Entry point: {}\n", *args.entry_point);
    } else {
        fmt::print("Entry point: (default)\n");
    }
    fmt::print("Program arguments: {}\n", args.program_args.size());
    for (const auto& arg : args.program_args) {
        fmt::print("  {}\n", arg);
    }
    
    return 0;
}

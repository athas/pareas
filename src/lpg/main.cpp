#include "pareas/lpg/parser/grammar.hpp"
#include "pareas/lpg/parser/grammar_parser.hpp"
#include "pareas/lpg/parser/terminal_set_functions.hpp"
#include "pareas/lpg/parser/ll/generator.hpp"
#include "pareas/lpg/parser/llp/generator.hpp"
#include "pareas/lpg/parser/llp/render.hpp"
#include "pareas/lpg/error_reporter.hpp"
#include "pareas/lpg/parser.hpp"
#include "pareas/lpg/cli_util.hpp"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <iostream>
#include <fstream>
#include <string_view>
#include <string>
#include <iterator>
#include <cstdlib>

namespace {
    struct Options {
        const char* input_path;
        const char* output_path;
        bool help;
        bool verbose_grammar;
        bool verbose_sets;
        bool verbose_psls;
        bool verbose_ll;
        bool verbose_llp;
    };

    void print_usage(const char* progname) {
        fmt::print(
            "Usage: {} [options...] <input path>\n"
            "Available options:\n"
            "-o --output <path>  Write the output to <output path>. (default: stdout)\n"
            "--verbose-grammar   Dump parsed grammar to stderr\n"
            "--verbose-sets      Dump first/last/follow/before sets to stderr\n"
            "--verbose-psls      Dump PSLS table to stderr\n"
            "--verbose-ll        Dump LL table to stderr\n"
            "--verbose-llp       Dump LLP table to stderr\n"
            "-h --help           Show this message and exit\n"
            "\n"
            "When <input path> and/or <output path> are '-', standard input and standard\n"
            "output are used respectively.\n",
            progname
        );
    }

    bool parse_options(Options* opts, int argc, const char* argv[]) {
        *opts = {
            .input_path = nullptr,
            .output_path = "-",
            .help = false,
            .verbose_grammar = false,
            .verbose_sets = false,
            .verbose_psls = false,
            .verbose_ll = false,
            .verbose_llp = false,
        };

        for (int i = 1; i < argc; ++i) {
            auto arg = std::string_view(argv[i]);

            if (arg == "-o" || arg == "--output") {
                if (++i >= argc) {
                    fmt::print(std::cerr, "Error: Expected argument <path> to option {}\n", arg);
                    return false;
                }
                opts->output_path = argv[i];
            } else if (arg == "--help" || arg == "-h") {
                opts->help = true;
            } else if (arg == "--verbose-grammar") {
                opts->verbose_grammar = true;
            } else if (arg == "--verbose-sets") {
                opts->verbose_sets = true;
            } else if (arg == "--verbose-psls") {
                opts->verbose_psls = true;
            } else if (arg == "--verbose-ll") {
                opts->verbose_ll = true;
            } else if (arg == "--verbose-llp") {
                opts->verbose_llp = true;
            } else if (!opts->input_path) {
                opts->input_path = argv[i];
            } else {
                fmt::print("Error: Unknown option {}\n", arg);
                return false;
            }
        }

        if (opts->help)
            return true;

        if (!opts->input_path) {
            fmt::print(std::cerr, "Error: Missing required argument <input path>\n");
            return false;
        }

        return true;
    }
}

int main(int argc, const char* argv[]) {
    Options opts;
    if (!parse_options(&opts, argc, argv)) {
        fmt::print(std::cerr, "See '{} --help' for usage\n", argv[0]);
        return EXIT_FAILURE;
    } else if (opts.help) {
        print_usage(argv[0]);
    }

    std::string input;
    if (auto maybe_input = pareas::read_input(opts.input_path)) {
        input = std::move(maybe_input.value());
    } else {
        fmt::print(std::cerr, "Error: Failed to open input path '{}'\n", opts.input_path);
        return EXIT_FAILURE;
    }

    auto er = pareas::ErrorReporter(input, std::clog);

    try {
        auto parser = pareas::Parser(&er, input);
        auto grammar_parser = pareas::parser::GrammarParser(&parser);
        auto g = grammar_parser.parse();

        if (opts.verbose_grammar)
            g.dump(std::clog);

        auto tsf = pareas::parser::TerminalSetFunctions(g);
        if (opts.verbose_sets)
            tsf.dump(std::clog);

        auto gen = pareas::parser::llp::Generator(&er, &g, &tsf);

        auto psls_table = gen.build_psls_table();
        if (opts.verbose_psls)
            psls_table.dump_csv(std::clog);

        auto ll_table = pareas::parser::ll::Generator(&er, &g, &tsf).build_parsing_table();
        if (opts.verbose_ll)
            ll_table.dump_csv(std::clog);

        auto llp_table = gen.build_parsing_table(ll_table, psls_table);
        if (opts.verbose_llp)
            llp_table.dump_csv(std::clog);

        if (std::string_view(opts.output_path) == "-") {
            pareas::parser::llp::render_parser(std::cout, g, llp_table);
        } else {
            auto out = std::ofstream(opts.output_path, std::ios::binary);
            if (!out) {
                fmt::print(std::cerr, "Error: Failed to open output path '{}'\n", opts.output_path);
                return EXIT_FAILURE;
            }
            pareas::parser::llp::render_parser(out, g, llp_table);
        }
    } catch (const pareas::parser::InvalidGrammarError& e) {
        fmt::print(std::cerr, "Failed: {}\n", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

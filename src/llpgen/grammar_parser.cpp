#include "pareas/llpgen/grammar_parser.hpp"

#include <fmt/ostream.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace {
    bool is_word_start_char(int c) {
        return std::isalpha(c) || c == '_';
    }

    bool is_word_continue_char(int c) {
        return std::isalnum(c) || c == '_';
    }
}

namespace pareas {
    GrammarParser::GrammarParser(ErrorReporter* er, std::string_view source):
        parser(er, source),
        start{"", {0}}, left_delim{"", {0}}, right_delim{"", {0}} {}

    Grammar GrammarParser::parse() {
        bool error = false;

        this->eat_delim();
        int c;
        while ((c = this->parser.peek()) != EOF) {
            bool ok = c == '%' ? this->directive() : this->production();
            if (!ok) {
                error = true;
                this->skip_statement();
            }
            this->eat_delim();
        }

        if (this->start.value.size() == 0) {
            this->parser.er->error(this->parser.loc(), "Missing directive %start");
            error = true;
        }

        if (this->left_delim.value.size() == 0) {
            this->parser.er->error(this->parser.loc(), "Missing directive %left_delim");
            error = true;
        }

        if (this->right_delim.value.size() == 0) {
            this->parser.er->error(this->parser.loc(), "Missing directive %right_delim");
            error = true;
        }

        const auto* start = this->find_start_rule();

        if (error || !start)
            throw GrammarParseError();

        auto g = Grammar{
            .left_delim = Terminal{std::string(this->left_delim.value)},
            .right_delim = Terminal{std::string(this->right_delim.value)},
            .start = start,
            .productions = std::move(this->productions),
        };
        g.validate(*this->parser.er);
        return g;
    }

    const Production* GrammarParser::find_start_rule() const {
        // Find the start rule
        // Only one is allowed
        const Production* start = nullptr;
        bool error = false;

        auto start_nt = NonTerminal{std::string(this->start.value)};

        for (const auto& prod : this->productions) {
            if (prod.lhs != start_nt)
                continue;
            if (start) {
                this->parser.er->error(prod.loc, "Duplicate start rule definition");
                this->parser.er->note(start->loc, "First defined here");
                error = true;
            } else {
                start = &prod;
            }
        }

        if (!start) {
            this->parser.er->error(this->parser.loc(), "Missing start rule");
            return nullptr;
        }

        auto left_delim = Terminal{std::string(this->left_delim.value)};
        auto right_delim = Terminal{std::string(this->right_delim.value)};

        // Verify that the starting rule is of the right form
        if (start->rhs.empty() || start->rhs.front() != left_delim || start->rhs.back() != right_delim) {
            this->parser.er->error(start->loc, "Start rule not in correct form");
            this->parser.er->note(start->loc, fmt::format("Expected form {} -> '{}' ... '{}';", start->lhs, left_delim, right_delim));
            error = true;
        }

        if (error) {
            return nullptr;
        }

        return start;
    }

    bool GrammarParser::eat_delim() {
        // Eat any delimiter, such as whitespace and comments
        bool delimited = false;

        while (true) {
            int c = this->parser.peek();
            switch (c) {
                case ' ':
                case '\t':
                case '\r':
                case '\n':
                    this->parser.consume();
                    break;
                case '#':
                    while (this->parser.peek() != '\n' && this->parser.peek() != EOF)
                        this->parser.consume();
                    break;
                default:
                    return delimited;
            }
            delimited = true;
        }
    }

    void GrammarParser::skip_statement() {
        while (true) {
            this->eat_delim(); // make sure to skip comments
            int c = this->parser.consume();
            if (c == EOF || c == ';')
                break;
        }
    }

    bool GrammarParser::directive() {
        auto directive_loc = this->parser.loc();
        if (!this->parser.expect('%'))
            return false;
        auto name = this->word();
        if (name.size() == 0)
            return false;

        Directive* dir = nullptr;
        bool word = false;
        if (name == "start") {
            dir = &this->start;
            word = true;
        } else if (name == "left_delim") {
            dir = &this->left_delim;
        } else if (name == "right_delim") {
            dir = &this->right_delim;
        } else {
            this->parser.er->error(directive_loc, fmt::format("Invalid directive '%{}'", name));
            return false;
        }

        bool error = false;
        if (dir->value.size() != 0) {
            this->parser.er->error(directive_loc, fmt::format("Duplicate directive '%{}'", name));
            this->parser.er->note(dir->loc, "First defined here");
            error = true;
        } else {
            dir->loc = directive_loc;
        }

        this->eat_delim();
        if (!this->parser.expect('='))
            return false;
        this->eat_delim();

        auto value = word ? this->word() : this->terminal();
        if (value.size() == 0)
            return false;

        dir->value = value;
        this->eat_delim();
        return this->parser.expect(';') && !error;
    }

    bool GrammarParser::production() {
        auto lhs_loc = this->parser.loc();
        auto lhs = this->word();
        if (lhs.size() == 0)
            return false;

        this->eat_delim();

        auto tag_loc = lhs_loc;
        auto tag = lhs;
        if (this->parser.peek() == '[') {
            tag_loc = this->parser.loc();
            tag = this->tag();
            if (tag.size() == 0)
                return false;

            this->eat_delim();
        }

        if (!this->parser.expect('-') || !this->parser.expect('>'))
            return false;

        this->eat_delim();

        auto syms = std::vector<Symbol>();
        bool delimited = true;

        while (true) {
            int c = this->parser.peek();
            auto sym_loc = this->parser.loc();
            if (c == '\'') {
                auto t = this->terminal();
                if (t.size() == 0)
                    return false;
                syms.push_back(Terminal{std::string(t)});
            } else if (is_word_start_char(c)) {
                auto nt = this->word();
                if (nt.size() == 0)
                    return false;
                syms.push_back(NonTerminal{std::string(nt)});
            } else {
                break;
            }

            if (!delimited) {
                this->parser.er->error(sym_loc, "Delimiter required between production RHS symbols");
                return false;
            }

            delimited = this->eat_delim();
        }

        if (!this->parser.expect(';'))
            return false;

        auto it = this->tags.find(tag);
        if (it == this->tags.end()) {
            this->tags.insert(it, {tag, tag_loc});
        } else {
            this->parser.er->error(tag_loc, fmt::format("Duplicate tag '{}'", tag));
            this->parser.er->note(it->second, "First defined here");
            return false;
        }

        this->productions.push_back({lhs_loc, std::string(tag), NonTerminal{std::string(lhs)}, syms});
        return true;
    }

    std::string_view GrammarParser::word() {
        bool error = false;
        size_t start = this->parser.offset;
        int c = this->parser.peek();

        if (!is_word_start_char(c)) {
            this->parser.er->error(this->parser.loc(), fmt::format(
                "Invalid character '{}', expected <word>",
                static_cast<char>(c)
            ));
            error = true;
        }

        this->parser.consume();

        c = this->parser.peek();
        while (is_word_continue_char(c)) {
            this->parser.consume();
            c = this->parser.peek();
        }

        if (error)
            return "";

        return this->parser.source.substr(start, this->parser.offset - start);
    }

    std::string_view GrammarParser::terminal() {
        if (!this->parser.expect('\''))
            return "";

        auto word = this->word();
        if (word.size() == 0)
            return "";

        if (!this->parser.expect('\''))
            return "";

        return word;
    }

    std::string_view GrammarParser::tag() {
        if (!this->parser.expect('['))
            return "";

        auto word = this->word();
        if (word.size() == 0)
            return "";

        if (!this->parser.expect(']'))
            return "";

        return word;
    }
}

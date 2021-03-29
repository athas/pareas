#include "pareas/lpg/parser/llp/render.hpp"
#include "pareas/lpg/parser/llp/admissible_pair.hpp"
#include "pareas/lpg/render_util.hpp"

#include <fmt/ostream.h>

#include <bit>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cassert>

namespace {
    using namespace pareas;
    using namespace pareas::parser;
    using namespace pareas::parser::llp;

    struct String {
        int32_t offset;
        int32_t size;
    };

    template <typename T>
    struct StringTable {
        std::vector<T> superstring;
        std::unordered_map<AdmissiblePair, String, AdmissiblePair::Hash> strings;

        template <typename F>
        StringTable(const ParsingTable& pt, F get_string);

        void render(
            std::ostream& out,
            const std::string& base_name,
            const std::string& table_type,
            const TokenMapping* tm
        );
    };

    template <typename T>
    template <typename F>
    StringTable<T>::StringTable(const ParsingTable& pt, F get_string) {
        // Simple implementation for now
        int32_t offset = 0;
        for (const auto& [ap, entry] : pt.table) {
            auto string = get_string(entry);
            this->superstring.insert(superstring.end(), string.begin(), string.end());
            this->strings[ap] = {offset, static_cast<int32_t>(string.size())};
            offset += string.size();
        }
    }

    template <typename T>
    void StringTable<T>::render(
        std::ostream& out,
        const std::string& base_name,
        const std::string& table_type,
        const TokenMapping* tm
    ) {
        fmt::print(out, "let {}_table_size: i64 = {}\n", base_name, this->superstring.size());
        fmt::print(out, "let {}_table = [", base_name);

        bool first = true;
        for (auto val : this->superstring) {
            fmt::print(out, "{}{}", first ? first = false, "" : ", ", val);
        }
        fmt::print(out, "] :> [{}_table_size]{}\n", base_name, table_type);

        size_t n_tokens = tm->num_tokens();
        auto stringrefs = std::vector<std::vector<String>>(
            n_tokens,
            std::vector<String>(n_tokens, {-1, -1})
        );

        for (const auto& [ap, string] : this->strings) {
            auto i = tm->token_id(ap.x.as_token());
            auto j = tm->token_id(ap.y.as_token());
            stringrefs[i][j] = string;
        }

        fmt::print(out, "let {0}_refs = [\n    ", base_name);
        bool outer_first = true;
        for (const auto& v : stringrefs) {
            if (outer_first)
                outer_first = false;
            else
                fmt::print(out, ",\n    ");

            fmt::print(out, "[");
            bool inner_first = true;
            for (const auto& [offset, size] : v) {
                if (inner_first)
                    inner_first = false;
                else
                    fmt::print(out, ", ");
                fmt::print(out, "({}, {})", offset, size);
            }
            fmt::print(out, "]");
        }

        fmt::print(out, "\n] :> [num_tokens][num_tokens](i{0}, i{0})\n", ParserRenderer::TABLE_OFFSET_BITS);
    }
}

namespace pareas::parser::llp {
    ParserRenderer::ParserRenderer(Renderer* r, const TokenMapping* tm, const Grammar* g, const ParsingTable* pt):
        r(r), tm(tm), g(g), pt(pt) {

        for (const auto& [ap, entry] : this->pt->table) {
            for (const auto& sym : entry.initial_stack)
                this->symbol_mapping.insert({sym, this->symbol_mapping.size()});
            for (const auto& sym : entry.final_stack)
                this->symbol_mapping.insert({sym, this->symbol_mapping.size()});
        }
    }

    void ParserRenderer::render() const {
        this->render_productions();

        fmt::print(
            this->r->hpp,
            "using Bracket = uint{}_t;\n"
            "template <typename T>\n"
            "struct StrTab {{\n"
            "    size_t n;\n"
            "    const T* table; // n\n"
            "    const uint32_t* offsets; // NUM_TOKENS\n"
            "    const uint32_t* lengths; // NUM_TOKENS\n"
            "}};\n"
            "extern const StrTab<Bracket> stack_change_table;\n"
            "extern const StrTab<Production> parse_table;\n",
            this->bracket_backing_bits()
        );

        this->render_production_arity_data();
        this->render_stack_change_table();
        this->render_parse_table();
    }

    size_t ParserRenderer::bracket_id(const Symbol& sym, bool left) const {
        auto id = this->symbol_mapping.at(sym);

        // Left brackets get odd ID's, rightb rackets get even ID's.
        // This way, we can perform a simply subtract and reduce by bit and to
        // check if all the brackets match up.
        return left ? id * 2 + 1 : id * 2;
    }

    size_t ParserRenderer::bracket_backing_bits() const {
        return pareas::int_bit_width(2 * this->symbol_mapping.size());
    }

    void ParserRenderer::render_productions() const {
        auto n = this->g->productions.size();
        auto bits = pareas::int_bit_width(n);

        fmt::print(this->r->fut, "module production = u{}\n", bits);

        fmt::print(this->r->hpp, "enum class Production : uint{}_t {{\n", bits);

        fmt::print(this->r->cpp, "const char* production_name(Production p) {{\n");
        fmt::print(this->r->cpp, "    switch (p) {{\n");

        // Tags are already guaranteed to be unique, so we don't need to do any kind
        // of deduplication here. As added bonus, the ID of a production is now only
        // dependent on the order in which the productions are defined.
        for (size_t id = 0; id < n; ++id) {
            const auto& tag = this->g->productions[id].tag;

            auto tag_upper = tag;
            std::transform(tag_upper.begin(), tag_upper.end(), tag_upper.begin(), ::toupper);

            fmt::print(this->r->fut, "let production_{}: production.t = {}\n", tag, id);

            fmt::print(this->r->hpp, "    {} = {},\n", tag_upper, id);

            fmt::print(this->r->cpp, "        case Production::{}: return \"{}\";\n", tag_upper, tag);
        }

        fmt::print(this->r->fut, "let num_productions: i64 = {}\n", n);

        fmt::print(this->r->hpp, "}};\n");
        fmt::print(this->r->hpp, "constexpr const size_t NUM_PRODUCTIONS = {};\n", n);
        fmt::print(this->r->hpp, "const char* production_name(Production p);\n");

        fmt::print(this->r->cpp, "    }}\n}}\n");
    }

    void ParserRenderer::render_production_arity_data() const {
        this->r->align_data(sizeof(uint32_t));
        auto offset = this->r->data_offset();

        fmt::print(this->r->hpp, "extern const uint32_t* arities; // NUM_PRODUCTIONS\n");

        fmt::print(this->r->cpp, "const uint32_t* arities = {};\n", this->r->render_offset_cast(offset, "uint32_t"));

        // Production id's are assigned according to their index in the
        // productions vector, so we can just push_back the arities.
        for (const auto& prod : this->g->productions) {
            this->r->write_data_int(prod.arity(), sizeof(uint32_t));
        }
    }

    void ParserRenderer::render_stack_change_table() const {
        auto strtab = StringTable<size_t>(
            *this->pt,
            [&](const ParsingTable::Entry& entry) {
                auto result = std::vector<size_t>();

                for (auto it = entry.initial_stack.rbegin(); it != entry.initial_stack.rend(); ++it) {
                    result.push_back(this->bracket_id(*it, false));
                }

                for (auto it = entry.final_stack.begin(); it != entry.final_stack.end(); ++it) {
                    result.push_back(this->bracket_id(*it, true));
                }

                return result;
            }
        );

        size_t bracket_bits = this->bracket_backing_bits();
        fmt::print(this->r->fut, "module bracket = u{}\n", bracket_bits);
        strtab.render(this->r->fut, "stack_change", fmt::format("u{}", bracket_bits), this->tm);
    }

    void ParserRenderer::render_parse_table() const {
           auto strtab = StringTable<std::string>(
            *this->pt,
            [&](const ParsingTable::Entry& entry) {
                auto result = std::vector<std::string>();
                for (const auto* prod : entry.productions)
                    result.push_back(fmt::format("production_{}", prod->tag));
                return result;
            }
        );
        strtab.render(this->r->fut, "parse", "production.t", this->tm);
    }
}

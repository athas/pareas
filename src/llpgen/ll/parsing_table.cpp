#include "pareas/llpgen/ll/parsing_table.hpp"
#include "pareas/llpgen/hash_util.hpp"

#include <fmt/ostream.h>

#include <unordered_set>

namespace pareas::ll {
        size_t State::Hash::operator()(const State& key) const {
        return hash_combine(std::hash<NonTerminal>{}(key.stack_top), std::hash<Terminal>{}(key.lookahead));
    }

    bool operator==(const State& lhs, const State& rhs) {
        return lhs.stack_top == rhs.stack_top && lhs.lookahead == rhs.lookahead;
    }

    std::vector<const Production*> ParsingTable::partial_parse(const Terminal& y, std::vector<Symbol>& stack) const {
        // TODO: Convert asserts to errors

        auto productions = std::vector<const Production*>();
        while (true) {
            assert(!stack.empty());
            auto top = stack.back();
            stack.pop_back();

            if (top.is_null()) {
                continue;
            } else if (top.is_terminal) {
                assert(y == top.as_terminal());
                break;
            }

            auto nt = top.as_non_terminal();

            auto it = this->table.find({nt, y});
            assert(it != this->table.end());
            productions.push_back(it->second);
            const auto& to_push = it->second->rhs;

            stack.insert(stack.end(), to_push.rbegin(), to_push.rend());
        }

        return productions;
    }

    void ParsingTable::dump_csv(std::ostream& os) const {
        auto nts = std::unordered_set<NonTerminal>();
        auto ts = std::unordered_set<Terminal>();

        for (const auto& [state, prod] : this->table) {
            const auto& [nt, t] = state;
            nts.insert(nt);
            ts.insert(t);
        }

        for (const auto& t : ts) {
            fmt::print(os, ",{}", t);
        }
        fmt::print(os, "\n");

        for (const auto& nt : nts) {
            fmt::print(os, "{}", nt);
            // Hope that this iterates in the same order
            for (const auto& t : ts) {
                fmt::print(os, ",");
                auto it = this->table.find({nt, t});
                if (it == this->table.end())
                    continue;

                fmt::print(os, "\"{}\"", *it->second);
            }
            fmt::print(os, "\n");
        }
    }
}

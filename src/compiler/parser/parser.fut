import "string_packing"
import "bracket_matching"
import "../util"
module bt = import "binary_tree"

module type grammar = {
    module production: integral
    val num_productions: i64
    val production_arity: [num_productions]i32

    module token: integral
    val num_tokens: i64

    module bracket: integral
    module stack_change_offset: integral
    val stack_change_table_size: i64
    val stack_change_table: [stack_change_table_size]bracket.t
    val stack_change_refs: [num_tokens][num_tokens](stack_change_offset.t, stack_change_offset.t)

    module parse_offset: integral
    val parse_table_size: i64
    val parse_table: [parse_table_size]production.t
    val parse_refs: [num_tokens][num_tokens](parse_offset.t, parse_offset.t)
}

module parser (g: grammar) = {
    let is_open_bracket (b: g.bracket.t) =
        (g.bracket.to_i64 b) % 2 == 1

    let is_bracket_pair (a: g.bracket.t) (b: g.bracket.t) =
        (g.bracket.to_i64 a) - (g.bracket.to_i64 b) == 1

    -- For now expected to include soi and eoi
    let check [n] (input: [n]g.token.t): bool =
        -- Evaluate the RBR/LBR functions for each pair of input tokens
        -- RBR(a) and LBR(w^R) are pre-concatenated by the parser generator
        let (offsets, lens) =
            in_windows_of_pairs input
            |> map (\(a, b) -> copy g.stack_change_refs[g.token.to_i64 a, g.token.to_i64 b])
            |> unzip
        -- Check whether all the values are valid (not -1)
        let bracket_refs_valid = offsets |> map g.stack_change_offset.to_i64 |> all (>= 0)
        -- Early return if there is an error
        in if !bracket_refs_valid then false else
        -- Extract the stack changes from the grammar
        pack_nonempty_strings
            g.stack_change_table
            (map g.stack_change_offset.to_i64 offsets)
            (map g.stack_change_offset.to_i64 lens)
        -- Check whether the stack changes match up
        |> check_brackets_bt -- or use check_brackets_radix
            is_open_bracket
            is_bracket_pair

    -- For now expected to include soi and eoi
    -- Input is expected to be `check`ed at this point. If its not valid according to `check`,
    -- this function might produce invalid results.
    let parse [n] (input: [n]g.token.t): []g.production.t =
        let (offsets, lens) =
            in_windows_of_pairs input
            |> map (\(a, b) -> copy g.parse_refs[g.token.to_i64 a, g.token.to_i64 b])
            |> unzip
        in pack_strings
            g.parse_table
            (map g.parse_offset.to_i64 offsets)
            (map g.parse_offset.to_i64 lens)

    -- Given a parse, as generated by the `parse` function, build a parent vector. For each
    -- production in the parse, the related index in the parent vector points to the production
    -- which produced it.
    let build_parent_vector [n] (parse: [n]g.production.t) =
        let tree =
            parse
            -- Get the arity (the number of nonterminals in its RHS; its number of children
            -- in the parse tree) of each production.
            |> map (\p -> g.production_arity[g.production.to_i64 p])
            -- Map it to a stack change: Every production would pop itself (1 value) and push
            -- its children (number of children). Thus, final stack change is #children - 1.
            |> map (+ -1)
            -- Calculate the depth
            |> scan (+) 0
            -- The depth is inclusive, shift it over to make them exclusive
            |> rotate (-1)
            |> map2 (\i x -> if i == 0 then 0i32 else x) (iota n)
            -- We are going to find the parent of each node using a previous-smaller-or-equal
            -- scan, which requires a binary tree. Build the binary tree.
            |> bt.construct i32.min i32.highest
        -- For each node, look up its parent by finding the index of the previous
        -- smaller or equal depth.
        in iota n
        |> map i32.i64
        |> map (bt.find_psev tree)
}

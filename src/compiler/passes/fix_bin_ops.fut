import "util"
import "../../../gen/pareas_grammar"

-- Node types which are list intermediates (may appear in list, but neither start nor end).
local let is_list_intermediate = mk_production_mask [
        production_logical_or_list,

        production_logical_and_list,

        production_rela_eq,
        production_rela_neq,
        production_rela_gt,
        production_rela_gte,
        production_rela_lt,
        production_rela_lte,

        production_bitwise_and,
        production_bitwise_or,
        production_bitwise_xor,

        production_shift_lr,
        production_shift_ar,
        production_shift_ll,

        production_sum_add,
        production_sum_sub,

        production_prod_mul,
        production_prod_div,
        production_prod_mod
    ]

-- Node types which are list ends.
local let is_list_end = mk_production_mask [
        production_logical_or_end,
        production_logical_and_end,
        production_rela_end,
        production_bitwise_end,
        production_shift_end,
        production_sum_end,
        production_prod_end
    ]

-- A mapping of node types to an expression type, nodes which may appear in the same expression
-- list need to have the same type. Otherwise, these values are arbitrary.
-- The value of 0 is reserved for nodes which are not equal to eachother.
local let not_in_list = 0i32
local let expr_list_type = mk_production_array not_in_list [
        (production_logical_or_list, 1),
        (production_logical_or_end, 1),

        (production_logical_and_list, 2),
        (production_logical_and_end, 2),

        (production_rela_eq, 3),
        (production_rela_neq, 3),
        (production_rela_gt, 3),
        (production_rela_gte, 3),
        (production_rela_lt, 3),
        (production_rela_lte, 3),
        (production_rela_end, 3),

        (production_bitwise_and, 4),
        (production_bitwise_xor, 4),
        (production_bitwise_or, 4),
        (production_bitwise_end, 4),

        (production_shift_lr, 5),
        (production_shift_ar, 5),
        (production_shift_ll, 5),
        (production_shift_end, 5),

        (production_sum_add, 6),
        (production_sum_sub, 6),
        (production_sum_end, 6),

        (production_prod_mul, 7),
        (production_prod_div, 7),
        (production_prod_mod, 7),
        (production_prod_end, 7)
    ]

-- | This pass processes expression lists into a better form.
-- List start out of the form
--    X
--    |
--    sum
--   / \
--  A   sum_add
--     / \
--    B   sum_add
--       / \
--      C   sum_end
-- for the an expression like `A + B + C`.
-- These lists need to be rotated to form the proper expression tree, so
-- that they represent `(A + B) + C` instead of `A + (B + C)`. This is done
-- in a few steps. First, the proper parent of the expression lists' children
-- are computed by moving the parent of all of the children except the first
-- up one element:
--    X
--    |
--    sum
--   /|\
--  A | sum_add
--    | |\
--    B | sum_add
--      |  \
--      C   sum_end
-- Next, the types of the list intermediate and end nodes is shifted one up,
-- and the original list end is removed (by making itself its own parent)
--    X
--    |
--    sum_add
--   /|\
--  A | sum_add
--    | |\
--    B | sum_end
--      |
--      C   sum_end
-- Next, for each of the ends which havent been removed, compute the
-- parent of the list:
--    X--------
--    |        \
--    sum_add   |
--   /|\        |
--  A | sum_add |
--    | |       |
--    B | sum_end
--      |
--      C   sum_end
-- Next*, invert the direction of the parents between the list intermediates.
--        X
--        |
--        sum_end
--       /
--      sum_add
--     /      \
--    sum_add  C
--   / \
--  A   B
--         sum_end
-- Finally, the intermediate ends are removed. This also removes
-- ends of other lists which have no elements:
--      X   sum_end
--      |
--      sum_add
--     /      \
--    sum_add  C
--   / \
--  A   B
--         sum_end
-- (*) Note that this step will mess up the general pre-order layout of the tree.
-- It still holds that left childs will have a lower index than right childs,
-- however, children will no longer have a higher ID than their parents.
-- Consider a tree like:
--    0
--   / \
--  1   2
--     / \
--    3   4
--       / \
--      5   6 <- list end
-- This will be transformed in:
--      2
--     / \
--    0   5
--   / \
--  1   3
let fix_bin_ops [n] (types: [n]production.t) (parents: [n]i32) =
    -- First, move all the parent pointers of nodes that point to list intermediates one up.
    let new_parents =
        iota n
        |> map
            (\i ->
                !is_list_end[production.to_i64 types[i]]
                && !is_list_intermediate[production.to_i64 types[i]]
                && parents[i] != -1
                && is_list_intermediate[production.to_i64 types[parents[i]]])
        |> map2
            (\parent im -> if im then parents[parent] else parent)
            parents
        -- Also remove old list ends - these should have no children so removing them should be cheap
        |> map2
            (\i parent -> if is_list_end[production.to_i64 types[i]] then i else parent)
            (iota n |> map i32.i64)
    -- Compute new nodes by moving all list intermediate/end nodes one up.
    let new_types =
        let is =
            types
            |> map production.to_i64
            |> map (\node -> is_list_intermediate[node] || is_list_end[node])
            -- Generate the new nodes list simply by scatterin To avoid a filter here, simply set the scatter target
            -- index of a node that shouldn't be moved up to out of bounds, which scatter will ignore for us.
            --
            -- It does not really matter to use the new or old parents array here.
            |> map2 (\parent is_tail_node -> if is_tail_node then parent else -1) parents
            |> map i64.i32
        in scatter
            (copy types)
            is
            types
    let expr_type = map (\node -> expr_list_type[production.to_i64 node]) new_types
    -- Compute whether this node's expression type is the same as that of the parent - and thus whether their
    -- pointers need to be flipped.
    let same_type_as_parent =
        iota n
        |> map i32.i64
        -- Also remove those old list ends here so we don't get any problems down the line.
        |> map (\i -> new_parents[i] != i && new_parents[i] != -1 && expr_type[i] != not_in_list && expr_type[i] == expr_type[new_parents[i]])
    -- Make the parent of each of the list end nodes the parent of the entire list.
    -- Do this simply by marking the same_type_as_parent nodes, computing the parents, and computing the parents
    -- of those again.
    let new_parents =
        let end_parents = find_unmarked_parents new_parents same_type_as_parent
        in
            iota n
            -- Careful to not mess up lists that only have the end node here
            |> map2 (\node i -> is_list_end[production.to_i64 node] && same_type_as_parent[i]) new_types
            |> map3
                (\parent end_parent is_end -> if is_end then new_parents[end_parent] else parent)
                new_parents
                end_parents
    -- Invert the lists by, for each of the same_type_as_parents node, scatter to their _original_ parent,
    -- This step relies on that the old list ends have been removed, which is why they are filtered
    -- out during construction of `same_type_as_parent`.
    let new_parents =
        let is = map2 (\parent same_type -> if same_type then parent else -1) parents same_type_as_parent
        in
            scatter
                (copy new_parents)
                (is |> map i64.i32)
                (iota n |> map i32.i64)
    -- Finally, just remove all the list end markers.
    let new_parents =
        new_types
        |> map production.to_i64
        |> map (\node -> is_list_end[node])
        |> remove_nodes new_parents
    in (new_types, new_parents)

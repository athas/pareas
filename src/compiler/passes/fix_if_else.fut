import "util"
import "../../../gen/pareas_grammar"

-- The parser cannot handle else and else-if type constructions, so in the grammar, these are separate
-- statements. This pass checks their structure and fixes the tree up, early-returning false if the
-- structure is not correct.
-- This pass also removes the else nodes themselves, as they don't carry any useful information. Furthermore,
-- if-nodes which gain a new else-child are translated into if_else nodes (which is an extra production
-- provided by the grammar but which is never generated by the parser, and only exists for the result of this
-- pass).
-- Note: This pass works because the else-statement always has a higher index than the if-node and any
-- of its children, and so the final child order of the new if nodes is correct.
let fix_if_else [n] (types: [n]production.t) (parents: [n]i32): (bool, [n]production.t, [n]i32) =
    -- Construct arrays indicating whether a node is if-type (if or elif), and else-type (elif or else).
    let is_if_node = map (\ty -> ty == production_stat_if || ty == production_stat_elif) types
    let is_else_node = map (\ty -> ty == production_stat_elif || ty == production_stat_else) types
    -- First, build a vector for each else-type node its corresponding if-type node (which will
    -- become its new parent). This happens in 2 stages: First, for each if-type node, scatter
    -- its index to its parent. Then, for each else-type node, fetch its grandparent in that
    -- array.
    let new_parents =
        -- Compute the list of indices to scatter to.
        let stat_list_if_children =
            map2 (\if_node parent -> if if_node then parent else -1) is_if_node parents
            -- Scatter node indices to these indices. Remember, if- and else-type nodes should be
            -- child of a stat_list.
            -- Set the if-index of nodes which don't have such a child to -1.
            |> invert
        -- Finally, gather the new parent by first computing the grandparent and then fetching in the
        -- stat_list_if_children array.
        in
            parents
            -- Compute grandparents.
            |> map (\parent -> if parent == -1 then -1 else parents[parent])
            -- Gather to find the new parents.
            |> map (\grand_parent -> if grand_parent == -1 then -1 else stat_list_if_children[grand_parent])
            -- Mask with with else-type nodes
            |> map2 (\else_node if_node -> if else_node then if_node else -1) is_else_node
    -- Check if the source is syntactically valid by for each else-type node checking if it has a new parent
    -- which is not -1.
    let valid =
        new_parents
        |> map (!= -1)
        |> map2 (==) is_else_node
        |> reduce (&&) true
    -- Early return if invalid
    in if !valid then (false, types, parents) else
    -- Construct the real new parents array by filling in the -1 entries with the original parents.
    let new_parents = map2 (\new_parent parent -> if new_parent == -1 then parent else new_parent) new_parents parents
    -- Finally, do a bunch of cleaning up.
    -- First, any if-type node needs to be replaced with an if-else node if applicable.
    -- Do this simply by scattering into the types array.
    let new_types =
        let is =
            map2 (\else_node parent -> if else_node then parent else -1) is_else_node new_parents
            |> map i64.i32
        in
            scatter
                (copy types)
                is
                (replicate n production_stat_if_else)
            -- Also replace any remaining elif node; these need to become if nodes.
            |> map (\ty -> if ty == production_stat_elif then production_stat_if else ty)
    -- Now, remove the stat_list children which originally had an else-type node as child, and also remove
    -- else nodes.
    let new_parents =
        -- First, scatter a mask of stat_list nodes to remove.
        let is =
            map2 (\else_node old_parent -> if else_node then old_parent else -1) is_else_node parents
            |> map i64.i32
        in scatter
            (replicate n false)
            is
            (replicate n true)
        -- Merge that with else nodes.
        |> map2
            (||)
            (map (== production_stat_else) new_types)
        -- And remove all of these.
        -- We expect there only a small amount of subsequent nodes to remove here, as it is limited
        -- by the length of the longest if-elif-else chain.
        |> remove_nodes_lin new_parents
    in (valid, new_types, new_parents)

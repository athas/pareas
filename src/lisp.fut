import "../lib/github.com/diku-dk/segmented/segmented"

type TokenKind =
    #neutral |
    #invalid |
    #lparen |
    #rparen |
    #whitespace |
    #name |
    #int

type Token = (u8, TokenKind)

let is_whitespace (c: u8) : bool = c == ' '
let is_num (c: u8) : bool = '0' <= c && c <= '9'
let is_alpha (c: u8) : bool = ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')
let is_alnum (c: u8) : bool = is_alpha c || is_num c
let is_operator (c: u8) : bool = c == '+' || c == '-' || c == '*' || c == '/'
let is_namechar (c: u8) : bool = is_alnum c || is_operator c || c == '_'

-- Determine whether a token split should be inserted between `lhs` and `rhs`
let should_split (lhs: u8) (rhs: u8) : bool =
    ! ((is_namechar lhs && is_namechar rhs) ||
        (is_whitespace lhs && is_whitespace rhs))

let map_pairs_windowed [n] 't 'u (f: t -> t -> u) (initial: u) (ts: [n]t) : *[n]u =
    let m = n - 1
    let us = map2 f (ts[1 : n] :> [m]t) (ts[0 : n - 1] :> [m]t)
    in ([initial] ++ us) :> [n]u

let tokenize [n] (input: [n]u8): *[]Token =
    let flags = map_pairs_windowed should_split true input
    let initial_kinds: *[n]TokenKind = copy input |> map
        (\c ->
            if c == '(' then #lparen
            else if c == ')' then #rparen
            else if is_whitespace c then #whitespace
            else if is_num c then #int
            else if is_namechar c then #name
            else #invalid)
    let op (lhs: TokenKind) (rhs: TokenKind) : TokenKind =
        match (lhs, rhs)
        case (#neutral, _) -> rhs
        case (_, #neutral) -> lhs
        case (#int, #int) -> #int
        case (#name, #int) -> #name
        case (#int, #name) -> #name
        case (#name, #name) -> #name
        case _ -> #invalid
    let kinds = segmented_reduce op #neutral flags initial_kinds
    let lengths = segmented_reduce (+) 0 flags (replicate n 1)
    let m = length kinds
    in zip (lengths :> [m]u8) (kinds :> [m]TokenKind)

-- returns (valid, depths)
let depths [n] (kinds: [n]TokenKind) : (bool, [n]i8) =
    let map_op (x: TokenKind) : i8 =
        match x
        case #lparen -> 1
        case #rparen -> - 1
        case _ -> 0
    let d = kinds |> map map_op |> scan (+) 0 |> rotate (- 1)
    -- In order for the input to be correct, none of the values should be less than 0, and
    -- the first element should be 0.
    in if d[0] != 0 || (reduce (i8.min) 0 d) < 0
        then (false, d)
        else (true, d)

let input = "(+ (- 1 2) (* 3 4))"

entry main =
    -- First tokenize the input
    let (token_lengths, token_kinds) = tokenize input |> unzip
    -- Get the depths of the tokens, including whitespace and closing parens
    let depths = depths token_kinds |> (.1)
    -- filter out whitespace and closing parens
    -- TODO: Also perform filtering and translating to node-type in one step
    in zip3
        token_lengths
        token_kinds
        depths
    |> filter (\tok -> tok.1 != #whitespace && tok.1 != #rparen)
    |> unzip3
    |> (.2)

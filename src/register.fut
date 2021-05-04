import "instr"

type FuncInfo = {
    id: u32,
    start: u32,
    size: u32
}

type SymbolData = {
    register: u8,
    active: bool,
    swapped: bool
}

let EMPTY_SYMBOL_DATA : SymbolData = {
    register = 0u8,
    active = false,
    swapped = false
}

let NUM_SYSTEM_REGS = 64i64

let is_system_register (register: i64) =
    register < NUM_SYSTEM_REGS

let needs_float_register (instr: u32) (offset: u32) =
    instr & 0b1111111 == 0b1010011

let find_free_register (instr: u32) (lifetime_mask: u64) (offset: u32) =
    let fixed_registers = lifetime_mask | (0xFFFFFFFFu64 << (if needs_float_register instr offset then 0 else 32))
    in
    u64.ctz (!fixed_registers)

let make_symbol_data (reg: i32) =
    {
        register = u8.i32 reg,
        active = true,
        swapped = false
    }

let get_symbol_data (symbol_registers: []SymbolData) (reg: i64) : SymbolData =
    if is_system_register reg then
        {
            register = u8.i64 reg,
            active = true,
            swapped = false
        }
    else
        symbol_registers[reg - NUM_SYSTEM_REGS]

let lifetime_analyze_valid [n] (instrs: [n]Instr) (symbol_registers: []SymbolData) (instr_offset: u32) (lifetime_mask: u64) =
    let instr = instrs[i64.u32 instr_offset]
    let old_rd_data = get_symbol_data symbol_registers instr.rd
    let old_rs1_data = get_symbol_data symbol_registers instr.rs1
    let old_rs2_data = get_symbol_data symbol_registers instr.rs2
    let rd_register = find_free_register instr.instr lifetime_mask 0
    let new_lifetime_mask = lifetime_mask | (1u64 << u64.i32 rd_register)
    let register_info = [
        (if !(is_system_register instr.rd) then instr.rd - NUM_SYSTEM_REGS else -1, rd_register |> make_symbol_data),
        (if !(is_system_register instr.rs1) then instr.rs1 - NUM_SYSTEM_REGS else -1, old_rs1_data),
        (if !(is_system_register instr.rs2) then instr.rs2 - NUM_SYSTEM_REGS else -1, old_rs2_data)
    ]
    in
    (0i32, new_lifetime_mask, register_info)

let lifetime_analyze [n] (instrs: [n]Instr) (symbol_registers: []SymbolData) (instr_offset: u32) (lifetime_mask: u64) =
    let register_info = [(-1i64, EMPTY_SYMBOL_DATA), (-1i64, EMPTY_SYMBOL_DATA), (-1i64, EMPTY_SYMBOL_DATA)] in
    if instr_offset == 0xFFFFFFFF then
        (0i32, lifetime_mask, register_info)
    else
        lifetime_analyze_valid instrs symbol_registers instr_offset lifetime_mask

let current_func_offset(i: i64) (f: FuncInfo) =
    if i >= i64.u32 f.size then
        0xFFFFFFFFu32
    else
        u32.i64 i + f.start 

let register_alloc [n] [m] (instrs: [n]Instr) (functions: [m]FuncInfo) =
    let max_func_size = functions |> map (.size) |> u32.maximum |> i64.u32
    let instr_offsets_init = replicate n 0i32
    let lifetime_masks_init = replicate m 0x0000000000000001u64
    let symbol_registers_init = replicate n EMPTY_SYMBOL_DATA

    let results = loop (instr_offsets, lifetime_masks, symbol_registers) = (instr_offsets_init, lifetime_masks_init, symbol_registers_init) for i < max_func_size do
        let old_offsets = map (current_func_offset i) functions 
        let (new_offsets, lifetime_masks, updated_symbols) = map2 (lifetime_analyze instrs symbol_registers) old_offsets lifetime_masks |> unzip3
        let (symbol_offsets, symbol_data) = updated_symbols |> flatten |> unzip2
        in
        (
            scatter instr_offsets (old_offsets |> map i64.u32) new_offsets,
            lifetime_masks,
            scatter symbol_registers symbol_offsets symbol_data
        )

    in

    (
        results.0,
        results.1,
        results.2 |> map (\i -> i.register)
    )
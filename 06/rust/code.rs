use crate::instruction::*;
use crate::parser::ParseResult;

pub fn generate_code(parse_result: ParseResult) -> String {
    parse_result
        .commands
        .into_iter()
        .filter_map(|com| generate(com.inst))
        .collect()
}

fn generate(inst: Instruction) -> Option<String> {
    // println!("generate: {:?}", inst);
    Some(
        match inst {
            Instruction::AInstruction {
                address: AInstAddress::Address(address),
            } => format!("0{:015b}", address),
            Instruction::CInstruction { comp, dest, jump } => format!(
                "111{comp}{dest:03b}{jump:03b}",
                comp = comp.fmt_binary(),
                dest = dest,
                jump = jump
            ),
            _ => {
                println!("Unknown code instruction: {:?}", inst);
                return None;
            }
        } + "\n",
    )
}

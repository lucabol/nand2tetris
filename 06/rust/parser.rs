use crate::instruction::*;

#[derive(Debug)]
pub struct Command {
    pub inst: Instruction,
    pub raw: String,
}

#[derive(Debug)]
pub struct ParseResult {
    pub commands: Vec<Command>,
    pub inst_counter: u16,
}

pub struct Parser<'a> {
    input: &'a str,
    var_addr_next: u16,
}

impl<'a> Parser<'a> {
    pub fn new(input: &'a str) -> Self {
        Parser {
            input,
            var_addr_next: 16,
        }
    }

    pub fn parse(mut self) -> ParseResult {
        let mut symbols = self.build_symbols();
        self.parse_input(&mut symbols, true); // Build symbols table
                                              // println!("symbols: {:?}", symbols);

        // println!("symbols final: {:?}", symbols);
        self.parse_input(&mut symbols, false)
    }

    pub fn parse_input(&mut self, symbols: &mut SymbolTable, first_pass: bool) -> ParseResult {
        let mut inst_counter = 0u16;
        let mut commands = Vec::<Command>::new();
        for line in self.input.lines() {
            if let Some(command) = Self::parse_line(line, symbols) {
                match &command.inst {
                    Instruction::LInstruction { label } => {
                        // dbg!(symbols.get(label));
                        symbols.insert(label.into(), inst_counter);
                    }
                    Instruction::AInstruction {
                        address: AInstAddress::Label(label),
                    } if !first_pass => {
                        let addr = self.var_addr_next;
                        symbols.insert(label.into(), addr);
                        inst_counter += 1;
                        commands.push(Command {
                            inst: Instruction::AInstruction {
                                address: AInstAddress::Address(addr),
                            },
                            ..command
                        });
                        self.var_addr_next += 1;
                    }
                    _ => {
                        inst_counter += 1;
                        commands.push(command);
                    }
                };
            } else {
                // println!("Unable to parse line: {}", line);
            };
        }
        ParseResult {
            commands,
            inst_counter,
        }
    }

    fn parse_line(line: &str, symbols: &mut SymbolTable) -> Option<Command> {
        let stmt = line.split("//").next().unwrap_or_default().trim();
        // println!("parse_line: {:?}, {:?}", &line, &stmt);
        match stmt {
            "" => None,
            x if x.starts_with('@') => {
                let label: String = x.chars().skip(1).collect();
                let address = str::parse::<u16>(&label).ok();
                Some(Command {
                    raw: x.into(),
                    inst: match address {
                        Some(address) => Instruction::AInstruction {
                            address: AInstAddress::Address(address),
                        },
                        _ => {
                            let addr = symbols.get(&label);
                            Instruction::AInstruction {
                                address: match addr {
                                    Some(addr) => AInstAddress::Address(*addr),
                                    _ => AInstAddress::Label(label),
                                },
                            }
                        }
                    },
                })
            }
            x if x.starts_with('(') => {
                let label = x[1..x.len() - 1].into();
                Some(Command {
                    raw: x.into(),
                    inst: Instruction::LInstruction { label },
                })
            }
            x => {
                let dest = x.split('=').next();
                let jump = x.split(';').nth(1);
                let comp = x
                    .replace(&format!("{}{}", dest.unwrap_or_default(), "="), "")
                    .replace(&format!("{}{}", ";", jump.unwrap_or_default()), "");
                Some(Command {
                    raw: x.into(),
                    inst: Instruction::CInstruction {
                        comp: Self::parse_comp(comp),
                        dest: Self::parse_dest(dest),
                        jump: Self::parse_jump(jump),
                    },
                })
            }
        }
    }

    fn parse_comp(s: String) -> Comp {
        make_comp(s)
    }

    fn parse_dest(s: Option<&str>) -> Dest {
        match s {
            Some("M") => Dest::M,
            Some("D") => Dest::D,
            Some("MD") => Dest::MD,
            Some("A") => Dest::A,
            Some("AM") => Dest::AM,
            Some("AD") => Dest::AD,
            Some("AMD") => Dest::AMD,
            _ => Dest::Null,
        }
    }

    fn parse_jump(s: Option<&str>) -> Jump {
        match s {
            Some("JGT") => Jump::JGT,
            Some("JEQ") => Jump::JEQ,
            Some("JGE") => Jump::JGE,
            Some("JLT") => Jump::JLT,
            Some("JNE") => Jump::JNE,
            Some("JLE") => Jump::JLE,
            Some("JMP") => Jump::JMP,
            _ => Jump::Null,
        }
    }

    pub fn build_symbols(&self) -> SymbolTable {
        default_symbols()
    }
}

pub fn parse(content: String) -> ParseResult {
    let parser = Parser::new(&content);
    parser.parse()
}

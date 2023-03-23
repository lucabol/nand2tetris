pub mod code;
pub mod config;
pub mod instruction;
pub mod parser;

use std::error::Error;
use std::fs;
use std::path;

use config::Config;

pub fn run(config: Config) -> Result<(), Box<dyn Error>> {
    let contents = fs::read_to_string(&config.filename)?;
    let parse_result = parser::parse(contents);
    let mut target_filename = path::PathBuf::from(&config.filename);
    target_filename.set_extension("hack");
    fs::write(target_filename, code::generate_code(parse_result))?;
    Ok(())
}

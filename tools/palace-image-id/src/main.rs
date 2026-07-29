use std::{
    env,
    ffi::OsStr,
    fs::File,
    io::{self, Read},
    process,
};

const MAX_RISC0_ELF_BYTES: usize = 16 * 1024 * 1024;

fn read_bounded(mut reader: impl Read) -> Result<Vec<u8>, String> {
    let mut bytes = Vec::new();
    reader
        .by_ref()
        .take((MAX_RISC0_ELF_BYTES + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(|error| error.to_string())?;
    if bytes.len() > MAX_RISC0_ELF_BYTES {
        return Err(format!("input exceeds {MAX_RISC0_ELF_BYTES} bytes"));
    }
    Ok(bytes)
}

fn read_input(path: &OsStr) -> Result<Vec<u8>, String> {
    if path == OsStr::new("-") {
        return read_bounded(io::stdin().lock());
    }
    let file = File::open(path).map_err(|error| error.to_string())?;
    read_bounded(file)
}

fn main() {
    let mut arguments = env::args_os();
    let _program = arguments.next();
    let Some(path) = arguments.next() else {
        eprintln!("usage: palace-image-id <risc0-elf|->");
        process::exit(2);
    };
    if arguments.next().is_some() {
        eprintln!("usage: palace-image-id <risc0-elf|->");
        process::exit(2);
    }

    let bytes = match read_input(&path) {
        Ok(bytes) => bytes,
        Err(error) => {
            eprintln!("could not read RISC0 ELF: {error}");
            process::exit(1);
        }
    };
    let image_id = match risc0_binfmt::compute_image_id(&bytes) {
        Ok(image_id) => image_id,
        Err(error) => {
            eprintln!("could not compute RISC0 image ID: {error}");
            process::exit(1);
        }
    };
    println!("{image_id}");
}

#[cfg(test)]
mod tests {
    use std::io::Cursor;

    use super::{read_bounded, MAX_RISC0_ELF_BYTES};

    #[test]
    fn rejects_truncated_and_non_risc0_inputs() {
        assert!(risc0_binfmt::compute_image_id(&[]).is_err());
        assert!(risc0_binfmt::compute_image_id(b"not-a-risc0-executable",).is_err());
    }

    #[test]
    fn rejects_input_above_bound() {
        let oversized = vec![0_u8; MAX_RISC0_ELF_BYTES + 1];
        assert!(read_bounded(Cursor::new(oversized)).is_err());
    }
}

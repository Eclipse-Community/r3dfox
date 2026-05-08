use crate::Error;
use core::{mem::MaybeUninit};

extern "system" {
    #[link_name = "SystemFunction036"]
    fn RtlGenRandom(RandomBuffer: *mut u8, RandomBufferLength: u32) -> u8;
}

pub fn getrandom_inner(dest: &mut [MaybeUninit<u8>]) -> Result<(), Error> {
    // Prevent overflow of u32
    for chunk in dest.chunks_mut(u32::max_value() as usize) {
        let ret = unsafe { RtlGenRandom(chunk.as_mut_ptr() as *mut u8, chunk.len() as u32) };
        if ret == 0 {
            return Err(Error::WINDOWS_RTL_GEN_RANDOM);
        }
    }
    Ok(())
}

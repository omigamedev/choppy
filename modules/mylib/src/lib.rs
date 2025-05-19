#[cxx::bridge(namespace = "lib")]
mod bridge {
    extern "Rust" {
        fn print(slice: &[u64]);
        fn prints(string: &str);
        fn foo(a: i32, b: i32) -> i32;
    }
}

fn print(slice: &[u64]) {
    println!("Hello cxxbridge from lib.rs! {:?}", slice);
}

fn prints(string: &str) {
    println!("Hello cxxbridge from lib.rs! {}", string);
}

#[unsafe(no_mangle)]
pub extern "C" fn foo(a: i32, b: i32) -> i32 {
    a + b
}

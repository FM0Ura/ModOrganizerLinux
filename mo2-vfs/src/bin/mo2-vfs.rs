fn main() {
    println!(
        "{{\"event\":\"hello\",\"version\":\"{}\"}}",
        env!("CARGO_PKG_VERSION")
    );
}

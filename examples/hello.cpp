// C++ Hello World for GC AOS
// Compile with: aarch64-none-elf-g++ -nostdlib -ffreestanding hello.cpp -o
// hello

extern "C" void puts(const char *str);

extern "C" int main() {
  puts("Hello from C++!");
  puts("This is a C++ program running on GC AOS, the world's most professional Operating System.");
  return 0;
}

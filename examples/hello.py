# Hello World in Python for GC AOS
# Run with: run hello.py


def greet(name):
    return f"Hello, {name}!"


def main():
    print("=" * 40)
    print("  Welcome to GC AOS Python Demo")
    print("=" * 40)

    message = greet("GC AOS User")
    print(message)

    # Simple calculation demo
    numbers = [1, 2, 3, 4, 5]
    total = sum(numbers)
    print(f"Sum of {numbers} = {total}")

    print("\nPython is working on GC AOS!")


if __name__ == "__main__":
    main()

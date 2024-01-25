# M-Shell (msh)

M-Shell (msh) is a simple shell implementation in C that provides basic shell functionality, including executing commands, built-in commands, and handling process execution.

## Getting Started

### Prerequisites

To compile and run the M-Shell program, you'll need a C compiler (such as GCC or Clang) and basic build tools.

### Building the Program

1. Clone this repository:
~~~
git clone https://github.com/your-username/msh.git
cd msh
~~~

2. Build the program:
~~~
make
~~~

This will compile the M-Shell program and generate the executable `msh` in the project directory.

### Running the Shell

1. Make sure you have the program built.

2. Run the M-Shell:
~~~
./msh
~~~
This will start the M-Shell, allowing you to enter commands and interact with the shell.

## Usage

The M-Shell provides a basic command-line interface similar to other shell programs. It supports running external programs and executing built-in commands.

### Built-in Commands

- `cd`: Change the current working directory.
- `help`: Display information about built-in commands.
- `exit`: Exit the shell.
- `kill`: Send a signal to a process.
- `whoami`: Display the current user's username.

## Contributing

Contributions are welcome! If you find any issues, want to improve existing features, or add new features, feel free to open a pull request.

## License

This project is licensed under the [MIT License](LICENSE) - see the LICENSE file for details.

## Acknowledgements

- [lsh](https://github.com/brenns10/lsh): A simple shell implementation in C, which served as a reference for this project.

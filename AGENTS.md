# Agent Coding Guidelines for Blackcoin More

This document provides essential information for agentic coding agents working in the Blackcoin More repository.

## Build System

**Build System**: Autotools (GNU Autoconf/Automake)
**Configuration**: `./configure`
**Build**: `make -j$(nproc)`

### Essential Build Commands
```bash
# Initial setup
./autogen.sh                    # Generate configure script
./configure --disable-tests    # Minimal build
./configure --enable-tests     # Build with tests
make                           # Build all targets
make install                   # Install to system

# Specific targets
make blackmored                # Build daemon only
make blackmore-cli             # Build CLI only  
make blackmore-qt              # Build GUI only
make blackmore-wallet          # Build wallet tool
make test_blackmore            # Build test binary
make check                     # Run all tests
make dist                      # Create source distribution
```

### Dependencies
- **Required**: libssl-dev, libevent-dev, libboost-dev, libsodium-dev
- **Wallet**: libdb-dev, libsqlite3-dev
- **GUI**: qtbase5-dev, qttools5-dev
- **Tests**: python3, bash, docker, gdb

## Testing

### Running Tests
```bash
# All tests
make check

# C++ unit tests only
src/test/test_blackmore

# Python functional tests
test/functional/test_runner.py

# Single functional test
test/functional/test_runner.py wallet_upgradewallet.py

# Single test with verbose output
test/functional/test_runner.py --verbose wallet_upgradewallet.py

# Run specific test category
test/functional/test_runner.py --cached wallet_*.py

# Debug failing test
test/functional/test_runner.py --verbose --failfast <test_name>

# CI test environment locally  
env -i HOME="$HOME" PATH="$PATH" USER="$USER" bash -c 'FILE_ENV="./ci/test/00_setup_env_native_asan.sh" ./ci/test_run_all.sh'
```

### Test Categories
- **Unit Tests**: C++ tests in `src/test/`
- **Functional Tests**: Python tests in `test/functional/`
- **Fuzzing**: `./src/test/fuzz/fuzz_bitcoin`
- **Benchmarks**: `./src/bench/bench_bitcoin`
- **Linting**: `./ci/lint/06_script.sh`

## Code Style

### C++ Guidelines
**Style**: Bitcoin Core style (clang-format enforced)
**Standard**: C++17
**Formatter**: `clang-format` (config in `src/.clang-format`)

**Key Conventions**:
- **Files**: Use `.cpp` for implementation, `.h` for headers
- **Imports**: Group by standard library, Bitcoin Core, third-party, local
- **Naming**: 
  - Classes: `PascalCase` (`CBlock`, `CWallet`)
  - Functions: `PascalCase` (`GetBlockHash()`, `CreateTransaction()`)
  - Variables: `snake_case` (`block_height`, `transaction_fee`)
  - Constants: `UPPER_CASE` (`MAX_BLOCK_SIZE`)
- **Headers**: Order: standard library → project headers → third-party
- **Memory**: Use RAII, smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- **Error Handling**: Use exceptions for exceptional cases, `std::optional`/`Result` for expected errors

### Python Guidelines  
**Style**: PEP 8
**Version**: Python 3.6+
**Testing**: Use `unittest` framework

**Key Conventions**:
- **Files**: Use lowercase with underscores (`test_runner.py`)
- **Naming**: 
  - Classes: `PascalCase`
  - Functions/variables: `snake_case`
  - Constants: `UPPER_CASE`
- **Imports**: Group by standard library, third-party, local

### Documentation
- **C++**: Doxygen-style comments for public APIs
- **Python**: Docstrings for all public functions/classes
- **Commit Messages**: Follow conventional commits format

## Linting and Quality

### Lint Commands
```bash
# Run all linting (main CI lint check)
./ci/lint/06_script.sh

# Individual lint checks
test/lint/all-lint.py          # Code style, formatting
test/lint/check-doc.py         # Documentation consistency
test/lint/commit-script-check.sh # Commit message validation
```

### Code Quality Tools
- **Clang Format**: Code formatting (config: `src/.clang-format`)
- **Clang Tidy**: Static analysis (config: `src/.clang-tidy`)
- **Cppcheck**: Static analysis  
- **Sanitizers**: ASan, UBSan, TSan (in CI)
- **EditorConfig**: `.editorconfig` for basic formatting

## Project Structure

### Key Directories
- **src/**: C++ source code
- **src/test/**: C++ unit tests
- **src/bench/**: Performance benchmarks
- **src/qt/**: Qt GUI application
- **test/functional/**: Python integration tests
- **ci/**: Continuous integration scripts
- **doc/**: Documentation
- **contrib/**: External contributions and tools

### Core Components
- **Consensus**: `src/consensus/` - Protocol validation
- **Validation**: `src/validation.cpp` - Block/transaction validation
- **Mempool**: `src/txmempool.cpp` - Transaction pool
- **P2P**: `src/net/` - Peer-to-peer networking
- **Wallet**: `src/wallet/` - Wallet functionality
- **RPC**: `src/rpc/` - Remote procedure calls

## Development Workflow

### Testing Changes
```bash
# After any change
make check                    # Run all tests
./ci/lint/06_script.sh       # Run linting
make clean && make           # Clean rebuild to catch issues

# For specific areas
make -j$(nproc) check TESTS="test_blackmore"  # Unit tests only
make src/test/test_blackmore # Build test binary only
```

### Build Debug Versions
```bash
# Debug build
./configure CFLAGS="-g -O0" --enable-debug
make

# Sanitizer builds
./configure --with-sanitizers=address,undefined
make

# Profiling build  
./configure --enable-gprof
make
```

## Repository Rules

### Don't Modify
- **src/secp256k1/**: External library (subtree)
- **src/leveldb/**: External library (subtree) 
- **src/minisketch/**: External library (subtree)
- **src/crc32c/**: External library (subtree)
- **depends/**: Build dependencies (auto-generated)

### Configuration Files
- **src/.clang-format**: C++ formatting rules
- **src/.clang-tidy**: Static analysis rules
- **.editorconfig**: Basic formatting rules
- **.gitignore**: Git ignore patterns
- **configure.ac**: Build configuration
- **Makefile.am**: Build rules

## Common Agent Tasks

### Adding New RPC Methods
1. Declare in `src/rpc/` header files
2. Implement in corresponding `.cpp` files
3. Add tests in `test/functional/`
4. Update documentation in `doc/JSON-RPC-interface.md`

### Adding Tests
1. **Unit Tests**: Add to `src/test/` directory
2. **Functional Tests**: Add Python test to `test/functional/`
3. **Benchmark Tests**: Add to `src/bench/`

### Modifying Consensus
1. Check consensus rules in `src/consensus/`
2. Update validation in `src/validation.cpp`
3. Add comprehensive tests
4. Consider network upgrade implications

## Emergency Procedures

### Reverting Changes
```bash
git reset --hard HEAD~1    # Last commit
git stash                 # Stash uncommitted changes
git checkout -- .         # Discard local changes
```

### Build Issues
```bash
make clean && make distclean
./autogen.sh && ./configure && make
```

### Test Failures
```bash
# Run specific failing test with verbose output
test/functional/test_runner.py --verbose --failfast <test_name>

# Debug with gdb
gdb --args src/test/test_blackmore
```

---
*Last Updated: December 29, 2025*